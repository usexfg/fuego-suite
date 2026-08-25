package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"os/exec"
	"os/signal"
	"runtime"
	"sort"
	"strconv"
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
	Host          string
	PublicMode    bool
}

func parseFlags() Config {
	cfg := Config{
		DashboardPort: 18918,
		DaemonPort:    18180,
		WalletPort:    18183,
		SwapdPort:     18900,
		Host:          "127.0.0.1",
		PublicMode:    false,
	}
	flag.IntVar(&cfg.DashboardPort, "port", cfg.DashboardPort, "Dashboard HTTP port")
	flag.IntVar(&cfg.DaemonPort, "daemon-port", cfg.DaemonPort, "Fuegod RPC port")
	flag.IntVar(&cfg.WalletPort, "wallet-port", cfg.WalletPort, "Walletd RPC port")
	flag.IntVar(&cfg.SwapdPort, "swapd-port", cfg.SwapdPort, "Swap daemon status port")
	flag.StringVar(&cfg.Host, "host", cfg.Host, "HTTP listen host address")
	flag.BoolVar(&cfg.PublicMode, "public", cfg.PublicMode, "Enable public web explorer mode (disable local wallet proxy, allow iframe framing)")
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
	Daemon  bool `json:"daemon"`
	Wallet  bool `json:"wallet"`
	Swapd   bool `json:"swapd"`
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

func fetchRaw(url string, timeout time.Duration) ([]byte, error) {
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
	return io.ReadAll(io.LimitReader(resp.Body, 1<<20))
}

// ── Live Data Stores ────────────────────────────────────────────────────────────

type SpotTick struct {
	Timestamp int64   `json:"t"`
	Price     float64 `json:"p"`
	Volume    float64 `json:"v"`
}

type PriceHistoryStore struct {
	mu    sync.RWMutex
	ticks []SpotTick
}

var globalPriceHistory = &PriceHistoryStore{}

func (p *PriceHistoryStore) AddTick(priceAtomic, volumeAtomic uint64) {
	if priceAtomic == 0 {
		return
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	now := time.Now().Unix()
	if len(p.ticks) > 0 && (now-p.ticks[len(p.ticks)-1].Timestamp) < 2 {
		return
	}
	price := float64(priceAtomic)
	volume := float64(volumeAtomic)
	p.ticks = append(p.ticks, SpotTick{Timestamp: now, Price: price, Volume: volume})
	if len(p.ticks) > 10000 {
		p.ticks = p.ticks[len(p.ticks)-5000:]
	}
}

func (p *PriceHistoryStore) GetOHLCV(timeframe string, count int) []map[string]interface{} {
	p.mu.RLock()
	defer p.mu.RUnlock()

	if len(p.ticks) == 0 {
		return []map[string]interface{}{}
	}

	interval := int64(3600)
	switch timeframe {
	case "1m":
		interval = 60
	case "5m":
		interval = 300
	case "15m":
		interval = 900
	case "1h":
		interval = 3600
	case "4h":
		interval = 14400
	case "1d":
		interval = 86400
	}

	type candleData struct {
		t          int64
		o, h, l, c float64
		v          float64
	}
	candlesMap := make(map[int64]*candleData)
	var bucketKeys []int64

	for _, tick := range p.ticks {
		bKey := (tick.Timestamp / interval) * interval
		if c, exists := candlesMap[bKey]; exists {
			if tick.Price > c.h {
				c.h = tick.Price
			}
			if tick.Price < c.l {
				c.l = tick.Price
			}
			c.c = tick.Price
			c.v += tick.Volume
		} else {
			candlesMap[bKey] = &candleData{
				t: bKey, o: tick.Price, h: tick.Price, l: tick.Price, c: tick.Price, v: tick.Volume,
			}
			bucketKeys = append(bucketKeys, bKey)
		}
	}

	res := make([]map[string]interface{}, 0, len(bucketKeys))
	for _, k := range bucketKeys {
		c := candlesMap[k]
		res = append(res, map[string]interface{}{
			"t": c.t, "o": c.o, "h": c.h, "l": c.l, "c": c.c, "v": c.v,
		})
	}
	if count > 0 && len(res) > count {
		res = res[len(res)-count:]
	}
	return res
}

// priceLevel groups resting orders at the same price into a depth level.
// order_ids allows MMs to cancel specific orders at that level.
type priceLevel struct {
	Price    uint64   `json:"price"`
	Amount   uint64   `json:"amount"`
	Depth    uint64   `json:"depth"` // cumulative from best
	OrderIDs []string `json:"order_ids"`
}

func fetchOrderbookState(daemonPort int) map[string]interface{} {
	emptyBook := map[string]interface{}{
		"bids": []priceLevel{}, "asks": []priceLevel{},
		// Legacy flat arrays for hearth.js backwards compat
		"bid_prices": []uint64{}, "bid_amounts": []uint64{}, "bid_depths": []uint64{},
		"ask_prices": []uint64{}, "ask_amounts": []uint64{}, "ask_depths": []uint64{},
	}
	data, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/get_limit_orders", daemonPort), 3*time.Second)
	if err != nil {
		return emptyBook
	}
	rawOrders, ok := data["orders"].([]interface{})
	if !ok || len(rawOrders) == 0 {
		return emptyBook
	}

	// Group by (side, target_price) → accumulated amount + order_ids
	type levelKey struct {
		side  uint8
		price uint64
	}
	type levelAcc struct {
		amount   uint64
		orderIDs []string
	}
	levels := make(map[levelKey]*levelAcc)
	var bidKeys, askKeys []levelKey

	for _, item := range rawOrders {
		m, ok := item.(map[string]interface{})
		if !ok {
			continue
		}
		withdrawn, _ := m["withdrawn"].(bool)
		if withdrawn {
			continue
		}
		amt, _ := m["amount"].(float64)
		if amt <= 0 {
			continue
		}
		price, _ := m["target_price"].(float64)
		side, _ := m["side"].(float64)
		orderID, _ := m["order_id"].(string)

		key := levelKey{side: uint8(side), price: uint64(price)}
		if acc, exists := levels[key]; exists {
			acc.amount += uint64(amt)
			if orderID != "" {
				acc.orderIDs = append(acc.orderIDs, orderID)
			}
		} else {
			ids := []string{}
			if orderID != "" {
				ids = append(ids, orderID)
			}
			levels[key] = &levelAcc{amount: uint64(amt), orderIDs: ids}
			if key.side == 0 {
				bidKeys = append(bidKeys, key)
			} else {
				askKeys = append(askKeys, key)
			}
		}
	}

	// Sort bids desc, asks asc
	sort.Slice(bidKeys, func(i, j int) bool { return bidKeys[i].price > bidKeys[j].price })
	sort.Slice(askKeys, func(i, j int) bool { return askKeys[i].price < askKeys[j].price })

	// Build structured depth levels
	bidLevels := make([]priceLevel, 0, len(bidKeys))
	var cumBid uint64
	for _, k := range bidKeys {
		acc := levels[k]
		cumBid += acc.amount
		bidLevels = append(bidLevels, priceLevel{Price: k.price, Amount: acc.amount, Depth: cumBid, OrderIDs: acc.orderIDs})
	}
	askLevels := make([]priceLevel, 0, len(askKeys))
	var cumAsk uint64
	for _, k := range askKeys {
		acc := levels[k]
		cumAsk += acc.amount
		askLevels = append(askLevels, priceLevel{Price: k.price, Amount: acc.amount, Depth: cumAsk, OrderIDs: acc.orderIDs})
	}

	// Flat arrays for hearth.js chart backwards compatibility
	bidPrices := make([]uint64, len(bidLevels))
	bidAmounts := make([]uint64, len(bidLevels))
	bidDepths := make([]uint64, len(bidLevels))
	for i, l := range bidLevels {
		bidPrices[i], bidAmounts[i], bidDepths[i] = l.Price, l.Amount, l.Depth
	}
	askPrices := make([]uint64, len(askLevels))
	askAmounts := make([]uint64, len(askLevels))
	askDepths := make([]uint64, len(askLevels))
	for i, l := range askLevels {
		askPrices[i], askAmounts[i], askDepths[i] = l.Price, l.Amount, l.Depth
	}

	return map[string]interface{}{
		"bids": bidLevels, "asks": askLevels,
		"bid_prices": bidPrices, "bid_amounts": bidAmounts, "bid_depths": bidDepths,
		"ask_prices": askPrices, "ask_amounts": askAmounts, "ask_depths": askDepths,
	}
}

// ── Market Maker REST Endpoints ────────────────────────────────────────────────

func apiPoolHandler(daemonPort int) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		data, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/amm_pool_info", daemonPort), 3*time.Second)
		if err != nil {
			http.Error(w, `{"error":"failed to fetch pool info"}`, http.StatusBadGateway)
			return
		}
		json.NewEncoder(w).Encode(data)
	}
}

func apiOrderbookHandler(daemonPort int) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		ob := fetchOrderbookState(daemonPort)
		json.NewEncoder(w).Encode(ob)
	}
}

func apiQuoteHandler(daemonPort int) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		amtStr := r.URL.Query().Get("amount")
		dirStr := r.URL.Query().Get("direction")
		if amtStr == "" {
			amtStr = "10000000" // 1 XFG default
		}
		if dirStr == "" {
			dirStr = "0"
		}
		reqBody, _ := json.Marshal(map[string]interface{}{
			"input_amount": parseUint64Default(amtStr, 10000000),
			"direction":    parseUint64Default(dirStr, 0),
		})
		resp, err := http.Post(fmt.Sprintf("http://127.0.0.1:%d/amm_quote", daemonPort), "application/json", bytes.NewReader(reqBody))
		if err != nil {
			http.Error(w, `{"error":"failed to fetch quote"}`, http.StatusBadGateway)
			return
		}
		defer resp.Body.Close()
		io.Copy(w, resp.Body)
	}
}

func parseUint64Default(s string, def uint64) uint64 {
	v, err := strconv.ParseUint(s, 10, 64)
	if err != nil {
		return def
	}
	return v
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
		if data, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/heat_metrics", daemonPort), 3*time.Second); err == nil {
			bus.Broadcast(Event{Type: EventHeat, Payload: data, Time: time.Now().Unix()})
		}

		// Pool info + Spot price history tick
		if data, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/amm_pool_info", daemonPort), 3*time.Second); err == nil {
			if sp, ok := data["spot_price"].(float64); ok && sp > 0 {
				rx, _ := data["reserve_xfg"].(float64)
				globalPriceHistory.AddTick(uint64(sp), uint64(rx))
			}
			bus.Broadcast(Event{Type: EventPool, Payload: data, Time: time.Now().Unix()})
		}

		// Orderbook
		if data := fetchOrderbookState(daemonPort); data != nil {
			bus.Broadcast(Event{Type: EventOrderbook, Payload: data, Time: time.Now().Unix()})
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
		if err != nil {
			return false
		}
		host := u.Hostname()
		return host == "127.0.0.1" || host == "localhost" || host == "::1"
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

// ── Security Middleware ────────────────────────────────────────────────────────

func securityHeaders(next http.Handler, publicMode bool) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if publicMode {
			w.Header().Set("Content-Security-Policy",
				"default-src 'self'; "+
					"script-src 'self' 'unsafe-inline'; "+
					"style-src 'self' 'unsafe-inline'; "+
					"img-src 'self' data:; "+
					"connect-src 'self' ws: wss: http: https:; "+
					"frame-ancestors 'self' https: http:; "+
					"object-src 'none'; "+
					"base-uri 'self'; "+
					"form-action 'self'")
			w.Header().Set("X-Frame-Options", "SAMEORIGIN")
		} else {
			w.Header().Set("Content-Security-Policy",
				"default-src 'self'; "+
					"script-src 'self'; "+
					"style-src 'self' 'unsafe-inline'; "+
					"img-src 'self' data:; "+
					"connect-src 'self' ws://127.0.0.1:*; "+
					"frame-src 'none'; "+
					"object-src 'none'; "+
					"base-uri 'self'; "+
					"form-action 'self'")
			w.Header().Set("X-Frame-Options", "DENY")
		}
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("Referrer-Policy", "strict-origin-when-cross-origin")
		w.Header().Set("X-XSS-Protection", "1; mode=block")
		next.ServeHTTP(w, r)
	})
}

func corsMiddleware(next http.Handler, publicMode bool) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		origin := r.Header.Get("Origin")
		if origin != "" {
			if publicMode {
				w.Header().Set("Access-Control-Allow-Origin", origin)
				w.Header().Set("Vary", "Origin")
			} else {
				ou, err := url.Parse(origin)
				if err != nil {
					http.Error(w, "bad origin", http.StatusForbidden)
					return
				}
				oh := ou.Hostname()
				if oh != "127.0.0.1" && oh != "localhost" && oh != "::1" {
					http.Error(w, "forbidden origin", http.StatusForbidden)
					return
				}
				w.Header().Set("Access-Control-Allow-Origin", origin)
				w.Header().Set("Vary", "Origin")
			}
		}
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")
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
	"get_address":           true,
	"get_transfers":         true,
	"get_height":            true,
	"getstatus":             true,
	"get_limit_orders":      true,
	"list_cds":              true,
	"estimate_cd_yield":     true,
	"get_cd_claim_preview":  true,
	// Write — Hearth limit orders
	"place_limit_order":     true,
	"cancel_limit_order":    true,
	// Write — AMM swaps & LP
	"amm_swap":              true,
	"amm_add_liquidity":     true,
	"amm_remove_liquidity":  true,
	"amm_claim_lp_fees":     true,
	// Write — HEAT
	"heat_mint":             true,
	"send_heat":             true,
	// Write — CDs
	"create_cd":             true,
	"withdraw_cd":           true,
	"rollover_cd":           true,
	// Write — atomic swap initiation
	"initiate_swap":         true,
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

var walletRateLimiter = newRateLimiter(200 * time.Millisecond)

func walletProxyHandler(walletPort int, publicMode bool) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if publicMode {
			http.Error(w, "wallet RPC proxy disabled in public mode", http.StatusForbidden)
			return
		}
		if r.Method != http.MethodPost {
			http.Error(w, "POST required", http.StatusMethodNotAllowed)
			return
		}

		// Rate limit (per IP)
		ip := r.RemoteAddr
		if fwd := r.Header.Get("X-Forwarded-For"); fwd != "" {
			ip = strings.Split(fwd, ",")[0]
		}
		if !walletRateLimiter.allow(ip) {
			http.Error(w, "rate limited", http.StatusTooManyRequests)
			return
		}

		body, err := io.ReadAll(io.LimitReader(r.Body, 1<<20))
		if err != nil {
			http.Error(w, "read error", http.StatusBadRequest)
			return
		}

		// Parse and validate method
		var rpcReq struct {
			Method string      `json:"method"`
			Params interface{} `json:"params"`
			ID     interface{} `json:"id"`
		}
		if err := json.Unmarshal(body, &rpcReq); err != nil {
			http.Error(w, "invalid JSON", http.StatusBadRequest)
			return
		}

		if !walletAllowedMethods[rpcReq.Method] {
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
			fmt.Sprintf("http://127.0.0.1:%d/json_rpc", walletPort),
			strings.NewReader(string(cleanBody)))
		if err != nil {
			http.Error(w, "proxy error", http.StatusBadGateway)
			return
		}
		proxyReq.Header.Set("Content-Type", "application/json")
		resp, err := http.DefaultClient.Do(proxyReq)
		if err != nil {
			http.Error(w, "walletd unreachable", http.StatusBadGateway)
			return
		}
		defer resp.Body.Close()
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(resp.StatusCode)
		io.Copy(w, resp.Body)
	}
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

func jsonRpcHandler(daemonProxy *httputil.ReverseProxy, daemonPort int) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			daemonProxy.ServeHTTP(w, r)
			return
		}
		body, err := io.ReadAll(io.LimitReader(r.Body, 1<<20))
		if err != nil {
			http.Error(w, "read error", http.StatusBadRequest)
			return
		}
		r.Body = io.NopCloser(bytes.NewBuffer(body))

		var req struct {
			JsonRpc string                 `json:"jsonrpc"`
			ID      interface{}            `json:"id"`
			Method  string                 `json:"method"`
			Params  map[string]interface{} `json:"params"`
		}
		if err := json.Unmarshal(body, &req); err == nil {
			if req.Method == "get_ohlcv" || req.Method == "get_ohlvc" {
				tf := "1h"
				cnt := 200
				if req.Params != nil {
					if t, ok := req.Params["timeframe"].(string); ok && t != "" {
						tf = t
					}
					if c, ok := req.Params["count"].(float64); ok && c > 0 {
						cnt = int(c)
					}
				}
				candles := globalPriceHistory.GetOHLCV(tf, cnt)
				w.Header().Set("Content-Type", "application/json")
				json.NewEncoder(w).Encode(map[string]interface{}{
					"jsonrpc": "2.0",
					"id":      req.ID,
					"result":  map[string]interface{}{"candles": candles},
				})
				return
			}
			if req.Method == "get_orderbook_state" {
				obData := fetchOrderbookState(daemonPort)
				w.Header().Set("Content-Type", "application/json")
				json.NewEncoder(w).Encode(map[string]interface{}{
					"jsonrpc": "2.0",
					"id":      req.ID,
					"result":  obData,
				})
				return
			}
			if req.Method == "getswapoffers" {
				offers, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/getswapoffers", daemonPort), 3*time.Second)
				if err == nil {
					w.Header().Set("Content-Type", "application/json")
					json.NewEncoder(w).Encode(map[string]interface{}{
						"jsonrpc": "2.0",
						"id":      req.ID,
						"result":  offers,
					})
					return
				}
			}
			if req.Method == "getactiveswaps" {
				swaps, err := fetchJSON(fmt.Sprintf("http://127.0.0.1:%d/getactiveswaps", daemonPort), 3*time.Second)
				if err == nil {
					w.Header().Set("Content-Type", "application/json")
					json.NewEncoder(w).Encode(map[string]interface{}{
						"jsonrpc": "2.0",
						"id":      req.ID,
						"result":  swaps,
					})
					return
				}
			}
		}

		daemonProxy.ServeHTTP(w, r)
	}
}

// ── Main ───────────────────────────────────────────────────────────────────────

func main() {
	cfg := parseFlags()
	bus := NewEventBus()

	// Daemon reverse proxies
	daemonTarget := fmt.Sprintf("http://127.0.0.1:%d", cfg.DaemonPort)
	daemonProxy := newReverseProxy(daemonTarget)

	walletTarget := fmt.Sprintf("http://127.0.0.1:%d", cfg.WalletPort)
	walletProxy := newReverseProxy(walletTarget)

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
	mux.HandleFunc("/api/wallet", walletProxyHandler(cfg.WalletPort, cfg.PublicMode))

	// Daemon proxy (read-only)
	mux.HandleFunc("/api/daemon/", proxyHandler(daemonProxy))

	// Direct daemon RPC proxy
	mux.HandleFunc("/json_rpc", jsonRpcHandler(daemonProxy, cfg.DaemonPort))
	mux.HandleFunc("/heat_metrics", proxyHandler(daemonProxy))
	mux.HandleFunc("/amm_pool_info", proxyHandler(daemonProxy))
	mux.HandleFunc("/hearth_pool_info", proxyHandler(daemonProxy))
	mux.HandleFunc("/hearth_info", proxyHandler(daemonProxy))
	mux.HandleFunc("/amm_quote", proxyHandler(daemonProxy))
	mux.HandleFunc("/hearth_quote", proxyHandler(daemonProxy))
	mux.HandleFunc("/getswapprice", proxyHandler(daemonProxy))

	// Hearth & MM REST endpoints
	mux.HandleFunc("/api/pool", apiPoolHandler(cfg.DaemonPort))
	mux.HandleFunc("/api/hearth/pool", apiPoolHandler(cfg.DaemonPort))
	mux.HandleFunc("/api/orderbook", apiOrderbookHandler(cfg.DaemonPort))
	mux.HandleFunc("/api/hearth/orderbook", apiOrderbookHandler(cfg.DaemonPort))
	mux.HandleFunc("/api/quote", apiQuoteHandler(cfg.DaemonPort))
	mux.HandleFunc("/api/hearth/quote", apiQuoteHandler(cfg.DaemonPort))

	// Wallet proxy (direct)
	if !cfg.PublicMode {
		mux.HandleFunc("/wallet_rpc", proxyHandler(walletProxy))
	}

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

	// Static files
	staticDir := "static"
	if _, err := os.Stat(staticDir); os.IsNotExist(err) {
		staticDir = "./static"
	}
	fileServer := http.FileServer(http.Dir(staticDir))

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/" {
			http.Redirect(w, r, "/hearth.html", http.StatusTemporaryRedirect)
			return
		}
		// hearth.html lives at the dashboard root, serve it from there
		if r.URL.Path == "/hearth.html" {
			http.ServeFile(w, r, "hearth.html")
			return
		}
		// All other static assets served from static/
		fileServer.ServeHTTP(w, r)
	})

	// Apply middleware chain
	handler := corsMiddleware(securityHeaders(mux, cfg.PublicMode), cfg.PublicMode)

	addr := fmt.Sprintf("%s:%d", cfg.Host, cfg.DashboardPort)
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
	if cfg.PublicMode {
		log.Printf("fuego-dashboard listening on %s (PUBLIC EXPLORER MODE - wallet proxy disabled)", browserURL)
	} else {
		log.Printf("fuego-dashboard listening on %s (LOCAL DESKTOP MODE)", browserURL)
		go openBrowser(browserURL)
	}

	if err := server.ListenAndServe(); err != http.ErrServerClosed {
		log.Fatalf("server error: %v", err)
	}
	log.Println("server stopped")
}
