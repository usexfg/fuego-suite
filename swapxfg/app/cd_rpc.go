// swapxfg/app/cd_rpc.go
package app

import "fmt"

// CdOffer represents a CD sell/buy offer from the fuegod P2P relay.
type CdOffer struct {
	OfferID      string `json:"offerId"`
	IsSell       bool   `json:"isSell"`
	CdAmount     uint64 `json:"cdAmount"` // atomic units (HEAT)
	CdTerm       uint32 `json:"cdTerm"`   // epochs (6, 18, 36, 72)
	CdEpoch      uint32 `json:"cdEpoch"`
	CdKeyImage   string `json:"cdKeyImage"`
	AskPrice     uint64 `json:"askPrice"` // atomic units XFG
	MakerPubKey  string `json:"makerPubKey"`
	Timestamp    uint64 `json:"timestamp"`
	TTLBlocks    uint32 `json:"ttlBlocks"`
	PostedHeight uint32 `json:"postedHeight"`
}

// CdPriceStats holds yield-estimation stats for a specific CD amount tier,
// populated from the daemon's /estimate_cd_yield endpoint.
type CdPriceStats struct {
	Amount            uint64  `json:"cdAmount"`
	EstimatedInterest uint64  `json:"estimated_interest"` // atomic units, from /estimate_cd_yield
	EffectiveEpochs   uint64  `json:"effective_epochs"`
	MedianAsk         uint64  `json:"medianAsk"`
	DiscountPct       float64 `json:"discountPct"` // negative = below face value
	ActiveOffers      int     `json:"activeOffers"`
}

// CreateCdResponse is returned by the wallet create_cd JSON-RPC.
type CreateCdResponse struct {
	TxHash       string `json:"tx_hash"`
	DepositID    uint64 `json:"deposit_id"`
	UnlockHeight uint64 `json:"unlock_height"`
	Status       string `json:"status"`
}

// AcceptCdResponse is kept for backward compatibility with tui.go call sites.
// /acceptcd has no real daemon equivalent; calls return an error.
type AcceptCdResponse struct {
	PartialTx string `json:"partialTx"`
	ExpiresAt uint32 `json:"expiresAt"`
	Status    string `json:"status"`
}

// GetCdOffers fetches all CD offers (amount=0 means all tiers).
// NOTE: the daemon /getcdoffers endpoint does not yet exist; this returns
// an empty list until the P2P CD relay is implemented in fuegod.
func (c *FuegoClient) GetCdOffers(amount uint64) ([]CdOffer, error) {
	req := map[string]interface{}{"amount": amount}
	var resp struct {
		Offers []CdOffer `json:"offers"`
		Status string    `json:"status"`
	}
	if err := c.post("/getcdoffers", req, &resp); err != nil {
		// Endpoint does not yet exist — return the error so FetchAll can handle it
		return nil, err
	}
	return resp.Offers, nil
}

// GetCdPrice returns yield statistics for a CD amount tier by calling the
// daemon's real /estimate_cd_yield endpoint.
// The creation_height is set to 0 so the daemon uses the current height.
func (c *FuegoClient) GetCdPrice(amount uint64) (*CdPriceStats, error) {
	req := map[string]interface{}{
		"amount":          amount,
		"creation_height": 0,
		"current_height":  0,
	}
	var resp struct {
		EstimatedInterest uint64 `json:"estimated_interest"`
		EffectiveEpochs   uint64 `json:"effective_epochs"`
		Status            string `json:"status"`
	}
	if err := c.post("/estimate_cd_yield", req, &resp); err != nil {
		return nil, fmt.Errorf("estimate_cd_yield: %w", err)
	}
	return &CdPriceStats{
		Amount:            amount,
		EstimatedInterest: resp.EstimatedInterest,
		EffectiveEpochs:   resp.EffectiveEpochs,
	}, nil
}

// CreateCd creates a new certificate of deposit via the wallet's create_cd
// JSON-RPC method. The caller must pass a WalletClient for authentication.
func (w *WalletClient) CreateCd(amount uint64, term uint32) (*CreateCdResponse, error) {
	var outer struct {
		Result CreateCdResponse `json:"result"`
		Error  *struct {
			Message string `json:"message"`
		} `json:"error,omitempty"`
	}
	if err := w.fc.post("/json_rpc", map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  "create_cd",
		"params": map[string]interface{}{
			"amount": amount,
			"term":   term,
		},
		"id": 1,
	}, &outer); err != nil {
		return nil, fmt.Errorf("create_cd: %w", err)
	}
	if outer.Error != nil {
		return nil, fmt.Errorf("create_cd: %s", outer.Error.Message)
	}
	return &outer.Result, nil
}

// AcceptCdOffer attempts to accept a CD offer via the wallet's accept_cd
// JSON-RPC method. Returns an error if the daemon does not yet support this.
func (w *WalletClient) AcceptCdOffer(offerID string) (*AcceptCdResponse, error) {
	var outer struct {
		Result AcceptCdResponse `json:"result"`
		Error  *struct {
			Message string `json:"message"`
		} `json:"error,omitempty"`
	}
	if err := w.fc.post("/json_rpc", map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  "accept_cd",
		"params": map[string]interface{}{
			"offer_id": offerID,
		},
		"id": 1,
	}, &outer); err != nil {
		return nil, fmt.Errorf("accept_cd: %w", err)
	}
	if outer.Error != nil {
		return nil, fmt.Errorf("accept_cd: %s", outer.Error.Message)
	}
	return &outer.Result, nil
}

// SubmitCdOffer is not yet implemented on FuegoClient.
// Use WalletClient.CreateCd to lock XFG into a new CD instead.
func (c *FuegoClient) SubmitCdOffer(offer CdOffer) error {
	return fmt.Errorf("not yet implemented: /submitcd — use WalletClient.CreateCd (wallet create_cd RPC)")
}

// CancelCdOffer is not yet implemented — no real daemon or wallet equivalent
// for cancelling a CD offer exists.
func (c *FuegoClient) CancelCdOffer(offerID, pubKey, sig string) error {
	return fmt.Errorf("not yet implemented: /cancelcd — no real daemon equivalent")
}

// HEAT_LAUNCH_RATIO is the launch conversion: 1 HEAT = 10 XFG.
const HEAT_LAUNCH_RATIO uint64 = 10

// CdDiscount returns the discount percentage vs face value in XFG.
// cdAmount is in HEAT atomic units, askPrice is in XFG atomic units.
// Negative = selling below face value (discount), positive = premium.
func CdDiscount(cdAmount, askPrice uint64) float64 {
	if cdAmount == 0 {
		return 0
	}
	faceValueXfg := cdAmount * HEAT_LAUNCH_RATIO
	return (float64(askPrice) - float64(faceValueXfg)) / float64(faceValueXfg) * 100.0
}
