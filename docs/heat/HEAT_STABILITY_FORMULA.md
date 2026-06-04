# HEAT Stability Formula

## One constant, everything else is protocol-native

```
target_ratio = LAUNCH_RATIO × LAUNCH_TWAP / current_TWAP
```

## Where

| Variable | Source | Example |
|---|---|---|
| `LAUNCH_RATIO` | Hardcoded constant: `1/5 = 0.2` | When XFG price is stable, 1 XFG mints 5 HEAT |
| `LAUNCH_TWAP` | One-time snapshot of swapxfg TWAP at first epoch | Captures XFG's market price at launch |
| `current_TWAP` | Rolling 1-epoch TWAP of swapxfg execution prices | Updates every ~5 days from real atomic swap data |
| `target_ratio` | Result — XFG per 1 HEAT | The mint rate the PI controller targets |

## What it does

When XFG's market price goes up (relative to launch):
- `current_TWAP` goes up
- `target_ratio` goes down
- Users get **fewer HEAT per XFG** burned
- HEAT maintains the same real value

When XFG's market price goes down:
- `current_TWAP` goes down
- `target_ratio` goes up
- Users get **more HEAT per XFG** burned
- HEAT maintains the same real value

No USD. No oracle. No governance. The ratio adjusts based on the protocol's own swap execution data.

## Band (PI controller constraint)

```
low_band  = target_ratio × 0.50
high_band = target_ratio × 1.50

PI integral is clamped so heat_px stays within [low_band, high_band]
```

The band is a safety net — with a 1-epoch TWAP, it almost never activates because the TWAP tracks fast enough.

## In code

```cpp
// CryptoNoteConfig.h (one constant)
const uint64_t HEAT_LAUNCH_RATIO_NUM = 1;
const uint64_t HEAT_LAUNCH_RATIO_DENOM = 5;  // 0.2 XFG/HEAT at launch

// Blockchain.h (state)
uint64_t m_heatLaunchTwap = 0;
bool     m_heatLaunchTwapSet = false;

// Blockchain.cpp (each epoch)
// Record launch TWAP once
if (!m_heatLaunchTwapSet && m_twapBlockCount > 0) {
    m_heatLaunchTwap = m_currentTwapSnapshot;
    m_heatLaunchTwapSet = true;
}

// Compute target ratio
// target = LAUNCH_RATIO × LAUNCH_TWAP / current_TWAP
uint64_t targetNum = HEAT_LAUNCH_RATIO_NUM * m_heatLaunchTwap;
uint64_t targetDen = HEAT_LAUNCH_RATIO_DENOM * m_currentTwap;
FixedPoint64 target = FixedPoint64::fromRatio(targetNum, targetDen);

// PI controller targets this ratio
m_piController.calculate(target, m_heatRedemptionPrice, m_heatRedemptionRate);
```

## Example

| XFG price | current_TWAP | target_ratio (XFG/HEAT) | XFG to mint 1 HEAT | Result |
|---|---|---|---|---|
| $5 (launch) | $5.00 | 0.20 | 0.20 XFG | HEAT ≈ $1 |
| $25 | $25.00 | 0.04 | 0.04 XFG | HEAT ≈ $1 |
| $0.50 | $0.50 | 2.00 | 2.00 XFG | HEAT ≈ $1 |
| $82 | $82.00 | 0.0122 | 0.0122 XFG | HEAT ≈ $1 |
| $0.01 | $0.01 | 100.00 | 100.00 XFG | HEAT ≈ $1 |

HEAT stays at the same real value regardless of XFG's market price.
