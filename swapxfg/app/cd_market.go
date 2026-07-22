// swapxfg/app/cd_market.go
package app

import (
	"fmt"
	"strconv"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

// Allowed CD term epochs (1 epoch ≈ 5 days).
var allowedCdEpochs = map[uint32]bool{6: true, 18: true, 36: true, 72: true}

// cdCreateRequestMsg is returned by the CD form when the user submits.
// tui.go handles the actual RPC call.
type cdCreateRequestMsg struct {
	amountXfg float64 // raw XFG amount from form
	termEpochs uint32 // converted from months
}

// cdCreateResultMsg carries the result of a create_cd RPC call.
type cdCreateResultMsg struct {
	resp *CreateCdResponse
	err  error
}

// cdAcceptRequestMsg is returned when the user confirms accepting a CD offer.
type cdAcceptRequestMsg struct {
	offerID string
}

// cdAcceptResultMsg carries the result of an accept_cd RPC call.
type cdAcceptResultMsg struct {
	resp *AcceptCdResponse
	err  error
}

// CdMarketModel is the embedded sub-model for the CD/XFG secondary market tab.
type CdMarketModel struct {
	offers   []CdOffer
	prices   map[uint64]*CdPriceStats // keyed by CD amount tier (atomic units)
	selected int
	scroll   int
	currentHeight uint64 // current blockchain height for remaining-epoch calc

	// Create CD form
	createActive bool
	createAmount string // raw input in XFG
	createTerm   string // raw input in months
	createFocus  int    // 0=amount, 1=term, 2=submit
	createErr    string
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

func (m CdMarketModel) Update(msg tea.Msg) (CdMarketModel, tea.Cmd) {
	if !m.createActive {
		return m, nil
	}

	kmsg, ok := msg.(tea.KeyMsg)
	if !ok {
		return m, nil
	}

	switch kmsg.Type {
	case tea.KeyEscape:
		m.createActive = false
		m.createErr = ""
		return m, nil

	case tea.KeyTab:
		m.createFocus = (m.createFocus + 1) % 3
		return m, nil

	case tea.KeyShiftTab:
		m.createFocus = (m.createFocus + 2) % 3
		return m, nil

	case tea.KeyEnter:
		if m.createFocus == 2 {
			// Validate and submit
			amt, err := strconv.ParseFloat(m.createAmount, 64)
			if err != nil || amt <= 0 {
				m.createErr = "invalid amount"
				return m, nil
			}
			months, err := strconv.Atoi(m.createTerm)
			if err != nil || months <= 0 {
				m.createErr = "invalid term"
				return m, nil
			}
			epochBlocks := uint32(months * 6) // 1 month ≈ 6 epochs (5 days each)
			if !allowedCdEpochs[epochBlocks] {
				m.createErr = fmt.Sprintf("term must be 1, 3, 6, or 12 months (got %d)", months)
				return m, nil
			}
			m.createActive = false
			m.createErr = ""
			return m, func() tea.Msg {
				return cdCreateRequestMsg{amountXfg: amt, termEpochs: epochBlocks}
			}
		}
		m.createFocus = (m.createFocus + 1) % 3
		return m, nil

	case tea.KeyBackspace, tea.KeyDelete:
		switch m.createFocus {
		case 0:
			if len(m.createAmount) > 0 {
				m.createAmount = m.createAmount[:len(m.createAmount)-1]
			}
		case 1:
			if len(m.createTerm) > 0 {
				m.createTerm = m.createTerm[:len(m.createTerm)-1]
			}
		}
		return m, nil
	}

	ch := kmsg.String()
	if len(ch) == 1 {
		switch m.createFocus {
		case 0:
			if (ch >= "0" && ch <= "9") || ch == "." {
				m.createAmount += ch
			}
		case 1:
			if ch >= "0" && ch <= "9" {
				m.createTerm += ch
			}
		}
	}

	return m, nil
}

// RenderCdMarket renders the full CD market tab within the given dimensions.
func RenderCdMarket(m *CdMarketModel, width, height int) string {
	listW := width * 60 / 100
	detailW := width - listW - 3

	if listW < 20 {
		listW = 20
	}
	if detailW < 20 {
		detailW = 20
	}

	ob := RenderCdOrderbook(m.offers, m.prices, m.currentHeight, m.selected, listW, height)
	sel := m.selectedOffer()
	price := m.priceForOffer(sel)
	detail := RenderCdDetail(sel, price, detailW)

	sep := lipgloss.NewStyle().Foreground(ColorMuted).Render(
		strings.Repeat("│\n", height))

	result := lipgloss.JoinHorizontal(lipgloss.Top, ob, sep, detail)

	// Create CD form overlay
	if m.createActive {
		form := renderCdCreateForm(m, 40)
		_, formH := lipgloss.Size(form)
		placeY := (height - formH) / 2
		if placeY < 1 {
			placeY = 1
		}
		placeX := (width - 42) / 2
		if placeX < 0 {
			placeX = 0
		}
		result = overlayAt(result, form, placeX, placeY)
	}

	return result
}

func renderCdCreateForm(m *CdMarketModel, width int) string {
	focusedStyle := lipgloss.NewStyle().Foreground(ColorAccent).Bold(true)

	// Amount
	amtLabel := "amount (XFG)"
	amtVal := m.createAmount
	if m.createFocus == 0 {
		amtLabel = focusedStyle.Render("▶ amount (XFG)")
		amtVal = focusedStyle.Render(m.createAmount + "█")
	}

	// Term
	termLabel := "term (months)"
	termVal := m.createTerm
	if m.createFocus == 1 {
		termLabel = focusedStyle.Render("▶ term (months)")
		termVal = focusedStyle.Render(m.createTerm + "█")
	}

	// Submit
	submitLabel := "[ENTER]"
	if m.createFocus == 2 {
		submitLabel = focusedStyle.Render("▶ [ENTER]")
	}

	var lines []string
	lines = append(lines, StyleAccent.Render(" CREATE CD"))
	lines = append(lines, StyleMuted.Render(strings.Repeat("─", width-2)))
	lines = append(lines, "")
	lines = append(lines, fmt.Sprintf("  %-18s  %s", amtLabel, amtVal))
	lines = append(lines, fmt.Sprintf("  %-18s  %s", termLabel, termVal))
	lines = append(lines, "")
	lines = append(lines, "  "+submitLabel)

	if m.createErr != "" {
		lines = append(lines, "")
		lines = append(lines, StyleBear.Render("  ⚠ "+m.createErr))
	}

	lines = append(lines, "")
	lines = append(lines, StyleMuted.Render("  Tab: navigate  Enter: submit  Esc: cancel"))

	content := strings.Join(lines, "\n")
	border := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorAccent).
		Width(width).
		Padding(0, 1)

	return border.Render(content)
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
