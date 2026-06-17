package app

import (
	"fmt"
	"strconv"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

// wizardStep is the current step of the interactive config wizard.
type wizardStep int

const (
	wizDaemonRPC wizardStep = iota
	wizWalletRPC
	wizTestnet
	wizPair
	wizBridgePort
	wizNoBridge
	wizBchRPC
	wizNoBch
	wizHeadless
	wizConfirm
)

var wizStepNames = []string{
	"Daemon RPC",
	"Wallet RPC",
	"Testnet",
	"Starting pair",
	"Bridge port",
	"Bridge server",
	"BCH RPC",
	"BCH connection",
	"Headless mode",
	"Confirm",
}

type wizardModel struct {
	cfg      Config
	step     wizardStep
	input    string
	width    int
	cursorOn bool
	err      string
	done     bool
}

// RunInteractive starts the interactive config wizard, then launches the TUI.
func RunInteractive(forceFlags Config) error {
	m := wizardModel{
		cfg:   forceFlags,
		step:  wizDaemonRPC,
		input: forceFlags.DaemonRPC,
	}
	p := tea.NewProgram(m, tea.WithAltScreen())
	result, err := p.Run()
	if err != nil {
		return fmt.Errorf("interactive wizard: %w", err)
	}
	final := result.(wizardModel)
	if !final.done {
		return nil
	}
	if final.cfg.Headless {
		return RunHeadless(final.cfg)
	}
	return Run(final.cfg)
}

func (m wizardModel) Init() tea.Cmd {
	return nil
}

func (m wizardModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		return m, nil
	case tea.KeyMsg:
		return m.handleKey(msg)
	}
	return m, nil
}

func (m wizardModel) handleKey(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	k := msg.String()

	switch k {
	case "ctrl+c":
		return m, tea.Quit
	case "esc":
		if m.step > wizDaemonRPC {
			m.step--
			m.loadCurrentInput()
			m.err = ""
		}
		return m, nil
	}

	if k == "enter" {
		return m.advance()
	}

	// Text input
	if k == "backspace" {
		if len(m.input) > 0 {
			m.input = m.input[:len(m.input)-1]
		}
		return m, nil
	}
	if len(k) == 1 {
		m.input += k
	}
	return m, nil
}

func (m wizardModel) advance() (tea.Model, tea.Cmd) {
	m.err = ""

	switch m.step {
	case wizDaemonRPC:
		v := strings.TrimSpace(m.input)
		if v == "" {
			v = "http://127.0.0.1:18180"
		}
		m.cfg.DaemonRPC = v
		m.step = wizWalletRPC
		m.input = m.cfg.WalletRPC

	case wizWalletRPC:
		v := strings.TrimSpace(m.input)
		m.cfg.WalletRPC = v
		m.step = wizTestnet
		if m.cfg.Testnet {
			m.input = "y"
		} else {
			m.input = "n"
		}

	case wizTestnet:
		v := strings.ToLower(strings.TrimSpace(m.input))
		if v == "y" || v == "yes" {
			m.cfg.Testnet = true
			if m.cfg.DaemonRPC == "http://127.0.0.1:18180" {
				m.cfg.DaemonRPC = "http://127.0.0.1:28280"
			}
			if m.cfg.WalletRPC == "http://127.0.0.1:18282" || m.cfg.WalletRPC == "" {
				m.cfg.WalletRPC = "http://127.0.0.1:28282"
			}
		} else {
			m.cfg.Testnet = false
		}
		m.step = wizPair
		m.input = PairShort(m.cfg.StartPair)

	case wizPair:
		v := strings.ToLower(strings.TrimSpace(m.input))
		if v != "" {
			p := PairFromString(v)
			if p == 255 {
				m.err = "unknown pair: " + v + " (use: sol, eth, xmr, bch, arb, base)"
				return m, nil
			}
			m.cfg.StartPair = p
		}
		m.step = wizBridgePort
		m.input = ""

	case wizBridgePort:
		v := strings.TrimSpace(m.input)
		if v != "" {
			p, err := strconv.Atoi(v)
			if err != nil || p < 0 || p > 65535 {
				m.err = "invalid port number"
				return m, nil
			}
			m.cfg.BridgePort = p
		}
		m.step = wizNoBridge
		if m.cfg.NoBridge {
			m.input = "y"
		} else {
			m.input = "n"
		}

	case wizNoBridge:
		v := strings.ToLower(strings.TrimSpace(m.input))
		m.cfg.NoBridge = v == "y" || v == "yes"
		m.step = wizBchRPC
		m.input = m.cfg.BchRPC

	case wizBchRPC:
		v := strings.TrimSpace(m.input)
		if v != "" {
			m.cfg.BchRPC = v
		}
		m.step = wizNoBch
		if m.cfg.NoBch {
			m.input = "y"
		} else {
			m.input = "n"
		}

	case wizNoBch:
		v := strings.ToLower(strings.TrimSpace(m.input))
		m.cfg.NoBch = v == "y" || v == "yes"
		m.step = wizHeadless
		if m.cfg.Headless {
			m.input = "y"
		} else {
			m.input = "n"
		}

	case wizHeadless:
		v := strings.ToLower(strings.TrimSpace(m.input))
		m.cfg.Headless = v == "y" || v == "yes"
		m.step = wizConfirm
		m.input = ""

	case wizConfirm:
		v := strings.ToLower(strings.TrimSpace(m.input))
		if v == "y" || v == "yes" || v == "" {
			m.done = true
			return m, tea.Quit
		}
		// Restart wizard
		m.step = wizDaemonRPC
		m.input = m.cfg.DaemonRPC
	}

	return m, nil
}

func (m *wizardModel) loadCurrentInput() {
	switch m.step {
	case wizDaemonRPC:
		m.input = m.cfg.DaemonRPC
	case wizWalletRPC:
		m.input = m.cfg.WalletRPC
	case wizTestnet:
		if m.cfg.Testnet {
			m.input = "y"
		} else {
			m.input = "n"
		}
	case wizPair:
		m.input = PairShort(m.cfg.StartPair)
	case wizBridgePort:
		m.input = ""
	case wizNoBridge:
		if m.cfg.NoBridge {
			m.input = "y"
		} else {
			m.input = "n"
		}
	case wizBchRPC:
		m.input = m.cfg.BchRPC
	case wizNoBch:
		if m.cfg.NoBch {
			m.input = "y"
		} else {
			m.input = "n"
		}
	case wizHeadless:
		if m.cfg.Headless {
			m.input = "y"
		} else {
			m.input = "n"
		}
	}
	m.err = ""
}

func (m wizardModel) View() string {
	if m.width == 0 {
		return ""
	}

	w := m.width

	titleStyle := lipgloss.NewStyle().
		Foreground(ColorAccent).
		Bold(true).
		Width(w)

	stepStyle := lipgloss.NewStyle().
		Foreground(ColorMuted).
		Width(w)

	promptStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("#FFFFFF")).
		Bold(true)

	inputStyle := lipgloss.NewStyle().
		Foreground(ColorBullish).
		Bold(true)

	errStyle := lipgloss.NewStyle().
		Foreground(ColorBearish)

	navStyle := lipgloss.NewStyle().
		Foreground(ColorMuted).
		Width(w)

	// Header
	header := titleStyle.Render("  SWAPXFG  interactive setup")
	stepNum := fmt.Sprintf("  step %d/%d: %s", int(m.step)+1, len(wizStepNames), wizStepNames[m.step])
	stepLine := stepStyle.Render(stepNum)

	// Separator
	sep := lipgloss.NewStyle().Foreground(ColorMuted).Render(strings.Repeat("\u2500", min(w-2, 60)))

	// Prompt
	var prompt string
	var def string
	switch m.step {
	case wizDaemonRPC:
		prompt = "Fuego daemon RPC endpoint"
		def = "http://127.0.0.1:18180"
	case wizWalletRPC:
		prompt = "Wallet RPC endpoint (empty = no wallet)"
		def = ""
	case wizTestnet:
		prompt = "Use testnet? (y/n)"
		def = "n"
	case wizPair:
		prompt = "Starting pair (sol, eth, xmr, bch, arb, base)"
		def = PairShort(m.cfg.StartPair)
	case wizBridgePort:
		prompt = "Bridge server port (0 = random)"
		def = "0"
	case wizNoBridge:
		prompt = "Disable browser bridge server? (y/n)"
		def = "n"
	case wizBchRPC:
		prompt = "Electron Cash RPC endpoint"
		def = "http://127.0.0.1:7773"
	case wizNoBch:
		prompt = "Disable BCH connection? (y/n)"
		def = "n"
	case wizHeadless:
		prompt = "Run headless (no TUI, auto-execute)? (y/n)"
		def = "n"
	case wizConfirm:
		return m.viewConfirm(w, promptStyle, navStyle, errStyle, sep)
	}

	promptLine := promptStyle.Render("  " + prompt)
	defLine := ""
	if def != "" {
		defLine = defaultStyle.Render("    default: " + def)
	}

	cursor := "_"
	inputLine := inputStyle.Render("  > " + m.input) + inputStyle.Render(cursor)

	errLine := ""
	if m.err != "" {
		errLine = errStyle.Render("  " + m.err)
	}

	nav := navStyle.Render("  enter: continue  esc: back  ctrl+c: quit")

	content := lipgloss.JoinVertical(lipgloss.Left,
		"",
		header,
		"",
		stepLine,
		sep,
		"",
		promptLine,
		defLine,
		"",
		inputLine,
		"",
		errLine,
		"",
		sep,
		nav,
	)

	return lipgloss.Place(w, 0,
		lipgloss.Left, lipgloss.Top,
		content,
	)
}

func (m wizardModel) viewConfirm(w int, promptStyle, navStyle, errStyle lipgloss.Style, sep string) string {
	titleStyle := lipgloss.NewStyle().
		Foreground(ColorAccent).
		Bold(true).
		Width(w)

	header := titleStyle.Render("  SWAPXFG  configuration summary")

	lines := []string{
		"",
		header,
		"",
		sep,
		"",
		"  Daemon:     " + promptStyle.Render(m.cfg.DaemonRPC),
	}

	if m.cfg.WalletRPC != "" {
		lines = append(lines, "  Wallet:     "+promptStyle.Render(m.cfg.WalletRPC))
	} else {
		lines = append(lines, "  Wallet:     "+defaultStyle.Render("(none)"))
	}

	lines = append(lines,
		"  Testnet:    "+boolStr(m.cfg.Testnet),
		"  Pair:       "+promptStyle.Render(PairName(m.cfg.StartPair)),
	)

	if m.cfg.BridgePort > 0 {
		lines = append(lines, "  Bridge:     "+promptStyle.Render(fmt.Sprintf(":%d", m.cfg.BridgePort)))
	} else {
		lines = append(lines, "  Bridge:     "+defaultStyle.Render("random"))
	}

	lines = append(lines,
		"  BCH:        "+bchStr(m.cfg.BchRPC, m.cfg.NoBch),
		"  Headless:   "+boolStr(m.cfg.Headless),
		"",
		sep,
		"",
	)

	lines = append(lines,
		promptStyle.Render("  launch swapxfg with this config? [Y/n]"),
		"",
		navStyle.Render("  enter: launch  esc: reconfigure  ctrl+c: quit"),
	)

	return lipgloss.JoinVertical(lipgloss.Left, lines...)
}

func boolStr(b bool) string {
	if b {
		return lipgloss.NewStyle().Foreground(ColorBullish).Render("yes")
	}
	return defaultStyle.Render("no")
}

func bchStr(rpc string, noBch bool) string {
	if noBch {
		return lipgloss.NewStyle().Foreground(ColorMuted).Render("disabled")
	}
	if rpc != "" {
		return lipgloss.NewStyle().Foreground(ColorBullish).Render(rpc)
	}
	return defaultStyle.Render("(none)")
}

var defaultStyle = lipgloss.NewStyle().Foreground(ColorMuted)
