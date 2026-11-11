# HEAT Cross-Chain Bridging: Path Analysis

## Current State

The codebase already has **three separate cross-chain mechanisms**:

| Mechanism | Status | How It Works |
|---|---|---|
| `HEATClaimer.sol` + `FuegoCheckpointVerifier.sol` | Exists, deployed | Old wXFG model: SP1 zk-proof of Fuego state commitment → mint wHEAT on ETH |
| Adaptor signature atomic swaps (SwapDaemon) | Exists, proven | Trustless swap XFG ↔ ETH/SOL/XMR/BCH via HTLC + adaptor sigs |
| Burn-accounting (swap fee pool) | Exists as pattern | `m_currentEpochSwapFees` tracks protocol accounting. Pattern can be reused. |

**Key constraint:** The new HEAT is a colored coin (`AssetId::HEAT`), not a deposit commitment. It cannot use the old COLD-style commitment infrastructure for bridging without significant rework.

---

## Path A: Lock + ZK Commitment (Old COLD-style)

### What it would need
- User locks HEAT in a `TransactionOutputUnified` with deposit term
- Commitment goes in tx_extra, nullifier prevents double-claim
- Merkle proof of commitment → ETH contract → mints wHEAT
- Unlock: user proves wHEAT burned on ETH (event proof or ZK proof)

### What exists
- `StarkCommitmentGenerator` (secret + commitment + nullifier)
- `HEATClaimer.sol` (merkle proof verification, nullifier tracking)
- `FuegoCheckpointVerifier.sol` (SP1 zk-proof of chain state)
- `CommitmentIndex` (commitment merkle tree)

### Killer problem: unlocking deposit terms
The deposit term system (`DEPOSIT_TERM_*`) was designed for **time-based unlocks** (3mo/9mo/1yr/3yr/5yr). There is no mechanism for "unlock when external event occurs." To add event-based unlocking, you'd need:
- A new `CommitmentType` for HEAT bridge locks
- An unlocking mechanism that verifies ETH burn events (oracle, SP1 proof, or relayer)
- The deposit term system to accept dynamic (non-constant) terms

This is a major engineering effort. Also: the ZK-STARK proving pipeline requires SP1 infra, proving latency, and significant complexity.

### Verdict: HIGH complexity, LOW suitability
The deposit infrastructure was designed for the old wXFG model. Retrofitting it for HEAT colored coin bridging would fight the architecture. **Not recommended for launch.**

---

## Path B: Burn-Accounting Bridge

### Mechanism

```
User burns 100 HEAT on Fuego
    │
    ├── new tx_extra tag (0xD6): "HEAT_BRIDGE_BURN"
    ├── HEAT destroyed from user's balance
    └── m_heatBridgePool += 100     (new accounting pool)

Relayer observes burn tx (new tag 0xD6)
    │
    ├── constructs mapping proof: "tx_hash burned 100 HEAT"
    ├── submits to ETH contract
    └── contract mints 100 wHEAT to user's ETH address

User returns 50 wHEAT to ETH contract
    │
    ├── burn wHEAT on ETH
    ├── relayer observes burn event
    └── submits to Fuego → protocol mints 50 HEAT from pool
```

### Key design points

**Separation from ethereal_xfg:**
```cpp
// New, separate pool — NOT BankingIndex:
uint64_t m_heatBridgePool;  // HEAT burned for bridging, tracked separately
```
`m_ethereal_xfg` tracks XFG burned (→ affects block rewards via EternalFlame). HEAT bridge burns are a separate accounting pool with no effect on emission.

**New tag:**
```cpp
#define TX_EXTRA_HEAT_BRIDGE_BURN  0xD6   // HEAT bridge burn (separate from D5 DEPOSIT_SECRET)
```

**Supply invariant:**
```
total_HEAT_minted_on_Fuego = HEAT_in_circulation + HEAT_in_bridge_pool + HEAT_locked_in_CDs
```

### What exists
- Accounting pattern: `m_currentEpochSwapFees` — same uint64 tracked in Blockchain state, serialized, reorg-safe
- `EthRpcClient` — already watches ETH chain for events
- `TransactionExtra` parsing infra — adding a new tag is mechanical
- Blockchain serialization — adding a uint64 member is mechanical

### What needs building
| Component | Complexity |
|---|---|
| New tx_extra tag + struct + parsing | Trivial (~50 lines) |
| `m_heatBridgePool` in Blockchain state + serialization | Trivial (~30 lines) |
| Validation (enforce tx has assetId=HEAT, no re-use of tag) | Small (~50 lines) |
| ETH contract for wHEAT mint/burn | Medium (~200 lines Solidity) |
| Relayer service (watching Fuego txs, submitting to ETH) | Medium (~300 lines in SwapDaemon) |
| Return path (wHEAT burn → Fuego mint) | Medium (~200 lines) |

### Partial amounts
If user bridges 100 HEAT to ETH, then wants to bridge 50 HEAT back:
- Burn 50 wHEAT on ETH → relayer submits proof → protocol mints 50 HEAT from `m_heatBridgePool`
- No need to "unlock a deposit term" — just mint from the pool
- Pool still has 50 HEAT "available" for future claims

### Relayer trust
- Single relayer is trust-centralized but simple
- Can be decentralized later: multi-relayer consensus, economic stake, or SP1 proofs
- For v1: single relayer (the protocol itself via SwapDaemon) is acceptable

### Verdict: LOW complexity, HIGH utility
Simple accounting, no ZK proofs, no deposit term changes, partial amounts work. Uses proven patterns from the codebase. **Recommended if canonical wHEAT is needed.**

---

## Path C: Atomic Swap

### Mechanism
```
Alice has 100 HEAT on Fuego
Bob has 0.05 ETH on Ethereum
    │
    ├── Bob deploys HTLC on ETH (locks 0.05 ETH)
    ├── Alice offers 100 HEAT with adaptor signature on Fuego
    ├── Alice reveals secret → claims 0.05 ETH on ETH
    └── Bob extracts secret from ETH tx → claims 100 HEAT on Fuego
```

### What exists
| Component | Status |
|---|---|
| Adaptor signature crypto (`src/crypto/adaptor.h`) | Compiled and working |
| MuSig2 key aggregation (`src/crypto/musig2.h`) | Compilation testing, single architecture |
| SwapDaemon (`src/SwapDaemon/SwapDaemon.cpp`) | Compiled and running |
| SwapStateMachine (Proposed → Escrow → Keys → Claim/Refund → Complete) | Compiled |
| SwapDatabase (swap persistence) | Compiled |
| EthRpcClient (HTLC deploy/claim/refund) | Compiled |
| SwapXFG TUI (Go, Bubble Tea) | Compiled and running |
| Price oracle integration | Exists (PriceOracle.cpp) |

### What needs building for HEAT support

| Change | Complexity |
|---|---|
| `SwapTxBuilder` — select HEAT outputs instead of XFG | Small (±50 lines) |
| Price oracle — support HEAT/ETH quote | Small (±30 lines) |
| SwapXFG TUI — add HEAT pair | Trivial (already has PairSOL/ETH/XMR/BCH pattern) |
| ETH contract — support wHEAT mint/burn for atomic swap | Medium (if wHEAT ERC-20 desired) |

### The wHEAT question for atomic swaps
Atomic swaps can work two ways:
1. **HEAT ↔ native ETH**: The HTLC holds ETH. Alice trades HEAT for ETH. No wHEAT needed. This works today.
2. **HEAT ↔ wHEAT (ERC-20)**: The HTLC holds wHEAT tokens. Requires wHEAT to exist and be minted somehow (Path B bridge).

For option 1: atomic swaps work immediately with no new ETH contract.
For option 2: you need Path B bridge to create wHEAT, then atomic swaps can trade it.

### Verdict: LOW complexity, TRUSTLESS, LAUNCH-READY for native ETH
For swapping HEAT ↔ native ETH, the infrastructure is 90% done. Only the SwapTxBuilder needs HEAT-aware asset selection. **Best option for launch — works today, no new infrastructure.**

---

## Comparison

| Dimension | Path A (Lock+ZK) | Path B (Burn-Accounting) | Path C (Atomic Swap) |
|---|---|---|---|
| Trust model | Trustless (ZK proofs) | Trusted relayer | Trustless |
| Complexity | Very high | Low | Low |
| Existing infra | 40% | 60% (pattern reuse) | 80% |
| Partial amounts | Difficult | Trivial | N/A (atomic = full) |
| wHEAT composability | Yes | Yes | Only with Path B |
| ZK proof needed | Yes (SP1) | No | No |
| New ETH contract | Minor | Medium | None (or minimal) |
| Launch readiness | v12+ | v12+ | **v11** |

---

## Recommendation

**Three layers, shipped incrementally:**

### v11 launch: Atomic swaps only
The existing SwapDaemon + SwapXFG handles HEAT ↔ native ETH today with minor changes:
1. Make `SwapTxBuilder` asset-aware (select HEAT outputs)
2. Add HEAT pair to SwapXFG TUI
3. No new bridge infrastructure needed
4. Price oracle supports HEAT/ETH

This gets HEAT cross-chain immediately with zero new trust assumptions.

### v12+: Burn-accounting bridge (if needed)
Add Path B when there's proven demand for DeFi composability on ETH:
1. New tx_extra tag `0xD6 HEAT_BRIDGE_BURN`
2. `m_heatBridgePool` in Blockchain accounting
3. Simple Relayer in SwapDaemon
4. ETH ERC-20 wHEAT contract with mint/burn

This creates canonical wHEAT for DeFi while keeping the Fuego side simple (no deposit terms, no ZK proofs, no unlock complexity).

### Path A: Never (unless absolutely required)
The lock+ZK approach was designed for the old wXFG model. The new HEAT colored coin doesn't need it. Path B + Path C cover every use case with less complexity and lower audit surface.

---

## Comparison to existing Burn2Mint (old HEAT)

The old model was: burn XFG → STARK proof → mint wXFG-like HEAT on ETH.
The new model is: burn XFG on Fuego → mint HEAT colored coin on Fuego → atomic swap for ETH.

The key difference: HEAT now exists on Fuego L1 as a first-class asset. It doesn't need an L2 bridge to exist. The bridge is only for DeFi composability on other chains.
