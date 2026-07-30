# Sovereign Yield System — Complete Architecture

> Ties together: YEM v3 (smoothed CD engine), Mode 4 (fixed-peg flatcoin), burn scalp (paper banking), legacy-only bonds (collateralized debt), coinbase payouts (sovereign mint), Eternal Flame recycling, 8M8 supply cap.

---

## 1. The Five Pools

Every pool serves a distinct role. None can be merged without breaking a property.

| Pool | Type | Purpose | Supply Impact |
|---|---|---|---|
| **Collateral Reserve** | Real XFG (blockchain state) | Backs HEAT supply 1:1 at creation cost | Neutral (parked coins) |
| **Eternal Flame** | Counter (BankingIndex) | Destroyed XFG available for re-emission via block rewards | Reduces circulation, increases future emission |
| **YEM Reserve** | Paper credit (YemState) | Deferred emission rights from burn scalp; authorizes coinbase minting | Counted as emission when realized |
| **SWF** | Blockchain state | Smooths CD yield; saves surplus, covers deficit | Revenue redistribution |
| **Treasury** | Blockchain state | Peg defense, LP bootstrapping, emergency backstop | Revenue accumulation |

---

## 2. Two-Way Flatcoin Supply Accounting

### Mint (XFG → HEAT)

```
User wants N HEAT at redemption_price = HEAT_PEG / oracle_xfg_price

User pays:   N × redemption_price × 1.05 XFG
             │
             ├── 95% → Collateral Reserve (backs the N HEAT)
             │          Reserve[creditor] += N × redemption_price
             └──  5% → Premium
                         │
                         ├── 92% → Eternal Flame (recycled via block rewards)
                         └──  8% → YEM Reserve (paper credit for coinbase)
```

### Burn (HEAT → XFG)

```
User burns N HEAT, receives min(N × redemption_price × 0.95, Reserve[creditor]) XFG

             Reserve[creditor] -= N × redemption_price
             HEAT supply       -= N
             User gets XFG from Reserve (NOT new emission)
             5% premium → Treasury (protocol revenue)
```

### Total XFG Supply

```
XFG_total = alreadyGeneratedCoins
          = coins from block rewards
          + coins from coinbase YEM payouts
          ≤ MONEY_SUPPLY (= 8,000,880,000,008 atomic units)

Osavvirsak (determines baseReward):
  = max(alreadyGeneratedCoins - EternalFlame, 0)

Collateral Reserve:
  XFG in reserve was already counted in alreadyGeneratedCoins
  In/out flows are supply-neutral
  Reserve ⊆ alreadyGeneratedCoins

YEM Reserve:
  Paper credit, NOT XFG. Not counted in supply.
  When realized via coinbase → becomes new XFG → counted in alreadyGeneratedCoins
  Total realized ≤ total Eternal Flame diverted (emission capacity preserved)
```

### Proof: 8M8 Holds

```
Every mint:  5% premium → Eternal Flame (4.6%) + YEM Reserve (0.4%)
Coinbase:    mints 0.4% from YEM Reserve credit
Block reward: based on Osavvirsak = alreadyGenerated - Eternal Flame

Without scalp: 100% premium → EF → recycled via block rewards over time
With scalp:    92% premium → EF → recycled via block rewards over time
                8% premium → YEM Reserve → coinbase mint now

Total emission: IDENTICAL. Time-shifted, not expanded.
MONEY_SUPPLY = 8,000,880,000,008 asymptote holds.
```

---

## 3. YEM Bonds as Collateralized Debt

### Structure

Pre-v10 COLD deposit holders migrate locked XFG into YEM bonds. Principal seeds the SWF. The bond is collateralized by the YEM Reserve — paper credits from ongoing burn scalp operations.

```
Bond issuance:
  Legacy COLD holder → migrate_legacy_deposit --as-yem-bond
  Principal → SWF (operational capital)
  Bond recorded in YemBondIndex with rate, term, creditor

Bond coupon (per epoch):
  coupon = principal × bond_rate / EPY
  paid via coinbase from YEM Reserve
  YEM Reserve debited

Bond maturity:
  owed = principal + final coupon
  paid from SWF first (up to SWF balance)
  remainder → coinbase from YEM Reserve
```

### Collateralization Ratio

```
Collateral = SWF_balance + YEM_Reserve_balance
Liabilities = outstanding_bond_principal + accrued_coupons

System is solvent when: Collateral ≥ Liabilities

At launch: SWF = 0, YEM Reserve = 0
  → bonds are the ONLY source of initial SWF
  → each bond's principal IS the SWF
  → bonds are self-collateralized until burn scalp revenue accumulates
  → YEM Reserve grows over time from ongoing HEAT mints
  → eventually Collateral > Liabilities, bonds are over-collateralized
```

### Issuance Cap

```
per_epoch_bond_cap = min(
    remaining_migration_pool,
    total_cd_locked × 0.005    // 0.5% of CD locked per epoch
)
```

Prevents SWF from being flooded with bond principal faster than the YEM Reserve can collateralize it. With CD locked = 80K and bond pool = 50K, full migration takes ~10 years.

---

## 4. CD Yield Engine (Highest APY via Smoothing)

### The Problem

Raw per-epoch CD yield (today's system) pays `epochCdShare / totalCdLocked` each epoch. Empty or low-volume epochs = zero. Variance is high.

### The Solution

```
CD_rate = min(rolling_3_epoch_avg_organic_rate, tier_cap(lock_duration))
        + SWF_drip_share
        + bond_coupon_pass_through (if bond-holder's CD)
```

### Income → CD Holder Flow

```
SWAP FEES ──→ 80% CD pool ──→ YEM smoothing ──→ capped + smoothed yield per CD
           ──→ 15% Treasury ──→ peg defense + LP bootstrapping
           ──→  5% SWF direct ──→ SWF balance

BURN SCALP ──→ 8% of mint premium ──→ YEM Reserve ──→ coinbase ──→ bond coupons
                                                                    ──→ deficit coverage

LP YIELD ──→ 75% → SWF (passive protocol income)
        ──→ 25% → Treasury

BOND PRINCIPAL ──→ SWF (one-time at migration)

SWF DRIP ──→ 0.5% of SWF per epoch ──→ all CD holders (proportional to lock)
```

### APY Composition (typical epoch)

| Component | Source | Typical contribution |
|---|---|---|
| Base yield | Swap fees (80% of 1% on volume) | 3-5% annualized |
| Time-tiered cap | Longer lock = higher cap ceiling | 0-47% headroom above base |
| SWF drip | 0.5% of SWF distributed per epoch | 0.1-0.5% annualized |
| Bond pass-through | Bond coupons paid to CD's owner | varies |

**Total: 4-7% APY** under baseline conditions (validated by simulation).

---

## 5. Eternal Flame Recycling

### Why It Must Stay

Without Eternal Flame recycling, every XFG burned is permanently lost, and the total supply asymptote shrinks. With recycling, burned XFG returns as block rewards, keeping the supply trending toward 8M8.

### How It Works With Two-Way Flatcoin

```
XFG minted via block reward ──→ user wallet
                                 │
                                 ├── user burns XFG → mints HEAT
                                 │     95% → Collateral Reserve
                                 │      5% → Eternal Flame (counter)
                                 │              └── 92% → counted in Osavvirsak
                                 │              └──  8% → YEM Reserve
                                 │
                                 ├── user burns HEAT → redeems XFG
                                 │     Reserve releases XFG back to user
                                 │     HEAT destroyed
                                 │
                                 └── Eternal Flame grows monotonically
                                        ↓
                                    Osavvirsak = alreadyGenerated - EF
                                        ↓
                                    baseReward = (MONEY_SUPPLY - Osavvirsak) >> 20
                                        ↓
                                    XFG re-enters circulation via miners
```

Eternal Flame is strictly increasing (never debited). It feeds the Osavvirsak formula which determines the block reward. Burned coins are always available for re-emission — this is the "recycling" loop.

The Collateral Reserve is the path that XFG takes INTO backing HEAT. It is distinct from Eternal Flame. The two never interact. XFG in the Reserve is not burned — it's parked, and returns to circulation when HEAT is redeemed.

---

## 6. Coinbase Payout Engine

### Why Coinbase

Coins minted via coinbase are the ONLY source of new XFG emission. Every other pool (reserve, treasury, SWF) holds pre-existing coins. Only coinbase creates new ones. By tying YEM Reserve credits to coinbase minting, we ensure:

1. Every new coin is authorized by a burn event (no unbacked emission)
2. Total emission ≤ MONEY_SUPPLY (the 8M8 cap)
3. Payouts are atomic and consensus-verified (part of block validation)
4. No intermediate protocol address can be drained or hacked

### Payout Types

| Payout | Recipient | Source | Frequency |
|---|---|---|---|
| Bond coupon | Bondholder address | YEM Reserve → coinbase | Per epoch |
| Bond maturity | Bondholder address | SWF + YEM Reserve → coinbase | At maturity |
| Deficit coverage | Fee pool (internal) | YEM Reserve → coinbase | Per epoch, when needed |

### Per-Block Draining

The payout queue is drained N at a time per block (N = YEM_MAX_PAYOUTS_PER_BLOCK). Large maturity events are spread across multiple blocks. The `constructMinerTx()` function creates one-time-key outputs to each recipient, identical to how mining rewards work.

### Consensus Safety

```cpp
// validate_miner_transaction()
uint64_t expectedYemPayouts = computeExpectedYemPayouts(height);
if (coinbaseTotal != baseReward + fee + expectedYemPayouts)
    return false;  // reject block
```

`computeExpectedYemPayouts()` is deterministic — given the blockchain state at height H, it returns the exact payout total that the miner must include. Miners cannot inflate or skip payouts.

---

## 7. Complete Epoch Lifecycle

```
EVERY EPOCH (900 blocks, ~5 days):
───────────────────────────────────

PER-BLOCK:
  1. Accumulate swap fees → m_currentEpochSwapFees
  2. Burn scalp: 8% of mint premiums → YEM Reserve
  3. Drain payout queue: up to 10 coinbase payouts per block

EPOCH BOUNDARY:
  4. Fee split: 80% CD / 15% Treasury / 5% SWF direct
  5. LP yield feed: 75% → SWF, 25% → Treasury
  6. Bond migration (yearly): pre-v10 COLD → SWF principal
  7. Bond coupons: paid from YEM Reserve → coinbase queue
  8. Bond maturities: principal repaid from SWF + YEM Reserve
  9. YEM smoothing (post-lag):
     a. Rolling 3-epoch average → target rate
     b. Time-tiered caps per CD
     c. Surplus → SWF (60%), Deficit → draw SWF → YEM Reserve → treasury backstop
     d. SWF drip → all CD holders (proportional)
  10. Execute CD yield: CD pool XFG → AMM pool → HEAT
  11. Record epoch fee rates in CommitmentIndex
  12. Save EpochStateSnapshot (for popBlock reversal)
  13. Reset accumulators
```

---

## 8. State Dependencies

```
YEM Reserve
  ↑ fed by: burn scalp (per block)
  ↓ drained by: coinbase bond coupons, coinbase deficit coverage

SWF
  ↑ fed by: surplus smoothing, SWF direct feed (5% fees), LP yield feed (75%), bond principal
  ↓ drained by: deficit smoothing, bond maturity repayment, SWF drip

Treasury
  ↑ fed by: 15% swap fees, burn premium (HEAT→XFG), protocol arb profit
  ↓ drained by: LP contribution, peg defense buys, treasury backstop (0.1%/epoch)

Collateral Reserve
  ↑ fed by: 95% of HEAT mint payments
  ↓ drained by: HEAT redemptions

Eternal Flame
  ↑ fed by: 92% of mint premium (the portion NOT scalped)
  ↓ drained by: NEVER (strictly increasing counter)
```

---

## 9. XFG Supply Proof (Formal)

```
Let:
  A = alreadyGeneratedCoins (total XFG ever minted by block rewards + coinbase)
  E = EternalFlame (cumulative burned XFG)
  R = YEM Reserve (paper credits, not XFG)
  C = Collateral Reserve (parked XFG, subset of A)

Define:
  Circulating = A - C                    (XFG in wallets + pools, not in reserve)
  Osavvirsak  = max(A - E, 0)           (effective supply for emission formula)
  baseReward  = (MONEY_SUPPLY - Osavvirsak) >> 20

Constraints:
  1. A ≤ MONEY_SUPPLY                    (total emission cap)
  2. E ≤ A                               (can't burn more than exists)
  3. C ⊂ A                               (reserve is existing coins)
  4. Each coinbase YEM payout ∈ [0, R] (authorized by prior burn scalp)
  5. R = Σ(scalped_burns) - Σ(yem_coinbase_payouts)

Induction:
  At genesis: A=0, E=0, R=0, C=0. All constraints hold.
  
  Mint 100 XFG → HEAT (5% premium):
    A unchanged (no new emission)
    C += 95 (parked in reserve)
    E += 4.6 (92% of 5 premium → Eternal Flame)
    R += 0.4 (8% of 5 premium → paper credit)
    A ≤ MONEY_SUPPLY holds (no increase)
  
  Coinbase payout of 0.4 XFG from YEM Reserve:
    A += 0.4 (new emission)
    R -= 0.4 (credit used)
    A ≤ MONEY_SUPPLY holds (was 0, now 0.4, still << 8.8M)
    R ≥ 0 holds (was 0.4, now 0)
  
  Block reward of ε XFG from baseReward:
    A += ε
    E unchanged
    A ≤ MONEY_SUPPLY holds (asymptotically approaches cap)
    
  Redeem HEAT → XFG:
    A unchanged
    C -= 100 (returned from reserve)
    E unchanged
    R unchanged
    
  Therefore: A ≤ MONEY_SUPPLY for all valid state transitions.
  The 8M8 cap is inviolate.
```

---

## 10. Implementation Order

| Phase | What | Dependencies |
|---|---|---|
| 1 | Fee split (80/15/5) + ReBalancer vault removal | None |
| 2 | YEM core (SWF, lag, rolling, caps, drip) | Phase 1 |
| 3 | Burn scalp (8% interception in pushToBankingIndex) | Phase 2 |
| 4 | Collateral Reserve + two-way HEAT validation | Phase 3 |
| 5 | Coinbase payout engine (queue + constructMinerTx extension) | Phase 4 |
| 6 | Legacy-only bonds (0xCF tag, YemBondIndex) | Phase 5 |
| 7 | Treasury LP feed + peg defense | Phase 6 |

Phases 4 and 5 are the critical path — they require consensus-level validation changes (`validate_miner_transaction`, `validateHeatBurn`). Phases 1-3 and 6-7 are state-machine additions that can be tested in isolation.

---

## 11. Key Constants

```cpp
// Supply
constexpr uint64_t MONEY_SUPPLY = 8000'8800'0000'08ULL;  // 8M8 cap

// Fee split
constexpr uint64_t SWAP_FEE_CD_SHARE       = 80;
constexpr uint64_t SWAP_FEE_TREASURY_SHARE = 15;
constexpr uint64_t SWAP_FEE_SWF_DIRECT     = 5;

// Mode 4 peg
constexpr uint64_t HEAT_PEG_INDEX = 158;   // $1.58 CPI-indexed
constexpr uint64_t HEAT_PEG_SCALE = 100;

// YEM
constexpr uint32_t YEM_LAG_EPOCHS      = 3;
constexpr uint64_t YEM_SWF_SAVE_PCT    = 60;
constexpr uint64_t YEM_SWF_DRIP_BPS    = 50;     // 0.5%/epoch
constexpr uint64_t YEM_BURN_SCALP_BPS  = 800;    // 8%
constexpr uint64_t YEM_TREAS_BACKSTOP  = 10;     // 0.1%/epoch

// Tiers
constexpr uint64_t TIER_CAP_MIN_BPS = 3300;      // 33%
constexpr uint64_t TIER_CAP_MAX_BPS = 8000;      // 80%

// Bonds
constexpr uint64_t YEM_BOND_MAX_RATE_BPS = 2500; // 25%/yr
constexpr uint64_t YEM_BOND_ISSUE_CAP_BPS = 500; // 0.5% CD locked/epoch

// Flatcoin
constexpr uint64_t HEAT_MINT_PREMIUM_BPS = 500;  // 5%
constexpr uint64_t HEAT_BURN_PREMIUM_BPS = 500;  // 5%
```
