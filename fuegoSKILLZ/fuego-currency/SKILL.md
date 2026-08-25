---
name: fuego-currency
description: "Fuego domain expert for CD interest, certificates of deposit, APY calculations, deposits, tokenomics, fee pool, and emissions. Expert in Epoch-based fee distribution."
risk: low
source: user-provided
---

# Fuego Currency Expert

Domain expert for Fuego blockchain tokenomics: CD interest, deposits, HEAT mint/burn, fee pool, emissions, and treasury.

## Scope

- **Yield (APY)**: HEAT CD & XFG CD yield calculations
- **Deposits**: HEAT (burn) for CDs, XFG CDs, LP shares, legacy bonds
- **Tokenomics**: Supply cap, coin units, emissions, HEAT flatcoin supply
- **Fee Pool**: Epoch-based fee distribution (80% CD yield, 20% treasury)
- **HEAT Treasury**: Mint burn 50/50 split, LP reserve (60%), peg defense (40%)
- **Epochs**: 900 blocks per epoch (~5 days)

## Trigger Set

**Should trigger on:**
- "fuego CD interest", "calculate CD APY", "certificate of deposit"
- "interest calculation", "APY estimate", "yield"
- "deposit", "HEAT", "COLD", "locked deposit"
- "fee pool", "epoch fee", "treasury share"
- "tokenomics", "supply", "emission", "money supply"
- "epochs", "epoch duration"
- "heat cd", "heat deposit", "heat yield"
- "heat treasury", "eternal flame", "heat mint burn split"
- "cd yield pipeline", "cd yield floor", "xfg buyback"

**Should NOT trigger on:**
- Non-Fuego currency questions
- Generic cryptocurrency tokenomics unrelated to Fuego

## CD Interest Formula

**Source:** `src/CryptoNoteCore/Currency.cpp::calculateCdInterest()`

```
interest = amount × Σ (epoch_fee_rate[i] / total_locked_cd[i])
```
where i ranges from creation_epoch to current_epoch

**APY Estimate:**
```
APY = (0.8 × epoch_swap_fees × 73) / total_cd_locked × 100%
```

**CD Yield Pipeline:**
- 80% of atomic swap fees → CD Yield Pool
- 20% of CD Yield Pool → protocol XFG accumulator (buyback to stabilize)
- 2% APY floor backed by treasury LP reserve
- 40% of CD yield routed to treasury when pool is XFG-heavy (>2:1)

## Key Constants

From `src/CryptoNoteConfig.h`:

| Constant | Value | Meaning |
|----------|-------|---------|
| COIN | 10,000,000 | Atomic units (10^7) |
| MONEY_SUPPLY | 8,000,008,800,008 | Max supply (8M8) |
| DIFFICULTY_TARGET | 480 | Seconds per block |
| EPOCH_DURATION_BLOCKS | 900 | ~5 days |
| SWAP_FEE_RATE_BPS | 100 | 1% swap fee |
| SWAP_FEE_CD_SHARE_PCT | 80 | 80%→CD pool |
| DEPOSIT_MIN_TERM | 5400 | 6 epochs (~1 month) |
| MINIMUM_FEE | 8,000 | 0.0008 XFG |
| CD_MIN_EPOCHS | 6 | minimum 6 epochs (~1 month) |
| CD_MAX_EPOCHS | 72 | ~1 year |
| CD_ALLOWED_TIERS | 6, 18, 36, 72 | epochs |
| CD_YIELD_FLOOR_APY_PCT | 2 | 2% floor |
| CD_YIELD_XFG_BUYBACK_PCT | 20 | buyback share |
| SWAP_FEE_CD_SHARE_PCT | 80 | 80%→CD pool |
| SWAP_FEE_TREASURY_SHARE_PCT | 20 | 20%→treasury |
| CD_YIELD_TREASURY_ROUTE_PCT | 40 | route to treasury when XFG-heavy |
| MINT_BURN_EF_PCT | 50 | 50%→Eternal Flame |
| MINT_BURN_TREASURY_PCT | 50 | 50%→Treasury |
| TREASURY_LP_PCT | 60 | LP Reserve |
| TREASURY_PEG_PCT | 40 | Peg Defense |
| BOOTSTRAP_REPAY_PCT | 20 | bootstrap repayment |
| LOYALTY_BONUS_PCT | 150 | +150% extra = 2.5× total on bonus epochs |
| LOYALTY_BONUS_FULL_EPOCHS | 2 | last 2 full epochs get full bonus |

## Deposit Types

### HEAT CDs (Burn Deposits)
- **Min Amount:** 0.1 HEAT (HEAT_MINT_MIN_HEAT = 1,000,000 atomic)
- **Term:** Forever (0xFFFFFFFF)
- **Yield:** 80% atomic swap fee pool share
- **Mint:** `mint_heat <xfg_amount>` — burns XFG at redemption price
- **Mint Premium:** 3.33% (HEAT_MINT_PREMIUM_BPS = 333)

### XFG CDs
- **Min Amount:** 8 XFG (AMOUNT_TIER_0)
- **Term Tiers:** 6, 18, 36, 72 epochs
- **Yield:** Atomic swap fee pool share + 2% APY floor
- **Loyalty Bonus:** 2.5× yield multiplier on last 2.5 epochs for max-term (72-epoch) CDs

### Legacy Bonds (Debt Cap: ₲254,250)
- **Min Amount:** 0.8 XFG
- **Target APY:** 50%
- **Term:** 72 epochs (~1 year)
- **50% of CD yield share** → legacy bond yield pool (v1.10.00+)
- **Purpose:** Recover bug-era multisig deposits

## Fee Pool Distribution

**Source:** `src/CryptoNoteCore/CommitmentIndex.cpp`

### Per Epoch (900 blocks)
```
Swap Fees (1% of atomic swaps)
  ├─ 80% → CD Yield Pool
  │    ├─ (100 - CD_YIELD_XFG_BUYBACK_PCT)% → CD holders proportionally
  │    └─ CD_YIELD_XFG_BUYBACK_PCT (20%) → protocol XFG accumulator (buyback)
  │    └─ CD_YIELD_FLOOR_APY_PCT (2%) → treasury LP reserve backs floor
  └─ 20% → Treasury Reserve
       └─ BOOTSTRAP_REPAY_PCT (20%) → bootstrap repayment vault
```

### CD Yield Pipeline Rules
- 40% routed to treasury when pool is XFG-heavy (XFG:HEAT > 2:1)
- 2% APY floor guaranteed by treasury LP reserve
- Floor backed by HEAT mint treasury (60% LP reserve allocation)

### HEAT Mint Burn Split
```
XFG burned for HEAT mint:
  50% → Eternal Flame (permanent deflation)
  50% → Treasury
       ├─ 60% → LP Reserve (CD yield floor + Hearth LP)
       └─ 40% → Peg Defense Balance
```

## Usage

```python
from references import CurrencyExpert

expert = CurrencyExpert(source_dir="/Users/aejt/xfgo")

# Calculate CD interest
interest = expert.calculate_cd_interest(
    amount=100000000,  # 10 XFG
    creation_height=1000000,
    current_height=1100000,
    epoch_fee_rates=[1000, 1500, 2000]
)
# Returns: interest in atomic units

# Estimate CD APY (80% swap fee share)
apy = expert.estimate_apy(
    epoch_swap_fees=5000000000,
    total_cd_locked=1000000000000
)

# Calculate HEAT mint economics
# 50% eternal flame, 50% treasury (60% LP / 40% peg)
xfg_burned = 100000000  # 10 XFG
ef_share = xfg_burned * 50 / 100
treasury_share = xfg_burned * 50 / 100
lp_reserve = treasury_share * 60 / 100
peg_defense = treasury_share * 40 / 100
```

## Key Files

| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/Currency.cpp` | CD interest calculation |
| `src/CryptoNoteCore/Currency.h` | Currency class |
| `src/CryptoNoteCore/CommitmentIndex.cpp` | Fee pool, merkle tree |
| `src/CryptoNoteCore/BankingIndex.cpp` | Deposit tracking |
| `src/CryptoNoteCore/HeatMintEngine.h/cpp` | HEAT mint validation |
| `src/CryptoNoteConfig.h` | All constants (incl. HEAT/HEARTH) |
| `docs/heat/FUEGO_HEAT_FINAL_DESIGN.md` | HEAT flatcoin design |

## Utilities

- Use `fuego-rag` for semantic code search
- Use `fuego-codebase-mapper` for file/function search
