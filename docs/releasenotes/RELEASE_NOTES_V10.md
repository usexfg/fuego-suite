# Fuego Suite v1.10.02 — Wildfire Release

| | |
|---|---|
| **Codename** | Wildfire |
| **Block Version** | `BLOCK_MAJOR_VERSION_10` |
| **Activation Height** | 1,100,000 |
| **Release Version** | v1.10.02 |
| **Testnet Activation** | Block 25 |

Atomic swaps, cross-chain expansion, privacy hardening, and a broad security pass.

---

## Cross-Chain Atomic Swaps (SwapXFG)

### Supported Chains

| Chain | Adapter | Keying |
|---|---|---|
| Bitcoin Cash | `BchChainClient` | secp256k1 |
| Ethereum (+ L2: **Arbitrum**, **Base**) | `EthChainClient` | EIP-155 secp256k1 offline signing + `eth_sendRawTransaction` |
| Monero | `XmrChainClient` | Ed25519 reserve-proof verification |
| Solana | `SolChainClient` | Ed25519 + base58 |

- Polymorphic `ChainRegistry` dispatches all swap pairs through a plugin `IChainClient` interface
- Cross-chain timelock-ordering safety (`SwapTimelock`) across all supported pairs

### AFK Auto-Execution Engine
- Soft-order auto-execution via OfferManager — griefing protection with taker rate-limiting, partial fills, price-drift auto-cancel
- Headless daemon mode (`--service`) with loopback JSON status endpoint (StatusServer)
- Taker P2P swap-request relay

### Swap Privacy
- Swap offers relayed via Dandelion++ (separate stem path: 80% stay, max 5 hops)

---

## Privacy

### Dynamic Ring Sizes (DynaMaxin / OSPEAD)
- Minimum mixin **2 → 8**; max remains **32**
- Dynamic ring size buckets: **{32, 16, 8}** — selected based on output pool depth
- OSPEAD logarithmic age-bin sampling — decoy spend-probability filter wired into tx build path
- Mixin upper bound gated behind `keeped_by_block` for sync-era compatibility

### Dandelion++ Network Privacy
- Stem-fluff transaction relay — per-hop coin-flip (90% stay, max 10 hops)
- Embargo timer: 30s; epoch rotation: 90s
- Relay constants extracted to config

### Alias Registration Privacy
- Per-transaction sub-address rotation during `@fire` alias registration
- Ring-signature mixin applied to alias registration transactions

---

## @Fire Aliases
- Human-readable `@fire` addresses registered on-chain
- Registration fee: 1 XFG to Fuego Development Fund (`@fuegoxfg`)
- Standard individual aliases only

---

## Security

### Swap Security
- Adaptor-secret at-rest encryption: CryptoNight-KDF + ChaCha8 + HMAC-SHA256; derived from wallet key, never persisted
- Ed25519 authentication on all swap P2P messages
- Small-subgroup point rejection in adaptor/DLEQ verifiers
- RPC error responses sanitized

### Wallet & P2P Hardening
- Wallet container integrity upgraded to HMAC-SHA256 (v7); wallet files set to `0600`
- MLSAG nonce zeroing on all paths; RNG fork-safety; modulo-bias elimination
- Mempool DoS hardening: tx size 4MB cap, extra 4KB cap, 800MB pool limit
- P2P: Levin max packet size 1MB→100MB
- 11 CRITICAL/HIGH audit findings resolved (C1-C4, H1-H9)

---

## Consensus Changes

### Difficulty Algorithm

| | v9 | v10 |
|---|---|---|
| **Algorithm** | LWMA (N=60) | **LWMA-1 (N=36)** |
| **Function** | `nextDifficultyV5()` | `nextDifficultyV6()` |
| **Solvetime clamp** | T/2 – 8T | **T/3 – 6T** |
| **Rounding** | Standard | Clean number rounding |

### Penalty Formula Bugfix
64-bit safe intermediate multiplication — prevents overflow on ARM/32-bit. Pre-v10 formula preserved for historical blocks.

---

## Economic Changes

| Parameter | v9 | v10 |
|---|---|---|
| **Transaction fee** | 0.008 XFG | **0.0008 XFG** (10× reduction) |
| **Dust threshold** | 0.002 XFG | **0.0001 XFG** |
| **Min ring size** | 2 | **8** |
| **Coinbase validation** | Lax | **Strict exact-match** |

---

## Infrastructure

- `IndexManager` delegates blockchain indices; async `rebuildCache` with lock gate
- ARM hardware AES on Apple Silicon
- arm64 Homebrew prefix detection for `jsoncpp`
- Docs: SPV light-client ADR, swap expansion guide

---

## Commit Breakdown (247 total since v1.9.3)

| Area | Count |
|---|---|
| Build + CI | 57 |
| Refactor + misc | 45 |
| HEAT + Hearth (v11 prep) | 39 |
| Swaps | 31 |
| CDs + Yield (v11 prep) | 27 |
| Docs | 27 |
| v11/v12 scaffolding | 22 |
| Wallet UX | 13 |
| Privacy | 11 |
| Deposit framework (v11 prep) | 8 |
| Security | 7 |
| Index + P2P infra | 6 |
| Aliases | 4 |
| Per-asset + auth | 4 |
| Consensus + economic | 3 |

---

*Previous: v9 Godflame (height 826,420)*
*Next: v11 HEATWAVE (height 1,111,111) — HEAT + Hearth AMM + CDs + YEM*
