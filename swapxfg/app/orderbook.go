// swapxfg/app/orderbook.go
package app

import (
	"fmt"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

const priceDivisor = 1e7 // prices are in piconeros

// RenderOrderbook draws a depth-ladder orderbook from an OrderBookSnapshot.
// Asks are shown with lowest at bottom (near spread), bids with highest at top (near spread).
func RenderOrderbook(book *OrderBookSnapshot, width, height int) string {
	title := lipgloss.NewStyle().Bold(true).Foreground(ColorActiveTab).
		Width(width).Align(lipgloss.Center).Render("ORDER BOOK")

	sep := lipgloss.NewStyle().Foreground(ColorMuted).
		Width(width).Align(lipgloss.Center).Render(strings.Repeat("─", max(width-2, 1)))

	if book == nil || (len(book.Bids) == 0 && len(book.Asks) == 0) {
		empty := StyleMuted.Render("  no orders")
		return lipgloss.JoinVertical(lipgloss.Left, title, sep, empty)
	}

	// Reserve rows: 2 header/sep, 1 spread line, 1 footer = 4
	maxRows := height - 4
	if maxRows < 2 {
		maxRows = 2
	}

	// Show asks (reversed so lowest ask is at bottom, nearest spread)
	askCount := len(book.Asks)
	askShow := askCount
	if askShow > maxRows/2 {
		askShow = maxRows / 2
	}
	if askShow < 1 && askCount > 0 {
		askShow = 1
	}

	// Show bids (best bid at top, nearest spread at bottom)
	bidCount := len(book.Bids)
	bidShow := bidCount
	if bidShow > maxRows-askShow {
		bidShow = maxRows - askShow
	}
	if bidShow < 1 && bidCount > 0 {
		bidShow = 1
	}

	// Compute max cumulative depth for bar scaling
	maxDepth := uint64(0)
	depth := uint64(0)
	for i := askCount - askShow; i < askCount; i++ {
		if i >= 0 && i < askCount {
			depth += book.Asks[i].Amount
			if depth > maxDepth {
				maxDepth = depth
			}
		}
	}
	depth = 0
	for i := 0; i < bidShow && i < bidCount; i++ {
		depth += book.Bids[i].Amount
		if depth > maxDepth {
			maxDepth = depth
		}
	}
	if maxDepth == 0 {
		maxDepth = 1
	}

	barW := width/2 - 12 // bar area width per side
	if barW < 4 {
		barW = 4
	}

	var lines []string
	lines = append(lines, title, sep)

	// Column header
	hdr := fmt.Sprintf("  %-12s %8s  %s", "PRICE", "XFG", "DEPTH")
	lines = append(lines, StyleMuted.Render(truncPad(hdr, width)))

	// Asks: show the `askShow` lowest asks, bottom row = lowest ask (best ask)
	askStart := askCount - askShow
	if askStart < 0 {
		askStart = 0
	}
	cumDepth := uint64(0)
	for i := askCount - 1; i >= askStart; i-- {
		lvl := book.Asks[i]
		cumDepth += lvl.Amount
		price := float64(lvl.Price) / priceDivisor
		amtXfg := float64(lvl.Amount) / priceDivisor
		barLen := int(float64(cumDepth) / float64(maxDepth) * float64(barW))
		bar := strings.Repeat("█", barLen)
		priceStr := fmt.Sprintf("%12.5f", price)
		amtStr := fmt.Sprintf("%8.1f", amtXfg)
		line := fmt.Sprintf("%s  %s  %s%s",
			StyleBear.Render(priceStr),
			StyleBear.Render(amtStr),
			StyleBear.Render(bar),
			strings.Repeat(" ", max(barW-barLen, 0)),
		)
		lines = append(lines, truncPad(line, width))
	}

	// Spread line
	spreadStr := "—"
	if len(book.Asks) > 0 && len(book.Bids) > 0 {
		bestAsk := float64(book.Asks[0].Price) / priceDivisor
		bestBid := float64(book.Bids[0].Price) / priceDivisor
		spreadStr = fmt.Sprintf("%.5f", bestAsk-bestBid)
	}
	spreadLine := StyleSpread.Render(
		lipgloss.NewStyle().Width(width).Align(lipgloss.Center).
			Render(fmt.Sprintf("━━ spread %s ━━", spreadStr)))
	lines = append(lines, spreadLine)

	// Bids: show the `bidShow` best bids, top row = highest bid (best bid)
	depth = 0
	for i := 0; i < bidShow && i < bidCount; i++ {
		lvl := book.Bids[i]
		depth += lvl.Amount
		price := float64(lvl.Price) / priceDivisor
		amtXfg := float64(lvl.Amount) / priceDivisor
		barLen := int(float64(depth) / float64(maxDepth) * float64(barW))
		bar := strings.Repeat("█", barLen)
		priceStr := fmt.Sprintf("%12.5f", price)
		amtStr := fmt.Sprintf("%8.1f", amtXfg)
		line := fmt.Sprintf("%s  %s  %s%s",
			StyleBull.Render(priceStr),
			StyleBull.Render(amtStr),
			StyleBull.Render(bar),
			strings.Repeat(" ", max(barW-barLen, 0)),
		)
		lines = append(lines, truncPad(line, width))
	}

	return lipgloss.JoinVertical(lipgloss.Left, lines...)
}

func truncPad(s string, w int) string {
	if len(s) >= w {
		return s[:w]
	}
	return s + strings.Repeat(" ", w-len(s))
}
