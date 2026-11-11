# HEAT CD APY Models — Complete Survey

**Context:** HEAT CDs currently distribute 80% of swap fees as variable per-epoch yield with zero smoothing. This produces volatile, unpredictable returns. The goal is to provide CD holders with stable, predictable APY while protecting the protocol treasury.

**All models assume:**
- 8:1 Full Float PI stability (proven best from Monte Carlo)
- 80/20 swap fee split (80% CD pool / 20% treasury)
- Hearth AMM with 0.3% swap fee
- `m_treasuryLpYield` (protocol LP fees) exists and currently only accumulates, never spent

---

## Model 1: Raw Fee Share (Baseline / Current)
**Mechanism:** 80% of swap fees distributed as-is each epoch. No smoothing, no reserves, no treasury involvement.

**Yield formula:** `epochRate = (swapFees × 0.80) / totalCdLocked`

**Strengths:** Simple to implement. No treasury risk. Market-pure yields.

**Weaknesses:** Highly volatile. Users cannot predict returns. CD APY crashes instantly when swap volume drops.

---

## Model 2: Treasury-Smoothed Macro-Epoch
**Mechanism:** Every 3 epochs (= 1 macro-epoch), compute a 3-epoch rolling average target rate. Treasury open-ended backfills any deficit between actual fees and target rate.

**Yield formula:**
```
targetRate = avg(last 3 epochFeeRates)
actualRate = currentFees / totalLocked
if actualRate < targetRate: treasury pays the difference (unbounded)
if actualRate > targetRate: excess goes to treasury
```

**Strengths:** Users get stable, predictable APY over macro-epoch windows. Pro-rata for mid-epoch locks works naturally with per-epoch rate recording.

**Weaknesses:** Treasury is the primary shock absorber — unbounded liability. Moral hazard: less incentive to drive swap volume. Treasury can drain rapidly in prolonged low-fee periods.

---

## Model 3: Rolling Average + Yield Smoothing Fund (YSF)
**Mechanism:** Compute a 6-epoch rolling average target rate. Separately maintain a Yield Smoothing Fund that receives excess fees during boom epochs and pays out during lean epochs. Treasury only backstops if YSF is empty.

**Yield formula:**
```
targetRate = avg(last 6 epochFeeRates)
if actualRate > targetRate: excess → YieldSmoothingFund
if actualRate < targetRate: deficit ← YieldSmoothingFund (capped by balance)
if YSF empty AND actualRate < targetRate: deficit ← treasury (capped)
```

**Strengths:** Self-sustaining smoothing fund. Treasury is secondary, not primary. Smooths over 6 epochs.

**Weaknesses:** Adds a separate fund entity. YSF can deplete in extended bear markets. New fund requires additional accounting.

---

## Model 4: 3-Layer Yield Stabilization
**Mechanism:** Three-tier smoothing hierarchy — Layer 1: CD SWF (internal to 80% pool, saves/draws surplus), Layer 2: Treasury LP Yield Recapture (redirects idle LP yield), Layer 3: Treasury backstop (capped 0.5%/epoch).

**Yield formula:**
```
targetRate = avg(last 6 epochFeeRates)
required = targetRate × totalLocked
deficit = max(0, required - cdShare)

Layer1: draw from cdStabilizationReserve (saved surplus from prior boom epochs)
Layer2: draw from lpYieldBuffer (redirected m_treasuryLpYield)
Layer3: draw from treasury (max 0.5% of treasury/epoch)
```

**Strengths:** Treasury is last resort only. Captures idle capital (`m_treasuryLpYield`) that currently does nothing. Internal CD SWF is self-sustaining.

**Weaknesses:** Most complex implementation. Three layers of state to manage. LP yield may be too small to be meaningful in early epochs.

---

## Model 5: Floor+Bonus with Reserve Proof
**Mechanism:** At each macro-epoch start, calculate a guaranteed floor APY from available reserves (CD reserve + LP yield + 5% of treasury). Organic fees above the floor are distributed as bonus yield. The safe floor is mathematically provable.

**Yield formula:**
```
available = cdReserve + lpYieldBuffer + treasury × 0.05
floorRate = available / (3 × totalLocked)  // over 3-epoch window

if organicRate >= floorRate:
  floor_yield = floorRate  (guaranteed)
  bonus_yield = organicRate - floorRate  (variable)
else:
  floor_yield = organicRate  (can't pay more than earned)
  bonus_yield = 0
```

**Strengths:** Users know their minimum guaranteed return. Preserves upside (bonus). Floor is mathematically provable. Self-regulating (floor rises with reserves, falls with depletion).

**Weaknesses:** Floor+bonus accounting is slightly more complex per-epoch. Floor may be very low in early system stages with small reserves. Bonus is volatile by nature.

---

## Model 6: Emission Schedule with Fee Offset
**Mechanism:** Define a pre-determined declining APY schedule (e.g., Y1: 12%, Y2: 10%, Y3: 8%...). Protocol mints HEAT to pay the target APY on all CDs. Swap fee revenue offsets the mint: fees that cover the emission → zero net minting. Fees that fall short → mint the difference. Fees that exceed → burn excess HEAT.

**Yield formula:**
```
targetAPY = emissionSchedule(year)
cost = targetAPY × totalLocked
if swapFees > cost: burn(excess)  → deflationary
if swapFees < cost: mint(deficit)  → slightly inflationary
```

**Strengths:** Maximum predictability — users know exact APY for their CD's duration. Self-regulating supply (burn when hot, mint when cold). Proven by PoS chains (Cosmos, Polkadot).

**Weaknesses:** Minting HEAT to cover deficits dilutes existing holders. HEAT already has mint supply path (mint_heat with premium). Requires governance to define the emission schedule. "Yield from thin air" may be controversial.

---

## Model 7: Burn-to-Boost Premium Tier
**Mechanism:** Users can optionally burn a small percentage of their CD principal (e.g., 2%) in exchange for guaranteed APY status. Burned funds feed the stabilization reserve. Non-burning users get the variable organic rate.

**Yield formula:**
```
if user opted for boost:
  yield = guaranteedRate  (paid from stabilization reserve)
else:
  yield = organicRate  (paid from current fees)
```

**Strengths:** Market-driven insurance — stability-seekers pay for the guarantee. Treasury not involved. Voluntary — users choose their risk/reward profile.

**Weaknesses:** Requires user education. Only premium-tier CDs get stable APY. Guaranteed rate may still fluctuate if reserve depletes.

---

## Model 8: CD Tranche Model
**Mechanism:** Segment CDs into Senior/Mezzanine/Equity tranches. Fee revenue distributed waterfall-style: Senior first (guaranteed by specific treasury reserves), Mezzanine second (semi-guaranteed by CD reserve), Equity last (absorbs all volatility).

**Yield formula:**
```
Senior (50% capacity):    fixed 3-5% APY, backed by treasury reserve
Mezzanine (30% capacity): trailing 3-epoch avg × 0.9, backed by CD reserve
Equity (20% capacity):    100% residual after Senior + Mezzanine paid
Waterfall: Senior first → Mezzanine second → Equity last
```

**Strengths:** Risk segmentation matches user preferences. Senior tranche is genuinely risk-free (backed by specific reserves). Equity captures all upside.

**Weaknesses:** Highest implementation complexity. Mezzanine/Equity may get zero in worst-case. Requires tranche fill-and-routing logic. Users must understand risk tiers.

---

## Model 9: Batch Issuance
**Mechanism:** CDs issued in fixed-size batches with locked APY. At batch opening, protocol calculates `SafeAPY = ReserveCommitment / BatchSize × TimeFactor`. Batch has fixed capacity and duration. Once filled, new CDs go to next batch with freshly calculated APY.

**Yield formula:**
```
BatchCap = 100,000 HEAT max
SafeAPY = (availableReserves × safetyMargin × EpochsPerYear) / BatchCap

Batch 1 (open): SafeAPY = 8.5% (calculated when batch opened)
Batch 2 (fills after Batch 1): SafeAPY = 9.2% (recalculated)
```

**Strengths:** APY is locked per batch — zero volatility for batch holders. Protocol liability is math-bounded (batch size × APY). New CDs never dilute existing ones. Treasurers pre-commit for each batch.

**Weaknesses:** Limited capacity — users may need to wait for next batch. APY may differ between batches causing user confusion. Batch lifecycle state machine adds complexity.

---

## Model 10: Fixed Reward Pool
**Mechanism:** Protocol allocates a fixed total reward pool per macro-epoch (e.g., 50,000 HEAT). All active CDs share it pro-rata based on amount × time locked. APY is reported ex-post, never guaranteed upfront.

**Yield formula:**
```
rewardPool = treasuryLpYield(3 epochs) + treasuryPreCommit + cdReserveBuffer

each CD earns: rewardPool × (CD_amount / totalLocked) × (epochs_active / 3)
APY = reported AFTER macro-epoch closes
```

**Strengths:** Protocol liability is absolutely fixed (pool is known and funded). Zero treasury risk beyond pre-commit. Self-regulating — more CDs → less per-CD yield automatically.

**Weaknesses:** No upfront APY promise — users cannot predict returns. Yield per CD decreases as more users lock. May not attract stability-seeking users.

---

## Model 11: Pre-Funded Macro-Epoch
**Mechanism:** At each macro-epoch start (every 3 epochs), treasury moves a configurable percentage (e.g., 5%) into a "Macro-Epoch Fund." APY is calculated from this committed fund + projected fees. Unused funds return to treasury at macro-epoch end.

**Yield formula:**
```
Macro-epoch start:
  commitment = treasury × 0.05
  treasury -= commitment  (moved to fund)
  safeAPY = (commitment + projectedFees) / (totalLocked × 3)

Each micro-epoch:
  pay safeAPY × totalLocked from: fees first, then fund

Macro-epoch end:
  unused fund → returned to treasury
  surplus fees → cdReserve or treasury
```

**Strengths:** Treasury liability is pre-committed and bounded (known at macro-epoch start). APY backed by real money, not projections. Unused funds return — treasury is not drained. Directly answers "I have X → I can safely offer Y."

**Weaknesses:** APY may fluctuate between macro-epochs. Requires pre-commitment logic and fund segregation. Conservative: safeAPY may be lower than organic rate during bull markets.

---

## Model 12: Retroactive Bonus (With-Profit / Insurance Model)
**Mechanism:** Protocol NEVER promises APY upfront. Instead, after each macro-epoch, it declares a **reversionary bonus** (conservative, based on actual fees, added to CD principal and locked). At CD maturity, a **terminal bonus** is paid: actual lifetime fees minus already-paid reversionary bonuses. Protocol publishes a **projected APY** as informational only (not guaranteed).

**Yield formula:**
```
Reversionary bonus (each macro-epoch):
  bonus = conservative × actualFeeRate × totalLocked  (e.g., 50% haircut)
  added to all active CDs' principal (permanently locked)

Terminal bonus (at CD maturity):
  total lifetime fees - sum of all reversionary bonuses = terminal bonus
  paid as lump sum upon CD unlock
```

**Strengths:** Zero treasury liability — protocol never promises what it can't deliver. CD holders get stable growth (reversionary bonuses compound). Captures full upside via terminal bonus. Eliminates moral hazard completely.

**Weaknesses:** No guaranteed APY upfront — hard to market. Requires trust that terminal bonus will be fair. CD holders may want predictability, not retroactive payments.

---

## Model 13: CD Sovereign Wealth Fund (CD-SWF)
**Mechanism:** Internal to the 80% CD pool — no new entities. In high-fee epochs, save excess above the rolling average into a reserve. In low-fee epochs, draw from the reserve to maintain the rolling average rate. Pool self-smooths without touching treasury or any external mechanism.

**Yield formula:**
```
targetRate = avg(last 6 epochFeeRates)
if actualRate > targetRate:
  distribute targetRate to CDs
  save (actualRate - targetRate) × locked → cdReserve
if actualRate < targetRate:
  draw deficit from cdReserve (capped by balance)
  distribute targetRate (or as much as reserve allows)
```

**Strengths:** Simplest non-trivial model. Zero treasury involvement. Internal to the existing 80% split. Self-sustaining in the long run. Reserve grows organically in bull markets.

**Weaknesses:** Reserve can deplete in sustained bear markets (at which point yield drops to organic rate). No fallback layer beyond the reserve. May not provide enough smoothing in early stages with small reserves.

---

## Summary Comparison Matrix

| # | Model | APY Guarantee | Treasury Risk | Complexity | Smoothing | Uses Idle LP Yield |
|---|-------|--------------|---------------|------------|-----------|-------------------|
| 1 | Raw Fee Share | None | None | Minimal | None | No |
| 2 | Treasury-Smoothed Macro-Epoch | Yes (macro-epoch) | **High** (unbounded) | Medium | 3-epoch avg | No |
| 3 | Rolling Avg + YSF | No (smoothed only) | Medium (YSF backstop) | Medium | 6-epoch avg | No |
| 4 | 3-Layer Stabilization | No (smoothed only) | Low (capped, last resort) | High | 6-epoch avg + 3 layers | **Yes** |
| 5 | Floor+Bonus | Yes (floor guaranteed) | Low (5% of treasury) | Medium-High | Reserve-proven floor | **Yes** |
| 6 | Emission Schedule | Yes (by protocol rule) | Low (controlled minting) | Medium | Declining schedule | No |
| 7 | Burn-to-Boost | Yes (premium tier only) | None | Medium | User-funded | No |
| 8 | CD Tranche | Yes (Senior tier) | Low (Senior reserve) | High | Tiered waterfall | **Yes** |
| 9 | Batch Issuance | Yes (per batch) | Low (per-batch commitment) | Medium-High | Batch-bounded | **Yes** |
| 10 | Fixed Reward Pool | No (ex-post only) | Very Low (fixed pool) | Low | Fixed pool | **Yes** |
| 11 | Pre-Funded Macro-Epoch | Yes (pre-funded) | **Low** (bounded, returned) | Medium | Pre-funded + 3-epoch | **Yes** |
| 12 | Retroactive Bonus | No (ex-post) | **Zero** | Low | Conservative bonus | No |
| 13 | CD Sovereign Wealth Fund | No (smoothed only) | **Zero** | Low | 6-epoch avg internal | No |

---

## Models Selected for Monte Carlo Simulation

Five models were selected for the comprehensive simulation (`sim_cd_apy_compare.py`) based on:
- Coverage of the three main paradigms (treasury-backed, self-smoothing, pre-funded)
- Level of merit and theoretical soundness
- Representation of different risk/certainty profiles

| Sim # | Model | Paradigm |
|-------|-------|----------|
| M1 | Raw Fee Share | Baseline — no smoothing |
| M2 | Treasury-Smoothed Macro-Epoch | Treasury as primary backstop |
| M3 | 3-Layer Stabilization | Self-smoothing with treasury as last resort |
| M4 | Floor+Bonus | Guaranteed floor + variable upside |
| M5 | Pre-Funded Macro-Epoch | Treasury pre-commits → known liability |

**Model 6 (Emission Schedule)** was excluded because it requires a governance-defined schedule, not market-driven comparison.
**Model 7 (Burn-to-Boost)** excluded because it's a user-optional premium tier, not a system-wide distribution model.
**Model 8 (CD Tranche)** excluded due to orthogonal tranche routing logic that doesn't compete with the others.
**Model 9 (Batch Issuance)** excluded because its batch lifecycle is too different from continuous CD flow to compare fairly.
**Model 10 (Fixed Reward Pool)** excluded because it doesn't offer APY, which is the explicit goal.
**Models 12-13** were combined into M3/M4 as variations of self-smoothing.

---

## Monte Carlo Design

| Parameter | Value |
|-----------|-------|
| Sims | 200 |
| Duration | 21 years (1,533 epochs at 73/yr) |
| Stability | 8:1 Full Float, KP=0.08, KI=0.015 |
| CD lock rate | 8% of supply locks per epoch (normal jitter) |
| CD duration | Normal(mean=180 epochs, std=60) ≈ 2.5yr avg |
| Swap fee | 2% per trade |
| Fee split | 80% CD pool / 20% treasury |
| Mint premium | 5% |
| Macro-epoch | 3 normal epochs |
| XFG price | $2 → ~$25 over 20yr (exponential + noise) |

### Metrics Scored

| Metric | Weight | Higher/Lower is better |
|--------|--------|----------------------|
| Effective CD APY | 35% | Higher |
| Treasury preservation | 25% | Higher |
| APY stability (1/vol) | 20% | Higher |
| Fee efficiency (%→CDs) | 10% | Higher |
| Pool liquidity depth | 10% | Higher |

### Sensitivity Tests

1. **Low-fee doldrums** — 6-month volume crash to 10% of trend
2. **Fee explosion** — 10× volume spike
3. **CD utilization spike** — lock rate 8% → 25%
4. **Cold start** — first 3 epochs with zero fees
