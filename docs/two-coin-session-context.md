# Two-Coin Session — Compressed Context (2026-05-11)

## Architecture Decisions

### Two-Coin Model: XFG + HEAT
- **XFG:** Hard-capped reserve coin (8M). Existing. Unchanged.
- **HEAT:** Algorithmic colored stablecoin. Minted by burning XFG (one-way, permanent). Burned XFG recycles into block rewards via EternalFlame.
- **PARA/iMOB:** Removed. Two-coin only.
- **COLD/XFG CDs:** Removed. HEAT CDs only (avoids cluttering CommitmentIndex with two asset types).

### HEAT Initial Parameters
- Redemption price: **0.2 XFG per HEAT** (1/5). Not 0.5.
- Mint: burn XFG → receive HEAT at current PI-adjusted redemption price.
- Exit: sell HEAT on Hearth AMM. No reverse-mint.
- HEAT supply: no hard cap.

### Hearth AMM
- Single XFG/HEAT constant-product pool embedded in consensus.
- Bootstrap at v10 fork: 100 XFG + 500 HEAT.
- Fee: 1% (100 bps), accumulates for CD yield pool.
- TWAP: cumulative spot price over epoch, resists single-block manipulation.
- Tags: `0xF0` (swap), `0xF1` (add liquidity), `0xF2` (remove liquidity).

### PI Controller
- Named: Deviation (not "error"), integralDeviation, redemptionRate.
- Two modes: XFG-only (oracle=0) or USD-targeting (oracle>0).
- USD mode: `xfgPerUsd` from OracleEngine, targets $1.00. Keeps HEAT small-value even if XFG moons.
- OracleEngine: `feedSwapPrices()` accepts swap TWAPs. Median aggregation.

### HEAT CD Yield Pipeline
- 80% swap fees → CD pool. 20% → treasury.
- PI controller modulates HEAT CD buyback: `spendRate = 1.0 + clamp(rate, -0.5, +2.0)`.
- Protocol buys HEAT from Hearth at epoch boundary.
- Reserve caps: 2× base pool, overflow → treasury.

### Per-Asset Decoy Pool
- `encodeAssetAmount(amount, assetId) = (amount << 8) | assetId`.
- HEAT and XFG decoys are separate pools. Privacy-critical — cross-contamination breaks indistinguishability.

### Privacy — Commitment Output Model (Planned)
- Route HEAT mint + AMM through existing `TransactionOutputCommitment` type.
- All deposit operations share identical on-chain footprint.
- Only fingerprint: term>0 = CD, term=0 = non-locked (mint or AMM).
- HEAT tiers must match XFG tiers to prevent amount-based fingerprinting.
- Plan: `docs/heath-amm-commitment-plan.md`

## Deliverables Built

| Phase | What | Files |
|---|---|---|
| 0 | V10 fork scaffolding (UPGRADE_HEIGHT_V10, Currency) | 4 modified |
| 1 | FixedPoint64 Q64.64 math (__int128, exp/ln) | 2 new — 24 tests |
| 2 | HEAT colored coin + AssetId | 3 new, 5 modified |
| 3 | Hearth AMM consensus (swap/LP math) | 2 new, 3 modified — 10 tests |
| 4+5 | PI Controller + CD yield pipeline | 2 new, 3 modified — 7 tests |
| 6 | RPC endpoints + wallet CLI + Go TUI | 5 modified |
| 7 | Integration tests | 2 new — 1 integration test |

**Total:** 13 new files, 28 modified files. 42/42 tests pass. 22/22 build targets.

## New Files Created

```
src/Common/FixedPoint.h/cpp         — Q64.64 deterministic math
src/CryptoNoteCore/AssetId.h         — XFG=0x00, HEAT=0x01
src/CryptoNoteCore/AmmPool.h/cpp     — Hearth AMM math
src/CryptoNoteCore/HeatMintEngine.h/cpp — XFG burn → HEAT mint validation
src/CryptoNoteCore/PiController.h/cpp — PI controller with USD-targeting
src/CryptoNoteCore/OracleEngine.h/cpp — XFG/USD price feed
tests/CoreTests/FixedPoint.cpp       — 24 tests
tests/CoreTests/HeatAmm.cpp          — 10 AMM + 7 PI + 1 integration
fuego-two-coin-plan.md               — Full implementation plan
fuego-cd-yield-plan.md               — CD yield distribution design
heath-bridge-analysis.md             — Cross-chain bridging analysis
docs/heath-amm-commitment-plan.md    — Privacy commitment model plan
```

## Key Design Decisions Log

| Decision | Resolution | Rationale |
|---|---|---|
| Two-coin vs tri-coin | Two-coin (XFG + HEAT only) | PI controller proven without sink token (RAI model) |
| HEAT mint source | Burn XFG (not lock) | Simpler. EternalFlame recycles into block rewards. |
| HEAT CD yield currency | HEAT (bought with XFG fees from Hearth) | Creates structural demand. PI modulates buy pressure. |
| PI factor by XFG or HEAT? | XFG amounts in CD pool | PI sets policy; Hearth AMM determines execution price. |
| Initial redemption price | 0.2 XFG/HEAT (changed from 0.5) | More headroom for PI. Signals HEAT is distinct from XFG. |
| Oracle feed | Swap TWAPs → XFG/USD | Uses existing SwapDaemon infra. No external oracle dependency. |
| Reserve for CD yield? | Yes, capped at 2× base pool | Allows boosted yields during positive rate. Prevents hoarding. |
| HEAT tiers vs XFG tiers | Must match for privacy | Different values create fingerprinting in commitment model. |

## Atomic Swap Fee

- 1% fee on EACH side of atomic swaps (2% total).
- 80/20 split: CD pool / treasury.
- Already implemented in existing commitment infrastructure.

## Build State

- C++17 standard. Boost coroutine + context.
- 22/22 targets (fuegod, wallet-cli, walletd, optimizer).
- 42/42 tests pass.
- Pre-existing P2P NetNode errors fixed (UPnP, #endif, try_ping).
- Full codebase intact — no scorched earth. SwapDaemon, crypto primitives, BankingIndex all present.
