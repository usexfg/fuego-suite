// swapxfg/app/wallet.go
package app

import "fmt"

// WalletClient talks to the local fire_wallet JSON-RPC.
// Reuses FuegoClient's HTTP machinery pointed at the wallet endpoint.
type WalletClient struct {
	fc *FuegoClient
}

func NewWalletClient(endpoint string) *WalletClient {
	return &WalletClient{fc: NewFuegoClient(endpoint)}
}

// NewWalletClientAuth creates a WalletClient that attaches HTTP Basic Auth to
// every request, matching fire_wallet's --rpc-user / --rpc-password flags.
func NewWalletClientAuth(endpoint, username, password string) *WalletClient {
	return &WalletClient{fc: NewFuegoClientAuth(endpoint, username, password)}
}

// ── Response types ─────────────────────────────────────────────────────

type WalletBalance struct {
	Available uint64 `json:"available_balance"`
	Locked    uint64 `json:"locked_amount"`
}

type SignedOffer struct {
	OfferID     string `json:"offerId"`
	MakerPubKey string `json:"makerPubKey"`
	Signature   string `json:"signature"`
	Timestamp   uint64 `json:"timestamp"`
}

type SwapInitResult struct {
	SwapID        string `json:"swapId"`
	OurPubKey     string `json:"ourPubKey"`
	Nonce0        string `json:"nonce0"`
	Nonce1        string `json:"nonce1"`
	EscrowKey     string `json:"escrowKey"`
	AdaptorPoint  string `json:"adaptorPoint"`
	DleqChallenge string `json:"dleqChallenge"`
	DleqResponse  string `json:"dleqResponse"`
}

type AfkLockResult struct {
	LockID       string `json:"lockId"`
	AdaptorPoint string `json:"adaptorPoint"`
	PreSig       string `json:"preSig"`
	Status       string `json:"status"`
}

// ── Methods ────────────────────────────────────────────────────────────

func (w *WalletClient) GetBalance() (*WalletBalance, error) {
	var outer struct {
		Result WalletBalance `json:"result"`
	}
	if err := w.fc.post("/json_rpc", map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  "getbalance",
		"params":  map[string]interface{}{},
		"id":      1,
	}, &outer); err != nil {
		return nil, err
	}
	return &outer.Result, nil
}

func (w *WalletClient) CreateAfkLock(amount uint64, timeoutHrs uint32, pair uint8) (*AfkLockResult, error) {
	var outer struct {
		Result AfkLockResult `json:"result"`
	}
	if err := w.fc.post("/json_rpc", map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  "create_afk_lock",
		"params": map[string]interface{}{
			"amount":        amount,
			"timeout_hours": timeoutHrs,
			"pair":          pair,
		},
		"id": 1,
	}, &outer); err != nil {
		return nil, err
	}
	return &outer.Result, nil
}

func (w *WalletClient) GetAddress() (string, error) {
	var outer struct {
		Result struct {
			Address string `json:"address"`
		} `json:"result"`
	}
	if err := w.fc.post("/json_rpc", map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  "get_address",
		"params":  map[string]interface{}{},
		"id":      1,
	}, &outer); err != nil {
		return "", err
	}
	return outer.Result.Address, nil
}

func (w *WalletClient) SignOffer(xfgAmount, rateNum uint64, pair uint8, ttlBlocks uint32, isSell bool) (*SignedOffer, error) {
	var outer struct {
		Result SignedOffer `json:"result"`
	}
	if err := w.fc.post("/json_rpc", map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  "sign_offer",
		"params": map[string]interface{}{
			"xfgAmount": xfgAmount,
			"rateNum":   rateNum,
			"pair":      pair,
			"ttlBlocks": ttlBlocks,
			"isSell":    isSell,
		},
		"id": 1,
	}, &outer); err != nil {
		return nil, err
	}
	return &outer.Result, nil
}

func (w *WalletClient) InitiateSwap(xfgAmount uint64, peerPubKey, pair, role string) (*SwapInitResult, error) {
	var outer struct {
		Result SwapInitResult `json:"result"`
	}
	if err := w.fc.post("/json_rpc", map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  "initiate_swap",
		"params": map[string]interface{}{
			"xfgAmount":  xfgAmount,
			"peerPubKey": peerPubKey,
			"pair":       pair,
			"role":       role,
		},
		"id": 1,
	}, &outer); err != nil {
		return nil, err
	}
	return &outer.Result, nil
}

// IsConnected performs a lightweight check by calling get_address.
func (w *WalletClient) IsConnected() bool {
	_, err := w.GetAddress()
	return err == nil
}

// ── Helpers ────────────────────────────────────────────────────────────

// FormatBalance converts atomic units (7 decimals) to XFG display string.
func FormatBalance(atomic uint64) string {
	return fmt.Sprintf("%.7f", float64(atomic)/1e7)
}
