// swapxfg/app/cd_orderbook.go
package app

import (
	"fmt"
	"sort"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

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

// RenderCdOrderbook renders the CD offer list as a fixed-size panel.
// selected is the highlighted row index (-1 = none).
func RenderCdOrderbook(offers []CdOffer, selected, width, height int) string {
	hdr := StyleMuted.Render(fmt.Sprintf(
		"%-10s %-6s %-6s %-10s %s",
		"AMOUNT", "TERM", "EPOCH", "ASK", "DISC",
	))
	sep := StyleMuted.Render(strings.Repeat("─", width))

	rows := []string{hdr, sep}
	sorted := sortCdOffers(offers)

	maxRows := height - 2
	if maxRows < 1 {
		maxRows = 1
	}

	for i, o := range sorted {
		if i >= maxRows {
			break
		}
		disc := CdDiscount(o.CdAmount, o.AskPrice)
		amtXfg := float64(o.CdAmount) / 1e7
		askXfg := float64(o.AskPrice) / 1e7

		var discStyle lipgloss.Style
		if disc < 0 {
			discStyle = StyleSpread // yellow for discount
		} else {
			discStyle = StyleBull // green for premium
		}

		line := fmt.Sprintf("%-10s %-6s %-6s %-10s %s",
			fmt.Sprintf("%.1f XFG", amtXfg),
			fmt.Sprintf("%dmo", o.CdTerm),
			fmt.Sprintf("ep.%d", o.CdEpoch),
			fmt.Sprintf("%.1f XFG", askXfg),
			discStyle.Render(fmt.Sprintf("%+.1f%%", disc)),
		)

		if i == selected {
			line = lipgloss.NewStyle().
				Foreground(ColorAccent).Bold(true).
				Render("▶ " + line)
		} else {
			line = "  " + line
		}
		rows = append(rows, line)
	}

	if len(sorted) == 0 {
		rows = append(rows, StyleMuted.Render("  no offers"))
	}

	// Pad to height
	for len(rows) < height {
		rows = append(rows, "")
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
		estInt = fmt.Sprintf("~%.4f XFG", float64(price.EstimatedInterest)/1e7)
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
		StyleMuted.Render("SELECTED OFFER"),
		"",
		fmt.Sprintf("  Amount:   %.7f XFG", amtXfg),
		fmt.Sprintf("  Term:     %d months", offer.CdTerm),
		fmt.Sprintf("  Epoch:    %d", offer.CdEpoch),
		fmt.Sprintf("  Est.int:  %s", estInt),
		fmt.Sprintf("  Ask:      %.7f XFG", askXfg),
		fmt.Sprintf("  Disc:     %s", discStr),
		fmt.Sprintf("  Seller:   %s", seller),
		"",
		StyleMuted.Render("  [enter: accept]  [b: bid]"),
	}

	_ = width
	return strings.Join(lines, "\n")
}
