// swapxfg/app/open_orders.go
package app

import (
	"fmt"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

type openOrdersModel struct {
	active bool
	orders []OpenOrder
	sel    int // selected row index
	err    string
}

func (m *openOrdersModel) open(orders []OpenOrder) {
	m.active = true
	m.orders = orders
	m.sel = 0
	m.err = ""
}

func (m *openOrdersModel) close() {
	m.active = false
	m.orders = nil
	m.err = ""
}

func (m openOrdersModel) Update(msg tea.Msg) (openOrdersModel, tea.Cmd) {
	if !m.active {
		return m, nil
	}

	kmsg, ok := msg.(tea.KeyMsg)
	if !ok {
		return m, nil
	}

	switch kmsg.Type {
	case tea.KeyEscape, tea.KeyRunes:
		if kmsg.Type == tea.KeyRunes && string(kmsg.Runes) == "o" {
			// 'o' also closes
		} else if kmsg.Type == tea.KeyEscape {
			// escape closes
		} else {
			return m, nil
		}
		m.close()
		return m, nil

	case tea.KeyUp:
		if m.sel > 0 {
			m.sel--
		}
		return m, nil

	case tea.KeyDown:
		if m.sel < len(m.orders)-1 {
			m.sel++
		}
		return m, nil

	case tea.KeyEnter:
		// Cancel selected order
		if len(m.orders) == 0 {
			return m, nil
		}
		order := m.orders[m.sel]
		cmd := func() tea.Msg {
			return cancelOrderRequestMsg{orderId: order.OrderId}
		}
		return m, cmd
	}

	return m, nil
}

func (m openOrdersModel) View() string {
	if !m.active {
		return ""
	}

	w := 68
	var lines []string

	// Header
	title := StyleAccent.Render(" YOUR OPEN ORDERS")
	lines = append(lines, title)
	lines = append(lines, StyleMuted.Render(strings.Repeat("─", w-2)))
	lines = append(lines, "")

	if m.err != "" {
		lines = append(lines, StyleBear.Render("  "+m.err))
		lines = append(lines, "")
	}

	if len(m.orders) == 0 {
		lines = append(lines, StyleMuted.Render("  no open orders"))
		lines = append(lines, "")
		lines = append(lines, StyleMuted.Render("  Esc to close"))
		return m.renderBox(lines, w)
	}

	// Column header
	hdr := fmt.Sprintf("  %-8s  %-4s  %-6s  %-12s  %-12s  %-12s  %s",
		"ID", "SIDE", "PAIR", "PRICE", "AMOUNT", "FILLED", "TTL")
	lines = append(lines, StyleMuted.Render(hdr))
	lines = append(lines, StyleMuted.Render(strings.Repeat("─", w-2)))

	for i, o := range m.orders {
		side := strings.ToUpper(o.Side)
		pair := PairShort(o.Pair)

		// TTL countdown
		ttlStr := fmt.Sprintf("%d blk", o.TtlBlocks)

		// Fill percentage
		fillPct := float64(0)
		if o.Amount > 0 {
			fillPct = float64(o.Filled) / float64(o.Amount) * 100
		}
		fillStr := fmt.Sprintf("%d (%.0f%%)", o.Filled, fillPct)

		row := fmt.Sprintf("  %-8s  %-4s  %-6s  %-12d  %-12d  %-12s  %s",
			o.OrderId[:min(8, len(o.OrderId))], side, pair, o.Price, o.Amount, fillStr, ttlStr)

		// Truncate to width
		if len([]rune(row)) > w-4 {
			row = string([]rune(row)[:w-4])
		}

		if i == m.sel {
			lines = append(lines, StyleActiveTab.Render("▸ "+row))
		} else {
			lines = append(lines, "  "+row)
		}
	}

	lines = append(lines, "")
	lines = append(lines, StyleMuted.Render("  ↑↓ select  Enter cancel  Esc close"))

	return m.renderBox(lines, w)
}

func (m openOrdersModel) renderBox(lines []string, w int) string {
	content := strings.Join(lines, "\n")

	border := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorAccent).
		Width(w).
		Padding(0, 1)

	return border.Render(content)
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// tea.Msg types for open orders
type cancelOrderRequestMsg struct {
	orderId string
}
