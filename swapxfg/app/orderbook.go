// swapxfg/app/orderbook.go
package app

import (
	"fmt"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

const priceDivisor = 1e7 // prices are in piconeros

// aggregateLevels merges price levels within 1% of each other into buckets.
// Levels closer to the spread keep fine granularity, levels further out are coarser.
func aggregateLevels(levels []OrderBookLevel) []OrderBookLevel {
	if len(levels) == 0 {
		return levels
	}

	var result []OrderBookLevel
	current := levels[0]

	for i := 1; i < len(levels); i++ {
		next := levels[i]
		// Check if within 1% of current bucket's price
		curPrice := float64(current.Price)
		nextPrice := float64(next.Price)
		if curPrice > 0 && nextPrice > 0 {
			diff := nextPrice - curPrice
			if diff < 0 {
				diff = -diff
			}
			if diff/curPrice < 0.01 {
				// Merge into current bucket (check for overflow)
				if next.Amount > 0 && current.Amount+next.Amount < current.Amount {
					current.Amount = ^uint64(0) // cap at max
				} else {
					current.Amount += next.Amount
				}
				continue
			}
		}
		result = append(result, current)
		current = next
	}
	result = append(result, current)
	return result
}

// RenderOrderbook draws a depth-ladder orderbook from an OrderBookSnapshot.
// Asks are shown with lowest at bottom (near spread), bids with highest at top (near spread).
// If aggregate is true, levels within 1% of each other are merged into buckets.
func RenderOrderbook(book *OrderBookSnapshot, width, height int, aggregate bool) string {
	title := lipgloss.NewStyle().Bold(true).Foreground(ColorActiveTab).
		Width(width).Align(lipgloss.Center).Render("ORDER BOOK")

	sep := lipgloss.NewStyle().Foreground(ColorMuted).
		Width(width).Align(lipgloss.Center).Render(strings.Repeat("─", max(width-2, 1)))

	if book == nil || (len(book.Bids) == 0 && len(book.Asks) == 0) {
		empty := StyleMuted.Render("  no orders")
		return lipgloss.JoinVertical(lipgloss.Left, title, sep, empty)
	}

	// Aggregate levels if requested
	asks := book.Asks
	bids := book.Bids
	if aggregate {
		asks = aggregateLevels(asks)
		bids = aggregateLevels(bids)
	}

	// Reserve rows: 2 header/sep, 1 spread line, 1 footer = 4
	maxRows := height - 4
	if maxRows < 2 {
		maxRows = 2
	}

	// Show asks (reversed so lowest ask is at bottom, nearest spread)
	askCount := len(asks)
	askShow := askCount
	if askShow > maxRows/2 {
		askShow = maxRows / 2
	}
	if askShow < 1 && askCount > 0 {
		askShow = 1
	}

	// Show bids (best bid at top, nearest spread at bottom)
	bidCount := len(bids)
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
			depth += asks[i].Amount
			if depth > maxDepth {
				maxDepth = depth
			}
		}
	}
	depth = 0
	for i := 0; i < bidShow && i < bidCount; i++ {
		depth += bids[i].Amount
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
		lvl := asks[i]
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
	pctStr := ""
	midStr := ""
	if len(asks) > 0 && len(bids) > 0 {
		bestAsk := float64(asks[0].Price) / priceDivisor
		bestBid := float64(bids[0].Price) / priceDivisor
		spread := bestAsk - bestBid
		spreadStr = fmt.Sprintf("%.5f", spread)
		mid := (bestAsk + bestBid) / 2
		midStr = fmt.Sprintf("mid %.5f", mid)
		if mid > 0 {
			pct := spread / mid * 100
			pctStr = fmt.Sprintf("%.2f%%", pct)
		}
	}
	spreadRender := fmt.Sprintf("━━ spread %s", spreadStr)
	if pctStr != "" {
		spreadRender += fmt.Sprintf(" (%s)", pctStr)
	}
	if midStr != "" {
		spreadRender += fmt.Sprintf("  %s", midStr)
	}
	spreadLine := StyleSpread.Render(
		lipgloss.NewStyle().Width(width).Align(lipgloss.Center).
			Render(spreadRender + " ━━"))
	lines = append(lines, spreadLine)

	// Bids: show the `bidShow` best bids, top row = highest bid (best bid)
	depth = 0
	for i := 0; i < bidShow && i < bidCount; i++ {
		lvl := bids[i]
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
