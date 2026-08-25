---
name: fuego-heat-and-hearth
description: "Fuego domain expert for HEAT stablecoin and HEARTH AMM: mint/burn mechanics, PI-controlled stability, liquidity pool operations, treasury allocation, and flatcoin economics."
risk: low
source: user-provided
---

# Fuego HEAT & Hearth Expert

Domain expert for Fuego's HEAT algorithmic flatcoin and the HEARTH AMM (automated market maker).

## Scope

- **HEAT Token**: Mint via XFG burn, burn-to-redeem, flatcoin mechanics (CPI-adjusted purchasing power)
- **HEARTH AMM**: Constant product XFG/HEAT pool, liquidity provision, swapping
- **Stability System**: PI controller, basin discovery, 3 stability modes, Hill damping
- **Treasury**: Mint burn split (50/50 EF/treasury), treasury LP reserve (60%), peg defense (40%)
- **Oracle**: Multi-pair TWAP, XFG/USD price, heat_metrics RPC
- **Wallet**: hearth_add, hearth_heat, hearth_xfg, hearth_exit, hearth_info commands

## Trigger Set

**Should trigger on:**
- "heat", "heat token", "heat stablecoin", "flatcoin"
- "hearth", "hearth amm", "liquidity pool", "xfg/heat"
- "heat mint", "heat burn", "heat cd", "heat deposit"
- "peg", "stability", "pi controller", "basin discovery"
- "heat_metrics", "xfg_heat_ratio", "heat_peg_usd"
- "hearth_add", "hearth_xfg", "hearth_heat", "hearth_exit"
- "heat treasury", "eternal flame", "treasury lp"
- "overcollateralization", "redemption price", "twap oracle"

**Should NOT trigger on:**
- Non-Fuego stablecoins
- Generic AMM mechanics unrelated to Fuego HEARTH

## Key Constants

From `src/CryptoNoteConfig.h`:

### HEAT Token
| Constant | Value | Meaning |
|----------|-------|---------|
| HEAT_PEG_USD | 1.58 | USD peg reference (launch) |
| HEAT_STABILITY_MODE | 2 | 0=CPI, 1=5:1 band, 2=8:1 float |
| HEAT_LAUNCH_RATIO | 10:1 | XFG:HEAT pool ratio at launch |
| HEAT_MINT_MIN_HEAT | 0.1 HEAT | minimum mint amount (1,000,000 atomic) |
| HEAT_MINT_PREMIUM_BPS | 333 | 3.33% mint premium |
| MINT_BURN_EF_PCT | 50% | XFG burned → Eternal Flame |
| MINT_BURN_TREASURY_PCT | 50% | XFG burned → Treasury |
| TREASURY_LP_PCT | 60% | Treasury → LP Reserve |
| TREASURY_PEG_PCT | 40% | Treasury → Peg Defense |

### HEARTH AMM
| Constant | Value | Meaning |
|----------|-------|---------|
| HEARTH_FEE_BPS | 30 | 0.3% swap fee → LPs |
| HEARTH_POOL_SEED_XFG | 10,000 XFG | genesis pool side |
| HEARTH_POOL_SEED_HEAT | 1,000 HEAT | genesis pool side (10:1 ratio) |
| HEARTH_INITIAL_XFG | 10,000 XFG | v11 activation pool side |
| HEARTH_INITIAL_HEAT | 1,000 HEAT | v11 activation pool side (10:1) |
| HEARTH_MIN_XFG_DEPTH | 5,000 XFG | aspirational depth target |
| HEARTH_MIN_HEAT_DEPTH | 5,000 HEAT | aspirational depth target |

### PI Controller
| Constant | Value | Meaning |
|----------|-------|---------|
| PI_KP | 0.08 | proportional gain |
| PI_KI | 0.015 | integral gain |
| PI_BASE_RATE | ±50%/yr | basin-locked rate |
| PI_ABS_MAX_RATE | ±1000%/yr | absolute ceiling |
| PI_INTEGRAL_CLAMP | ±100% | anti-windup clamp |
| HEAT_PI_USE_DAMP | true | Hill-damping at extremes |
| HEAT_PI_DAMP_M | 200% | midpoint deviation |
| HEAT_PI_DAMP_N | 4 | Hill coefficient |

### CPI Peg (Mode 0)
| Constant | Value | Meaning |
|----------|-------|---------|
| HEAT_CPI_BASE_FLOOR | 1.50 | floor at CPI=100 |
| HEAT_CPI_BASE_CEIL | 2.50 | ceiling at CPI=100 |
| HEAT_CPI_LAUNCH_INDEX | 100 | CPI at launch |
| HEAT_CPI_AUTO_INFLATION_BPS | 2.50%/yr | simulated drift |
| HEAT_CPI_UPDATE_INTERVAL | 730 blocks | ~4 days |

## HEAT Mint/Burn Mechanics

### Mint (XFG → HEAT)
```
User burns XFG → 50% Eternal Flame (permanent deflation)
                 → 50% Treasury (60% LP Reserve / 40% Peg Defense)
                 → HEAT minted at redemption price
```

Via CLI:
```
mint_heat <xfg_amount>
```

The system validates mint via `HeatMintEngine::validateMint()`:
- Commitment outputs with `term==0` mark HEAT mint
- Redemption price computed from XFG oracle
- 5% mint premium applied via PI gate

### Burn (HEAT → XFG) — Not Yet Implemented

The design specifies burn mechanics (3-day TWAP oracle, 1% extraction fee) but no `burn_heat` command exists yet. `HeatMintEngine` has no `validateBurn` method. HEAT is currently mint-only.

To reduce HEAT exposure without a burn:
- Use `hearth_heat` to swap HEAT→XFG on the Hearth AMM
- Burn via the AMM is backstopped by the treasury's 40% peg defense allocation

### Overcollateralization Gate
- Mint paused if `XFG / outstanding_HEAT × xfg_oracle_price < OVERCOLL_MIN_RATIO`
- Default: 1.5x minimum
- Prevents undercollateralized minting during price drops

## HEARTH AMM

### Constant Product Formula
```
xfg_reserve × heat_reserve = k
```

### Swapping
```
xfg_out = xfg_reserve - (k / (heat_reserve + heat_in × (1 - fee)))
```

### Fees
- 0.3% (30 bps) per swap → LP providers
- No subsidy from Sovereign Wealth Fund

### Liquidity Provision
| Command | Action |
|---------|--------|
| `hearth_add <xfg> <heat>` | Add XFG+HEAT liquidity, receive LP shares |
| `hearth_exit <shares> <min_xfg> <min_heat>` | Remove liquidity, burn LP shares |
| `hearth_info` | Show pool state (reserves, ratio, depth) |
| `hearth_xfg <xfg> <expected> <min_heat>` | Buy HEAT with XFG |
| `hearth_heat <heat> <expected> <min_xfg>` | Sell HEAT for XFG |

### Pool State (from `heat_metrics` RPC)
| Field | Meaning |
|-------|---------|
| heat_supply | Total HEAT in circulation |
| xfg_heat_ratio | reserve_xfg / reserve_heat (pool price) |
| heat_peg_usd | $1.58 peg reference |
| xfg_spot_usd | implied XFG/USD from pool: ratio × peg |

### v11 Activation
- Pool initialized at UPGRADE_HEIGHT_V11 (1111111)
- Initial ratio: 10,000 XFG / 1,000 HEAT (10:1)
- Genesis seed: 10,000 XFG / 1,000 HEAT (10:1 ratio)

## Stability System

### Mode 0 — CPI-Adjusted (flatcoin mode)
- Peg adjusts monthly with US CPI
- HEAT preserves Jan 2009 purchasing power
- EUR display for public-facing output
- BLS CPI-U fetched monthly by CpiOracleService
- CPI bounds: ±10%/year max, ±0.5%/month gate
- Stale > 90 days: peg freezes

### Mode 1 — 5:1 Self-Sovereign
- Fixed $1.50-$2.50 band
- Activates when XFG ≥ $5.00
- PI controller within band

### Mode 2 — 8:1 Full Float (default, best APY)
- PI-only control
- No fixed band
- Basin discovery determines equilibrium

### Basin Discovery
- 3 epoch bootstrap → 7 epoch observation → 4 stable epochs to lock
- Stability range: ±10%
- Exit after 3 epochs outside basin
- Rebalance at 2× basin half-width

### PI Controller
```
rate = PI_KP × error + PI_KI × integral(error)
rate_clamped = clamp(rate, -PI_ABS_MAX_RATE, PI_ABS_MAX_RATE)
```

- Proportional: 0.08 (instant response)
- Integral: 0.015 (accumulates persistent error)
- Base rate: ±50%/yr (basin-locked)
- Hill-damping at extremes (prevents black swan overreaction)

## Treasury Architecture

```
XFG Burn
  ├─ 50% → Eternal Flame (permanent supply burn)
  └─ 50% → Treasury
       ├─ 60% → LP Reserve (CD yield floor + Hearth LP)
       └─ 40% → Peg Defense Balance

Swap Fees (1% atomic swap)
  ├─ 80% → CD Yield Pool
  │    └─ 20% → Protocol XFG accumulator (buyback)
  └─ 20% → Treasury Reserve
       └─ 20% → Bootstrap repayment vault
```

### CD Yield Pipeline
- Floor APY: 2% (backed by treasury LP reserve)
- Buyback: 20% of CD yield pool → XFG accumulator
- Treasury route: 40% → treasury when pool is XFG-heavy (>2:1)

## HEAT CDs

HEAT deposits use permanent term (`0xFFFFFFFF`):
- Min amount: 0.1 HEAT (HEAT_MINT_MIN_HEAT = 1,000,000 atomic)
- Yield from atomic swap fee pool (80% CD share)
- Indistinguishable from XFG CDs via commitment outputs
- Mint premium: 3.33% (HEAT_MINT_PREMIUM_BPS = 333)

### Deposit Terms
| Term | Value | Type |
|------|-------|------|
| HEAT_TERM | 0xFFFFFFFF | Permanent (HEAT CD) |
| DEPOSIT_TERM_LP | 0xFFFFFFFD | Hearth LP share |
| TERM_REGULAR | 0 | Regular XFG transfer |

## RPC Endpoints

### heat_metrics
```
GET /heat_metrics
→ {
    "heat_supply": "1234567890",
    "xfg_heat_ratio": "0.125",
    "heat_peg_usd": "1.58",
    "xfg_spot_usd": "12.64"
  }
```

### Commitment Endpoints (ZK proof claims)
```
/get_commitment - Commitment details
/get_commitment_merkle_proof - Merkle proof for claim
/check_commitment_exists - Existence check
```

## Key Files

| File | Purpose |
|------|---------|
| `src/CryptoNoteConfig.h` | HEAT/HEARTH constants |
| `src/CryptoNoteCore/HeatMintEngine.h/cpp` | Mint validation logic |
| `src/CryptoNoteCore/Blockchain.cpp` | Heat supply, treasury, pool tracking |
| `src/CryptoNoteCore/TransactionExtra.cpp` | Commitment tags (0x08 HEAT, 0xCD COLD) |
| `src/Rpc/RpcServer.cpp` | heat_metrics endpoint |
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | RPC structs |
| `src/SimpleWallet/SimpleWallet.cpp` | Wallet commands (hearth_*, mint_heat) |
| `docs/heat/FUEGO_HEAT_FINAL_DESIGN.md` | Full design spec |
| `docs/heat/hearth-amm-commitment-plan.md` | Commitment output model |

## Usage

```python
# Analyze pool state
xfg = 10000
heat = 1000
k = xfg * heat  # constant product

# Calculate swap output (buy HEAT with XFG)
xfg_in = 100
fee = 30  # 0.3%
xfg_after_fee = xfg_in * (10000 - fee) / 10000  # 99.7
heat_out = heat - (k / (xfg + xfg_after_fee))

# Estimate mint premium
redemption = peg_usd / xfg_price  # how much HEAT per XFG
mint_premium = min_amount * 333 / 10000  # 3.33%
```

## Related Skills

- `fuego-currency` - CD interest, deposits, fee pool (economic layer with HEAT CDs)
- `fuego-swaps` - Cross-chain atomic swaps (complementary to HEARTH)
- `fuego-wallet` - Wallet operations including hearth commands
- `fuego-zk-proofs` - ZK burn proofs for HEAT claims
- `fuego-orchestrator` - Routes to this skill for heat/hearth queries

## References

- `src/CryptoNoteConfig.h` lines 169-295 — all HEAT/HEARTH constants
- `docs/heat/FUEGO_HEAT_FINAL_DESIGN.md` — v2.0 final design
- `docs/heat/hearth-amm-commitment-plan.md` — commitment output model
- `docs/heat/HEAT_STABILITY_FORMULA.md` — PI controller math
- `docs/heat/HEAT_FLATCOIN_FORMULA.md` — flatcoin formula
