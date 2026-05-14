// swapxfg/app/tui.go
package app

import (
	"fmt"
	"strconv"
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

// statusUpdateMsg carries a plain status string back to Update() from a
// tea.Cmd goroutine.  Using this instead of writing m.statusMsg directly
// inside a goroutine eliminates the data race on the model field.
type statusUpdateMsg struct{ text string }

// ethConnectedMsg is returned by the MetaMask connect tea.Cmd once the
// bridge has retrieved the wallet address and balance.
type ethConnectedMsg struct {
	addr string
	bal  string
	err  error
}

// bchConnectedMsg is returned by the BCH connect tea.Cmd once the
// Electron Cash RPC has returned the balance.
type bchConnectedMsg struct {
	bal string
	err error
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

	case statusUpdateMsg:
		m.statusMsg = msg.text

	case ethConnectedMsg:
		if msg.err != nil {
			m.statusMsg = "MetaMask error: " + msg.err.Error()
		} else {
			m.ethAddr = msg.addr
			m.ethBal = msg.bal
			m.statusMsg = "MetaMask connected: " + msg.addr[:min(12, len(msg.addr))] + "..."
		}

	case bchConnectedMsg:
		if msg.err != nil {
			m.statusMsg = "BCH error: " + msg.err.Error()
		} else {
			m.bchBal = msg.bal
			m.statusMsg = "BCH connected: " + msg.bal
		}
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
			cmd := m.handleCommand(m.cmdBuf)
			m.cmdBuf = ""
			m.cmdFocus = false
			return m, cmd
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

// handleCommand parses and dispatches a TUI command string, returning a
// tea.Cmd for any asynchronous work that must not mutate model state directly.
// Goroutines that previously wrote m.statusMsg / m.ethAddr / m.bchBal directly
// have been converted to tea.Cmd functions that return typed messages handled
// in Update().
func (m *tuiModel) handleCommand(cmd string) tea.Cmd {
	cmd = strings.TrimSpace(cmd)
	if cmd == "" {
		return nil
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
	case "offer":
		// Usage: offer <amount_xfg> <amount_target> <pair> <timeout_hrs>
		if len(parts) < 5 {
			m.statusMsg = "usage: offer <amount_xfg> <amount_target> <pair> <timeout_hrs>"
			return nil
		}
		amtXfg, amtCtr, pair, timeout := parts[1], parts[2], parts[3], parts[4]
		m.statusMsg = fmt.Sprintf("offer %s XFG for %s %s (%s hrs) | enter 'confirm-offer %s %s %s %s' to lock",
			amtXfg, amtCtr, pair, timeout, amtXfg, amtCtr, pair, timeout)
	case "confirm-offer":
		// Usage: confirm-offer <amount_xfg> <amount_target> <pair> <timeout_hrs> [soft_order: true/false]
		if len(parts) < 5 {
			m.statusMsg = "usage: confirm-offer <amount_xfg> <amount_target> <pair> <timeout_hrs> [true/false]"
			return nil
		}
		if m.wallet == nil {
			m.statusMsg = "no wallet connected"
			return nil
		}
		amtXfg, amtCtr, pair, timeout := parts[1], parts[2], parts[3], parts[4]
		isSoftOrder := true
		if len(parts) > 5 && parts[5] == "false" {
			isSoftOrder = false
		}

		if isSoftOrder {
			m.statusMsg = "publishing soft order intent..."
		} else {
			m.statusMsg = "locking funds on-chain..."
		}
		wallet := m.wallet
		client := m.client
		return func() tea.Msg {
			xfgAtomic, err := parseAmountAtomic(amtXfg, 1e7)
			if err != nil {
				return statusUpdateMsg{"invalid amount: " + err.Error()}
			}
			var timeoutHrs uint32
			if _, err := fmt.Sscanf(timeout, "%d", &timeoutHrs); err != nil {
				return statusUpdateMsg{"invalid timeout: " + err.Error()}
			}

			// We need a rate (XFG per 1 CTR, scaled by 1e7)
			// This is a naive calculation for demonstration.
			ctrFloat, _ := strconv.ParseFloat(amtCtr, 64)
			rateFloat := (float64(xfgAtomic) / 1e7) / ctrFloat
			rateNum := uint64(rateFloat * 1e7)

			if isSoftOrder {
				res, err := wallet.SignOffer(xfgAtomic, rateNum, pairToID(pair), timeoutHrs*30, true) // ~30 blocks per hour
				if err != nil {
					return statusUpdateMsg{"sign_offer failed: " + err.Error()}
				}

				// Submit soft AFK offer to daemon
				offerReq := map[string]interface{}{
					"offerId":     res.OfferID,
					"xfgAmount":   xfgAtomic,
					"rateNum":     rateNum,
					"pair":        pairToID(pair),
					"makerPubKey": res.MakerPubKey,
					"signature":   res.Signature,
					"ttlBlocks":   timeoutHrs * 30,
					"isSoftOrder": true,
				}
				var submitResp struct {
					Status string `json:"status"`
				}
				if err := client.post("/submitswap", offerReq, &submitResp); err != nil {
					return statusUpdateMsg{"submit failed: " + err.Error()}
				}
				return statusUpdateMsg{"Soft intent posted. Wallet will auto-execute when taken: " + res.OfferID[:12] + "..."}
			} else {
				res, err := wallet.CreateAfkLock(xfgAtomic, timeoutHrs, pairToID(pair))
				if err != nil {
					return statusUpdateMsg{"create_afk_lock failed: " + err.Error()}
				}

				// Submit AFK offer to daemon
				offerReq := map[string]interface{}{
					"offerId":      res.LockID,
					"xfgAmount":    xfgAtomic,
					"ctrAmount":    amtCtr,
					"pair":         pairToID(pair),
					"adaptorPoint": res.AdaptorPoint,
					"preSig":       res.PreSig,
					"timeoutHrs":   timeoutHrs,
					"isSell":       true,
					"isSoftOrder":  false,
				}
				var submitResp struct {
					Status string `json:"status"`
				}
				if err := client.post("/submitswap", offerReq, &submitResp); err != nil {
					return statusUpdateMsg{"submit failed: " + err.Error()}
				}
				return statusUpdateMsg{"AFK offer locked and posted: " + res.LockID[:12] + "..."}
			}
		}
	case "accept":
		// accept <offer_id>
		if len(parts) < 2 {
			m.statusMsg = "usage: accept <offer_id>"
			return nil
		}
		offerID := parts[1]
		client := m.client
		return func() tea.Msg {
			// Now actually accept
			var resp struct {
				Status string `json:"status"`
			}
			if err := client.post("/accept", map[string]interface{}{"swap_id": offerID}, &resp); err != nil {
				return statusUpdateMsg{"accept failed: " + err.Error()}
			}
			return statusUpdateMsg{"Offer accepted! Please lock funds."}
		}
	case "swap":

		// swap initiate <amount> <peer_pubkey> <pair> [role]
		if len(parts) < 5 {
			m.statusMsg = "usage: swap initiate <amount> <peer_pubkey> <pair> [role]"
			return nil
		}
		if parts[1] != "initiate" {
			m.statusMsg = "usage: swap initiate <amount> <peer_pubkey> <pair> [role]"
			return nil
		}
		if m.wallet == nil {
			m.statusMsg = "no wallet connected (use --wallet <endpoint>)"
			return nil
		}
		amtStr, peerPub, pair := parts[2], parts[3], parts[4]
		role := "alice"
		if len(parts) > 5 {
			role = parts[5]
		}
		wallet := m.wallet
		return func() tea.Msg {
			xfgAtomic, err := parseAmountAtomic(amtStr, 1e7)
			if err != nil {
				return statusUpdateMsg{"invalid amount: " + err.Error()}
			}
			result, err := wallet.InitiateSwap(xfgAtomic, peerPub, pair, role)
			if err != nil {
				return statusUpdateMsg{"initiate_swap failed: " + err.Error()}
			}
			return statusUpdateMsg{fmt.Sprintf("swap %s started | escrow: %s... | share: pubkey=%s nonce0=%s",
				result.SwapID, result.EscrowKey[:12], result.OurPubKey[:12], result.Nonce0[:12])}
		}

	case "connect":
		// connect metamask | connect phantom | connect bch
		if len(parts) < 2 {
			m.statusMsg = "usage: connect metamask | connect phantom"
			return nil
		}
		if m.bridge == nil {
			m.statusMsg = "bridge not running (start with --bridge-port or omit --no-bridge)"
			return nil
		}
		switch parts[1] {
		case "metamask":
			if err := m.bridge.OpenEthBridge(); err != nil {
				m.statusMsg = "open eth bridge: " + err.Error()
				return nil
			}
			m.statusMsg = fmt.Sprintf("MetaMask bridge at %s — open it in your browser", m.bridge.EthURL())
			bridge := m.bridge
			return func() tea.Msg {
				addr, err := bridge.EthGetAddress()
				if err != nil {
					return ethConnectedMsg{err: err}
				}
				bal, _ := bridge.EthGetBalance(addr)
				return ethConnectedMsg{addr: addr, bal: bal}
			}
		case "phantom":
			if err := m.bridge.OpenSolBridge(); err != nil {
				m.statusMsg = "open sol bridge: " + err.Error()
				return nil
			}
			m.statusMsg = fmt.Sprintf("Phantom bridge at %s — open it in your browser", m.bridge.SolURL())
		case "bch":
			if m.bch == nil {
				m.statusMsg = "BCH not configured (use --bch-rpc <endpoint>)"
				return nil
			}
			bch := m.bch
			return func() tea.Msg {
				if !bch.IsConnected() {
					return bchConnectedMsg{err: fmt.Errorf("cannot connect to Electron Cash at %s", bch.endpoint)}
				}
				bal, err := bch.GetBalance()
				if err != nil {
					return bchConnectedMsg{err: fmt.Errorf("balance error: %w", err)}
				}
				return bchConnectedMsg{bal: FormatBchBalance(bal)}
			}
		default:
			m.statusMsg = "usage: connect metamask | connect phantom | connect bch"
		}

	case "bch":
		// bch lock <amount_bch> <hashlock_hex> <timeout_blocks> <counterparty_bch_addr>
		// bch claim <htlc_txid> <htlc_vout> <preimage_hex>
		// bch refund <htlc_txid> <htlc_vout>
		if len(parts) < 2 {
			m.statusMsg = "usage: bch lock|claim|refund ..."
			return nil
		}
		if m.bch == nil {
			m.statusMsg = "BCH not configured (use --bch-rpc or connect bch)"
			return nil
		}
		switch parts[1] {
		case "lock":
			if len(parts) < 6 {
				m.statusMsg = "usage: bch lock <amount_bch> <hashlock_hex> <timeout_blocks> <counterparty_bch_addr>"
				return nil
			}
			_, _, _, _ = parts[2], parts[3], parts[4], parts[5]
			m.statusMsg = fmt.Errorf("bch lock: not yet implemented — SwapDaemon must expose an HTLC P2SH address endpoint first").Error()

		case "claim":
			if len(parts) < 5 {
				m.statusMsg = "usage: bch claim <htlc_txid> <htlc_vout> <preimage_hex>"
				return nil
			}
			txid, vout, pre := parts[2], parts[3], parts[4]
			client := m.client
			return func() tea.Msg {
				var processResp struct {
					Advanced bool   `json:"advanced"`
					NewState string `json:"new_state"`
					Status   string `json:"status"`
				}
				req := map[string]interface{}{"swap_id": txid + ":" + vout}
				if err := client.post("/processswap", req, &processResp); err != nil {
					return statusUpdateMsg{"bch claim (processswap) failed: " + err.Error()}
				}
				if processResp.Advanced {
					return statusUpdateMsg{fmt.Sprintf("bch claim: swap advanced → %s (preimage: %s...)", processResp.NewState, pre[:min(8, len(pre))])}
				}
				return statusUpdateMsg{"bch claim: swap not yet advanceable (check chain state)"}
			}

		case "refund":
			if len(parts) < 4 {
				m.statusMsg = "usage: bch refund <htlc_txid> <htlc_vout>"
				return nil
			}
			txid, vout := parts[2], parts[3]
			client := m.client
			return func() tea.Msg {
				var refundResp struct {
					Status string `json:"status"`
				}
				req := map[string]interface{}{"swap_id": txid + ":" + vout}
				if err := client.post("/refundswap", req, &refundResp); err != nil {
					return statusUpdateMsg{"bch refund failed: " + err.Error()}
				}
				return statusUpdateMsg{"bch refund: " + refundResp.Status}
			}

		default:
			m.statusMsg = "usage: bch lock|claim|refund ..."
		}

	case "eth":
		// eth lock <amount_eth> <htlc_contract> <hashlock> <timeout>
		if len(parts) < 5 || parts[1] != "lock" {
			m.statusMsg = "usage: eth lock <amount_wei> <htlc_contract> <hashlock> <timeout_hex>"
			return nil
		}
		if m.bridge == nil || !m.bridge.IsConnected() {
			m.statusMsg = "MetaMask not connected (try: connect metamask)"
			return nil
		}
		amtWei, htlcAddr, hashlock, timeout := parts[2], parts[3], parts[4], ""
		if len(parts) > 5 {
			timeout = parts[5]
		}
		if err := validateETHAddress(htlcAddr); err != nil {
			m.statusMsg = "invalid HTLC contract address: " + err.Error()
			return nil
		}
		if _, err := parseAmount(amtWei); err != nil {
			m.statusMsg = "invalid amount: " + err.Error()
			return nil
		}
		calldata, calldataErr := buildHTLCLockCalldata(hashlock, timeout)
		if calldataErr != nil {
			m.statusMsg = "eth calldata error: " + calldataErr.Error()
			return nil
		}
		bridge := m.bridge
		return func() tea.Msg {
			txHash, err := bridge.EthSendTransaction(htlcAddr, amtWei, calldata)
			if err != nil {
				return statusUpdateMsg{"eth lock failed: " + err.Error()}
			}
			return statusUpdateMsg{"eth lock tx: " + txHash[:min(20, len(txHash))] + "..."}
		}

	case "sell":
		// sell cd <key_image> <ask_price_xfg>
		if len(parts) < 4 || parts[1] != "cd" {
			m.statusMsg = "usage: sell cd <key_image> <ask_price_xfg>"
			return nil
		}
		if m.wallet == nil {
			m.statusMsg = "no wallet connected (use --wallet <endpoint>)"
			return nil
		}
		keyImage := parts[2]
		askAtomic, err := parseAmountAtomic(parts[3], 1e7)
		if err != nil {
			m.statusMsg = "invalid ask price: " + err.Error()
			return nil
		}
		return func() tea.Msg {
			// wallet must sign the offer — stub until wallet endpoint added
			return statusUpdateMsg{fmt.Sprintf("sell cd: key_image=%s ask=%.7f XFG (signing not yet wired)", keyImage, float64(askAtomic)/1e7)}
		}

	case "cancel":
		// cancel <offer_id>
		if len(parts) < 2 {
			m.statusMsg = "usage: cancel <offer_id>"
			return nil
		}
		if m.wallet == nil {
			m.statusMsg = "no wallet connected"
			return nil
		}
		offerID := parts[1]
		client := m.client
		return func() tea.Msg {
			if err := client.CancelCdOffer(offerID, "", ""); err != nil {
				return statusUpdateMsg{"cancel failed: " + err.Error()}
			}
			return statusUpdateMsg{"offer cancelled: " + offerID[:min(12, len(offerID))]}
		}

	case "accept_cd":
		// accept_cd [offer_id]  — uses selected row if no arg
		var offerID string
		if len(parts) >= 2 {
			offerID = parts[1]
		} else if o := m.cdMarket.selectedOffer(); o != nil {
			offerID = o.OfferID
		} else {
			m.statusMsg = "usage: accept_cd <offer_id> (or select a row in CD tab)"
			return nil
		}
		if offerID == "" {
			m.statusMsg = "No CD offer selected"
			return nil
		}
		if m.wallet == nil {
			m.statusMsg = "no wallet connected (use --wallet <endpoint>)"
			return nil
		}
		client := m.client
		return func() tea.Msg {
			resp, err := client.AcceptCdOffer(offerID, "")
			if err != nil {
				return statusUpdateMsg{"accept_cd failed: " + err.Error()}
			}
			return statusUpdateMsg{fmt.Sprintf("partial tx ready (expires blk %d): %s...", resp.ExpiresAt, resp.PartialTx[:min(20, len(resp.PartialTx))])}
		}

	case "help":
		m.statusMsg = "pair <name> | c: CD | connect metamask|phantom|bch | bch lock|claim|refund | eth lock | sell cd | accept [id] | offer <amt> <pair> | confirm-offer <amt> <pair> | q: quit"
	default:
		m.statusMsg = "unknown: " + cmd + " (type help)"
	}
	return nil
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
