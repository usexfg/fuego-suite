# Fuego v10 Protocol Upgrade — Harbinja

| | |
|---|---|
| **Codename** | Harbinja |
| **Block Version** | `BLOCK_MAJOR_VERSION_10` |
| **Activation Height** | 1,100,000 |
| **Release Version** | v1.10.00-PROPHETS (build 1153) |
| **Testnet Activation** | Block 25 |

---

## New Features

### Two-Coin Ecosystem (HEAT + Hearth AMM)
- HEAT colored-coin stablecoin — minted by burning XFG with PI-controlled supply targeting $1 peg
- Hearth AMM — constant-product XFG/HEAT pool with permissionless liquidity provision
- LP token algebra — add/remove liquidity, withdraw proportional pool share
- Per-asset balance validation — XFG, HEAT, and LP share inputs/outputs tracked and enforced independently per transaction
- Osavvirsak burn-adjusted block rewards — burned coins re-enter the emission curve, extending tail emission
- Banking fee infrastructure — 0.1% deposit fee on HEAT routed to stakers via coinbase split

### Dynamic Ring Sizes (DynaMaxin)
- Adaptive decoy selection via OSPEAD (logarithmic age-bin sampling)
- Minimum ring size raised from 2 to 8
- Target ring sizes: {18, 15, 12, 11, 10, 9, 8} selected dynamically based on output pool depth
- Max ring size remains 32

### Dandelion++ Network Privacy
- Stem-fluff transaction relay — per-hop coin-flip decides stem continuation (90% stay, max 10 hops)
- Separate Dandelion++ path for swap offers (80% stay, max 5 hops) to prevent metadata correlation
- Embargo timer: 30s before fluff broadcast
- Epoch rotation: 90s for stem phase key rotation
- Pre-v10: all transactions flood the network directly

### @Fire Aliases
- Human-readable `@fire` addresses registered on-chain
- Registration fee: 1 XFG to Fuego Development Fund (`@fuegoxfg`)
- Standard individual aliases only (group/multi-sig tandalias in design phase)

### SwapXFG Atomic Swaps
- Cross-chain atomic swaps via HTLC with integrated fee routing
- 1% swap fee collected from HTLC claims → YEM Reserve
- 1% sender surcharge → YEM Reserve
- Swap offers relayed via Dandelion++ for privacy

### Commitment Deposits (Ring-CT)
- `TransactionOutputCommitment` replaces legacy `MultisignatureOutput` — Pedersen commitments with 1-of-4 OR amount proofs
- `TransactionInputCommitmentSpend` replaces legacy `MultisignatureInput` — withdrawal inputs with key image + claimed interest

---

## Economic Changes

| Parameter | v9 (Godflame) | v10 (Harbinja) |
|---|---|---|
| **Transaction fee** | 0.008 XFG (80,000 atomic) | **0.0008 XFG (8,000 atomic)** — 10x reduction |
| **Dust threshold** | 0.002 XFG (20,000 atomic) | **0.0001 XFG (1,000 atomic)** |
| **Min ring size** | 2 | **8** |
| **Block reward** | Standard tail emission | **Osavvirsak burn-adjusted re-emission** |
| **Coinbase validation** | Miners may claim less than reward | **Strict exact-match enforced** |

---

## Consensus Changes

### Difficulty Algorithm
| | v9 | v10 |
|---|---|---|
| **Algorithm** | LWMA (N=60) | **LWMA-1 (N=36)** |
| **Function** | `nextDifficultyV5()` | `nextDifficultyV6()` |
| **Solvetime clamp** | T/2 – 8T | **T/3 – 6T** (symmetric) |
| **Rounding** | Standard | **Clean number rounding** |

### Penalty Formula Bugfix
Monero-style 64-bit safe variant: intermediate multiplication uses 64-bit to prevent overflow on ARM/32-bit platforms. Pre-v10 formula preserved for historical block validation.

---

## YEM v10 Skeleton
- Swap fee pool activated — 1% of HTLC claims + 1% sender surcharge routed to YEM Reserve
- Legacy bond registration for pre-v10 COLD deposit migration
- YEM v10 allocation: 100% of swap fees → YEM Reserve (10,000 bps)
- Full Sovereign Yield Engine (coupons, maturities, coinbase, scalp, SWF, smoothing) activates at v11

---

## Testnet
- Upgrade height: block 25
- Testnet daemon version: 10.00.016
- Genesis extra nonce encodes v10 upgrade height

---

*Previous: v9 Godflame (height 826,420)*
*Next: v11 HEATWAVE (height 1,111,111)*
