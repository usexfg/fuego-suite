// swapxfg/app/order_entry.go
// P2P limit-order entry form for the swapxfg TUI.
package app

import (
	"fmt"
	"strconv"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

// Side values match SwapOrder::Side in SwapOfferRelay.h (BID=0, ASK=1).
const (
	OrderSideBid uint8 = 0
	OrderSideAsk uint8 = 1
)

type orderEntryModel struct {
	active   bool
	side     uint8 // OrderSideBid or OrderSideAsk
	pair     uint8
	fields   [3]string // price, amount, ttl
	focusIdx int       // 0=price, 1=amount, 2=ttl
}

var orderEntryFieldLabels = [3]string{"Price", "Amount", "TTL (blocks)"}

func (m *orderEntryModel) open(pair uint8) {
	*m = orderEntryModel{
		active:   true,
		side:     OrderSideAsk,
		pair:     pair,
		fields:   [3]string{"", "", "8640"},
		focusIdx: 0,
	}
}

func (m orderEntryModel) Update(msg tea.Msg) (orderEntryModel, tea.Cmd) {
	if !m.active {
		return m, nil
	}
	switch msg := msg.(type) {
	case tea.KeyMsg:
		k := msg.String()
		switch k {
		case "tab":
			m.focusIdx = (m.focusIdx + 1) % 3
		case "shift+tab":
			m.focusIdx = (m.focusIdx + 2) % 3
		case "up":
			m.focusIdx = (m.focusIdx + 2) % 3
		case "down":
			m.focusIdx = (m.focusIdx + 1) % 3
		case "b", "B":
			m.side = OrderSideBid
		case "a", "A":
			m.side = OrderSideAsk
		case " ":
			if m.side == OrderSideBid {
				m.side = OrderSideAsk
			} else {
				m.side = OrderSideBid
			}
		case "esc", "ctrl+c":
			m.active = false
		case "enter":
			// Parent (tuiModel) handles Enter → executePlaceOrder.
			return m, nil
		case "backspace":
			if m.focusIdx < 3 && len(m.fields[m.focusIdx]) > 0 {
				m.fields[m.focusIdx] = m.fields[m.focusIdx][:len(m.fields[m.focusIdx])-1]
			}
		default:
			if len(k) == 1 && m.focusIdx < 3 {
				// Allow digits, '.', and digits-only for ttl field.
				ch := k[0]
				if m.focusIdx == 2 { // TTL — integers only
					if ch >= '0' && ch <= '9' {
						m.fields[m.focusIdx] += k
					}
				} else if (ch >= '0' && ch <= '9') || ch == '.' {
					m.fields[m.focusIdx] += k
				}
			}
		}
	}
	return m, nil
}

// Render draws the centered order-entry form at the given width.
func (m orderEntryModel) Render(width int) string {
	if !m.active {
		return ""
	}
	if width < 28 {
		width = 28
	}

	sideLabel := "ASK"
	sideStyle := StyleBear
	if m.side == OrderSideBid {
		sideLabel = "BID"
		sideStyle = StyleBull
	}

	title := StyleAccent.Render(fmt.Sprintf(" Place Order  %s  %s ",
		sideStyle.Render(sideLabel), PairShort(m.pair)))

	var lines []string
	lines = append(lines, title)
	lines = append(lines, StyleMuted.Render(strings.Repeat("─", width-2)))
	lines = append(lines, StyleMuted.Render("  [a]sk  [b]id  [space] toggle  [tab] field  [esc] cancel"))

	for i, label := range orderEntryFieldLabels {
		prefix := "  "
		if i == m.focusIdx {
			prefix = StyleAccent.Render("> ")
		}
		val := m.fields[i]
		cursor := ""
		if i == m.focusIdx {
			cursor = "█"
		}
		line := fmt.Sprintf("%s%-12s %s%s", prefix, label+":", val, cursor)
		if i == m.focusIdx {
			lines = append(lines, StyleInput.Render(line))
		} else {
			lines = append(lines, StyleMuted.Render(line))
		}
	}
	lines = append(lines, StyleMuted.Render(strings.Repeat("─", width-2)))
	lines = append(lines, StyleMuted.Render("  enter = submit"))

	body := lipgloss.JoinVertical(lipgloss.Left, lines...)
	return lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorAccent).
		Width(width).
		Padding(0, 1).
		Render(body)
}

// ParsePrice converts the price field to rate-num (XFG per CTR × 1e7).
func (m orderEntryModel) ParsePrice() (uint64, bool) {
	s := strings.TrimSpace(m.fields[0])
	if s == "" {
		return 0, false
	}
	// Integer atomic units accepted as-is; decimals scaled by 1e7.
	if !strings.Contains(s, ".") {
		v, err := strconv.ParseUint(s, 10, 64)
		if err != nil || v == 0 {
			return 0, false
		}
		// Heuristic: values < 1e6 treated as human decimal → scale.
		if v < 1_000_000 {
			return v * 10_000_000, true
		}
		return v, true
	}
	atomic, err := parseAmountAtomic(s, 1e7)
	if err != nil || atomic == 0 {
		return 0, false
	}
	return atomic, true
}

// ParseAmount converts the amount field to XFG atomic units (1e7).
func (m orderEntryModel) ParseAmount() (uint64, bool) {
	s := strings.TrimSpace(m.fields[1])
	if s == "" {
		return 0, false
	}
	atomic, err := parseAmountAtomic(s, 1e7)
	if err != nil || atomic == 0 {
		return 0, false
	}
	return atomic, true
}

// ParseTTL returns the TTL in blocks (default 8640 ≈ 1 day at 10s blocks).
func (m orderEntryModel) ParseTTL() uint32 {
	s := strings.TrimSpace(m.fields[2])
	if s == "" {
		return 8640
	}
	v, err := strconv.ParseUint(s, 10, 32)
	if err != nil || v == 0 {
		return 8640
	}
	return uint32(v)
}
