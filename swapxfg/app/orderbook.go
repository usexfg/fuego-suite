// swapxfg/app/orderbook.go
package app

import (
	"fmt"
	"sort"
	"strconv"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

// RenderOrderbook draws the ask/bid display for a given pair.
// width and height constrain the panel size.
func RenderOrderbook(offers []SwapOffer, width, height int) string {
	title := lipgloss.NewStyle().Bold(true).Foreground(ColorActiveTab).
		Width(width).Align(lipgloss.Center).Render("ORDER BOOK")

	sep := lipgloss.NewStyle().Foreground(ColorMuted).
		Width(width).Align(lipgloss.Center).Render(strings.Repeat("─", width-2))

	if len(offers) == 0 {
		empty := StyleMuted.Render("  no offers")
		return lipgloss.JoinVertical(lipgloss.Left, title, sep, empty)
	}

	// Split into asks (sellers) and bids (buyers)
	// Convention: offers where maker is selling XFG are asks, buying XFG are bids
	// For now, treat RateNum > median as asks, below as bids
	// Simple heuristic: just sort by rate and split in half
	type entry struct {
		rate   float64
		amount float64
		raw    SwapOffer
	}

	var entries []entry
	for _, o := range offers {
		r, err := strconv.ParseFloat(fmt.Sprintf("%d.%07d", o.RateNum/10000000, o.RateNum%10000000), 64)
		if err != nil || r <= 0 {
			// Fallback: use rateNum as integer rate
			r = float64(o.RateNum)
		}
		amt := float64(o.XfgAmount) / 1e7
		entries = append(entries, entry{rate: r, amount: amt, raw: o})
	}

	sort.Slice(entries, func(i, j int) bool {
		return entries[i].rate > entries[j].rate
	})

	// Top half = asks (higher prices), bottom half = bids (lower prices)
	mid := len(entries) / 2
	if mid == 0 {
		mid = 1
	}
	asks := entries[:mid]
	bids := entries[mid:]

	maxRows := (height - 4) / 2 // leave room for title, sep, spread
	if maxRows < 1 {
		maxRows = 1
	}

	var lines []string
	lines = append(lines, title, sep)

	// Asks: show highest first (cheapest near spread)
	for i := 0; i < len(asks) && i < maxRows; i++ {
		a := asks[len(asks)-1-i] // reverse: cheapest at bottom
		if i >= len(asks) {
			break
		}
		a = asks[i]
		line := fmt.Sprintf("  ASK %10.5f  %8.1f XFG", a.rate, a.amount)
		lines = append(lines, StyleBear.Render(truncPad(line, width)))
	}

	// Spread line
	spread := "—"
	if len(asks) > 0 && len(bids) > 0 {
		lowestAsk := asks[len(asks)-1].rate
		highestBid := bids[0].rate
		spread = fmt.Sprintf("%.5f", lowestAsk-highestBid)
	}
	spreadLine := StyleSpread.Render(
		lipgloss.NewStyle().Width(width).Align(lipgloss.Center).
			Render(fmt.Sprintf("━━━ spread %s ━━━", spread)))
	lines = append(lines, spreadLine)

	// Bids: best bid at top
	for i := 0; i < len(bids) && i < maxRows; i++ {
		b := bids[i]
		line := fmt.Sprintf("  BID %10.5f  %8.1f XFG", b.rate, b.amount)
		lines = append(lines, StyleBull.Render(truncPad(line, width)))
	}

	return lipgloss.JoinVertical(lipgloss.Left, lines...)
}

func truncPad(s string, w int) string {
	if len(s) >= w {
		return s[:w]
	}
	return s + strings.Repeat(" ", w-len(s))
}
