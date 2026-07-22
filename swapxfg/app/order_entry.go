// swapxfg/app/order_entry.go
package app

import (
	"fmt"
	"strconv"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

const (
	minOrderXfg    = 10  // minimum order size in XFG
	minOrderAtomic = uint64(minOrderXfg * 1e7)
)

type orderEntryModel struct {
	active bool

	// Fields
	side   uint8  // 0=bid, 1=ask
	pair   uint8  // 0=sol,1=eth,2=xmr,3=bch,4=arb,5=base
	price  string // raw string input
	amount string // raw string input in XFG
	ttl    string // raw string input in blocks

	focus int // 0=side,1=price,2=amount,3=ttl,4=submit

	// Validation
	balance uint64 // available XFG balance in atomic units (0 = no wallet)
}

func newOrderEntryModel() orderEntryModel {
	return orderEntryModel{
		side: 0,
		pair: 0,
		focus: 0,
	}
}

func (m *orderEntryModel) open(pair uint8, balance uint64) {
	m.active = true
	m.pair = pair
	m.side = 0
	m.price = ""
	m.amount = ""
	m.ttl = "1440"
	m.focus = 1
	m.balance = balance
}

func (m *orderEntryModel) close() {
	m.active = false
}

func (m orderEntryModel) Update(msg tea.Msg) (orderEntryModel, tea.Cmd) {
	if !m.active {
		return m, nil
	}

	kmsg, ok := msg.(tea.KeyMsg)
	if !ok {
		return m, nil
	}

	switch kmsg.Type {
	case tea.KeyEscape:
		m.close()
		return m, nil

	case tea.KeyTab:
		m.focus = (m.focus + 1) % 5
		return m, nil

	case tea.KeyShiftTab:
		m.focus = (m.focus + 4) % 5
		return m, nil

	case tea.KeyEnter:
		if m.focus == 4 {
			// Submit handled in tui.go via placeorder command
			return m, nil
		}
		m.focus = (m.focus + 1) % 5
		return m, nil

	case tea.KeyBackspace, tea.KeyDelete:
		switch m.focus {
		case 1:
			if len(m.price) > 0 {
				m.price = m.price[:len(m.price)-1]
			}
		case 2:
			if len(m.amount) > 0 {
				m.amount = m.amount[:len(m.amount)-1]
			}
		case 3:
			if len(m.ttl) > 0 {
				m.ttl = m.ttl[:len(m.ttl)-1]
			}
		}
		return m, nil

	case tea.KeySpace:
		if m.focus == 0 {
			m.side = 1 - m.side // toggle bid/ask
		}
		return m, nil
	}

	// Character input for text fields
	ch := kmsg.String()
	if len(ch) == 1 {
		switch m.focus {
		case 0:
			if ch == "b" || ch == "B" {
				m.side = 0
			} else if ch == "s" || ch == "S" || ch == "a" || ch == "A" {
				m.side = 1
			} else if ch == "p" || ch == "P" {
				m.pair = nextSwapPair(m.pair)
			}
		case 1:
			if isPriceChar(ch) {
				if ch == "." && strings.Contains(m.price, ".") {
					// only one dot allowed
				} else {
					m.price += ch
				}
			}
		case 2:
			if isAmountChar(ch) {
				if ch == "." && strings.Contains(m.amount, ".") {
					// only one dot allowed
				} else {
					m.amount += ch
				}
			}
		case 3:
			if ch >= "0" && ch <= "9" {
				m.ttl += ch
			}
		}
	}

	return m, nil
}

func isPriceChar(ch string) bool {
	return (ch >= "0" && ch <= "9") || ch == "."
}

func isAmountChar(ch string) bool {
	return (ch >= "0" && ch <= "9") || ch == "."
}

func (m orderEntryModel) ParsePrice() (uint64, bool) {
	if m.price == "" {
		return 0, false
	}
	f, err := strconv.ParseFloat(m.price, 64)
	if err != nil || f <= 0 {
		return 0, false
	}
	return uint64(f * priceDivisor), true
}

func (m orderEntryModel) ParseAmount() (uint64, bool) {
	if m.amount == "" {
		return 0, false
	}
	f, err := strconv.ParseFloat(m.amount, 64)
	if err != nil || f <= 0 {
		return 0, false
	}
	return uint64(f * priceDivisor), true
}

func (m orderEntryModel) ParseTTL() uint32 {
	if m.ttl == "" {
		return 1440
	}
	n, err := strconv.Atoi(m.ttl)
	if err != nil || n <= 0 {
		return 1440
	}
	return uint32(n)
}

// ValidateAmount checks if the entered amount is valid:
// - must be a valid number > 0
// - must be >= minimum order size (10 XFG)
// - must not exceed wallet balance (if wallet connected)
func (m orderEntryModel) ValidateAmount() (ok bool, msg string) {
	amt, amtOk := m.ParseAmount()
	if !amtOk || amt == 0 {
		return false, ""
	}

	if amt < minOrderAtomic {
		return false, fmt.Sprintf("below minimum (%d XFG)", minOrderXfg)
	}

	if m.balance > 0 && amt > m.balance {
		balXfg := float64(m.balance) / priceDivisor
		return false, fmt.Sprintf("exceeds balance (%.2f XFG)", balXfg)
	}

	return true, ""
}

func (m orderEntryModel) Render(width int) string {
	if !m.active {
		return ""
	}

	sideStr := "BID"
	sideStyle := StyleBull
	if m.side == 1 {
		sideStr = "ASK"
		sideStyle = StyleBear
	}

	focusedStyle := lipgloss.NewStyle().Bold(true).Foreground(ColorActiveTab)
	normalStyle := StyleMuted

	var fields []string

	// Side
	sideLabel := "side"
	sideVal := sideStyle.Render(sideStr + " [space]")
	if m.focus == 0 {
		sideLabel = focusedStyle.Render("▶ side")
	}
	fields = append(fields, fmt.Sprintf("  %-6s  %s", sideLabel, sideVal))

	// Pair
	pairName := PairShort(m.pair)
	fields = append(fields, fmt.Sprintf("  %-6s  %s", normalStyle.Render("pair"), normalStyle.Render(pairName)))

	// Price
	priceVal := m.price
	if priceVal == "" {
		priceVal = "_"
	}
	priceLabel := "price"
	if m.focus == 1 {
		priceLabel = focusedStyle.Render("▶ price")
		priceVal = focusedStyle.Render(priceVal + "█")
	}
	fields = append(fields, fmt.Sprintf("  %-6s  %s", priceLabel, priceVal))

	// Amount
	amtVal := m.amount
	if amtVal == "" {
		amtVal = "_"
	}
	amtLabel := "amount"
	if m.focus == 2 {
		amtLabel = focusedStyle.Render("▶ amount")
		amtVal = focusedStyle.Render(amtVal + "█")
	}
	fields = append(fields, fmt.Sprintf("  %-6s  %s XFG", amtLabel, amtVal))

	// TTL
	ttlVal := m.ttl
	if ttlVal == "" {
		ttlVal = "1440"
	}
	ttlLabel := "ttl"
	if m.focus == 3 {
		ttlLabel = focusedStyle.Render("▶ ttl")
		ttlVal = focusedStyle.Render(ttlVal + "█")
	}
	fields = append(fields, fmt.Sprintf("  %-6s  %s blocks", ttlLabel, ttlVal))

	// Submit
	submitLabel := "[ENTER]"
	if m.focus == 4 {
		submitLabel = focusedStyle.Render("▶ [ENTER]")
	}
	fields = append(fields, "  "+submitLabel)

	// Validate
	_, priceOk := m.ParsePrice()
	amt, amtOk := m.ParseAmount()
	var lines []string

	if priceOk && amtOk {
		totalXfg := float64(amt) / priceDivisor
		lines = append(lines, StyleMuted.Render(fmt.Sprintf("  total: %.4f XFG @ %s per XFG", totalXfg, m.price)))
	}

	// Amount validation warning
	if amtOk {
		_, amtMsg := m.ValidateAmount()
		if amtMsg != "" {
			lines = append(lines, StyleBear.Render("  ⚠ "+amtMsg))
		}
	}

	lines = append(lines, StyleMuted.Render("  Tab/ShiftTab: navigate | Space: toggle side | p: pair | Enter: submit | Esc: cancel"))
	fields = append(fields, strings.Join(lines, "\n"))

	pairLabel := PairShort(m.pair)
	hdr := lipgloss.NewStyle().Bold(true).Foreground(ColorActiveTab).
		Width(width).Align(lipgloss.Center).Render(fmt.Sprintf("PLACE ORDER  %s", pairLabel))
	sep := lipgloss.NewStyle().Foreground(ColorMuted).
		Width(width).Align(lipgloss.Center).Render(strings.Repeat("─", max(width-2, 1)))

	content := lipgloss.JoinVertical(lipgloss.Left, fields...)
	box := lipgloss.JoinVertical(lipgloss.Left, hdr, sep, content)

	return box
}
