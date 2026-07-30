# Fuego v11 Master Implementation Plan

> Status: Foundation complete. 9 shipped, 2 deferred. Testnet-ready.

---

## 0. Core Architecture Decision: Hard-Coded Peg + Floating XFG

- **HEAT peg** = $1.58 constant, set in `redemptionPrice`, never changes
- **XFG price** = market discovery via Hearth pool + cross-chain swap volume
- **PI controller** arbs ONLY HEAT price deviation, never XFG price
- **Pool ratio** = consequence of XFG market price, never a target
- **Launch = 1:1 pool** (5K XFG + 5K HEAT) for immediate peg, zero convergence time

### Why $1.58 not $0.10 or $0.01

Sub-parity starts create a bootstrap paradox: the pool needs depth for the arb to work, but the arb needs the pool to already be at parity to generate depth. Only 1:1 launch solves this.

| Launch | Pool ratio | Convergence | Pool depth | Verdict |
|--------|-----------|-------------|------------|---------|
| $0.01 | 0.0063 | 5000 arb rounds | HEAT side: ~32 units | Dead |
| $0.10 | 0.063 | 500 arb rounds | HEAT side: ~316 units | Broken |
| $1.58 | 1.0 | Zero | HEAT side: 5000 units | Works |

---

## 1. Shipped Code (master — 10 commits this session)

### 1a. Mixin Enforcement
All ring-based input types now have server-side mixin validation.
- **Files**: `Core.cpp`, `CryptoNoteFormatUtils.cpp`
- **Coverage**: KeyInput (v7+), TransactionInputCommitmentSpend (v10+), TransactionInputCommitmentTransfer (v11+), TransactionInputUnified (v11+)
- **Both min and max** enforced per version gate

### 1b. Treasury Architecture
Three distinct treasury pools with clear funding and spending paths.

| Pool | Variable | Funded by | Spent on | Spent? |
|------|----------|-----------|----------|--------|
| Permanent XFG Reserve | `m_treasuryXfgReserve` | 20% CD yield buyback | Nothing | No |
| Peg Defense Balance | `m_treasuryBalance` | Mint burn 20% + swap fee treasury share | Peg defense, arb | Yes |
| LP Reserve | `m_treasuryLpReserve` | Mint burn 30% | CD yield floor (2% APY) | Conditional |

**Files**: `Blockchain.h`, `Blockchain.cpp`, `CryptoNoteConfig.h`

### 1c. HEAT Mint Burn Split
```
100% xfgBurned:
  ├─ 50% → Eternal Flame (BankingIndex, permanent deflation)
  └─ 50% → Treasury
          ├─ 60% → m_treasuryLpReserve
          └─ 40% → m_treasuryBalance
```
**Files**: `Blockchain.cpp:3558-3562`

### 1d. CD Yield Flow
```
100% swap fees → CD yield pool:
  ├─ 80% → HEAT buyback from Hearth pool → CD holders
  └─ 20% → m_treasuryXfgReserve (permanent protocol accumulator)
```
**Files**: `Blockchain.cpp`, `CryptoNoteConfig.h`

### 1e. CD Yield Floor
If organic CD fee rate drops below 2% APY, treasury LP reserve injects XFG to maintain the floor.
- `CD_YIELD_FLOOR_APY_PCT = 2`
- `CD_YIELD_XFG_BUYBACK_PCT = 20`

**Files**: `Blockchain.cpp:3999-4021`

### 1f. Removed: Protocol LP Rebalancer
The treasury LP rebalancer (single-sided LP deposits once per epoch) was removed. PI controller handles peg via 40 rounds of mint-sell per epoch — the rebalancer was redundant.
**Files**: `Blockchain.cpp` (49 lines deleted)

### 1g. 1:1 Hearth Pool Bootstrap
Pool starts at 5000 XFG + 5000 HEAT. Archive version bumped to 10.
**Config**: `HEARTH_POOL_SEED_XFG = HEARTH_POOL_SEED_HEAT = 5000`
**Files**: `Blockchain.cpp` (constructor), `CryptoNoteConfig.h`

### 1h. PopBlock Reversibility
All new state variables properly saved to `EpochStateSnapshot` and restored on `popBlock`. Verified complete.
**Coverage**: `m_treasuryXfgReserve`, `m_treasuryLpReserve`, `m_ammPool`

### 1i. Testnet Simulator (Milæsandra)
- XFG price starts at $1.58 (matching 1:1 pool)
- Growing price mode enabled (5% per 100 blocks)
- Removed stale rebalancer references

### 1j. SwapDaemon Seed Price
Hardcoded seed price updated from $0.01 to $1.58. All counterparty rates recalculated.

---

## 2. Designed (docs written, not yet coded)

### 2a. YEM v5 — Market-Clearing CD Yield
- No SWF smoothing (simulation proved unnecessary — deficit backstop never triggered)
- Raw organic CD rate with market-clearing elasticity
- Loyalty maturity bonus (deferred — needs per-CD tracking)
- LP-funded 2% yield floor (shipped)
- **Doc**: `docs/implementation/YEM_V5_DESIGN_DECISION.md`

### 2b. DIGM Dual-Curve Architecture
- **Doc**: `docs/implementation/DIGM_DUAL_CURVE_DESIGN.md`
- Bancor curves: 82.36% reserve ratio, 1M cap
- XFG/DIGM + HEAT/DIGM parallel curves (same token)
- Buy-at-release only, no holding/speculation phase
- DIGM locked to album metadata on-chain
- Float pools for speculator trading
- 100K album target → $15K HEAT locked permanently

### 2c. DIGM Stable Asset Design
- **Doc**: `DIGM_STABLE_ASSET_DESIGN.md` (referenced)
- Multi-asset collateralization (XFG 40%, BTC 30%, ETH 20%, USDC 10%)
- 150% target collateralization ratio
- Artist pricing in stable units

### 2d. Unified System Architecture
- **Doc**: `docs/implementation/UNIFIED_SYSTEM_ARCHITECTURE.md`
- All pools, flows, config constants, state variables documented

### 2e. Monte Carlo Simulation (v22)
- **Script**: `scripts/montecarlos/sim_final_v22.py`
- 1:1 pool bootstrap, market-clearing CD elasticity, DIGM curves, treasury splits
- 30-year, 1,000 sims
- Key results: 67% peg health, 32.8% CD APY, SWF $209M, 246 epochs zero-volume coverage

---

## 3. HEAT Peg Architecture

### 3a. Hard-Coded Peg = $1.58

HEAT's peg is a **constant** set at launch. It never changes.

```
redemptionPrice = FixedPoint64(heatLaunchRatio_num / heatLaunchRatio_denom)
                = FixedPoint64(1.58)
```

This is the rate at which XFG is burned to mint HEAT. One unit of HEAT always costs `redemptionPrice` units of XFG to create. This anchors HEAT to $1.58 in the pool.

### 3b. PI Controller — Peg Defense

The arb runs 40 rounds per epoch at the epoch boundary:

```
pool_ratio = reserveXfg / reserveHeat
spot_price = pool_ratio × XFG_USD_price
deviation  = |spot_price − $1.58| / $1.58

if spot_price > $1.58:  HEAT overvalued → mint new HEAT, sell to pool
                        (removes XFG from pool, adds HEAT, pushes ratio downward)
                        Treasury keeps the XFG profit

if spot_price < $1.58:  HEAT undervalued → treasury buys HEAT from pool
                        (adds XFG to pool, removes HEAT, pushes ratio upward)
                        Treasury accumulates HEAT for future sales
```

### 3c. XFG Floats Freely

The arb does NOT target pool ratio or XFG price. It only corrects HEAT price. XFG can be $0.50, $5.00, or $50.00 — as long as `pool_ratio × XFG_price = $1.58`, the peg holds and the arb stays idle. The pool ratio is the *consequence* of XFG's market price, not a target.

### 3d. Launch at $1.58, Not $0.10

| Start | Pool ratio | Convergence needed | Treasury required | Verdict |
|-------|-----------|-------------------|-------------------|---------|
| $0.01 | 0.0063 | 158:1 gap, ~5000 arb rounds | Massive, never converges | Dead |
| $0.10 | 0.063 | 15:1 gap, ~500 arb rounds | 50K+ XFG minimum | Broken, slow |
| $1.58 | 1.0 | Zero convergence | 5K XFG bootstrap | Immediate peg |

At sub-parity starts, the pool is too thin on the HEAT side. A single swap drains the pool. The arb can't function without sufficient pool depth, and sufficient pool depth requires the arb to have already converged. This is a bootstrap paradox — the pool must start at parity or it never reaches it.

1:1 launch at $1.58 means the peg is tight from block 0. The PI controller only *maintains*, never *catches up*.

## 4. Deferred

### 4a. DIGM Implementation
Full dual-curve Bancor infrastructure, float pools, album-lock mechanism.
**Priority**: Medium. Foundation ships first.

### 4b. Live Hearth Pool Rate RPC for SwapDaemon
SwapDaemon currently uses updated seed price ($1.58). Live pool rate via RPC requires:
1. New fuegod RPC endpoint (`get_fuego_price`)
2. `FuegoRpcClient` method to query it
3. `PriceOracle` integration to use it
**Priority**: Low. Seed price is sufficient for testnet launch.

### 4c. Loyalty Maturity Bonus
Per-CD bonus at maturity (2.5× for last 2.5 epochs of 72-epoch CD).
Requires per-CD creation-epoch tracking in CommitmentIndex.
**Priority**: Low. Nice-to-have.

### 4d. YEM V10 Cleanup
YEM skeleton was removed from codebase (f0335e27). Verify no residual references.

---

## 5. Config Constants (Complete)

```cpp
// Hearth
HEARTH_FEE_BPS                 = 30        // 0.3% swap fee
HEARTH_POOL_SEED_XFG           = 5000      // genesis pool
HEARTH_POOL_SEED_HEAT          = 5000      // 1:1 ratio

// HEAT mint
MINT_BURN_EF_PCT               = 50        // Eternal Flame
MINT_BURN_TREASURY_PCT         = 50        // Treasury
TREASURY_LP_PCT                = 60        // of treasury → LP
TREASURY_PEG_PCT               = 40        // of treasury → peg

// CD yield
CD_YIELD_XFG_BUYBACK_PCT       = 20        // protocol XFG accumulator
CD_YIELD_FLOOR_APY_PCT         = 2         // LP-funded floor

// Mixin
MIN_TX_MIXIN_SIZE_V10          = 8

// Milæsandra (testnet)
MILAESANDRA_SIMULATE_FEES      = true
MILAESANDRA_BASE_FEES_ATOMIC   = 2'000'000'000
MILAESANDRA_INITIAL_XFG_PRICE  = 158       // $1.58
MILAESANDRA_GROWING_PRICE      = true
MILAESANDRA_ACTIVATION_HEIGHT  = 650

// Serialization
CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER = 10
```

---

## 6. State Variables (Current)

```cpp
// Treasury
uint64_t m_treasuryXfgReserve  = 0;   // permanent protocol XFG (never spent)
uint64_t m_treasuryBalance     = 0;   // working capital for peg defense
uint64_t m_treasuryLpReserve   = 0;   // CD yield floor + growth
uint64_t m_treasuryHeatReserve = 0;   // HEAT bought for peg defense
uint64_t m_treasuryLpYield     = 0;   // LP earnings from protocol shares

// Bootstrap
bool     m_bootstrapRepaid       = false;
uint64_t m_bootstrapXfgOwed      = 0;
uint64_t m_bootstrapRepaymentVault = 0;

// CD / HEAT
uint64_t m_cdYieldPool         = 0;
uint64_t m_cdReserve           = 0;
uint64_t m_heatSupply          = 0;
uint64_t m_heatCdFeePool       = 0;

// AMM
AmmPoolState m_ammPool;              // reserveXfg, reserveHeat, totalLpShares
uint64_t     m_protocolLpShares = 0;

// PI Controller
PiControllerState m_piState;         // redemptionPrice, basinPhase
```

---

## 7. Net Asset Flows (Final)

```
SWAP VOLUME → CD YIELD:
  ├─ 80% → HEAT buyback → CD holders
  └─ 20% → m_treasuryXfgReserve (permanent)

HEAT MINT (xfgBurned):
  ├─ 50% → Eternal Flame (permanent deflation, recycled via block rewards)
  └─ 50% → Treasury:
           ├─ 60% → m_treasuryLpReserve → CD yield floor
           └─ 40% → m_treasuryBalance → peg defense

PEG ARB (PI controller):
  HEAT overvalued → mint HEAT, sell to pool → XFG to treasury
  HEAT undervalued → treasury buys HEAT → XFG to pool, HEAT to treasury

DIGM (deferred):
  XFG/DIGM Bancor → locks XFG in curve → permanent sink
  HEAT/DIGM Bancor → locks HEAT in curve → permanent sink
```

## 8. What Gets Testnet

1. ✅ 1:1 pool (peg holds from block 0)
2. ✅ PI controller (40-round mint-sell arb per epoch)
3. ✅ CD yield (80% HEAT buyback + 20% XFG accumulator)
4. ✅ CD yield floor (2% LP-funded minimum)
5. ✅ Treasury split (60/40 LP/peg)
6. ✅ Mixin enforcement (all commitment types)
7. ✅ Milæsandra simulated fees + price
8. ✅ Bootstrap repayment mechanism
9. ✅ HEAT peg: hard-coded $1.58 constant. XFG floats free. PI arb corrects pool ratio only when HEAT price deviates. Pool ratio is output, not target.
10. ⬜ DIGM dual-curve (deferred — see §4)
