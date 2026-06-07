# Mode 4: Fixed-Peg HEAT — Implementation Plan & Dev Guide

## Overview

Mode 4 replaces HEAT's PI controller, basin discovery, and self-referencing ratio with a single formula:

```cpp
redemption_price = HEAT_PEG / oracle_xfg_price
```

Where `HEAT_PEG = 158` (CPI index, representing $1.58 in Dec 2008 dollars, BLS CPI-U verified). Updated monthly.

This eliminates: PI controller (Kp, Ki, integral, rate clamp, Hill damping), basin discovery (4-phase state machine), self-referencing target (`launchTwap/currentTwap`), protocol rebalancer, CD spend multiplier.

Arbitrage handles peg maintenance. One-way mint (XFG→HEAT) plus AMM pool trading provides
directionally symmetric peg pressure through arbitrage and treasury operations.

Oracle: Exbitron as primary, SwapXFG atomic swap TWAP as fallback. Last-known-good during staleness.

## Files Changed

### 1. `CryptoNoteConfig.h` — Constants

Add:
```cpp
// Mode 4: Fixed-peg flatcoin (replaces PI controller + basin discovery)
const uint8_t  HEAT_STABILITY_MODE = 4;                   // new default
const uint64_t HEAT_PEG_INDEX = 158;                      // $1.58 in Dec 2008 dollars (×100 scale)
const uint64_t HEAT_PEG_SCALE = 100;                      // peg index scale factor
const uint32_t HEAT_CPI_UPDATE_INTERVAL_M4 = 65700;        // ~1 year blocks between CPI updates

// Oracle configuration
const uint32_t MAX_ORACLE_STALE_EPOCHS = 2;               // unchanged — already exists
const uint32_t ORACLE_STALE_FREEZE_EPOCHS = 146;           // 10 epochs = freeze peg if no oracle
const uint32_t ORACLE_FALLBACK_TIER2_MIN_TRADES = 5;      // min SwapXFG trades for Tier 2

// Two-way redemption
const uint64_t HEAT_MINT_PREMIUM_BPS = 500;               // 5% — unchanged
const uint64_t HEAT_BURN_PREMIUM_BPS = 500;                // 5% on HEAT→XFG burn
```

Modify (existing values):
```cpp
// REMOVED constants (no longer needed for Mode 4):
// PI_KP_NUM, PI_KP_DENOM, PI_KI_NUM, PI_KI_DENOM
// PI_BASE_RATE_NUM, PI_BASE_RATE_DENOM, PI_ABS_MAX_RATE, PI_INTEGRAL_CLAMP
// HEAT_PI_USE_DAMP, HEAT_PI_DAMP_M, HEAT_PI_DAMP_N
// BASIN_BOOTSTRAP_EPOCHS, BASIN_OBSERVE_EPOCHS, BASIN_STABLE_REQUIRED
// BASIN_STABILITY_RANGE, BASIN_EXIT_THRESHOLD, BASIN_REBALANCE_MULT
// PROTOCOL_LP_MAX_FRACTION, PROTOCOL_REBALANCE_MAX
// HEAT_CPI_AUTO_INFLATION_BPS, HEAT_CPI_UPDATE_INTERVAL (old)
// HEAT_LAUNCH_RATIO_5X/8X (no longer used for target)
// HEAT_VALUE_FLOOR, HEAT_VALUE_CEILING (no band)
// HEAT_CPI_BASE_FLOOR, HEAT_CPI_BASE_CEIL, HEAT_CPI_SCALE, HEAT_CPI_LAUNCH_INDEX
```

PiController constants can be kept for backward compatibility during hardfork transition. Mark as deprecated:
```cpp
// Mode 4 deprecations: these are only used in old modes (0-2), kept for hardfork transition
const uint64_t HEAT_PEG_INDEX_M4              = 158;   // <- the ONE constant that matters
```

### 2. `Blockchain.h` — State Changes

Add:
```cpp
// Mode 4 state (replaces PiControllerState + basin for peg logic)
uint64_t m_heatPegIndex      = parameters::HEAT_PEG_INDEX;  // 158 = $1.58
uint64_t m_heatPegScale      = parameters::HEAT_PEG_SCALE;  // 100
uint64_t m_lastOraclePrice   = 0;        // last-known-good XFG price × VALUE_SCALE
uint32_t m_lastOracleHeight  = 0;        // block height of last valid oracle
uint32_t m_oracleStaleEpochs = 0;        // consecutive epochs without oracle update
uint64_t m_xfgBurnedLifetime  = 0;       // lifetime XFG→HEAT burns
uint64_t m_heatBurnedLifetime = 0;       // lifetime HEAT→XFG burns
```

Keep but gate behind Mode 4 activation:
```cpp
// Legacy PI state (kept for hardfork, inactive when Mode 4 is active)
CryptoNote::PiControllerState m_piState;   // kept, never updated in Mode 4
```
The `m_piState` is kept for serialization compatibility. It stops receiving updates when Mode 4 engages.

Remove from epoch-level logic (mode-gated):
- `m_piState` updates in epoch handler removed
- Basin phase checks removed
- Protocol rebalancer removed (arb handles this)
- `m_heatLaunchTwap` / `launchTwapSet` no longer used
- CPI multiplier in epoch handler (now replaced by direct peg)

### 3. `Blockchain.cpp` — Epoch Handler Rewrite

#### 3a. Epoch Boundary (lines 3257-3359)

**Remove:**
- computeTargetRatio() call (lines 3294-3301)
- CPI multiplier block (lines 3297-3301 — replaced by direct peg)
- computeNewRedemptionPrice() call (line 3303 — replaced by fixed formula)
- Protocol rebalancer block (lines 3313-3359 — arb handles this)

**Replace with:**
```cpp
// Mode 4 peg: redemption = HEAT_PEG / oracle_price
uint64_t oraclePrice = getReliableOraclePrice(block.height, epochDuration);
if (oraclePrice > 0) {
    FixedPoint64 pegFp = FixedPoint64::fromRatio(m_heatPegIndex, m_heatPegScale);
    FixedPoint64 priceFp = FixedPoint64::fromRatio(oraclePrice, parameters::VALUE_SCALE);
    m_piState.redemptionPrice = pegFp.div(priceFp);  // reuse the state field
    m_lastOraclePrice = oraclePrice;
    m_lastOracleHeight = block.height;
    m_oracleStaleEpochs = 0;
} else {
    // Oracle stale: freeze at last known good
    m_oracleStaleEpochs++;
    // redemptionPrice stays at last value (already set in m_piState)
}
```

#### 3b. New Oracle Selection Function

```cpp
uint64_t Blockchain::getReliableOraclePrice(uint32_t height, uint32_t epochDuration) {
    uint64_t oraclePrice = 0;

    // Tier 1: Exbitron (direct USD, 60s poll)
    if (m_xfgMarketValueHeight > 0) {
        uint32_t epochsSinceUpdate = (height - m_xfgMarketValueHeight) / epochDuration;
        if (epochsSinceUpdate < parameters::MAX_ORACLE_STALE_EPOCHS) {
            oraclePrice = m_xfgMarketValue;
        }
    }

    // Tier 2: SwapXFG atomic swap TWAP (cross-pair triangulation)
    if (oraclePrice == 0 && m_swapRelay != nullptr) {
        PriceSource swapPrice = m_swapRelay->getXfgPriceFromAtomicSwaps(
            parameters::ORACLE_FALLBACK_TIER2_MIN_TRADES);
        if (!swapPrice.stale) {
            oraclePrice = swapPrice.rate * VALUE_SCALE;
        }
    }

    // Tier 3: None — freeze peg at last known
    return oraclePrice;
}
```

#### 3c. Arbitrage in Epoch Handler

After redemption price is set, run arbitrage:
```cpp
// Mode 4 arbitrage: push pool toward peg
if (oraclePrice > 0 && !m_ammPool.isEmpty()) {
    FixedPoint64 pegValue = FixedPoint64::fromRatio(m_heatPegIndex, m_heatPegScale);
    FixedPoint64 xfgPrice = FixedPoint64::fromRatio(oraclePrice, parameters::VALUE_SCALE);
    FixedPoint64 pegRatio = pegValue.div(xfgPrice);  // target pool ratio

    FixedPoint64 currentSpot = FixedPoint64::fromRatio(
        m_ammPool.reserveXfg, m_ammPool.reserveHeat);
    FixedPoint64 deviation = currentSpot.sub(pegRatio).div(pegRatio);

    // Arbitrage threshold: 0.5% deviation triggers intervention
    FixedPoint64 threshold = FixedPoint64::fromRatio(50, 10000); // 0.005

    while (deviation.absolute() > threshold) {
        uint64_t arbAmount = (std::min(m_ammPool.reserveXfg, m_ammPool.reserveHeat)
                             * parameters::HEAT_ARB_MAX_FRACTION) / 100;

        if (currentSpot > pegRatio) {
            // HEAT too expensive (pool ratio > target):
            // -- MINT new HEAT → sell to pool
            FixedPoint64 heatToMint = FixedPoint64::fromUint64(arbAmount)
                .div(m_piState.redemptionPrice);
            uint64_t heatMinted = heatToMint.toUint64();
            if (heatMinted > 0) {
                m_ammPool.reserveHeat += heatMinted;
                uint64_t xfgFromSwap = ammGetOutputAmount(heatMinted,
                    m_ammPool.reserveHeat, m_ammPool.reserveXfg, parameters::HEARTH_FEE_BPS);
                m_ammPool.reserveXfg -= xfgFromSwap;
                m_heatSupply += heatMinted;
                m_xfgBurnedLifetime += heatMinted * m_piState.redemptionPrice.toUint64();
            }
        } else {
            // HEAT too cheap (pool ratio < target):
            // -- BUY HEAT from pool → BURN → create XFG
            uint64_t xfgFromSwap = ammGetOutputAmount(arbAmount,
                m_ammPool.reserveXfg, m_ammPool.reserveHeat, parameters::HEARTH_FEE_BPS);
            if (xfgFromSwap > 0 && xfgFromSwap < m_ammPool.reserveXfg) {
                m_ammPool.reserveXfg += arbAmount;
                m_ammPool.reserveHeat -= xfgFromSwap;
                m_heatSupply -= xfgFromSwap;
                m_heatBurnedLifetime += xfgFromSwap;
                // XFG is created back into the pool (no reserve needed)
            }
        }

        // Recompute
        currentSpot = FixedPoint64::fromRatio(
            m_ammPool.reserveXfg, m_ammPool.reserveHeat);
        deviation = currentSpot.sub(pegRatio).div(pegRatio);
    }
}
```

#### 3d. CD Yield Processing (lines 3920-4029)

**Line 4017-4027 (fallback mint path):** Change from `m_piState.redemptionPrice` to use the fixed redemption. This already works because we set `m_piState.redemptionPrice` to the peg value in the epoch handler.

```cpp
// Line 4017: this path already uses m_piState.redemptionPrice
// No change needed — we just set it differently in the epoch handler
```

**Line 3981-3992 (lopsided pool routing):** Remove the threshold routing. In Mode 4, 100% of CD share goes to the yield pool. The arbitrage mechanism handles pool imbalance — no need to route CD fees to treasury.
```cpp
// Simplified Mode 4 path:
m_cdYieldPool += regularCdShare;
```

#### 3e. HEAT Mint Validation (lines 2960-3020)

**No change.** `HeatMintEngine::validateMint()` already uses `redemptionPrice` passed as argument. The price we set in the epoch handler flows through.

#### 3f. Serialization (lines 220-260)

Add new fields:
```cpp
s(m_bs.m_heatPegIndex, "heat_peg_index");
s(m_bs.m_heatPegScale, "heat_peg_scale");
s(m_bs.m_lastOraclePrice, "last_oracle_price");
s(m_bs.m_lastOracleHeight, "last_oracle_height");
s(m_bs.m_oracleStaleEpochs, "oracle_stale_epochs");
s(m_bs.m_xfgBurnedLifetime, "xfg_burned_lifetime");
s(m_bs.m_heatBurnedLifetime, "heat_burned_lifetime");
```

Keep `m_piState` serialization for backward compat but it won't receive updates post-activation.

#### 3g. Remove Rebalancer from popBlock

PopBlock restores from `EpochStateSnapshot`. The rebalancer fields become no-ops.

### 4. `PiController.h` — Mark Mode 4 Path

```cpp
// Mode 4: computeTargetRatio/NewRedemptionPrice/RebalanceAmount are NO-OPs
// The fixed redemption formula replaces all three
// Kept for backward compat with old serialized state
```

Do NOT delete `PiControllerState` — it's serialized in blockchain state and removing it would break existing chain data on upgrade.

### 5. `Blockchain.h` — EpochStateSnapshot Extend

Add Mode 4 fields:
```cpp
struct EpochStateSnapshot {
    // ... existing 16 fields ...
    uint64_t heatPegIndex;
    uint64_t lastOraclePrice;
    uint32_t lastOracleHeight;
    uint32_t oracleStaleEpochs;
    uint64_t xfgBurnedLifetime;
    uint64_t heatBurnedLifetime;
};
```

### 6. `RpcServer.cpp` — New Endpoints

```cpp
// GET /heat_peg_status
// Response: { peg_index: 158, peg_dollars: 1.58, oracle_price: 500, oracle_source: "exbitron",
//              oracle_age_epochs: 1, redemption_price: 0.316, last_cpi_update: "2026-04-15" }

// POST /heat_set_peg_index (auth required, CPI signer key)
// Body: { peg_index: 159, signature: "..." }
// Updates m_heatPegIndex — used for monthly CPI updates
```

### 7. `SwapOfferRelay.h` — Oracle Priority

Add Tier 2 query method:
```cpp
// Get XFG price derived from atomic swap cross-pair triangulation
// Returns struct with: rate (XFG per 1 USD), pair, updatedAt, stale
PriceSource getXfgPriceFromAtomicSwaps(uint32_t minTrades);
```

### 8. `Currency.cpp` — Mint/Burn (New Burn Path)

Add HEAT→XFG burn validation:
```cpp
bool Currency::validateHeatBurn(const Transaction& tx, uint64_t fee,
                                 FixedPoint64 redemptionPrice,
                                 uint64_t& heatBurned, uint64_t& xfgReturned) const;
```

Uses same redemption price (symmetric). New tx extra tag for heat burn:
```cpp
const uint8_t TX_EXTRA_HEAT_BURN = 0xFA;  // HEAT→XFG burn operation
```

### 9. HeatMintEngine Additions

Add `validateHeatBurn()`:
```cpp
bool HeatMintEngine::validateHeatBurn(const Transaction& tx,
                                       uint64_t fee,
                                       FixedPoint64 redemptionPrice,
                                       uint64_t& heatBurned,
                                       uint64_t& xfgReturned) const {
    // Mirror of validateMint, reversed:
    // 1. Sum HEAT inputs (commitments with HEAT_TERM)
    // 2. Sum XFG outputs
    // 3. xfgReturned = heatInputs * redemptionPrice
    // 4. xfgOutputs + fee <= xfgReturned (can send LESS — premium)
    // 5. Record heatBurned = heatInputs_total
}
```

## Transaction Flow

### Mint (XFG → HEAT) — Existing, Unchanged

```
User burns XFG → sends to burn address
Protocol creates HEAT at redemption_price = HEAT_PEG / oracle_price
1 XFG at $5 oracle → 1 / (1.58/5) = 3.16 HEAT minted
5% premium → 3.00 HEAT to user, 0.16 HEAT to protocol
8% scalp → 0.08 XFG credited to YEM Reserve
```

### HEAT Exit (via AMM pool only — no burn/redeem path) — CORRECTED

```
HEAT exits ONLY through the Hearth AMM pool (trade HEAT for XFG).
No protocol burn/redeem path exists.

Peg defense (HEAT < peg):   Treasury buys HEAT from pool, holds it as reserve asset.
Peg arbitrage (HEAT > peg): Protocol mints HEAT, sells to pool for XFG → Treasury.
```

## Hardfork Activation

```cpp
// CryptoNoteConfig.h
const uint32_t UPGRADE_HEIGHT_V12 = 1500000;  // {HeatDeath} Mode 4 flatcoin activation

// Blockchain.cpp — gate all Mode 4 logic
bool mode4Active = (block.height >= parameters::UPGRADE_HEIGHT_V12);

if (mode4Active) {
    // Mode 4 epoch handler
} else {
    // Legacy PI controller path
}
```

Before activation height: existing Mode 2 behavior (PI controller + CPI multiplier).
At activation height: Mode 4 engages. First oracle reading snapshots the peg. No CPI retroactive credit.

## Testing Strategy

### Unit Tests

| Test | Verifies |
|------|----------|
| `FixedRedemption` | `1.58 / oracle_price` gives correct redemption at $5, $12.64, $33 |
| `OracleSelection` | Tier 1 (Exbitron) → Tier 2 (SwapXFG) → Tier 3 (freeze) |
| `OracleStaleness` | Freeze at last-known-good after STALE_FREEZE_EPOCHS |
| `TwoWayArbitrage` | HEAT>peg → mint+sell correct. HEAT<peg → buy+burn correct |
| `HeatBurnValidation` | Symmetric mint/burn ratio. Premium deducted correctly |
| `CpiUpdate` | Signed peg update transaction accepted/rejected correctly |
| `HardforkGate` | Mode 4 logic only activates at height. Old logic runs before |

### Integration Tests

| Test | Verifies |
|------|----------|
| `EpochBoundaryMode4` | Full epoch: fee split → oracle → redemption → CD yield → arb |
| `PegDefenseUnderVolatility` | 60% XFG vol → HEAT stays within 20% of $1.58 |
| `OracleFailover` | Kill Exbitron thread → SwapXFG takes over → peg holds |
| `StalenessFreeze` | Kill all oracles → peg freezes → no arb → pool drifts (expected) |
| `HardforkTransition` | At height N-1: PI mode. At height N: Mode 4 engages |
| `PopBlockMode4` | Epoch boundary + popBlock restores all Mode 4 state |

### Monte Carlo Simulation

Run `scripts/sim_v13_full.py` (existing):
- 500 sims × 15yr
- Compare M2 + CPI vs M4
- Track: HEAT $, treasury, SWF, YEM Reserve, CD APY, death spiral
- Verify: M4 peg deviation < 20% for >75% of epochs post-bootstrap
- Verify: M4 death spiral < 5%
- Verify: M4 net deflationary (XFG supply contracts over time)

## Implementation Sequence

### Phase 1: Oracle Infrastructure (2 days)
- `Blockchain.cpp`: `getReliableOraclePrice()` with Tier 1/2/3
- `SwapOfferRelay.h/cpp`: `getXfgPriceFromAtomicSwaps()`
- Test: oracle selection fallback verified

### Phase 2: Fixed Redemption (1 day)
- `Blockchain.cpp`: epoch handler rewrite (replace PI with fixed formula)
- `Blockchain.h`: new state fields
- Test: redemption price correct at all oracle levels

### Phase 3: Two-Way Arbitrage (2 days)
- `Blockchain.cpp`: arb loop in epoch handler
- `HeatMintEngine.cpp`: `validateHeatBurn()`
- `TransactionExtra.h`: `TX_EXTRA_HEAT_BURN`
- Test: symmetrical mint/burn, peg defense

### Phase 4: Hardfork Gate + Serialization (2 days)
- `CryptoNoteConfig.h`: activation height, module gate
- `Blockchain.cpp`: serialize new fields, popBlock
- Test: pre/post activation behavior

### Phase 5: CPI Update + RPC (1 day)
- `RpcServer.cpp`: `/heat_peg_status`, `/heat_set_peg_index`
- CLI tool: `cpi-update --value 159 --signer-key <key>`
- Test: signed CPI update accepted, bad sig rejected

### Phase 6: Integration + Monte Carlo (2 days)
- Full system test: testnet deployment
- Monte Carlo: 500 sims, verify all metrics
- Mainnet activation checklist

## Risk Mitigation

| Risk | Mitigation |
|------|-----------|
| Oracle failure | Tier 1→2 fallback, freeze on Tier 3. Peg stops updating, doesn't crash |
| Oracle manipulation | Exbitron has API auth. SwapXFG requires ≥5 trades. Change-rate cap (TBD) |
| Bootstrap peg jump | Activate at oracle-available epoch. First reading = current price |
| Arbitrage exhaustion | Arb limited to PROTOCOL_LP_MAX_FRACTION (if reused). Pool depth gates |
| HEAT→XFG drain | Burn path draws from pool XFG, creates new XFG. Pool balance neutral |
| Hardfork rejection | 30-day notice window, node operator communication, testnet pre-deploy |
