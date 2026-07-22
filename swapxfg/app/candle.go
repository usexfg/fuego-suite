// swapxfg/app/candle.go
package app

import (
	"sort"
	"strconv"
	"time"
)

// SwapTrade is defined in rpc.go

// Candle represents one OHLCV candlestick.
type Candle struct {
	Time   time.Time
	Open   float64
	High   float64
	Low    float64
	Close  float64
	Volume float64 // XFG volume
}

// BucketCandles groups trades into OHLCV candles of the given interval.
// Trades are sorted by timestamp ascending before bucketing.
func BucketCandles(trades []SwapTrade, interval time.Duration) []Candle {
	if len(trades) == 0 {
		return nil
	}

	// Sort by timestamp ascending
	sorted := make([]SwapTrade, len(trades))
	copy(sorted, trades)
	sort.Slice(sorted, func(i, j int) bool {
		return sorted[i].Timestamp < sorted[j].Timestamp
	})

	secs := int64(interval.Seconds())
	if secs <= 0 {
		secs = 300 // default 5 min
	}

	var candles []Candle
	var cur *Candle
	var curBucket int64

	for _, t := range sorted {
		rate, _ := strconv.ParseFloat(t.Rate, 64)
		if rate <= 0 {
			continue
		}
		vol := float64(t.XfgAmount) / 1e7
		bucket := int64(t.Timestamp) / secs

		if cur == nil || bucket != curBucket {
			if cur != nil {
				candles = append(candles, *cur)
			}
			cur = &Candle{
				Time:   time.Unix(bucket*secs, 0),
				Open:   rate,
				High:   rate,
				Low:    rate,
				Close:  rate,
				Volume: vol,
			}
			curBucket = bucket
		} else {
			if rate > cur.High {
				cur.High = rate
			}
			if rate < cur.Low {
				cur.Low = rate
			}
			cur.Close = rate
			cur.Volume += vol
		}
	}
	if cur != nil {
		candles = append(candles, *cur)
	}

	return candles
}

// convertDaemonCandles converts daemon RPC candles to the local Candle type.
func convertDaemonCandles(dc []DaemonCandle) []Candle {
	candles := make([]Candle, len(dc))
	for i, d := range dc {
		candles[i] = Candle{
			Time:   time.Unix(int64(d.Timestamp), 0),
			Open:   d.Open,
			High:   d.High,
			Low:    d.Low,
			Close:  d.Close,
			Volume: d.Volume,
		}
	}
	return candles
}
