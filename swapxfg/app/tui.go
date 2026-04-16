// swapxfg/app/tui.go
package app

import (
	"fmt"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

// refreshInterval controls the data polling rate.
const refreshInterval = 5 * time.Second

// tuiModel is the main bubbletea model for the trading terminal.
type tuiModel struct {
	cfg    Config
	client *FuegoClient
	wallet *WalletClient // nil when --wallet not provided

	// Terminal dimensions
	width, height int

	// Market state
	activePair uint8
	data       *AllPairData
	connected  bool

	// CD market sub-model
	cdMarket CdMarketModel

	// Browser bridge (MetaMask / Phantom)
	bridge  *BridgeServer
	ethAddr string
	ethBal  string
	solAddr string
	solBal  string

	// BCH / Electron Cash
	bch    *BchClient
	bchBal string

	// Wallet state
	balance    *WalletBalance
	walletAddr string

	// Input
	cmdBuf    string
	cmdFocus  bool
	cursorOn  bool
	blinkTick int

	// Status
	lastErr   string
	statusMsg string
}

type refreshMsg struct {
	data    *AllPairData
	err     error
	balance *WalletBalance
	balErr  error
	bchBal  string // formatted BCH balance, empty if unavailable
}

type refreshTickMsg time.Time

func refreshTick() tea.Cmd {
	return tea.Tick(refreshInterval, func(t time.Time) tea.Msg {
		return refreshTickMsg(t)
	})
}

type cursorBlinkMsg time.Time

func cursorBlink() tea.Cmd {
	return tea.Tick(530*time.Millisecond, func(t time.Time) tea.Msg {
		return cursorBlinkMsg(t)
	})
}

func newTuiModel(cfg Config) tuiModel {
	m := tuiModel{
		cfg:        cfg,
		client:     NewFuegoClient(cfg.DaemonRPC),
		activePair: cfg.StartPair,
		data: &AllPairData{
			Offers:   make(map[uint8][]SwapOffer),
			Prices:   make(map[uint8]*SwapPriceResponse),
			Trades:   make(map[uint8][]SwapTrade),
			CdPrices: make(map[uint64]*CdPriceStats),
		},
		cdMarket: newCdMarketModel(),
		cursorOn: true,
	}
	if cfg.WalletRPC != "" {
		m.wallet = NewWalletClientAuth(cfg.WalletRPC, cfg.WalletUser, cfg.WalletPass)
	}
	if !cfg.NoBch && cfg.BchRPC != "" {
		m.bch = NewBchClient(cfg.BchRPC)
	}
	return m
}

func (m tuiModel) Init() tea.Cmd {
	return tea.Batch(
		m.fetchData(),
		refreshTick(),
		cursorBlink(),
	)
}

func (m tuiModel) fetchData() tea.Cmd {
	client := m.client
	wallet := m.wallet
	bch := m.bch
	return func() tea.Msg {
		data, err := client.FetchAll(ActivePairs)
		msg := refreshMsg{data: data, err: err}
		if wallet != nil {
			bal, balErr := wallet.GetBalance()
			msg.balance = bal
			msg.balErr = balErr
		}
		if bch != nil {
			if bal, berr := bch.GetBalance(); berr == nil {
				msg.bchBal = FormatBchBalance(bal)
			}
		}
		return msg
	}
}

func (m tuiModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height

	case tea.KeyMsg:
		return m.handleKey(msg)

	case refreshMsg:
		if msg.err != nil {
			m.connected = false
			m.lastErr = msg.err.Error()
		} else {
			m.connected = true
			m.lastErr = ""
			m.data = msg.data
			m.cdMarket.offers = msg.data.CdOffers
			m.cdMarket.prices = msg.data.CdPrices
		}
		if msg.balance != nil {
			m.balance = msg.balance
		}
		if msg.bchBal != "" {
			m.bchBal = msg.bchBal
		}

	case refreshTickMsg:
		return m, tea.Batch(m.fetchData(), refreshTick())

	case cursorBlinkMsg:
		m.blinkTick++
		m.cursorOn = m.blinkTick%2 == 0
		return m, cursorBlink()
	}

	return m, nil
}

func (m tuiModel) handleKey(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	k := msg.String()

	// Quit
	if k == "q" && !m.cmdFocus {
		return m, tea.Quit
	}
	if k == "esc" {
		if m.cmdFocus {
			m.cmdFocus = false
			m.cmdBuf = ""
			return m, nil
		}
		return m, tea.Quit
	}
	if k == "ctrl+c" {
		return m, tea.Quit
	}

	// Command input mode
	if m.cmdFocus {
		switch k {
		case "enter":
			m.handleCommand(m.cmdBuf)
			m.cmdBuf = ""
			m.cmdFocus = false
		case "backspace":
			if len(m.cmdBuf) > 0 {
				m.cmdBuf = m.cmdBuf[:len(m.cmdBuf)-1]
			}
		default:
			if len(k) == 1 {
				m.cmdBuf += k
			}
		}
		return m, nil
	}

	// Normal mode hotkeys
	switch k {
	case "/":
		m.cmdFocus = true
		return m, nil
	case "tab":
		m.activePair = nextPair(m.activePair)
	case "r":
		return m, m.fetchData()
	case "?":
		m.statusMsg = "0-3: pairs  c: CD market  /: cmd  r: refresh  q: quit"
	case "up":
		if m.activePair == PairCD {
			m.cdMarket.moveUp()
		}
	case "down":
		if m.activePair == PairCD {
			m.cdMarket.moveDown()
		}
	case "enter":
		if m.activePair == PairCD {
			if o := m.cdMarket.selectedOffer(); o != nil {
				m.statusMsg = fmt.Sprintf("accept offer %s (enter: confirm <addr> <amt> <pair>)", o.OfferID[:min(12, len(o.OfferID))])
			}
		}
	default:
		if len(k) == 1 {
			r := rune(k[0])
			if p := HotkeyPair(r); p != 255 {
				m.activePair = p
			}
		}
	}

	return m, nil
}

func (m *tuiModel) handleCommand(cmd string) {
	cmd = strings.TrimSpace(cmd)
	if cmd == "" {
		return
	}
	parts := strings.Fields(cmd)
	switch parts[0] {
	case "pair":
		if len(parts) > 1 {
			p := PairFromString(parts[1])
			if p != 255 {
				m.activePair = p
			} else {
				m.statusMsg = "unknown pair: " + parts[1]
			}
		}
	case "initiate":
		// Usage: initiate <alias_or_address> <amount_xfg> <pair>
		if len(parts) < 4 {
			m.statusMsg = "usage: initiate <alias_or_address> <amount> <pair>"
			return
		}
		aliasOrAddr := parts[1]
		// Resolve alias if needed
		addr := aliasOrAddr
		// XFG addresses are 98 chars and start with lowercase 'f'
		if !strings.HasPrefix(aliasOrAddr, "f") || len(aliasOrAddr) < 98 {
			// Looks like an alias — try to resolve
			candidate := strings.TrimPrefix(aliasOrAddr, "@")
			if resolved, ok := m.client.ResolveAlias(candidate); ok {
				addr = resolved
				m.statusMsg = "resolved " + aliasOrAddr + " → " + addr[:12] + "..."
			} else {
				// Not found as alias — use as-is (may be a raw address)
				addr = aliasOrAddr
				m.statusMsg = fmt.Sprintf("using address: %s...", addr[:min(12, len(addr))])
			}
		}
		// Show the resolved address to user
		m.statusMsg = fmt.Sprintf("address: %s (enter 'confirm %s %s %s' to proceed)", addr[:16]+"...", addr, parts[2], parts[3])
	case "confirm":
		// confirm <address> <amount_xfg> <pair> [role]
		if len(parts) < 4 {
			m.statusMsg = "usage: confirm <address> <amount_xfg> <pair> [role]"
			return
		}
		if m.wallet == nil {
			m.statusMsg = "no wallet connected (use --wallet <endpoint>)"
			return
		}
		// Validate XFG/XMR-family address format before launching goroutine.
		if err := validateAddress("xfg", parts[1]); err != nil {
			m.statusMsg = "invalid address: " + err.Error()
			return
		}
		m.statusMsg = "signing offer..."
		go func(addr, amtStr, pair string) {
			// Parse amount with bounds checking.
			xfgAtomic, err := parseAmountAtomic(amtStr, 1e7)
			if err != nil {
				m.statusMsg = "invalid amount: " + err.Error()
				return
			}
			signed, err := m.wallet.SignOffer(xfgAtomic, 0, pairToID(pair), 144, true)
			if err != nil {
				m.statusMsg = "sign_offer failed: " + err.Error()
				return
			}
			// Submit offer to daemon
			offerReq := map[string]interface{}{
				"offerId":      signed.OfferID,
				"xfgAmount":    xfgAtomic,
				"rateNum":      uint64(0),
				"pair":         pairToID(pair),
				"makerPubKey":  signed.MakerPubKey,
				"signature":    signed.Signature,
				"timestamp":    signed.Timestamp,
				"ttlBlocks":    uint32(144),
				"postedHeight": uint32(0),
				"isSell":       true,
			}
			var submitResp struct{ Status string `json:"status"` }
			if err := m.client.post("/submitswap", offerReq, &submitResp); err != nil {
				m.statusMsg = "submit failed: " + err.Error()
				return
			}
			m.statusMsg = "offer posted: " + signed.OfferID[:12] + "..."
		}(parts[1], parts[2], parts[3])

	case "swap":
		// swap initiate <amount> <peer_pubkey> <pair> [role]
		if len(parts) < 5 {
			m.statusMsg = "usage: swap initiate <amount> <peer_pubkey> <pair> [role]"
			return
		}
		if parts[1] != "initiate" {
			m.statusMsg = "usage: swap initiate <amount> <peer_pubkey> <pair> [role]"
			return
		}
		if m.wallet == nil {
			m.statusMsg = "no wallet connected (use --wallet <endpoint>)"
			return
		}
		go func(amtStr, peerPub, pair, role string) {
			xfgAtomic, err := parseAmountAtomic(amtStr, 1e7)
			if err != nil {
				m.statusMsg = "invalid amount: " + err.Error()
				return
			}
			result, err := m.wallet.InitiateSwap(xfgAtomic, peerPub, pair, role)
			if err != nil {
				m.statusMsg = "initiate_swap failed: " + err.Error()
				return
			}
			m.statusMsg = fmt.Sprintf("swap %s started | escrow: %s... | share: pubkey=%s nonce0=%s",
				result.SwapID, result.EscrowKey[:12], result.OurPubKey[:12], result.Nonce0[:12])
		}(parts[2], parts[3], parts[4], func() string {
			if len(parts) > 5 {
				return parts[5]
			}
			return "alice"
		}())

	case "connect":
		// connect metamask | connect phantom
		if len(parts) < 2 {
			m.statusMsg = "usage: connect metamask | connect phantom"
			return
		}
		if m.bridge == nil {
			m.statusMsg = "bridge not running (start with --bridge-port or omit --no-bridge)"
			return
		}
		switch parts[1] {
		case "metamask":
			if err := m.bridge.OpenEthBridge(); err != nil {
				m.statusMsg = "open eth bridge: " + err.Error()
				return
			}
			m.statusMsg = fmt.Sprintf("MetaMask bridge at %s — open it in your browser", m.bridge.EthURL())
			go func() {
				if addr, err := m.bridge.EthGetAddress(); err == nil {
					m.ethAddr = addr
					if bal, err2 := m.bridge.EthGetBalance(addr); err2 == nil {
						m.ethBal = bal
					}
					m.statusMsg = "MetaMask connected: " + addr[:min(12, len(addr))] + "..."
				}
			}()
		case "phantom":
			if err := m.bridge.OpenSolBridge(); err != nil {
				m.statusMsg = "open sol bridge: " + err.Error()
				return
			}
			m.statusMsg = fmt.Sprintf("Phantom bridge at %s — open it in your browser", m.bridge.SolURL())
		case "bch":
			if m.bch == nil {
				m.statusMsg = "BCH not configured (use --bch-rpc <endpoint>)"
				return
			}
			go func() {
				if m.bch.IsConnected() {
					if bal, err := m.bch.GetBalance(); err == nil {
						m.bchBal = FormatBchBalance(bal)
						m.statusMsg = "BCH connected: " + m.bchBal
					} else {
						m.statusMsg = "BCH connected but balance error: " + err.Error()
					}
				} else {
					m.statusMsg = "BCH: cannot connect to Electron Cash at " + m.bch.endpoint
				}
			}()
		default:
			m.statusMsg = "usage: connect metamask | connect phantom | connect bch"
		}

	case "bch":
		// bch lock <amount_bch> <hashlock_hex> <timeout_blocks> <counterparty_bch_addr>
		// bch claim <htlc_txid> <htlc_vout> <preimage_hex>
		// bch refund <htlc_txid> <htlc_vout>
		if len(parts) < 2 {
			m.statusMsg = "usage: bch lock|claim|refund ..."
			return
		}
		if m.bch == nil {
			m.statusMsg = "BCH not configured (use --bch-rpc or connect bch)"
			return
		}
		switch parts[1] {
		case "lock":
			if len(parts) < 6 {
				m.statusMsg = "usage: bch lock <amount_bch> <hashlock_hex> <timeout_blocks> <counterparty_bch_addr>"
				return
			}
			_, _, _, _ = parts[2], parts[3], parts[4], parts[5]
			m.statusMsg = fmt.Errorf("bch lock: not yet implemented — SwapDaemon must expose an HTLC P2SH address endpoint first").Error()

		case "claim":
			if len(parts) < 5 {
				m.statusMsg = "usage: bch claim <htlc_txid> <htlc_vout> <preimage_hex>"
				return
			}
			htlcTxid, htlcVout, preimage := parts[2], parts[3], parts[4]
			go func(txid, vout, pre string) {
				// SwapDaemon (via /processswap) builds the claim tx using C++ HtlcScript.
				// Trigger processSwap then broadcast the resulting tx.
				var processResp struct {
					Advanced bool   `json:"advanced"`
					NewState string `json:"new_state"`
					Status   string `json:"status"`
				}
				req := map[string]interface{}{"swap_id": txid + ":" + vout}
				if err := m.client.post("/processswap", req, &processResp); err != nil {
					m.statusMsg = "bch claim (processswap) failed: " + err.Error()
					return
				}
				if processResp.Advanced {
					m.statusMsg = fmt.Sprintf("bch claim: swap advanced → %s (preimage: %s...)", processResp.NewState, pre[:min(8, len(pre))])
				} else {
					m.statusMsg = "bch claim: swap not yet advanceable (check chain state)"
				}
			}(htlcTxid, htlcVout, preimage)

		case "refund":
			if len(parts) < 4 {
				m.statusMsg = "usage: bch refund <htlc_txid> <htlc_vout>"
				return
			}
			htlcTxid, htlcVout := parts[2], parts[3]
			go func(txid, vout string) {
				var refundResp struct{ Status string `json:"status"` }
				req := map[string]interface{}{"swap_id": txid + ":" + vout}
				if err := m.client.post("/refundswap", req, &refundResp); err != nil {
					m.statusMsg = "bch refund failed: " + err.Error()
					return
				}
				m.statusMsg = "bch refund: " + refundResp.Status
			}(htlcTxid, htlcVout)

		default:
			m.statusMsg = "usage: bch lock|claim|refund ..."
		}

	case "eth":
		// eth lock <amount_eth> <htlc_contract> <hashlock> <timeout>
		if len(parts) < 5 || parts[1] != "lock" {
			m.statusMsg = "usage: eth lock <amount_wei> <htlc_contract> <hashlock> <timeout_hex>"
			return
		}
		if m.bridge == nil || !m.bridge.IsConnected() {
			m.statusMsg = "MetaMask not connected (try: connect metamask)"
			return
		}
		amtWei, htlcAddr, hashlock, timeout := parts[2], parts[3], parts[4], ""
		if len(parts) > 5 {
			timeout = parts[5]
		}
		if err := validateETHAddress(htlcAddr); err != nil {
			m.statusMsg = "invalid HTLC contract address: " + err.Error()
			return
		}
		if _, err := parseAmount(amtWei); err != nil {
			m.statusMsg = "invalid amount: " + err.Error()
			return
		}
		calldata, calldataErr := buildHTLCLockCalldata(hashlock, timeout)
		if calldataErr != nil {
			m.statusMsg = "eth calldata error: " + calldataErr.Error()
			return
		}
		go func(to, value, data string) {
			txHash, err := m.bridge.EthSendTransaction(to, value, data)
			if err != nil {
				m.statusMsg = "eth lock failed: " + err.Error()
				return
			}
			m.statusMsg = "eth lock tx: " + txHash[:min(20, len(txHash))] + "..."
		}(htlcAddr, amtWei, calldata)

	case "sell":
		// sell cd <key_image> <ask_price_xfg>
		if len(parts) < 4 || parts[1] != "cd" {
			m.statusMsg = "usage: sell cd <key_image> <ask_price_xfg>"
			return
		}
		if m.wallet == nil {
			m.statusMsg = "no wallet connected (use --wallet <endpoint>)"
			return
		}
		keyImage := parts[2]
		askAtomic, err := parseAmountAtomic(parts[3], 1e7)
		if err != nil {
			m.statusMsg = "invalid ask price: " + err.Error()
			return
		}
		go func(ki string, ask uint64) {
			// wallet must sign the offer — stub until wallet endpoint added
			m.statusMsg = fmt.Sprintf("sell cd: key_image=%s ask=%.7f XFG (signing not yet wired)", ki, float64(ask)/1e7)
		}(keyImage, askAtomic)

	case "cancel":
		// cancel <offer_id>
		if len(parts) < 2 {
			m.statusMsg = "usage: cancel <offer_id>"
			return
		}
		if m.wallet == nil {
			m.statusMsg = "no wallet connected"
			return
		}
		offerID := parts[1]
		go func(id string) {
			if err := m.client.CancelCdOffer(id, "", ""); err != nil {
				m.statusMsg = "cancel failed: " + err.Error()
			} else {
				m.statusMsg = "offer cancelled: " + id[:min(12, len(id))]
			}
		}(offerID)

	case "accept":
		// accept [offer_id]  — uses selected row if no arg
		var offerID string
		if len(parts) >= 2 {
			offerID = parts[1]
		} else if o := m.cdMarket.selectedOffer(); o != nil {
			offerID = o.OfferID
		} else {
			m.statusMsg = "usage: accept <offer_id> (or select a row in CD tab)"
			return
		}
		if offerID == "" {
			m.statusMsg = "No CD offer selected"
			return
		}
		if m.wallet == nil {
			m.statusMsg = "no wallet connected (use --wallet <endpoint>)"
			return
		}
		go func(id string) {
			resp, err := m.client.AcceptCdOffer(id, "")
			if err != nil {
				m.statusMsg = "accept failed: " + err.Error()
				return
			}
			m.statusMsg = fmt.Sprintf("partial tx ready (expires blk %d): %s...", resp.ExpiresAt, resp.PartialTx[:min(20, len(resp.PartialTx))])
		}(offerID)

	case "help":
		m.statusMsg = "pair <name> | c: CD | connect metamask|phantom|bch | bch lock|claim|refund | eth lock | sell cd | accept [id] | initiate <alias> <amt> <pair> | confirm <addr> <amt> <pair> | q: quit"
	default:
		m.statusMsg = "unknown: " + cmd + " (type help)"
	}
}

// pairToID converts a pair name string to its numeric ID.
func pairToID(pair string) uint8 {
	return PairFromString(strings.ToLower(pair))
}

// allPairsWithCD includes CD as the last tab in the rotation.
var allPairsWithCD = append(ActivePairs, PairCD)

func nextPair(cur uint8) uint8 {
	for i, p := range allPairsWithCD {
		if p == cur {
			return allPairsWithCD[(i+1)%len(allPairsWithCD)]
		}
	}
	return allPairsWithCD[0]
}

// ─── view ─────────────────────────────────────────────────────────────

func (m tuiModel) View() string {
	if m.width == 0 {
		return ""
	}

	w := m.width
	h := m.height

	// Layout rows: ticker(2) + main(h-4) + input(1) + status(1)
	tickerH := 1
	inputH := 1
	statusH := 1
	mainH := h - tickerH - inputH - statusH - 1 // -1 for borders
	if mainH < 5 {
		mainH = 5
	}

	// ── Ticker ──
	ticker := RenderTickerWithCD(m.activePair, m.data.Prices, m.data.CdOffers, m.data.Height, w, m.connected)

	// ── Main area ──
	var mainArea string
	if m.activePair == PairCD {
		mainArea = RenderCdMarket(&m.cdMarket, w, mainH)
	} else {
		// chart (left 60%) | orderbook+tape (right 40%)
		rightW := w * 38 / 100
		if rightW < 30 {
			rightW = 30
		}
		leftW := w - rightW - 3 // 3 for border
		if leftW < 20 {
			leftW = 20
		}

		chartH := mainH - 2
		if chartH < 3 {
			chartH = 3
		}
		trades := m.data.Trades[m.activePair]
		chart := RenderChart(trades, leftW, chartH)
		priceLine := RenderPriceLine(m.activePair, m.data.Prices)
		leftPanel := lipgloss.JoinVertical(lipgloss.Left, chart, priceLine)

		obH := mainH * 55 / 100
		if obH < 5 {
			obH = 5
		}
		tapeH := mainH - obH
		if tapeH < 3 {
			tapeH = 3
		}

		offers := m.data.Offers[m.activePair]
		ob := RenderOrderbook(offers, rightW, obH)
		tape := RenderTape(m.data.Trades[m.activePair], rightW, tapeH)
		rightPanel := lipgloss.JoinVertical(lipgloss.Left, ob, tape)

		sep := lipgloss.NewStyle().Foreground(ColorMuted).Render(
			strings.Repeat("│\n", mainH))
		mainArea = lipgloss.JoinHorizontal(lipgloss.Top, leftPanel, sep, rightPanel)
	}

	// ── Input bar ──
	xfgBal := ""
	if m.wallet != nil && m.balance != nil {
		xfgBal = FormatBalance(m.balance.Available)
	} else if m.wallet != nil {
		xfgBal = "syncing..."
	}
	// ETH balance: convert wei → ETH for display
	ethBalStr := ""
	if m.ethBal != "" {
		var weiF float64
		fmt.Sscanf(m.ethBal, "%f", &weiF)
		ethBalStr = fmt.Sprintf("%.4f ETH", weiF/1e18)
	}
	// Combined right-side balance: XFG + ETH inline, BCH handled by input bar
	if ethBalStr != "" {
		if xfgBal != "" {
			xfgBal += "  " + ethBalStr
		} else {
			xfgBal = ethBalStr
		}
	}
	inputBar := RenderInputBar(m.cmdBuf, m.cursorOn && m.cmdFocus, xfgBal, m.bchBal, m.cfg.DaemonRPC, m.connected, w)

	// ── Status ──
	status := ""
	if m.lastErr != "" {
		status = StyleStatus.Render(m.lastErr)
	} else if m.statusMsg != "" {
		status = StyleMuted.Render(m.statusMsg)
	}

	// ── Border ──
	hline := lipgloss.NewStyle().Foreground(ColorMuted).Render(strings.Repeat("─", w))

	return lipgloss.JoinVertical(lipgloss.Left,
		ticker,
		hline,
		mainArea,
		hline,
		inputBar,
		status,
	)
}
