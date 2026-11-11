// swapxfg/app/splash.go
package app

import (
	"math"
	"math/rand"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

const (
	splashFW = 38 // fire simulation width
	splashFH = 10 // fire simulation height

	// Diamond geometry (Fuego logo)
	dMaxHW = 13
	dTopH  = 13
	dBotH  = 7
	dH     = dTopH + dBotH // 20
	dW     = dMaxHW * 2    // 26
	dLap   = 3             // fire rows overlapping diamond bottom
)

// Diamond palette
var (
	dEdge = lipgloss.Color("#FFDD66")
	dSeam = lipgloss.Color("#FF8C00")
	dMid  = lipgloss.Color("#FFA030")
	dGlow = lipgloss.Color("#FFFFCC")
)

var tlB = [3]lipgloss.Color{"#FFD060", "#E8A030", "#CC7818"}
var trB = [3]lipgloss.Color{"#FFE888", "#FFD060", "#FFAA30"}
var blB = [2]lipgloss.Color{"#8B2000", "#A03010"}
var brB = [2]lipgloss.Color{"#CC4400", "#DD5510"}

const splashTitle = `  ███████╗██╗    ██╗ █████╗ ██████╗ ██╗  ██╗██╗   ██╗██████╗
  ██╔════╝██║    ██║██╔══██╗██╔══██╗██║  ██║██║   ██║██╔══██╗
  ███████╗██║ █╗ ██║███████║██████╔╝███████║██║   ██║██████╔╝
  ╚════██║██║███╗██║██╔══██║██╔═══╝ ██╔══██║██║   ██║██╔══██╗
  ███████║╚███╔███╔╝██║  ██║██║     ██║  ██║╚██████╔╝██████╔╝
  ╚══════╝ ╚══╝╚══╝ ╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚═════╝`

type splashModel struct {
	width, height int
	frame         int
	fire          [][]float64
}

type splashTickMsg time.Time

func splashTick() tea.Cmd {
	return tea.Tick(80*time.Millisecond, func(t time.Time) tea.Msg {
		return splashTickMsg(t)
	})
}

func newSplashModel() splashModel {
	f := make([][]float64, splashFH)
	for i := range f {
		f[i] = make([]float64, splashFW)
	}
	return splashModel{fire: f}
}

func (m splashModel) Init() tea.Cmd {
	return splashTick()
}

func (m splashModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		return m, tea.Quit
	case tea.MouseMsg:
		_ = msg
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
	case splashTickMsg:
		m.frame++
		if m.frame > 37 { // ~3 seconds at 80ms
			return m, tea.Quit
		}
		m.stepFire()
		return m, splashTick()
	}
	return m, nil
}

func (m *splashModel) stepFire() {
	cx := float64(splashFW) / 2.0
	for x := 0; x < splashFW; x++ {
		d := math.Abs(float64(x)-cx) / cx
		pk := 1.0 - d*0.4
		m.fire[splashFH-1][x] = pk * (0.6 + rand.Float64()*0.4)
		if splashFH > 1 {
			m.fire[splashFH-2][x] = pk * (0.25 + rand.Float64()*0.45)
		}
	}
	for y := 0; y < splashFH-2; y++ {
		for x := 0; x < splashFW; x++ {
			s := m.fire[y+1][x] * 3.0
			if x > 0 {
				s += m.fire[y+1][x-1]
			} else {
				s += m.fire[y+1][x]
			}
			if x < splashFW-1 {
				s += m.fire[y+1][x+1]
			} else {
				s += m.fire[y+1][x]
			}
			if y+2 < splashFH {
				s += m.fire[y+2][x]
			} else {
				s += m.fire[y+1][x]
			}
			m.fire[y][x] = math.Max(0, s/6.0-0.05-rand.Float64()*0.07)
		}
	}
}

// Diamond rendering (reused from xfg-eth-swap)
func dHW(row int) int {
	if row < dTopH {
		return 1 + (dMaxHW-1)*row/(dTopH-1)
	}
	return dMaxHW - (dMaxHW-1)*(row-dTopH)/(dBotH-1)
}

func dPixel(row, col, frame int) (bool, lipgloss.Color) {
	hw := dHW(row)
	l := dMaxHW - hw
	r := dMaxHW + hw - 1

	if col < l || col > r {
		return false, ""
	}
	if wp := frame % (dH + 8); row == wp && col > l && col < r {
		return true, dGlow
	}
	if col == l || col == r {
		return true, dEdge
	}
	if row == dTopH-1 || row == dTopH {
		return true, dSeam
	}
	if col == dMaxHW-1 || col == dMaxHW {
		return true, dMid
	}
	if row < dTopH {
		b := row * 3 / dTopH
		if b > 2 {
			b = 2
		}
		if col < dMaxHW {
			return true, tlB[b]
		}
		return true, trB[b]
	}
	b := (row - dTopH) * 2 / dBotH
	if b > 1 {
		b = 1
	}
	if col < dMaxHW {
		return true, blB[b]
	}
	return true, brB[b]
}

func (m splashModel) View() string {
	if m.width == 0 {
		return ""
	}

	artW := splashFW
	dOff := (artW - dW) / 2
	fStart := dH - dLap
	totalH := fStart + splashFH

	lines := make([]string, 0, totalH)
	for row := 0; row < totalH; row++ {
		var b strings.Builder
		for col := 0; col < artW; col++ {
			dc := col - dOff
			if row < dH && dc >= 0 && dc < dW {
				if ok, c := dPixel(row, dc, m.frame); ok {
					b.WriteString(lipgloss.NewStyle().Foreground(c).Render("\u2588"))
					continue
				}
			}
			fr := row - fStart
			if fr >= 0 && fr < splashFH && col >= 0 && col < splashFW {
				heat := m.fire[fr][col]
				if heat > 0.04 {
					idx := int(heat * 10)
					if idx > 10 {
						idx = 10
					}
					if FireChars[idx] != ' ' {
						b.WriteString(lipgloss.NewStyle().Foreground(FirePalette[idx]).Render(string(FireChars[idx])))
						continue
					}
				}
			}
			b.WriteString(" ")
		}
		lines = append(lines, b.String())
	}

	art := strings.Join(lines, "\n")
	tS := StyleAccent
	sS := StyleMuted

	content := lipgloss.JoinVertical(lipgloss.Center,
		art,
		"",
		tS.Render(splashTitle),
		"",
		sS.Render("cross-chain atomic swaps  \u00b7  press any key"),
	)

	return lipgloss.Place(m.width, m.height,
		lipgloss.Center, lipgloss.Center,
		content,
	)
}
