package main

import (
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func testServerPort(t *testing.T, rawURL string) int {
	t.Helper()
	parsed, err := url.Parse(rawURL)
	if err != nil {
		t.Fatal(err)
	}
	port, err := strconv.Atoi(parsed.Port())
	if err != nil {
		t.Fatal(err)
	}
	return port
}

func TestWalletRPCProxyRejectsTradingMethods(t *testing.T) {
	var calls atomic.Int32
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
		if r.URL.Path != "/json_rpc" {
			t.Errorf("path = %q, want /json_rpc", r.URL.Path)
		}
		body, _ := io.ReadAll(r.Body)
		if !strings.Contains(string(body), `"method":"getbalance"`) {
			t.Errorf("unexpected normalized body: %s", body)
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"jsonrpc":"2.0","result":{"available_balance":0},"id":1}`))
	}))
	defer upstream.Close()

	handler := localRPCProxyHandler(localRPCProxyConfig{
		port: testServerPort(t, upstream.URL), path: "/json_rpc", service: "walletd",
		allowed: walletAllowedMethods, limiter: newRateLimiter(time.Nanosecond),
	})
	req := httptest.NewRequest(http.MethodPost, "/api/wallet",
		strings.NewReader(`{"jsonrpc":"2.0","id":1,"method":"getbalance","params":{}}`))
	res := httptest.NewRecorder()
	handler.ServeHTTP(res, req)

	if res.Code != http.StatusOK {
		t.Fatalf("status = %d, body = %s", res.Code, res.Body.String())
	}
	if calls.Load() != 1 {
		t.Fatalf("upstream calls = %d, want 1", calls.Load())
	}
	for _, method := range []string{
		"amm_swap", "place_limit_order", "cancel_limit_order",
		"initiate_swap", "accept", "refund", "check_timeouts",
	} {
		forbidden := httptest.NewRequest(http.MethodPost, "/api/wallet",
			strings.NewReader(`{"jsonrpc":"2.0","id":2,"method":"`+method+`","params":{}}`))
		forbiddenRes := httptest.NewRecorder()
		handler.ServeHTTP(forbiddenRes, forbidden)
		if forbiddenRes.Code != http.StatusForbidden {
			t.Fatalf("%s status = %d, want 403", method, forbiddenRes.Code)
		}
		if calls.Load() != 1 {
			t.Fatalf("%s reached upstream", method)
		}
	}

	oversized := httptest.NewRequest(http.MethodPost, "/api/wallet",
		strings.NewReader(`{"jsonrpc":"2.0","id":3,"method":"getbalance","params":{}}`+
			strings.Repeat(" ", 1<<20)))
	oversizedRes := httptest.NewRecorder()
	handler.ServeHTTP(oversizedRes, oversized)
	if oversizedRes.Code != http.StatusBadRequest || calls.Load() != 1 {
		t.Fatalf("oversized request status = %d, upstream calls = %d", oversizedRes.Code, calls.Load())
	}
}

func TestDashboardHasNoSwapControlRoute(t *testing.T) {
	files := []string{"main.go", "static/js/app.js", "static/js/swapxfg.js"}
	for _, path := range files {
		body, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		text := string(body)
		for _, forbidden := range []string{"/api/swapd-rpc", "App.swapRpc", "initiate_swap"} {
			if strings.Contains(text, forbidden) {
				t.Errorf("%s still contains %q", path, forbidden)
			}
		}
	}
}

func TestSwapdStatusProxyIsGetOnly(t *testing.T) {
	var calls atomic.Int32
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
		if r.Method != http.MethodGet || r.URL.Path != "/" || r.URL.RawQuery != "" {
			t.Fatalf("upstream request = %s %s?%s", r.Method, r.URL.Path, r.URL.RawQuery)
		}
		_, _ = w.Write([]byte(`{"network":"testnet","profile":"testnet"}`))
	}))
	defer upstream.Close()

	handler := swapdStatusProxyHandler(newReverseProxy(upstream.URL))
	post := httptest.NewRequest(http.MethodPost, "/api/swapd", strings.NewReader(`{"method":"refund"}`))
	postResult := httptest.NewRecorder()
	handler.ServeHTTP(postResult, post)
	if postResult.Code != http.StatusMethodNotAllowed || calls.Load() != 0 {
		t.Fatalf("POST status = %d, upstream calls = %d", postResult.Code, calls.Load())
	}

	get := httptest.NewRequest(http.MethodGet, "/api/swapd/anything?method=refund", nil)
	getResult := httptest.NewRecorder()
	handler.ServeHTTP(getResult, get)
	if getResult.Code != http.StatusOK || calls.Load() != 1 {
		t.Fatalf("GET status = %d, upstream calls = %d", getResult.Code, calls.Load())
	}
}

func TestFetchJSONPostUsesDaemonFlatJSON(t *testing.T) {
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			t.Errorf("method = %q, want POST", r.Method)
		}
		body, _ := io.ReadAll(r.Body)
		if string(body) != "{}" {
			t.Errorf("body = %q, want {}", body)
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"heat_supply":123,"status":"OK"}`))
	}))
	defer upstream.Close()

	result, err := fetchJSONPost(upstream.URL, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	if result["heat_supply"] != float64(123) {
		t.Fatalf("heat_supply = %v", result["heat_supply"])
	}
}

func TestOperatorMiddlewareRejectsCrossOriginAndSimplePosts(t *testing.T) {
	var reached atomic.Int32
	handler := corsMiddleware(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		reached.Add(1)
		w.WriteHeader(http.StatusNoContent)
	}))
	tests := []struct {
		name, host, origin, operator string
		want                         int
	}{
		{"same origin operator", "127.0.0.1:18918", "http://127.0.0.1:18918", "1", http.StatusNoContent},
		{"same origin without operator header", "127.0.0.1:18918", "http://127.0.0.1:18918", "", http.StatusForbidden},
		{"other loopback port", "127.0.0.1:18918", "http://127.0.0.1:3000", "1", http.StatusForbidden},
		{"other loopback host", "127.0.0.1:18918", "http://localhost:18918", "1", http.StatusForbidden},
		{"rebinding host", "attacker.example:18918", "http://attacker.example:18918", "1", http.StatusForbidden},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			req := httptest.NewRequest(http.MethodPost, "/api/wallet", nil)
			req.Host = tt.host
			if tt.origin != "" {
				req.Header.Set("Origin", tt.origin)
			}
			if tt.operator != "" {
				req.Header.Set("X-Fuego-Operator", tt.operator)
			}
			res := httptest.NewRecorder()
			handler.ServeHTTP(res, req)
			if res.Code != tt.want {
				t.Fatalf("status = %d, want %d", res.Code, tt.want)
			}
		})
	}
	if got := reached.Load(); got != 1 {
		t.Fatalf("handler reached %d times, want 1", got)
	}
}
