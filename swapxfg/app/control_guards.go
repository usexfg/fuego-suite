package app

import (
	"crypto/subtle"
	"net"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"
)

// Guards for the headless control API.
//
// The headless listener exposes /offer and /cancel, which call walletd's signing
// RPC. That makes every unauthenticated request to this listener a potential
// signature. It therefore gets the same treatment as the suite dashboard plus a
// bearer token, and it fails closed if no token is configured.

const maxControlBody = 1 << 20 // 1 MiB

// controlGuards wraps a handler with Host/Origin validation, bearer-token
// auth, a body cap, and per-client rate limiting.
type controlGuards struct {
	token   string
	limiter *controlLimiter
}

func (g *controlGuards) wrap(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		// 1. Host allowlist. Without this, DNS rebinding lets a remote page
		//    address this listener as if it were loopback.
		host := r.Host
		if h, _, err := net.SplitHostPort(host); err == nil {
			host = h
		}
		if host != "127.0.0.1" && host != "localhost" && host != "::1" {
			http.Error(w, "forbidden host", http.StatusForbidden)
			return
		}

		// 2. Origin must match Host. Rejects cross-origin browser requests.
		if origin := r.Header.Get("Origin"); origin != "" {
			ou, err := url.Parse(origin)
			if err != nil || ou.Scheme != "http" || !strings.EqualFold(ou.Host, r.Host) {
				http.Error(w, "bad origin", http.StatusForbidden)
				return
			}
		}

		// 3. Bearer token. Fail closed: an empty configured token denies all.
		presented := r.Header.Get("X-Control-Token")
		if presented == "" {
			if auth := r.Header.Get("Authorization"); strings.HasPrefix(auth, "Bearer ") {
				presented = strings.TrimPrefix(auth, "Bearer ")
			}
		}
		if g.token == "" ||
			subtle.ConstantTimeCompare([]byte(presented), []byte(g.token)) != 1 {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}

		// 4. Rate limit per client address.
		ip, _, err := net.SplitHostPort(r.RemoteAddr)
		if err != nil {
			ip = r.RemoteAddr
		}
		if !g.limiter.allow(ip) {
			http.Error(w, "rate limited", http.StatusTooManyRequests)
			return
		}

		// 5. Require application/json on bodies. This is what stops CSRF from a
		//    web page: a cross-origin POST with a simple content type (e.g.
		//    text/plain) is sent without a preflight, whereas
		//    application/json forces one that this server will refuse.
		if r.Method == http.MethodPost || r.Method == http.MethodPut {
			ct := r.Header.Get("Content-Type")
			if i := strings.IndexByte(ct, ';'); i >= 0 {
				ct = ct[:i]
			}
			if !strings.EqualFold(strings.TrimSpace(ct), "application/json") {
				http.Error(w, "Content-Type: application/json required", http.StatusUnsupportedMediaType)
				return
			}
		}

		// 6. Bound the body.
		r.Body = http.MaxBytesReader(w, r.Body, maxControlBody)

		next(w, r)
	}
}

// controlLimiter is a simple per-address interval throttle.
type controlLimiter struct {
	mu       sync.Mutex
	buckets  map[string]time.Time
	interval time.Duration
}

func newControlLimiter(interval time.Duration) *controlLimiter {
	return &controlLimiter{
		buckets:  make(map[string]time.Time),
		interval: interval,
	}
}

func (l *controlLimiter) allow(key string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	now := time.Now()
	if last, ok := l.buckets[key]; ok && now.Sub(last) < l.interval {
		return false
	}
	l.buckets[key] = now
	return true
}