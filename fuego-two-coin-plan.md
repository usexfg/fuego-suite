# Fuego Two-Coin Ecosystem — Phased Development Plan

## The Two Assets

| Asset | Role | Supply Model |
|-------|------|-------------|
| **XFG** | Hard-capped reserve & privacy coin (~8M atomic units) | Existing — immutable cap. Burned XFG recycles into future block rewards via EternalFlame. |
| **HEAT** | Algorithmic colored stablecoin | Floating — PI-controlled redemption price, starting at 0.5 XFG/HEAT. Minted by burning XFG (one-way, permanent). |

**XFG is the collateral. HEAT is the money.** The PI controller is the sole stability mechanism, proven by RAI (which operated without a secondary volatility token for years before adding FLX governance).

### Key Design Decisions

| Decision | Resolution |
|---|---|
| HEAT initial redemption price | 0.5 XFG per HEAT (signals HEAT is distinct, not wrapped XFG) |
| HEAT mint source | Burn XFG (not lock) — irreversible, feeds EternalFlame → recycles into block rewards |
| HEAT burn path | None at launch |
| HEAT exit | Sell on Hearth (AMM swap) |
| AMM pools | Single pool: XFG/HEAT ("Hearth") |
| AMM swap privacy | Public amounts in v1 (pool reserves are public, swap output is deterministic) |
| HEAT CD yield currency | HEAT (bought with XFG swap fees from Hearth each epoch) |
| Fee source for HEAT CD yield | Single XFG swap fee pool → 80% split between XFG CDs + HEAT CDs → HEAT CD share buys HEAT from Hearth |
| CD yield currency rule | XFG CDs pay XFG yield; HEAT CDs pay HEAT yield |
| PI controller tuning | Hard-coded at launch (no governance mechanism at v11) |
| Path B CDP loans | Deferred to post-launch |
| Maelisandra / wHEAT bridge | Deferred to post-launch |
| JIT Zaps | Deferred to post-launch |

### How the EternalFlame recycling works (existing mechanism)

When XFG is burned for HEAT minting, `BankingIndex::addForeverDeposit()` increments `m_ethereal_xfg`. The block reward formula in `Currency::getBlockReward()` (line 250-256) subtracts burned coins from already-generated coins, making the remaining emission pool larger:

```
Osavvirsak = alreadyGeneratedCoins - eternalFlame
baseReward = (MONEY_SUPPLY - Osavvirsak) >> emissionSpeedFactor
```

**Effect:** XFG burned today becomes available for miners to re-mine gradually. The XFG supply tends toward `MONEY_SUPPLY` but burned coins don't permanently leave — they recycle. HEAT is minted from XFG that miners eventually reclaim, creating a symbiotic dynamic between miners and HEAT stability.

---

## Phase 0: v11 Fork Scaffolding & Tag Space Audit

**Duration: 3-5 days | New files: 0 | Prerequisite for all other phases**

`BLOCK_MAJOR_VERSION_11` exists as a constant but has NO fork height, no `Currency` member, no upgrade path, and no testnet height. A v11 gate must exist before any consensus change.

### 0A — Define v11 fork height

Add to `src/CryptoNoteConfig.h`:
```cpp
const uint32_t UPGRADE_HEIGHT_V11 = <TBD>;  // Set before testnet deployment
```

Add to `src/CryptoNoteCore/Currency.h` (alongside existing `m_upgradeHeightV10`):
```cpp
uint32_t m_upgradeHeightV11;
```

Add to `CurrencyBuilder` in `Currency.h`:
```cpp
uint32_t upgradeHeightV11 = std::numeric_limits<uint32_t>::max();
```

Add to `Currency::upgradeHeight()` in `Currency.cpp`:
```cpp
case BLOCK_MAJOR_VERSION_11: return m_upgradeHeightV11;
```

Add to the testnet upgrade heights block in `Currency.cpp`:
```cpp
m_upgradeHeightV11 = <next sequential testnet height>;
```

### 0B — Tag space audit

All new tx_extra tags must not collide with existing ones. The existing tag space includes active tags at 0x00-0xEF (with some gaps). After audit:

| Tag | Purpose | Status |
|---|---|---|
| `0xF0` | `TX_EXTRA_AMM_SWAP` | Free |
| `0xF1` | `TX_EXTRA_AMM_ADD_LIQ` | Free |
| `0xF2` | `TX_EXTRA_AMM_REM_LIQ` | Free |

**Note:** The original tri-coin plan proposed 0xEC/0xED/0xEE, but 0xEC is `TX_EXTRA_ELDERFIER_MESSAGE` (active) and 0xEF is `TX_EXTRA_ELDERFIER_DEPOSIT` (active). The 0xF0-0xF2 range is clear.

### 0C — Verify COLD decoy pool

The plan's original claim that `DepositType::COLD = 0x01` is "commented out" is incorrect. COLD is active at `TransactionExtra.h:218` and the unified `0xD5` decoy pool already works. Run an integration test to confirm: create COLD deposit → withdraw → verify decoys are drawn from the unified pool (existing behavior, just verify).

### Verify Phase 0

- Node with V11 fork height activates V11 consensus rules at the correct testnet height
- All proposed tx_extra tags confirmed unused via static_assert or compile-time check
- COLD deposit → withdrawal uses unified decoy pool (confirmed existing behavior)

---

## Phase 1: Fixed-Point Math Library (Q64.64)

**Duration: 1-2 weeks | New files: 3 | No consensus changes**

IEEE 754 floating-point varies across compilers/architectures and is forbidden in consensus code. The PI controller, AMM price calculations, and yield modifiers all require deterministic decimal math. The existing codebase already uses `__uint128_t` for intermediate products in epoch fee rate calculation (`Blockchain.cpp` line 3258). Q64.64 extends this proven pattern.

### 1A — Create library

| File | Purpose |
|------|---------|
| `src/Common/FixedPoint.h` | Q64.64 type: 64-bit integer + 64-bit fractional, backed by `__int128` |
| `src/Common/FixedPoint.cpp` | `exp_approx` / `ln_approx` via fixed-iteration Taylor series; `mul` / `div` / `add` / `sub` |
| `tests/UnitTests/FixedPointTests.cpp` | Cross-platform determinism + edge case tests |

### API
```
FixedPoint64                    -- value stored as int64_t (raw Q64.64 bits)
add(a, b), sub(a, b)            -- basic arithmetic
mul(a, b), div(a, b)            -- uses __int128 intermediates
exp_approx(x), ln_approx(x)     -- truncated Taylor, fixed iteration count
fromUint64(v), toUint64(v)      -- conversion helpers
fromRatio(num, denom)           -- construct from integer ratio
```

### Rules
- Pure integer arithmetic only — no `<cmath>`, no `double`, no `float`
- Overflow/underflow saturate to `FIXEDPOINT_MAX` / `FIXEDPOINT_MIN`, never UB
- All operations use `__int128` intermediates

### Modify
- `src/CMakeLists.txt` — add `FixedPoint.cpp` to Common library sources

### Verify
- 10,000 random input pairs through every operation, assert byte-identical results on x86_64 vs ARM64
- Edge cases: max/min values, near-zero denominators, overflow saturation
- Identity tests: `a * (1/a) == 1`, `a + (-a) == 0`

---

## Phase 2: HEAT as Native Colored Coin (Hard Fork v11)

**Duration: 2-3 weeks | New files: 3 | Consensus-breaking**

Transitions HEAT from "wXFG mirror on ETH" to a sovereign L1 colored coin with its own price dynamics. HEAT has a floating redemption price starting at 0.5 XFG/HEAT — it is NOT wrapped XFG. XFG is burned (not locked) to mint HEAT, and burned XFG recycles into future block rewards via the existing EternalFlame mechanism.

### 2A — Asset Identification

Create `src/CryptoNoteCore/AssetId.h`:
```cpp
enum class AssetId : uint8_t {
    XFG  = 0x00,   // Native Fuego coin
    HEAT = 0x01,   // Algorithmic stablecoin
};
```

Modify `include/CryptoNote.h`:
- Add `uint8_t assetId = 0;` to `TransactionOutputUnified` (line ~98). XFG outputs default to 0 for backward compatibility.
- Add `uint8_t assetId = 0;` to `TransactionInputUnified` (line ~111).

Modify `src/CryptoNoteCore/CryptoNoteSerialization.cpp`:
- Serialize `assetId` field for v11+ transactions only. Pre-v11 txs omit the field.

Modify `src/CryptoNoteCore/DepositCommitment.h`:
- Verify HEAT=0, COLD=1, YIELD=2 in `CommitmentType` enum. Add comments documenting each.

### 2B — HEAT Minting Engine

Create:
| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/HeatMintEngine.h` | HEAT minting consensus rules, isolated from core |
| `src/CryptoNoteCore/HeatMintEngine.cpp` | Implementation |
| `tests/UnitTests/HeatMintTests.cpp` | Unit tests |

**Minting path (v11 launch — Path A only):**

XFG is burned and HEAT is minted at the current redemption price:

```
HEAT_minted = XFG_burned / RedemptionPrice
```

At launch (redemption price 0.5 XFG/HEAT): burning 1 XFG mints 2 HEAT.

- XFG burn uses the existing `DEPOSIT_TERM_FOREVER` (0xFFFFFFFF) → `BankingIndex::addForeverDeposit()` → increments `ethereal_xfg`
- Burned XFG recycles into future block rewards via the EternalFlame mechanism (existing, see Currency.cpp lines 250-256)
- HEAT mint produces a `TransactionOutputUnified` with `assetId = HEAT`
- Exit is via selling HEAT on Hearth (no reverse-mint / unburn path)

Modify `src/CryptoNoteConfig.h`:
```cpp
const uint64_t HEAT_INITIAL_REDEMPTION_PRICE_NUM = 1;   // 0.5 XFG per HEAT
const uint64_t HEAT_INITIAL_REDEMPTION_PRICE_DENOM = 2;

// HEAT denomination tiers (HEAT is its own asset with different tier amounts)
const uint64_t HEAT_TIER_0 =  16,000,000;    // 1.6 HEAT
const uint64_t HEAT_TIER_1 = 160,000,000;     // 16 HEAT
const uint64_t HEAT_TIER_2 = 1,600,000,000;   // 160 HEAT
const uint64_t HEAT_TIER_3 = 16,000,000,000;  // 1600 HEAT
```

Modify `src/CryptoNoteCore/Currency.h/cpp`:
- Add `uint64_t m_heatSupply` with getter/setter
- Add `mintHeat(uint64_t amount)` / `burnHeat(uint64_t amount)` accounting methods
- Note: HEAT supply is separate from XFG `m_moneySupply` — HEAT has no hard cap

Modify `src/CryptoNoteCore/Blockchain.h`:
- Add `uint64_t m_heatSupply` to serialized blockchain state
- Add `HeatMintEngine m_heatMintEngine` member

Modify `src/CryptoNoteCore/Blockchain.cpp`:
- In `pushTransaction()`: detect XFG burn-to-mint-HEAT in tx_extra, call `HeatMintEngine::validateMint()`, update HEAT supply, ensure XFG burn flows through `BankingIndex::addForeverDeposit()`
- In `popBlock()`: reverse HEAT supply changes and burn accounting
- **Fail-safe**: all HEAT state transitions wrapped in try/catch — failure reverts the individual transaction, never halts block processing

### 2C — Per-Asset Decoy Pool Design

The existing `CommitmentIndex::getRandomCommitmentOutputs()` selects decoys by amount tier only. Multi-asset needs per-asset filtering so HEAT ring signatures draw decoys from HEAT outputs, not XFG outputs. Cross-asset decoy contamination would reveal the real output's asset type.

Modify `src/CryptoNoteCore/CommitmentIndex.h/cpp`:
- Add `getRandomCommitmentOutputsForAsset(amount, count, assetId)` — returns decoys from outputs matching both the tier and assetId
- The existing `getRandomCommitmentOutputs()` call chain in wallet transaction construction must pass `assetId` through

**This is a privacy-critical change.** Mixing XFG and HEAT outputs in the same ring signature would break indistinguishability.

### 2D — Privacy Unification

- COLD deposits already share the unified decoy pool with regular outputs (verified in Phase 0C)
- HEAT burn-to-mint deposits join the same `0xD5` encrypted payload system
- All deposit types produce outputs indistinguishable on-chain — differentiated only by the encrypted `depositType` inside the 45-byte `DepositSecretPayload`
- HEAT outputs use `HEAT_TIER_0..3` for ring signature decoy selection (separate from XFG's `AMOUNT_TIER_0..3`)

### 2E — Fail-Safe Isolation (Mandatory Consensus Rules)

1. XFG `MONEY_SUPPLY` (80,000,088,000,008) remains an immutable hard cap — HEAT minting burns XFG (never alters the cap). Burned XFG recycles into block rewards via EternalFlame.
2. HEAT state failure (any exception in `HeatMintEngine`) reverts the individual transaction — block processing continues
3. If HEAT is ever abandoned, XFG transfers, atomic swaps, and COLD CDs operate without degradation

### Verify Phase 2

- Create HEAT via burn: burn 1 XFG at 0.5 redemption price, verify 2 HEAT minted with `assetId = HEAT`
- Verify `ethereal_xfg` incremented by the burned amount
- Verify HEAT supply increased by minted amount
- Decoy pool isolation: HEAT ring signature only draws HEAT-tier outputs; XFG ring signature only draws XFG-tier outputs
- Fail-safe: inject corrupted HEAT state, verify XFG consensus continues unaffected
- Reorg: push/pop blocks with HEAT mint transactions, verify supply and burn tracking is consistent

---

## Phase 3: Hearth — Consensus-Embedded AMM

**Duration: 2-3 weeks | New files: 3 | Consensus-breaking**

A single XFG/HEAT constant-product AMM pool (`X * Y = K`) embedded in consensus rules. Named "Hearth" — the central fireplace where XFG and HEAT meet. Provides the on-chain price feed the PI controller needs and enables HEAT liquidity without external exchanges.

### 3A — AMM State

Create:
| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/AmmPool.h` | Pool state struct, consensus validation interface |
| `src/CryptoNoteCore/AmmPool.cpp` | Pool state transitions, delegates math to existing `PoolAMM.h` |
| `tests/UnitTests/AmmPoolTests.cpp` | AMM math + consensus validation tests |

**Pool state:**
```cpp
struct AmmPoolState {
    uint64_t reserveXfg;        // XFG reserve (public)
    uint64_t reserveHeat;       // HEAT reserve (public)
    uint64_t totalLpShares;     // total LP token supply
    uint64_t feeAccumulator;    // accumulated XFG fees → routed to epoch CD pool
};
```

**Reuse existing math** from `src/SwapDaemon/PoolAMM.h` (already compiled in current build, proven):
- `poolGetOutputAmount(inputAmount, reserveIn, reserveOut, feeBps)` — swap calculation
- `poolGetInputAmount(outputAmount, reserveIn, reserveOut, feeBps)` — reverse swap quote
- `poolMintLPShares(amountA, amountB, totalShares, reserveA, reserveB)` — LP share minting
- `poolGetWithdrawalAmounts(burn, totalShares, reserveA, reserveB, feeAccA, feeAccB)` — proportional withdrawal
- `poolGetSpotPrice(reserveA, reserveB)` — price feed for PI controller (returns `price * 1e18`)
- `poolValidateSwap(input, output, reserveIn, reserveOut, feeBps)` — invariant validation
- `poolValidateDepositRatio(amountA, amountB, reserveA, reserveB, toleranceBps)` — deposit ratio check

The `AmmPool` module wraps these functions in consensus-validated state transitions. The math functions themselves are not duplicated.

### 3B — Transaction Extra Tags

Add to `src/CryptoNoteCore/TransactionExtra.h`:
```cpp
#define TX_EXTRA_AMM_SWAP       0xF0   // AMM swap
#define TX_EXTRA_AMM_ADD_LIQ    0xF1   // Add liquidity
#define TX_EXTRA_AMM_REM_LIQ    0xF2   // Remove liquidity
```

New structs:
```cpp
struct TransactionExtraAmmSwap {
    uint8_t  direction;     // 0 = XFG→HEAT, 1 = HEAT→XFG
    uint64_t inputAmount;
    uint64_t minOutput;     // slippage protection
};

struct TransactionExtraAmmAddLiquidity {
    uint64_t amountXfg;
    uint64_t amountHeat;
};

struct TransactionExtraAmmRemoveLiquidity {
    uint64_t lpSharesBurned;
    uint64_t minAmountXfg;  // slippage protection
    uint64_t minAmountHeat;
};
```

Modify `src/CryptoNoteCore/TransactionExtra.cpp`:
- Add parsing for tags 0xF0, 0xF1, 0xF2
- Add structs to `TransactionExtraField` boost::variant typedef

### 3C — Consensus Validation & Serialization

Modify `src/CryptoNoteCore/Blockchain.h`:
- Add `AmmPoolState m_ammPool` member
- Add serialization of `m_ammPool` in `Blockchain::save()` / `load()`

Modify `src/CryptoNoteCore/Blockchain.cpp`:
- In `pushTransaction()` after input processing:
  - Parse tx_extra for AMM tags
  - `TX_EXTRA_AMM_SWAP`: validate `minOutput` achievable, execute swap via `AmmPool`, update reserves, deduct fee, route XFG fee to `m_currentEpochSwapFees`
  - `TX_EXTRA_AMM_ADD_LIQ`: validate deposit ratio within ±1% tolerance, mint LP shares, update reserves
  - `TX_EXTRA_AMM_REM_LIQ`: validate LP share ownership, burn shares, return proportional reserves
- In `popBlock()`: reverse AMM state changes using stored pre-state deltas
- In epoch boundary: AMM XFG fees flow into `m_currentEpochSwapFees` alongside atomic swap fees
- **Fail-safe**: AMM validation failure rejects the transaction, never halts block processing
- **Transaction ordering**: Txs within a block are processed in canonical order — AMM swaps are order-dependent but deterministic and identical across all nodes

### 3D — LP Token Tracking

LP shares tracked as `TransactionOutputUnified` outputs with `assetId = XFG` and a reserved term code:
```cpp
// Add to CryptoNoteConfig.h:
const uint32_t DEPOSIT_TERM_LP = 0xFFFFFFFD;  // LP share marker
```

Spending LP shares (to remove liquidity) uses the standard `TransactionInputUnified` mechanism. LP share amounts are derivable from public pool reserves, so ring signatures provide identity privacy (hides *who* provided liquidity), not amount privacy.

### 3E — Pool Bootstrapping

At v11 fork activation height, Hearth is seeded at protocol-defined ratio:
```cpp
const uint64_t HEARTH_INITIAL_XFG_RESERVE  = 1'000'000'000;  // 100 XFG in atomic units
const uint64_t HEARTH_INITIAL_HEAT_RESERVE = 2'000'000'000;  // 200 HEAT (at 0.5 XFG/HEAT)
```

The initial LP shares (sqrt(reserveXfg * reserveHeat)) are sent to a burn/null address to prevent front-running genesis liquidity.

### 3F — Privacy Model

- Pool reserves are **public** — necessary for price calculation, cannot be hidden
- Individual swap amounts are **public in v1** (swap output is deterministic from input + public reserves, hiding input provides minimal privacy gain)
- LP additions/removals: amounts visible (proportional to public reserves)
- User identity: hidden via standard ring signatures

### Verify Phase 3

- Constant product invariant: `reserveXfg_new * reserveHeat_new >= reserveXfg_old * reserveHeat_old` after every swap
- Slippage protection: swap rejected when output < `minOutput`
- LP share proportionality: add/remove liquidity produces correct share ratios
- Fee routing: AMM swap fees appear in `m_currentEpochSwapFees` at epoch boundary
- State survives daemon restart: serialize, restart, verify pool state intact
- Reorg: push/pop blocks with AMM transactions, verify reserve state consistency
- Multi-swap block: 3 swaps in one block produce correct cumulative state

---

## Phase 4: PI Controller (Epoch-Based Redemption Rate)

**Duration: 1-2 weeks | New files: 3**

The "central bank algorithm." Runs at every epoch boundary (900 blocks mainnet, 10 blocks testnet). Adjusts the HEAT redemption price based on market price deviation. This is the sole stability mechanism — proven by RAI's years of operation with only a PI controller.

### 4A — Core Algorithm

Create:
| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/PiController.h` | Controller interface |
| `src/CryptoNoteCore/PiController.cpp` | PI math using Q64.64 FixedPoint |
| `tests/UnitTests/PiControllerTests.cpp` | Stability + determinism tests |

```
Error = (MarketPrice - RedemptionPrice) / RedemptionPrice

RedemptionRate = Kp * Error + Ki * IntegralError

NewRedemptionPrice = RedemptionPrice * (1 + RedemptionRate * BlocksElapsed / BlocksPerYear)
```

- `MarketPrice` = `poolGetSpotPrice(m_ammPool)` — Hearth spot price
- `RedemptionPrice` = persisted Q64.64 state
- `IntegralError` = accumulated Q64.64 state (clamped to prevent wind-up)
- All math uses `FixedPoint64` — zero floating-point

### 4B — Constants

Add to `src/CryptoNoteConfig.h`:
```cpp
// PI Controller — hard-coded at launch, not governance-adjustable
const uint64_t PI_KP_NUM = 2;
const uint64_t PI_KP_DENOM = 100;          // Kp = 0.02
const uint64_t PI_KI_NUM = 2;
const uint64_t PI_KI_DENOM = 10000;        // Ki = 0.0002
const uint64_t PI_MAX_RATE_NUM = 50;
const uint64_t PI_MAX_RATE_DENOM = 100;    // Max ±50% annual rate
const uint64_t PI_INTEGRAL_CLAMP_NUM = 100;
const uint64_t PI_INTEGRAL_CLAMP_DENOM = 100;  // Integral wind-up limit

const uint64_t BLOCKS_PER_YEAR = 65700;    // ~180 blocks/day × 365
```

These are initial values sourced from RAI's proven parameters, scaled for Fuego's supply and block time. Monte Carlo simulation (Phase 7) validates or adjusts them.

### 4C — Integration

Modify `src/CryptoNoteCore/Blockchain.h`:
- Add `FixedPoint64 m_heatRedemptionPrice`, `m_heatIntegralError`, `m_heatRedemptionRate` (all serialized with blockchain state)
- Add `PiController m_piController` member
- Initialize `m_heatRedemptionPrice` from `HEAT_INITIAL_REDEMPTION_PRICE` constants

Modify `src/CryptoNoteCore/Blockchain.cpp`:
- At epoch boundary (~line 3240, **before** fee distribution): read Hearth spot price, call `PiController::calculateRedemptionRate()`, update `m_heatRedemptionPrice`, store result
- This ordering ensures the PI controller reads the price *before* the epoch's HEAT CD buyback affects it
- Serialize/deserialize PI state alongside other blockchain state in save()/load()

Modify `src/CryptoNoteCore/CommitmentIndex.h`:
- Add `uint64_t redemptionPriceFixedPoint` and `uint64_t redemptionRateFixedPoint` fields to `EpochReport`

### Verify Phase 4

- Stability: simulate 1,000 epochs of random price shocks, verify convergence to equilibrium
- Boundary: rate clamped at `PI_MAX_RATE`, integral clamped at `PI_INTEGRAL_CLAMP`
- Determinism: identical inputs produce identical outputs across architectures
- No floating-point: grep PiController for `double`/`float` — must find zero
- Serialization: PI state survives daemon restart
- Epoch boundary ordering: PI reads price before HEAT CD buyback modifies it

---

## Phase 5: HEAT-Denominated CDs & PI-Modulated Buyback

**Duration: 1-2 weeks**

The PI controller modulates how much XFG from swap fees is used to buy HEAT for CD yield distribution, creating the feedback loop that stabilizes HEAT:

- HEAT too expensive → negative rate → less XFG buys HEAT → less HEAT demand → price drops
- HEAT too cheap → positive rate → more XFG buys HEAT → more HEAT demand → price rises

### 5A — HEAT-Denominated CDs

Users can create CDs denominated in HEAT using the same term structure as XFG CDs. HEAT CDs are distinguished by `assetId = HEAT` in the `TransactionOutputUnified`.

**HEAT CD amount tiers** (add to `CryptoNoteConfig.h`):
```cpp
const uint64_t HEAT_CD_TIER_0 =  16,000,000;    // 1.6 HEAT
const uint64_t HEAT_CD_TIER_1 = 160,000,000;     // 16 HEAT
const uint64_t HEAT_CD_TIER_2 = 1,600,000,000;   // 160 HEAT
const uint64_t HEAT_CD_TIER_3 = 16,000,000,000;  // 1600 HEAT
```

### 5B — Epoch Boundary Buyback (Core Mechanism)

At each epoch boundary, the accumulated swap fees follow this pipeline:

```
1. m_currentEpochSwapFees (XFG) accumulated from atomic swaps + Hearth AMM
2. Split: 80% → CD yield pool | 20% → Treasury (existing, unchanged)
3. Split CD yield pool between XFG CDs and HEAT CDs:
     xfgCdShare = xfgLocked / (xfgLocked + heatLocked × redemptionPrice) × cdPool
     heatCdShare = cdPool - xfgCdShare
4. PI modifier applied to HEAT CD share:
     yieldModifier = 1.0 + clamp(redemptionRate, -1.0, 2.0)
     adjustedHeatShare = heatCdShare × yieldModifier     (floor at 0)
5. Protocol executes AMM swap on Hearth: XFG → HEAT
     heatReceived = poolGetOutputAmount(adjustedHeatShare, reserveXfg, reserveHeat, feeBps)
     reserveXfg += adjustedHeatShare
     reserveHeat -= heatReceived
6. heatReceived → HEAT CD yield pool
   xfgCdShare → XFG CD yield pool (existing)
7. Record fee rates for both assets in CommitmentIndex
8. Reset epoch accumulator
```

**What if Hearth has insufficient HEAT?** The AMM swap always executes — `poolGetOutputAmount()` handles any reserves. If liquid HEAT is scarce, slippage is high (few HEAT received per XFG), creating extreme incentive for arbitrageurs to deposit HEAT. The mechanism is self-correcting. No special fallback needed.

**Why this works:**
- Single fee pool (XFG) eliminates bootstrapping problem — atomic swap fees exist pre-v11
- Every epoch creates guaranteed HEAT demand (structural backstop)
- PI controller modulates buy pressure (stronger signal than yield rate modulation alone)
- Treasury 20% share is unchanged — treasury accumulates XFG as before

### 5C — Yield Currency Rule

| CD Type | Yield Currency | Fee Source |
|---------|---------------|------------|
| XFG CD | XFG | Direct share of XFG swap fee pool |
| HEAT CD | HEAT | XFG CD share buys HEAT from Hearth at epoch boundary |

### 5D — Behavior Table

| HEAT Market | Redemption Rate | HEAT CD Buyback | XFG CD Yield | Effect |
|---|---|---|---|---|
| HEAT too expensive | Negative | Reduced (< 1.0×) | Unchanged | Less protocol HEAT buying → more HEAT on market |
| HEAT at target | Zero | Normal (1.0×) | Unchanged | Steady state |
| HEAT too cheap | Positive | Boosted (> 1.0×) | Unchanged | More protocol HEAT buying → HEAT supply absorbed |

**Constraint:** Yield modifier is floored at 0.0 — CDs never lose value.

### Modify

| File | Change |
|------|--------|
| `src/CryptoNoteConfig.h` | Add `HEAT_CD_TIER_0..3` constants, `HEAT_CD_YIELD_MODIFIER_MIN = 0` |
| `src/CryptoNoteCore/Blockchain.cpp` | Implement the epoch boundary buyback pipeline at ~line 3240. Add `m_heatCdYieldPool` state variable (serialized). |
| `src/CryptoNoteCore/CommitmentIndex.h/cpp` | Add separate per-epoch tracking for XFG-locked CDs vs HEAT-locked CDs. Store yield modifier in `EpochReport`. |
| `src/CryptoNoteCore/Currency.h/cpp` | Extend `calculateCdInterest()` to handle HEAT-denominated CDs with asset-specific accumulated yields. |

### Verify Phase 5

- Positive redemption rate → `yieldModifier > 1.0` → more XFG used to buy HEAT
- Negative rate → `yieldModifier < 1.0` but ≥ 0.0 → less XFG used to buy HEAT
- XFG CD yields are completely unaffected by PI controller
- HEAT CD creation at all four HEAT_CD_TIER amounts
- HEAT CD withdrawal pays HEAT (not XFG)
- Epoch boundary buyback correctly updates Hearth reserves
- Reorg: push/pop epochs with buybacks, verify HEAT CD yield pool and reserves are consistent
- Edge case: no HEAT CDs exist → entire 80% CD pool goes to XFG CDs (no buyback)
- Edge case: no XFG CDs exist → entire 80% CD pool (adjusted by PI) buys HEAT

---

## Phase 6: RPC, Wallet, and TUI Integration

**Duration: 2-3 weeks**

### 6A — Daemon RPC

Modify `src/Rpc/CoreRpcServerCommandsDefinitions.h` — add structs:

| Endpoint | Returns |
|---|---|
| `/get_heat_metrics` | `redemption_price`, `market_price`, `redemption_rate`, `error`, `integral_error`, `heat_supply` |
| `/amm_quote` | Params: `input_amount`, `direction`. Returns: `expected_output`, `price_impact_bps`, `fee` |
| `/amm_pool_info` | `reserve_xfg`, `reserve_heat`, `total_lp_shares`, `spot_price`, `fee_accumulator` |

Modify `src/Rpc/RpcServer.h/cpp` — register handlers.
Modify `src/CryptoNoteCore/ICore.h` — add virtual methods for AMM/HEAT queries.

### 6B — Wallet Backend (Multi-Asset Pipeline)

The current transaction construction pipeline assumes XFG-only. Multi-asset support requires asset-awareness throughout the tx building chain.

Modify `src/Wallet/WalletGreen.h/cpp`:
- `getHeatBalance()` — scan wallet outputs for `assetId = HEAT`
- `createHeatBurnTransaction(xfgAmount)` — select XFG inputs, produce HEAT output with `assetId = HEAT` and burn receipt in tx_extra
- `createAmmSwapTransaction(direction, amount, minOutput)` — AMM swap with slippage
- `createAddLiquidityTransaction(amountXfg, amountHeat)` — LP deposit
- `createRemoveLiquidityTransaction(lpShares, minXfg, minHeat)` — LP withdrawal
- `createHeatCdDeposit(amount, term)` — HEAT-denominated CD creation

Modify `src/WalletLegacy/WalletTransactionSender.h/cpp` — input selection pipeline:
- `selectTransfersToSend()` must filter by `assetId` for the target asset
- `prepareKeyInputs()` must pass `assetId` through to decoy selection
- `getRandomCommitmentOutsRequest()` must request per-asset decoys via `getRandomCommitmentOutputsForAsset()`
- `splitDestinations()` must use the correct asset's denomination tiers (XFG tiers vs HEAT tiers)

Modify `src/Wallet/WalletUserTransactionsCache.h/cpp`:
- Track per-asset balance pools: actual (spendable), pending (unconfirmed), locked (CD), unlocked (CD matured)

### 6C — SimpleWallet CLI

Add commands to `src/SimpleWallet/SimpleWallet.cpp`:

| Command | Action |
|---|---|
| `mint_heat <xfg_amount>` | Burn XFG permanently, mint HEAT at current redemption price |
| `swap <direction> <amount> <min_output>` | AMM swap (`xfg_to_heat` or `heat_to_xfg`) |
| `add_liq <xfg> <heat>` | Add liquidity to Hearth |
| `remove_liq <lp_shares> <min_xfg> <min_heat>` | Remove liquidity, get proportional XFG + HEAT |
| `deposit_heat <amount> <term>` | Create HEAT-denominated CD |
| `heat_info` | Show HEAT metrics (redemption price, rate, supply) |
| `pool_info` | Show Hearth reserves, spot price, fees |
| `balance` (extended) | Show XFG balance + HEAT balance |

### 6D — TUI (Go)

Modify `tui/`:
- HEAT minting screen: burn XFG, display HEAT amount at current redemption price
- Hearth swap screen: select direction, input amount, view quote with price impact, confirm
- Hearth pool dashboard: reserves, spot price, volume
- Extend `show_balance` to include HEAT balance

Modify `swapxfg/`:
- Add `PairHEAT` to the pairs system (Hearth view)
- Integrate RPC calls for `amm_quote` and `get_heat_metrics`

### Verify Phase 6

- RPC endpoints return data matching blockchain state
- Wallet commands produce valid transactions passing consensus validation
- Balance tracking correctly separates XFG/HEAT (no cross-contamination of asset pools)
- TUI pool info matches RPC response
- End-to-end: CLI `mint_heat 1` → daemon processes → TUI shows updated HEAT balance

---

## Phase 7: Integration Testing, Simulation & Security Audit

**Duration: 3-5 weeks**

### 7A — Testnet Deployment

- Use `TESTNET_EPOCH_DURATION_BLOCKS = 10` for fast epoch cycling
- Set `UPGRADE_HEIGHT_V11` to a low testnet block (e.g., 30) for fast activation
- Deploy 3+ nodes with mining enabled
- Verify identical blockchain state after 1,000 blocks with mixed transactions:
  - XFG transfers
  - HEAT minting via XFG burn
  - Hearth AMM swaps (both directions)
  - LP additions and removals
  - HEAT CD creation and withdrawal (after sufficient epochs)
  - Atomic swaps (XFG → ETH/SOL/XMR/BCH)
- Verify reorg handling: intentionally fork the chain, verify all nodes converge

### 7B — Monte Carlo Simulation

Simulate 10,000 epochs of random market conditions:

1. **PI controller convergence**: Does HEAT peg deviation decrease over time under varied price shocks?
2. **Extreme scenarios**: 90% price shock up/down, zero Hearth liquidity, all liquidity in CDs
3. **Initial redemption price sweep**: Test 0.25, 0.5, 1.0, 2.0 XFG/HEAT starting points under identical conditions. Select the value that minimizes peg deviation during the first 100 epochs. Adjust `HEAT_INITIAL_REDEMPTION_PRICE` before mainnet if simulations reveal a better default than 0.5.
4. **HEAT CD buyback feedback**: Verify PI-modulated buy pressure effectively pulls HEAT off Hearth during undersupply and releases it during oversupply.
5. **Property-based tests**: Random walk price series, fuzzed PI parameter ranges
6. **EternalFlame recycling**: Verify that burned XFG re-entering circulation via block rewards doesn't destabilize the system at scale

### 7C — Consensus Fork Detection

- Run 3+ nodes with identical blockchain from genesis
- Submit identical transaction sequences via scripted RPC calls
- Assert byte-identical blockchain state across all nodes after each block
- Test cross-platform: x86_64 node + ARM64 node produce identical state (critical for FixedPoint determinism)

### 7D — Security Audit Focus Areas

| Area | Specific Concerns |
|---|---|
| **HEAT minting** | Inflation: more HEAT minted than XFG burned; integer overflow in redemption price division; reorg double-mint; `assetId` field injection (non-v11 tx claiming v11 assetId) |
| **EternalFlame** | Burned XFG double-counted (both in BankingIndex and HEAT supply tracking); `m_ethereal_xfg` overflow on extreme burn volume; recycling ratio correctness after v11 fork |
| **Hearth AMM** | Constant-product invariant violations; sandwich attacks (same-block ordering manipulation); division-by-zero on empty pool; precision loss on extreme reserve ratios; reorg reverting AMM deltas incorrectly |
| **PI Controller** | Death spiral: positive feedback loop where rate adjustment worsens peg; integral wind-up overflow; Q64.64 precision errors compounding over epochs; rate calculation ordering relative to buyback |
| **CD yield & buyback** | Yield modifier overflow/underflow; HEAT buyback draining Hearth entirely; epoch boundary crash halting chain; yield modifier applied to wrong CD type (HEAT modifier affecting XFG CDs) |
| **Privacy** | Cross-asset decoy pool contamination (HEAT decoy in XFG ring sig or vice versa); `assetId` leak in unencrypted tx fields; LP share amount derivability from public reserves |
| **Consensus** | AMM state divergence on reorg; PI state mismatch between nodes; v11 fork activation race condition; nodes running pre-v11 rejecting v11 blocks |
| **Serialization** | Missing fields in pre-v11 → post-v11 state migration; corrupted AMM or PI state on load; state size growth from new fields exceeding reasonable bounds |

### 7E — Documentation

- **v11 hard fork specification**: All consensus rule changes, new tx types, new constants, upgrade height
- **HEAT economics paper**: PI controller derivation, parameter justification, Monte Carlo results, EternalFlame recycling analysis
- **Node operator upgrade guide**: Pre-v11 → post-v11 migration steps, state compatibility notes
- **Wallet user guide**: HEAT minting, Hearth swapping, LP provision, CD management

---

## File Inventory

### New Files (11 total)

| File | Phase | Purpose |
|------|-------|---------|
| `src/Common/FixedPoint.h` | 1 | Q64.64 type definition |
| `src/Common/FixedPoint.cpp` | 1 | exp/ln/mul/div implementations |
| `src/CryptoNoteCore/AssetId.h` | 2 | Asset type enum |
| `src/CryptoNoteCore/HeatMintEngine.h` | 2 | HEAT minting consensus rules |
| `src/CryptoNoteCore/HeatMintEngine.cpp` | 2 | Implementation |
| `src/CryptoNoteCore/AmmPool.h` | 3 | Hearth pool state + validation |
| `src/CryptoNoteCore/AmmPool.cpp` | 3 | Pool state transitions |
| `src/CryptoNoteCore/PiController.h` | 4 | PI controller interface |
| `src/CryptoNoteCore/PiController.cpp` | 4 | PI algorithm implementation |
| `tests/UnitTests/FixedPointTests.cpp` | 1 | Cross-platform math tests |
| `tests/UnitTests/HeatMintTests.cpp` | 2 | HEAT lifecycle tests |
| `tests/UnitTests/AmmPoolTests.cpp` | 3 | AMM math + consensus tests |
| `tests/UnitTests/PiControllerTests.cpp` | 4 | Controller stability tests |

### Modified Files (13 key files)

| File | Phases | Changes |
|------|--------|---------|
| `src/CryptoNoteConfig.h` | 0-5 | V11 height, HEAT tiers, HEAT CD tiers, PI gains, LP marker, initial redemption price, Hearth seed amounts, yield modifier clamp |
| `include/CryptoNote.h` | 2 | `assetId` field in `TransactionOutputUnified` and `TransactionInputUnified` |
| `src/CryptoNoteCore/CryptoNoteSerialization.cpp` | 2 | Serialize `assetId` for v11+ |
| `src/CryptoNoteCore/TransactionExtra.h/cpp` | 3 | Tags 0xF0/0xF1/0xF2 + structs + parsing + boost::variant entries |
| `src/CryptoNoteCore/Blockchain.h/cpp` | 2-5 | Hearth state, PI state, HEAT supply, epoch boundary buyback, CD yield distribution, fail-safes, serialization |
| `src/CryptoNoteCore/Currency.h/cpp` | 0,2,5 | V11 upgrade height + testnet height, HEAT supply methods, HEAT CD interest calculation |
| `src/CryptoNoteCore/CommitmentIndex.h/cpp` | 2,4,5 | Per-asset decoy selection, EpochReport extensions, HEAT CD tracking, redemption price/rate fields |
| `src/CryptoNoteCore/ICore.h` | 6 | AMM/HEAT query virtual methods |
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | 6 | New RPC command structs (heat_metrics, amm_quote, amm_pool_info) |
| `src/Rpc/RpcServer.h/cpp` | 6 | Endpoint handlers |
| `src/Wallet/WalletGreen.h/cpp` | 6 | Multi-asset tx builders + per-asset balance queries |
| `src/WalletLegacy/WalletTransactionSender.h/cpp` | 6 | Per-asset input selection, per-asset decoy requests, per-asset denomination splitting |
| `src/SimpleWallet/SimpleWallet.cpp` | 6 | New CLI commands |
| `src/CMakeLists.txt` | 1-4 | New source files |

### Go Files Modified

| File | Changes |
|------|---------|
| `tui/tview_main.go` | HEAT minting screen, Hearth swap screen, pool dashboard, extended balance display |
| `swapxfg/app/pairs.go` | Add `PairHEAT` constant and hotkey |
| `swapxfg/app/tui.go` | Hearth view with AMM quote integration |

---

## Summary Timeline

```
Phase 0  ██                           v11 Fork Scaffolding        (3-5 days)
Phase 1  █████                        FixedPoint Math             (1-2 weeks)
Phase 2  ███████                      HEAT Colored Coin           (2-3 weeks)
Phase 3  ███████                      Hearth AMM                  (2-3 weeks)
Phase 4  █████                        PI Controller               (1-2 weeks)
Phase 5  █████                        HEAT CDs + Buyback          (1-2 weeks)
Phase 6  ███████                      RPC / Wallet / TUI          (2-3 weeks)
Phase 7  ██████████                   Testnet + Simulation        (3-5 weeks)
         ───────────────────────────────────────────────────
         Total: ~14-24 weeks solo
```

## Deferred to Post-Launch (v12+)

| Feature | Rationale |
|---------|-----------|
| **Path B CDP loans** | Requires ZK event-based unlocks, collateral ratio tracking, liquidation mechanics. Complex, high-risk. |
| **Maelisandra / wHEAT bridge** | EVM contract for HEAT on Ethereum. Depends on stable L1 HEAT first. |
| **JIT Zaps** | One-transaction onboarding into HEAT + LP positions. Optimization, not core stability. |
| **ZK-private AMM swaps** | Hide individual swap amounts. Requires ZK proof infrastructure (STARKs) verified on-chain. |
| **PI controller governance** | Mechanism for tuning Kp/Ki if needed. Requires multi-sig + timelock + careful attack surface analysis. |
| **HEAT-based UI pricing shift** | Default UI to HEAT as unit of account. Cosmetic but critical for mental model: HEAT is money, XFG is collateral. |

---

## References

- **EternalFlame mechanism**: `src/CryptoNoteCore/Currency.cpp:239-260`, `BankingIndex.cpp:186-214`
- **Epoch boundary logic**: `src/CryptoNoteCore/Blockchain.cpp:3223-3305`
- **Existing AMM math**: `src/SwapDaemon/PoolAMM.h` (compiled, proven)
- **COLD deposit system**: `src/CryptoNoteCore/DepositCommitment.h:86-90`, `TransactionExtra.h:217-221`
- **Commitment index**: `src/CryptoNoteCore/CommitmentIndex.h:54-78`
- **Transaction types**: `include/CryptoNote.h:96-118`
- **Tier constants**: `src/CryptoNoteConfig.h:125-133`
- **Fee constants**: `src/CryptoNoteConfig.h:140-149`
- **RAI stability model**: Reflexer Finance (PI-controlled redemption rate, no volatility sink at launch)
