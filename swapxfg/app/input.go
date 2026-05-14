// swapxfg/app/input.go
package app

import (
	"fmt"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

// RenderInputBar draws the bottom command bar with prompt, balance, and connection info.
// xfgBal is the formatted XFG balance (empty if unavailable).
// bchBal is the formatted BCH balance (empty if BCH not connected).
func RenderInputBar(cmdBuf string, cursorOn bool, xfgBal, bchBal string, daemonAddr string, connected bool, width int) string {
	// Prompt
	cursor := " "
	if cursorOn {
		cursor = "█"
	}
	prompt := StyleInput.Render(fmt.Sprintf("> %s%s", cmdBuf, cursor))

	// Right side: balance(s) + daemon
	var right []string
	if xfgBal != "" {
		right = append(right, StyleBull.Render(fmt.Sprintf("BAL %s XFG", xfgBal)))
	}
	if bchBal != "" {
		right = append(right, StyleBull.Render(bchBal))
	}

	connStyle := lipgloss.NewStyle().Foreground(ColorConnOK)
	connChar := "■"
	if !connected {
		connStyle = lipgloss.NewStyle().Foreground(ColorConnLost)
	}
	// Extract port from daemon address
	port := daemonAddr
	if idx := strings.LastIndex(daemonAddr, ":"); idx >= 0 {
		port = ":" + daemonAddr[idx+1:]
	}
	right = append(right, connStyle.Render(connChar), StyleMuted.Render(port))
	rightStr := strings.Join(right, "  ")

	// Fill middle with spaces
	promptW := lipgloss.Width(prompt)
	rightW := lipgloss.Width(rightStr)
	gap := width - promptW - rightW
	if gap < 1 {
		gap = 1
	}

	return prompt + strings.Repeat(" ", gap) + rightStr
}
