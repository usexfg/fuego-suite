package app

import (
	"fmt"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

type SidebarView = int

const (
	ViewMarkets SidebarView = iota
	ViewCD
	ViewStatus
)

func viewLabel(v SidebarView) string {
	switch v {
	case ViewMarkets:
		return "Markets"
	case ViewCD:
		return "CD Market"
	case ViewStatus:
		return "Status"
	default:
		return "?"
	}
}

func viewHotkey(v SidebarView) string {
	switch v {
	case ViewMarkets:
		return "M"
	case ViewCD:
		return "C"
	case ViewStatus:
		return "S"
	default:
		return "?"
	}
}

var sidebarViews = []SidebarView{ViewMarkets, ViewCD, ViewStatus}

func RenderSidebar(
	activeView SidebarView,
	activePair uint8,
	bridge *BridgeServer,
	ethAddr, ethBal, solAddr, solBal, bchBal string,
	height uint64,
	connected bool,
	width int,
) string {
	bg := lipgloss.NewStyle().Background(lipgloss.Color("#111111"))
	headerStyle := lipgloss.NewStyle().Bold(true).Foreground(ColorAccent).Background(lipgloss.Color("#111111")).PaddingLeft(1)
	sectionStyle := lipgloss.NewStyle().Foreground(ColorMuted).Background(lipgloss.Color("#111111")).PaddingLeft(1)
	viewActiveStyle := lipgloss.NewStyle().Bold(true).Foreground(ColorActiveTab).Background(lipgloss.Color("#222222")).PaddingLeft(1)
	viewInactiveStyle := lipgloss.NewStyle().Foreground(ColorMuted).Background(lipgloss.Color("#111111")).PaddingLeft(1)
	pairActiveStyle := lipgloss.NewStyle().Bold(true).Foreground(ColorActiveTab).Background(lipgloss.Color("#111111")).PaddingLeft(3)
	pairInactiveStyle := lipgloss.NewStyle().Foreground(ColorMuted).Background(lipgloss.Color("#111111")).PaddingLeft(3)
	bridgeOKStyle := lipgloss.NewStyle().Foreground(ColorConnOK).Background(lipgloss.Color("#111111"))
	bridgeOffStyle := lipgloss.NewStyle().Foreground(ColorMuted).Background(lipgloss.Color("#111111"))

	pad := func(s string) string {
		return bg.Render(lipgloss.NewStyle().Width(width).PaddingLeft(0).Render(s))
	}

	var lines []string

	// Header
	lines = append(lines, pad(headerStyle.Render("SWAPXFG")))

	// Divider
	lines = append(lines, pad(sectionStyle.Render(strings.Repeat("─", width-1))))

	// Views
	for _, v := range sidebarViews {
		hotkey := viewHotkey(v)
		label := viewLabel(v)
		var line string
		if v == activeView {
			line = viewActiveStyle.Render(fmt.Sprintf("[%s] %s", hotkey, label))
		} else {
			line = viewInactiveStyle.Render(fmt.Sprintf("[%s] %s", hotkey, label))
		}
		lines = append(lines, pad(line))
	}

	// Pair list (only visible when Markets is active)
	if activeView == ViewMarkets {
		lines = append(lines, pad(sectionStyle.Render("")))
		for _, p := range ActivePairs {
			marker := " "
			if p == activePair {
				marker = ">"
			}
			label := PairShort(p)
			if p == activePair {
				lines = append(lines, pad(pairActiveStyle.Render(fmt.Sprintf("%s %s", marker, label))))
			} else {
				lines = append(lines, pad(pairInactiveStyle.Render(fmt.Sprintf("  %s", label))))
			}
		}
	}

	// Bridge status
	lines = append(lines, pad(sectionStyle.Render("")))
	lines = append(lines, pad(sectionStyle.Render("BRIDGE")))

	ethStatus := bridgeOffStyle.Render("✗")
	if ethAddr != "" {
		ethStatus = bridgeOKStyle.Render(fmt.Sprintf("✓ %s", formatShortBal(ethBal, "ETH")))
	}
	lines = append(lines, pad(sectionStyle.Render(fmt.Sprintf("  ETH  %s", ethStatus))))

	solStatus := bridgeOffStyle.Render("✗")
	if solAddr != "" {
		solStatus = bridgeOKStyle.Render("✓")
	}
	lines = append(lines, pad(sectionStyle.Render(fmt.Sprintf("  SOL  %s", solStatus))))

	bchStatus := bridgeOffStyle.Render("✗")
	if bchBal != "" {
		bchStatus = bridgeOKStyle.Render(fmt.Sprintf("✓ %s", bchBal))
	}
	lines = append(lines, pad(sectionStyle.Render(fmt.Sprintf("  BCH  %s", bchStatus))))

	// Block height + connection
	lines = append(lines, pad(sectionStyle.Render("")))
	connChar := bridgeOffStyle.Render("●")
	if connected {
		connChar = bridgeOKStyle.Render("●")
	}
	lines = append(lines, pad(sectionStyle.Render(fmt.Sprintf("BLK %d  %s", height, connChar))))

	return lipgloss.JoinVertical(lipgloss.Left, lines...)
}

func formatShortBal(weiStr, symbol string) string {
	if weiStr == "" {
		return "0"
	}
	var weiF float64
	fmt.Sscanf(weiStr, "%f", &weiF)
	return fmt.Sprintf("%.2f %s", weiF/1e18, symbol)
}
