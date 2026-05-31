# One-Way HEAT — System Redesign

> Removes HEAT→XFG redemption. Collateral Reserve eliminated. 95% of mint capital deployed productively through SWF. Maximum APY for CD holders. 8M8 supply cap preserved. Eternal Flame recycling maintained.

---

## 1. The Four Pools

Collateral Reserve is eliminated. Its 95% of mint proceeds is merged into the SWF, which becomes the primary capital pool.

| Pool | Type | Purpose | Supply Impact |
|---|---|---|---|
| **SWF** | Blockchain state | Deployable capital: receives 95% of mint + 5% fees + LP yield. Seeds AMM LP, lends against CDs, generates yield stream for CD holders. | Neutral (deployed existing coins) |
| **Treasury** | Blockchain state | Peg defense + LP bootstrapping + emergency backstop. Funded by 15% fees + arb profits + SWF overflow. | Neutral |
| **YEM Reserve** | Paper credit (YemState) | 8% of mint premium. Authorizes coinbase minting for bond coupons + deficit coverage. | Counted as emission when realized |
| **Eternal Flame** | Counter (BankingIndex) | 92% of mint premium. Recycled via Osavvirsak → block rewards. | Reduces circulation, increases future emission |

---

## 2. One-Way HEAT Mint Flow

```
User pays: N × peg_price × (1 + premium_bps/10000) XFG
           for N HEAT at peg_price = HEAT_PEG / oracle_xfg_price

Example (XFG=$3.50, HEAT peg=$1.58, 2% premium):
  peg_price = 1.58 / 3.50 = 0.4514 XFG per HEAT
  user pays 0.4514 × 1.02 = 0.4604 XFG per HEAT
    ├── 0.4514 (98%) → SWF
    └── 0.0090  (2%) → Premium
           ├── 92% → Eternal Flame (0.0083 XFG/HEAT)
           └──  8% → YEM Reserve (0.0007 XFG/HEAT credit)
```

**Key change**: The 98% base goes to SWF (not a dead Collateral Reserve). The SWF deploys it productively — AMM LP positions generate swap fees, CD lending generates interest. Yield flows back to CD holders.

**Premium reduced**: 2% instead of 5%. With no redemption, HEAT is less useful — a lower premium keeps minting economically attractive. The 92/8 split within the premium is preserved (EF recycling + YEM Reserve).

---

## 3. No HEAT→XFG Burn

HEAT is mint-only. There is no `validateHeatBurn`, no redemption price check, no HEAT supply decrease. HEAT supply is monotonically increasing.

Consequences:
- No burn premium revenue for Treasury (was 5% of redeemed amount in two-way model)
- No Collateral Reserve to manage
- No cost-basis tracking per HEAT
- HEAT is a one-way synthetic — you can enter, you cannot exit via protocol

---

## 4. Peg Maintenance (Asymmetric)

### Above Peg (HEAT > $1.58)

Self-correcting via mint arbitrage:

```
If HEAT market price > mint_price (= peg × 1.02):
  Arb: mint HEAT at 1.02 × peg, sell at market_price
  Profit per HEAT = market_price - peg × 1.02
  As arb mints → HEAT supply increases → market price pushes down toward peg
```

The mint premium creates a natural band. HEAT can trade between ~peg and ~peg × 1.02 without triggering arb. Above 1.02, arb is profitable and brings it down.

### Below Peg (HEAT < $1.58)

No protocol floor mechanism. Treasury intervenes with market buys:

```
Every epoch (if HEAT < peg_threshold):
  treasury_allocation = min(
    SWF_balance × 0.001,        // 0.1% of SWF per epoch
    Treasury_balance × 0.01     // 1% of Treasury per epoch
  )
  Treasury: buy HEAT on open market with XFG
  Effect: reduces HEAT supply (burn purchased HEAT), increases demand
```

The system CANNOT guarantee the peg floor. HEAT relies on:
1. **DeFi utility demand** — HEAT usable on Mode 3 (lending, LP, leverage)
2. **Yield premium** — holding HEAT earns CD-like yield (HEAT holders get a portion of SWF yield)
3. **Treasury market operations** — buys when HEAT is below peg, gradually withdraws supply
4. **Mint cost floor** — no one mints below peg, so supply growth pauses during discounts

### Zero-Redemption Risk

If HEAT drops and stays below peg:
- Mint volume pauses (no incentive to mint at a premium)
- Premium flow to EF + YEM Reserve pauses
- System runs on existing SWF yield + swap fees
- Treasury gradually absorbs HEAT supply through market buys
- When HEAT rises above peg, minting resumes, EF/YEM restart

This is a **softer circuit breaker** than two-way redemption runs — no solvency crisis from mass redemption, just a revenue drought.

---

## 5. SWF — The Deployable Capital Engine

This is the core APY multiplier. In the two-way model, 95% of mint proceeds sat idle in the Collateral Reserve. In the one-way model, it enters the SWF and works.

### Capital Sources

```
SWF_balance =
  + Σ(80% of mint base)                    // every HEAT mint
  + Σ(5% of swap fees)                      // direct fee allocation
  + Σ(60% of CD surplus)                    // YEM smoothing overflow
  + Σ(75% of LP yield)                      // passive AMM income
  + Σ(bond principal)                       // legacy migration (one-time)

Drains:
  - SWF_drip_to_CD_holders (0.5%/epoch direct)
  - deficit_smoothing_draw
  - bond_maturity_repayment (principal portion)
  - treasury_overflow (10% of SWF growth/epoch)
```

### Deployment Strategy

```
SWF portfolio:
  ├── 75% → AMM LP positions (XFG/HEAT, XFG/fXFG, XFG/FLX pools)
  │        Generates: swap fees (0.5-2% APR typically)
  │        Feeds: 75% → SWF, 25% → Treasury
  │
  ├── 20% → CD collateral lending
  │        Generates: interest on CD-backed loans
  │        Feeds: 100% → SWF
  │
  └──  5% → Liquid reserves (for peg defense ops)
```

The SWF LP yield is the primary new revenue source. In the two-way model, 75% of LP yield went to SWF — but SWF was small. In the one-way model, SWF is large (it receives all mint capital), so the absolute LP yield is much higher.

### SWF Drip

```
per_epoch_drip = SWF_balance × SWF_DRIP_BPS / 10000

Each CD holder receives proportional to their locked amount:
  holder_drip = per_epoch_drip × (holder_locked / total_cd_locked)
```

This is identical to the two-way model — but the absolute drip amount is much larger because SWF is funded by mint capital.

---

## 6. CD Yield Engine

Identical mechanics to YEM v3, but with a larger SWF feeding the drip.

### APY Composition (One-Way vs Two-Way)

| Component | Two-Way | One-Way | Why |
|---|---|---|---|
| Base yield (swap fees 80%) | 3-5% | 3-5% | Same — swap fee revenue unaffected |
| SWF drip | 0.1-0.5% | **2-8%** | SWF is 20-50× larger (receives mint capital) |
| CD lending yield | 0% | 0.5-1% | SWF lends against CDs |
| Bond pass-through | varies | varies | Same |
| **Total** | **4-7%** | **6-14%** | |

The APY ceiling is no longer constrained by swap fee volume alone. The SWF generates yield from its deployed capital base, which grows with every HEAT mint.

### Drip Calibration

With the SWF absorbing 98% of every mint (the base), SWF grows proportionally to HEAT demand. The drip rate must be calibrated to avoid overpaying CDs:

```
SWF_DRIP_BPS = max(25, min(100, target_CD_APY / SWF_balance_ratio))

SWF_balance_ratio = SWF_balance / total_cd_locked
```

If SWF = 1M and CD locked = 100K (ratio = 10), a 50 BPS drip distributes 50K/epoch = 50% of CD locked per epoch. Too high. The drip must be bounded.

**Recommended: cap SWF_DRIP_BPS at 25 (0.25%/epoch)** and recalculate dynamically based on SWF:CD ratio.

Better formulation:

```
base_drip = floor(SWF_balance × 25 / 10000)     // 0.25% of SWF
max_drip  = floor(CD_locked × CD_APY_MAX / EPY)  // don't exceed target APY
actual_drip = min(base_drip, max_drip)
holder_drip = actual_drip × (holder_locked / total_cd_locked)
```

This ensures CD yield is bounded by a reasonable APY ceiling (~15-20%) even if SWF is massive.

---

## 7. Treasury

### Revenue Sources

| Source | Rate | Notes |
|---|---|---|
| Swap fee share | 15% of swap fees | Same as two-way |
| Arb profit | Variable | Treasury sells HEAT when above peg, buys when below |
| SWF overflow | 10% of SWF growth/epoch | New — SWF shares growth with Treasury |

### Expenditures

| Use | Rate | Notes |
|---|---|---|
| Peg defense buys | Up to 1% of Treasury/epoch | Market buy HEAT when below peg |
| LP bootstrapping | One-time at launch | Seed initial XFG/HEAT liquidity |
| Treasury backstop | 0.1% of Treasury/epoch | Emergency transfer to CD pool if SWF + YEM Reserve are empty |

### Treasury Without Burn Premium

In the two-way model, HEAT→XFG burn generated a 5% burn premium → Treasury. This is gone. Treasury is funded by fees + arb + SWF overflow.

To compensate:
- SWF overflow (10% of growth/epoch) provides a continuous feed
- Arb profits increase during volatile periods (when HEAT deviates from peg)
- Lower premium reduces the need for Treasury to defend the peg (less price pressure from minting)

---

## 8. Bonds

### Collateralization (New)

In the two-way model, bonds were collateralized by the YEM Reserve + SWF. The SWF was initially empty. Bonds were "self-collateralized" until scalp revenue accumulated.

In the one-way model:
- SWF receives 98% of every mint
- SWF grows immediately (not waiting for scalp accumulation)
- Bonds are immediately over-collateralized by the SWF

```
Collateral = SWF_balance + YEM_Reserve_credit
Liabilities = outstanding_bond_principal + accrued_coupons

After first HEAT mint of 1000 HEAT:
  SWF += 451.4 XFG (98% × 1000 × peg_price)
  If bonds outstanding = 500 XFG principal:
    Collateral = 451.4 + 0.7 + ... = ~452+ (grows with each mint)
    Collateralization ratio = ~90%+ immediately
    Reaches 100%+ within a few mints
```

YEM Reserve (2% premium × 8% = 0.16% of mint) is proportionally smaller — but SWF absorbs the 98% base, so total collateral is actually higher than the two-way model (where only the premium pool collateralized bonds).

### Coupon Payment

Same mechanism: coinbase from YEM Reserve credit.

```
coupon = principal × bond_rate / EPY
YEM_Reserve -= coupon (if sufficient)
SWF -= coupon (backup if YEM Reserve is insufficient)
```

### Maturity

```
owed = principal + final_coupon
paid from SWF (immediately available)
YEM_Reserve covers only if SWF is insufficient (very unlikely in one-way)
```

---

## 9. Eternal Flame Recycling

### Premium Flow (2%) vs Two-Way (5%)

| | Two-Way | One-Way |
|---|---|---|
| Premium rate | 5% | 2% |
| → Eternal Flame (92%) | 4.6% of mint | **1.84% of mint** |
| → YEM Reserve (8%) | 0.4% of mint | **0.16% of mint** |

Less per-mint goes to EF because the premium is lower. However:

**EF recycling is unchanged at the supply level**. The Osavvirsak formula still counts Eternal Flame. Each burned coin reduces Osavvirsak and increases future baseReward. The 8M8 cap asymptote holds:

```
Osavvirsak = alreadyGeneratedCoins - EternalFlame
baseReward = (MONEY_SUPPLY - Osavvirsak) >> 20

If EF grows slower (lower premium), then:
  - Osavvirsak shrinks slower
  - baseReward grows slower
  - Emission approaches 8M8 more slowly
  - But still converges to 8M8 — no cap violation
```

The trade-off is that **miner emissions are lower** with a lower premium. This could affect network security if block rewards are a significant portion of miner income. Mitigant: the 2% premium still feeds EF faster than zero (no mint at all), and the one-way system incentivizes more mints (because APY is higher, creating demand for HEAT → more mints → more EF volume).

**Net EF comparison**:

```
One-way:  2% premium, 92% to EF = 1.84% of mint volume
Two-way:  5% premium, 92% to EF = 4.6% of mint volume

One-way needs 2.5× the mint volume to match EF flow.
Likely achievable: higher APY → HEAT demand → more mints.
```

---

## 10. Supply Proof (8M8 Cap)

Same induction as two-way model, adjusted for one-way flows:

```
Let:
  A = alreadyGeneratedCoins
  E = EternalFlame
  R = YEM Reserve (paper)
  S = SWF (real XFG, ⊂ A)

Constraints:
  1. A ≤ MONEY_SUPPLY
  2. R ≥ 0 (paper credit non-negative)
  3. Coinbase payout ∈ [0, R]
  4. S ⊆ A (SWF holds existing coins)

Mint (1000 HEAT at 2% premium, peg_price = 0.4514):
  A unchanged
  S += 451.4                       // 98% → SWF (existing coins)
  E += 8.3                         // 92% of 2% premium → EF
  R += 0.72                        // 8% of 2% premium → YEM credit
  A ≤ MONEY_SUPPLY holds

Coinbase payout (0.72 from YEM Reserve):
  A += 0.72                        // new emission
  R -= 0.72
  A ≤ MONEY_SUPPLY holds

Block reward: ε from baseReward:
  A += ε
  A ≤ MONEY_SUPPLY holds (asymptotic)

HEAT peg market buy (Treasury spends XFG):
  A unchanged                      // existing coins, just moving
  A ≤ MONEY_SUPPLY holds

Therefore: A ≤ MONEY_SUPPLY for all transitions.
```

---

## 11. Solvency

### Two-Way Solvency (Old)

```
Collateral_Assets = Collateral_Reserve + SWF + Treasury + YEM_Reserve
Liabilities = Bond_P + Bond_coupons + CD_accrued + HEAT_supply × peg_price

RISK: If XFG price crashes, HEAT redemption liability exceeds Collateral_Reserve
      → classic stablecoin bank run (Terra-style death spiral)
```

### One-Way Solvency (New)

```
Protocol_Assets = SWF + Treasury + YEM_Reserve_credit
Liabilities = Bond_P + Bond_coupons + CD_accrued

Note: No HEAT redemption liability. HEAT holders have no claim on protocol assets.

HEAT peg is maintained via:
  1. Mint arbitrage (self-correcting above peg)
  2. Treasury market operations (discretionary below peg)
  3. HEAT utility demand (DeFi, yield)
```

**The system is always solvent by construction.** It cannot face a redemption run. The worst case is that HEAT trades below peg and mint volume drops — a revenue problem, not a solvency problem.

### Bond Solvency

```
Bond_collateralization_ratio = (SWF + YEM_Reserve) / Bond_liabilities

Minimum acceptable: 1.0 (fully backed)
Target: > 1.5 (over-collateralized)

Breach: if < 1.0, new bond issuance pauses, SWF drip reduces,
        YEM Reserve prioritized for bond payments.
```

---

## 12. Risk Analysis

### Peg Floor Risk (Medium)

HEAT has no guaranteed floor. If demand drops, HEAT trades below peg. Recovery depends on Treasury market ops and organic demand.

Mitigations:
- Treasury buys with SWF allocation
- HEAT yield premium (competitive returns attract holders)
- DeFi utility locks in baseline demand

### Premium Risk (Low)

At 2% premium, HEAT minting is cheaper than two-way (was 5%). This makes HEAT more accessible but reduces per-mint EF + YEM funding.

Mitigations: Higher mint volume compensates (target 2.5× volume of two-way model).

### Treasury Starvation Risk (Low-Medium)

No burn premium revenue. Treasury funded by 15% fees + SWF overflow + arb. If volume is low and peg stable (no arb), Treasury grows slowly.

Mitigations: SWF overflow provides continuous feed regardless of peg activity.

### SWF Concentration Risk (Low)

SWF holds 98% of mint capital + LP yield + bond principal. If the SWF is mismanaged, the entire system suffers.

Mitigations: SWF deployed conservatively (75% LP, 20% CD lending, 5% liquid). No risky assets. Diversified across multiple AMM pools.

---

## 13. Comparison Summary

| | Two-Way | One-Way |
|---|---|---|
| **Collateral Reserve** | Required (95% of mint) | Eliminated |
| **SWF** | Small (only fees + bond principal) | Large (98% of mint capital) |
| **Premium** | 5% | 2% |
| **Redemption** | Guaranteed at cost basis | None |
| **Peg floor** | Guaranteed (redemption) | Market-dependent (Treasury ops) |
| **Peg ceiling** | Mint premium cap | Mint premium cap |
| **APY ceiling** | ~7% | ~14% |
| **Solvency risk** | Redemption run | None by construction |
| **EF per mint** | 4.6% of mint | 1.84% of mint |
| **Treasury burn revenue** | 5% on HEAT→XFG | None |
| **Implementation phases** | 7 phases | **5 phases** (no Phase 4) |
| **Consensus changes** | validate_miner_tx + validateHeatBurn | **validate_miner_tx only** |
| **HEAT supply** | Two-way (up + down) | One-way (up only) |
| **Peg stability** | High (redemption guarantees) | Medium (no floor guarantee) |

---

## 14. Implementation Phases

| Phase | What | Dependencies |
|---|---|---|
| 1 | Fee split (80/15/5) + ReBalancer vault removal | None |
| 2 | SWF creation + mint capital routing (98% → SWF) | Phase 1 |
| 3 | YEM core (lag, rolling, caps, SWF drip, bond engine) | Phase 2 |
| 4 | Burn scalp + coinbase payout engine | Phase 3 |
| 5 | Peg defense (Treasury market ops for below-peg) | Phase 4 |

One less phase than two-way (no Collateral Reserve phase). No `validateHeatBurn` consensus change needed — the consensus change is limited to `constructMinerTx` extension for coinbase payouts.

---

## 15. Key Constants

```cpp
// Supply
constexpr uint64_t MONEY_SUPPLY = 8000'8800'0000'08ULL;

// HEAT mint
constexpr uint64_t HEAT_MINT_PREMIUM_BPS = 200;   // 2% (was 5%)
constexpr uint64_t HEAT_EF_SHARE_BPS     = 9200;  // 92%
constexpr uint64_t HEAT_YEM_SHARE_BPS    = 800;   // 8%

// Fee split
constexpr uint64_t SWAP_FEE_CD_SHARE       = 80;
constexpr uint64_t SWAP_FEE_TREASURY_SHARE = 15;
constexpr uint64_t SWAP_FEE_SWF_DIRECT     = 5;

// Peg
constexpr uint64_t HEAT_PEG_INDEX = 158;
constexpr uint64_t HEAT_PEG_SCALE = 100;
constexpr uint64_t PEG_BUY_THRESHOLD_BPS = 9900;   // 99% of peg (trigger treasury buy)

// SWF
constexpr uint64_t SWF_LP_SHARE_BPS    = 7500;     // 75% LP
constexpr uint64_t SWF_LOAN_SHARE_BPS  = 2000;     // 20% CD lending
constexpr uint64_t SWF_LIQUID_BPS      = 500;      // 5% liquid
constexpr uint64_t SWF_DRIP_BPS        = 25;       // 0.25%/epoch (calibrated)
constexpr uint64_t SWF_GROWTH_FEED_BPS = 1000;     // 10% growth → Treasury

// YEM
constexpr uint32_t YEM_LAG_EPOCHS     = 3;
constexpr uint64_t YEM_SWF_SAVE_PCT   = 60;
constexpr uint64_t YEM_TREAS_BACKSTOP = 10;        // 0.1%/epoch

// Bonds
constexpr uint64_t YEM_BOND_MAX_RATE_BPS    = 2500;
constexpr uint64_t YEM_BOND_ISSUE_CAP_BPS   = 500;
```

---

## 16. Decision Matrix

| Criterion | Two-Way Scores | One-Way Scores | Notes |
|---|---|---|---|
| **Solvency safety** | 7/10 | **10/10** | No redemption risk in one-way |
| **APY potential** | 5/10 | **9/10** | SWF deploys 95% of mint capital |
| **Peg stability** | **9/10** | 6/10 | No redemption floor in one-way |
| **Implementation complexity** | 4/10 | **7/10** | 5 phases vs 7, fewer consensus changes |
| **Eternal Flame recycling** | **9/10** | 7/10 | Lower premium = less EF per mint |
| **User trust** | **8/10** | 5/10 | Redemption is a strong guarantee |
| **Capital efficiency** | 4/10 | **10/10** | No idle Collateral Reserve |
| **Ecosystem growth flywheel** | 5/10 | **9/10** | Mint → SWF → yield → HEAT demand → more mints |

**Net**: One-way is safer (no solvency risk), much higher APY, simpler to implement, but weaker peg guarantee. Two-way is stronger on peg stability and user trust but capital-inefficient and complex.
```
