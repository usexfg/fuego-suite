# Hidden Amounts for Commitments — Dev Guide

## Current Architecture

### What Commitments Are
Each `TransactionOutputCommitment` is a cryptographic paper claim:
- `commitKey` — one-time spend key (stealth address equivalent)
- `term` — asset type tag (0=XFG, FOREVER=HEAT, finite=CD, magic=pools)
- `amount` — **PLAINTEXT** (visible on-chain)
- `keyImage` — double-spend prevention (same mechanism as regular CryptoNote UTXOs)

### Double-Spend Protection
Key images: `H_p(commitKey) * keyScalar`. Blockchain maintains `m_indexManager.spentKeys()` set. When spent, key image is inserted; duplicates rejected. This works identically for hidden amounts — key images don't reveal amounts.

### All Commitment Types (unified via `term`)

| Term Constant | Value | Purpose |
|---|---|---|
| `TERM_REGULAR` | `0` | Regular XFG transfer |
| `HEAT_TERM` | `0xFFFFFFFF` | H∆T stablecoin |
| `DEPOSIT_TERM_LP` | `0xFFFFFFFD` | Hearth LP shares |
| `DEPOSIT_TERM_POOL_XFG` | `0x504F4C58` | AMM pool XFG (unspendable) |
| `DEPOSIT_TERM_POOL_HEAT` | `0x504F4C48` | AMM pool H∆T (unspendable) |
| `DEPOSIT_TERM_SWAP_RECEIVE_XFG` | `0x53575258` | Swap receive XFG |
| 6–72 epochs | numeric | CD tiers |

### Existing Code (Ready to Use)

| Component | Location | Status |
|---|---|---|
| `pedersen_commit()` / `pedersen_verify()` | `src/crypto/pedersen.h/.cpp` | Compiles, no callers |
| `generate_mlsag()` / `check_mlsag()` | `src/crypto/mlsag.h/.cpp` | Fully implemented, needs wiring |
| `deriveCommitmentKeys()` | `TransactionExtra.cpp:1920` | Returns `commitKey`, `keyScalar`, `keyImage`, `amountMask` |
| `TransactionOutputUnified` | `include/CryptoNote.h:96` | Wire tag `0x5` assigned |
| `TransactionInputUnified` | `include/CryptoNote.h` | Has `pseudoCommitment`, `sigC0` fields |

---

## The Problem with Plaintext Amounts

Currently `amount` is visible on-chain. This leaks:
1. **Transaction value** — observer knows exactly how much was transferred
2. **Ring pool is per-amount** — ring members must match amounts, shrinking anonymity set
3. **Asset type discrimination** — amount patterns reveal H∆T vs XFG vs CD

---

## Hidden Amounts: How It Works

### Pedersen Commitment
```
C = amount * H + mask * G
```
- `H` = generator point (fixed, public)
- `G` = base point (fixed, public)
- `mask` = random blinding factor (secret, per-output)
- `amount` = the value being committed (secret)

**Properties:**
- Hides amount (computationally infeasible to recover)
- Homomorphic: `C1 + C2 = (a1+a2)*H + (m1+m2)*G`
- Binding: can't change amount without knowing mask

### Range Proof (Bulletproofs+)
Proves committed amount is in `[0, 2^64)` without revealing it.
~672 bytes per output (aggregated for multiple outputs).

### Balance Proof (MLSAG)
Proves: `sum(input_C) - sum(output_C) - fee*H = 0`
Without revealing which input is real or what amounts are.

### Pseudo-Output Trick
For each input, spender creates fresh `C_pseudo = amount*H + z*G`.
MLSAG proves `C_pseudo - C_real = (z - mask)*G` (known discrete log).
Enables balance proof without revealing input masks.

---

## Effects & Issues of Hidden Amounts

### Critical Issues

| # | Issue | Severity | Mitigation |
|---|---|---|---|
| 1 | **Inflation attack** — without range proofs, hidden amounts enable creating money from nothing | CRITICAL | Range proofs (BP+) must be mandatory before amounts hide |
| 2 | **Balance proof failure** — MLSAG wiring bug could allow unbalanced transactions | CRITICAL | Two independent defenses: range proof caps + balance equation check |
| 3 | **Legacy compatibility** — pre-v11 outputs have visible amounts | HIGH | Retroactive commitment: `C = amount*H + 0*G` for v10 outputs |
| 4 | **Wire format breaking change** — removing `amount` field breaks old nodes | HIGH | Version-gate: only in v12+ blocks; pre-v12 nodes skip |

### Privacy Improvements

| # | Improvement | Before | After |
|---|---|---|---|
| 1 | **Amount hidden** | Visible on-chain | Only in Pedersen commitment |
| 2 | **Global ring pool** | Per-amount (small) | All outputs (large) |
| 3 | **Asset indistinguishable** | Amount patterns reveal type | All commitment outputs look identical |
| 4 | **Transaction graph** | Input/output counts reveal purpose | Uniform structure |

### Technical Issues

| # | Issue | Details | Mitigation |
|---|---|---|---|
| 1 | **Output size increase** | +32 bytes (commitment) + ~672 bytes (range proof) | Aggregated proofs; ~1.9KB total tx size |
| 2 | **Verification cost** | BP+ verify is ~10x slower than plaintext check | Pippenger multi-scalar multiplication (~500 LOC port from Monero) |
| 3 | **CD interest calculation** | Can't read amount for interest math | Wallet-side calculation; or reveal amount for CD inputs only |
| 4 | **Pool math** | AMM operations need amounts | Use commitment homomorphic properties; or reveal for pool txs |
| 5 | **Treasury tracking** | Can't sum amounts on-chain | Reveal amount for treasury-specific tx types |
| 6 | **Audit/tracing** | Regulators can't verify balances | View keys (optional disclosure) |
| 7 | **Dust attacks** | Small hidden amounts used for tagging | Range proof minimum value; or reject very small amounts |
| 8 | **Decoy selection** | Can't filter by amount for decoy pool | OSPEAD (already in codebase) works without amounts |
| 9 | **Wallet recovery** | Can't scan by amount for known deposits | ECDH + commitment key derivation (already works) |
| 10 | **Block explorer** | Can't show amounts | View key disclosure; or explorer runs full node with view key |

### Consensus Issues

| # | Issue | Details | Mitigation |
|---|---|---|---|
| 1 | **Inflation detection** | Harder to spot if balance proof bypassed | Two independent checks: range proof + balance equation |
| 2 | **Supply calculation** | Can't sum amounts from UTXO set | Track supply via coinbase + burn commitments |
| 3 | **Fee validation** | Fee is plaintext in tx prefix | Fee stays visible (small privacy leak, acceptable) |
| 4 | **Ring signature size** | MLSAG larger than simple ring sig | CLSAG in v13 cuts size back (~384 bytes/input) |

### Implementation Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| 1 | BP+ port from Monero has bugs | Medium | High | Monero test vectors; exhaustive unit tests; testnet-only |
| 2 | MLSAG wiring causes inflation | Low | Critical | Two independent defenses; capped range |
| 3 | `rangeProof` variable-length breaks old deserializers | Medium | Medium | Version-gate serialization |
| 4 | `classifyOutputAsset` misclassifies unified outputs | Low | Medium | Fixed in Phase 0 before unified outputs exist |
| 5 | Phase 1 dual-write doubles output count | High | Low | Wire amount=0 on unified; short window (~10k blocks) |

---

## Implementation Phases

### Phase 0: Constant Cleanup (no fork)
- Rename `DEPOSIT_TERM_FOREVER` → `HEAT_TERM`
- Define `TERM_REGULAR = 0`
- Fix `classifyOutputAsset` for unified outputs

### Phase 1: Bridge — Dual-Write (no fork)
- XFG sends emit both `KeyOutput` + `TransactionOutputUnified` (term=0)
- Commitment outputs indexed in global decoy pool
- Legacy wallets ignore unified outputs (graceful degradation)

### Phase 2: BP+ Range Proofs (soft fork)
- Add `rangeProof` field to `TransactionOutputUnified`
- Wallet generates Pedersen commitment + BP+ range proof
- Blockchain verifies range proof (empty proof = skip for legacy)
- Global ring pool (no per-amount filter)

### Phase 3: MLSAG + Hidden Amounts (hard fork v12)
- All new outputs carry range proofs
- All new inputs use MLSAG
- `amount` field removed from wire format
- Legacy `KeyOutput`/`KeyInput` rejected in v12+ blocks

### Phase 4: Cleanup (no fork)
- Remove `KeyOutput`/`KeyInput` creation code
- Remove legacy scanning paths
- Remove per-amount ring pools

---

## Wire Format (After Hidden Amounts)

### Output: `TransactionOutputUnified` (tag `0x5`)
```
key             (32 bytes)  — stealth address / commitKey
term            (4 bytes)   — asset type tag
commitment      (32 bytes)  — Pedersen C = amount*H + mask*G
rangeProof      (variable)  — BP+ proof (~672 bytes)
```
Total: ~740 bytes per output

### Input: `TransactionInputUnified` (tag `0x5`)
```
outputIndexes   (variable)  — global ring indices
keyImage        (32 bytes)  — double-spend prevention
pseudoCommitment (32 bytes) — C_pseudo for MLSAG balance
sigC0           (32 bytes)  — MLSAG initial challenge
```
Total: ~320 bytes per input (ring size 11)

### Typical Transaction (2 outputs, 2 inputs)
```
Outputs:      2 × 740    = 1,480 bytes
CLSAG:        2 × 384    =   768 bytes
ecdhInfo:     2 × 64     =   128 bytes
Fee:                        8 bytes
Extra:                    ~100 bytes
Total:                   ~2.5 KB
```

---

## References

- `docs/developer/unified-commitment-plan.md` — original plan (pre-audit)
- `docs/implementation/newUnifiedplan.md` — corrected plan (Guardian audit)
- `docs/PRIVACY_ROADMAP.md` — long-term privacy evolution
- `docs/MLSAG_DEV_PLAN.md` — MLSAG algorithm and integration
- Monero `rct/rctSigs.cpp` — CLSAG reference implementation
- Monero `rct/bulletproofs_plus.cc` — BP+ reference implementation
