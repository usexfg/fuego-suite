# Fuego Tri-Token Dev Guide: HEAT Colored Coin, Grand Central DEX, iMOB Inverse Token

## Context

Fuego's evolution from a single-asset privacy chain (XFG) into a sovereign tri-token economy: XFG (hard-capped reserve), HEAT (algorithmic stablecoin), and iMOB (elastic volatility sink). Today, HEAT exists only as a permanent burn (`DEPOSIT_TERM_FOREVER`) that destroys XFG and produces a STARK proof for L2 minting -- essentially a wXFG-like wrapper for ETH. That old model is abandoned. The new HEAT is a **sovereign algorithmic stable asset** with its own floating price dynamics, distinct from XFG, governed by a PI controller -- not a 1:1 XFG mirror.

There is no native HEAT balance on L1, no AMM, no PI controller, and no iMOB token. The orderbook-based SwapXFG handles cross-chain atomic swaps but not internal asset pairs. A deferred `pool_v11/` directory contains proven constant-product AMM math (`PoolAMM.h`) that runs off-chain in the Go SwapDaemon -- this math can be reused but the DEX itself must be consensus-embedded. The Elderfier validator path has been abandoned.

This plan creates a single build-order with files to create, files to modify, consensus rules, and verification steps.

### Key Design Decisions (Resolved)

| Decision | Resolution |
|----------|------------|
| HEAT initial redemption price | **0.5 XFG per HEAT** (not 1:1 -- see rationale below) |
| AMM pools | All three: XFG/HEAT, XFG/iMOB, HEAT/iMOB (triangular arbitrage) |
| AMM swap privacy | Public amounts in v1 (pool reserves are public anyway) |
| Path B (CDP loans) | Deferred to post-launch |
| Maelisandra / wHEAT bridge | Deferred to post-launch |
| JIT Zaps | Deferred to post-launch |
| iMOB governance scope | Treasury fee routing only (not PI controller tuning) |
| PI controller tuning | Hard-coded at launch; governance mechanism TBD post-launch |
| CD yield currency | XFG CDs pay XFG yield; HEAT CDs pay HEAT yield |
| iMOB bank-run backstop | Burn-to-mint iMOB is sufficient (no CD routing needed since CDPs are deferred) |
| Old HEAT (wXFG mirror) | Abandoned -- new HEAT is a sovereign floating asset |

### Why 0.5 XFG per HEAT (Not 1:1)

A 1:1 initial ratio creates the false impression HEAT is "wrapped XFG." RAI proved the initial price doesn't matter -- RAI launched at $3.14 and settled around $2.80. What matters is that the PI controller can move the price smoothly. Starting at 0.5 XFG/HEAT:
- Immediately signals HEAT is a distinct asset with its own price dynamics
- Gives the PI controller headroom to adjust in both directions from day one
- Avoids confusion with the abandoned wXFG model
- The exact value (0.5) is a sensible default; Monte Carlo simulations during Phase 7 can validate or adjust this before mainnet launch

---

## Phase 0: Fixed-Point Math Library (Q64.64)

**Why first:** The PI controller, AMM price calculations, and yield modifiers all require deterministic decimal math. IEEE 754 floating-point varies across compilers/architectures and is forbidden in consensus code. The existing codebase uses `__uint128_t` for intermediate products (see `Blockchain.cpp` epoch fee rate calculation) -- Q64.64 extends this pattern.

### Create

| File | Purpose |
|------|---------|
| `src/Common/FixedPoint.h` | Q64.64 type: 64-bit integer + 64-bit fractional, backed by `__int128` |
| `src/Common/FixedPoint.cpp` | Lookup tables for `exp`/`ln` Taylor approximations (deterministic iteration count) |
| `tests/UnitTests/FixedPointTests.cpp` | Cross-platform determinism + edge case tests |

### API

```
add(a, b), sub(a, b), mul(a, b), div(a, b)
exp_approx(x), ln_approx(x)           -- truncated Taylor, fixed iteration count
fromUint64(v), toUint64(v)             -- conversion helpers
fromRatio(num, denom)                  -- construct from integer ratio
```

### Rules
- Pure integer arithmetic only -- no `<cmath>`, no `double`, no `float`
- Overflow/underflow saturate to `FIXEDPOINT_MAX` / `FIXEDPOINT_MIN`, never UB
- All operations use `__int128` intermediates (proven pattern in this codebase)

### Modify
- `src/CMakeLists.txt` -- add `FixedPoint.cpp` to Common library sources

### Verify
- Compile identical test binary on x86_64 and ARM64
- Run 10,000 random input pairs through every operation, assert byte-identical results
- Edge cases: max values, min values, near-zero denominators, overflow saturation
- Identity tests: `a * (1/a) == 1`, `a + (-a) == 0`

---

## Phase 1: HEAT as a Native Colored Coin (Hard Fork v11)

**Why:** Transitions HEAT from "permanent burn with L2 mint" to a native L1 colored coin with two minting paths. This is a consensus-breaking change requiring `BLOCK_MAJOR_VERSION_11`.

### 1A. Asset Identification

**Create:**

| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/AssetId.h` | Asset type enum and helpers |

```cpp
enum class AssetId : uint8_t {
    XFG  = 0x00,   // Native Fuego coin
    HEAT = 0x01,   // Algorithmic stablecoin
    MOB = 0x02,   // Inverse DAO token (Phase 4)
};
```

**Modify:**

| File | Change |
|------|--------|
| `include/CryptoNote.h:96-101` | Add `uint8_t assetId = 0;` to `TransactionOutputUnified`. XFG outputs default to 0 = backward-compatible. |
| `include/CryptoNote.h:109-114` | Add `uint8_t assetId = 0;` to `TransactionInputUnified`. |
| `src/CryptoNoteCore/CryptoNoteSerialization.cpp` | Serialize `assetId` field for v11+ transactions only. |
| `src/CryptoNoteCore/DepositCommitment.h:86-90` | Add `IMOB = 3` to the `CommitmentType` enum (currently has HEAT=0, COLD=1, YIELD=2). |

### 1B. HEAT Minting Engine

**Create:**

| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/HeatMintEngine.h` | HEAT minting/redemption logic, isolated from core consensus |
| `src/CryptoNoteCore/HeatMintEngine.cpp` | Implementation |
| `tests/UnitTests/HeatMintTests.cpp` | Unit tests |

**Minting path (v11 launch -- Path A only, Path B deferred):**

| Path | Mechanism | Term Flag | Redeemable? |
|------|-----------|-----------|-------------|
| A: Permanent Burn | Lock XFG forever, mint HEAT at Redemption Price | `DEPOSIT_TERM_FOREVER` (0xFFFFFFFF) | No -- exit via DEX swap only |
| ~~B: CDP Loan~~ | ~~Lock XFG in zk-event-locked CD, mint HEAT as loan~~ | ~~`DEPOSIT_TERM_ZK_EVENT`~~ | **Deferred** to post-launch hard fork |

Path A produces a `TransactionOutputUnified` with `assetId = HEAT`. Users who want to exit HEAT positions swap on the Grand Central DEX.

**HEAT is NOT wXFG.** The old model (mirror XFG atomic units on ETH) is abandoned. HEAT has its own floating redemption price starting at 0.5 XFG/HEAT. The mint rate is: `HEAT_minted = XFG_locked / RedemptionPrice`. At launch with price 0.5: locking 1 XFG mints 2 HEAT.

**Modify:**

| File | Change |
|------|--------|
| `src/CryptoNoteConfig.h` | Add: `HEAT_INITIAL_REDEMPTION_PRICE_NUM = 1`, `HEAT_INITIAL_REDEMPTION_PRICE_DENOM = 2` (0.5 XFG per HEAT), `HEAT_TIER_0..3` (HEAT denomination tiers for ring sigs -- own atomic scale, not mirroring XFG), `HEAT_CD_TIER_0..3` (HEAT-denominated CD tiers, see Phase 5) |
| `src/CryptoNoteCore/Currency.h/cpp` | Add `m_heatSupply` field, `mintHeat()` / `burnHeat()` accounting methods |
| `src/CryptoNoteCore/Blockchain.h` | Add `m_heatSupply` to serialized state, `HeatMintEngine` member |
| `src/CryptoNoteCore/Blockchain.cpp` | In `pushTransaction()`: detect XFG lock-to-mint-HEAT, call `HeatMintEngine::validateMint()`, update supply. In `popBlock()`: reverse. **Fail-safe**: all HEAT state transitions wrapped in try/catch -- failure reverts the transaction, never halts block processing |
| `src/CryptoNoteCore/CommitmentIndex.h/cpp` | Track HEAT outputs in the unified decoy pool |
| `src/CMakeLists.txt` | Add `HeatMintEngine.cpp` to CryptoNoteCore sources |

**HEAT denomination tiers** (separate from XFG tiers -- HEAT is its own asset):
```
HEAT_TIER_0 =  16,000,000   // 1.6 HEAT (0.8 XFG worth at initial 0.5 redemption price)
HEAT_TIER_1 = 160,000,000   // 16 HEAT
HEAT_TIER_2 = 1,600,000,000 // 160 HEAT
HEAT_TIER_3 = 16,000,000,000 // 1600 HEAT
```
These tiers use the same 7-decimal COIN base (1 HEAT = 10,000,000 atomic units) but the tier amounts reflect HEAT's own price level, not XFG's.

### 1C. Privacy Unification (0xD5 Decoy Pool)

The unified `0xD5` system already exists and works correctly. The `DepositSecretPayload` (45 bytes) encodes `depositType` inside the encrypted payload, so on-chain all deposits are indistinguishable. Changes:

- Activate `DepositType::COLD = 0x01` (currently defined but commented out at `TransactionExtra.h:216`)
- **All deposit types** (COLD, HEAT permanent burn) produce outputs sharing the same ring-signature decoy pool
- HEAT uses its own 4-tier denomination system (HEAT_TIER_0..3) -- ring signature decoys are drawn from outputs at matching HEAT tiers
- Metadata padding: all `0xD5` payloads maintain the fixed 45-byte encrypted payload size

### 1D. Fail-Safe Isolation (Mandatory)

These are hard consensus rules, not guidelines:

1. XFG `MONEY_SUPPLY` (8.8M) remains an immutable hard cap -- HEAT minting locks/unlocks circulating XFG, never alters emission
2. HEAT state failure (any exception in `HeatMintEngine`) reverts the individual transaction -- block processing continues
3. If HEAT is abandoned, XFG transfers, atomic swaps, and COLD CDs operate without degradation

### Verify Phase 1
- Create HEAT via Path A: lock 1 XFG at 0.5 redemption price, verify 2 HEAT minted with correct assetId
- Verify XFG locked amount matches (not destroyed -- locked for permanent backing)
- Verify HEAT supply increased by minted amount
- Decoy pool test: HEAT and COLD outputs share ring members in a withdrawal ring signature
- Fail-safe test: inject corrupted HEAT state, verify XFG consensus continues unaffected
- Reorg test: push/pop blocks with HEAT transactions, verify supply tracking is consistent
- Verify old wXFG-style 1:1 burns are rejected post-v11 fork

---

## Phase 2: Grand Central DEX (Consensus-Embedded AMM)

**Why:** The PI controller needs an on-chain price feed. The design specifies a Constant Product AMM (`X * Y = K`) built into consensus rules -- not the existing off-chain orderbook.

### Scope: All Three Pools

All three pools ship at launch for triangular arbitrage:
- **XFG/HEAT** -- primary price discovery, PI controller feed
- **XFG/iMOB** -- protocol recapitalization, treasury buybacks
- **HEAT/iMOB** -- direct arbitrage when HEAT deviates from peg

Without all three, arbitrageurs must route through two hops (wider spread, slower correction). The marginal cost of a third pool is one more `AmmPoolState` entry.

### 2A. AMM State

**Create:**

| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/AmmPool.h` | Pool state struct, consensus validation interface |
| `src/CryptoNoteCore/AmmPool.cpp` | Pool state transitions, delegates math to existing `PoolAMM.h` |
| `tests/UnitTests/AmmPoolTests.cpp` | AMM math + consensus validation tests |

**Pool state (per pool):**
```cpp
enum class PoolPair : uint8_t {
    XFG_HEAT  = 0,
    XFG_IMOB  = 1,
    HEAT_IMOB = 2,   // recommended -- see Recommendations
};

struct AmmPoolState {
    PoolPair pair;
    uint64_t reserveA;         // base asset reserve (public)
    uint64_t reserveB;         // quote asset reserve (public)
    uint64_t totalLpShares;    // total LP token supply
    uint64_t feeAccumulator;   // accumulated fees (routed to epoch pool)
};
```

**Reuse existing math** from `src/SwapDaemon/pool_v11/PoolAMM.h`:
- `poolGetOutputAmount()`, `poolGetInputAmount()` -- swap calculation
- `poolMintLPShares()` -- LP share minting (initial: `sqrt(A*B) - MIN_LIQUIDITY`)
- `poolGetWithdrawalAmounts()` -- proportional withdrawal
- `poolGetSpotPrice()` -- price feed for PI controller (returns `price * 1e18`)
- `poolValidateSwap()` -- invariant validation
- `poolValidateDepositRatio()` -- deposit ratio check

The `AmmPool` module wraps these functions in consensus-validated state transitions. The math functions themselves are not duplicated.

### 2B. New Transaction Extra Tags

**Modify `src/CryptoNoteCore/TransactionExtra.h`:**

```cpp
#define TX_EXTRA_AMM_SWAP      0xEC   // AMM swap operation
#define TX_EXTRA_AMM_ADD_LIQ   0xED   // Add liquidity
#define TX_EXTRA_AMM_REM_LIQ   0xEE   // Remove liquidity
```

**New structs:**

```cpp
struct TransactionExtraAmmSwap {
    uint8_t  poolId;        // PoolPair enum
    uint8_t  direction;     // 0 = A->B, 1 = B->A
    uint64_t inputAmount;   // amount being swapped
    uint64_t minOutput;     // slippage protection
};

struct TransactionExtraAmmAddLiquidity {
    uint8_t  poolId;
    uint64_t amountA;
    uint64_t amountB;
};

struct TransactionExtraAmmRemoveLiquidity {
    uint8_t  poolId;
    uint64_t lpSharesBurned;
    uint64_t minAmountA;    // slippage protection
    uint64_t minAmountB;
};
```

**Also modify:**
- `src/CryptoNoteCore/TransactionExtra.cpp` -- parsing for 0xEC, 0xED, 0xEE
- `TransactionExtraField` boost::variant typedef -- include new structs

### 2C. Consensus Validation

**Modify `src/CryptoNoteCore/Blockchain.h`:**
- Add `std::array<AmmPoolState, 3> m_ammPools` (or 2 if deferring HEAT/iMOB)

**Modify `src/CryptoNoteCore/Blockchain.cpp`:**
- In `pushTransaction()` after input processing:
  - Parse tx_extra for AMM tags
  - `TX_EXTRA_AMM_SWAP`: validate `minOutput` achievable, execute swap via `AmmPool`, update reserves, deduct fee, route fee to `m_currentEpochSwapFees`
  - `TX_EXTRA_AMM_ADD_LIQ`: validate deposit ratio within tolerance, mint LP shares, update reserves
  - `TX_EXTRA_AMM_REM_LIQ`: validate LP share ownership, burn shares, return proportional reserves
- In `popBlock()`: reverse AMM state changes
- In epoch boundary: AMM fees flow into the existing epoch fee accumulator alongside atomic swap fees
- **Fail-safe**: AMM validation failure rejects the transaction, never halts block processing

### 2D. LP Token Tracking

LP shares are tracked as special `TransactionOutputCommitment` / `TransactionOutputUnified` outputs with a reserved term code (e.g., `DEPOSIT_TERM_LP = 0xFFFFFFFD`). This reuses the existing ring-signature infrastructure for LP privacy. Spending LP shares (to remove liquidity) uses the standard `TransactionInputCommitmentSpend` / `TransactionInputUnified` mechanism.

**Add to `src/CryptoNoteConfig.h`:**
```cpp
const uint32_t DEPOSIT_TERM_LP = 0xFFFFFFFD;  // LP share marker
```

### 2E. Privacy Model for AMM

- Pool reserves (`reserveA`, `reserveB`) are **public** -- necessary for price calculation, cannot be hidden
- Individual swap amounts: **public in v1** (practical recommendation -- swap output is deterministic from input + public reserves, so hiding the input provides minimal privacy gain). ZK-private swaps can be layered in a future version
- LP additions/removals: amounts visible (proportional to public reserves)
- User identity: hidden via standard ring signatures -- the AMM knows *what* was swapped but not *who* swapped

### Verify Phase 2
- Constant product invariant holds after every swap: `reserveA_new * reserveB_new >= reserveA_old * reserveB_old`
- Slippage protection: swap rejected when output < `minOutput`
- LP share proportionality: add/remove liquidity produces correct share ratios
- Fee routing: AMM swap fees appear in `m_currentEpochSwapFees` and distribute at epoch boundary
- Front-running: verify deterministic transaction ordering within a block
- Reorg: push/pop blocks with AMM transactions, verify reserve state consistency

---

## Phase 3: PI Controller (Epoch-Based Redemption Rate)

**Why:** The PI controller is the "central bank algorithm" that stabilizes HEAT by adjusting the Redemption Price based on the AMM market price. Without it, HEAT has no peg mechanism.

### Create

| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/PiController.h` | Controller interface |
| `src/CryptoNoteCore/PiController.cpp` | PI math using Q64.64 FixedPoint |
| `tests/UnitTests/PiControllerTests.cpp` | Stability + determinism tests |

### Core Logic

```
Error = (MarketPrice - RedemptionPrice) / RedemptionPrice

RedemptionRate = Kp * Error + Ki * IntegralError

NewRedemptionPrice = RedemptionPrice * (1 + RedemptionRate * BlocksElapsed / BlocksPerYear)
```

- Runs at every epoch boundary (every 900 blocks)
- `MarketPrice` = `poolGetSpotPrice(m_ammPools[XFG_HEAT])`
- `RedemptionPrice` = persisted Q64.64 state
- `IntegralError` = accumulated Q64.64 state (clamped to prevent wind-up)

### Constants (add to `src/CryptoNoteConfig.h`)

```cpp
// PI Controller tuning for 8.8M XFG supply
// RAI used Kp ≈ 2e-7 for ~$1B market cap; scale proportionally
const uint64_t PI_KP_NUM = 2;           // Kp = 2/100 = 0.02 (Q64.64)
const uint64_t PI_KP_DENOM = 100;
const uint64_t PI_KI_NUM = 2;           // Ki = 2/10000 = 0.0002 (Q64.64)
const uint64_t PI_KI_DENOM = 10000;
const uint64_t PI_MAX_RATE_NUM = 50;    // Max ±50% annual rate
const uint64_t PI_MAX_RATE_DENOM = 100;
const uint64_t PI_INTEGRAL_CLAMP_NUM = 100;  // Integral wind-up limit
const uint64_t PI_INTEGRAL_CLAMP_DENOM = 100;
```

These are initial values, **hard-coded at launch**. iMOB governance is limited to treasury fee routing only -- it cannot tune PI controller parameters. A governance mechanism for Kp/Ki tuning is TBD post-launch (requires careful design to prevent hostile parameter manipulation).

### Modify

| File | Change |
|------|--------|
| `src/CryptoNoteCore/Blockchain.h` | Add `FixedPoint64 m_heatRedemptionPrice`, `m_heatIntegralError`, `m_heatRedemptionRate` (all persisted) |
| `src/CryptoNoteCore/Blockchain.cpp` | At epoch boundary (after fee distribution, ~line 3279): read AMM spot price, call `PiController::calculateRedemptionRate()`, update `m_heatRedemptionPrice`, store in `EpochReport` |
| `src/CryptoNoteCore/CommitmentIndex.h` | Add redemption rate/price fields to `EpochReport` struct |

### Verify Phase 3
- Stability: simulate 1,000 epochs of random price shocks, verify convergence to equilibrium
- Boundary: rate clamped at `PI_MAX_RATE`, integral clamped at `PI_INTEGRAL_CLAMP`
- Determinism: identical inputs produce identical outputs across architectures
- No floating-point: grep the entire PiController for `double`/`float` -- must find zero

---

## Phase 4: iMOB Inverse Token

**Why:** iMOB absorbs excess HEAT during bull markets (burn HEAT -> mint iMOB) and provides recapitalization during bear markets. It is the "ultimate pressure release valve."

### 4A. iMOB Asset Type

Already defined in Phase 1A (`AssetId::IMOB = 0x02`). Additional work:

**Add to `src/CryptoNoteConfig.h`:**
```cpp
const uint64_t IMOB_TIER_0 = AMOUNT_TIER_0;   // Same atomic values as XFG
const uint64_t IMOB_TIER_1 = AMOUNT_TIER_1;    // Distinguished by assetId
const uint64_t IMOB_TIER_2 = AMOUNT_TIER_2;
const uint64_t IMOB_TIER_3 = AMOUNT_TIER_3;
const uint64_t IMOB_BURN_THRESHOLD_BPS = 100;  // 1% peg deviation before iMOB mint allowed
const uint64_t IMOB_TREASURY_BUYBACK_PCT = 5;  // 5% of treasury used for iMOB buybacks/epoch
```

### 4B. iMOB Minting Engine

**Create:**

| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/ImobMintEngine.h/cpp` | iMOB minting and burning consensus rules |
| `tests/UnitTests/ImobTests.cpp` | iMOB lifecycle tests |

**New tx_extra tags (add to `TransactionExtra.h`):**
```cpp
#define TX_EXTRA_IMOB_MINT   0xD6   // Burn HEAT to mint iMOB
#define TX_EXTRA_IMOB_BURN   0xD7   // Burn iMOB (treasury buyback)
```

**Minting conditions:**
1. **Bull market sink (primary backstop)**: User burns HEAT, protocol mints iMOB. Only allowed when `abs(MarketPrice - RedemptionPrice) / RedemptionPrice > IMOB_BURN_THRESHOLD_BPS / 10000`. This is sufficient as the sole backstop mechanism -- with CDPs deferred, there is no bad debt to recapitalize against, so the CD-routing approach from the design docs is not needed at launch.
2. **Bear market recapitalization**: Deferred to post-launch (only relevant when Path B CDPs ship, creating potential undercollateralization scenarios)

**Why burn-to-mint is enough without CD routing:** The three-layer defense at launch is:
- Layer 1: PI controller adjusts redemption rate (boosts CD yields to pull HEAT off market)
- Layer 2: Swap fee buy-and-burn (constant baseline upward pressure)
- Layer 3: iMOB burn-to-mint (instant HEAT destruction when peg deviates >1%)

Without CDPs, there's no leveraged minting and no bad debt. The burn-to-mint pathway handles the only remaining risk: excess HEAT from permanent burns entering circulation faster than demand absorbs it.

**Burning:**
- At each epoch boundary: `treasuryBuyback = m_treasuryBalance * IMOB_TREASURY_BUYBACK_PCT / 100`
- Execute implicit AMM swap on XFG/iMOB pool: buy iMOB with XFG, burn the iMOB
- This is a protocol-level operation embedded in epoch boundary logic (no user transaction)

### Modify

| File | Change |
|------|--------|
| `src/CryptoNoteCore/Currency.h/cpp` | Add `m_imobSupply` (no hard cap -- key difference from XFG) |
| `src/CryptoNoteCore/Blockchain.h/cpp` | Add `m_imobSupply`, integrate `ImobMintEngine` into tx processing, add treasury buyback to epoch boundary |
| `src/CryptoNoteCore/TransactionExtra.h/cpp` | Tags 0xD6, 0xD7 + structs + parsing |
| `src/CMakeLists.txt` | Add `ImobMintEngine.cpp` |

### 4C. Privacy & Governance Isolation

- iMOB uses identical privacy primitives: ring sigs, Pedersen commitments, unified 0xD5 decoy pool
- iMOB DAO governance scope is **strictly limited to treasury fee routing percentages only**
- iMOB governance **cannot** modify: PI controller parameters (Kp, Ki), XFG hard cap, XFG emission schedule, core consensus rules, AMM fee rates, or CD yield mechanics
- PI controller tuning is hard-coded at launch; a governance mechanism is TBD post-launch

### Verify Phase 4
- Minting threshold: iMOB mint rejected when HEAT peg deviation < 1%
- Minting threshold: iMOB mint accepted when HEAT peg deviation >= 1%
- Treasury buyback: correct iMOB amount burned at epoch boundary
- Supply elasticity: simulate bull/bear cycles, verify iMOB supply expands/contracts correctly
- No hard cap: iMOB supply can grow unbounded (no `MONEY_SUPPLY` equivalent for iMOB)

---

## Phase 5: HEAT-Denominated CDs & PI-Modulated Yield

**Why:** The Redemption Rate directly modulates CD yields, creating the feedback loop that stabilizes HEAT: positive rate -> boost yields -> incentivize locking -> reduce HEAT circulation. This only works if HEAT CDs exist and pay yields in HEAT.

### 5A. HEAT-Denominated CDs

Users can create CDs denominated in HEAT (not just XFG). This is essential for the PI controller feedback loop.

**HEAT CD amount tiers** (add to `src/CryptoNoteConfig.h`):
```cpp
const uint64_t HEAT_CD_TIER_0 =  16,000,000;    // 1.6 HEAT
const uint64_t HEAT_CD_TIER_1 = 160,000,000;     // 16 HEAT
const uint64_t HEAT_CD_TIER_2 = 1,600,000,000;   // 160 HEAT
const uint64_t HEAT_CD_TIER_3 = 16,000,000,000;  // 1600 HEAT
```

HEAT CDs use the same term structure as XFG CDs (1, 18, 36, 72 epochs) and the same ring-signature privacy model. They are distinguished by `assetId = HEAT` in the `TransactionOutputUnified`.

### 5B. Yield Currency Rule

**XFG CDs pay XFG yield. HEAT CDs pay HEAT yield.** Each asset's yield comes from its own fee pool:

| CD Type | Yield Currency | Fee Source |
|---------|---------------|------------|
| XFG CD | XFG | XFG-side of AMM swap fees + atomic swap fees |
| HEAT CD | HEAT | HEAT-side of AMM swap fees (protocol buys HEAT off DEX if needed) |

This is critical because:
- Boosting HEAT CD yields creates HEAT demand (protocol must acquire HEAT to pay yields)
- Reducing HEAT CD yields releases HEAT back to market
- The PI controller modulates **HEAT CD yields only** -- XFG CD yields remain stable and unaffected

### 5C. PI-Modulated Yield

### Modify

| File | Change |
|------|--------|
| `src/CryptoNoteCore/Blockchain.cpp` (~line 3279) | Compute `yieldModifier = 1.0 + clamp(redemptionRate, -1.0, 2.0)` in Q64.64. Apply to HEAT CD share only: `adjustedHeatCdShare = heatCdShare * yieldModifier`. XFG CD share unchanged. Floor at 0 (yield never goes negative). |
| `src/CryptoNoteCore/CommitmentIndex.h` | Store per-epoch yield modifier in `EpochReport` alongside existing `feeRateFixedPoint`. Add separate tracking for XFG-locked CDs vs HEAT-locked CDs. |
| `src/CryptoNoteCore/Blockchain.cpp` | Split epoch fee distribution: XFG fees -> XFG CD pool, HEAT fees -> HEAT CD pool (with PI modifier) |

### Behavior

| HEAT Market State | Redemption Rate | HEAT CD Yield Modifier | XFG CD Yield | Effect |
|---|---|---|---|---|
| HEAT too expensive (> target) | Negative | < 1.0 | Unchanged | Lower HEAT CD yields -> users unlock HEAT -> sell -> supply increases |
| HEAT at target | Zero | 1.0 | Unchanged | Normal HEAT CD yields |
| HEAT too cheap (< target) | Positive | > 1.0 | Unchanged | Higher HEAT CD yields -> users lock HEAT in CDs -> supply decreases |

**Constraint:** Yield modifier is floored at 0.0 -- CDs never lose value (that would break trust in the CD system).

### Verify Phase 5
- Positive rate produces HEAT yield modifier > 1.0
- Negative rate produces HEAT yield modifier < 1.0 but >= 0.0
- XFG CD yields are completely unaffected by PI controller
- Interest calculation with varying modifiers across epochs produces correct accumulated interest
- Floor test: extremely negative rate still produces modifier = 0.0, not negative
- HEAT CD creation at all four HEAT_CD_TIER amounts works correctly
- HEAT CD withdrawal pays HEAT (not XFG)

---

## Phase 6: RPC, Wallet, and TUI Integration

### 6A. Daemon RPC

**Modify `src/Rpc/CoreRpcServerCommandsDefinitions.h`** -- add structs:

| Endpoint | Returns |
|----------|---------|
| `/get_heat_metrics` | `redemption_price`, `market_price`, `redemption_rate`, `error`, `integral_error`, `heat_supply`, `imob_supply` |
| `/amm_quote` | Params: `pool_id`, `input_amount`, `direction`. Returns: `expected_output`, `price_impact_bps`, `fee` |
| `/amm_pool_info` | `reserve_a`, `reserve_b`, `total_lp_shares`, `spot_price`, `fee_accumulator` |
| `/get_imob_metrics` | `imob_supply`, `treasury_balance`, `buyback_rate` |

**Modify `src/Rpc/RpcServer.h/cpp`** -- register handlers.
**Modify `src/CryptoNoteCore/ICore.h`** -- add virtual methods for AMM/HEAT queries.

### 6B. SimpleWallet CLI

**Modify `src/SimpleWallet/SimpleWallet.cpp`** -- add commands:

| Command | Action |
|---------|--------|
| `mint_heat <xfg_amount>` | Lock XFG permanently, mint HEAT at current redemption price |
| `swap <from> <to> <amount>` | AMM swap (e.g., `swap xfg heat 10`) |
| `add_liq <pool> <amount_a> <amount_b>` | Add liquidity |
| `remove_liq <pool> <lp_shares>` | Remove liquidity |
| `mint_imob <heat_amount>` | Burn HEAT to mint iMOB (only when peg deviation > 1%) |
| `deposit_heat <amount> <tier>` | Create HEAT-denominated CD |
| `heat_info` | Show HEAT metrics (redemption price, rate, supply) |
| `pool_info [pool]` | Show AMM pool state |
| `balances` | Show XFG, HEAT, and iMOB balances |

### 6C. Wallet Backend

**Modify `src/Wallet/WalletGreen.h/cpp`:**
- `createHeatMintTransaction()`, `createAmmSwapTransaction()`, `createAddLiquidityTransaction()`, `createRemoveLiquidityTransaction()`, `createImobMintTransaction()`
- `getHeatBalance()`, `getImobBalance()` -- per-asset balance queries scanning for `assetId`

### 6D. TUI (Go)

**Modify `tui/` and `swapxfg/`:**
- HEAT minting screen (lock XFG, show HEAT amount at current redemption price)
- AMM swap screen (select pool, input amount, view quote with price impact)
- Pool info dashboard (reserves, price, volume)
- iMOB metrics display
- Add `PairHEAT` and `PairIMOB` to `swapxfg/app/pairs.go`

### 6E. UI Viewpoint Shift: HEAT-Based Pricing (Note for Future)

**Important UX direction:** Once HEAT is live and stable, the UI should shift toward **HEAT-based pricing** as the default viewpoint. HEAT is the stable unit of account; XFG is the volatile reserve. Users should see:
- "This CD yields 18% APR in HEAT" (not XFG)
- "Current XFG price: 2.1 HEAT" (not the other way around)
- Swap quotes denominated in HEAT
- Portfolio value shown in HEAT

This is a cosmetic/UX refactor, not a protocol change, but it's critical for the mental model shift: **HEAT is the money, XFG is the collateral.** Flag this for the TUI/wallet UI team as a post-launch priority.

### Verify Phase 6
- RPC endpoints return correct data matching blockchain state
- Wallet commands produce valid transactions that pass consensus validation
- Balance tracking correctly separates XFG/HEAT/iMOB
- TUI displays match RPC responses

---

## Phase 7: Integration Testing & Simulation

### Testnet Deployment
- Use `TESTNET_EPOCH_DURATION_BLOCKS = 10` for fast epoch cycling
- Deploy 3+ nodes, verify identical state after 1,000 blocks with mixed transaction types

### Monte Carlo Simulation
- Simulate 10,000 epochs of random market conditions
- Verify: PI controller converges, HEAT peg deviation stays within bounds, iMOB supply expands/contracts correctly
- Reproduce the design doc claim: iMOB sink reduces maximum peg deviation by >36%
- **Initial redemption price sweep**: test 0.25, 0.5, 1.0, 2.0 XFG/HEAT starting points under identical conditions. Select the value that minimizes peg deviation during the first 100 epochs. Adjust `HEAT_INITIAL_REDEMPTION_PRICE` before mainnet if simulations reveal a better default than 0.5
- **HEAT CD yield feedback**: verify that PI-modulated HEAT CD yields effectively pull HEAT off market during undersupply and release it during oversupply

### Consensus Fork Detection
- Run 3+ nodes with identical blockchain
- Submit identical transaction sequences
- Assert byte-identical blockchain state across all nodes after each block

### Security Audit Focus Areas
1. AMM: reentrancy, flash-loan-style manipulation, sandwich attacks
2. PI controller: death spiral scenarios, extreme parameter values
3. HEAT minting: inflation bugs (more HEAT minted than XFG locked)
4. iMOB: infinite mint attacks, treasury drain
5. Ring signature decoy pool: verify HEAT/COLD/iMOB outputs are genuinely indistinguishable

---

## Summary: All New Files

| File | Phase | Purpose |
|------|-------|---------|
| `src/Common/FixedPoint.h/cpp` | 0 | Q64.64 deterministic math |
| `src/CryptoNoteCore/AssetId.h` | 1 | Asset type enum |
| `src/CryptoNoteCore/HeatMintEngine.h/cpp` | 1 | HEAT minting/burning consensus rules |
| `src/CryptoNoteCore/AmmPool.h/cpp` | 2 | Consensus-embedded AMM state + validation |
| `src/CryptoNoteCore/PiController.h/cpp` | 3 | PI controller algorithm |
| `src/CryptoNoteCore/ImobMintEngine.h/cpp` | 4 | iMOB minting/burning rules |
| `tests/UnitTests/FixedPointTests.cpp` | 0 | Cross-platform math tests |
| `tests/UnitTests/HeatMintTests.cpp` | 1 | HEAT lifecycle tests |
| `tests/UnitTests/AmmPoolTests.cpp` | 2 | AMM math + consensus tests |
| `tests/UnitTests/PiControllerTests.cpp` | 3 | Controller stability tests |
| `tests/UnitTests/ImobTests.cpp` | 4 | iMOB lifecycle tests |

## Summary: Key Modified Files

| File | Phases | Changes |
|------|--------|---------|
| `src/CryptoNoteConfig.h` | 0-5 | New constants: HEAT tiers, HEAT CD tiers, PI gains (hard-coded), iMOB thresholds, LP marker, initial redemption price (0.5 XFG/HEAT) |
| `include/CryptoNote.h` | 1 | `assetId` field in `TransactionOutputUnified` and `TransactionInputUnified` |
| `src/CryptoNoteCore/TransactionExtra.h/cpp` | 1-4 | Tags 0xEC, 0xED, 0xEE, 0xD6, 0xD7 + structs + DepositType updates |
| `src/CryptoNoteCore/Blockchain.h/cpp` | 1-5 | AMM state, PI state, HEAT/iMOB supply tracking, epoch yield modifiers, fail-safe isolation |
| `src/CryptoNoteCore/Currency.h/cpp` | 1-4 | HEAT/iMOB supply methods |
| `src/CryptoNoteCore/DepositCommitment.h` | 1 | CommitmentType::IMOB addition |
| `src/CryptoNoteCore/CommitmentIndex.h/cpp` | 1-5 | HEAT/iMOB output tracking, EpochReport extensions |
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | 6 | New RPC command structs |
| `src/Rpc/RpcServer.h/cpp` | 6 | Endpoint handlers |
| `src/Wallet/WalletGreen.h/cpp` | 6 | Transaction builders + balance queries |
| `src/SimpleWallet/SimpleWallet.cpp` | 6 | CLI commands |
| `src/CMakeLists.txt` | 0-4 | New source files |

---

## Recommendations (All Adopted)

### 1. All Three AMM Pools at Launch
XFG/HEAT, XFG/iMOB, and HEAT/iMOB ship together. Triangular arbitrage is what forces equilibrium without oracles. Marginal cost of the third pool is near zero.

### 2. Defer Path B (CDP Loans) to Post-Launch
Path B requires ZK proof event-based unlocks, CDP tracking, collateral ratios, and liquidation mechanics. Path A alone provides sufficient HEAT liquidity. Users exit via DEX swap. Ship Path B in a subsequent hard fork when STARK infra is proven.

### 3. Defer Maelisandra & wHEAT Bridge to Post-Launch
Depends on stable L1 HEAT first. Requires EVM contract development and audit. Not needed for L1 functionality.

### 4. Defer JIT Zaps to Post-Launch
Depends on stable HEAT minting + AMM liquidity + orderbook integration. Optimization for onboarding, not a core stability feature.

### 5. Public AMM Swap Amounts in v1
Pool reserves are public; swap output is deterministic from input + reserves. Hiding the input provides minimal privacy gain. Ship public amounts, layer ZK-private swaps in a future version.

### 6. iMOB DAO Governance Limited to Treasury Fee Routing Only
PI controller parameters (Kp, Ki) are hard-coded at launch. iMOB governance cannot touch PI tuning, XFG emission, hard cap, AMM fees, or CD mechanics. A governance mechanism for PI tuning is TBD post-launch with careful attack surface analysis.

### 7. Hard-Code Initial AMM Pool Bootstrapping
At v11 hard fork height, seed pools at protocol-defined ratios:
- XFG/HEAT: seeded at 0.5 XFG per HEAT (initial redemption price)
- XFG/iMOB: seeded at protocol-defined ratio (TBD via Monte Carlo, see Phase 7)
- HEAT/iMOB: derived from the other two pools' initial ratios
This prevents front-running genesis liquidity and ensures sane starting prices.

### 8. Use Monte Carlo to Validate Initial Redemption Price
The 0.5 XFG/HEAT default is a sensible starting point, but Phase 7 Monte Carlo simulations should test multiple initial prices (0.25, 0.5, 1.0, 2.0) under varied market conditions to find the value that minimizes peg deviation during the first 100 epochs. Adjust before mainnet if simulations reveal a better starting point.

### 9. HEAT-Based UI Pricing Shift
Post-launch, refactor all UI surfaces (TUI, wallet, explorer) to default to HEAT as the unit of account. HEAT is the stable money; XFG is the volatile collateral. This is a cosmetic change but critical for the mental model shift.

### 10. Burn-to-Mint iMOB is Sufficient Backstop at Launch
With CDPs deferred, there's no leveraged minting and no bad debt scenario. The three-layer defense (PI controller -> swap fee buy-and-burn -> iMOB burn-to-mint) handles all realistic risks. CD-routing iMOB backstop only becomes relevant when Path B CDPs ship.

## Launch Order

1. **Phase 0** (FixedPoint) -- 1-2 weeks
2. **Phase 1** (HEAT colored coin, Path A only) -- 2-3 weeks
3. **Phase 2** (Grand Central DEX, all 3 pools) -- 2-3 weeks
4. **Phase 3** (PI Controller) -- 1-2 weeks
5. **Phase 4** (iMOB token) -- 1-2 weeks
6. **Phase 5** (HEAT CDs + PI yield integration) -- 1-2 weeks
7. **Phase 6** (RPC + wallet + TUI) -- 1-2 weeks
8. **Phase 7** (Monte Carlo, testnet, security audit) -- 2-4 weeks

**Total estimate: ~12-18 weeks** for a functional tri-token economy with all three AMM pools, PI-controller-driven HEAT stability, HEAT-denominated CDs with modulated yields, and iMOB as a volatility sink.
