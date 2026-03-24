# SwapHub M1: Core TUI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `swaphub` standalone read-only TUI binary that connects to a Fuego daemon and displays a multi-pair trading terminal with candlestick charts, orderbook, trade tape, and market ticker.

**Architecture:** Single Go binary using bubbletea for the TUI framework. The main model composes sub-components (ticker, chart, orderbook, tape, input) into a responsive layout. A multi-pair RPC client fetches data from `fuegod` in parallel goroutines on a 5-second tick. No wallet connection needed — M1 is read-only.

**Tech Stack:** Go 1.21+, bubbletea v1.3+, lipgloss v1.1+, bubbles (textinput, spinner), net/http (stdlib)

**Spec:** `docs/superpowers/specs/2026-03-22-swaphub-design.md` (sections 1.1–1.8, 5, 7)

**Scope (M1 only):**
- Splash screen with doom fire animation
- Market ticker bar showing all 5 active pairs
- Candlestick chart for selected pair
- Orderbook (asks + bids with spread)
- Trade tape (recent trades)
- Command bar with pair switching, HTLC query, help
- CLI flags: `--daemon`, `--testnet`, `--pair`, `--no-splash`, `--compact`
- Graceful degradation when daemon is offline

**NOT in scope (later milestones):**
- Wallet RPC integration, `wallet_rpc.go` (M2)
- MetaMask bridge, `eth_bridge.go` (M3)
- BCH RPC connection, `bch_rpc.go` (M4)
- Solana bridge, `sol_bridge.go`, `sol_rpc.go` (M3)
- EFier web UI (M5)
- Wallet `swap` command rewrite (M6)
- HTLC monitor view `htlc.go` — M1 handles HTLC queries inline in `input.go`; full monitor view deferred to M2

---

## File Structure

```
swaphub/
├── main.go                 CLI entry, flag parsing, launch app.Run()
├── go.mod                  Module: github.com/usexfg/swaphub
├── app/
│   ├── pairs.go            Pair ID constants, names, display helpers
│   ├── styles.go           lipgloss styles and color palette (spec §7)
│   ├── config.go           Config struct, DefaultConfig(), testnet defaults
│   ├── rpc.go              FuegoClient: multi-pair daemon RPC (all /getswap* + /gethtlc* + /getinfo)
│   ├── rpc_test.go         Table-driven tests for RPC response parsing
│   ├── candle.go           Candlestick bucketing logic (trades → OHLCV candles)
│   ├── candle_test.go      Tests for candle bucketing
│   ├── splash.go           Doom fire + SwapHub ASCII art splash screen
│   ├── ticker.go           Top market ticker bar component (all-pairs prices)
│   ├── chart.go            Candlestick chart renderer (box-drawing characters)
│   ├── orderbook.go        Order book component (asks/bids/spread)
│   ├── tape.go             Trade tape component (recent trades)
│   ├── input.go            Command bar: text input + command parsing + dispatch
│   ├── input_test.go       Tests for command parsing
│   ├── layout.go           Layout math: responsive panel sizing
│   ├── tui.go              Main TUI model: composes all components, handles refresh
│   └── app.go              Run() orchestrator: splash → TUI
```

**Responsibilities:**

| File | Responsibility | Dependencies |
|------|---------------|--------------|
| `pairs.go` | Pair ID constants (ETH=1, BCH=2, HEAT=3, LUSD=4), `PairName()`, `PairSymbol()`, `ActivePairs` slice | none |
| `styles.go` | All lipgloss styles from spec §7 color palette. Exported style vars. | lipgloss |
| `config.go` | `Config` struct with all CLI-mappable fields, `DefaultConfig()`, `TestnetConfig()` | `pairs.go` |
| `rpc.go` | `FuegoClient` with `GetOffers(pair)`, `GetPrice(pair)`, `GetTrades(pair, limit)`, `GetHtlc(idx)`, `GetHtlcCount()`, `GetInfo()`. Shared RPC types. | net/http, encoding/json |
| `candle.go` | `BucketCandles(trades, interval) []Candle` — groups trades into OHLCV candles | `rpc.go` (SwapTrade type) |
| `splash.go` | `splashModel` bubbletea model with doom fire + ASCII title, auto-advance 3s | bubbletea, lipgloss, `styles.go` |
| `ticker.go` | `tickerView(prices map[uint8]*SwapPriceResponse, selected uint8, height uint32, width int) string` — pure render function | lipgloss, `pairs.go`, `styles.go`, `rpc.go` |
| `chart.go` | `chartView(candles []Candle, width, height int) string` — candlestick renderer using box-drawing chars | lipgloss, `candle.go`, `styles.go` |
| `orderbook.go` | `orderbookView(offers []SwapOffer, pair uint8, width, height int) string` — asks/bids/spread | lipgloss, `rpc.go`, `styles.go` |
| `tape.go` | `tapeView(trades []SwapTrade, width, height int) string` — recent trades | lipgloss, `rpc.go`, `styles.go` |
| `input.go` | `inputModel` sub-model with text input, `parseCommand(input string) (cmd, args)`, status display | bubbletea, `styles.go` |
| `layout.go` | `calcLayout(termW, termH int, compact bool) Layout` — panel dimensions | none |
| `tui.go` | `tuiModel` — main bubbletea model. Holds all state. `Init()` starts refresh tick. `Update()` handles keys/ticks. `View()` composes all sub-views via layout. | everything above |
| `app.go` | `Run(cfg Config) error` — splash → TUI sequencing | bubbletea, `config.go`, `splash.go`, `tui.go` |
| `main.go` | `main()` — parse os.Args into Config, call `app.Run()` | `app/config.go`, `app/app.go` |

---

### Task 1: Project scaffold and pair definitions

**Files:**
- Create: `swaphub/go.mod`
- Create: `swaphub/main.go`
- Create: `swaphub/app/pairs.go`
- Create: `swaphub/app/config.go`

- [ ] **Step 1: Create go.mod**

```
swaphub/go.mod
```

```go
module github.com/usexfg/swaphub

go 1.21

require (
	github.com/charmbracelet/bubbletea v1.3.4
	github.com/charmbracelet/lipgloss v1.1.0
)
```

Run: `cd /home/ar/fuego/swaphub && go mod tidy`

- [ ] **Step 2: Create pairs.go**

```go
// swaphub/app/pairs.go
package app

// Pair IDs match fuegod's SwapOfferRelay pair numbering.
const (
	PairSOL  uint8 = 0
	PairETH  uint8 = 1
	PairBCH  uint8 = 2
	PairHEAT uint8 = 3
	PairLUSD uint8 = 4
)

// ActivePairs lists all supported pairs.
var ActivePairs = []uint8{PairETH, PairSOL, PairBCH, PairHEAT, PairLUSD}

// PairName returns the display name for a pair (e.g. "ETH/XFG").
func PairName(pair uint8) string {
	switch pair {
	case PairSOL:
		return "SOL/XFG"
	case PairETH:
		return "ETH/XFG"
	case PairBCH:
		return "BCH/XFG"
	case PairHEAT:
		return "HEAT/XFG"
	case PairLUSD:
		return "LUSD/XFG"
	default:
		return "?/XFG"
	}
}

// PairShort returns the short counterparty symbol (e.g. "ETH").
func PairShort(pair uint8) string {
	switch pair {
	case PairSOL:
		return "SOL"
	case PairETH:
		return "ETH"
	case PairBCH:
		return "BCH"
	case PairHEAT:
		return "HEAT"
	case PairLUSD:
		return "LUSD"
	default:
		return "?"
	}
}

// PairFromString parses a pair name string to its ID. Returns (0, false) on failure.
func PairFromString(s string) (uint8, bool) {
	switch s {
	case "sol":
		return PairSOL, true
	case "eth":
		return PairETH, true
	case "bch":
		return PairBCH, true
	case "heat":
		return PairHEAT, true
	case "lusd":
		return PairLUSD, true
	default:
		return 0, false
	}
}

// HotkeyToPair maps hotkey number (1-5) to pair ID.
func HotkeyToPair(key int) (uint8, bool) {
	if key >= 1 && key <= len(ActivePairs) {
		return ActivePairs[key-1], true
	}
	return 0, false
}
```

- [ ] **Step 3: Create config.go**

```go
// swaphub/app/config.go
package app

type Config struct {
	DaemonRPC string // fuegod RPC endpoint
	Testnet   bool
	StartPair uint8  // initial pair to display
	NoSplash  bool
	Compact   bool
}

func DefaultConfig() Config {
	return Config{
		DaemonRPC: "http://127.0.0.1:18180",
		StartPair: PairETH,
	}
}

func TestnetConfig() Config {
	return Config{
		DaemonRPC: "http://127.0.0.1:28280",
		Testnet:   true,
		StartPair: PairETH,
	}
}
```

- [ ] **Step 4: Create minimal main.go with flag parsing**

```go
// swaphub/main.go
package main

import (
	"fmt"
	"os"
	"strings"

	"github.com/usexfg/swaphub/app"
)

func main() {
	cfg := app.DefaultConfig()

	for i := 1; i < len(os.Args); i++ {
		arg := os.Args[i]
		next := func() string {
			if i+1 < len(os.Args) {
				i++
				return os.Args[i]
			}
			fmt.Fprintf(os.Stderr, "missing value for %s\n", arg)
			os.Exit(1)
			return ""
		}
		switch arg {
		case "--daemon", "-d", "--efier", "-e":
			cfg.DaemonRPC = next()
		case "--testnet":
			cfg.DaemonRPC = "http://127.0.0.1:28280"
			cfg.Testnet = true
		case "--pair", "-p":
			p, ok := app.PairFromString(strings.ToLower(next()))
			if !ok {
				fmt.Fprintf(os.Stderr, "unknown pair (use: eth, sol, bch, heat, lusd)\n")
				os.Exit(1)
			}
			cfg.StartPair = p
		case "--no-splash":
			cfg.NoSplash = true
		case "--compact":
			cfg.Compact = true
		case "--help", "-h":
			fmt.Println("swaphub — Fuego cross-chain atomic swap terminal")
			fmt.Println()
			fmt.Println("Usage: swaphub [flags]")
			fmt.Println()
			fmt.Println("Connection:")
			fmt.Println("  --daemon, -d    Fuego daemon RPC (default: http://127.0.0.1:18180)")
			fmt.Println("                  Aliases: --efier, -e")
			fmt.Println("  --testnet       Use testnet defaults (:28280)")
			fmt.Println()
			fmt.Println("Display:")
			fmt.Println("  --pair, -p      Starting pair: eth, sol, bch, heat, lusd (default: eth)")
			fmt.Println("  --no-splash     Skip splash screen")
			fmt.Println("  --compact       Compact layout for small terminals")
			fmt.Println()
			fmt.Println("  --help, -h      Show this help")
			os.Exit(0)
		default:
			fmt.Fprintf(os.Stderr, "unknown flag: %s (try --help)\n", arg)
			os.Exit(1)
		}
	}

	if err := app.Run(cfg); err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		os.Exit(1)
	}
}
```

- [ ] **Step 5: Create stub app.go so it compiles**

```go
// swaphub/app/app.go
package app

import "fmt"

func Run(cfg Config) error {
	fmt.Printf("swaphub starting — daemon=%s pair=%s\n", cfg.DaemonRPC, PairName(cfg.StartPair))
	return nil
}
```

- [ ] **Step 6: Build to verify scaffold compiles**

Run: `cd /home/ar/fuego/swaphub && go mod tidy && go build -o swaphub .`
Expected: Binary `swaphub` created with no errors.

Run: `./swaphub --help`
Expected: Help text displayed.

Run: `./swaphub --testnet --pair bch`
Expected: `swaphub starting — daemon=http://127.0.0.1:28280 pair=BCH/XFG`

- [ ] **Step 7: Commit**

```bash
cd /home/ar/fuego
git add swaphub/
git commit -m "swaphub M1: project scaffold with config, pairs, flag parsing"
```

---

### Task 2: Color palette and styles

**Files:**
- Create: `swaphub/app/styles.go`

- [ ] **Step 1: Create styles.go with the full color palette from spec §7**

```go
// swaphub/app/styles.go
package app

import "github.com/charmbracelet/lipgloss"

// Palette from spec §7
var (
	ColorAccent     = lipgloss.Color("#FF5500") // Fuego orange
	ColorBullish    = lipgloss.Color("#00CC66") // green
	ColorBearish    = lipgloss.Color("#FF3344") // red
	ColorSpread     = lipgloss.Color("#FFAA00") // yellow
	ColorMuted      = lipgloss.Color("#555555") // gray
	ColorActiveTab  = lipgloss.Color("#FFFFFF") // white text
	ColorInactive   = lipgloss.Color("#777777")
	ColorOwn        = lipgloss.Color("#00CCCC") // cyan
	ColorHtlc       = lipgloss.Color("#FFDD00") // pulsing yellow
	ColorConnOK     = lipgloss.Color("#00FF00")
	ColorConnLost   = lipgloss.Color("#FF0000")
)

// Reusable styles
var (
	StyleAccent = lipgloss.NewStyle().Foreground(ColorAccent).Bold(true)
	StyleBull   = lipgloss.NewStyle().Foreground(ColorBullish)
	StyleBear   = lipgloss.NewStyle().Foreground(ColorBearish)
	StyleSpread = lipgloss.NewStyle().Foreground(ColorSpread)
	StyleMuted  = lipgloss.NewStyle().Foreground(ColorMuted)
	StyleInput  = lipgloss.NewStyle().Foreground(lipgloss.Color("#FFFFFF")).Bold(true)
	StyleStatus = lipgloss.NewStyle().Foreground(ColorBearish)

	StyleActiveTab = lipgloss.NewStyle().
			Foreground(ColorActiveTab).
			Background(ColorAccent).
			Bold(true).
			Padding(0, 1)

	StyleInactiveTab = lipgloss.NewStyle().
				Foreground(ColorInactive).
				Padding(0, 1)
)

// Fire palette for splash screen (11 stops: near-black -> white-yellow)
var FirePalette = [11]lipgloss.Color{
	"#180400", "#3D0A00", "#6D1400", "#9E2000",
	"#D43800", "#FF5500", "#FF7F11", "#FFA940",
	"#FFD066", "#FFE899", "#FFFADD",
}

var FireChars = [11]rune{' ', '.', ':', '*', '\u2591', '\u2592', '\u2593', '\u2588', '\u2588', '\u2588', '\u2588'}
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/ar/fuego/swaphub && go build -o swaphub .`
Expected: Compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add swaphub/app/styles.go
git commit -m "swaphub M1: color palette and lipgloss styles"
```

---

### Task 3: Multi-pair RPC client

**Files:**
- Create: `swaphub/app/rpc.go`
- Create: `swaphub/app/rpc_test.go`

- [ ] **Step 1: Write test for RPC response JSON parsing**

```go
// swaphub/app/rpc_test.go
package app

import (
	"encoding/json"
	"testing"
)

func TestSwapOfferParsing(t *testing.T) {
	raw := `{"offers":[{"offerId":"abc123","xfgAmount":80000000,"rateNum":22000,"pair":1,"makerPubKey":"deadbeef","timestamp":1711100000,"ttlBlocks":180,"postedHeight":184000}],"status":"OK"}`
	var resp struct {
		Offers []SwapOffer `json:"offers"`
		Status string      `json:"status"`
	}
	if err := json.Unmarshal([]byte(raw), &resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if len(resp.Offers) != 1 {
		t.Fatalf("expected 1 offer, got %d", len(resp.Offers))
	}
	o := resp.Offers[0]
	if o.OfferID != "abc123" {
		t.Errorf("offerId = %q, want abc123", o.OfferID)
	}
	if o.XfgAmount != 80000000 {
		t.Errorf("xfgAmount = %d, want 80000000", o.XfgAmount)
	}
	if o.Pair != 1 {
		t.Errorf("pair = %d, want 1", o.Pair)
	}
}

func TestSwapPriceParsing(t *testing.T) {
	raw := `{"twap":"22000.0","seedRate":"22000.0","compositeRate":"21500.5","sourceCount":3,"sources":[],"xfgUsdLow":"0.0080","xfgUsdHigh":"0.0120","xfgUsdMid":"0.0100","pairImplied":[],"status":"OK"}`
	var resp SwapPriceResponse
	if err := json.Unmarshal([]byte(raw), &resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if resp.CompositeRate != "21500.5" {
		t.Errorf("compositeRate = %q, want 21500.5", resp.CompositeRate)
	}
	if resp.XfgUsdMid != "0.0100" {
		t.Errorf("xfgUsdMid = %q, want 0.0100", resp.XfgUsdMid)
	}
}

func TestHtlcOutputParsing(t *testing.T) {
	raw := `{"amount":8000000,"recipientKey":"aabb","refundKey":"ccdd","hashLock":"eeff","timeoutHeight":185000,"isSpent":false,"status":"OK"}`
	var resp HtlcOutput
	if err := json.Unmarshal([]byte(raw), &resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if resp.Amount != 8000000 {
		t.Errorf("amount = %d, want 8000000", resp.Amount)
	}
	if resp.IsSpent {
		t.Error("expected isSpent=false")
	}
}
```

- [ ] **Step 2: Run tests — expect compile failure (types don't exist yet)**

Run: `cd /home/ar/fuego/swaphub && go test ./app/ -run TestSwap -v`
Expected: Compile error — `SwapOffer`, `SwapPriceResponse`, `HtlcOutput` undefined.

- [ ] **Step 3: Write rpc.go with types and multi-pair client**

```go
// swaphub/app/rpc.go
package app

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sync"
	"time"
)

type FuegoClient struct {
	endpoint string
	client   *http.Client
}

func NewFuegoClient(endpoint string) *FuegoClient {
	return &FuegoClient{
		endpoint: endpoint,
		client:   &http.Client{Timeout: 10 * time.Second},
	}
}

// --- RPC response types (mirror CoreRpcServerCommandsDefinitions.h) ---

type SwapOffer struct {
	OfferID      string `json:"offerId"`
	XfgAmount    uint64 `json:"xfgAmount"`
	RateNum      uint64 `json:"rateNum"`
	Pair         uint8  `json:"pair"`
	MakerPubKey  string `json:"makerPubKey"`
	Timestamp    uint64 `json:"timestamp"`
	TTLBlocks    uint32 `json:"ttlBlocks"`
	PostedHeight uint32 `json:"postedHeight"`
}

type SwapTrade struct {
	Pair      uint8  `json:"pair"`
	XfgAmount uint64 `json:"xfgAmount"`
	CtrAmount uint64 `json:"ctrAmount"`
	Rate      string `json:"rate"`
	Height    uint32 `json:"blockHeight"`
	Timestamp uint64 `json:"timestamp"`
}

type PriceSourceEntry struct {
	Name      string `json:"name"`
	Pair      uint8  `json:"pair"`
	Weight    string `json:"weight"`
	Rate      string `json:"rate"`
	UpdatedAt uint64 `json:"updatedAt"`
	Stale     bool   `json:"stale"`
}

type PairImplied struct {
	Pair       uint8  `json:"pair"`
	ImpliedUsd string `json:"impliedUsd"`
}

type SwapPriceResponse struct {
	Twap          string             `json:"twap"`
	SeedRate      string             `json:"seedRate"`
	CompositeRate string             `json:"compositeRate"`
	SourceCount   uint32             `json:"sourceCount"`
	Sources       []PriceSourceEntry `json:"sources"`
	XfgUsdLow     string            `json:"xfgUsdLow"`
	XfgUsdHigh    string            `json:"xfgUsdHigh"`
	XfgUsdMid     string            `json:"xfgUsdMid"`
	PairImplied   []PairImplied     `json:"pairImplied"`
	Status        string            `json:"status"`
}

type HtlcOutput struct {
	Amount        uint64 `json:"amount"`
	RecipientKey  string `json:"recipientKey"`
	RefundKey     string `json:"refundKey"`
	HashLock      string `json:"hashLock"`
	TimeoutHeight uint32 `json:"timeoutHeight"`
	IsSpent       bool   `json:"isSpent"`
	Status        string `json:"status"`
}

type NodeInfo struct {
	Height         uint64 `json:"height"`
	Difficulty     uint64 `json:"difficulty"`
	TxCount        uint64 `json:"tx_count"`
	TxPoolSize     uint64 `json:"tx_pool_size"`
	IncomingConns  uint64 `json:"incoming_connections_count"`
	OutgoingConns  uint64 `json:"outgoing_connections_count"`
	Version        string `json:"version"`
	Status         string `json:"status"`
}

// --- Multi-pair fetch methods ---

func (c *FuegoClient) GetOffers(pair uint8) ([]SwapOffer, error) {
	req := map[string]interface{}{"pair": pair}
	var resp struct {
		Offers []SwapOffer `json:"offers"`
		Status string      `json:"status"`
	}
	if err := c.post("/getswapoffers", req, &resp); err != nil {
		return nil, err
	}
	return resp.Offers, nil
}

func (c *FuegoClient) GetPrice(pair uint8) (*SwapPriceResponse, error) {
	req := map[string]interface{}{"pair": pair}
	var resp SwapPriceResponse
	if err := c.post("/getswapprice", req, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

func (c *FuegoClient) GetTrades(pair uint8, limit int) ([]SwapTrade, error) {
	req := map[string]interface{}{"pair": pair, "limit": limit}
	var resp struct {
		Trades []SwapTrade `json:"trades"`
		Status string      `json:"status"`
	}
	if err := c.post("/getswaptrades", req, &resp); err != nil {
		return nil, err
	}
	return resp.Trades, nil
}

func (c *FuegoClient) GetHtlc(index uint32) (*HtlcOutput, error) {
	req := map[string]interface{}{"index": index}
	var resp HtlcOutput
	if err := c.post("/gethtlc", req, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

func (c *FuegoClient) GetHtlcCount() (uint32, error) {
	var resp struct {
		Count  uint32 `json:"count"`
		Status string `json:"status"`
	}
	if err := c.post("/gethtlccount", nil, &resp); err != nil {
		return 0, err
	}
	return resp.Count, nil
}

func (c *FuegoClient) GetInfo() (*NodeInfo, error) {
	var resp NodeInfo
	if err := c.post("/getinfo", nil, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

// AllPairData holds fetched data for all active pairs.
type AllPairData struct {
	Offers map[uint8][]SwapOffer
	Prices map[uint8]*SwapPriceResponse
	Height uint64
}

// FetchAllPairs fetches offers and prices for all active pairs in parallel.
func (c *FuegoClient) FetchAllPairs() (*AllPairData, error) {
	data := &AllPairData{
		Offers: make(map[uint8][]SwapOffer),
		Prices: make(map[uint8]*SwapPriceResponse),
	}

	var mu sync.Mutex
	var wg sync.WaitGroup
	var firstErr error

	// Fetch info (block height)
	wg.Add(1)
	go func() {
		defer wg.Done()
		info, err := c.GetInfo()
		if err != nil {
			mu.Lock()
			if firstErr == nil {
				firstErr = err
			}
			mu.Unlock()
			return
		}
		mu.Lock()
		data.Height = info.Height
		mu.Unlock()
	}()

	// Fetch offers + prices per pair
	for _, pair := range ActivePairs {
		p := pair
		wg.Add(2)
		go func() {
			defer wg.Done()
			offers, err := c.GetOffers(p)
			mu.Lock()
			if err == nil {
				data.Offers[p] = offers
			} else if firstErr == nil {
				firstErr = err
			}
			mu.Unlock()
		}()
		go func() {
			defer wg.Done()
			price, err := c.GetPrice(p)
			mu.Lock()
			if err == nil {
				data.Prices[p] = price
			} else if firstErr == nil {
				firstErr = err
			}
			mu.Unlock()
		}()
	}

	wg.Wait()
	return data, firstErr
}

// --- HTTP helper ---

func (c *FuegoClient) post(path string, reqBody interface{}, result interface{}) error {
	var body io.Reader
	if reqBody != nil {
		data, err := json.Marshal(reqBody)
		if err != nil {
			return fmt.Errorf("marshal: %w", err)
		}
		body = bytes.NewReader(data)
	}

	resp, err := c.client.Post(c.endpoint+path, "application/json", body)
	if err != nil {
		return fmt.Errorf("request %s: %w", path, err)
	}
	defer resp.Body.Close()

	raw, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("read %s: %w", path, err)
	}

	if err := json.Unmarshal(raw, result); err != nil {
		return fmt.Errorf("decode %s: %w", path, err)
	}
	return nil
}
```

- [ ] **Step 4: Run tests — expect PASS**

Run: `cd /home/ar/fuego/swaphub && go test ./app/ -run TestSwap -v && go test ./app/ -run TestHtlc -v`
Expected: All 3 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add swaphub/app/rpc.go swaphub/app/rpc_test.go
git commit -m "swaphub M1: multi-pair RPC client with parallel fetch"
```

---

### Task 4: Candlestick bucketing logic

**Files:**
- Create: `swaphub/app/candle.go`
- Create: `swaphub/app/candle_test.go`

- [ ] **Step 1: Write failing test for candle bucketing**

```go
// swaphub/app/candle_test.go
package app

import (
	"testing"
	"time"
)

func TestBucketCandles(t *testing.T) {
	now := time.Now()
	base := uint64(now.Add(-15 * time.Minute).Unix())

	trades := []SwapTrade{
		{Timestamp: base, Rate: "100.0", XfgAmount: 8_0000000},       // candle 0
		{Timestamp: base + 60, Rate: "105.0", XfgAmount: 80_0000000}, // candle 0
		{Timestamp: base + 120, Rate: "95.0", XfgAmount: 8000000},    // candle 0
		{Timestamp: base + 300, Rate: "110.0", XfgAmount: 8_0000000}, // candle 1
		{Timestamp: base + 600, Rate: "108.0", XfgAmount: 80_0000000},// candle 2
	}

	candles := BucketCandles(trades, 5*time.Minute)
	if len(candles) < 2 {
		t.Fatalf("expected at least 2 candles, got %d", len(candles))
	}

	c0 := candles[0]
	if c0.Open != 100.0 {
		t.Errorf("candle[0].Open = %f, want 100.0", c0.Open)
	}
	if c0.High != 105.0 {
		t.Errorf("candle[0].High = %f, want 105.0", c0.High)
	}
	if c0.Low != 95.0 {
		t.Errorf("candle[0].Low = %f, want 95.0", c0.Low)
	}
	if c0.Close != 95.0 {
		t.Errorf("candle[0].Close = %f, want 95.0", c0.Close)
	}
	if c0.Volume == 0 {
		t.Error("candle[0].Volume should be > 0")
	}
}

func TestBucketCandlesEmpty(t *testing.T) {
	candles := BucketCandles(nil, 5*time.Minute)
	if len(candles) != 0 {
		t.Errorf("expected 0 candles for nil trades, got %d", len(candles))
	}
}
```

- [ ] **Step 2: Run test — expect compile failure**

Run: `cd /home/ar/fuego/swaphub && go test ./app/ -run TestBucket -v`
Expected: Compile error — `BucketCandles` and `Candle` undefined.

- [ ] **Step 3: Implement candle.go**

```go
// swaphub/app/candle.go
package app

import (
	"sort"
	"strconv"
	"time"
)

// Candle represents one OHLCV candlestick.
type Candle struct {
	Time   time.Time
	Open   float64
	High   float64
	Low    float64
	Close  float64
	Volume float64 // XFG volume
}

// BucketCandles groups trades into OHLCV candles of the given interval.
// Trades are sorted by timestamp ascending before bucketing.
func BucketCandles(trades []SwapTrade, interval time.Duration) []Candle {
	if len(trades) == 0 {
		return nil
	}

	// Sort by timestamp ascending
	sorted := make([]SwapTrade, len(trades))
	copy(sorted, trades)
	sort.Slice(sorted, func(i, j int) bool {
		return sorted[i].Timestamp < sorted[j].Timestamp
	})

	secs := int64(interval.Seconds())
	if secs <= 0 {
		secs = 300 // default 5 min
	}

	var candles []Candle
	var cur *Candle
	var curBucket int64

	for _, t := range sorted {
		rate, _ := strconv.ParseFloat(t.Rate, 64)
		if rate <= 0 {
			continue
		}
		vol := float64(t.XfgAmount) / 1e7
		bucket := int64(t.Timestamp) / secs

		if cur == nil || bucket != curBucket {
			if cur != nil {
				candles = append(candles, *cur)
			}
			cur = &Candle{
				Time:   time.Unix(bucket*secs, 0),
				Open:   rate,
				High:   rate,
				Low:    rate,
				Close:  rate,
				Volume: vol,
			}
			curBucket = bucket
		} else {
			if rate > cur.High {
				cur.High = rate
			}
			if rate < cur.Low {
				cur.Low = rate
			}
			cur.Close = rate
			cur.Volume += vol
		}
	}
	if cur != nil {
		candles = append(candles, *cur)
	}

	return candles
}
```

- [ ] **Step 4: Run tests — expect PASS**

Run: `cd /home/ar/fuego/swaphub && go test ./app/ -run TestBucket -v`
Expected: Both tests PASS.

- [ ] **Step 5: Commit**

```bash
git add swaphub/app/candle.go swaphub/app/candle_test.go
git commit -m "swaphub M1: candlestick bucketing logic with tests"
```

---

### Task 5: Splash screen

**Files:**
- Create: `swaphub/app/splash.go`

- [ ] **Step 1: Create splash.go with doom fire + SwapHub ASCII title**

The splash uses the same doom fire algorithm as the existing swap clients (`xfg-eth-swap/app/splash.go`). The key differences: no per-chain logo (just the Fuego diamond), SwapHub ASCII title, Fuego orange palette, auto-advance after 3 seconds (~37 frames at 80ms).

```go
// swaphub/app/splash.go
package app

import (
	"math"
	"math/rand"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

const (
	splashFW   = 38 // fire simulation width
	splashFH   = 10 // fire simulation height

	// Diamond geometry (Fuego logo)
	dMaxHW = 13
	dTopH  = 13
	dBotH  = 7
	dH     = dTopH + dBotH // 20
	dW     = dMaxHW * 2    // 26
	dLap   = 3             // fire rows overlapping diamond bottom
)

// Diamond palette
var (
	dEdge = lipgloss.Color("#FFDD66")
	dSeam = lipgloss.Color("#FF8C00")
	dMid  = lipgloss.Color("#FFA030")
	dGlow = lipgloss.Color("#FFFFCC")
)

var tlB = [3]lipgloss.Color{"#FFD060", "#E8A030", "#CC7818"}
var trB = [3]lipgloss.Color{"#FFE888", "#FFD060", "#FFAA30"}
var blB = [2]lipgloss.Color{"#8B2000", "#A03010"}
var brB = [2]lipgloss.Color{"#CC4400", "#DD5510"}

const splashTitle = `  ███████╗██╗    ██╗ █████╗ ██████╗ ██╗  ██╗██╗   ██╗██████╗
  ██╔════╝██║    ██║██╔══██╗██╔══██╗██║  ██║██║   ██║██╔══██╗
  ███████╗██║ █╗ ██║███████║██████╔╝███████║██║   ██║██████╔╝
  ╚════██║██║███╗██║██╔══██║██╔═══╝ ██╔══██║██║   ██║██╔══██╗
  ███████║╚███╔███╔╝██║  ██║██║     ██║  ██║╚██████╔╝██████╔╝
  ╚══════╝ ╚══╝╚══╝ ╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚═════╝`

type splashModel struct {
	width, height int
	frame         int
	fire          [][]float64
}

type splashTickMsg time.Time

func splashTick() tea.Cmd {
	return tea.Tick(80*time.Millisecond, func(t time.Time) tea.Msg {
		return splashTickMsg(t)
	})
}

func newSplashModel() splashModel {
	f := make([][]float64, splashFH)
	for i := range f {
		f[i] = make([]float64, splashFW)
	}
	return splashModel{fire: f}
}

func (m splashModel) Init() tea.Cmd {
	return splashTick()
}

func (m splashModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		return m, tea.Quit
	case tea.MouseMsg:
		_ = msg
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
	case splashTickMsg:
		m.frame++
		if m.frame > 37 { // ~3 seconds at 80ms
			return m, tea.Quit
		}
		m.stepFire()
		return m, splashTick()
	}
	return m, nil
}

func (m *splashModel) stepFire() {
	cx := float64(splashFW) / 2.0
	for x := 0; x < splashFW; x++ {
		d := math.Abs(float64(x)-cx) / cx
		pk := 1.0 - d*0.4
		m.fire[splashFH-1][x] = pk * (0.6 + rand.Float64()*0.4)
		if splashFH > 1 {
			m.fire[splashFH-2][x] = pk * (0.25 + rand.Float64()*0.45)
		}
	}
	for y := 0; y < splashFH-2; y++ {
		for x := 0; x < splashFW; x++ {
			s := m.fire[y+1][x] * 3.0
			if x > 0 {
				s += m.fire[y+1][x-1]
			} else {
				s += m.fire[y+1][x]
			}
			if x < splashFW-1 {
				s += m.fire[y+1][x+1]
			} else {
				s += m.fire[y+1][x]
			}
			if y+2 < splashFH {
				s += m.fire[y+2][x]
			} else {
				s += m.fire[y+1][x]
			}
			m.fire[y][x] = math.Max(0, s/6.0-0.05-rand.Float64()*0.07)
		}
	}
}

// Diamond rendering (reused from xfg-eth-swap)
func dHW(row int) int {
	if row < dTopH {
		return 1 + (dMaxHW-1)*row/(dTopH-1)
	}
	return dMaxHW - (dMaxHW-1)*(row-dTopH)/(dBotH-1)
}

func dPixel(row, col, frame int) (bool, lipgloss.Color) {
	hw := dHW(row)
	l := dMaxHW - hw
	r := dMaxHW + hw - 1

	if col < l || col > r {
		return false, ""
	}
	if wp := frame % (dH + 8); row == wp && col > l && col < r {
		return true, dGlow
	}
	if col == l || col == r {
		return true, dEdge
	}
	if row == dTopH-1 || row == dTopH {
		return true, dSeam
	}
	if col == dMaxHW-1 || col == dMaxHW {
		return true, dMid
	}
	if row < dTopH {
		b := row * 3 / dTopH
		if b > 2 {
			b = 2
		}
		if col < dMaxHW {
			return true, tlB[b]
		}
		return true, trB[b]
	}
	b := (row - dTopH) * 2 / dBotH
	if b > 1 {
		b = 1
	}
	if col < dMaxHW {
		return true, blB[b]
	}
	return true, brB[b]
}

func (m splashModel) View() string {
	if m.width == 0 {
		return ""
	}

	artW := splashFW
	dOff := (artW - dW) / 2
	fStart := dH - dLap
	totalH := fStart + splashFH

	lines := make([]string, 0, totalH)
	for row := 0; row < totalH; row++ {
		var b strings.Builder
		for col := 0; col < artW; col++ {
			dc := col - dOff
			if row < dH && dc >= 0 && dc < dW {
				if ok, c := dPixel(row, dc, m.frame); ok {
					b.WriteString(lipgloss.NewStyle().Foreground(c).Render("\u2588"))
					continue
				}
			}
			fr := row - fStart
			if fr >= 0 && fr < splashFH && col >= 0 && col < splashFW {
				heat := m.fire[fr][col]
				if heat > 0.04 {
					idx := int(heat * 10)
					if idx > 10 {
						idx = 10
					}
					if FireChars[idx] != ' ' {
						b.WriteString(lipgloss.NewStyle().Foreground(FirePalette[idx]).Render(string(FireChars[idx])))
						continue
					}
				}
			}
			b.WriteString(" ")
		}
		lines = append(lines, b.String())
	}

	art := strings.Join(lines, "\n")
	tS := StyleAccent
	sS := StyleMuted

	content := lipgloss.JoinVertical(lipgloss.Center,
		art,
		"",
		tS.Render(splashTitle),
		"",
		sS.Render("cross-chain atomic swaps  \u00b7  press any key"),
	)

	return lipgloss.Place(m.width, m.height,
		lipgloss.Center, lipgloss.Center,
		content,
	)
}
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/ar/fuego/swaphub && go build -o swaphub .`
Expected: Compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add swaphub/app/splash.go
git commit -m "swaphub M1: doom fire splash screen with Fuego diamond"
```

---

### Task 6: Layout engine

**Files:**
- Create: `swaphub/app/layout.go`

- [ ] **Step 1: Create layout.go with responsive panel sizing**

```go
// swaphub/app/layout.go
package app

// Layout holds calculated dimensions for each panel.
type Layout struct {
	TermW, TermH int

	TickerH int // market ticker bar height
	ChartW  int // candlestick chart width (left panel)
	ChartH  int // candlestick chart height
	BookW   int // orderbook width (right panel)
	BookH   int // orderbook height (top right)
	TapeH   int // trade tape height (bottom right)
	InputH  int // command bar height (bottom)
}

// CalcLayout computes responsive panel dimensions.
// Spec: chart is ~60% left, book/tape is ~40% right.
func CalcLayout(w, h int, compact bool) Layout {
	l := Layout{TermW: w, TermH: h}

	l.TickerH = 2
	l.InputH = 3

	bodyH := h - l.TickerH - l.InputH - 2 // 2 for borders/gaps
	if bodyH < 6 {
		bodyH = 6
	}

	if compact {
		l.ChartW = w
		l.BookW = 0
		l.ChartH = bodyH
		l.BookH = 0
		l.TapeH = 0
		return l
	}

	// 60/40 split
	l.ChartW = w * 60 / 100
	l.BookW = w - l.ChartW - 1 // 1 for vertical separator

	// Book gets top 60%, tape gets bottom 40% of right panel
	l.BookH = bodyH * 60 / 100
	l.TapeH = bodyH - l.BookH
	l.ChartH = bodyH

	return l
}
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/ar/fuego/swaphub && go build -o swaphub .`
Expected: Compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add swaphub/app/layout.go
git commit -m "swaphub M1: responsive layout engine"
```

---

### Task 7: Ticker bar component

**Files:**
- Create: `swaphub/app/ticker.go`

- [ ] **Step 1: Create ticker.go — top bar showing all pair prices**

```go
// swaphub/app/ticker.go
package app

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

// TickerView renders the market ticker bar.
func TickerView(prices map[uint8]*SwapPriceResponse, selected uint8, height uint64, connected bool, width int) string {
	var parts []string

	// Logo
	parts = append(parts, StyleAccent.Render("\u25c6 SWAPHUB"))

	// Per-pair prices
	for _, pair := range ActivePairs {
		p := prices[pair]
		name := PairShort(pair)
		var rateStr string
		if p != nil {
			rate, _ := strconv.ParseFloat(p.CompositeRate, 64)
			if rate <= 0 {
				rate, _ = strconv.ParseFloat(p.SeedRate, 64)
			}
			if rate > 0 {
				if pair == PairHEAT {
					rateStr = fmt.Sprintf("1:%s", formatCompact(rate))
				} else if pair == PairLUSD {
					// LUSD is a stablecoin — show implied USD
					inv := 1.0 / rate
					rateStr = fmt.Sprintf("$%.4f", inv)
				} else {
					inv := 1.0 / rate
					rateStr = fmt.Sprintf("%.6f", inv)
				}
			}
		}

		label := name
		if rateStr != "" {
			label = name + " " + rateStr
		}

		if pair == selected {
			parts = append(parts, StyleActiveTab.Render(label))
		} else {
			parts = append(parts, StyleInactiveTab.Render(label))
		}
	}

	// Block height
	parts = append(parts, StyleMuted.Render(fmt.Sprintf("BLK %d", height)))

	// Connection indicator
	if connected {
		parts = append(parts, lipgloss.NewStyle().Foreground(ColorConnOK).Render("\u25cf"))
	} else {
		parts = append(parts, lipgloss.NewStyle().Foreground(ColorConnLost).Render("\u25cf"))
	}

	line := strings.Join(parts, "  ")

	// Pad to terminal width
	pad := width - lipgloss.Width(line)
	if pad > 0 {
		line += strings.Repeat(" ", pad)
	}

	return line
}

func formatCompact(v float64) string {
	switch {
	case v >= 1_000_000:
		return fmt.Sprintf("%.0fM", v/1_000_000)
	case v >= 1_000:
		return fmt.Sprintf("%.0fK", v/1_000)
	default:
		return fmt.Sprintf("%.0f", v)
	}
}
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/ar/fuego/swaphub && go build -o swaphub .`
Expected: Compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add swaphub/app/ticker.go
git commit -m "swaphub M1: market ticker bar with all-pair prices"
```

---

### Task 8: Orderbook component

**Files:**
- Create: `swaphub/app/orderbook.go`

- [ ] **Step 1: Create orderbook.go — asks/bids/spread display**

```go
// swaphub/app/orderbook.go
package app

import (
	"fmt"
	"sort"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

// OrderbookView renders the order book for a given pair.
// Asks: cheapest near spread (bottom of asks section).
// Bids: best bid near spread (top of bids section).
func OrderbookView(offers []SwapOffer, pair uint8, width, height int) string {
	if height < 4 {
		return ""
	}

	header := StyleAccent.Render("ORDER BOOK")
	headerLine := centerPad(header, width)

	if len(offers) == 0 {
		return strings.Join([]string{
			headerLine,
			StyleMuted.Render(centerPad("no offers", width)),
		}, "\n")
	}

	// Split into asks (sells) and bids (buys)
	// In our orderbook, all offers are from XFG holders offering to sell XFG.
	// So they are all "asks" from the counterparty buyer's perspective.
	// We display them sorted by rate.
	sorted := make([]SwapOffer, len(offers))
	copy(sorted, offers)
	sort.Slice(sorted, func(i, j int) bool {
		return sorted[i].RateNum > sorted[j].RateNum // best rate first
	})

	// Format
	var lines []string
	lines = append(lines, headerLine)
	lines = append(lines, StyleMuted.Render(fmt.Sprintf(" %-8s %12s %12s", "RATE", "XFG", "CTR")))

	maxOffers := height - 3 // header + column header + spread
	if maxOffers < 1 {
		maxOffers = 1
	}

	for i, o := range sorted {
		if i >= maxOffers {
			break
		}
		rate := float64(o.RateNum) / 1e7
		xfg := float64(o.XfgAmount) / 1e7
		ctr := xfg / rate

		// Color by position (top = expensive = red, bottom = cheap = green)
		style := StyleBull
		if i < len(sorted)/2 {
			style = StyleBear
		}

		id := o.OfferID
		if len(id) > 6 {
			id = id[:6]
		}

		line := fmt.Sprintf(" %8.1f %9.2f XFG %9.6f", rate, xfg, ctr)
		lines = append(lines, style.Render(line))
	}

	if len(sorted) > 1 {
		best := float64(sorted[0].RateNum) / 1e7
		worst := float64(sorted[len(sorted)-1].RateNum) / 1e7
		spread := best - worst
		if spread < 0 {
			spread = -spread
		}
		lines = append(lines, StyleSpread.Render(centerPad(fmt.Sprintf("spread %.1f", spread), width)))
	}

	return strings.Join(lines, "\n")
}

func centerPad(s string, w int) string {
	pad := w - lipgloss.Width(s)
	if pad <= 0 {
		return s
	}
	left := pad / 2
	return strings.Repeat(" ", left) + s
}
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/ar/fuego/swaphub && go build -o swaphub .`
Expected: Compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add swaphub/app/orderbook.go
git commit -m "swaphub M1: orderbook component with asks/bids/spread"
```

---

### Task 9: Trade tape component

**Files:**
- Create: `swaphub/app/tape.go`

- [ ] **Step 1: Create tape.go — recent trades display**

```go
// swaphub/app/tape.go
package app

import (
	"fmt"
	"strconv"
	"strings"
	"time"
)

// TapeView renders the recent trade tape.
func TapeView(trades []SwapTrade, width, height int) string {
	if height < 2 {
		return ""
	}

	header := StyleAccent.Render("TRADE TAPE")
	headerLine := centerPad(header, width)

	if len(trades) == 0 {
		return strings.Join([]string{
			headerLine,
			StyleMuted.Render(centerPad("no trades", width)),
		}, "\n")
	}

	var lines []string
	lines = append(lines, headerLine)
	lines = append(lines, StyleMuted.Render(fmt.Sprintf(" %10s %10s %6s", "RATE", "XFG", "AGE")))

	maxTrades := height - 2
	if maxTrades < 1 {
		maxTrades = 1
	}

	for i, t := range trades {
		if i >= maxTrades {
			break
		}
		rate, _ := strconv.ParseFloat(t.Rate, 64)
		xfg := float64(t.XfgAmount) / 1e7
		age := FormatAge(t.Timestamp)

		line := fmt.Sprintf(" %10.1f %7.2f XFG %6s", rate, xfg, age)
		lines = append(lines, StyleMuted.Render(line))
	}

	return strings.Join(lines, "\n")
}

// FormatAge returns a compact age string (e.g. "2m", "3h", "1d").
func FormatAge(ts uint64) string {
	if ts == 0 {
		return "-"
	}
	d := time.Since(time.Unix(int64(ts), 0))
	switch {
	case d < time.Minute:
		return fmt.Sprintf("%ds", int(d.Seconds()))
	case d < time.Hour:
		return fmt.Sprintf("%dm", int(d.Minutes()))
	case d < 24*time.Hour:
		return fmt.Sprintf("%dh", int(d.Hours()))
	default:
		return fmt.Sprintf("%dd", int(d.Hours()/24))
	}
}
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/ar/fuego/swaphub && go build -o swaphub .`
Expected: Compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add swaphub/app/tape.go
git commit -m "swaphub M1: trade tape component"
```

---

### Task 10: Candlestick chart renderer

**Files:**
- Create: `swaphub/app/chart.go`

- [ ] **Step 1: Create chart.go — box-drawing candlestick renderer**

```go
// swaphub/app/chart.go
package app

import (
	"fmt"
	"math"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

// ChartView renders a candlestick chart using box-drawing characters.
// Green candles: close >= open (bullish). Red candles: close < open (bearish).
func ChartView(candles []Candle, twap string, composite string, width, height int) string {
	if height < 4 {
		return ""
	}

	chartH := height - 2 // reserve 2 lines for TWAP/composite

	if len(candles) == 0 {
		lines := make([]string, chartH)
		for i := range lines {
			lines[i] = strings.Repeat(" ", width)
		}
		lines = append(lines, StyleMuted.Render(fmt.Sprintf(" TWAP: %s  Composite: %s", twap, composite)))
		return strings.Join(lines, "\n")
	}

	// Determine visible candles (rightmost N that fit)
	candleW := 3 // each candle is 3 chars wide (space + body + space)
	maxCandles := width / candleW
	if maxCandles < 1 {
		maxCandles = 1
	}
	start := 0
	if len(candles) > maxCandles {
		start = len(candles) - maxCandles
	}
	visible := candles[start:]

	// Find price range
	minP := math.Inf(1)
	maxP := math.Inf(-1)
	for _, c := range visible {
		if c.Low < minP {
			minP = c.Low
		}
		if c.High > maxP {
			maxP = c.High
		}
	}

	priceRange := maxP - minP
	if priceRange <= 0 {
		priceRange = 1
	}

	// Map price to row (0 = top = high, chartH-1 = bottom = low)
	priceToRow := func(p float64) int {
		r := int((maxP - p) / priceRange * float64(chartH-1))
		if r < 0 {
			r = 0
		}
		if r >= chartH {
			r = chartH - 1
		}
		return r
	}

	// Build grid
	grid := make([][]lipgloss.Style, chartH)
	chars := make([][]rune, chartH)
	for i := range grid {
		grid[i] = make([]lipgloss.Style, width)
		chars[i] = make([]rune, width)
		for j := range chars[i] {
			chars[i][j] = ' '
		}
	}

	for ci, c := range visible {
		x := ci*candleW + 1 // center column of this candle
		if x >= width {
			break
		}

		bullish := c.Close >= c.Open
		style := StyleBull
		if !bullish {
			style = StyleBear
		}

		// Wick (high to low)
		wickTop := priceToRow(c.High)
		wickBot := priceToRow(c.Low)
		for row := wickTop; row <= wickBot; row++ {
			chars[row][x] = '\u2502' // │
			grid[row][x] = style
		}

		// Body (open to close)
		bodyTop := priceToRow(math.Max(c.Open, c.Close))
		bodyBot := priceToRow(math.Min(c.Open, c.Close))
		if bodyTop == bodyBot {
			// Doji
			chars[bodyTop][x] = '\u2500' // ─
			grid[bodyTop][x] = style
		} else {
			for row := bodyTop; row <= bodyBot; row++ {
				if row == bodyTop {
					chars[row][x] = '\u2580' // ▀ (top of body)
				} else if row == bodyBot {
					chars[row][x] = '\u2584' // ▄ (bottom of body)
				} else {
					chars[row][x] = '\u2588' // █ (filled body)
				}
				grid[row][x] = style
			}
		}
	}

	// Render grid to string
	var lines []string
	for row := 0; row < chartH; row++ {
		var b strings.Builder
		for col := 0; col < width; col++ {
			ch := chars[row][col]
			if ch == ' ' {
				b.WriteRune(' ')
			} else {
				b.WriteString(grid[row][col].Render(string(ch)))
			}
		}
		lines = append(lines, b.String())
	}

	// Price scale on right edge
	if width > 10 && chartH > 2 {
		topLabel := fmt.Sprintf("%.1f", maxP)
		botLabel := fmt.Sprintf("%.1f", minP)
		if len(lines[0]) > 0 {
			lines[0] = lines[0] + " " + StyleMuted.Render(topLabel)
		}
		if len(lines[chartH-1]) > 0 {
			lines[chartH-1] = lines[chartH-1] + " " + StyleMuted.Render(botLabel)
		}
	}

	// TWAP and composite price line
	lines = append(lines, "")
	lines = append(lines, StyleMuted.Render(fmt.Sprintf(" TWAP: %s  Composite: %s", twap, composite)))

	return strings.Join(lines, "\n")
}
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/ar/fuego/swaphub && go build -o swaphub .`
Expected: Compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add swaphub/app/chart.go
git commit -m "swaphub M1: candlestick chart renderer with box-drawing chars"
```

---

### Task 11: Command input component

**Files:**
- Create: `swaphub/app/input.go`
- Create: `swaphub/app/input_test.go`

- [ ] **Step 1: Write test for command parsing**

```go
// swaphub/app/input_test.go
package app

import "testing"

func TestParseCommand(t *testing.T) {
	tests := []struct {
		input string
		cmd   string
		args  []string
	}{
		{"help", "help", nil},
		{"pair eth", "pair", []string{"eth"}},
		{"htlc 42", "htlc", []string{"42"}},
		{"accept abc123", "accept", []string{"abc123"}},
		{"  HELP  ", "help", nil},
		{"", "", nil},
	}
	for _, tt := range tests {
		cmd, args := ParseCommand(tt.input)
		if cmd != tt.cmd {
			t.Errorf("ParseCommand(%q) cmd = %q, want %q", tt.input, cmd, tt.cmd)
		}
		if len(args) != len(tt.args) {
			t.Errorf("ParseCommand(%q) args = %v, want %v", tt.input, args, tt.args)
		}
	}
}
```

- [ ] **Step 2: Run test — expect compile failure**

Run: `cd /home/ar/fuego/swaphub && go test ./app/ -run TestParseCommand -v`
Expected: Compile error — `ParseCommand` undefined.

- [ ] **Step 3: Create input.go**

```go
// swaphub/app/input.go
package app

import (
	"fmt"
	"strings"
	"time"

	"github.com/charmbracelet/lipgloss"
)

// ParseCommand splits raw input into command + args, normalizing case.
func ParseCommand(raw string) (string, []string) {
	parts := strings.Fields(raw)
	if len(parts) == 0 {
		return "", nil
	}
	cmd := strings.ToLower(parts[0])
	if len(parts) > 1 {
		return cmd, parts[1:]
	}
	return cmd, nil
}

// InputView renders the command bar at the bottom of the screen.
func InputView(input string, status string, statusAt time.Time, connected bool, balanceXFG float64, width int) string {
	// Status line (fades after 5s)
	var statusLine string
	if status != "" && time.Since(statusAt) < 5*time.Second {
		statusLine = StyleStatus.Render("  " + status)
	}

	// Prompt
	prompt := StyleInput.Render("  > " + input + "\u2588")

	// Right side: balance + connection
	var right string
	if balanceXFG >= 0 {
		right = fmt.Sprintf("BAL %.2f XFG", balanceXFG)
	}
	if connected {
		right += "  " + StyleBull.Render("\u25a0") + " connected"
	} else {
		right += "  " + StyleBear.Render("\u25a0") + " offline"
	}
	right = StyleMuted.Render(right)

	// Compose — use lipgloss.Width for styled/unicode strings
	gap := width - lipgloss.Width(prompt) - lipgloss.Width(right)
	if gap < 1 {
		gap = 1
	}

	promptLine := prompt + strings.Repeat(" ", gap) + right

	if statusLine != "" {
		return strings.Join([]string{statusLine, promptLine}, "\n")
	}
	return promptLine
}
```

- [ ] **Step 4: Run test — expect PASS**

Run: `cd /home/ar/fuego/swaphub && go test ./app/ -run TestParseCommand -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add swaphub/app/input.go swaphub/app/input_test.go
git commit -m "swaphub M1: command input bar with parser"
```

---

### Task 12: Main TUI model — wires everything together

**Files:**
- Create: `swaphub/app/tui.go`
- Modify: `swaphub/app/app.go` (replace stub with real orchestration)

- [ ] **Step 1: Create tui.go — main bubbletea model**

```go
// swaphub/app/tui.go
package app

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

type tuiModel struct {
	cfg       Config
	client    *FuegoClient
	width     int
	height    int

	// Data
	allData   *AllPairData
	trades    map[uint8][]SwapTrade
	connected bool
	lastFetch time.Time

	// UI state
	selected   uint8  // active pair
	input      string
	focusInput bool   // true = keystrokes go to input, false = hotkeys active
	status     string
	statusAt   time.Time
}

type refreshMsg struct {
	data   *AllPairData
	trades []SwapTrade
	pair   uint8 // pair trades were fetched for
	err    error
}

type tickMsg time.Time

func newTuiModel(cfg Config, client *FuegoClient) tuiModel {
	return tuiModel{
		cfg:      cfg,
		client:   client,
		selected: cfg.StartPair,
		allData:  &AllPairData{Offers: make(map[uint8][]SwapOffer), Prices: make(map[uint8]*SwapPriceResponse)},
		trades:   make(map[uint8][]SwapTrade),
	}
}

func (m tuiModel) Init() tea.Cmd {
	return tea.Batch(
		m.doRefresh(),
		tea.Tick(5*time.Second, func(t time.Time) tea.Msg { return tickMsg(t) }),
	)
}

func (m tuiModel) doRefresh() tea.Cmd {
	pair := m.selected // capture current pair to avoid race
	return func() tea.Msg {
		data, err := m.client.FetchAllPairs()
		var trades []SwapTrade
		if err == nil {
			trades, _ = m.client.GetTrades(pair, 50)
		}
		return refreshMsg{data: data, trades: trades, pair: pair, err: err}
	}
}

func (m tuiModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		return m.handleKey(msg)

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		return m, nil

	case refreshMsg:
		if msg.err != nil {
			m.connected = false
			m.status = "daemon offline: " + msg.err.Error()
			m.statusAt = time.Now()
		} else {
			m.allData = msg.data
			m.connected = true
			if msg.trades != nil {
				m.trades[msg.pair] = msg.trades
			}
		}
		m.lastFetch = time.Now()
		return m, nil

	case tickMsg:
		var cmds []tea.Cmd
		if time.Since(m.lastFetch) > 4*time.Second {
			cmds = append(cmds, m.doRefresh())
		}
		cmds = append(cmds, tea.Tick(5*time.Second, func(t time.Time) tea.Msg { return tickMsg(t) }))
		return m, tea.Batch(cmds...)
	}
	return m, nil
}

func (m *tuiModel) handleKey(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	key := msg.String()

	// ctrl+c always quits
	if key == "ctrl+c" {
		return m, tea.Quit
	}

	// Input-focused mode: all printable keys go to input buffer
	if m.focusInput {
		switch key {
		case "enter":
			m.processInput()
			m.input = ""
			m.focusInput = false
			return m, nil
		case "esc":
			m.input = ""
			m.focusInput = false
			return m, nil
		case "backspace":
			if len(m.input) > 0 {
				m.input = m.input[:len(m.input)-1]
			}
			return m, nil
		default:
			if len(key) == 1 && key[0] >= 32 {
				m.input += key
			}
		}
		return m, nil
	}

	// Hotkey mode: single keys trigger actions
	switch key {
	case "q":
		return m, tea.Quit
	case "1", "2", "3", "4", "5":
		n, _ := strconv.Atoi(key)
		if pair, ok := HotkeyToPair(n); ok {
			m.selected = pair
			m.status = "Switched to " + PairName(pair)
			m.statusAt = time.Now()
			return m, m.doRefresh()
		}
	case "tab":
		for i, p := range ActivePairs {
			if p == m.selected {
				m.selected = ActivePairs[(i+1)%len(ActivePairs)]
				m.status = "Switched to " + PairName(m.selected)
				m.statusAt = time.Now()
				return m, m.doRefresh()
			}
		}
	case "r":
		m.status = "Refreshing..."
		m.statusAt = time.Now()
		return m, m.doRefresh()
	case "/":
		m.focusInput = true
		return m, nil
	case "?":
		m.status = "1-4=pairs Tab=cycle /=command b=buy s=sell h=htlc r=refresh q=quit"
		m.statusAt = time.Now()
		return m, nil
	case "b":
		m.status = "buy order entry requires wallet connection (M2)"
		m.statusAt = time.Now()
		return m, nil
	case "s":
		m.status = "sell order entry requires wallet connection (M2)"
		m.statusAt = time.Now()
		return m, nil
	case "h":
		// Show HTLC count
		cnt, err := m.client.GetHtlcCount()
		if err != nil {
			m.status = "HTLC query failed: " + err.Error()
		} else {
			m.status = fmt.Sprintf("HTLC outputs on chain: %d — type /htlc <index> to inspect", cnt)
		}
		m.statusAt = time.Now()
		return m, nil
	case "esc":
		return m, nil
	default:
		// Any printable key enters input focus mode
		if len(key) == 1 && key[0] >= 32 {
			m.focusInput = true
			m.input = key
		}
	}
	return m, nil
}

func (m *tuiModel) processInput() {
	cmd, args := ParseCommand(m.input)
	switch cmd {
	case "":
		return
	case "pair":
		if len(args) < 1 {
			m.status = "Usage: pair <eth|sol|bch|heat|lusd>"
		} else if p, ok := PairFromString(args[0]); ok {
			m.selected = p
			m.status = "Switched to " + PairName(p)
		} else {
			m.status = "Unknown pair: " + args[0]
		}
	case "htlc":
		if len(args) < 1 {
			cnt, err := m.client.GetHtlcCount()
			if err != nil {
				m.status = "HTLC query failed: " + err.Error()
			} else {
				m.status = fmt.Sprintf("HTLC outputs on chain: %d — Usage: htlc <index>", cnt)
			}
		} else {
			idx, err := strconv.ParseUint(args[0], 10, 32)
			if err != nil {
				m.status = "Invalid index: " + args[0]
			} else {
				htlc, err := m.client.GetHtlc(uint32(idx))
				if err != nil {
					m.status = "HTLC query failed: " + err.Error()
				} else {
					xfg := float64(htlc.Amount) / 1e7
					state := "active"
					if htlc.IsSpent {
						state = "spent"
					}
					hash := htlc.HashLock
					if len(hash) > 16 {
						hash = hash[:16]
					}
					m.status = fmt.Sprintf("HTLC #%d: %.2f XFG timeout=%d %s hash=%s...", idx, xfg, htlc.TimeoutHeight, state, hash)
				}
			}
		}
	case "help":
		m.status = "pair <name> | htlc [idx] | accept <id> (M2) | 1-4=pairs | r=refresh | q=quit"
	case "accept":
		m.status = "accept requires wallet connection (M2)"
	case "offer":
		m.status = "offer requires wallet connection (M2)"
	case "cancel":
		m.status = "cancel requires wallet connection (M2)"
	case "connect":
		if len(args) > 0 && args[0] == "metamask" {
			m.status = "MetaMask bridge requires M3"
		} else {
			m.status = "wallet connection requires M2"
		}
	default:
		m.status = "Unknown: " + cmd + " (type 'help')"
	}
	m.statusAt = time.Now()
}

func (m tuiModel) View() string {
	if m.width == 0 {
		return ""
	}

	layout := CalcLayout(m.width, m.height, m.cfg.Compact)

	// Ticker bar
	ticker := TickerView(m.allData.Prices, m.selected, m.allData.Height, m.connected, layout.TermW)

	// Chart
	trades := m.trades[m.selected]
	candles := BucketCandles(trades, 5*time.Minute)
	twap := ""
	composite := ""
	if p := m.allData.Prices[m.selected]; p != nil {
		twap = p.Twap
		composite = p.CompositeRate
	}
	chart := ChartView(candles, twap, composite, layout.ChartW, layout.ChartH)

	// Orderbook
	offers := m.allData.Offers[m.selected]
	book := OrderbookView(offers, m.selected, layout.BookW, layout.BookH)

	// Trade tape
	tape := TapeView(trades, layout.BookW, layout.TapeH)

	// Input bar
	inputBar := InputView(m.input, m.status, m.statusAt, m.connected, -1, layout.TermW)

	// Compose layout
	if layout.Compact || layout.BookW <= 0 {
		// Compact: chart only, no side panels
		return lipgloss.JoinVertical(lipgloss.Left,
			ticker,
			chart,
			inputBar,
		)
	}

	// Normal: chart left, book+tape right
	rightPanel := lipgloss.JoinVertical(lipgloss.Left, book, "", tape)

	body := lipgloss.JoinHorizontal(lipgloss.Top,
		chart,
		" ", // separator
		rightPanel,
	)

	return lipgloss.JoinVertical(lipgloss.Left,
		ticker,
		body,
		inputBar,
	)
}
```

- [ ] **Step 2: Update app.go with real Run() orchestration**

```go
// swaphub/app/app.go
package app

import (
	"fmt"

	tea "github.com/charmbracelet/bubbletea"
)

// Run starts the swaphub: optional splash → main TUI.
func Run(cfg Config) error {
	// Phase 1: splash screen
	if !cfg.NoSplash {
		splash := newSplashModel()
		p := tea.NewProgram(splash, tea.WithAltScreen())
		if _, err := p.Run(); err != nil {
			return fmt.Errorf("splash: %w", err)
		}
	}

	// Phase 2: main TUI
	client := NewFuegoClient(cfg.DaemonRPC)
	tui := newTuiModel(cfg, client)

	p := tea.NewProgram(tui, tea.WithAltScreen(), tea.WithMouseCellMotion())
	if _, err := p.Run(); err != nil {
		return fmt.Errorf("tui: %w", err)
	}

	return nil
}
```

- [ ] **Step 3: Build to verify everything compiles**

Run: `cd /home/ar/fuego/swaphub && go build -o swaphub .`
Expected: Compiles cleanly.

- [ ] **Step 4: Run all tests**

Run: `cd /home/ar/fuego/swaphub && go test ./app/ -v`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add swaphub/app/tui.go swaphub/app/app.go
git commit -m "swaphub M1: main TUI model with layout composition and data refresh"
```

---

### Task 13: Manual smoke test and polish

**Files:**
- Possibly modify: various `swaphub/app/*.go` for fixes found during testing

- [ ] **Step 1: Run swaphub with --no-splash against a live daemon**

Run: `cd /home/ar/fuego/swaphub && ./swaphub --no-splash`

Verify:
- Ticker bar shows at top with all 4 pairs
- Connection indicator shows green if daemon is running, red if not
- Block height displays
- Chart area renders (empty if no trades)
- Orderbook renders (empty if no offers)
- Trade tape renders

- [ ] **Step 2: Test splash screen**

Run: `cd /home/ar/fuego/swaphub && ./swaphub`

Verify:
- Doom fire animation plays
- Fuego diamond renders
- SwapHub ASCII title displays
- Auto-advances after ~3 seconds or on keypress

- [ ] **Step 3: Test hotkeys and commands**

In the running TUI:
- Press `1`, `2`, `3`, `4` — pair switching works
- Press `Tab` — cycles through pairs
- Press `r` — refreshes
- Press `?` — shows help
- Type `help` + Enter — shows command help
- Type `pair bch` + Enter — switches pair
- Type `htlc` + Enter — shows HTLC count (or error if no daemon)
- Press `q` — quits

- [ ] **Step 4: Test testnet mode**

Run: `./swaphub --testnet --pair heat --no-splash`

Verify: daemon endpoint is `http://127.0.0.1:28280`, starting pair is HEAT.

- [ ] **Step 5: Test compact mode**

Run: `./swaphub --compact --no-splash`

Verify: Only chart renders (no side panels).

- [ ] **Step 6: Fix any issues found and commit**

```bash
git add swaphub/
git commit -m "swaphub M1: smoke test fixes and polish"
```

---

### Task 14: Final build and tag

- [ ] **Step 1: Run full test suite**

Run: `cd /home/ar/fuego/swaphub && go test ./app/ -v -count=1`
Expected: All tests pass.

- [ ] **Step 2: Build release binary**

Run: `cd /home/ar/fuego/swaphub && go build -o swaphub . && ls -la swaphub`
Expected: Binary exists, reasonable size (~5-15MB).

- [ ] **Step 3: Verify --help output matches spec §1.6**

Run: `./swaphub --help`

Verify output includes:
- `--daemon`, `-d`
- `--testnet`
- `--pair`, `-p` with valid pairs listed
- `--no-splash`
- `--compact`

- [ ] **Step 4: Final commit**

```bash
git add -A swaphub/
git commit -m "swaphub M1 complete: standalone read-only trading terminal

Multi-pair TUI with candlestick charts, orderbook, trade tape,
market ticker, doom fire splash, and parallel daemon RPC fetching.
Supports ETH, SOL, BCH, HEAT, LUSD pairs."
```

---

## Milestone Boundary

This plan covers **M1 only**. After M1 is complete and merged, the following milestones each get their own plan:

| Milestone | Summary | Key dependency |
|-----------|---------|----------------|
| **M2** | Wallet RPC integration (offer/accept/cancel/HTLC ops) | Wallet RPC endpoints must exist in WalletRpcServer |
| **M3** | MetaMask + Phantom bridges (ETH/HEAT/LUSD + SOL signing) | M2 (needs wallet for XFG side) |
| **M4** | BCH connection (Electron Cash RPC) | M2 |
| **M5** | EFier web UI (embedded in fuegod, TradingView charts) | Independent of swaphub binary |
| **M6** | Wallet `swap` command rewrite (fork/execvp swaphub) | M2 |
