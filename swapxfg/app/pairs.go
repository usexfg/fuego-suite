package app

import (
	"fmt"
	"sort"
	"strings"
	"sync"
)

// Pair IDs match fuegod's SwapPair enum (SwapTypes.h).
const (
	PairSOL    uint8 = 0
	PairETH    uint8 = 1
	PairXMR    uint8 = 2
	PairBCH    uint8 = 3
	PairARB    uint8 = 4
	PairBASE   uint8 = 5
	PairKMD    uint8 = 6 // KMD_SPV (daemon-side only)
	PairBNB    uint8 = 7
	PairCD     uint8 = 99  // not a swap pair — CD/XFG secondary market mode
	PairDaemon uint8 = 100 // daemon status monitoring view
)

// defaultPairs is the offline fallback order used before the swapd catalog
// arrives (legacy 7-pair table, SOL first).
var defaultPairs = []uint8{PairSOL, PairETH, PairXMR, PairBCH, PairARB, PairBASE, PairBNB}

// ── Dynamic chain catalog (from xfg-swapd status) ──
//
// The swap daemon's status endpoint (loopback, StatusPort) publishes the full
// pair catalog — every pair id with protocol/configured/ready/readinessError —
// in the "chains" array (SwapDaemon::buildChainCatalogJson). The TUI adopts
// that catalog so it shows every protocol pair, not just the legacy seven;
// without a reachable swapd it falls back to defaultPairs.

// ChainCatalogEntry mirrors one row of SwapDaemon::buildChainCatalogJson.
type ChainCatalogEntry struct {
	Id             uint8  `json:"id"`
	Key            string `json:"key"`
	Symbol         string `json:"symbol"`
	Name           string `json:"name"`
	Family         string `json:"family"`
	Protocol       bool   `json:"protocol"`
	Configured     bool   `json:"configured"`
	Ready          bool   `json:"ready"`
	ReadinessError string `json:"readinessError"`
}

var (
	swapChainsMu sync.RWMutex
	swapChains   = map[uint8]ChainCatalogEntry{}
	pairOrder    = append([]uint8(nil), defaultPairs...)
	catalogFp    = ""
)

// ApplySwapChains adopts the daemon's chain catalog: all protocol pairs become
// the active pair list (legacy order first, remaining ids ascending), and
// per-pair readiness is cached for labels and the swap guard. No-ops when the
// payload is empty or has not changed.
func ApplySwapChains(entries []ChainCatalogEntry) {
	fresh := make(map[uint8]ChainCatalogEntry, len(entries))
	for _, e := range entries {
		if !e.Protocol || e.Key == "" {
			continue
		}
		fresh[e.Id] = e
	}
	// Never shrink back to fewer pairs than the fallback table.
	if len(fresh) == 0 {
		return
	}

	order := make([]uint8, 0, len(fresh))
	seen := make(map[uint8]bool, len(fresh))
	for _, p := range defaultPairs {
		if e, ok := fresh[p]; ok && e.Protocol {
			order = append(order, p)
			seen[p] = true
		}
	}
	rest := make([]uint8, 0, len(fresh)-len(order))
	for id := range fresh {
		if !seen[id] {
			rest = append(rest, id)
		}
	}
	sort.Slice(rest, func(i, j int) bool { return rest[i] < rest[j] })
	order = append(order, rest...)

	fp := catalogFingerprintOf(order, fresh)
	swapChainsMu.Lock()
	defer swapChainsMu.Unlock()
	if fp == catalogFp {
		return
	}
	swapChains = fresh
	pairOrder = order
	catalogFp = fp
}

// CurrentPairs returns a snapshot of the active pair list, safe for the
// refresh goroutines that iterate it while the catalog updates.
func CurrentPairs() []uint8 {
	swapChainsMu.RLock()
	defer swapChainsMu.RUnlock()
	out := make([]uint8, len(pairOrder))
	copy(out, pairOrder)
	return out
}

// SwapChainInfo returns the daemon catalog entry for a pair, if present.
func SwapChainInfo(pair uint8) (ChainCatalogEntry, bool) {
	swapChainsMu.RLock()
	defer swapChainsMu.RUnlock()
	e, ok := swapChains[pair]
	return e, ok
}

func catalogFingerprintOf(order []uint8, m map[uint8]ChainCatalogEntry) string {
	var b strings.Builder
	fmt.Fprintf(&b, "%d|", len(order))
	for _, id := range order {
		e := m[id]
		fmt.Fprintf(&b, "%d:%s:%t%t|", id, e.Name, e.Configured, e.Ready)
	}
	return b.String()
}

// ── Labels ──

// PairName returns the display name for a pair (e.g. "ETH/XFG").
func PairName(pair uint8) string {
	switch pair {
	case PairCD:
		return "CD"
	case PairDaemon:
		return "Daemon"
	}
	if e, ok := SwapChainInfo(pair); ok && e.Symbol != "" {
		return e.Symbol + "/XFG"
	}
	switch pair {
	case PairSOL:
		return "SOL/XFG"
	case PairETH:
		return "ETH/XFG"
	case PairXMR:
		return "XMR/XFG"
	case PairBCH:
		return "BCH/XFG"
	case PairARB:
		return "ARB/XFG"
	case PairBASE:
		return "BASE/XFG"
	case PairBNB:
		return "BNB/XFG"
	default:
		return "???"
	}
}

func PairLabelLong(pair uint8) string {
	switch pair {
	case PairCD:
		return "Certificates Of Deposit"
	case PairDaemon:
		return "Daemon Status"
	}
	if e, ok := SwapChainInfo(pair); ok && e.Name != "" {
		return e.Name
	}
	switch pair {
	case PairSOL:
		return "Solana"
	case PairETH:
		return "Ethereum"
	case PairXMR:
		return "Monero"
	case PairBCH:
		return "Bitcoin Cash"
	case PairARB:
		return "Arbitrum L2"
	case PairBASE:
		return "Base L2"
	case PairBNB:
		return "BNB Chain"
	default:
		return "Unknown"
	}
}

// PairShort returns the short counterparty symbol (e.g. "ETH").
func PairShort(pair uint8) string {
	if pair == PairCD {
		return "CD"
	}
	if e, ok := SwapChainInfo(pair); ok && e.Symbol != "" {
		return e.Symbol
	}
	switch pair {
	case PairSOL:
		return "SOL"
	case PairETH:
		return "ETH"
	case PairXMR:
		return "XMR"
	case PairBCH:
		return "BCH"
	case PairARB:
		return "ARB"
	case PairBASE:
		return "BASE"
	case PairBNB:
		return "BNB"
	default:
		return "?"
	}
}

// PairFromString returns the pair ID for a string name, or 255 if unknown.
// Catalog keys and symbols win; the legacy table is the fallback.
func PairFromString(s string) uint8 {
	if s == "cd" || s == "CD" {
		return PairCD
	}
	for _, p := range CurrentPairs() {
		e, ok := SwapChainInfo(p)
		if !ok {
			continue
		}
		if strings.EqualFold(e.Key, s) || (e.Symbol != "" && strings.EqualFold(e.Symbol, s)) {
			return p
		}
	}
	switch s {
	case "sol", "SOL":
		return PairSOL
	case "eth", "ETH":
		return PairETH
	case "xmr", "XMR":
		return PairXMR
	case "bch", "BCH":
		return PairBCH
	case "arb", "ARB":
		return PairARB
	case "base", "BASE":
		return PairBASE
	case "bnb", "BNB":
		return PairBNB
	default:
		return 255
	}
}

// TradingViewSymbol returns a TradingView symbol for the counterparty asset.
// XFG itself is not listed on any CEX — it trades solely on the Fuego Hearth
// DEX. We show the counterparty asset's market chart as a reference so users
// can see market context for the asset they're swapping.
// Returns empty string if no TradingView symbol exists.
func TradingViewSymbol(pair uint8) string {
	switch pair {
	case PairSOL:
		return "BINANCE:SOLUSDT"
	case PairETH:
		return "BINANCE:ETHUSDT"
	case PairXMR:
		return "BINANCE:XMRUSDT"
	case PairBCH:
		return "BINANCE:BCHUSDT"
	case PairARB:
		return "BINANCE:ARBUSDT"
	case PairBNB:
		return "BINANCE:BNBUSDT"
	default:
		return ""
	}
}

// HotkeyPair maps a digit hotkey to the pair at that position in the active
// pair list ('0' = first), so hotkeys track the dynamic catalog ('c' stays CD).
func HotkeyPair(r rune) uint8 {
	if r == 'c' {
		return PairCD
	}
	if r < '0' || r > '9' {
		return 255
	}
	pairs := CurrentPairs()
	idx := int(r - '0')
	if idx >= len(pairs) {
		return 255
	}
	return pairs[idx]
}
