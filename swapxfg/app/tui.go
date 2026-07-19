// swapxfg/app/tui.go
package app

import (
	"fmt"
	"os/exec"
	"strconv"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

const refreshInterval = 5 * time.Second

type tuiModel struct {
	cfg    Config
	client *FuegoClient
	wallet *WalletClient

	width, height int

	activeView int   // ViewMarkets, ViewCD, ViewStatus
	activePair uint8 // used in ViewMarkets

	data      *AllPairData
	connected bool

	cdMarket CdMarketModel

	// Browser bridge
	bridge  *BridgeServer
	ethAddr string
	ethBal  string
	solAddr string
	solBal  string

	bch    *BchClient
	bchBal string

	balance    *WalletBalance
	walletAddr string

	// Command input
	cmdBuf    string
	cmdFocus  bool
	cursorOn  bool
	blinkTick int

	lastErr   string
	statusMsg string

	// Daemon monitoring
	daemonStatus     *DaemonStatus
	daemonStatusAddr string
	daemonLastErr    string

	// Sovereign UX additions
	swapModal       swapModalModel
	bridgeAttempted map[uint8]bool

	// Swap draft (before modal opens)
	draftAmount string
	draftPair   uint8
	draftRate   string

	// P2P order entry
	orderEntry orderEntryModel

	// Chart mode: candlestick or line
	chartMode ChartMode
}

// ── Messages ──

type refreshMsg struct {
	data    *AllPairData
	err     error
	balance *WalletBalance
	balErr  error
	bchBal  string
}

type refreshTickMsg time.Time
type cursorBlinkMsg time.Time
type statusUpdateMsg struct{ text string }

type ethConnectedMsg struct {
	addr string
	bal  string
	err  error
}

type bchConnectedMsg struct {
	bal string
	err error
}

// bridgeAttemptMsg triggers an auto-bridge connection attempt for a pair.
type bridgeAttemptMsg struct {
	pair uint8
}

// P2P order messages
type placeOrderResultMsg struct {
	result *PlaceOrderResult
	err    error
}

type cancelOrderResultMsg struct {
	err error
}

// ── Ticks ──

func refreshTick() tea.Cmd {
	return tea.Tick(refreshInterval, func(t time.Time) tea.Msg {
		return refreshTickMsg(t)
	})
}

func cursorBlink() tea.Cmd {
	return tea.Tick(530*time.Millisecond, func(t time.Time) tea.Msg {
		return cursorBlinkMsg(t)
	})
}

// ── Init ──

func newTuiModel(cfg Config) tuiModel {
	daemonAddr := fmt.Sprintf("127.0.0.1:%d", cfg.StatusPort)
	m := tuiModel{
		cfg:              cfg,
		client:           NewFuegoClient(cfg.DaemonRPC),
		activeView:       ViewMarkets,
		activePair:       cfg.StartPair,
		daemonStatusAddr: daemonAddr,
		bridgeAttempted:  make(map[uint8]bool),
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
		m.autoBridgeCmd(m.activePair),
	)
}

// ── auto-bridge ──

func (m *tuiModel) autoBridgeCmd(pair uint8) tea.Cmd {
	if m.bridge == nil {
		return nil
	}
	if m.bridgeAttempted[pair] {
		return nil
	}
	m.bridgeAttempted[pair] = true

	switch pair {
	case PairETH:
		if m.ethAddr != "" {
			return nil // already connected
		}
		return func() tea.Msg {
			if err := m.bridge.OpenEthBridge(); err != nil {
				return statusUpdateMsg{text: "auto-connect ETH: " + err.Error()}
			}
			addr, err := m.bridge.EthGetAddress()
			if err != nil {
				return statusUpdateMsg{text: fmt.Sprintf("ETH bridge open at %s (open in browser)", m.bridge.EthURL())}
			}
			bal, _ := m.bridge.EthGetBalance(addr)
			return ethConnectedMsg{addr: addr, bal: bal}
		}
	case PairSOL:
		if m.solAddr != "" {
			return nil
		}
		return func() tea.Msg {
			if err := m.bridge.OpenSolBridge(); err != nil {
				return statusUpdateMsg{text: "auto-connect SOL: " + err.Error()}
			}
			return statusUpdateMsg{text: fmt.Sprintf("Phantom bridge at %s (open in browser)", m.bridge.SolURL())}
		}
	}
	return nil
}

// ── fetchData ──

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

// ── Update ──

func (m tuiModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	// Delegate to swap modal when active
	if m.swapModal.active {
		modal, cmd := m.swapModal.Update(msg)
		m.swapModal = modal
		if m.swapModal.done {
			m.swapModal.active = false
			if m.swapModal.confirm {
				return m, m.executeSwap()
			}
			m.swapModal = swapModalModel{}
		}
		return m, cmd
	}

	// Delegate to order entry form when active
	if m.orderEntry.active {
		var cmd tea.Cmd
		m.orderEntry, cmd = m.orderEntry.Update(msg)
		if !m.orderEntry.active {
			// Form was closed (esc), just return
			return m, cmd
		}
		if cmd == nil {
			// Check for Enter on submit field
			if kmsg, ok := msg.(tea.KeyMsg); ok && kmsg.Type == tea.KeyEnter {
				return m, m.executePlaceOrder()
			}
		}
		return m, cmd
	}

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
		return m, tea.Batch(m.fetchData(), refreshTick(), func() tea.Msg { return m.fetchDaemonStatus() })

	case cursorBlinkMsg:
		m.blinkTick++
		m.cursorOn = m.blinkTick%2 == 0
		return m, cursorBlink()

	case statusUpdateMsg:
		m.statusMsg = msg.text

	case daemonStatusMsg:
		if msg.err != nil {
			m.daemonLastErr = msg.err.Error()
		} else {
			m.daemonStatus = msg.status
			m.daemonLastErr = ""
		}

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

	case placeOrderResultMsg:
		if msg.err != nil {
			m.lastErr = "order failed: " + msg.err.Error()
		} else if msg.result != nil {
			if msg.result.Filled > 0 {
				m.statusMsg = fmt.Sprintf("order %s placed, filled %d", msg.result.OrderId[:8], msg.result.Filled)
			} else {
				m.statusMsg = "order placed: " + msg.result.OrderId[:8]
			}
		}

	case cancelOrderResultMsg:
		if msg.err != nil {
			m.lastErr = "cancel failed: " + msg.err.Error()
		} else {
			m.statusMsg = "order cancelled"
		}
	}

	return m, nil
}

// ── handleKey ──

func (m tuiModel) handleKey(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	k := msg.String()

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

	// Global keys
	switch k {
	case "q":
		return m, tea.Quit
	case "esc":
		return m, tea.Quit
	case "ctrl+c":
		return m, tea.Quit
	case "/":
		m.cmdFocus = true
		return m, nil
	case "r":
		return m, m.fetchData()
	case "?":
		m.statusMsg = "m:markets  c:CD  s:status  ↑↓:pair  t:TradingView  /:cmd  r:refresh  q:quit"
	}

	// Sidebar view navigation
	switch k {
	case "m":
		m.activeView = ViewMarkets
		return m, m.autoBridgeCmd(m.activePair)
	case "c":
		m.activeView = ViewCD
	case "s":
		m.activeView = ViewStatus
	}

	// View-specific keys
	switch m.activeView {
	case ViewMarkets:
		return m.handleMarketsKey(k)
	case ViewCD:
		return m.handleCDKey(k)
	case ViewStatus:
		// no special keys
	}

	return m, nil
}

func (m tuiModel) handleMarketsKey(k string) (tea.Model, tea.Cmd) {
	switch k {
	case "up":
		m.activePair = prevPair(m.activePair)
		return m, m.autoBridgeCmd(m.activePair)
	case "down":
		m.activePair = nextPair(m.activePair)
		return m, m.autoBridgeCmd(m.activePair)
	case "t":
		return m, m.openTradingView()
	case "l":
		if m.chartMode == ChartCandles {
			m.chartMode = ChartLine
		} else {
			m.chartMode = ChartCandles
		}
		return m, nil
	default:
		if len(k) == 1 {
			r := rune(k[0])
			if p := HotkeyPair(r); p != 255 && p < 100 {
				m.activePair = p
				if m.activePair != p {
					m.activePair = p
					return m, m.autoBridgeCmd(m.activePair)
				}
			}
		}
	}
	return m, nil
}

func (m tuiModel) handleCDKey(k string) (tea.Model, tea.Cmd) {
	switch k {
	case "up":
		m.cdMarket.moveUp()
	case "down":
		m.cdMarket.moveDown()
	case "enter":
		if o := m.cdMarket.selectedOffer(); o != nil {
			m.statusMsg = fmt.Sprintf("accept offer %s (enter: confirm <addr> <amt> <pair>)", o.OfferID[:min(12, len(o.OfferID))])
		}
	}
	return m, nil
}

// ── prevPair / nextPair (markets-only) ──

func prevPair(cur uint8) uint8 {
	for i := len(ActivePairs) - 1; i >= 0; i-- {
		if ActivePairs[i] == cur {
			if i == 0 {
				return ActivePairs[len(ActivePairs)-1]
			}
			return ActivePairs[i-1]
		}
	}
	return ActivePairs[0]
}

func nextPair(cur uint8) uint8 {
	for i, p := range ActivePairs {
		if p == cur {
			return ActivePairs[(i+1)%len(ActivePairs)]
		}
	}
	return ActivePairs[0]
}

// ── handleCommand ──

func (m *tuiModel) handleCommand(cmd string) tea.Cmd {
	cmd = strings.TrimSpace(cmd)
	if cmd == "" {
		return nil
	}
	parts := strings.Fields(cmd)
	switch parts[0] {

	// ── Sovereign UX: single-command swap ──
	case "swap":
		return m.handleSwapCmd(parts)

	case "pair":
		if len(parts) > 1 {
			p := PairFromString(parts[1])
			if p != 255 {
				m.activeView = ViewMarkets
				m.activePair = p
				m.bridgeAttempted[p] = false
				m.statusMsg = fmt.Sprintf("switched to %s", PairName(p))
				return m.autoBridgeCmd(p)
			}
			m.statusMsg = "unknown pair: " + parts[1]
			return nil
		}
		m.statusMsg = "usage: pair <sol|eth|xmr|bch|arb|base>"

	// ── Legacy: direct pair switch ──
	case "markets":
		m.activeView = ViewMarkets
		m.statusMsg = "Markets"
	case "cd":
		m.activeView = ViewCD
		m.statusMsg = "CD Market"
	case "status":
		m.activeView = ViewStatus
		m.statusMsg = "Status"

	// ── Bridge ──
	case "connect":
		return m.handleBridgeCmd(parts)

	// ── Sell XFG or CD ──
	case "sell":
		if len(parts) >= 2 && parts[1] == "cd" {
			return m.handleSellCDCmd(parts)
		}
		return m.handleSwapCmd(parts)

	// ── P2P order entry form ──
	case "order", "placeorder":
		m.orderEntry.open(m.activePair)
		m.cmdBuf = ""
		m.cmdFocus = false
		return nil

	// ── P2P cancel order ──
	case "cancelorder":
		return m.handleCancelOrderCmd(parts)

	// ── P2P my open orders ──
	case "myorders":
		return m.handleMyOrdersCmd(parts)

	// ── TradingView ──
	case "tv", "chartview":
		return m.openTradingView()

	// ── Accept ──
	case "accept":
		return m.handleAcceptCmd(parts)

	// ── BCH ──
	case "bch":
		return m.handleBchCmd(parts)

	// ── ETH ──
	case "eth":
		return m.handleEthCmd(parts)

	case "accept_cd":
		return m.handleAcceptCDCmd(parts)

	// ── Cancel ──
	case "cancel":
		return m.handleCancelCmd(parts)

	// ── Help ──
	case "help":
		m.statusMsg = "swap <amt> | order | cancelorder <id> | myorders | tv | pair <name> | m:c:s | legend | connect | q:quit"
	case "legend":
		m.statusMsg = "Ticker: ETH 214,000 ($2140) → rate=on-chain XFG/ETH (TWAP), ($)=DeFiLlama ref USD"

	default:
		m.statusMsg = "unknown: " + cmd + " (type help)"
	}
	return nil
}

// ── Sub-command handlers ──

func (m *tuiModel) handleSwapCmd(parts []string) tea.Cmd {
	if len(parts) < 2 {
		m.statusMsg = "usage: swap <amount> [pair]"
		return nil
	}

	amount := parts[1]

	// Determine pair: from argument or active pair
	pair := m.activePair
	if len(parts) > 2 {
		p := PairFromString(parts[2])
		if p != 255 && p < 100 {
			pair = p
		}
	}

	// Get current rate
	var rate string
	if pr := m.data.Prices[pair]; pr != nil && pr.CompositeRate != "" {
		rate = pr.CompositeRate
	} else {
		rate = "—"
	}

	// Store draft and open modal
	m.draftAmount = amount
	m.draftPair = pair
	m.draftRate = rate
	m.swapModal = newSwapModal(pair, amount, rate)

	return nil
}

func (m *tuiModel) executeSwap() tea.Cmd {
	if m.wallet == nil {
		m.statusMsg = "no wallet connected"
		return nil
	}

	pair := m.draftPair
	xfgAtomic := m.swapModal.xfgAtomic
	rate := m.swapModal.rate
	pairName := m.swapModal.pairName
	amount := m.draftAmount

	m.statusMsg = fmt.Sprintf("executing swap: %s XFG → %s @ %s", amount, pairName, rate)

	wallet := m.wallet
	client := m.client
	return func() tea.Msg {
		rateNum := uint64(0)
		if r, err := strconv.ParseFloat(rate, 64); err == nil && r > 0 {
			rateNum = uint64(r * 1e7)
		} else {
			return statusUpdateMsg{text: "invalid rate: " + rate}
		}

		// Sign soft order
		res, err := wallet.SignOffer(xfgAtomic, rateNum, pair, 8640, true)
		if err != nil {
			return statusUpdateMsg{text: "sign_offer failed: " + err.Error()}
		}

		// Submit to daemon
		offerReq := map[string]interface{}{
			"offerId":     res.OfferID,
			"xfgAmount":   xfgAtomic,
			"rateNum":     rateNum,
			"pair":        pair,
			"makerPubKey": res.MakerPubKey,
			"signature":   res.Signature,
			"ttlBlocks":   uint32(8640),
			"isSoftOrder": true,
		}
		var submitResp struct {
			Status string `json:"status"`
		}
		if err := client.post("/submitswap", offerReq, &submitResp); err != nil {
			return statusUpdateMsg{text: "submit failed: " + err.Error()}
		}
		return statusUpdateMsg{text: fmt.Sprintf("Swap posted: %s XFG for %s @ %s (%s...)", amount, PairShort(pair), rate, res.OfferID[:12])}
	}
}

func (m *tuiModel) handleBridgeCmd(parts []string) tea.Cmd {
	if len(parts) < 2 {
		m.statusMsg = "usage: connect metamask | connect phantom | connect bch"
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
		m.statusMsg = fmt.Sprintf("MetaMask bridge at %s", m.bridge.EthURL())
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
		m.statusMsg = fmt.Sprintf("Phantom bridge at %s", m.bridge.SolURL())
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
	}
	m.statusMsg = "usage: connect metamask | connect phantom | connect bch"
	return nil
}

func (m *tuiModel) handleAcceptCmd(parts []string) tea.Cmd {
	if len(parts) < 2 {
		m.statusMsg = "usage: accept <offer_id>"
		return nil
	}
	offerID := parts[1]
	client := m.client
	wallet := m.wallet
	return func() tea.Msg {
		offers, err := client.GetOffers(m.activePair)
		if err != nil {
			return statusUpdateMsg{text: "fetch offers failed: " + err.Error()}
		}
		var isSoft bool
		for _, o := range offers {
			if o.OfferID == offerID {
				isSoft = o.IsSoftOrder
				break
			}
		}
		if isSoft && wallet != nil {
			addr, _ := wallet.GetAddress()
			if err := client.RequestSwap(offerID, 0, addr, ""); err != nil {
				return statusUpdateMsg{text: "request swap failed: " + err.Error()}
			}
			return statusUpdateMsg{text: "Swap request sent. Waiting for maker auto-lock..."}
		}
		var resp struct {
			Status string `json:"status"`
		}
		if err := client.post("/accept", map[string]interface{}{"swap_id": offerID}, &resp); err != nil {
			return statusUpdateMsg{text: "accept failed: " + err.Error()}
		}
		return statusUpdateMsg{text: "Offer accepted. Lock funds to proceed."}
	}
}

func (m *tuiModel) handleBchCmd(parts []string) tea.Cmd {
	if len(parts) < 2 {
		m.statusMsg = "usage: bch lock|claim|refund ..."
		return nil
	}
	if m.bch == nil {
		m.statusMsg = "BCH not configured"
		return nil
	}
	switch parts[1] {
	case "lock":
		if len(parts) < 6 {
			m.statusMsg = "usage: bch lock <amount_bch> <hashlock_hex> <timeout_blocks> <counterparty_bch_addr>"
			return nil
		}
		m.statusMsg = fmt.Errorf("bch lock: not yet implemented").Error()
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
			if err := client.post("/processswap", map[string]interface{}{"swap_id": txid + ":" + vout}, &processResp); err != nil {
				return statusUpdateMsg{text: "bch claim failed: " + err.Error()}
			}
			if processResp.Advanced {
				return statusUpdateMsg{text: fmt.Sprintf("bch claim: advanced → %s (preimage: %s...)", processResp.NewState, pre[:min(8, len(pre))])}
			}
			return statusUpdateMsg{text: "bch claim: not yet advanceable"}
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
			if err := client.post("/refundswap", map[string]interface{}{"swap_id": txid + ":" + vout}, &refundResp); err != nil {
				return statusUpdateMsg{text: "bch refund failed: " + err.Error()}
			}
			return statusUpdateMsg{text: "bch refund: " + refundResp.Status}
		}
	}
	m.statusMsg = "usage: bch lock|claim|refund ..."
	return nil
}

func (m *tuiModel) handleEthCmd(parts []string) tea.Cmd {
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
		m.statusMsg = "invalid HTLC contract: " + err.Error()
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
			return statusUpdateMsg{text: "eth lock failed: " + err.Error()}
		}
		return statusUpdateMsg{text: "eth lock tx: " + txHash[:min(20, len(txHash))] + "..."}
	}
}

func (m *tuiModel) handleSellCDCmd(parts []string) tea.Cmd {
	if len(parts) < 4 || parts[1] != "cd" {
		m.statusMsg = "usage: sell cd <key_image> <ask_price_xfg>"
		return nil
	}
	if m.wallet == nil {
		m.statusMsg = "no wallet connected"
		return nil
	}
	keyImage := parts[2]
	askAtomic, err := parseAmountAtomic(parts[3], 1e7)
	if err != nil {
		m.statusMsg = "invalid ask price: " + err.Error()
		return nil
	}
	return func() tea.Msg {
		return statusUpdateMsg{text: fmt.Sprintf("sell cd: key_image=%s ask=%.7f XFG (signing not yet wired)", keyImage, float64(askAtomic)/1e7)}
	}
}

func (m *tuiModel) handleAcceptCDCmd(parts []string) tea.Cmd {
	var offerID string
	if len(parts) >= 2 {
		offerID = parts[1]
	} else if o := m.cdMarket.selectedOffer(); o != nil {
		offerID = o.OfferID
	} else {
		m.statusMsg = "usage: accept_cd <offer_id> (or select in CD tab)"
		return nil
	}
	if m.wallet == nil {
		m.statusMsg = "no wallet connected"
		return nil
	}
	client := m.client
	return func() tea.Msg {
		resp, err := client.AcceptCdOffer(offerID, "")
		if err != nil {
			return statusUpdateMsg{text: "accept_cd failed: " + err.Error()}
		}
		return statusUpdateMsg{text: fmt.Sprintf("partial tx ready (expires blk %d): %s...", resp.ExpiresAt, resp.PartialTx[:min(20, len(resp.PartialTx))])}
	}
}

func (m *tuiModel) handleCancelCmd(parts []string) tea.Cmd {
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
	wallet := m.wallet
	return func() tea.Msg {
		signed, err := wallet.SignCancel(offerID)
		if err != nil {
			return statusUpdateMsg{text: "sign cancel failed: " + err.Error()}
		}
		if err := client.CancelSwapOffer(signed.OfferID, signed.MakerPubKey, signed.Signature); err != nil {
			return statusUpdateMsg{text: "cancel failed: " + err.Error()}
		}
		return statusUpdateMsg{text: "offer cancelled: " + offerID[:min(12, len(offerID))]}
	}
}

// ── View ──

func (m tuiModel) View() string {
	if m.width == 0 {
		return ""
	}

	w := m.width
	h := m.height

	sidebarW := 20
	if w < 80 {
		sidebarW = 14
	}
	mainW := w - sidebarW

	// Sidebar
	sidebar := RenderSidebar(m.activeView, m.activePair, m.bridge,
		m.ethAddr, m.ethBal, m.solAddr, m.solBal, m.bchBal,
		m.data.Height, m.connected, sidebarW)

	// Main area
	inputH := 1
	statusH := 1
	mainH := h - inputH - statusH - 1
	if mainH < 5 {
		mainH = 5
	}

	var mainArea string
	if m.swapModal.active {
		mainArea = m.renderMarketsContent(mainW, mainH)
	} else {
		switch m.activeView {
		case ViewMarkets:
			mainArea = m.renderMarketsContent(mainW, mainH)
		case ViewCD:
			mainArea = RenderCdMarket(&m.cdMarket, mainW, mainH)
		case ViewStatus:
			mainArea = RenderDaemonStatus(m.daemonStatus, m.daemonLastErr, mainW, mainH)
		}
	}

	content := lipgloss.JoinHorizontal(lipgloss.Top, sidebar, mainArea)

	// Input bar
	xfgBal := ""
	if m.wallet != nil && m.balance != nil {
		xfgBal = FormatBalance(m.balance.Available)
	} else if m.wallet != nil {
		xfgBal = "syncing..."
	}
	ethBalStr := ""
	if m.ethBal != "" {
		var weiF float64
		fmt.Sscanf(m.ethBal, "%f", &weiF)
		ethBalStr = fmt.Sprintf("%.4f ETH", weiF/1e18)
	}
	if ethBalStr != "" {
		if xfgBal != "" {
			xfgBal += "  " + ethBalStr
		} else {
			xfgBal = ethBalStr
		}
	}
	inputBar := RenderInputBar(m.cmdBuf, m.cursorOn && m.cmdFocus, xfgBal, m.bchBal, m.cfg.DaemonRPC, m.connected, mainW)

	// Status
	status := ""
	if m.lastErr != "" {
		status = StyleStatus.Render(m.lastErr)
	} else if m.statusMsg != "" {
		status = StyleMuted.Render(m.statusMsg)
	}

	// Swap modal overlay
	result := lipgloss.JoinVertical(lipgloss.Left, content, inputBar, status)

	if m.swapModal.active {
		modal := m.swapModal.View()
		_, modalH := lipgloss.Size(modal)
		placeY := (h - modalH) / 2
		if placeY < 1 {
			placeY = 1
		}
		result = overlayAt(result, modal, sidebarW+4, placeY)
	}

	// Order entry form overlay
	if m.orderEntry.active {
		formW := 38
		if mainW-4 < formW {
			formW = mainW - 4
		}
		form := m.orderEntry.Render(formW)
		_, formH := lipgloss.Size(form)
		placeY := (h - formH) / 2
		if placeY < 1 {
			placeY = 1
		}
		placeX := sidebarW + (mainW-formW)/2
		if placeX < sidebarW+1 {
			placeX = sidebarW + 1
		}
		result = overlayAt(result, form, placeX, placeY)
	}

	return result
}

func (m tuiModel) renderMarketsContent(w, h int) string {
	// Ticker row
	ticker := RenderTicker(m.activePair, m.data.Prices, m.data.ExtPrices, m.data.Height, w, m.connected)

	chartH := h - 4
	if chartH < 3 {
		chartH = 3
	}

	// Right panel: orderbook + tape
	rightW := w * 38 / 100
	if rightW < 30 {
		rightW = 30
	}
	leftW := w - rightW - 3
	if leftW < 20 {
		leftW = 20
	}

	// Chart (left)
	trades := m.data.Trades[m.activePair]
	var chart string
	if m.chartMode == ChartLine {
		chart = RenderChartLine(trades, leftW, chartH)
	} else {
		chart = RenderChart(trades, leftW, chartH)
	}
	priceLine := RenderPriceLine(m.activePair, m.data.Prices, m.data.ExtPrices)
	leftPanel := lipgloss.JoinVertical(lipgloss.Left, chart, priceLine)

	// Orderbook + tape (right)
	obH := h * 55 / 100
	if obH < 5 {
		obH = 5
	}
	tapeH := h - obH
	if tapeH < 3 {
		tapeH = 3
	}

	book := m.data.Books[m.activePair]
	ob := RenderOrderbook(book, rightW, obH)
	tape := RenderTape(m.data.Trades[m.activePair], rightW, tapeH)
	rightPanel := lipgloss.JoinVertical(lipgloss.Left, ob, tape)

	sep := lipgloss.NewStyle().Foreground(ColorMuted).Render(
		strings.Repeat("│\n", h-1))
	mainArea := lipgloss.JoinHorizontal(lipgloss.Top, leftPanel, sep, rightPanel)

	return lipgloss.JoinVertical(lipgloss.Left, ticker, mainArea)
}

func (m tuiModel) fetchDaemonStatus() tea.Msg {
	if m.daemonStatusAddr == "" {
		return nil
	}
	s, err := FetchDaemonStatus(m.daemonStatusAddr)
	return daemonStatusMsg{status: s, err: err}
}

type daemonStatusMsg struct {
	status *DaemonStatus
	err    error
}

// ── Daemon status view ──

func RenderDaemonStatus(status *DaemonStatus, lastErr string, w, h int) string {
	if lastErr != "" {
		return "Daemon status unavailable: " + lastErr
	}
	if status == nil {
		return "Connecting to daemon status endpoint..."
	}

	lines := []string{
		fmt.Sprintf("xfg-swapd  Height: %d  |  Active offers: %d  |  In-flight swaps: %d",
			status.Height, len(status.Offers), len(status.Swaps)),
		"",
	}

	for _, o := range status.Offers {
		avail := o.XfgAmount - o.FilledAmount
		pairName := PairShort(uint8(o.Pair))
		lines = append(lines,
			fmt.Sprintf("  %s  %-8s %12d / %-12d XFG  rate=%d  height=%d",
				o.OfferId[:8], pairName, avail, o.XfgAmount, o.RateNum, o.PostedHeight))
	}

	lines = append(lines, "")
	for _, s := range status.Swaps {
		pairName := PairShort(uint8(s.Pair))
		lines = append(lines,
			fmt.Sprintf("  %s  %-8s %-24s  timeout=%d",
				s.SwapId[:8], pairName, s.State, s.TimeoutHeight))
	}

	return strings.Join(lines, "\n")
}

// ── P2P Order Commands ──

func (m *tuiModel) executePlaceOrder() tea.Cmd {
	side := m.orderEntry.side
	pair := m.orderEntry.pair
	price, ok := m.orderEntry.ParsePrice()
	if !ok {
		m.lastErr = "invalid price"
		return nil
	}
	amount, ok := m.orderEntry.ParseAmount()
	if !ok {
		m.lastErr = "invalid amount"
		return nil
	}
	ttl := m.orderEntry.ParseTTL()

	client := m.client
	wallet := m.wallet
	return func() tea.Msg {
		// Sign the order through the local wallet (proves ownership of spend key)
		if wallet != nil {
			sig, err := wallet.SignOrder(side, pair, price, amount, ttl)
			if err != nil {
				return placeOrderResultMsg{err: fmt.Errorf("sign_order: %w", err)}
			}
			_ = sig // signature is embedded in the order by the daemon
		}
		result, err := client.PlaceOrder(side, pair, price, amount, ttl)
		return placeOrderResultMsg{result: result, err: err}
	}
}

func (m *tuiModel) handleCancelOrderCmd(parts []string) tea.Cmd {
	if len(parts) < 2 {
		m.statusMsg = "usage: cancelorder <orderId>"
		return nil
	}
	orderId := parts[1]
	client := m.client
	return func() tea.Msg {
		err := client.CancelOrder(orderId)
		return cancelOrderResultMsg{err: err}
	}
}

func (m *tuiModel) handleMyOrdersCmd(parts []string) tea.Cmd {
	client := m.client
	return func() tea.Msg {
		orders, err := client.GetOpenOrders("")
		if err != nil {
			return placeOrderResultMsg{err: err}
		}
		if len(orders) == 0 {
			return statusUpdateMsg{text: "no open orders"}
		}
		var lines []string
		for _, o := range orders {
			lines = append(lines, fmt.Sprintf("  %s  %s  %s  price=%d  amt=%d  filled=%d",
				o.OrderId[:8], o.Side, PairShort(o.Pair), o.Price, o.Amount, o.Filled))
		}
		return statusUpdateMsg{text: strings.Join(lines, "\n")}
	}
}

// overlayAt places an overlay string at position (x, y) over the base string.
func overlayAt(base, overlay string, x, y int) string {
	baseLines := strings.Split(base, "\n")
	overlayLines := strings.Split(overlay, "\n")

	for oy, ol := range overlayLines {
		ty := y + oy
		if ty >= len(baseLines) {
			break
		}
		bl := baseLines[ty]
		olW := lipgloss.Width(ol)

		if x+olW >= len(bl) {
			// Pad base line to accommodate overlay
			bl = lipgloss.NewStyle().Width(x + olW).Render(bl)
		}

		// Build new line: base[left of x] + overlay + base[right of x+olW]
		prefix := lipgloss.NewStyle().Inline(true).Width(x).Render("")
		_ = prefix
		baseLines[ty] = bl[:x] + ol + bl[x+olW:]
	}

	return strings.Join(baseLines, "\n")
}

// ── TradingView ──

func (m tuiModel) openTradingView() tea.Cmd {
	symbol := TradingViewSymbol(m.activePair)
	if symbol == "" {
		m.statusMsg = "no TradingView symbol for this pair"
		return nil
	}
	url := fmt.Sprintf("https://www.tradingview.com/chart/?symbol=%s", symbol)
	pairName := PairName(m.activePair)
	return func() tea.Msg {
		_ = exec.Command("open", url).Start() // macOS
		return statusUpdateMsg{text: fmt.Sprintf("opened TradingView %s in browser", pairName)}
	}
}
