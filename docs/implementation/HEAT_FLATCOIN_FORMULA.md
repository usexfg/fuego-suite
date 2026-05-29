# HEAT Flatcoin Formula

## Concept

HEAT is upgraded from a **stablecoin** (constant nominal USD) to a **flatcoin** (constant purchasing power). The peg target rises as USD loses purchasing power, so 1 HEAT preserves real value across time.

| | Stablecoin (previous target) | Flatcoin (new target) |
|---|---|---|
| Today | 1 HEAT = $1.00 | 1 HEAT = $1.00 |
| In 10 years (30% cumulative CPI) | 1 HEAT = $1.00 nominal *(–30% real)* | 1 HEAT = $1.30 nominal *(0% real)* |
| What pegs | USD nominal | USD purchasing power |
| Holder real return | –CPI/year | 0%/year |

The result is the first **private inflation-protected savings instrument** on-chain: a TIPS-equivalent with native privacy.

---

## Formula

### Previous (stablecoin)

```
target_ratio = LAUNCH_RATIO × LAUNCH_TWAP / current_TWAP
```

### New (flatcoin)

```
target_ratio = LAUNCH_RATIO × LAUNCH_TWAP / current_TWAP × (current_CPI / LAUNCH_CPI)
                                                            └─────── CPI multiplier ───────┘
```

### Variable reference

| Variable | Source | Notes |
|---|---|---|
| `LAUNCH_RATIO` | Hardcoded `1/5 = 0.2` | XFG per HEAT at launch |
| `LAUNCH_TWAP` | One-time snapshot at first epoch | Captures XFG price at launch |
| `current_TWAP` | Rolling 1-epoch TWAP from swapxfg | Updates every ~5 days |
| `LAUNCH_CPI` | One-time snapshot at first epoch | Same epoch as LAUNCH_TWAP |
| `current_CPI` | Median across CPI sources (see below) | Updates daily |
| `target_ratio` | Result — XFG per 1 HEAT | The PI controller target |

The CPI multiplier is **multiplicative on top of the existing stability mechanic**. XFG price tracking still works exactly as before. Adding CPI is a pure superset.

---

## Worked examples

| Scenario | XFG price | current_TWAP | CPI mult | target_ratio | XFG to mint 1 HEAT | 1 HEAT in USD |
|---|---|---|---|---|---|---|
| Launch | $5 | $5 | 1.00 | 0.200 | 0.200 XFG | **$1.00** |
| 1y, XFG flat, CPI +3% | $5 | $5 | 1.030 | 0.206 | 0.206 XFG | **$1.03** |
| 10y, XFG flat, CPI +30% | $5 | $5 | 1.300 | 0.260 | 0.260 XFG | **$1.30** |
| 10y, XFG $25, CPI +30% | $25 | $25 | 1.300 | 0.052 | 0.052 XFG | **$1.30** |
| 10y, XFG $0.50, CPI +30% | $0.50 | $0.50 | 1.300 | 2.600 | 2.600 XFG | **$1.30** |
| Deflation –5% scenario | $5 | $5 | 0.950 | 0.190 | 0.190 XFG | **$0.95** |

HEAT preserves purchasing power across **any** combination of XFG market moves and CPI changes.

---

## Code change

`CryptoNoteConfig.h` — add launch constants:

```cpp
const uint64_t HEAT_LAUNCH_RATIO_NUM   = 1;
const uint64_t HEAT_LAUNCH_RATIO_DENOM = 5;     // 0.2 XFG/HEAT at launch
const uint64_t HEAT_CPI_SCALE          = 10000; // CPI stored as int × 10000
```

`Blockchain.h` — add state:

```cpp
uint64_t m_heatLaunchTwap = 0;
bool     m_heatLaunchTwapSet = false;
uint64_t m_heatLaunchCpi  = 0;
bool     m_heatLaunchCpiSet  = false;
uint64_t m_heatCurrentCpi = 0;  // updated each epoch from oracle median
```

`Blockchain.cpp` — each epoch:

```cpp
// Record launch snapshots once
if (!m_heatLaunchTwapSet && m_twapBlockCount > 0) {
    m_heatLaunchTwap = m_currentTwapSnapshot;
    m_heatLaunchTwapSet = true;
}
if (!m_heatLaunchCpiSet && m_heatCurrentCpi > 0) {
    m_heatLaunchCpi = m_heatCurrentCpi;
    m_heatLaunchCpiSet = true;
}

// Compute CPI-aware target ratio
// target = LAUNCH_RATIO × LAUNCH_TWAP × current_CPI
//        / (LAUNCH_RATIO_DENOM × current_TWAP × LAUNCH_CPI)
uint64_t targetNum = HEAT_LAUNCH_RATIO_NUM   * m_heatLaunchTwap * m_heatCurrentCpi;
uint64_t targetDen = HEAT_LAUNCH_RATIO_DENOM * m_currentTwap    * m_heatLaunchCpi;
FixedPoint64 target = FixedPoint64::fromRatio(targetNum, targetDen);

m_piController.calculate(target, m_heatRedemptionPrice, m_heatRedemptionRate);
```

The PI controller, band, and existing swap mechanics are untouched.

---

## CPI Data Source Architecture

`current_CPI` is the median across multiple independent sources. No single source can move HEAT's peg.

### Source ranking

| Tier | Source | Cadence | Notes |
|---|---|---|---|
| **Primary** | Truflation | Daily | Decentralized index aggregating 30+ providers, 18M+ data points |
| **Anchor** | BLS CPI-U | Monthly | Authoritative US government feed |
| **Sanity** | Pyth commodity basket | Sub-second | Gold + Silver + Oil + FX, independent of CPI agencies |
| **Market** | FPI (Frax Price Index) | Real-time | Market-derived consensus on CPI value |

### Composite computation

```cpp
uint64_t Blockchain::getCurrentCpi() {
  std::vector<double> samples;
  time_t now = std::time(nullptr);
  std::lock_guard<std::mutex> lock(m_cpiMutex);

  if (auto t = m_truflationCache; (now - t.updatedAt) < 86400)
    samples.push_back(t.value);
  if (auto b = m_blsCache;        (now - b.updatedAt) < 86400 * 35)
    samples.push_back(b.value);
  if (auto p = m_pythBasketCache; (now - p.updatedAt) < 300)
    samples.push_back(normalizeBasketToCpi(p.value));
  if (auto f = m_fpiCache;        (now - f.updatedAt) < 3600)
    samples.push_back(f.value * m_heatLaunchCpi);

  if (samples.empty())
    return m_heatLaunchCpi;  // safest fallback — peg behaves as stablecoin

  std::sort(samples.begin(), samples.end());
  return static_cast<uint64_t>(samples[samples.size() / 2] * HEAT_CPI_SCALE);
}
```

### Fetcher threads

Pattern mirrors the existing `exbitronFetchThread()` in `SwapOfferRelay`:

```
truflationFetchThread()     → poll api.truflation.com hourly
blsFetchThread()            → poll api.bls.gov daily
pythCommodityBasketThread() → poll hermes.pyth.network every 60s
fpiFetchThread()            → query FPI/USDC TWAP from Uniswap V3 hourly
```

Each pushes into a per-source cache. `getCurrentCpi()` takes the median.

---

## Manipulation defenses

| Defense | Mechanism |
|---|---|
| **Multi-source median** | ≥ (N/2)+1 sources must agree to move CPI |
| **Per-epoch change cap** | CPI step limited to ±1% per epoch (CPI never moves that fast in reality) |
| **Confidence-weighted Pyth** | Pyth feeds with wide confidence intervals get down-weighted |
| **Divergence alerting** | If sources diverge >10%, log warning + use most-recent value |
| **PI band on stale** | If all sources stale > 7 days, tighten PI band to last known good CPI |
| **Hardcoded floor/ceiling** | CPI multiplier clamped to [0.5, 5.0] across the asset's life |

---

## Economic implications

1. **Counter-cyclical supply during inflation.** When CPI rises, fewer HEAT are minted per XFG burned. HEAT issuance naturally slows during inflationary periods — opposite of fiat behavior.

2. **Redemption is symmetric.** Burning HEAT to recover XFG returns proportionally more XFG when CPI is high. Redeemers are protected from inflation between mint and burn.

3. **Real-return CDs.** A 5–15% APR CD denominated in HEAT delivers that yield as **real** return regardless of USD inflation. This is categorically better than CEX yield products (which lose 3–8% real value per year to inflation).

4. **Pro-XFG holder dynamics over time.** As USD debases, fewer HEAT mint per XFG. XFG becomes "worth more HEAT" — long-term holders of XFG benefit from secular CPI growth.

5. **PI controller stability preserved.** CPI is slow-moving (~3%/year vs swap-TWAP volatility of ±20%). Adding CPI doesn't destabilize the controller.

---

## Comparison to existing flatcoins

| Project | Peg type | Mechanism | Privacy | Decentralization |
|---|---|---|---|---|
| **AMPL** (Ampleforth) | CPI | Daily wallet-balance rebase | None | High |
| **FPI** (Frax Price Index) | CPI | Algorithmic mint vs FRAX | None | Medium |
| **VOLT** (Volt Protocol) | CPI | Overcollateralized ETH/LUSD | None | Medium |
| **SPOT** (Ampleforth) | CPI | AMPL perpetual bonds | None | High |
| **nuonUSD** (Nuon) | CPI | Flexible peg + collateral | None | Medium |
| **HEAT (flatcoin)** | **CPI** | **PI controller × CPI mult on swapxfg TWAP** | **Native** | **High** |

Fuego's edge:

- **Only flatcoin with chain-native privacy** (CryptoNote ring signatures, stealth addresses)
- **No overcollateralization** — algorithmic via the existing PI controller
- **No rebases** — UX is "wallet balance never changes, but real value never falls"
- **Existing CD/HEAT mint flow** just gets a CPI scalar — no protocol redesign

This is a new product category: **private inflation-protected savings**.

---

## Implementation roadmap

| Phase | Work | Time |
|---|---|---|
| **1.** Formula change | Add CPI multiplier to `target_ratio` in `Blockchain.cpp` (1 line) + state snapshots | 1–2 days |
| **2.** Truflation integration | `truflationFetchThread()` polling Hermes API, cache + freshness check | 3–4 days |
| **3.** Pyth basket sanity | `pythCommodityBasketThread()` + basket-to-CPI normalizer | 2–3 days |
| **4.** BLS anchor | `blsFetchThread()` polling BLS API daily, monthly true-up | 2 days |
| **5.** Defenses | Median selection, change-rate cap, divergence alerting, band tightening | 2–3 days |
| **6.** Branding | Position as "first private flatcoin", update docs/UI | parallel |

**Total: ~2 weeks of engineering work.**

---

## Migration / backward compatibility

The formula change is backward-compatible:

- Before launch: `current_CPI / LAUNCH_CPI = 1.0` (multiplier is no-op) — HEAT behaves as stablecoin
- At first valid CPI reading: snapshot becomes `LAUNCH_CPI` — multiplier remains 1.0
- After first epoch with CPI movement: multiplier engages naturally

No fork, no migration, no holder action required. Existing HEAT holders' purchasing power is preserved going forward but **does not retroactively gain** the past CPI lag.

For retroactive credit, a separate mechanism (e.g. one-time "rebase mint" using historical CPI data at launch) would be needed. Recommended path: do **not** retroactively credit — clean break, flatcoin starts protecting from the activation block forward.

---

## Naming candidates

| Name | Pitch |
|---|---|
| **HEAT** (unchanged) | Lean into existing brand, market it as "real HEAT" |
| **FlatHEAT** | Categorical clarity — instantly identifies as flatcoin |
| **HEAT Real** | Emphasizes real (vs nominal) peg |
| **PowerHEAT** | Purchasing-power preservation framing |
| **Sound HEAT** | Sound-money / hard-money narrative |

Recommendation: keep the **HEAT** ticker; reposition narrative as "the world's first private flatcoin."

---

## Constants summary

```cpp
// CryptoNoteConfig.h
const uint64_t HEAT_LAUNCH_RATIO_NUM    = 1;
const uint64_t HEAT_LAUNCH_RATIO_DENOM  = 5;
const uint64_t HEAT_CPI_SCALE           = 10000;
const uint64_t HEAT_CPI_MAX_STEP_BPS    = 100;     // 1% max per-epoch CPI change
const uint64_t HEAT_CPI_MULT_FLOOR_BPS  = 5000;    // multiplier ≥ 0.5
const uint64_t HEAT_CPI_MULT_CEIL_BPS   = 50000;   // multiplier ≤ 5.0
const uint64_t HEAT_CPI_STALE_DAYS      = 7;       // tighten band if no fresh CPI
```

That's the whole specification.
