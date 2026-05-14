# Fuego Deposit Privacy — Honest Assessment & Roadmap
# Branch: HE4T | Updated: 2026-03-13

---

## What Actually Works Today (v10)

| Feature | Status | Privacy Gain |
|---------|--------|-------------|
| Ring-sig commitment deposits (mixin >= 8) | Working | Hides WHICH deposit is spent |
| CommitmentIndex per-amount decoy selection | Working | Decoy pool for withdrawal rings |
| HEAT burns as permanent decoys | Working | Bulks up ring pool (key discarded, unspendable) |
| Encrypted deposit secret (0xD5, chacha8+ECDH) | Implemented | Wallet recovery from seed |
| Stealth addresses (one-time keys) | Inherited | Hides recipient |
| Key images (double-spend prevention) | Working | Standard CryptoNote |
| Elderfier commitment H(spendPub\|\|ephemeralPub) | Working | No raw EFier address on-chain |
| Alias addressHash H(address) | Working | No raw owner address on-chain |

### What v10 Ring-Sig Deposits Actually Hide
- An observer sees: "someone withdrew ONE of these 9 commitment outputs of amount X"
- They cannot tell which one — ring signature hides the real output
- HEAT burns are indistinguishable from COLD/EFier outputs (same struct)
- Term is visible but doesn't leak which ring member is real (all expired)

### What v10 Does NOT Hide
- **Amounts are plaintext.** `TransactionOutput.amount` is visible on-chain
- **Ring pool is per-amount.** Observer knows the amount from the ring members
- **Deposit type is visible.** tx_extra tags (0x08/0xCD/0xEF) reveal COLD vs HEAT vs EFier.
  Only hidden when v11 unified output type makes all outputs structurally identical.
- **Transaction graph.** Input/output counts reveal tx purpose (deposit vs transfer)
- **Network layer.** No IP-level privacy (no Dandelion++)

---

## What We Built But Doesn't Do Anything Yet

| Code | Files | Status |
|------|-------|--------|
| Pedersen commitments (C = amount*H + mask*G) | pedersen.h/.cpp | Compiles, no callers |
| 1-of-4 OR membership proof | tier_proof.h/.cpp | Compiles, no callers |
| amountCommitment field in TransactionOutputCommitment | CryptoNote.h | Serialized to wire, never verified |
| amountProof field in TransactionOutputCommitment | CryptoNote.h | Serialized to wire, never verified |
| amountMask derivation in deriveCommitmentKeys() | TransactionExtra.cpp | Computed, never used |
| MembershipProof serializer | CryptoNoteSerialization.cpp | Exists, never called |

**These add 288 bytes per commitment output for zero privacy.**

The reason: hiding amounts requires MLSAG (multi-layered ring signatures) to prove
balance through commitment arithmetic. Without MLSAG, the visible amount field is
authoritative and the commitments are dead weight.

### Decision Needed: Strip or Keep?

**Strip (recommended):** Remove amountCommitment + amountProof from the wire format.
Keep pedersen.cpp and tier_proof.cpp in the tree for when MLSAG lands. Saves 288
bytes per output. No false sense of security. Clean chain.

**Keep:** "Dual-bookkeeping rehearsal." But nobody verifies them, so bugs hide until
the real switchover. And the wire format may change when MLSAG is integrated anyway.

---

## The Dependency Chain (Why We Can't Just "Turn On" Hidden Amounts)

```
Hidden amounts require:
  -> Balance proof: sum(input_C) = sum(output_C) + fee*H
     -> But ring sigs hide WHICH input is real
        -> Need ring sig that ALSO proves commitment arithmetic
           -> That's MLSAG (Multi-Layered Linkable Spontaneous Ad-hoc Group sig)

MLSAG unlocks:
  1. Hidden amounts (balance proof via commitment arithmetic)
  2. Global ring pool (ring members don't need matching amounts)
  3. Larger anonymity set (all commitment outputs are eligible decoys)

Without MLSAG:
  - Amounts must stay visible (no balance proof possible)
  - Ring pool must be per-amount (prevents inflation attack)
  - Pedersen commitments are theater
```

---

## Current Roadmap (ordered by dependency)

### Phase 1: DONE — Ring-Sig Commitment Deposits (v10)
Deposits use ring signatures with mixin >= 8. HEAT burns pad the decoy pool.
Amounts visible. Per-amount ring selection. This is where we are.

### Phase 2: Wire Format Cleanup (v10, next)
- Strip amountCommitment + amountProof from TransactionOutputCommitment wire format
- Wire format becomes: commitKey(32) + term(4) = 36 bytes
- Keep pedersen.cpp, tier_proof.cpp in tree (no callers yet, but code is ready)
- Note: 0xD5 is wallet recovery (encrypted deposit secret), NOT a privacy feature.
  Type tags (0x08/0xCD/0xEF) remain visible until v11 unified output type.

### Phase 3: Wallet Privacy Improvements (no fork needed)
- OSPEAD already in codebase (OSPEADDecoySelection.cpp) — adaptive decoy selection
  using logarithmic age bins with empirical spend pattern learning. Superset of
  MRL-0004 gamma distribution. Already wired into DynamicRingSizeCalculator.
- 2-output uniformity: always emit exactly 2 outputs per deposit tx
  (commitment output + change, pad with 0-amount dummy if needed)
- Estimated: ~400 LOC, wallet-side only, big privacy ROI

### Phase 4: Dandelion++ Transaction Relay (no fork needed)
- Stem phase: relay tx to single random peer (1-10 hops)
- Fluff phase: broadcast normally after stem
- Prevents ISP/node-level sender IP identification
- Estimated: ~800 LOC, P2P layer

### Phase 5: BP+ / CLSAG + Unified Output Type (v11 fork — the big one)

Replace simple ring sigs with CLSAG and visible amounts with Bulletproofs+.
Two outputs per tx (recipient + change). Global ring pool. No denominations,
no bin Merkle trees, no 16-output padding. Same privacy, 13x smaller transactions.

**Why BP+ over denominations:** The denomination system (N=28, 16 padded outputs,
28 Merkle trees, epoch sub-bins, flooding analysis) was designed to avoid BP+
complexity (~2500 LOC). But it ended up at ~3500-4000 LOC of its own system
complexity AND produced 32 KB transactions. BP+ is ~2500 LOC of well-understood
crypto, produces ~2.5 KB transactions, and Monero's implementation is a direct
port reference.

CLSAG proves:
1. Signer knows the secret key for ONE ring member
2. The commitment difference (C_input - C_pseudo) is zero for the real member
3. Key image linkability (double-spend prevention)

BP+ proves:
- Each output commitment C = amount*H + mask*G commits to a value in [0, 2^64)
- No denomination system needed — any amount is valid

Balance proof:
```
sum(input_pseudo_C) = sum(output_C) + fee*H
```

This enables:
- **Hidden amounts:** no amount field, only Pedersen commitment authoritative
- **Arbitrary amounts:** no denomination decomposition needed
- **Global ring pool:** all outputs are eligible decoys (same as Monero)
- **Balance proof:** commitment arithmetic across all inputs/outputs
- **Unified output type:** deposits, transfers, burns — all indistinguishable
- **2 outputs per tx:** recipient + change (not 16 padded)

Wire format per output: key(32) + term(4) + commitment(32) = 68 bytes
BP+ range proof: ~672 bytes per output (aggregated for 2 outputs: ~738 bytes)
CLSAG per input: c_0(32) + s[ring_size] * 32 = ~320 bytes at ring size 11

```
Per transaction (2 outputs, 2 inputs, ring size 11):
  Outputs:    2 × 68      = 136 bytes
  BP+ proof:  1 × 738     = 738 bytes (aggregated for 2 outputs)
  CLSAG:      2 × 384     = 768 bytes
  ecdhInfo:   2 × 64      = 128 bytes
  Fee:                       8 bytes
  Extra:                   ~100 bytes
  Total:                   ~1.9 KB
```

Compare: denomination system would be ~32 KB for the same transfer.

What's needed:
```
clsag.h/.cpp:
  - CLSAG sign/verify (~600 LOC, port from Monero rct/rctSigs.cpp)
  - Single-layer ring sig with commitment balance (replaces MLSAG)
  - Key image computation

bulletproofs_plus.h/.cpp:
  - BP+ range proof generation and verification (~2500 LOC)
  - Aggregated proof for multiple outputs
  - Inner-product argument
  - Port from Monero rct/bulletproofs_plus.cc (battle-tested since 2022)

multiexp.cpp:
  - Pippenger multi-scalar multiplication (~500 LOC)
  - Required for efficient BP+ verify
  - Port from Monero common/multiexp.cc

Integration:
  - Unified output type (replaces TransactionOutputCommitment + KeyOutput)
  - Global ring pool (single pool for all outputs, no per-amount segregation)
  - ecdhInfo encrypted amounts per output (chacha8 + ECDH, already have this)
  - Wallet: build CLSAG ring, generate BP+ range proofs
  - OSPEAD decoy selection (already exists) adapts to global pool
```

Estimated: ~3600 LOC crypto (CLSAG 600 + BP+ 2500 + multiexp 500)
           ~1000 LOC integration (simpler than denominations — no bins, no padding)
Reference: Monero's rct/rctSigs.cpp (CLSAG), rct/bulletproofs_plus.cc (BP+)

### Phase 6: Triptych — Logarithmic Ring Proofs (v12 fork)

Replace CLSAG (linear ring size) with Triptych (logarithmic). The anonymity set
expands from a fixed ring of 11 to the ENTIRE output set — every output ever
created on the chain.

With BP+ (no denominations), Triptych operates over a single global Merkle tree
of all outputs. No per-denomination binning needed — just one tree.

This is the "reference bin root, prove membership" model formalized by
Wicht & Cachin in "Toxic Decoys" (PoPETs 2025). Their pruning model applies
here too — epoch sub-trees within the global Merkle tree can be pruned when
all outputs in the sub-tree have published nullifiers.

Proof size comparison at output set size M:

| M (output set) | Triptych | CLSAG ring=11 |
|---|---|---|
| 1024 | ~1.5 KB | 0.38 KB |
| 65536 | ~1.9 KB | 0.38 KB |
| 1M+ | ~2.2 KB | 0.38 KB |

Larger per-input proofs, but anonymity set goes from 11 to the entire chain.

What's needed:
```
triptych.h/.cpp:
  - One-out-of-many proof (Groth-Kohlweiss, O(log N))
  - Key image linkability (replaces CLSAG linkability)
  - Commitment balance proof (replaces CLSAG commitment layer)

Integration:
  - Global output Merkle tree (one tree, maintained by nodes, updated per block)
  - Replace CLSAG in inputs with Triptych proof
  - Replace explicit outputIndexes[] with (tree_root, witness)
  - Epoch sub-tree pruning (Wicht & Cachin model)
  - Wallet: build witness from current global Merkle tree
```

References:
- Noether & Goodell 2020, "Triptych: Logarithmic-sized Linkable Ring Signatures"
- Wicht & Cachin 2025, "Toxic Decoys" (PoPETs 2025, https://eprint.iacr.org/2025/1124)
  — pruning model, minimum anonymity set, flooding resistance formalization

Estimated: ~2000 LOC crypto + ~1000 LOC integration
Priority: After CLSAG + BP+ stable on testnet. Long-term privacy endgame.

---

## What Hides What (summary table)

| Privacy Property | v10 (now) | v11 (BP+ / CLSAG) | v12 (Triptych) | Mechanism |
|-----------------|-----------|-------------|----------------|-----------|
| Which output is spent | HIDDEN | HIDDEN | HIDDEN | Ring/Triptych over commitKeys |
| Output amount | VISIBLE | HIDDEN | HIDDEN | Pedersen commitment + BP+ range proof |
| Output type (deposit/transfer/burn) | VISIBLE | HIDDEN | HIDDEN | Unified output type (all outputs same struct) |
| Deposit term | VISIBLE | VISIBLE | VISIBLE | Needed for timelock enforcement |
| Sender identity | HIDDEN | HIDDEN | HIDDEN | Stealth addresses (CryptoNote) |
| Transaction origin IP | VISIBLE | HIDDEN | HIDDEN | Dandelion++ (Phase 4) |
| Ring pool | Per-amount, ring=9 | Global, ring=11 | Entire chain | CLSAG→Triptych over global Merkle |
| Anonymity set | ~9 decoys | ~11 decoys | Entire chain (10k–1M+) | Triptych Merkle membership |
| Sig size per input | ~288 bytes | ~384 bytes (CLSAG) | ~1.5–2 KB | CLSAG→Triptych |
| Output size | 36 bytes | 68 bytes | 68 bytes | key(32) + term(4) + commitment(32) |
| Tx size (typical) | ~2 KB | ~1.9 KB | ~3.5 KB | 2 outputs, BP+ aggregated proof |

---

## Crypto Primitives Status

### Have and use:
- ge_scalarmult_base, ge_scalarmult, ge_add, ge_sub
- ge_double_scalarmult_base_vartime
- sc_add, sc_sub, sc_mulsub, sc_reduce32
- generate_ring_signature / check_ring_signature
- generate_key_image
- chacha8 (for 0xD5 encryption)

### Have but unused (ready for Phase 5):
- pedersen_init / pedersen_H / pedersen_commit / pedersen_verify
- amountMask derivation in deriveCommitmentKeys()

### Need for Phase 5 (BP+ / CLSAG):
- CLSAG sign/verify (~600 LOC, port from Monero rct/rctSigs.cpp)
- Bulletproofs+ range proof generation/verification (~2500 LOC, port from Monero rct/bulletproofs_plus.cc)
- Pippenger multi-scalar multiplication (~500 LOC, port from Monero common/multiexp.cc)
- Commitment difference computation per ring member (C_input - C_pseudo)
- ecdhInfo: encrypted amount per output (chacha8 + ECDH shared secret)
- Unified output type (replaces separate TransactionOutputCommitment + KeyOutput)
- Global ring pool (single pool for all outputs, no per-amount segregation)
- Possibly sc_muladd (or build from sc_mulsub + sc_add)

### Need for Phase 6 (Triptych):
- One-out-of-many proof — Groth-Kohlweiss (O(log N) prover + verifier)
- Global output Merkle tree (single tree, maintained by nodes, updated per block)
- Transcript/Fiat-Shamir hash chain (domain-separated)
- Epoch sub-tree pruning (Wicht & Cachin model)

---

## Open Decisions

1. **Strip amountCommitment/amountProof from wire now?**
   Recommendation: Yes. 288 bytes dead weight. Re-add when BP+/CLSAG lands.

2. **CLSAG + BP+: port from Monero or write from scratch?**
   Port from Monero. CLSAG (~600 LOC in rctSigs.cpp) and BP+ (~2500 LOC in
   bulletproofs_plus.cc) are battle-tested since 2022. Porting and adapting to
   Fuego's types is faster and safer than writing from scratch. Monero uses
   ge_dsmp precomputation tables we don't have — may need to add or work around.

3. **Ring pool model: global pool**
   Decided: Single global pool of all outputs (no per-amount segregation).
   BP+ hides amounts, so ring members don't need matching amounts.
   OSPEAD decoy selection (already in codebase) adapts to global pool.

4. **Term privacy (hidden term)?**
   Not needed. Term is visible for lock enforcement. Ring hides which expired
   output is spent. Hiding term adds ZK timelock proofs for marginal benefit.
   Revisit only if term visibility causes measurable anonymity set reduction.

5. **Ring size for CLSAG?**
   Monero uses 16 (as of 2024). We currently use 9. With global pool and CLSAG,
   11 is a reasonable starting point. Needs analysis of expected output set size.

---

## References

- Noether & Goodell 2020, "Triptych: Logarithmic-sized Linkable Ring Signatures"
- Wicht & Cachin 2025, "Toxic Decoys: A Path to Scaling Privacy-Preserving
  Cryptocurrencies" (PoPETs 2025, https://eprint.iacr.org/2025/1124)
  — pruning model, minimum anonymity set, flooding resistance formalization
- MRL-0004, "Improving Obfuscation in the CryptoNote Protocol" (gamma decoy selection)
- Monero rct/rctSigs.cpp (CLSAG reference), rct/bulletproofs_plus.cc (BP+ reference)
- Sarang Noether et al. 2020, "Bulletproofs+: Shorter Proofs for Privacy-Enhanced
  Distributed Ledger" (range proof for hidden amounts)
