package main

import (
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
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

func TestSwapdRPCProxyAllowlistAndToken(t *testing.T) {
	const token = "server-side-secret"
	var calls atomic.Int32
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
		if r.URL.Path != "/" {
			t.Errorf("path = %q, want /", r.URL.Path)
		}
		if got := r.Header.Get("X-Swap-Token"); got != token {
			t.Errorf("token = %q, want injected token", got)
		}
		body, _ := io.ReadAll(r.Body)
		if !strings.Contains(string(body), `"method":"list_chains"`) {
			t.Errorf("unexpected normalized body: %s", body)
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"jsonrpc":"2.0","result":{"chains":[]},"id":1}`))
	}))
	defer upstream.Close()

	handler := localRPCProxyHandler(localRPCProxyConfig{
		port: testServerPort(t, upstream.URL), path: "/", service: "xfg-swapd",
		token: token, allowed: swapdAllowedMethods, limiter: newRateLimiter(time.Nanosecond),
	})
	req := httptest.NewRequest(http.MethodPost, "/api/swapd-rpc",
		strings.NewReader(`{"jsonrpc":"2.0","id":1,"method":"list_chains","params":{}}`))
	res := httptest.NewRecorder()
	handler.ServeHTTP(res, req)

	if res.Code != http.StatusOK {
		t.Fatalf("status = %d, body = %s", res.Code, res.Body.String())
	}
	if calls.Load() != 1 {
		t.Fatalf("upstream calls = %d, want 1", calls.Load())
	}
	if strings.Contains(res.Body.String(), token) {
		t.Fatal("control token leaked into proxy response")
	}

	forbidden := httptest.NewRequest(http.MethodPost, "/api/swapd-rpc",
		strings.NewReader(`{"jsonrpc":"2.0","id":2,"method":"not_allowed","params":{}}`))
	forbiddenRes := httptest.NewRecorder()
	handler.ServeHTTP(forbiddenRes, forbidden)
	if forbiddenRes.Code != http.StatusForbidden {
		t.Fatalf("forbidden status = %d, want 403", forbiddenRes.Code)
	}
	if calls.Load() != 1 {
		t.Fatal("forbidden method reached upstream")
	}
}
