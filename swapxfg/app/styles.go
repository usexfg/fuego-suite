// swapxfg/app/styles.go
package app

import "github.com/charmbracelet/lipgloss"

// Palette from spec §7
var (
	ColorAccent    = lipgloss.Color("#FF5500") // Fuego orange
	ColorBullish   = lipgloss.Color("#00CC66") // green
	ColorBearish   = lipgloss.Color("#FF3344") // red
	ColorSpread    = lipgloss.Color("#FFAA00") // yellow
	ColorMuted     = lipgloss.Color("#555555") // gray
	ColorActiveTab = lipgloss.Color("#FFFFFF") // white text
	ColorInactive  = lipgloss.Color("#777777")
	ColorOwn       = lipgloss.Color("#00CCCC") // cyan
	ColorEscrow    = lipgloss.Color("#FFDD00") // pulsing yellow (escrow state)
	ColorConnOK    = lipgloss.Color("#00FF00")
	ColorConnLost  = lipgloss.Color("#FF0000")
)

// Reusable styles
var (
	StyleAccent = lipgloss.NewStyle().Foreground(ColorAccent).Bold(true)
	StyleBull   = lipgloss.NewStyle().Foreground(ColorBullish)
	StyleBear   = lipgloss.NewStyle().Foreground(ColorBearish)
	StyleSpread = lipgloss.NewStyle().Foreground(ColorSpread)
	StyleMuted  = lipgloss.NewStyle().Foreground(ColorMuted)
	StyleInput  = lipgloss.NewStyle().Foreground(lipgloss.Color("#FFFFFF")).Bold(true)
	StyleStatus = lipgloss.NewStyle().Foreground(ColorBearish)

	StyleActiveTab = lipgloss.NewStyle().
			Foreground(ColorActiveTab).
			Background(ColorAccent).
			Bold(true).
			Padding(0, 1)

	StyleInactiveTab = lipgloss.NewStyle().
				Foreground(ColorInactive).
				Padding(0, 1)
)

// Fire palette for splash screen (11 stops: near-black -> white-yellow)
var FirePalette = [11]lipgloss.Color{
	"#180400", "#3D0A00", "#6D1400", "#9E2000",
	"#D43800", "#FF5500", "#FF7F11", "#FFA940",
	"#FFD066", "#FFE899", "#FFFADD",
}

var FireChars = [11]rune{' ', '.', ':', '*', '\u2591', '\u2592', '\u2593', '\u2588', '\u2588', '\u2588', '\u2588'}
