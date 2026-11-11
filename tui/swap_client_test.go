package main

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"runtime"
	"strings"
	"testing"
)

type roundTripFunc func(*http.Request) (*http.Response, error)

func (fn roundTripFunc) RoundTrip(request *http.Request) (*http.Response, error) {
	return fn(request)
}

func rpcHTTPResponse(body string) *http.Response {
	return &http.Response{
		StatusCode: http.StatusOK,
		Header:     make(http.Header),
		Body:       io.NopCloser(strings.NewReader(body)),
	}
}

func TestParseSwapAmountsExactly(t *testing.T) {
	tests := []struct {
		input    string
		decimals int
		want     string
	}{
		{"1", 7, "10000000"},
		{"0.0000001", 7, "1"},
		{"18.446744073709551615", 18, "18446744073709551615"},
		{"12345678901234567890.123456789012345678", 18, "12345678901234567890123456789012345678"},
	}
	for _, test := range tests {
		got, err := parseCounterpartyAmount(test.input, test.decimals)
		if err != nil {
			t.Fatalf("parseCounterpartyAmount(%q): %v", test.input, err)
		}
		if got != test.want {
			t.Fatalf("parseCounterpartyAmount(%q) = %q, want %q", test.input, got, test.want)
		}
	}

	if got, err := parseXFGAmount("8000008.0000000"); err != nil || got != "80000080000000" {
		t.Fatalf("XFG amount mismatch: got %q, err %v", got, err)
	}
	for _, invalid := range []string{"0", "-1", "+1", "1e3", "1.00000001", " 1", "1 "} {
		if _, err := parseXFGAmount(invalid); err == nil {
			t.Errorf("parseXFGAmount accepted %q", invalid)
		}
	}
}

func TestNodeInfoDecodesNetworkForSafetyGate(t *testing.T) {
	var info nodeInfo
	if err := json.Unmarshal([]byte(`{"network":"testnet"}`), &info); err != nil {
		t.Fatal(err)
	}
	if info.Network != "testnet" {
		t.Fatalf("network = %q, want testnet", info.Network)
	}
}

func TestWalletReadinessRequiresSyncAndExpectedNode(t *testing.T) {
	base := model{runningW: true, runningNode: true, walletAddress: "address", nodeHeight: 100}
	updated, _ := base.handleRpcResult(rpcResultMsg{
		method: "getStatus",
		result: map[string]interface{}{"blockCount": json.Number("97"), "knownBlockCount": json.Number("100")},
	})
	if updated.(model).walletReady {
		t.Fatal("wallet three blocks behind was marked ready")
	}
	updated, _ = base.handleRpcResult(rpcResultMsg{
		method: "getStatus",
		result: map[string]interface{}{"blockCount": json.Number("98"), "knownBlockCount": json.Number("100")},
	})
	if !updated.(model).walletReady {
		t.Fatal("wallet within two blocks was not marked ready")
	}
	base.runningNode = false
	updated, _ = base.handleRpcResult(rpcResultMsg{
		method: "getStatus",
		result: map[string]interface{}{"blockCount": json.Number("100"), "knownBlockCount": json.Number("100")},
	})
	if updated.(model).walletReady {
		t.Fatal("wallet without a verified node was marked ready")
	}
}

func TestCoinAmountParsingNeverUsesFloatingPoint(t *testing.T) {
	amount, err := parseCoinAmount("1844674407370.9551615")
	if err != nil || amount != ^uint64(0) {
		t.Fatalf("max amount = %d, err %v", amount, err)
	}
	for _, invalid := range []string{"0", "1e3", "0.00000001", "1844674407370.9551616"} {
		if _, err := parseCoinAmount(invalid); err == nil {
			t.Errorf("parseCoinAmount accepted %q", invalid)
		}
	}
}

func TestSwapIdentityValidation(t *testing.T) {
	if err := validatePeerEndpoint("maker.example:18901"); err != nil {
		t.Fatal(err)
	}
	if err := validateExpectedPeerKey(strings.Repeat("ab", 32)); err != nil {
		t.Fatal(err)
	}
	for _, endpoint := range []string{"", "http://maker:18901", "maker", "maker:0", "maker:70000", "::1:18901"} {
		if err := validatePeerEndpoint(endpoint); err == nil {
			t.Errorf("validatePeerEndpoint accepted %q", endpoint)
		}
	}
	for _, key := range []string{"", strings.Repeat("0", 64), strings.Repeat("gg", 32), strings.Repeat("ab", 31)} {
		if err := validateExpectedPeerKey(key); err == nil {
			t.Errorf("validateExpectedPeerKey accepted %q", key)
		}
	}
}

func TestSwapControlTokenValidation(t *testing.T) {
	valid := strings.Repeat("a", 64)
	if got, err := validateSwapControlToken(valid + "\n"); err != nil || got != valid {
		t.Fatalf("valid token rejected: %q, %v", got, err)
	}
	for _, token := range []string{"", strings.Repeat("a", 31), strings.Repeat("a", 257), strings.Repeat("a", 63) + " "} {
		if _, err := validateSwapControlToken(token); err == nil {
			t.Errorf("invalid control token accepted: %q", token)
		}
	}
}

func TestSwapClientRefusesTokenlessMutationBeforeNetwork(t *testing.T) {
	calls := 0
	client := NewSwapDaemonClient(18902, "", "mainnet")
	client.httpClient = &http.Client{Transport: roundTripFunc(func(_ *http.Request) (*http.Response, error) {
		calls++
		return rpcHTTPResponse(`{"jsonrpc":"2.0","result":{},"id":1}`), nil
	})}

	var result map[string]any
	err := client.call(context.Background(), "check_timeouts", map[string]any{}, &result, true)
	if err == nil || !strings.Contains(err.Error(), "control token is required") {
		t.Fatalf("expected missing-control-token error, got %v", err)
	}
	if calls != 0 {
		t.Fatalf("tokenless mutation made %d network calls", calls)
	}
}

func TestSwapClientRefusesTokenlessHighLevelMutationsBeforeNetwork(t *testing.T) {
	calls := 0
	client := NewSwapDaemonClient(18902, "", "mainnet")
	client.httpClient = &http.Client{Transport: roundTripFunc(func(_ *http.Request) (*http.Response, error) {
		calls++
		return rpcHTTPResponse(`{"jsonrpc":"2.0","result":{},"id":1}`), nil
	})}

	if _, err := client.Accept(context.Background(), "swap-1"); err == nil ||
		!strings.Contains(err.Error(), "control token is required") {
		t.Fatalf("expected missing-control-token error for accept, got %v", err)
	}
	if _, err := client.Refund(context.Background(), "swap-1"); err == nil ||
		!strings.Contains(err.Error(), "control token is required") {
		t.Fatalf("expected missing-control-token error for refund, got %v", err)
	}
	if _, err := client.Initiate(context.Background(), SwapInitiateRequest{
		Pair:               "eth",
		Role:               "alice",
		XFGAmount:          "1",
		CounterpartyAmount: "1",
		Peer:               "maker.example:18901",
		ExpectedPeerKey:    strings.Repeat("ab", 32),
	}); err == nil || !strings.Contains(err.Error(), "control token is required") {
		t.Fatalf("expected missing-control-token error for initiate, got %v", err)
	}
	if calls != 0 {
		t.Fatalf("tokenless high-level mutation made %d network calls", calls)
	}
}

func TestSwapControlTokenFileRequiresOwnerOnlyRegularFile(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("owner-only file verification is unavailable on Windows")
	}
	file, err := os.CreateTemp("", "xfg-swapd-token-")
	if err != nil {
		t.Fatal(err)
	}
	path := file.Name()
	defer os.Remove(path)
	if _, err := file.WriteString(strings.Repeat("b", 64) + "\n"); err != nil {
		t.Fatal(err)
	}
	if err := file.Close(); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(path, 0o600); err != nil {
		t.Fatal(err)
	}
	if token, err := readSwapControlTokenFile(path); err != nil || token != strings.Repeat("b", 64) {
		t.Fatalf("owner-only token file rejected: %q, %v", token, err)
	}
	if err := os.Chmod(path, 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := readSwapControlTokenFile(path); err == nil {
		t.Fatal("world-readable token file accepted")
	}
}

func TestSwapClientInitiateUsesRuntimeReadinessAndStringAmounts(t *testing.T) {
	methods := make([]string, 0, 3)
	client := NewSwapDaemonClient(18902, "secret", "testnet")
	client.httpClient = &http.Client{Transport: roundTripFunc(func(request *http.Request) (*http.Response, error) {
		if request.Header.Get("X-Swap-Token") != "secret" {
			t.Fatal("missing swap token")
		}
		var call struct {
			Method string         `json:"method"`
			Params map[string]any `json:"params"`
		}
		decoder := json.NewDecoder(request.Body)
		decoder.UseNumber()
		if err := decoder.Decode(&call); err != nil {
			t.Fatal(err)
		}
		methods = append(methods, call.Method)
		switch call.Method {
		case "status":
			return rpcHTTPResponse(`{"jsonrpc":"2.0","result":{"network":"testnet","profile":"testnet"},"id":1}`), nil
		case "list_chains":
			return rpcHTTPResponse(`{"jsonrpc":"2.0","result":{"chains":[{"id":1,"key":"eth","symbol":"ETH","assetTicker":"ETH","name":"Ethereum","family":"EVM","chainId":11155111,"decimals":18,"implementation":"adapter","protocol":true,"configured":true,"ready":true,"readinessError":""}]},"id":1}`), nil
		case "initiate_swap":
			if _, ok := call.Params["xfg_amount"].(string); !ok {
				t.Fatalf("xfg_amount type is %T", call.Params["xfg_amount"])
			}
			if _, ok := call.Params["ctr_amount"].(string); !ok {
				t.Fatalf("ctr_amount type is %T", call.Params["ctr_amount"])
			}
			return rpcHTTPResponse(`{"jsonrpc":"2.0","result":{"swap_id":"abc123","our_swap_pubkey":"0123"},"id":1}`), nil
		default:
			t.Fatalf("unexpected RPC method %q", call.Method)
			return nil, nil
		}
	})}

	result, err := client.Initiate(context.Background(), SwapInitiateRequest{
		Pair:               "eth",
		Role:               "alice",
		XFGAmount:          "10000000",
		CounterpartyAmount: "1000000000000000000",
		Peer:               "maker.example:18901",
		ExpectedPeerKey:    strings.Repeat("ab", 32),
	})
	if err != nil {
		t.Fatal(err)
	}
	if result.SwapID != "abc123" {
		t.Fatalf("swap id = %q", result.SwapID)
	}
	if strings.Join(methods, ",") != "status,list_chains,initiate_swap" {
		t.Fatalf("RPC order = %v", methods)
	}
}

func TestSwapClientRejectsAliasAndInvalidInputsBeforeMutation(t *testing.T) {
	client := NewSwapDaemonClient(18902, strings.Repeat("a", 64), "mainnet")
	methods := make([]string, 0, 2)
	client.httpClient = &http.Client{Transport: roundTripFunc(func(request *http.Request) (*http.Response, error) {
		var call struct {
			Method string `json:"method"`
		}
		if err := json.NewDecoder(request.Body).Decode(&call); err != nil {
			t.Fatal(err)
		}
		methods = append(methods, call.Method)
		switch call.Method {
		case "status":
			return rpcHTTPResponse(`{"jsonrpc":"2.0","result":{"network":"mainnet","profile":"mainnet"},"id":1}`), nil
		case "list_chains":
			return rpcHTTPResponse(`{"jsonrpc":"2.0","result":{"chains":[{"id":1,"key":"eth","symbol":"ETH","assetTicker":"ETH","name":"Ethereum","family":"EVM","chainId":1,"decimals":18,"implementation":"adapter","protocol":true,"configured":true,"ready":true,"readinessError":""}]},"id":1}`), nil
		default:
			t.Fatalf("unexpected method %q", call.Method)
			return nil, nil
		}
	})}
	request := SwapInitiateRequest{
		Pair:               "ETH",
		Role:               "alice",
		XFGAmount:          "1",
		CounterpartyAmount: "1",
		Peer:               "maker.example:18901",
		ExpectedPeerKey:    strings.Repeat("ab", 32),
	}
	if _, err := client.Initiate(context.Background(), request); err == nil {
		t.Fatal("uppercase alias should not be accepted without runtime lookup")
	}
	if strings.Join(methods, ",") != "status,list_chains" {
		t.Fatalf("canonical-key check called %v", methods)
	}

	methods = nil
	request.Pair = "eth"
	request.CounterpartyAmount = "0"
	if _, err := client.Initiate(context.Background(), request); err == nil {
		t.Fatal("zero counterparty amount accepted")
	}
	if len(methods) != 0 {
		t.Fatalf("invalid amount made RPC calls: %v", methods)
	}
}

func TestSwapClientRejectsWrongNetworkBeforeMutation(t *testing.T) {
	calls := 0
	client := NewSwapDaemonClient(18902, strings.Repeat("a", 64), "testnet")
	client.httpClient = &http.Client{Transport: roundTripFunc(func(request *http.Request) (*http.Response, error) {
		calls++
		return rpcHTTPResponse(`{"jsonrpc":"2.0","result":{"network":"mainnet","profile":"mainnet"},"id":1}`), nil
	})}

	_, err := client.Accept(context.Background(), "swap-1")
	if err == nil || !strings.Contains(err.Error(), "context mismatch") {
		t.Fatalf("expected context mismatch, got %v", err)
	}
	if calls != 1 {
		t.Fatalf("wrong-network request made %d calls", calls)
	}
}

func TestSwapClientReportsUnknownMutationOutcome(t *testing.T) {
	call := 0
	client := NewSwapDaemonClient(18902, strings.Repeat("a", 64), "mainnet")
	client.httpClient = &http.Client{Transport: roundTripFunc(func(request *http.Request) (*http.Response, error) {
		call++
		if call == 1 {
			return rpcHTTPResponse(`{"jsonrpc":"2.0","result":{"network":"mainnet","profile":"mainnet"},"id":1}`), nil
		}
		return nil, errors.New("connection reset")
	})}

	_, err := client.Refund(context.Background(), "swap-1")
	if err == nil || !strings.Contains(err.Error(), "outcome is unknown") {
		t.Fatalf("expected unknown-outcome error, got %v", err)
	}
}

func TestSwapClientDoesNotForwardControlTokenAcrossRedirect(t *testing.T) {
	redirectTargetCalls := make(chan struct{}, 1)
	target := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		redirectTargetCalls <- struct{}{}
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{"jsonrpc":"2.0","result":{},"id":1}`))
	}))
	defer target.Close()

	redirect := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Redirect(w, r, target.URL, http.StatusTemporaryRedirect)
	}))
	defer redirect.Close()

	client := NewSwapDaemonClient(18902, "secret", "mainnet")
	client.rpcURL = redirect.URL
	var result map[string]any
	err := client.call(context.Background(), "accept", map[string]string{"swap_id": "swap-1"}, &result, true)
	if err == nil || !strings.Contains(err.Error(), "outcome is unknown") {
		t.Fatalf("expected unknown mutation outcome after redirect refusal, got %v", err)
	}
	select {
	case <-redirectTargetCalls:
		t.Fatal("control request followed redirect")
	default:
	}
}
