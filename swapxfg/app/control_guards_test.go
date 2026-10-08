package app

import (
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func guarded(tok string, inner http.HandlerFunc) http.HandlerFunc {
	g := &controlGuards{token: tok, limiter: newControlLimiter(time.Nanosecond)}
	return g.wrap(inner)
}

func okHandler(w http.ResponseWriter, r *http.Request) { w.WriteHeader(http.StatusOK) }

// A POST that a hostile page could originate: simple content type, no token.
func hostilePost() *http.Request {
	r := httptest.NewRequest(http.MethodPost, "/offer", strings.NewReader(`{"pair":"SOL"}`))
	r.Host = "127.0.0.1:18190"
	r.Header.Set("Content-Type", "text/plain") // simple type -> no preflight
	r.RemoteAddr = "203.0.113.9:1234"
	return r
}

func TestControlAPIGuards(t *testing.T) {
	const tok = "s3cret"

	t.Run("hostile simple-request POST is rejected without a token", func(t *testing.T) {
		rr := httptest.NewRecorder()
		guarded(tok, okHandler)(rr, hostilePost())
		if rr.Code != http.StatusUnauthorized {
			t.Fatalf("got %d, want 401 (CSRF path open)", rr.Code)
		}
	})

	t.Run("with a token but a simple content type, still rejected", func(t *testing.T) {
		// Proves the Content-Type rule independently of the token.
		r := hostilePost()
		r.Header.Set("X-Control-Token", tok)
		rr := httptest.NewRecorder()
		guarded(tok, okHandler)(rr, r)
		if rr.Code != http.StatusUnsupportedMediaType {
			t.Fatalf("got %d, want 415", rr.Code)
		}
	})

	t.Run("non-loopback Host is rejected (DNS rebinding)", func(t *testing.T) {
		r := hostilePost()
		r.Host = "attacker.example"
		r.Header.Set("X-Control-Token", tok)
		r.Header.Set("Content-Type", "application/json")
		rr := httptest.NewRecorder()
		guarded(tok, okHandler)(rr, r)
		if rr.Code != http.StatusForbidden {
			t.Fatalf("got %d, want 403", rr.Code)
		}
	})

	t.Run("cross-origin is rejected", func(t *testing.T) {
		r := hostilePost()
		r.Header.Set("X-Control-Token", tok)
		r.Header.Set("Content-Type", "application/json")
		r.Header.Set("Origin", "http://evil.example")
		rr := httptest.NewRecorder()
		guarded(tok, okHandler)(rr, r)
		if rr.Code != http.StatusForbidden {
			t.Fatalf("got %d, want 403", rr.Code)
		}
	})

	t.Run("wrong token is rejected", func(t *testing.T) {
		r := hostilePost()
		r.Header.Set("X-Control-Token", "wrong")
		r.Header.Set("Content-Type", "application/json")
		rr := httptest.NewRecorder()
		guarded(tok, okHandler)(rr, r)
		if rr.Code != http.StatusUnauthorized {
			t.Fatalf("got %d, want 401", rr.Code)
		}
	})

	t.Run("empty configured token fails closed", func(t *testing.T) {
		r := hostilePost()
		r.Header.Set("Content-Type", "application/json")
		rr := httptest.NewRecorder()
		guarded("", okHandler)(rr, r)
		if rr.Code != http.StatusUnauthorized {
			t.Fatalf("got %d, want 401 (must fail closed)", rr.Code)
		}
	})

	t.Run("correct token and content type is allowed", func(t *testing.T) {
		r := hostilePost()
		r.Header.Set("X-Control-Token", tok)
		r.Header.Set("Content-Type", "application/json")
		rr := httptest.NewRecorder()
		guarded(tok, okHandler)(rr, r)
		if rr.Code != http.StatusOK {
			t.Fatalf("got %d, want 200 (legit caller blocked)", rr.Code)
		}
	})

	t.Run("bearer authorization header is accepted", func(t *testing.T) {
		r := hostilePost()
		r.Header.Set("Authorization", "Bearer "+tok)
		r.Header.Set("Content-Type", "application/json")
		rr := httptest.NewRecorder()
		guarded(tok, okHandler)(rr, r)
		if rr.Code != http.StatusOK {
			t.Fatalf("got %d, want 200", rr.Code)
		}
	})

	t.Run("rate limit trips on burst", func(t *testing.T) {
		g := &controlGuards{token: tok, limiter: newControlLimiter(time.Hour)}
		h := g.wrap(okHandler)
		newReq := func() *http.Request {
			r := httptest.NewRequest(http.MethodPost, "/offer", strings.NewReader("{}"))
			r.Host = "127.0.0.1:18190"
			r.RemoteAddr = "198.51.100.7:9999"
			r.Header.Set("X-Control-Token", tok)
			r.Header.Set("Content-Type", "application/json")
			return r
		}
		first := httptest.NewRecorder()
		h(first, newReq())
		if first.Code != http.StatusOK {
			t.Fatalf("first request should pass, got %d", first.Code)
		}
		rr := httptest.NewRecorder()
		h(rr, newReq())
		if rr.Code != http.StatusTooManyRequests {
			t.Fatalf("got %d, want 429", rr.Code)
		}
	})

	t.Run("oversized body is refused by the cap", func(t *testing.T) {
		r := httptest.NewRequest(http.MethodPost, "/offer", strings.NewReader(strings.Repeat("a", maxControlBody+64)))
		r.Host = "127.0.0.1:18190"
		r.Header.Set("X-Control-Token", tok)
		r.Header.Set("Content-Type", "application/json")
		rr := httptest.NewRecorder()
		guarded(tok, okHandler)(rr, r)
		// MaxBytesReader surfaces an error once the limit is exceeded rather
		// than truncating, so the handler's decode must fail.
		if _, err := io.ReadAll(r.Body); err == nil {
			t.Fatal("expected the oversized body to error, got nil")
		}
	})
}
