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

	// Terminal dimensions
	width, height int

	// Market state
	activePair uint8
	data       *AllPairData
	connected  bool

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
	data *AllPairData
	err  error
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
	return tuiModel{
		cfg:        cfg,
		client:     NewFuegoClient(cfg.DaemonRPC),
		activePair: cfg.StartPair,
		data: &AllPairData{
			Offers: make(map[uint8][]SwapOffer),
			Prices: make(map[uint8]*SwapPriceResponse),
			Trades: make(map[uint8][]SwapTrade),
		},
		cursorOn: true,
	}
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
	return func() tea.Msg {
		data, err := client.FetchAll(ActivePairs)
		return refreshMsg{data: data, err: err}
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
		m.statusMsg = "0-3: pairs  /: cmd  r: refresh  q: quit"
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
	case "help":
		m.statusMsg = "pair <name> | initiate <alias/@alias/addr> <amount> <pair> | r: refresh | q: quit"
	default:
		m.statusMsg = "unknown: " + cmd + " (type help)"
	}
}

func nextPair(cur uint8) uint8 {
	for i, p := range ActivePairs {
		if p == cur {
			return ActivePairs[(i+1)%len(ActivePairs)]
		}
	}
	return ActivePairs[0]
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
	ticker := RenderTicker(m.activePair, m.data.Prices, m.data.Height, w, m.connected)

	// ── Main area: chart (left 60%) | orderbook+tape (right 40%) ──
	rightW := w * 38 / 100
	if rightW < 30 {
		rightW = 30
	}
	leftW := w - rightW - 3 // 3 for border
	if leftW < 20 {
		leftW = 20
	}

	// Chart
	chartH := mainH - 2 // leave room for price line
	if chartH < 3 {
		chartH = 3
	}
	trades := m.data.Trades[m.activePair]
	chart := RenderChart(trades, leftW, chartH)
	priceLine := RenderPriceLine(m.activePair, m.data.Prices)
	leftPanel := lipgloss.JoinVertical(lipgloss.Left, chart, priceLine)

	// Orderbook + tape
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
	tape := RenderTape(trades, rightW, tapeH)
	rightPanel := lipgloss.JoinVertical(lipgloss.Left, ob, tape)

	// Join left | right with separator
	sep := lipgloss.NewStyle().Foreground(ColorMuted).Render(
		strings.Repeat("│\n", mainH))
	mainArea := lipgloss.JoinHorizontal(lipgloss.Top, leftPanel, sep, rightPanel)

	// ── Input bar ──
	inputBar := RenderInputBar(m.cmdBuf, m.cursorOn && m.cmdFocus, "", m.cfg.DaemonRPC, m.connected, w)

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
