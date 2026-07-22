// swapxfg/app/pricefeed.go
//
// External price feed for display context only.
// These are REFERENCE prices — the actual swap rate comes from the daemon's
// on-chain TWAP (time-weighted average price) computed from completed swaps.
// Users should treat DeFiLlama prices as a starting point for their own
// pricing decisions, not as the authoritative swap rate.
package app

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sync"
	"time"
)

// ExternalPrices holds reference USD prices for all swap pair assets.
// These come from DeFiLlama (on-chain DEX pool aggregator) and are
// display-only — NOT used for swap matching or rate validation.
type ExternalPrices struct {
	XFG  float64
	SOL  float64
	ETH  float64
	XMR  float64
	BCH  float64
	BNB  float64
	ARB  float64 // uses ETH price
	BASE float64 // uses ETH price
	At   time.Time
}

var (
	externalPrices     *ExternalPrices
	externalPricesMu   sync.RWMutex
	externalPricesTime time.Time
)

const priceFeedTTL = 60 * time.Second

// PriceFeedSource returns a human-readable label for where reference prices come from.
func PriceFeedSource() string {
	return "DeFiLlama (DEX aggregator)"
}

// GetExternalPrices returns cached DeFiLlama prices or fetches fresh ones.
// These are REFERENCE prices for display only — the daemon's on-chain TWAP
// is the authoritative rate for swap matching.
func GetExternalPrices() *ExternalPrices {
	externalPricesMu.RLock()
	if externalPrices != nil && time.Since(externalPricesTime) < priceFeedTTL {
		defer externalPricesMu.RUnlock()
		return externalPrices
	}
	externalPricesMu.RUnlock()

	externalPricesMu.Lock()
	defer externalPricesMu.Unlock()

	// Double-check after acquiring write lock
	if externalPrices != nil && time.Since(externalPricesTime) < priceFeedTTL {
		return externalPrices
	}

	prices := fetchDefiLlamaPrices()
	if prices != nil {
		prices.ARB = prices.ETH // ARB ≈ ETH on L2
		prices.BASE = prices.ETH
		externalPrices = prices
		externalPricesTime = time.Now()
	}
	return externalPrices
}

// ── DeFiLlama — on-chain DEX pool price aggregator ──

type defiLlamaResponse struct {
	Coins map[string]defiLlamaCoin `json:"coins"`
}

type defiLlamaCoin struct {
	Price     float64 `json:"price"`
	Confidence float64 `json:"confidence"`
}

func fetchDefiLlamaPrices() *ExternalPrices {
	coins := "coingecko:solana,coingecko:ethereum,coingecko:monero,coingecko:bitcoin-cash,coingecko:binancecoin"
	url := fmt.Sprintf("https://coins.llama.fi/prices/current/%s", coins)

	resp, err := http.Get(url)
	if err != nil {
		return nil
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil
	}

	var data defiLlamaResponse
	if err := json.Unmarshal(body, &data); err != nil {
		return nil
	}

	prices := &ExternalPrices{At: time.Now()}

	if c, ok := data.Coins["coingecko:solana"]; ok {
		prices.SOL = c.Price
	}
	if c, ok := data.Coins["coingecko:ethereum"]; ok {
		prices.ETH = c.Price
	}
	if c, ok := data.Coins["coingecko:monero"]; ok {
		prices.XMR = c.Price
	}
	if c, ok := data.Coins["coingecko:bitcoin-cash"]; ok {
		prices.BCH = c.Price
	}
	if c, ok := data.Coins["coingecko:binancecoin"]; ok {
		prices.BNB = c.Price
	}

	if prices.SOL == 0 && prices.ETH == 0 {
		return nil
	}

	return prices
}
