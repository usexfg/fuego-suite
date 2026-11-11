// swapxfg/app/bch_rpc_test.go
package app

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
)

func bchMockServer(t *testing.T, responses map[string]interface{}) *httptest.Server {
	t.Helper()
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var req bchRpcRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, "bad request", 400)
			return
		}
		result, ok := responses[req.Method]
		if !ok {
			json.NewEncoder(w).Encode(bchRpcResponse{
				Error: &struct {
					Code    int    `json:"code"`
					Message string `json:"message"`
				}{Code: -32601, Message: "method not found"},
			})
			return
		}
		raw, _ := json.Marshal(result)
		json.NewEncoder(w).Encode(map[string]interface{}{
			"result": json.RawMessage(raw),
			"error":  nil,
			"id":     req.ID,
		})
	}))
}

func TestBchGetBalance(t *testing.T) {
	srv := bchMockServer(t, map[string]interface{}{
		"getbalance": map[string]interface{}{
			"confirmed":   "0.05000000",
			"unconfirmed": "0.00",
		},
	})
	defer srv.Close()

	c := NewBchClient(srv.URL)
	bal, err := c.GetBalance()
	if err != nil {
		t.Fatalf("GetBalance: %v", err)
	}
	if bal.Confirmed != 5_000_000 {
		t.Errorf("confirmed: got %d, want 5000000", bal.Confirmed)
	}
	if bal.Unconfirmed != 0 {
		t.Errorf("unconfirmed: got %d, want 0", bal.Unconfirmed)
	}
}

func TestBchGetBalanceUnconfirmed(t *testing.T) {
	srv := bchMockServer(t, map[string]interface{}{
		"getbalance": map[string]interface{}{
			"confirmed":   0.02,
			"unconfirmed": 0.005,
		},
	})
	defer srv.Close()

	c := NewBchClient(srv.URL)
	bal, err := c.GetBalance()
	if err != nil {
		t.Fatalf("GetBalance: %v", err)
	}
	if bal.Confirmed != 2_000_000 {
		t.Errorf("confirmed: got %d, want 2000000", bal.Confirmed)
	}
	if bal.Unconfirmed != 500_000 {
		t.Errorf("unconfirmed: got %d, want 500000", bal.Unconfirmed)
	}
}

func TestBchBroadcastTx(t *testing.T) {
	wantTxid := "abc123def456"
	srv := bchMockServer(t, map[string]interface{}{
		"broadcast": wantTxid,
	})
	defer srv.Close()

	c := NewBchClient(srv.URL)
	txid, err := c.BroadcastTx("deadbeef")
	if err != nil {
		t.Fatalf("BroadcastTx: %v", err)
	}
	if txid != wantTxid {
		t.Errorf("txid: got %q, want %q", txid, wantTxid)
	}
}

func TestBchErrorResponse(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		json.NewEncoder(w).Encode(map[string]interface{}{
			"result": nil,
			"error": map[string]interface{}{
				"code":    -1,
				"message": "wallet locked",
			},
			"id": 1,
		})
	}))
	defer srv.Close()

	c := NewBchClient(srv.URL)
	_, err := c.GetBalance()
	if err == nil {
		t.Fatal("expected error, got nil")
	}
	if err.Error() == "" {
		t.Error("expected non-empty error message")
	}
}

func TestBchIsConnected(t *testing.T) {
	srv := bchMockServer(t, map[string]interface{}{
		"getblockcount": 800_000,
	})
	defer srv.Close()

	c := NewBchClient(srv.URL)
	if !c.IsConnected() {
		t.Error("expected IsConnected() = true")
	}
}

func TestBchIsConnectedFail(t *testing.T) {
	c := NewBchClient("http://127.0.0.1:19999") // nothing listening
	if c.IsConnected() {
		t.Error("expected IsConnected() = false for unreachable server")
	}
}

func TestFormatBchBalance(t *testing.T) {
	cases := []struct {
		bal  BchBalance
		want string
	}{
		{BchBalance{5_000_000, 0}, "0.05000000 BCH"},
		{BchBalance{100_000_000, 500_000}, "1.00000000 BCH (+0.00500000 unconf)"},
	}
	for _, tc := range cases {
		got := FormatBchBalance(&tc.bal)
		if got != tc.want {
			t.Errorf("FormatBchBalance(%+v) = %q, want %q", tc.bal, got, tc.want)
		}
	}
}
