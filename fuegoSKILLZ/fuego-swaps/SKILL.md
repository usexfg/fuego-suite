---
name: fuego-swaps
description: "Fuego domain expert for atomic swaps, LP pools, adaptor signatures, swap state machine, and swap fees. Expert in COMIT protocol and cross-chain swaps."
risk: low
source: user-provided
---

# Fuego Swaps Expert

Domain expert for Fuego atomic swaps and the HEARTH AMM: state machine, LP pools, adaptor signatures, swap fees, and HEARTH constant-product XFG/HEAT pool.

## Scope

- **Atomic Swaps**: Cross-chain swaps (SOL, ETH, XMR, BCH)
- **HEARTH AMM**: On-chain XFG/HEAT constant product AMM, liquidity provision, swapping
- **State Machine**: Swap states and transitions
- **Adaptor Signatures**: MuSig2-based adaptor signatures
- **Swap Fees**: 1% atomic swap fee, 0.3% HEARTH LP fee

## Trigger Set

**Should trigger on:**
- "atomic swap", "swap", "swap state"
- "LP pool", "liquidity", "liquidity pool"
- "adaptor signature", "presig", "secret reveal"
- "swap fee", "htlc", "refund"
- "swap", "SOL", "ETH", "XMR", "BCH"
- "hearth", "hearth amm", "hearth pool", "xfg/heat"
- "constant product", "hearth_add", "hearth_xfg", "hearth_heat"
- "lp shares", "pool reserves", "xfg_heat_ratio"

**Should NOT trigger on:**
- Non-Fuego swap questions
- Generic DeFi unrelated to Fuego swaps

## Swap States

**Source:** `src/SwapDaemon/SwapTypes.h`

### Active States (Adaptor Protocol v1)
```
ADAPTOR_KEYS_EXCHANGED = 10   # Keys distributed
ADAPTOR_ESCROW_FUNDED = 11    # XFG locked in escrow
ADAPTOR_PRESIGS_READY = 12    # Presignatures ready
ADAPTOR_CTR_LOCKED = 13       # Counterparty locked
ADAPTOR_SECRET_REVEALED = 14  # Secret revealed
ADAPTOR_XFG_SPENT = 15        # XFG claimed by counterparty
ADAPTOR_REFUNDED = 16         # Refund on timeout
```

### AFK States (Non-interactive v2)
```
AFK_OFFER_LOCKED = 100        # Maker locked XFG on-chain
AFK_OFFER_ACCEPTED = 101      # Taker locked coins on counterparty chain
AFK_CLAIMED = 102             # Both sides claimed (swap finished)
AFK_REFUNDED = 103            # Maker refunded XFG after timeout
```

### Legacy States (Inactive)
```
INITIATED = 0, XFG_LOCKED = 1, CTR_LOCKED = 2,
XFG_CLAIMED = 3, CTR_CLAIMED = 4,
XFG_REFUNDED = 5, CTR_REFUNDED = 6, FAILED = 7
```

## Swap Pairs

| ID | Chain | Status |
|----|-------|--------|
| 0 | SOL | Active |
| 1 | ETH | Active |
| 2 | XMR | Active |
| 3 | BCH | Active |
| 4 | ARB | Active |
| 5 | BASE | Active |

## Fee Structure

**Source:** `src/CryptoNoteConfig.h`

| Parameter | Value |
|-----------|-------|
| SWAP_FEE_RATE_BPS | 100 (1%) |
| SWAP_FEE_CD_SHARE_PCT | 80 |
| SWAP_FEE_TREASURY_SHARE_PCT | 20 |

**Fee Calculation:**
```
fee = swap_amount × SWAP_FEE_RATE_BPS / 10000
```

**Distribution:**
- 80% → CD yield pool
- 20% → Chain treasury

## HEARTH AMM (XFG/HEAT Pool)

**Activation:** UPGRADE_HEIGHT_V11 (1111111) — HEATWAVE fork

### Constant Product Formula
```
xfg_reserve × heat_reserve = k
```

### Pool Parameters
| Parameter | Value |
|-----------|-------|
| HEARTH_FEE_BPS | 30 (0.3%) |
| Pool seed | 10,000 XFG / 1,000 HEAT (10:1 ratio) |
| Initial ratio | 10,000 XFG / 1,000 HEAT (10:1) |
| Min XFG depth target | 5,000 XFG |
| Min HEAT depth target | 5,000 HEAT |

### Swap Calculation
```
xfg_out = xfg_reserve - (k / (heat_reserve + heat_in × (1 - fee)))

# Example: buy HEAT with 100 XFG
fee = 100 × 30 / 10000 = 0.3 XFG
xfg_effective = 100 - 0.3 = 99.7
heat_out = heat_reserve - (k / (xfg_reserve + 99.7))
```

### Liquidity Operations

| Wallet Command | Action |
|----------------|--------|
| `hearth_add <xfg> <heat>` | Deposit XFG+HEAT, receive LP shares |
| `hearth_exit <shares> <min_xfg> <min_heat>` | Burn LP shares, withdraw |
| `hearth_xfg <xfg> <expected> <min_heat>` | Buy HEAT with XFG |
| `hearth_heat <heat> <expected> <min_xfg>` | Sell HEAT for XFG |
| `hearth_info` | Pool reserves, ratio, depth |

### LP Share Math
- LP shares minted proportional to `sqrt(xfg_deposit × heat_deposit)`
- Shares burned on exit, pool reserves decrease proportionally
- LP earns 0.3% fee on all swaps

### Pool State (from heat_metrics RPC)
- `heat_supply` — total HEAT in circulation
- `xfg_heat_ratio` — reserve_xfg / reserve_heat (pool price)
- `heat_peg_usd` — $1.58 reference peg
- `xfg_spot_usd` — implied XFG/USD: ratio × peg

### Special Pool Accounts
| Term Marker | Meaning |
|-------------|---------|
| DEPOSIT_TERM_POOL_XFG (0x504F4C58 / 'POLX') | AMM pool XFG reserves |
| DEPOSIT_TERM_POOL_HEAT (0x504F4C48 / 'POLH') | AMM pool HEAT reserves |
| DEPOSIT_TERM_SWAP_RECEIVE_XFG (0x53575258 / 'SWRX') | User receives XFG from HEAT→XFG swap |

## Adaptor Signatures

**Protocol:** COMIT with MuSig2

1. **Key Exchange**: Both parties generate adaptor keys
2. **Escrow Fund**: XFG locked in adaptor contract
3. **Presignatures**: Generate presigs for both outcomes
4. **Counterparty Lock**: Counterparty locks their funds
5. **Secret Reveal**: Reveal secret to claim, or refund after timeout

## Usage

```python
from references import SwapsExpert

expert = SwapsExpert(source_dir="/Users/aejt/fuego")

# Analyze swap fee
fee, net = expert.analyze_swap_fee(100000000)
# fee: 1000000 (1%), net: 90000000

# Validate state transition
valid = expert.validate_state_transition(11, 12)
# True if transition is valid

# Get swap states
states = expert.get_swap_states()
# Returns active states enum
```

## Key Files

| File | Purpose |
|------|---------|
| `src/SwapDaemon/SwapTypes.h` | State enum, constants |
| `src/SwapDaemon/SwapStateMachine.cpp` | State transitions |
| `src/SwapDaemon/SwapLedger.cpp` | Swap storage |
| `src/crypto/musig2.h` | MuSig2 adaptor signatures |
| `src/CryptoNoteConfig.h` | HEARTH_FEE_BPS, pool seeds, initial ratio |
| `src/SimpleWallet/SimpleWallet.cpp` | hearth_add/hearth_xfg/hearth_heat/hearth_exit |
| `src/Rpc/RpcServer.cpp` | heat_metrics endpoint |
| `docs/heat/hearth-amm-commitment-plan.md` | Commitment output model |

## Utilities

- Use `fuego-rag` for semantic code search
- Use `fuego-codebase-mapper` for file/function search
