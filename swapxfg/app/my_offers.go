package app

import (
	"fmt"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

type MyOffersModel struct {
	swaps []SwapStatus
	err   error
}

func newMyOffersModel() *MyOffersModel {
	return &MyOffersModel{}
}

func (m *MyOffersModel) Update(swaps []SwapStatus, err error) {
	m.swaps = swaps
	m.err = err
}

func (m *MyOffersModel) View(w, h int) string {
	if m.err != nil {
		return StyleStatus.Render("Error fetching offers: " + m.err.Error())
	}
	if len(m.swaps) == 0 {
		return StyleMuted.Render("No active swaps found.")
	}

	// Header
	header := fmt.Sprintf("%-32s | %-15s | %-10s | %-15s | %-10s", 
		"SWAP ID", "STATE", "PAIR", "XFG AMT", "ROLE")
	divider := strings.Repeat("-", len(header))
	
	var rows []string
	rows = append(rows, header)
	rows = append(rows, divider)

	for _, s := range m.swaps {
		row := fmt.Sprintf("%-32s | %-15s | %-10s | %-15.4f | %-10s", 
			s.SwapID[:min(32, len(s.SwapID))], 
			s.State, 
			PairName(s.Pair), 
			float64(s.XfgAmount)/1e7, 
			s.Role)
		rows = append(rows, row)
	}

	return lipgloss.JoinVertical(lipgloss.Left, rows...)
}
