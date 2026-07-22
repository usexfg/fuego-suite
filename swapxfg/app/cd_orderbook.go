// swapxfg/app/cd_orderbook.go
package app

import (
	"fmt"
	"sort"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

// HEAT CD amount tiers (atomic units, 1e7 = 1 HEAT)
const (
	CdTier0 = 80000000    // 8 HEAT
	CdTier1 = 800000000   // 80 HEAT
	CdTier2 = 8000000000  // 800 HEAT
	CdTier3 = 80000000000 // 8000 HEAT
)

// EpochBlocks is the mainnet epoch duration in blocks.
// Testnet overrides this at 10 blocks for fast testing.
const EpochBlocks = 900 // 5 days (180 blocks/day)

// CdTierLabel returns the human label for a CD amount tier.
func CdTierLabel(amount uint64) string {
	switch amount {
	case CdTier0:
		return "8 HEAT"
	case CdTier1:
		return "80 HEAT"
	case CdTier2:
		return "800 HEAT"
	case CdTier3:
		return "8000 HEAT"
	default:
		return fmt.Sprintf("%.0f HEAT", float64(amount)/1e7)
	}
}

// sortCdOffers returns a copy of offers sorted most-discounted first.
func sortCdOffers(offers []CdOffer) []CdOffer {
	sorted := make([]CdOffer, len(offers))
	copy(sorted, offers)
	sort.Slice(sorted, func(i, j int) bool {
		di := CdDiscount(sorted[i].CdAmount, sorted[i].AskPrice)
		dj := CdDiscount(sorted[j].CdAmount, sorted[j].AskPrice)
		return di < dj
	})
	return sorted
}

// RenderCdOrderbook renders the CD depth ladder as a table:
// TERM | REM | AMOUNT | DISC | INT | PRICE (XFG)
func RenderCdOrderbook(offers []CdOffer, prices map[uint64]*CdPriceStats, currentHeight uint64, selected, width, height int) string {
	title := lipgloss.NewStyle().Bold(true).Foreground(ColorActiveTab).
		Width(width).Align(lipgloss.Center).Render("HEAT CDs — priced in XFG")

	sep := StyleMuted.Render(strings.Repeat("─", width-2))

	if len(offers) == 0 {
		empty := StyleMuted.Render("  no HEAT CD offers")
		help := StyleMuted.Render("  ↑↓: select  n: create CD")
		return lipgloss.JoinVertical(lipgloss.Left, title, sep, "", empty, "", help)
	}

	sorted := sortCdOffers(offers)

	// Column header
	hdr := fmt.Sprintf("  %-5s  %-4s  %-10s  %-7s  %-8s  %s",
		"TERM", "REM", "AMOUNT", "DISC", "INT", "PRICE")
	rows := []string{title, sep, StyleMuted.Render(hdr), sep}

	for i, o := range sorted {
		// Term (total epochs)
		termStr := fmt.Sprintf("%dep", o.CdTerm)

		// Remaining epochs: CdTerm - (currentHeight - PostedHeight) / EpochBlocks
		remStr := "—"
		if currentHeight > 0 && o.PostedHeight > 0 && currentHeight > uint64(o.PostedHeight) {
			elapsed := currentHeight - uint64(o.PostedHeight)
			elapsedEpochs := uint32(elapsed / EpochBlocks)
			if elapsedEpochs < o.CdTerm {
				rem := o.CdTerm - elapsedEpochs
				remStr = fmt.Sprintf("%dep", rem)
			} else {
				remStr = "0ep"
			}
		} else if o.TTLBlocks > 0 {
			// Fallback: derive from TTLBlocks
			rem := o.TTLBlocks / EpochBlocks
			if rem > 0 {
				remStr = fmt.Sprintf("%dep", rem)
			}
		}

		// Amount tier label
		amtLabel := CdTierLabel(o.CdAmount)

		// Discount
		disc := CdDiscount(o.CdAmount, o.AskPrice)
		var discStyle lipgloss.Style
		if disc < 0 {
			discStyle = StyleSpread // yellow for discount
		} else {
			discStyle = StyleBull // green for premium
		}
		discStr := discStyle.Render(fmt.Sprintf("%+.1f%%", disc))

		// Interest (from price stats)
		intStr := "—"
		if p, ok := prices[o.CdAmount]; ok && p.EstimatedInterest > 0 {
			intF := float64(p.EstimatedInterest) / 1e7
			intStr = fmt.Sprintf("%.2f", intF)
		}

		// Price in XFG
		priceXfg := float64(o.AskPrice) / 1e7
		priceStr := fmt.Sprintf("%.4f", priceXfg)

		line := fmt.Sprintf("  %-5s  %-4s  %-10s  %-7s  %-8s  %s",
			termStr, remStr, amtLabel, discStr, intStr, priceStr)

		if len([]rune(line)) > width-2 {
			line = string([]rune(line)[:width-2])
		}

		if i == selected {
			rows = append(rows, StyleActiveTab.Render("▸ "+line))
		} else {
			rows = append(rows, "  "+line)
		}
	}

	// Summary
	totalOffers := len(offers)
	totalXfg := uint64(0)
	for _, o := range offers {
		// Check for overflow
		if o.AskPrice > 0 && totalXfg+o.AskPrice < totalXfg {
			totalXfg = ^uint64(0) // cap at max
		} else {
			totalXfg += o.AskPrice
		}
	}
	totalXfgF := float64(totalXfg) / 1e7

	rows = append(rows, sep)
	rows = append(rows, StyleMuted.Render(fmt.Sprintf(
		"  %d offers  %.1f XFG total value", totalOffers, totalXfgF)))
	rows = append(rows, StyleMuted.Render("  ↑↓ select  enter accept  n create"))

	// Pad to height
	for len(rows) < height {
		rows = append(rows, "")
	}
	if len(rows) > height {
		rows = rows[:height]
	}

	return strings.Join(rows, "\n")
}

// RenderCdDetail renders the detail panel for the selected CD offer.
func RenderCdDetail(offer *CdOffer, price *CdPriceStats, width int) string {
	if offer == nil {
		return StyleMuted.Render("  select an offer")
	}
	amtXfg := float64(offer.CdAmount) / 1e7
	askXfg := float64(offer.AskPrice) / 1e7
	disc := CdDiscount(offer.CdAmount, offer.AskPrice)

	seller := offer.MakerPubKey
	if len(seller) > 12 {
		seller = seller[:12] + "..."
	}

	var estInt string
	if price != nil && price.EstimatedInterest > 0 {
		estInt = fmt.Sprintf("~%.4f HEAT", float64(price.EstimatedInterest)/1e7)
	} else {
		estInt = "—"
	}

	var discStr string
	if disc < 0 {
		discStr = StyleSpread.Render(fmt.Sprintf("%+.1f%%", disc))
	} else {
		discStr = StyleBull.Render(fmt.Sprintf("%+.1f%%", disc))
	}

	lines := []string{
		StyleAccent.Render(" SELECTED OFFER"),
		StyleMuted.Render(strings.Repeat("─", width-2)),
		"",
		fmt.Sprintf("  Tier:     %s", CdTierLabel(offer.CdAmount)),
		fmt.Sprintf("  Amount:   %.1f HEAT (%.0f atomic)", amtXfg, float64(offer.CdAmount)),
		fmt.Sprintf("  Term:     %d epochs", offer.CdTerm),
		fmt.Sprintf("  Posted:   block %d", offer.PostedHeight),
		fmt.Sprintf("  Epoch:    %d", offer.CdEpoch),
		fmt.Sprintf("  Est.int:  %s", estInt),
		fmt.Sprintf("  Ask:      %.4f XFG", askXfg),
		fmt.Sprintf("  Disc:     %s", discStr),
		fmt.Sprintf("  Seller:   %s", seller),
		"",
		StyleMuted.Render("  [enter: accept]  [n: create CD]"),
	}

	_ = width
	return strings.Join(lines, "\n")
}
