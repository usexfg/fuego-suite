// swapxfg/app/chart.go
package app

import (
	"fmt"
	"math"
	"strings"
	"time"

	"github.com/charmbracelet/lipgloss"
)

const (
	wickChar = '│'
	bodyUp   = '┃'
	bodyDn   = '┃'
)

// ChartMode toggles between candlestick and line chart.
type ChartMode int

const (
	ChartCandles ChartMode = iota
	ChartLine
)

// Chart timeframes
var timeframes = []time.Duration{
	5 * time.Minute,
	15 * time.Minute,
	1 * time.Hour,
	4 * time.Hour,
	24 * time.Hour,
	7 * 24 * time.Hour,
}

func prevTimeframe(tf time.Duration) time.Duration {
	for i := len(timeframes) - 1; i >= 0; i-- {
		if timeframes[i] < tf {
			return timeframes[i]
		}
	}
	return timeframes[len(timeframes)-1]
}

func nextTimeframe(tf time.Duration) time.Duration {
	for i := 0; i < len(timeframes); i++ {
		if timeframes[i] > tf {
			return timeframes[i]
		}
	}
	return timeframes[0]
}

func timeframeLabel(tf time.Duration) string {
	switch tf {
	case 5 * time.Minute:
		return "5m"
	case 15 * time.Minute:
		return "15m"
	case 1 * time.Hour:
		return "1h"
	case 4 * time.Hour:
		return "4h"
	case 24 * time.Hour:
		return "1d"
	case 7 * 24 * time.Hour:
		return "1w"
	default:
		return "?"
	}
}

// RenderChart draws ASCII candlestick chart for the given trades.
// If daemonCandles is non-nil, those are used directly (better for longer timeframes).
func RenderChart(trades []SwapTrade, width, height int, tf time.Duration, daemonCandles []DaemonCandle) string {
	return renderChartMode(trades, width, height, ChartCandles, tf, daemonCandles)
}

// RenderChartLine draws an ASCII line chart for the given trades.
func RenderChartLine(trades []SwapTrade, width, height int, tf time.Duration, daemonCandles []DaemonCandle) string {
	return renderChartMode(trades, width, height, ChartLine, tf, daemonCandles)
}

func renderChartMode(trades []SwapTrade, width, height int, mode ChartMode, tf time.Duration, daemonCandles []DaemonCandle) string {
	var candles []Candle
	if len(daemonCandles) > 0 {
		candles = convertDaemonCandles(daemonCandles)
	} else {
		candles = BucketCandles(trades, tf)
	}
	if len(candles) == 0 {
		placeholder := StyleMuted.Render("  awaiting trades...")
		return lipgloss.Place(width, height, lipgloss.Center, lipgloss.Center, placeholder)
	}

	// Fit candles to width: each candle takes 2 columns (body + gap)
	scaleW := 9 // price scale width
	chartW := width - scaleW - 2
	maxCandles := chartW / 2
	if maxCandles < 1 {
		maxCandles = 1
	}
	if len(candles) > maxCandles {
		candles = candles[len(candles)-maxCandles:]
	}

	// Find price range
	hi := -math.MaxFloat64
	lo := math.MaxFloat64
	for _, c := range candles {
		if c.High > hi {
			hi = c.High
		}
		if c.Low < lo {
			lo = c.Low
		}
	}
	if hi == lo {
		hi = lo + 1
	}

	// Volume range
	maxVol := 0.0
	for _, c := range candles {
		if c.Volume > maxVol {
			maxVol = c.Volume
		}
	}

	volH := 3 // volume bar area height
	chartH := height - volH - 1
	if chartH < 3 {
		chartH = 3
	}

	priceToRow := func(p float64) int {
		r := int((hi - p) / (hi - lo) * float64(chartH-1))
		if r < 0 {
			r = 0
		}
		if r >= chartH {
			r = chartH - 1
		}
		return r
	}

	numCols := len(candles) * 2

	if mode == ChartLine {
		return renderLineChart(candles, numCols, chartH, volH, hi, lo, maxVol, scaleW, width, priceToRow)
	}
	return renderCandleChart(candles, numCols, chartH, volH, hi, lo, maxVol, scaleW, width, priceToRow)
}

func renderCandleChart(candles []Candle, numCols, chartH, volH int, hi, lo, maxVol float64, scaleW, width int, priceToRow func(float64) int) string {
	// Build grid
	grid := make([][]rune, chartH)
	colors := make([][]lipgloss.Color, chartH)
	for y := range grid {
		grid[y] = make([]rune, numCols)
		colors[y] = make([]lipgloss.Color, numCols)
		for x := range grid[y] {
			grid[y][x] = ' '
		}
	}

	for i, c := range candles {
		col := i * 2
		openRow := priceToRow(c.Open)
		closeRow := priceToRow(c.Close)
		highRow := priceToRow(c.High)
		lowRow := priceToRow(c.Low)

		bullish := c.Close >= c.Open
		var bodyColor lipgloss.Color
		if bullish {
			bodyColor = ColorBullish
		} else {
			bodyColor = ColorBearish
		}

		topBody := openRow
		botBody := closeRow
		if topBody > botBody {
			topBody, botBody = botBody, topBody
		}

		for y := highRow; y < topBody; y++ {
			grid[y][col] = wickChar
			colors[y][col] = bodyColor
		}
		for y := topBody; y <= botBody; y++ {
			if bullish {
				grid[y][col] = bodyUp
			} else {
				grid[y][col] = bodyDn
			}
			colors[y][col] = bodyColor
		}
		for y := botBody + 1; y <= lowRow; y++ {
			grid[y][col] = wickChar
			colors[y][col] = bodyColor
		}
	}

	// Volume bars
	volGrid := make([][]rune, volH)
	volColors := make([][]lipgloss.Color, volH)
	for y := range volGrid {
		volGrid[y] = make([]rune, numCols)
		volColors[y] = make([]lipgloss.Color, numCols)
		for x := range volGrid[y] {
			volGrid[y][x] = ' '
		}
	}
	for i, c := range candles {
		col := i * 2
		if maxVol <= 0 {
			continue
		}
		frac := c.Volume / maxVol
		barRows := int(frac * float64(volH))
		if barRows > volH {
			barRows = volH
		}
		bullish := c.Close >= c.Open
		vc := ColorMuted
		if bullish {
			vc = ColorBullish
		} else {
			vc = ColorBearish
		}
		for y := volH - barRows; y < volH; y++ {
			volGrid[y][col] = '█'
			volColors[y][col] = vc
		}
	}

	return assembleChart(grid, colors, volGrid, volColors, chartH, volH, hi, lo, maxVol, scaleW, width)
}

func renderLineChart(candles []Candle, numCols, chartH, volH int, hi, lo, maxVol float64, scaleW, width int, priceToRow func(float64) int) string {
	// Build price line grid
	grid := make([][]rune, chartH)
	colors := make([][]lipgloss.Color, chartH)
	for y := range grid {
		grid[y] = make([]rune, numCols)
		colors[y] = make([]lipgloss.Color, numCols)
		for x := range grid[y] {
			grid[y][x] = ' '
		}
	}

	// Plot close prices as a connected line
	var prevRow int
	first := true
	for i, c := range candles {
		col := i * 2
		row := priceToRow(c.Close)

		bullish := c.Close >= c.Open
		lineColor := ColorBullish
		if !bullish {
			lineColor = ColorBearish
		}

		if !first {
			// Draw vertical connector between previous and current
			minR := prevRow
			maxR := row
			if minR > maxR {
				minR, maxR = maxR, minR
			}
			for y := minR; y <= maxR; y++ {
				grid[y][col-1] = '│'
				colors[y][col-1] = lineColor
			}
		}

		// Plot the point
		grid[row][col] = '●'
		colors[row][col] = lineColor
		prevRow = row
		first = false
	}

	// Volume bars (same as candle chart)
	volGrid := make([][]rune, volH)
	volColors := make([][]lipgloss.Color, volH)
	for y := range volGrid {
		volGrid[y] = make([]rune, numCols)
		volColors[y] = make([]lipgloss.Color, numCols)
		for x := range volGrid[y] {
			volGrid[y][x] = ' '
		}
	}
	for i, c := range candles {
		col := i * 2
		if maxVol <= 0 {
			continue
		}
		frac := c.Volume / maxVol
		barRows := int(frac * float64(volH))
		if barRows > volH {
			barRows = volH
		}
		bullish := c.Close >= c.Open
		vc := ColorMuted
		if bullish {
			vc = ColorBullish
		} else {
			vc = ColorBearish
		}
		for y := volH - barRows; y < volH; y++ {
			volGrid[y][col] = '█'
			volColors[y][col] = vc
		}
	}

	return assembleChart(grid, colors, volGrid, volColors, chartH, volH, hi, lo, maxVol, scaleW, width)
}

func assembleChart(grid [][]rune, colors [][]lipgloss.Color, volGrid [][]rune, volColors [][]lipgloss.Color, chartH, volH int, hi, lo, maxVol float64, scaleW, width int) string {
	var lines []string

	// Price chart area
	for y := 0; y < chartH; y++ {
		var b strings.Builder
		if y == 0 || y == chartH/2 || y == chartH-1 {
			price := hi - float64(y)/float64(chartH-1)*(hi-lo)
			b.WriteString(StyleMuted.Render(fmt.Sprintf("%*s", scaleW, formatPrice(price))))
		} else {
			b.WriteString(strings.Repeat(" ", scaleW))
		}
		b.WriteString(" ")

		for x := 0; x < len(grid[y]) && x < width-scaleW-1; x++ {
			ch := grid[y][x]
			if ch == ' ' {
				b.WriteRune(' ')
			} else {
				b.WriteString(lipgloss.NewStyle().Foreground(colors[y][x]).Render(string(ch)))
			}
		}
		lines = append(lines, b.String())
	}

	// Separator
	lines = append(lines, StyleMuted.Render(strings.Repeat("─", width)))

	// Volume area
	for y := 0; y < volH; y++ {
		var b strings.Builder
		if y == 0 {
			b.WriteString(StyleMuted.Render(fmt.Sprintf("%*s", scaleW, "vol")))
		} else {
			b.WriteString(strings.Repeat(" ", scaleW))
		}
		b.WriteString(" ")

		for x := 0; x < len(volGrid[y]) && x < width-scaleW-1; x++ {
			ch := volGrid[y][x]
			if ch == ' ' {
				b.WriteRune(' ')
			} else {
				b.WriteString(lipgloss.NewStyle().Foreground(volColors[y][x]).Render(string(ch)))
			}
		}
		lines = append(lines, b.String())
	}

	return strings.Join(lines, "\n")
}

// formatPrice picks appropriate precision for the price display.
func formatPrice(p float64) string {
	if p >= 1000 {
		return fmt.Sprintf("%.0f", p)
	}
	if p >= 1 {
		return fmt.Sprintf("%.4f", p)
	}
	if p >= 0.001 {
		return fmt.Sprintf("%.6f", p)
	}
	return fmt.Sprintf("%.8f", p)
}
