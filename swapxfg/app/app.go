// swapxfg/app/app.go
package app

import (
	"fmt"

	tea "github.com/charmbracelet/bubbletea"
)

// Run is the main entry point. Shows splash then launches the trading TUI.
func Run(cfg Config) error {
	// Splash screen
	if !cfg.NoSplash {
		splash := newSplashModel()
		p := tea.NewProgram(splash, tea.WithAltScreen())
		if _, err := p.Run(); err != nil {
			return fmt.Errorf("splash: %w", err)
		}
	}

	// Bridge server (MetaMask / Phantom)
	var bridge *BridgeServer
	if !cfg.NoBridge {
		b, err := NewBridgeServer(cfg.BridgePort)
		if err == nil {
			bridge = b
			defer bridge.Stop()
		}
		// non-fatal: TUI works without bridge
	}

	// Main trading TUI
	tui := newTuiModel(cfg)
	tui.bridge = bridge
	p := tea.NewProgram(tui, tea.WithAltScreen())
	if _, err := p.Run(); err != nil {
		return fmt.Errorf("tui: %w", err)
	}

	return nil
}
