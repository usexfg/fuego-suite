// swapxfg/app/trade_history.go
package app

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

type tradeHistoryModel struct {
	active bool
	trades []SwapTrade
	sel    int
 scroll int
	err    string
}

func (m *tradeHistoryModel) open(trades []SwapTrade) {
	m.active = true
	m.trades = trades
	m.sel = 0
	m.scroll = 0
	m.err = ""
}

func (m *tradeHistoryModel) close() {
	m.active = false
	m.trades = nil
	m.err = ""
}

func (m tradeHistoryModel) Update(msg tea.Msg) (tradeHistoryModel, tea.Cmd) {
	if !m.active {
		return m, nil
	}

	kmsg, ok := msg.(tea.KeyMsg)
	if !ok {
		return m, nil
	}

	switch kmsg.Type {
	case tea.KeyEscape, tea.KeyRunes:
		if kmsg.Type == tea.KeyRunes && string(kmsg.Runes) == "h" {
			// 'h' also closes
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
			if m.sel < m.scroll {
				m.scroll = m.sel
			}
		}
		return m, nil

	case tea.KeyDown:
		if m.sel < len(m.trades)-1 {
			m.sel++
			// scroll window: max 20 visible rows
			maxVisible := 20
			if m.sel >= m.scroll+maxVisible {
				m.scroll = m.sel - maxVisible + 1
			}
		}
		return m, nil

	case tea.KeyPgUp:
		m.sel -= 10
		if m.sel < 0 {
			m.sel = 0
		}
		if m.sel < m.scroll {
			m.scroll = m.sel
		}
		return m, nil

	case tea.KeyPgDown:
		m.sel += 10
		if m.sel >= len(m.trades) {
			m.sel = len(m.trades) - 1
		}
		maxVisible := 20
		if m.sel >= m.scroll+maxVisible {
			m.scroll = m.sel - maxVisible + 1
		}
		return m, nil
	}

	return m, nil
}

func (m tradeHistoryModel) View() string {
	if !m.active {
		return ""
	}

	w := 72
	var lines []string

	title := StyleAccent.Render(" TRADE HISTORY")
	lines = append(lines, title)
	lines = append(lines, StyleMuted.Render(strings.Repeat("─", w-2)))
	lines = append(lines, "")

	if m.err != "" {
		lines = append(lines, StyleBear.Render("  "+m.err))
		lines = append(lines, "")
	}

	if len(m.trades) == 0 {
		lines = append(lines, StyleMuted.Render("  no trades recorded"))
		lines = append(lines, "")
		lines = append(lines, StyleMuted.Render("  Esc to close"))
		return m.renderBox(lines, w)
	}

	// Column header
	hdr := fmt.Sprintf("  %-14s  %10s  %8s  %-4s  %s",
		"TIME", "RATE", "XFG", "SIDE", "AGE")
	lines = append(lines, StyleMuted.Render(hdr))
	lines = append(lines, StyleMuted.Render(strings.Repeat("─", w-2)))

	now := time.Now().Unix()
	maxVisible := 20
	end := m.scroll + maxVisible
	if end > len(m.trades) {
		end = len(m.trades)
	}

	for i := m.scroll; i < end; i++ {
		t := m.trades[i]
		rate := t.Rate
		amt := float64(t.XfgAmount) / 1e7

		// Timestamp
		timeStr := "—"
		if t.Timestamp > 0 {
			ts := time.Unix(int64(t.Timestamp), 0)
			timeStr = ts.Format("01-02 15:04:05")
		}

		// Age
		age := "—"
		if t.Timestamp > 0 {
			diff := now - int64(t.Timestamp)
			if diff < 60 {
				age = fmt.Sprintf("%ds", diff)
			} else if diff < 3600 {
				age = fmt.Sprintf("%dm", diff/60)
			} else if diff < 86400 {
				age = fmt.Sprintf("%dh", diff/3600)
			} else {
				age = fmt.Sprintf("%dd", diff/86400)
			}
		}

		// Direction heuristic: compare with previous trade
		dir := "BUY "
		style := StyleBull
		rateF, _ := strconv.ParseFloat(rate, 64)
		if i > 0 {
			prevRate, _ := strconv.ParseFloat(m.trades[i-1].Rate, 64)
			if rateF < prevRate {
				dir = "SELL"
				style = StyleBear
			}
		}

		row := fmt.Sprintf("  %-14s  %10s  %8.1f  %-4s  %s",
			timeStr, rate, amt, dir, age)

		if len([]rune(row)) > w-4 {
			row = string([]rune(row)[:w-4])
		}

		if i == m.sel {
			lines = append(lines, StyleActiveTab.Render("▸ "+row))
		} else {
			lines = append(lines, style.Render("  "+row))
		}
	}

	// Scroll indicator
	if len(m.trades) > maxVisible {
		lines = append(lines, "")
		scrollInfo := fmt.Sprintf("  %d/%d trades  ↑↓ navigate  PgUp/PgDn scroll",
			m.sel+1, len(m.trades))
		lines = append(lines, StyleMuted.Render(scrollInfo))
	}

	lines = append(lines, "")
	lines = append(lines, StyleMuted.Render("  Esc to close"))

	return m.renderBox(lines, w)
}

func (m tradeHistoryModel) renderBox(lines []string, w int) string {
	content := strings.Join(lines, "\n")

	border := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(ColorAccent).
		Width(w).
		Padding(0, 1)

	return border.Render(content)
}
