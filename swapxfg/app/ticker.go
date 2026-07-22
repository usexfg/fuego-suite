// swapxfg/app/ticker.go
package app

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

// xfgUsdPrice extracts the XFG/USD reference price from DeFiLlama.
// This is NOT the swap rate — it's a reference for user pricing decisions.
func xfgRefUsd(ext *ExternalPrices) string {
	if ext != nil && ext.XFG > 0 {
		return fmt.Sprintf("$%.4f", ext.XFG)
	}
	return "$—"
}

// xfgSwapRate returns the on-chain swap rate from daemon TWAP/composite.
// This is the ACTUAL rate that swaps execute at.
func xfgSwapRate(prices map[uint8]*SwapPriceResponse) string {
	for _, p := range ActivePairs {
		if pr, ok := prices[p]; ok && pr.XfgUsdMid != "" && pr.XfgUsdMid != "0.000000" {
			if val, err := strconv.ParseFloat(pr.XfgUsdMid, 64); err == nil && val > 0 {
				return fmt.Sprintf("$%.4f", val)
			}
		}
	}
	return "$—"
}

// pairCtrUsd returns the counterparty asset's reference USD price from DeFiLlama.
func pairCtrUsd(pair uint8, ext *ExternalPrices) string {
	if ext == nil {
		return ""
	}
	switch pair {
	case PairSOL:
		if ext.SOL > 0 { return fmt.Sprintf("$%.0f", ext.SOL) }
	case PairETH:
		if ext.ETH > 0 { return fmt.Sprintf("$%.0f", ext.ETH) }
	case PairXMR:
		if ext.XMR > 0 { return fmt.Sprintf("$%.0f", ext.XMR) }
	case PairBCH:
		if ext.BCH > 0 { return fmt.Sprintf("$%.0f", ext.BCH) }
	case PairBNB:
		if ext.BNB > 0 { return fmt.Sprintf("$%.0f", ext.BNB) }
	case PairARB, PairBASE:
		if ext.ETH > 0 { return fmt.Sprintf("$%.0f", ext.ETH) }
	}
	return ""
}

// RenderTicker draws the top market ticker bar showing XFG USD, all pairs, and block height.
func RenderTicker(activePair uint8, prices map[uint8]*SwapPriceResponse, ext *ExternalPrices, height uint64, width int, connected bool) string {
	var parts []string

	// Logo
	logo := StyleAccent.Render("⚛SWAPXFG")
	parts = append(parts, logo)

	// XFG rate — daemon on-chain TWAP/composite (the actual swap rate)
	xfgSwap := StyleBull.Render(fmt.Sprintf("XFG %s", xfgSwapRate(prices)))
	parts = append(parts, xfgSwap)

	// XFG reference price from DeFiLlama (for user context)
	xfgRef := xfgRefUsd(ext)
	if xfgRef != "$—" {
		ref := StyleMuted.Render(fmt.Sprintf("ref %s", xfgRef))
		parts = append(parts, ref)
	}

	for _, p := range ActivePairs {
		name := PairShort(p)
		pr := prices[p]
		rate := "—"
		if pr != nil && pr.CompositeRate != "" {
			rate = pr.CompositeRate
		}

		// Show reference USD price in parentheses
		ctrUsd := pairCtrUsd(p, ext)
		label := fmt.Sprintf("%s %s", name, rate)
		if ctrUsd != "" {
			label = fmt.Sprintf("%s %s (%s)", name, rate, ctrUsd)
		}

		var styled string
		if p == activePair {
			styled = StyleActiveTab.Render(fmt.Sprintf(" %s ", label))
		} else {
			styled = StyleInactiveTab.Render(label)
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

// RenderTickerWithCD draws the ticker bar including XFG USD and CD/XFG market segment.
func RenderTickerWithCD(activePair uint8, prices map[uint8]*SwapPriceResponse, ext *ExternalPrices, cdOffers []CdOffer, height uint64, width int, connected bool) string {
	var parts []string

	logo := StyleAccent.Render("⚛SWAPXFG")
	parts = append(parts, logo)

	// XFG rate — daemon on-chain TWAP/composite
	xfgSwap := StyleBull.Render(fmt.Sprintf("XFG %s", xfgSwapRate(prices)))
	parts = append(parts, xfgSwap)

	// XFG reference price from DeFiLlama
	xfgRef := xfgRefUsd(ext)
	if xfgRef != "$—" {
		ref := StyleMuted.Render(fmt.Sprintf("ref %s", xfgRef))
		parts = append(parts, ref)
	}

	for _, p := range ActivePairs {
		name := PairShort(p)
		pr := prices[p]
		rate := "—"
		if pr != nil && pr.CompositeRate != "" {
			rate = pr.CompositeRate
		}

		ctrUsd := pairCtrUsd(p, ext)
		label := fmt.Sprintf("%s %s", name, rate)
		if ctrUsd != "" {
			label = fmt.Sprintf("%s %s (%s)", name, rate, ctrUsd)
		}

		var styled string
		if p == activePair {
			styled = StyleActiveTab.Render(fmt.Sprintf(" %s ", label))
		} else {
			styled = StyleInactiveTab.Render(label)
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

// RenderPriceLine shows the price beacon, TWAP, and composite below the chart.
// Sources are labeled: on-chain rates (swap execution) vs DeFiLlama (reference).
func RenderPriceLine(pair uint8, prices map[uint8]*SwapPriceResponse, ext *ExternalPrices) string {
	pr := prices[pair]
	if pr == nil {
		return StyleMuted.Render("  XFG — H\u2CB6\u2206T | ref $—  |  TWAP: —  Composite: —  (on-chain)")
	}
	
	twap := pr.Twap
	comp := pr.CompositeRate
	if twap == "" {
		twap = "—"
	}
	if comp == "" {
		comp = "—"
	}

	hearth := pr.HearthRatio
	if hearth == "" || hearth == "0.0" {
		hearth = "—"
	} else {
		if val, err := strconv.ParseFloat(hearth, 64); err == nil && val > 0 {
			// hearth is XFG per HⲶ∆T — invert for HⲶ∆T per XFG
			hearth = fmt.Sprintf("%.4f", 1.0/val)
		}
	}

	// Reference XFG USD from DeFiLlama (for user context, not swap execution)
	xfgRef := "—"
	if ext != nil && ext.XFG > 0 {
		xfgRef = fmt.Sprintf("%.4f", ext.XFG)
	}

	beacon := fmt.Sprintf("XFG %s H\u2CB6\u2206T | ref $%s", hearth, xfgRef)
	rates := fmt.Sprintf("TWAP: %s  Composite: %s  (on-chain)", twap, comp)
	
	return StyleAccent.Render("  " + beacon + "  ") + StyleMuted.Render("|  " + rates)
}
