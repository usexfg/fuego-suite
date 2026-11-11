// swapxfg/app/external_prices.go
// Reference USD prices from DeFiLlama (context only — not used for swap execution).
package app

import (
	"encoding/json"
	"io"
	"net/http"
	"sync"
	"time"
)

// ExternalPrices holds reference USD spot prices fetched from DeFiLlama.
// These are for user context only; on-chain TWAP/composite rates drive swaps.
type ExternalPrices struct {
	XFG float64
	SOL float64
	ETH float64
	XMR float64
	BCH float64
	BNB float64
}

const (
	defiLlamaURL = "https://coins.llama.fi/prices/current/" +
		"coingecko:solana,coingecko:ethereum,coingecko:monero," +
		"coingecko:bitcoin-cash,coingecko:binancecoin"
	extPriceCacheTTL = 60 * time.Second
	extPriceTimeout  = 4 * time.Second
)

var (
	extPriceMu    sync.Mutex
	extPriceCache *ExternalPrices
	extPriceAt    time.Time
)

// GetExternalPrices returns cached DeFiLlama USD prices, refreshing if stale.
// Never blocks longer than extPriceTimeout; returns last-good or empty prices on failure.
func GetExternalPrices() *ExternalPrices {
	extPriceMu.Lock()
	defer extPriceMu.Unlock()

	if extPriceCache != nil && time.Since(extPriceAt) < extPriceCacheTTL {
		return extPriceCache
	}

	fetched := fetchDefiLlamaPrices()
	if fetched != nil {
		extPriceCache = fetched
		extPriceAt = time.Now()
		return extPriceCache
	}
	if extPriceCache != nil {
		return extPriceCache
	}
	return &ExternalPrices{}
}

type llamaResponse struct {
	Coins map[string]struct {
		Price  float64 `json:"price"`
		Symbol string  `json:"symbol"`
	} `json:"coins"`
}

func fetchDefiLlamaPrices() *ExternalPrices {
	client := &http.Client{Timeout: extPriceTimeout}
	resp, err := client.Get(defiLlamaURL)
	if err != nil {
		return nil
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil
	}
	body, err := io.ReadAll(io.LimitReader(resp.Body, 1<<20))
	if err != nil {
		return nil
	}
	var parsed llamaResponse
	if err := json.Unmarshal(body, &parsed); err != nil {
		return nil
	}
	out := &ExternalPrices{}
	if c, ok := parsed.Coins["coingecko:solana"]; ok {
		out.SOL = c.Price
	}
	if c, ok := parsed.Coins["coingecko:ethereum"]; ok {
		out.ETH = c.Price
	}
	if c, ok := parsed.Coins["coingecko:monero"]; ok {
		out.XMR = c.Price
	}
	if c, ok := parsed.Coins["coingecko:bitcoin-cash"]; ok {
		out.BCH = c.Price
	}
	if c, ok := parsed.Coins["coingecko:binancecoin"]; ok {
		out.BNB = c.Price
	}
	// XFG is not listed on DeFiLlama/CoinGecko; leave 0 so ticker shows "$—".
	return out
}
