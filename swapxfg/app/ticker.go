// swapxfg/app/ticker.go
package app

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

// RenderTicker draws the top market ticker bar showing all pairs + block height.
func RenderTicker(activePair uint8, prices map[uint8]*SwapPriceResponse, height uint64, width int, connected bool) string {
	var parts []string

	// Logo
	logo := StyleAccent.Render("⚛️SWAPXFG")
	parts = append(parts, logo)

	for _, p := range ActivePairs {
		name := PairShort(p)
		pr := prices[p]
		rate := "—"
		if pr != nil && pr.CompositeRate != "" {
			rate = pr.CompositeRate
		}

		var styled string
		if p == activePair {
			styled = StyleActiveTab.Render(fmt.Sprintf(" %s %s ", name, rate))
		} else {
			styled = StyleInactiveTab.Render(fmt.Sprintf("%s %s", name, rate))
		}
		parts = append(parts, styled)
	}

	// Block height
	blk := StyleMuted.Render(fmt.Sprintf("BLK %d", height))
	parts = append(parts, blk)

	// Connection indicator
	if connected {
		parts = append(parts, lipgloss.NewStyle().Foreground(ColorConnOK).Render("●"))
	} else {
		parts = append(parts, lipgloss.NewStyle().Foreground(ColorConnLost).Render("●"))
	}

	row := strings.Join(parts, "  ")
	return lipgloss.NewStyle().Width(width).Render(row)
}

// RenderTickerWithCD draws the ticker bar including the CD/XFG market segment.
func RenderTickerWithCD(activePair uint8, prices map[uint8]*SwapPriceResponse, cdOffers []CdOffer, height uint64, width int, connected bool) string {
	var parts []string

	logo := StyleAccent.Render("⚛️SWAPXFG")
	parts = append(parts, logo)

	for _, p := range ActivePairs {
		name := PairShort(p)
		pr := prices[p]
		rate := "—"
		if pr != nil && pr.CompositeRate != "" {
			rate = pr.CompositeRate
		}

		var styled string
		if p == activePair {
			styled = StyleActiveTab.Render(fmt.Sprintf(" %s %s ", name, rate))
		} else {
			styled = StyleInactiveTab.Render(fmt.Sprintf("%s %s", name, rate))
		}
		parts = append(parts, styled)
	}

	// CD market tab
	parts = append(parts, RenderCdTicker(cdOffers, activePair == PairCD))

	blk := StyleMuted.Render(fmt.Sprintf("BLK %d", height))
	parts = append(parts, blk)

	if connected {
		parts = append(parts, lipgloss.NewStyle().Foreground(ColorConnOK).Render("●"))
	} else {
		parts = append(parts, lipgloss.NewStyle().Foreground(ColorConnLost).Render("●"))
	}

	row := strings.Join(parts, "  ")
	return lipgloss.NewStyle().Width(width).Render(row)
}

// RenderPriceLine shows TWAP + composite below the chart.
func RenderPriceLine(pair uint8, prices map[uint8]*SwapPriceResponse) string {
	pr := prices[pair]
	if pr == nil {
		return StyleMuted.Render("  TWAP: —  Composite: —")
	}
	twap := pr.Twap
	comp := pr.CompositeRate
	if twap == "" {
		twap = "—"
	}
	if comp == "" {
		comp = "—"
	}
	xfgUsd := ""
	if pr.XfgUsdMid != "" {
		v, err := strconv.ParseFloat(pr.XfgUsdMid, 64)
		if err == nil && v > 0 {
			xfgUsd = fmt.Sprintf("  XFG $%.4f", v)
		}
	}
	return StyleMuted.Render(fmt.Sprintf("  TWAP: %s  Composite: %s%s", twap, comp, xfgUsd))
}
