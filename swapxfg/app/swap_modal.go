package app

import (
	"fmt"
	"strconv"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

type swapModalModel struct {
	active bool

	pair        uint8
	pairName    string
	xfgAmount   string // user-friendly string (e.g. "100")
	xfgAtomic   uint64 // parsed atomic units
	ctrAmount   string // estimated counterparty amount
	rate        string // current composite rate
	feeEstimate string // estimated fee in XFG

	confirm bool  // user pressed enter
	done    bool  // modal is finished (either confirmed or cancelled)
	err     string
}

var (
	modalBorderStyle = lipgloss.NewStyle().Border(lipgloss.RoundedBorder()).BorderForeground(ColorAccent).Padding(1, 2)
	modalTitleStyle  = lipgloss.NewStyle().Bold(true).Foreground(ColorAccent)
	modalLabelStyle  = lipgloss.NewStyle().Foreground(ColorMuted)
	modalValueStyle  = lipgloss.NewStyle().Foreground(ColorActiveTab)
	modalHintStyle   = lipgloss.NewStyle().Foreground(ColorMuted)
	modalErrStyle    = lipgloss.NewStyle().Foreground(ColorBearish)
)

func newSwapModal(pair uint8, amount string, rate string) swapModalModel {
	xfgAtomic, err := parseAmountAtomic(amount, 1e7)
	if err != nil {
		xfgAtomic = 0
	}

	var ctrEstimate string
	if rateFloat, err := strconv.ParseFloat(rate, 64); err == nil && rateFloat > 0 {
		ctrAmount := float64(xfgAtomic) / 1e7 / rateFloat
		ctrEstimate = fmt.Sprintf("%.6f", ctrAmount)
	} else {
		ctrEstimate = "—"
	}

	fee := float64(xfgAtomic) / 1e7 * 0.001 // 0.1% fee estimate
	feeStr := fmt.Sprintf("%.4f XFG", fee)

	return swapModalModel{
		active:      true,
		pair:        pair,
		pairName:    PairShort(pair),
		xfgAmount:   amount,
		xfgAtomic:   xfgAtomic,
		ctrAmount:   ctrEstimate,
		rate:        rate,
		feeEstimate: feeStr,
	}
}

func (m swapModalModel) Init() tea.Cmd { return nil }

func (m swapModalModel) Update(msg tea.Msg) (swapModalModel, tea.Cmd) {
	if !m.active {
		return m, nil
	}
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "enter":
			m.confirm = true
			m.done = true
		case "esc", "ctrl+c":
			m.done = true
		}
	}
	return m, nil
}

func (m swapModalModel) View() string {
	if !m.active {
		return ""
	}

	var lines []string

	lines = append(lines, modalTitleStyle.Render("│  CONFIRM SWAP"))
	lines = append(lines, modalLabelStyle.Render("│"))
	lines = append(lines, modalLabelStyle.Render(fmt.Sprintf("│  Sell  %s %s", modalValueStyle.Render(m.xfgAmount), modalValueStyle.Render("XFG"))))
	lines = append(lines, modalLabelStyle.Render(fmt.Sprintf("│  Buy   %s %s", modalValueStyle.Render(m.ctrAmount), modalValueStyle.Render(m.pairName))))
	lines = append(lines, modalLabelStyle.Render("│"))
	lines = append(lines, modalLabelStyle.Render(fmt.Sprintf("│  Pair: %s", modalValueStyle.Render(PairName(m.pair)))))
	lines = append(lines, modalLabelStyle.Render(fmt.Sprintf("│  Rate: %s", modalValueStyle.Render(m.rate))))
	lines = append(lines, modalLabelStyle.Render(fmt.Sprintf("│  Fee:  %s", modalValueStyle.Render(m.feeEstimate))))
	lines = append(lines, modalLabelStyle.Render("│"))
	if m.err != "" {
		lines = append(lines, modalErrStyle.Render(fmt.Sprintf("│  %s", m.err)))
		lines = append(lines, modalLabelStyle.Render("│"))
	}
	lines = append(lines, modalHintStyle.Render("│  [enter] Confirm    [esc] Cancel"))
	lines = append(lines, modalLabelStyle.Render("│"))

	content := strings.Join(lines, "\n")
	return modalBorderStyle.Render(content)
}

func (m swapModalModel) Result() (confirmed bool, pair uint8, amount uint64, rate string) {
	return m.confirm, m.pair, m.xfgAtomic, m.rate
}

// RenderSwapStatus draws a status line showing the current swap being drafted.
func RenderSwapStatus(amount string, pairName string, rate string) string {
	if amount == "" {
		return StyleMuted.Render("  Type /swap <amount> [pair] to start")
	}
	return lipgloss.JoinHorizontal(lipgloss.Left,
		StyleMuted.Render("  Swap "),
		StyleBull.Render(fmt.Sprintf("%s XFG", amount)),
		StyleMuted.Render(fmt.Sprintf(" for %s ", pairName)),
		StyleMuted.Render(fmt.Sprintf("@ %s", rate)),
	)
}
