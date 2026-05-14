// swapxfg/app/tape.go
package app

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	"github.com/charmbracelet/lipgloss"
)

// RenderTape draws the recent trade tape for a pair.
func RenderTape(trades []SwapTrade, width, height int) string {
	title := lipgloss.NewStyle().Bold(true).Foreground(ColorActiveTab).
		Width(width).Align(lipgloss.Center).Render("TRADE TAPE")

	sep := lipgloss.NewStyle().Foreground(ColorMuted).
		Width(width).Align(lipgloss.Center).Render(strings.Repeat("─", width-2))

	if len(trades) == 0 {
		empty := StyleMuted.Render("  no trades")
		return lipgloss.JoinVertical(lipgloss.Left, title, sep, empty)
	}

	maxRows := height - 2
	if maxRows < 1 {
		maxRows = 1
	}

	now := time.Now().Unix()
	var lines []string
	lines = append(lines, title, sep)

	for i := 0; i < len(trades) && i < maxRows; i++ {
		t := trades[i]
		rate := t.Rate
		amt := float64(t.XfgAmount) / 1e7

		// Age
		age := "—"
		if t.Timestamp > 0 {
			diff := now - int64(t.Timestamp)
			if diff < 60 {
				age = fmt.Sprintf("%ds", diff)
			} else if diff < 3600 {
				age = fmt.Sprintf("%dm", diff/60)
			} else {
				age = fmt.Sprintf("%dh", diff/3600)
			}
		}

		// Direction heuristic: alternate based on index (no real buy/sell from RPC yet)
		dir := "BUY "
		style := StyleBull
		rateF, _ := strconv.ParseFloat(rate, 64)
		if i > 0 {
			prevRate, _ := strconv.ParseFloat(trades[i-1].Rate, 64)
			if rateF < prevRate {
				dir = "SELL"
				style = StyleBear
			}
		}

		line := fmt.Sprintf("  %8s %6.1f XFG  %s %4s", rate, amt, dir, age)
		lines = append(lines, style.Render(truncPad(line, width)))
	}

	return lipgloss.JoinVertical(lipgloss.Left, lines...)
}
