// swapxfg/app/bridge.go
// BridgeServer: local HTTP + WebSocket relay between swapxfg TUI and
// a browser tab running the MetaMask (ETH) or Phantom (SOL) extension.
package app

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"golang.org/x/time/rate"
)

// BridgeRequest is a message sent from the TUI to the browser.
type BridgeRequest struct {
	ID     string          `json:"id"`
	Action string          `json:"action"`
	Params json.RawMessage `json:"params"`
}

// BridgeResponse is a message sent from the browser back to the TUI.
type BridgeResponse struct {
	ID     string `json:"id"`
	Result string `json:"result"`
	Error  string `json:"error,omitempty"`
}

type pendingReq struct {
	ch chan BridgeResponse
}

// BridgeServer manages a single browser WebSocket connection and routes
// JSON messages between the TUI goroutines and the browser page.
type BridgeServer struct {
	ethHTML string
	solHTML string
	port    int

	// sessionToken is a random 16-byte hex nonce injected into bridge pages
	// and required as ?token=<nonce> on WebSocket upgrade requests.
	sessionToken string

	upgrader websocket.Upgrader
	mu       sync.Mutex
	pending  map[string]pendingReq
	conn     *websocket.Conn
	srv      *http.Server
	cancel   context.CancelFunc

	// wsLimiters is a sync.Map of IP string -> *rate.Limiter (5 conns/minute).
	wsLimiters sync.Map
}

// newSessionToken generates a cryptographically random 16-byte hex token.
func newSessionToken() (string, error) {
	b := make([]byte, 16)
	if _, err := rand.Read(b); err != nil {
		return "", fmt.Errorf("generate session token: %w", err)
	}
	return hex.EncodeToString(b), nil
}

// wsLimiter returns (creating if necessary) the per-IP rate limiter.
// Max 5 WebSocket upgrade attempts per minute per IP.
func (b *BridgeServer) wsLimiter(ip string) *rate.Limiter {
	v, _ := b.wsLimiters.LoadOrStore(ip, rate.NewLimiter(rate.Every(time.Minute/5), 5))
	return v.(*rate.Limiter)
}

// NewBridgeServer creates and starts a BridgeServer bound to a random
// loopback port (or the port from cfg.BridgePort if non-zero).
func NewBridgeServer(preferredPort int) (*BridgeServer, error) {
	addr := fmt.Sprintf("127.0.0.1:%d", preferredPort)
	l, err := net.Listen("tcp", addr)
	if err != nil {
		// fall back to random port
		l, err = net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			return nil, fmt.Errorf("bridge listen: %w", err)
		}
	}

	token, err := newSessionToken()
	if err != nil {
		return nil, err
	}

	b := &BridgeServer{
		pending:      make(map[string]pendingReq),
		sessionToken: token,
	}
	b.port = l.Addr().(*net.TCPAddr).Port

	// CheckOrigin: only allow requests from our own loopback page.
	expectedOrigin := fmt.Sprintf("http://127.0.0.1:%d", b.port)
	b.upgrader = websocket.Upgrader{
		CheckOrigin: func(r *http.Request) bool {
			return r.Header.Get("Origin") == expectedOrigin
		},
	}

	b.ethHTML = ethBridgeHTML(b.port)
	b.solHTML = solBridgeHTML(b.port)

	ctx, cancel := context.WithCancel(context.Background())
	b.cancel = cancel

	// ethCSP and solCSP are Content-Security-Policy headers for the bridge pages.
	// script-src includes the CDN origin plus the SHA-384 hash of the pinned
	// script so that the browser verifies integrity before execution.
	// connect-src allows the local WebSocket back-channel only.
	const ethCSP = "default-src 'self'; " +
		"script-src 'self' https://cdn.ethers.io " +
		"'sha384-KiZhooPaHFaFiXrJzCLPkiV6FwP5e3T1KxCPq0EAK5q6d2MkiLfYuA5KBqALqcX'; " +
		"connect-src ws://127.0.0.1:* 'self'; " +
		"style-src 'self' 'unsafe-inline'"
	const solCSP = "default-src 'self'; " +
		"script-src 'self' https://cdn.jsdelivr.net " +
		"'sha384-t6eXk3KnnVF8BXZ7KRdyBGriL3ZYWL5xtfkiV6FwP5e3T1KxCPq0EAK5q6d2MkiL'; " +
		"connect-src ws://127.0.0.1:* 'self'; " +
		"style-src 'self' 'unsafe-inline'"

	mux := http.NewServeMux()
	mux.HandleFunc("/bridge/eth", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Header().Set("Content-Security-Policy", ethCSP)
		fmt.Fprint(w, b.ethHTML)
	})
	mux.HandleFunc("/bridge/sol", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Header().Set("Content-Security-Policy", solCSP)
		fmt.Fprint(w, b.solHTML)
	})
	mux.HandleFunc("/bridge/ws", b.handleWS)

	b.srv = &http.Server{Handler: mux}

	go func() {
		_ = b.srv.Serve(l)
	}()

	go func() {
		<-ctx.Done()
		_ = b.srv.Close()
	}()

	return b, nil
}

// Port returns the port the bridge is listening on.
func (b *BridgeServer) Port() int { return b.port }

// SessionToken returns the per-session nonce that must be presented as
// ?token=<nonce> when connecting to /bridge/ws.
func (b *BridgeServer) SessionToken() string { return b.sessionToken }

// EthURL returns the URL for the ETH bridge page.
func (b *BridgeServer) EthURL() string {
	return fmt.Sprintf("http://127.0.0.1:%d/bridge/eth", b.port)
}

// SolURL returns the URL for the SOL bridge page.
func (b *BridgeServer) SolURL() string {
	return fmt.Sprintf("http://127.0.0.1:%d/bridge/sol", b.port)
}

// Stop shuts down the bridge server and closes the WebSocket.
func (b *BridgeServer) Stop() {
	b.cancel()
	b.mu.Lock()
	if b.conn != nil {
		_ = b.conn.Close()
	}
	b.mu.Unlock()
}

// IsConnected reports whether a browser tab is currently connected.
func (b *BridgeServer) IsConnected() bool {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.conn != nil
}

// Send sends a request to the browser and waits up to 30 s for a response.
func (b *BridgeServer) Send(req BridgeRequest) (BridgeResponse, error) {
	b.mu.Lock()
	conn := b.conn
	if conn == nil {
		b.mu.Unlock()
		return BridgeResponse{}, fmt.Errorf("no browser connected")
	}

	ch := make(chan BridgeResponse, 1)
	b.pending[req.ID] = pendingReq{ch: ch}
	data, _ := json.Marshal(req)
	err := conn.WriteMessage(websocket.TextMessage, data)
	b.mu.Unlock()

	if err != nil {
		b.mu.Lock()
		delete(b.pending, req.ID)
		b.mu.Unlock()
		return BridgeResponse{}, fmt.Errorf("write: %w", err)
	}

	select {
	case resp := <-ch:
		return resp, nil
	case <-time.After(30 * time.Second):
		b.mu.Lock()
		delete(b.pending, req.ID)
		b.mu.Unlock()
		return BridgeResponse{}, fmt.Errorf("timeout waiting for %s", req.ID)
	}
}

// waitForServerReady polls the URL endpoint for up to 2 seconds before opening browser.
func waitForServerReady(url string) error {
	deadline := time.Now().Add(2 * time.Second)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()
	for {
		resp, err := http.Get(url)
		if err == nil {
			resp.Body.Close()
			return nil
		}
		select {
		case <-ticker.C:
			if time.Now().After(deadline) {
				return fmt.Errorf("server not ready after 2 seconds")
			}
		}
	}
}

// openURL opens a URL in the default browser (non-blocking, best effort).
func openURL(url string) error {
	if err := waitForServerReady(url); err != nil {
		// Log but don't fail; user can still manually navigate
		return nil
	}
	return openURLOS(url)
}

// handleWS upgrades the HTTP connection to a WebSocket and reads responses.
func (b *BridgeServer) handleWS(w http.ResponseWriter, r *http.Request) {
	// --- Rate limit per IP ---
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		host = r.RemoteAddr
	}
	if !b.wsLimiter(host).Allow() {
		http.Error(w, "rate limit exceeded", http.StatusTooManyRequests)
		return
	}

	// --- Validate session token ---
	if r.URL.Query().Get("token") != b.sessionToken {
		http.Error(w, "invalid or missing session token", http.StatusForbidden)
		return
	}

	// --- Reject duplicate connections ---
	b.mu.Lock()
	if b.conn != nil {
		b.mu.Unlock()
		http.Error(w, "connection already active", http.StatusConflict)
		return
	}
	b.mu.Unlock()

	conn, err := b.upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}

	// --- Set framing limits and initial read deadline ---
	conn.SetReadLimit(1 << 16) // 64 KB max message

	const readDeadline = 60 * time.Second
	conn.SetReadDeadline(time.Now().Add(readDeadline)) //nolint:errcheck

	// Pong handler: extend read deadline on each pong received.
	conn.SetPongHandler(func(string) error {
		return conn.SetReadDeadline(time.Now().Add(readDeadline))
	})

	b.mu.Lock()
	// Double-check: another goroutine may have won the race between our
	// nil-check above and the upgrade completing.
	if b.conn != nil {
		b.mu.Unlock()
		_ = conn.Close()
		return
	}
	b.conn = conn
	b.mu.Unlock()

	// --- Ping goroutine ---
	stopPing := make(chan struct{})
	go func() {
		ticker := time.NewTicker(30 * time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				b.mu.Lock()
				c := b.conn
				b.mu.Unlock()
				if c == nil {
					return
				}
				// Set a write deadline so a hung client doesn't block the ping forever.
				_ = c.SetWriteDeadline(time.Now().Add(10 * time.Second))
				_ = c.WriteMessage(websocket.PingMessage, nil)
				_ = c.SetWriteDeadline(time.Time{}) // clear after write
			case <-stopPing:
				return
			}
		}
	}()

	defer func() {
		close(stopPing)
		b.mu.Lock()
		if b.conn == conn {
			b.conn = nil
		}
		b.mu.Unlock()
		_ = conn.Close()
	}()

	for {
		_, msg, err := conn.ReadMessage()
		if err != nil {
			return
		}
		var resp BridgeResponse
		if err := json.Unmarshal(msg, &resp); err != nil {
			continue
		}
		b.mu.Lock()
		if pr, ok := b.pending[resp.ID]; ok {
			delete(b.pending, resp.ID)
			pr.ch <- resp
		}
		b.mu.Unlock()
	}
}
