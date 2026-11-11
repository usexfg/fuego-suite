// swapxfg/app/bch_rpc.go
// BchClient: Electron Cash JSON-RPC client for BCH HTLC operations.
// Electron Cash exposes a standard Bitcoin-compatible JSON-RPC on :7773.
package app

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sync/atomic"
	"time"
)

// BchClient talks to Electron Cash via JSON-RPC.
type BchClient struct {
	endpoint string
	client   *http.Client
	idSeq    uint64
}

// NewBchClient returns a BchClient for the given endpoint (e.g. "http://127.0.0.1:7773").
func NewBchClient(endpoint string) *BchClient {
	return &BchClient{
		endpoint: endpoint,
		client:   &http.Client{Timeout: 15 * time.Second},
	}
}

type bchRpcRequest struct {
	Jsonrpc string        `json:"jsonrpc"`
	Method  string        `json:"method"`
	Params  []interface{} `json:"params"`
	ID      uint64        `json:"id"`
}

type bchRpcResponse struct {
	Result json.RawMessage `json:"result"`
	Error  *struct {
		Code    int    `json:"code"`
		Message string `json:"message"`
	} `json:"error"`
	ID uint64 `json:"id"`
}

func (c *BchClient) call(method string, params []interface{}, result interface{}) error {
	id := atomic.AddUint64(&c.idSeq, 1)
	reqBody, _ := json.Marshal(bchRpcRequest{
		Jsonrpc: "1.0",
		Method:  method,
		Params:  params,
		ID:      id,
	})

	resp, err := c.client.Post(c.endpoint, "application/json", bytes.NewReader(reqBody))
	if err != nil {
		return fmt.Errorf("bch rpc %s: %w", method, err)
	}
	defer resp.Body.Close()

	raw, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("bch read %s: %w", method, err)
	}

	var rpcResp bchRpcResponse
	if err := json.Unmarshal(raw, &rpcResp); err != nil {
		return fmt.Errorf("bch decode %s: %w", method, err)
	}
	if rpcResp.Error != nil {
		return fmt.Errorf("bch %s error %d: %s", method, rpcResp.Error.Code, rpcResp.Error.Message)
	}
	if result == nil {
		return nil
	}
	return json.Unmarshal(rpcResp.Result, result)
}

// BchBalance holds confirmed and unconfirmed balances in satoshis.
type BchBalance struct {
	Confirmed   int64
	Unconfirmed int64
}

// GetBalance returns the wallet's BCH balance in satoshis.
func (c *BchClient) GetBalance() (*BchBalance, error) {
	// Electron Cash returns {"confirmed": "0.05", "unconfirmed": "0.00"} as BCH strings.
	var result struct {
		Confirmed   json.RawMessage `json:"confirmed"`
		Unconfirmed json.RawMessage `json:"unconfirmed"`
	}
	if err := c.call("getbalance", nil, &result); err != nil {
		return nil, err
	}

	parseSat := func(raw json.RawMessage) int64 {
		// Could be a float string ("0.05") or a number
		var f float64
		if err := json.Unmarshal(raw, &f); err == nil {
			return int64(f * 1e8)
		}
		var s string
		if err := json.Unmarshal(raw, &s); err == nil {
			var fs float64
			if _, err2 := fmt.Sscanf(s, "%f", &fs); err2 == nil {
				return int64(fs * 1e8)
			}
		}
		return 0
	}

	return &BchBalance{
		Confirmed:   parseSat(result.Confirmed),
		Unconfirmed: parseSat(result.Unconfirmed),
	}, nil
}

// GetNewAddress returns a fresh BCH P2PKH address from the wallet.
func (c *BchClient) GetNewAddress() (string, error) {
	var addr string
	return addr, c.call("getnewaddress", nil, &addr)
}

// PayTo creates a signed BCH transaction paying addr the given amount (in BCH, e.g. "0.01").
// Returns the raw transaction hex. Does not broadcast.
func (c *BchClient) PayTo(addr, amountBCH string) (string, error) {
	var txHex string
	return txHex, c.call("payto", []interface{}{addr, amountBCH}, &txHex)
}

// BroadcastTx broadcasts a raw hex transaction. Returns the txid.
func (c *BchClient) BroadcastTx(rawHex string) (string, error) {
	var txid string
	return txid, c.call("broadcast", []interface{}{rawHex}, &txid)
}

// GetAddressUnspent returns UTXOs for the given address.
type BchUtxo struct {
	TxHash string `json:"tx_hash"`
	TxPos  int    `json:"tx_pos"`
	Value  int64  `json:"value"` // satoshis
	Height int    `json:"height"`
}

func (c *BchClient) GetAddressUnspent(addr string) ([]BchUtxo, error) {
	var utxos []BchUtxo
	return utxos, c.call("getaddressunspent", []interface{}{addr}, &utxos)
}

// GetRawTransaction fetches a raw transaction hex by txid.
func (c *BchClient) GetRawTransaction(txid string) (string, error) {
	var raw string
	return raw, c.call("gettransaction", []interface{}{txid, false}, &raw)
}

// GetBlockCount returns the current BCH chain height.
func (c *BchClient) GetBlockCount() (int, error) {
	var height int
	return height, c.call("getblockcount", nil, &height)
}

// IsConnected pings Electron Cash via getblockcount. Returns false on error.
func (c *BchClient) IsConnected() bool {
	_, err := c.GetBlockCount()
	return err == nil
}

// FormatBchBalance renders a BchBalance as a human-readable string.
// Shows confirmed BCH; appends unconfirmed if non-zero.
func FormatBchBalance(bal *BchBalance) string {
	conf := float64(bal.Confirmed) / 1e8
	if bal.Unconfirmed != 0 {
		unconf := float64(bal.Unconfirmed) / 1e8
		return fmt.Sprintf("%.8f BCH (+%.8f unconf)", conf, unconf)
	}
	return fmt.Sprintf("%.8f BCH", conf)
}
