package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/gorilla/websocket"
)

// ── Configuration ──────────────────────────────────────────────────────────────

type Config struct {
	DashboardPort int
	DaemonPort    int
	WalletPort    int
	SwapdPort     int
	SwapdRPCPort  int
	SwapdRPCToken string
}

func parseFlags() Config {
	cfg := Config{
		DashboardPort: 18918,
		DaemonPort:    18180,
		WalletPort:    18183,
		SwapdPort:     18900,
		SwapdRPCPort:  18902,
		SwapdRPCToken: os.Getenv("XFG_SWAPD_RPC_TOKEN"),
	}
	flag.IntVar(&cfg.DashboardPort, "port", cfg.DashboardPort, "Dashboard HTTP port")
	flag.IntVar(&cfg.DaemonPort, "daemon-port", cfg.DaemonPort, "Fuegod RPC port")
	flag.IntVar(&cfg.WalletPort, "wallet-port", cfg.WalletPort, "Walletd RPC port")
	flag.IntVar(&cfg.SwapdPort, "swapd-port", cfg.SwapdPort, "Swap daemon status port")
	flag.IntVar(&cfg.SwapdRPCPort, "swapd-rpc-port", cfg.SwapdRPCPort, "Swap daemon JSON-RPC port")
	flag.StringVar(&cfg.SwapdRPCToken, "swapd-rpc-token", cfg.SwapdRPCToken,
		"Swap daemon JSON-RPC token (prefer XFG_SWAPD_RPC_TOKEN)")
	flag.Parse()
	return cfg
}

// ── Event Bus ──────────────────────────────────────────────────────────────────

type EventType string

const (
	EventBlock     EventType = "block"
	EventHeat      EventType = "heat_metric"
	EventPool      EventType = "pool_info"
	EventSwap      EventType = "swap_update"
	EventSpv       EventType = "spv_status"
	EventHealth    EventType = "health"
	EventOrderbook EventType = "orderbook"
)

type Event struct {
	Type    EventType   `json:"type"`
	Payload interface{} `json:"payload"`
	Time    int64       `json:"ts"`
}

// Per-daemon health state
type DaemonHealth struct {
	Daemon bool `json:"daemon"`
	Wallet bool `json:"wallet"`
	Swapd  bool `json:"swapd"`
}

type EventBus struct {
	clients map[*websocket.Conn]bool
	mu      sync.RWMutex
}

func NewEventBus() *EventBus {
	return &EventBus{
		clients: make(map[*websocket.Conn]bool),
	}
}

func (eb *EventBus) Subscribe(conn *websocket.Conn) {
	eb.mu.Lock()
	defer eb.mu.Unlock()
	eb.clients[conn] = true
}

func (eb *EventBus) Unsubscribe(conn *websocket.Conn) {
	eb.mu.Lock()
	defer eb.mu.Unlock()
	delete(eb.clients, conn)
}

func (eb *EventBus) Broadcast(evt Event) {
	data, err := json.Marshal(evt)
	if err != nil {
		return
	}

	// Collect dead connections under RLock (no map mutations), then
	// clean up under a write lock to avoid the race condition of
	// deleting from a map while iterating it under RLock.
	var dead []*websocket.Conn
	eb.mu.RLock()
	for conn := range eb.clients {
		if err := conn.WriteMessage(websocket.TextMessage, data); err != nil {
			dead = append(dead, conn)
		}
	}
	eb.mu.RUnlock()

	if len(dead) > 0 {
		eb.mu.Lock()
		for _, conn := range dead {
			conn.Close()
			delete(eb.clients, conn)
		}
		eb.mu.Unlock()
	}
}

// ── Daemon Proxies ─────────────────────────────────────────────────────────────

func newReverseProxy(target string) *httputil.ReverseProxy {
	u, _ := url.Parse(target)
	return httputil.NewSingleHostReverseProxy(u)
}

func proxyHandler(proxy *httputil.ReverseProxy) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		proxy.ServeHTTP(w, r)
	}
}

// ── HTTP Fetch Helpers ─────────────────────────────────────────────────────────

func fetchJSON(url string, timeout time.Duration) (map[string]interface{}, error) {
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return nil, err
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(io.LimitReader(resp.Body, 1<<20))
	if err != nil {
		return nil, err
	}
	var result map[string]interface{}
	if err := json.Unmarshal(body, &result); err != nil {
		return nil, err
	}
	return result, nil
}

func fetchJSONPost(url string, timeout time.Duration) (map[string]interface{}, error) {
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "POST", url, strings.NewReader("{}"))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("daemon HTTP %d", resp.StatusCode)
	}
	var result map[string]interface{}
	if err := json.NewDecoder(io.LimitReader(resp.Body, 1<<20)).Decode(&result); err != nil {
		return nil, err
	}
	if status, ok := result["status"].(string); ok && status != "OK" {
		return nil, fmt.Errorf("daemon status %s", status)
	}
	return result, nil
}

// ── Event Pollers ──────────────────────────────────────────────────────────────

func pollDaemon(bus *EventBus, daemonPort int) {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()
	for range ticker.C {
		health := DaemonHealth{}

		// Block height + chain info
		if data, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/getinfo", daemonPort), 3*time.Second); err == nil {
			health.Daemon = true
			bus.Broadcast(Event{Type: EventBlock, Payload: data, Time: time.Now().Unix()})
		}

		// HEAT metrics
		if data, err := fetchJSONPost(fmt.Sprintf("http://127.0.0.1:%d/heat_metrics", daemonPort), 3*time.Second); err == nil {
			bus.Broadcast(Event{Type: EventHeat, Payload: data, Time: time.Now().Unix()})
		}

		// Pool info
		if data, err := fetchJSONPost(fmt.Sprintf("http://127.0.0.1:%d/amm_pool_info", daemonPort), 3*time.Second); err == nil {
			bus.Broadcast(Event{Type: EventPool, Payload: data, Time: time.Now().Unix()})
		}

		// Health status
		bus.Broadcast(Event{Type: EventHealth, Payload: health, Time: time.Now().Unix()})
	}
}

func pollWallet(bus *EventBus, walletPort int) {
	ticker := time.NewTicker(10 * time.Second)
	defer ticker.Stop()
	for range ticker.C {
		payload := map[string]interface{}{
			"available": 0, "locked": 0,
		}
		rpcBody := `{"jsonrpc":"2.0","id":"balance","method":"getbalance"}`
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		req, err := http.NewRequestWithContext(ctx, "POST",
			fmt.Sprintf("http://127.0.0.1:%d/json_rpc", walletPort),
			strings.NewReader(rpcBody))
		if err != nil {
			continue
		}
		req.Header.Set("Content-Type", "application/json")
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			continue
		}
		body, _ := io.ReadAll(io.LimitReader(resp.Body, 1<<20))
		resp.Body.Close()
		var rpcResp struct {
			Result struct {
				AvailableBalance uint64 `json:"availableBalance"`
				LockedAmount     uint64 `json:"lockedAmount"`
			} `json:"result"`
		}
		if json.Unmarshal(body, &rpcResp) == nil {
			payload["available"] = rpcResp.Result.AvailableBalance
			payload["locked"] = rpcResp.Result.LockedAmount
		}
		bus.Broadcast(Event{Type: EventHealth, Payload: map[string]interface{}{
			"wallet": true, "balance": payload,
		}, Time: time.Now().Unix()})
	}
}

func pollSwapd(bus *EventBus, swapdPort int) {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()
	for range ticker.C {
		data, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/", swapdPort), 3*time.Second)
		if err != nil {
			continue
		}
		bus.Broadcast(Event{Type: EventSwap, Payload: data, Time: time.Now().Unix()})

		// SPV status is embedded in swap status JSON
		if spv, ok := data["spv"].(map[string]interface{}); ok {
			bus.Broadcast(Event{Type: EventSpv, Payload: spv, Time: time.Now().Unix()})
		}
	}
}

// ── WebSocket Handler ──────────────────────────────────────────────────────────

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 4096,
	CheckOrigin: func(r *http.Request) bool {
		origin := r.Header.Get("Origin")
		if origin == "" {
			return true
		}
		u, err := url.Parse(origin)
		return err == nil && u.Scheme == "http" && strings.EqualFold(u.Host, r.Host)
	},
}

const maxWebSocketMsgSize = 4096

func handleWebSocket(bus *EventBus, w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("ws upgrade: %v", err)
		return
	}
	conn.SetReadLimit(maxWebSocketMsgSize)
	bus.Subscribe(conn)
	log.Printf("ws client connected (%d total)", len(bus.clients))

	// Drain reader (detect disconnect)
	go func() {
		defer func() {
			bus.Unsubscribe(conn)
			conn.Close()
			log.Printf("ws client disconnected (%d total)", len(bus.clients))
		}()
		for {
			if _, _, err := conn.ReadMessage(); err != nil {
				return
			}
		}
	}()
}

// ── Security Middleware ────────────────────────────────────────────────────────

func securityHeaders(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Security-Policy",
			"default-src 'self'; "+
				// The pre-paint register restore in hearth.html is inline so it runs
				// before first paint. 'unsafe-inline' would defeat the policy
				// entirely, so allow exactly that one script by hash instead.
				// The digest is base64, not hex: sha256sum output must be piped
				// through base64. Regenerate with TestInlineScriptHashMatchesCSP,
				// which fails loudly if the markup and this header ever drift.
				"script-src 'self' 'sha256-ibV0GXBtfrSbCnu6wFcVaDxNwt9yrvqauBsXRmZKA9Q='; "+
				"style-src 'self' 'unsafe-inline'; "+
				"img-src 'self' data:; "+
				"connect-src 'self' ws://127.0.0.1:*; "+
				"frame-src 'none'; "+
				"object-src 'none'; "+
				"base-uri 'self'; "+
				"form-action 'self'")
		w.Header().Set("X-Frame-Options", "DENY")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("Referrer-Policy", "strict-origin-when-cross-origin")
		w.Header().Set("X-XSS-Protection", "1; mode=block")
		next.ServeHTTP(w, r)
	})
}

func corsMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		host := (&url.URL{Host: r.Host}).Hostname()
		if host != "127.0.0.1" && host != "localhost" && host != "::1" {
			http.Error(w, "forbidden host", http.StatusForbidden)
			return
		}
		origin := r.Header.Get("Origin")
		if origin != "" {
			ou, err := url.Parse(origin)
			if err != nil || ou.Scheme != "http" || !strings.EqualFold(ou.Host, r.Host) {
				http.Error(w, "bad origin", http.StatusForbidden)
				return
			}
			w.Header().Set("Access-Control-Allow-Origin", origin)
			w.Header().Set("Vary", "Origin")
		}
		if r.Method == http.MethodPost &&
			(r.URL.Path == "/api/wallet" || r.URL.Path == "/api/swapd-rpc") &&
			r.Header.Get("X-Fuego-Operator") != "1" {
			http.Error(w, "operator header required", http.StatusForbidden)
			return
		}
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, X-Fuego-Operator")
		w.Header().Set("Access-Control-Max-Age", "86400")
		if r.Method == "OPTIONS" {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}

// ── Wallet RPC Proxy (browser-initiated execution) ────────────────────────────
// The browser never sees the wallet access key. It POSTs order parameters,
// the Go server proxies to walletd with the key.
// Only explicitly allowlisted methods are proxied; everything else is rejected.

var walletAllowedMethods = map[string]bool{
	// Read-only
	"getbalance":            true,
	"getaddress":            true,
	"getaddresses":          true,
	"getstatus":             true,
	"getheight":             true,
	"gettransfers":          true,
	"gettransferbytxid":     true,
	"getunconfirmedbalance": true,
	"getvalidateaddress":    true,
	// Write (trading)
	"place_limit_order":  true,
	"cancel_limit_order": true,
	"amm_swap":           true,
}

var swapdAllowedMethods = map[string]bool{
	"initiate_swap":     true,
	"accept":            true,
	"get_reserve_proof": true,
	"list_swaps":        true,
	"swap_status":       true,
	"refund":            true,
	"check_timeouts":    true,
	"list_chains":       true,
}

// Simple token-bucket rate limiter per IP.
type rateLimiter struct {
	mu       sync.Mutex
	buckets  map[string]time.Time
	interval time.Duration
}

func newRateLimiter(interval time.Duration) *rateLimiter {
	return &rateLimiter{
		buckets:  make(map[string]time.Time),
		interval: interval,
	}
}

func (rl *rateLimiter) allow(key string) bool {
	rl.mu.Lock()
	defer rl.mu.Unlock()
	now := time.Now()
	if last, ok := rl.buckets[key]; ok && now.Sub(last) < rl.interval {
		return false
	}
	rl.buckets[key] = now
	return true
}

var (
	walletRateLimiter = newRateLimiter(200 * time.Millisecond)
	swapdRateLimiter  = newRateLimiter(200 * time.Millisecond)
)

type localRPCProxyConfig struct {
	port    int
	path    string
	service string
	token   string
	allowed map[string]bool
	limiter *rateLimiter
}

// localRPCProxyHandler keeps daemon credentials server-side, validates the
// requested method against a narrow allowlist, and only forwards to loopback.
func localRPCProxyHandler(cfg localRPCProxyConfig) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "POST required", http.StatusMethodNotAllowed)
			return
		}

		// Rate limit (per IP)
		ip, _, err := net.SplitHostPort(r.RemoteAddr)
		if err != nil {
			ip = r.RemoteAddr
		}
		if !cfg.limiter.allow(ip) {
			http.Error(w, "rate limited", http.StatusTooManyRequests)
			return
		}

		body, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 1<<20))
		if err != nil {
			http.Error(w, "read error", http.StatusBadRequest)
			return
		}

		// Parse and validate method
		var rpcReq struct {
			JSONRPC string      `json:"jsonrpc,omitempty"`
			Method  string      `json:"method"`
			Params  interface{} `json:"params"`
			ID      interface{} `json:"id"`
		}
		if err := json.Unmarshal(body, &rpcReq); err != nil {
			http.Error(w, "invalid JSON", http.StatusBadRequest)
			return
		}

		if !cfg.allowed[rpcReq.Method] {
			http.Error(w, fmt.Sprintf("method %q not allowed", rpcReq.Method), http.StatusForbidden)
			return
		}

		// Re-serialize to normalize (strip any extra fields)
		cleanBody, err := json.Marshal(rpcReq)
		if err != nil {
			http.Error(w, "marshal error", http.StatusInternalServerError)
			return
		}

		ctx, cancel := context.WithTimeout(r.Context(), 10*time.Second)
		defer cancel()
		proxyReq, err := http.NewRequestWithContext(ctx, "POST",
			fmt.Sprintf("http://127.0.0.1:%d%s", cfg.port, cfg.path),
			strings.NewReader(string(cleanBody)))
		if err != nil {
			http.Error(w, "proxy error", http.StatusBadGateway)
			return
		}
		proxyReq.Header.Set("Content-Type", "application/json")
		if cfg.token != "" {
			proxyReq.Header.Set("X-Swap-Token", cfg.token)
		}
		resp, err := http.DefaultClient.Do(proxyReq)
		if err != nil {
			http.Error(w, cfg.service+" unreachable", http.StatusBadGateway)
			return
		}
		defer resp.Body.Close()
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(resp.StatusCode)
		io.Copy(w, resp.Body)
	}
}

func walletProxyHandler(walletPort int) http.HandlerFunc {
	return localRPCProxyHandler(localRPCProxyConfig{
		port: walletPort, path: "/json_rpc", service: "walletd",
		allowed: walletAllowedMethods, limiter: walletRateLimiter,
	})
}

func swapdRPCProxyHandler(swapdRPCPort int, token string) http.HandlerFunc {
	return localRPCProxyHandler(localRPCProxyConfig{
		port: swapdRPCPort, path: "/", service: "xfg-swapd", token: token,
		allowed: swapdAllowedMethods, limiter: swapdRateLimiter,
	})
}

// ── Health Check ───────────────────────────────────────────────────────────────

func healthHandler(daemonPort, walletPort, swapdPort int) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		h := DaemonHealth{}
		if _, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/getinfo", daemonPort), 1*time.Second); err == nil {
			h.Daemon = true
		}
		if _, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/json_rpc", walletPort), 1*time.Second); err == nil {
			h.Wallet = true
		}
		if _, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/", swapdPort), 1*time.Second); err == nil {
			h.Swapd = true
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(h)
	}
}

// ── Browser Auto-Open ──────────────────────────────────────────────────────────

func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "darwin":
		cmd = exec.Command("open", url)
	case "linux":
		cmd = exec.Command("xdg-open", url)
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	}
	if cmd != nil {
		cmd.Start()
	}
}

// ── Static Asset Resolution ────────────────────────────────────────────────────

// resolveAssetRoot finds the directory that holds hearth.html and static/.
// It is anchored to the executable's location rather than the process working
// directory, so the binary works when launched from anywhere (launchd, a
// release bin dir, a double-click, `open`, etc.) and not just from dashboard/.
func resolveAssetRoot() string {
	var candidates []string

	if exe, err := os.Executable(); err == nil {
		if resolved, err := filepath.EvalSymlinks(exe); err == nil {
			exe = resolved
		}
		exeDir := filepath.Dir(exe)
		candidates = append(candidates,
			exeDir,                                   // build/release/bin/ alongside static/
			filepath.Dir(exeDir),                     // bin/ inside build/release/
			filepath.Join(exeDir, "..", "dashboard"), // repo layout: build/release/bin -> dashboard
		)
	}

	if wd, err := os.Getwd(); err == nil {
		candidates = append(candidates,
			wd,
			filepath.Join(wd, "dashboard"),
		)
	}

	for _, dir := range candidates {
		if _, err := os.Stat(filepath.Join(dir, "static", "hearth.html")); err == nil {
			return dir
		}
	}

	// Last resort: current working directory, so error messages stay relative.
	wd, _ := os.Getwd()
	return wd
}

func main() {
	cfg := parseFlags()
	bus := NewEventBus()

	// Daemon reverse proxies
	daemonTarget := fmt.Sprintf("http://127.0.0.1:%d", cfg.DaemonPort)
	daemonProxy := newReverseProxy(daemonTarget)

	// Start event pollers
	go pollDaemon(bus, cfg.DaemonPort)
	go pollWallet(bus, cfg.WalletPort)
	if cfg.SwapdPort > 0 {
		go pollSwapd(bus, cfg.SwapdPort)
	}

	// Routes
	mux := http.NewServeMux()

	// WebSocket
	mux.HandleFunc("/ws/blocks", func(w http.ResponseWriter, r *http.Request) {
		handleWebSocket(bus, w, r)
	})

	// Health check
	mux.HandleFunc("/api/health", healthHandler(cfg.DaemonPort, cfg.WalletPort, cfg.SwapdPort))

	// Wallet RPC proxy (browser-initiated execution — keys never touch browser)
	mux.HandleFunc("/api/wallet", walletProxyHandler(cfg.WalletPort))
	if cfg.SwapdRPCPort > 0 {
		// xfg-swapd owns cross-chain execution. Keep its optional control token
		// out of browser JavaScript and inject it on this loopback-only hop.
		mux.HandleFunc("/api/swapd-rpc", swapdRPCProxyHandler(cfg.SwapdRPCPort, cfg.SwapdRPCToken))
	}

	// Explicitly read-only daemon routes; no generic RPC pass-through.
	mux.HandleFunc("/heat_metrics", proxyHandler(daemonProxy))
	mux.HandleFunc("/amm_pool_info", proxyHandler(daemonProxy))
	mux.HandleFunc("/amm_quote", proxyHandler(daemonProxy))
	mux.HandleFunc("/get_limit_orders", proxyHandler(daemonProxy))
	mux.HandleFunc("/get_fuego_price", proxyHandler(daemonProxy))
	mux.HandleFunc("/getswapprice", proxyHandler(daemonProxy))

	// Swap daemon status (offers + active swaps JSON from xfg-swapd HTTP root)
	if cfg.SwapdPort > 0 {
		swapdTarget := fmt.Sprintf("http://127.0.0.1:%d", cfg.SwapdPort)
		swapdProxy := newReverseProxy(swapdTarget)
		mux.HandleFunc("/api/swapd", func(w http.ResponseWriter, r *http.Request) {
			r.URL.Path = "/"
			swapdProxy.ServeHTTP(w, r)
		})
		mux.HandleFunc("/api/swapd/", func(w http.ResponseWriter, r *http.Request) {
			r.URL.Path = "/"
			swapdProxy.ServeHTTP(w, r)
		})
	}

	// Static files — resolved relative to the binary, not the working directory.
	// Every page lives in static/, so a single file server owns the whole surface.
	assetRoot := resolveAssetRoot()
	staticDir := filepath.Join(assetRoot, "static")
	fileServer := http.FileServer(http.Dir(staticDir))

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/" {
			http.Redirect(w, r, "/hearth.html", http.StatusTemporaryRedirect)
			return
		}
		fileServer.ServeHTTP(w, r)
	})

	// Apply middleware chain
	handler := corsMiddleware(securityHeaders(mux))

	addr := fmt.Sprintf("127.0.0.1:%d", cfg.DashboardPort)
	server := &http.Server{
		Addr:         addr,
		Handler:      handler,
		ReadTimeout:  30 * time.Second,
		WriteTimeout: 30 * time.Second,
		IdleTimeout:  120 * time.Second,
	}

	// Graceful shutdown
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		<-sigCh
		log.Println("shutting down...")
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		server.Shutdown(ctx)
	}()

	browserURL := fmt.Sprintf("http://%s", addr)
	log.Printf("fuego-dashboard listening on %s (assets: %s)", browserURL, assetRoot)
	go openBrowser(browserURL)

	if err := server.ListenAndServe(); err != http.ErrServerClosed {
		log.Fatalf("server error: %v", err)
	}
	log.Println("server stopped")
}
