# HEAT CD Yield Distribution — Design & Implementation Plan

## Context

Only HEAT CDs exist (XFG CDs removed with COLD to keep the decoy pool single-asset). Hearth AMM swaps produce XFG fees. At each epoch boundary (900 blocks):

- 80% of accumulated swap fees → HEAT CD yield pool
- 20% → Treasury

The PI controller modulates how aggressively the system buys HEAT from Hearth to pay CD yields — creating structural demand when HEAT is cheap and releasing pressure when expensive.

## Simplification: Single Asset

No XFG CD tracking. The entire 80% pool targets HEAT CD holders. The PI controller determines the **spend rate** — how much XFG to actually convert to HEAT this epoch.

```
             m_currentEpochSwapFees (XFG)
                      │
          ┌───────────┴───────────┐
          ▼                       ▼
    20% treasury           80% CD pool
    (unchanged)                 │
                                ▼
                      spendRate = 1.0 + clamp(rate, -0.5, 2.0)
                                │
                    ┌───────────┴───────────┐
                    ▼                       ▼
           spend = min(pool,         remainder → reserve
           pool * spendRate +       (capped at 2× pool)
           drawFromReserve)
                    │
                    ▼
           Buy HEAT from Hearth
                    │
                    ▼
           heatReceived → HEAT CD fee pool
```

## Key Design Decisions

| Decision | Resolution |
|---|---|
| CD types | HEAT CDs only |
| Yield currency | HEAT |
| PI controller input | **XFG amount in CD pool** (not Hearth liquidity) |
| PI signal | Error = (TWAP − RedemptionPrice) / RedemptionPrice |
| PI output | spendRate = 1.0 + clamp(rate, −0.5, +2.0) |
| Reserve | Yes — holds XFG from spendRate < 1.0 periods, drawn when > 1.0 |
| Reserve cap | 2× base pool share (prevents hoarding) |

### Why PI factors by XFG pool amount

The PI controller sets **policy** (how much to spend). The **market** (Hearth AMM) determines the price. If Hearth has little HEAT, the swap has high slippage → less HEAT acquired → HEAT price rises → arbitrageurs deposit HEAT → self-correcting.

**Do NOT** pre-calculate Hearth HEAT availability. It creates a feedback loop: low HEAT → lower yields → less CD demand → even less HEAT → death spiral.

### The XFG/HEAT rate IS the PI signal

```
Error = (HearthTWAP − RedemptionPrice) / RedemptionPrice
```

This error term directly captures the XFG/HEAT price deviation. The PI controller doesn't need a separate XFG/HEAT factor — the error IS that factor.

## Flow by Market State

| HEAT Market | Rate | Spend Rate | HEAT Buy | Reserve | Effect |
|---|---|---|---|---|---|
| Too cheap | +2% | 1.20× | Boosted (draws reserve) | Draining | More HEAT demand |
| At peg | 0% | 1.0× | Normal pool | Steady | Neutral |
| Too expensive | −2% | 0.80× | Reduced | Growing (capped) | Less HEAT demand |

## State Variables (add to Blockchain.h)

```cpp
// HEAT CD locked total
uint64_t m_heatCdLockedTotal = 0;

// HEAT CD fee pool (accumulated HEAT for yield distribution)
uint64_t m_heatCdFeePool = 0;

// Per-epoch HEAT CD yield rates (epoch → rate in FEE_POOL_RATE_PRECISION)
std::map<uint64_t, uint64_t> m_heatCdEpochRates;

// Reserve (XFG held over from spendRate < 1.0 periods)
uint64_t m_cdReserve = 0;

// Already exist:
// uint64_t m_currentEpochSwapFees = 0;
// uint64_t m_treasuryBalance = 0;
```

## Epoch Boundary Implementation

```cpp
// In pushBlock(BlockEntry&), at epoch boundary (height % 900 == 0):

uint64_t epochSwapFees = m_currentEpochSwapFees;

// 1. Treasury split
uint64_t treasuryShare = (epochSwapFees * SWAP_FEE_TREASURY_SHARE_PCT) / 100;
m_treasuryBalance += treasuryShare;
uint64_t cdPool = epochSwapFees - treasuryShare;

// 2. Compute spend rate from PI controller
//    spendRate = 1.0 + clamp(redemptionRate, -0.5, +2.0)
FixedPoint64 spendRate = FixedPoint64::one();
if (m_heatRedemptionRate.isPositive()) {
    FixedPoint64 clamp2 = FixedPoint64::fromUint64(2);
    FixedPoint64 capped = m_heatRedemptionRate > clamp2 ? clamp2 : m_heatRedemptionRate;
    spendRate = FixedPoint64::one().add(capped);
} else {
    FixedPoint64 clampNeg = FixedPoint64::fromRatio(5, 10); // -0.5 floor
    FixedPoint64 capped = m_heatRedemptionRate < clampNeg.negate()
        ? clampNeg.negate() : m_heatRedemptionRate;
    spendRate = FixedPoint64::one().add(capped);
}

// 3. Compute spend amount
uint64_t spend = spendRate.mulToUint64(cdPool);

// 4. If spending more than pool, draw from reserve
if (spend > cdPool) {
    uint64_t draw = spend - cdPool;
    if (draw > m_cdReserve) draw = m_cdReserve;
    spend = cdPool + draw;
    m_cdReserve -= draw;
}

// 5. If spending less than pool, save remainder to reserve
if (spend < cdPool) {
    uint64_t save = cdPool - spend;
    m_cdReserve += save;
    // Cap reserve at 2× pool
    uint64_t cap = cdPool * 2;
    if (m_cdReserve > cap) {
        m_treasuryBalance += (m_cdReserve - cap);
        m_cdReserve = cap;
    }
}

// 6. Buy HEAT from Hearth
if (spend > 0 && m_ammPool.reserveHeat > 0) {
    uint64_t heatReceived = ammGetOutputAmount(
        spend, m_ammPool.reserveXfg, m_ammPool.reserveHeat,
        SWAP_FEE_RATE_BPS);

    // If heatReceived is 0 (extreme slippage), add XFG to reserve instead
    if (heatReceived == 0) {
        m_cdReserve += spend;
    } else {
        m_ammPool.reserveXfg += spend;
        m_ammPool.reserveHeat -= heatReceived;
        m_heatCdFeePool += heatReceived;
    }
}

// 7. Record per-epoch rate
if (m_heatCdLockedTotal > 0 && m_heatCdFeePool > 0) {
    uint64_t currentEpoch = height / epochDuration;
    m_heatCdEpochRates[currentEpoch] = m_heatCdFeePool;
}

// 8. Reset
m_currentEpochSwapFees = 0;
```

## CD Creation and Withdrawal

### Creation
Wallet locks HEAT in a CD output with a term. `TransactionOutput` has `assetId=HEAT` and a term > 0. On `pushTransaction`:

```cpp
if (out.assetId == AssetId::HEAT && out.term > 0) {
    m_heatCdLockedTotal += out.amount;
}
```

### Withdrawal
When CD matures (height >= unlockTime), user spends the CD output:

```cpp
// Validate enough HEAT in fee pool
uint64_t yield = computeCdYield(amount, creationEpoch, currentEpoch);
if (yield > m_heatCdFeePool) yield = m_heatCdFeePool;

// Pay yield
m_heatCdFeePool -= yield;
m_heatCdLockedTotal -= amount;

// Add HEAT outputs: principal + yield (both assetId=HEAT)
```

### Yield computation

```cpp
uint64_t computeCdYield(uint64_t amount, uint64_t creationEpoch, uint64_t currentEpoch) {
    // Simple pro-rata: share of fee pool based on locked amount
    // For each epoch: yield = (amount / m_heatCdLockedTotal) * epochFeePool
    uint64_t totalYield = 0;
    for (uint64_t e = creationEpoch; e <= currentEpoch; ++e) {
        auto it = m_heatCdEpochRates.find(e);
        if (it != m_heatCdEpochRates.end()) {
            uint64_t epochPool = it->second;
            uint64_t epochLocked = getEpochLockedTotal(e);
            if (epochLocked > 0)
                totalYield += (amount * epochPool) / epochLocked;
        }
    }
    return totalYield;
}
```

## Implementation Phases

### Phase A: State + Epoch Pipeline (primary)
- Add `m_heatCdLockedTotal`, `m_heatCdFeePool`, `m_heatCdEpochRates`, `m_cdReserve` to Blockchain
- Implement the full epoch boundary pipeline (treasury split → spend rate → Hearth buy → rate recording)
- Reset in rebuildCache

### Phase B: CD Creation + Withdrawal Hooks
- Track CD lock on `pushTransaction` (assetId=HEAT + term > 0)
- Track CD unlock on withdrawal (reverse lock, compute yield, pay from fee pool)
- Implement `computeCdYield` in Currency

### Phase C: Wallet + RPC
- `deposit_heat <amount> <term>` CLI command
- `withdraw_heat_cd <id>` CLI command
- RPC endpoint: `get_cd_yield_estimate <amount> <term>`

### Phase D: Tests
- CD lifecycle test
- PI spend rate modulation test
- Reserve cap test

## Files

| File | Phase | Purpose |
|---|---|---|
| `src/CryptoNoteCore/Blockchain.h` | A | State variables |
| `src/CryptoNoteCore/Blockchain.cpp` | A+B | Epoch pipeline, CD create/withdraw hooks |
| `src/CryptoNoteCore/Currency.h/cpp` | B | CD yield computation |
| `src/SimpleWallet/SimpleWallet.h/cpp` | C | CD CLI commands |
| `tests/CoreTests/CdYield.cpp` | D | Tests |

~5 files, ~300 lines.
