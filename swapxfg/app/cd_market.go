// swapxfg/app/cd_market.go
package app

import (
	"fmt"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

// CdMarketModel is the embedded sub-model for the CD/XFG secondary market tab.
type CdMarketModel struct {
	offers   []CdOffer
	prices   map[uint64]*CdPriceStats // keyed by CD amount tier (atomic units)
	selected int
	scroll   int
}

func newCdMarketModel() CdMarketModel {
	return CdMarketModel{
		prices: make(map[uint64]*CdPriceStats),
	}
}

// selectedOffer returns the currently highlighted offer, or nil.
func (m *CdMarketModel) selectedOffer() *CdOffer {
	sorted := sortCdOffers(m.offers)
	if m.selected < 0 || m.selected >= len(sorted) {
		return nil
	}
	o := sorted[m.selected]
	return &o
}

// priceForOffer looks up CdPriceStats for the selected offer's amount tier.
func (m *CdMarketModel) priceForOffer(o *CdOffer) *CdPriceStats {
	if o == nil {
		return nil
	}
	p, ok := m.prices[o.CdAmount]
	if !ok {
		return nil
	}
	return p
}

// moveUp scrolls selection up.
func (m *CdMarketModel) moveUp() {
	if m.selected > 0 {
		m.selected--
	}
}

// moveDown scrolls selection down.
func (m *CdMarketModel) moveDown() {
	if m.selected < len(m.offers)-1 {
		m.selected++
	}
}

// RenderCdMarket renders the full CD market tab within the given dimensions.
// It replaces the main area when activePair == PairCD.
func RenderCdMarket(m *CdMarketModel, width, height int) string {
	listW := width * 60 / 100
	detailW := width - listW - 3 // 3 for separator

	if listW < 20 {
		listW = 20
	}
	if detailW < 20 {
		detailW = 20
	}

	ob := RenderCdOrderbook(m.offers, m.selected, listW, height)
	sel := m.selectedOffer()
	price := m.priceForOffer(sel)
	detail := RenderCdDetail(sel, price, detailW)

	sep := lipgloss.NewStyle().Foreground(ColorMuted).Render(
		strings.Repeat("│\n", height))

	return lipgloss.JoinHorizontal(lipgloss.Top, ob, sep, detail)
}

// RenderCdTicker renders the CD market ticker segment for the top bar.
func RenderCdTicker(offers []CdOffer, active bool) string {
	discLabel := "—"
	if len(offers) > 0 {
		var totalDisc float64
		for _, o := range offers {
			totalDisc += CdDiscount(o.CdAmount, o.AskPrice)
		}
		avgDisc := totalDisc / float64(len(offers))
		discLabel = fmt.Sprintf("%+.1f%% avg", avgDisc)
	}

	label := fmt.Sprintf("CD/XFG %s", discLabel)
	if active {
		return StyleActiveTab.Render(" " + label + " ")
	}
	return StyleInactiveTab.Render(label)
}
