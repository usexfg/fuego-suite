// swapxfg/app/chart.go
package app

import (
	"fmt"
	"math"
	"strings"
	"time"

	"github.com/charmbracelet/lipgloss"
)

// Box-drawing chars for candlesticks (vertical orientation)
const (
	wickChar = '│'
	bodyUp   = '┃' // bullish body
	bodyDn   = '┃' // bearish body (colored differently)
)

// RenderChart draws ASCII candlestick chart for the given trades.
func RenderChart(trades []SwapTrade, width, height int) string {
	candles := BucketCandles(trades, 5*time.Minute)
	if len(candles) == 0 {
		placeholder := StyleMuted.Render("  awaiting trades...")
		return lipgloss.Place(width, height, lipgloss.Center, lipgloss.Center, placeholder)
	}

	// Fit candles to width: each candle takes 2 columns (body + gap)
	maxCandles := (width - 8) / 2 // leave room for price scale
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

	chartH := height - 1 // leave 1 row for labels
	if chartH < 3 {
		chartH = 3
	}

	// Map price to row (0 = top = highest price)
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

	// Build grid
	grid := make([][]rune, chartH)
	colors := make([][]lipgloss.Color, chartH)
	for y := range grid {
		grid[y] = make([]rune, len(candles)*2)
		colors[y] = make([]lipgloss.Color, len(candles)*2)
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

		// Draw wick above body
		for y := highRow; y < topBody; y++ {
			grid[y][col] = wickChar
			colors[y][col] = bodyColor
		}
		// Draw body
		for y := topBody; y <= botBody; y++ {
			if bullish {
				grid[y][col] = bodyUp
			} else {
				grid[y][col] = bodyDn
			}
			colors[y][col] = bodyColor
		}
		// Draw wick below body
		for y := botBody + 1; y <= lowRow; y++ {
			grid[y][col] = wickChar
			colors[y][col] = bodyColor
		}
	}

	// Render grid to string
	var lines []string
	scaleW := 7 // price scale width
	for y := 0; y < chartH; y++ {
		var b strings.Builder
		// Price scale on left (show at top, middle, bottom)
		if y == 0 || y == chartH/2 || y == chartH-1 {
			price := hi - float64(y)/float64(chartH-1)*(hi-lo)
			b.WriteString(StyleMuted.Render(fmt.Sprintf("%*.5f", scaleW, price)))
		} else {
			b.WriteString(strings.Repeat(" ", scaleW))
		}
		b.WriteString(" ")

		for x := 0; x < len(candles)*2 && x < width-scaleW-1; x++ {
			ch := grid[y][x]
			if ch == ' ' {
				b.WriteRune(' ')
			} else {
				b.WriteString(lipgloss.NewStyle().Foreground(colors[y][x]).Render(string(ch)))
			}
		}
		lines = append(lines, b.String())
	}

	return strings.Join(lines, "\n")
}
