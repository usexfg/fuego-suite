// swapxfg/app/candle_test.go
package app

import (
	"testing"
	"time"
)

func TestBucketCandles(t *testing.T) {
	now := time.Now()
	base := uint64(now.Add(-15 * time.Minute).Unix())
	base = (base / 300) * 300 // align to 5-min boundary so all 3 trades land in same bucket

	trades := []SwapTrade{
		{Timestamp: base, Rate: "100.0", XfgAmount: 8_0000000},        // candle 0
		{Timestamp: base + 60, Rate: "105.0", XfgAmount: 80_0000000},  // candle 0
		{Timestamp: base + 120, Rate: "95.0", XfgAmount: 8000000},     // candle 0
		{Timestamp: base + 300, Rate: "110.0", XfgAmount: 8_0000000},  // candle 1
		{Timestamp: base + 600, Rate: "108.0", XfgAmount: 80_0000000}, // candle 2
	}

	candles := BucketCandles(trades, 5*time.Minute)
	if len(candles) < 2 {
		t.Fatalf("expected at least 2 candles, got %d", len(candles))
	}

	c0 := candles[0]
	if c0.Open != 100.0 {
		t.Errorf("candle[0].Open = %f, want 100.0", c0.Open)
	}
	if c0.High != 105.0 {
		t.Errorf("candle[0].High = %f, want 105.0", c0.High)
	}
	if c0.Low != 95.0 {
		t.Errorf("candle[0].Low = %f, want 95.0", c0.Low)
	}
	if c0.Close != 95.0 {
		t.Errorf("candle[0].Close = %f, want 95.0", c0.Close)
	}
	if c0.Volume == 0 {
		t.Error("candle[0].Volume should be > 0")
	}
}

func TestBucketCandlesEmpty(t *testing.T) {
	candles := BucketCandles(nil, 5*time.Minute)
	if len(candles) != 0 {
		t.Errorf("expected 0 candles for nil trades, got %d", len(candles))
	}
}

func TestBucketCandlesSingleTrade(t *testing.T) {
	base := uint64(time.Now().Unix())
	trades := []SwapTrade{
		{Timestamp: base, Rate: "42.5", XfgAmount: 10_0000000},
	}
	candles := BucketCandles(trades, 5*time.Minute)
	if len(candles) != 1 {
		t.Fatalf("expected 1 candle, got %d", len(candles))
	}
	c := candles[0]
	if c.Open != 42.5 || c.High != 42.5 || c.Low != 42.5 || c.Close != 42.5 {
		t.Errorf("single trade candle OHLC should all be 42.5, got O=%f H=%f L=%f C=%f",
			c.Open, c.High, c.Low, c.Close)
	}
	expected := 10.0 // 10_0000000 / 1e7
	if c.Volume != expected {
		t.Errorf("volume = %f, want %f", c.Volume, expected)
	}
}

func TestBucketCandlesGapFilling(t *testing.T) {
	// Two trades 10 minutes apart with 5-min candles => at least 2 candles
	// (bucket math may produce 2 or 3 depending on alignment)
	base := uint64(time.Now().Add(-20 * time.Minute).Unix())
	// Align base to a 300-second boundary so bucket math is predictable
	base = (base / 300) * 300

	trades := []SwapTrade{
		{Timestamp: base, Rate: "50.0", XfgAmount: 5_0000000},
		{Timestamp: base + 600, Rate: "55.0", XfgAmount: 5_0000000}, // 2 buckets later
	}
	candles := BucketCandles(trades, 5*time.Minute)
	if len(candles) != 2 {
		t.Fatalf("expected 2 candles (no gap fill in current impl), got %d", len(candles))
	}
	if candles[0].Close != 50.0 {
		t.Errorf("candle[0].Close = %f, want 50.0", candles[0].Close)
	}
	if candles[1].Open != 55.0 {
		t.Errorf("candle[1].Open = %f, want 55.0", candles[1].Open)
	}
}

func TestBucketCandlesOrdering(t *testing.T) {
	// Feed trades in reverse order; output should still be sorted ascending
	base := uint64(time.Now().Add(-10 * time.Minute).Unix())
	base = (base / 300) * 300

	trades := []SwapTrade{
		{Timestamp: base + 300, Rate: "60.0", XfgAmount: 1_0000000}, // later bucket
		{Timestamp: base, Rate: "50.0", XfgAmount: 1_0000000},       // earlier bucket
	}
	candles := BucketCandles(trades, 5*time.Minute)
	if len(candles) != 2 {
		t.Fatalf("expected 2 candles, got %d", len(candles))
	}
	if candles[0].Time.After(candles[1].Time) {
		t.Error("candles should be sorted ascending by time")
	}
	if candles[0].Open != 50.0 {
		t.Errorf("first candle Open = %f, want 50.0 (earlier trade)", candles[0].Open)
	}
	if candles[1].Open != 60.0 {
		t.Errorf("second candle Open = %f, want 60.0 (later trade)", candles[1].Open)
	}
}
