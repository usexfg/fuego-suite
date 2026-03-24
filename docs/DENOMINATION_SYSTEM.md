# Fuego Denomination System — Hidden Amounts W/o Bulletproofs
# Branch: HE4T | Updated: 2026-03-13

---

## Core Idea

ALL outputs use fixed denominations. Every amount is decomposed into
denomination-valued outputs, each with a Pedersen commitment + 1-of-N proof.
One unified output type. One global ring pool. MLSAG on all inputs.

No Bulletproofs. The existing tier proof code handles everything.

---

## Fuego Precision

- COIN = 10,000,000 (7 decimal places)
- Smallest non-dust: 0.0001 XFG = 1,000 atomic units (DEFAULT_DUST_THRESHOLD)
- Max supply: 8,000,008.8000008 XFG
- Denominations must go down to 0.0001 XFG to represent any non-dust amount

---

## Unified Output Type

Deposits and regular transfers merge into ONE output type:

```cpp
struct TransactionOutputUnified {
  Crypto::PublicKey key;                    // stealth address (regular) or commitKey (deposit)
  uint32_t term;                            // 0 = regular transfer, >0 = deposit lock (blocks)
  Crypto::EllipticCurvePoint commitment;    // C = amount*H + mask*G
  Crypto::MembershipProof proof;            // 1-of-N: amount is a valid denomination
};
```

One type, one ring pool. A 10 XFG regular transfer output and a 10 XFG deposit
output are indistinguishable to observers (both have the same proof set, same
commitment format). HEAT burns, COLD deposits, EFier stakes, regular sends — all
in one pool.

## Unified Input Type

```cpp
struct TransactionInputUnified {
  std::vector<uint32_t> outputIndexes;              // global ring (relative offsets)
  Crypto::KeyImage keyImage;                         // double-spend prevention
  Crypto::EllipticCurvePoint pseudoCommitment;       // C_pseudo for MLSAG balance
  // MLSAG signature stored in tx.signatures[]
};
```

No `amount` field (hidden). No type distinction. Ring members drawn from the
single global pool.

---

## Denomination Set

### Requirements
- Cover 0.0001 XFG to ~8,000,000 XFG (full supply range)
- Minimize outputs per tx (fewer = smaller tx, less bloat)
- Keep N manageable (proof size = N * 64 bytes)
- Include 0 for padding (dummy outputs indistinguishable from real)
- Every non-dust amount must be exactly representable

### Chosen: 1-2-5 Series (N=28)

Full denomination set covering 0.0001 to 5,000,000 XFG:

```
{0, 0.0001, 0.0002, 0.0005, 0.001, 0.002, 0.005, 0.01, 0.02, 0.05,
 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000,
 5000, 10000, 50000, 500000}

N = 28
Proof size = 28 * 64 = 1792 bytes per output
```

Why 1-2-5: standard banknote series. Greedy decomposition (largest-first) always
terminates in minimal outputs. Every amount with 4-decimal precision is representable.

Decomposition examples:
```
0.0003 XFG = 0.0002 + 0.0001                          = 2 outputs + 14 pads = 16
0.8    XFG = 0.5 + 0.2 + 0.1                          = 3 outputs + 13 pads = 16
47.3   XFG = 20+20+5+2+0.2+0.1                        = 6 outputs + 10 pads = 16
800    XFG = 500+200+100                               = 3 outputs + 13 pads = 16
4000   XFG = 2000+2000                                 = 2 outputs + 14 pads = 16
1234.5678 XFG = 1000+200+20+10+2+2+0.5+0.05+0.01+0.005+0.002+0.0005+0.0002+0.0001
                                                       = 14 outputs + 2 pads = 16
8000000 XFG = 500000×16 = needs 16 outputs             = 16 outputs, 0 pads (max)
```

Worst case for any amount up to ~5.5M XFG: 16 outputs (the pad limit).
Typical case: 3-8 real outputs.

### Per-Denomination Merkle Trees (Bin Merkles)

Each denomination gets its own Merkle tree ("bin"). When spending an output,
the ring is drawn from the same denomination bin (same committed value).

```
bin[0.0001] = Merkle tree of all 0.0001 XFG outputs ever created
bin[0.5]    = Merkle tree of all 0.5 XFG outputs ever created
bin[100]    = Merkle tree of all 100 XFG outputs ever created
...
```

With MLSAG: ring members are drawn from the same bin. The pseudo-commitment
layer proves balance without revealing which bin member is real.

With Triptych (future): the ENTIRE bin becomes the anonymity set. No fixed ring
size — proof covers all outputs in the bin via Merkle membership.

Nodes maintain 28 Merkle trees (one per denomination). Updated on each block.
Wallets query the relevant bin tree to build ring members or Merkle witnesses.

**Bin Merkle benefits:**
- Natural ring pool segmentation (all members have same committed value)
- Eliminates per-amount pool sparsity (denomination bins accumulate faster than
  arbitrary-amount pools because all txs decompose into the same fixed values)
- Triptych-ready: bin = Merkle domain for logarithmic ring proofs
- Commitment index already tracks outputs — adding per-bin trees is incremental

### Epoch Sub-Bins and Pruning (from Wicht & Cachin 2025)

Reference: "Toxic Decoys: A Path to Scaling Privacy-Preserving Cryptocurrencies"
(Wicht & Cachin, PoPETs 2025, https://eprint.iacr.org/2025/1124)

Each denomination bin is subdivided into epoch sub-bins (e.g., 1 epoch = 500 blocks).
Outputs created during an epoch are assigned to that epoch's sub-bin for their
denomination. This gives two practical benefits:

**1. Pruning fully-spent sub-bins (scalability)**

Each sub-bin tracks a nullifier counter. When nullifier_count == sub-bin.size,
every output in the sub-bin has been spent. The sub-bin (outputs + nullifiers)
can be pruned from the active ledger.

```
bin[10] epoch 0:  [out_0, out_1, ..., out_47]  nullifiers: 47/47 → PRUNED
bin[10] epoch 1:  [out_0, out_1, ..., out_63]  nullifiers: 31/63 → ACTIVE
bin[10] epoch 2:  [out_0, out_1, ..., out_55]  nullifiers: 2/55  → ACTIVE
```

Wicht & Cachin showed ~60% ledger reduction on Monero data. For Fuego (lower
volume), the savings are proportionally smaller but still meaningful long-term.

The sub-bin's Merkle root remains in the parent denomination tree (as a leaf)
even after pruning — only the underlying data is deleted. This preserves the
denomination bin's Merkle structure for Triptych proofs that reference historical
sub-bins (the prover must retain their own witness data).

**2. Minimum sub-bin occupancy (privacy floor)**

Don't allow spending from a sub-bin until it contains at least K outputs
(e.g., K=16, matching Wicht & Cachin's minimum anonymity set). This prevents
early-chain deanonymization when denomination bins are sparse.

```
bin[50000] epoch 5: size=3  → LOCKED (below minimum K=16, cannot spend yet)
bin[10]    epoch 5: size=41 → SPENDABLE
```

Rare denominations (50000, 500000) may take longer to reach minimum occupancy.
This is acceptable — users depositing large amounts can wait, or the wallet
can decompose differently to avoid sparse bins.

### Flooding Attack Analysis

Our denomination bins are deterministic — an attacker knows exactly which bin
their output lands in (unlike Wicht & Cachin's random assignment). This means
an attacker can target-flood a specific denomination bin to reduce effective
anonymity.

**Attack:** Attacker creates 100 outputs of denomination 10 XFG. When an honest
user spends from bin[10], the attacker knows 100 of the ring/bin members are
theirs, reducing the honest anonymity set.

**Mitigations (layered):**
1. **Epoch sub-bins limit exposure.** Attacker's outputs land in the current
   epoch's sub-bin. Older sub-bins are unaffected. The ring/Triptych proof
   can span multiple sub-bins, diluting attacker concentration.
2. **Minimum occupancy gate.** If a sub-bin is predominantly attacker-controlled,
   it won't reach minimum K until honest outputs arrive (or if it does, the
   attacker spent their own money to get there).
3. **16 padded outputs per tx.** Every honest tx creates 16 outputs across
   multiple denomination bins. Flooding one bin doesn't flood others.
4. **Cost.** Each flooding output costs network fee + denomination value locked.
   Flooding bin[100] with 100 outputs costs 10,000 XFG + fees. Expensive.
5. **Gamma-weighted decoy selection (MLSAG phase).** Prefer recent outputs as
   decoys — attacker must continuously flood, not just once.

---

## Output Count and Padding

**Fixed output count: 16 per transaction.**

Every tx produces exactly 16 outputs. Real outputs carry actual denomination
values. Padding outputs carry 0 XFG (valid denomination, valid proof,
random key — indistinguishable from real outputs to observers).

Why 16:
- Covers worst-case decomposition for amounts up to ~5.5M XFG with N=28 denoms
- Fixed count prevents output-count analysis (all txs look the same)
- 16 is a round power of 2 (efficient indexing)

For amounts over ~5.5M XFG: split across multiple transactions (extremely rare).

### Padding outputs

```
Padding output:
  key = random point (no one can spend)
  term = 0 (or same term as real outputs for deposit txs)
  commitment = pedersen_commit(0, random_mask)
  proof = valid 1-of-N proof for denomination 0
```

Observer cannot distinguish padding from a real 0-value output (both have valid
proofs for denomination 0). The random key ensures no one can accidentally spend
padding, and it contributes to the denomination-0 bin as a permanent decoy.

**Padding outputs go into bin[0] Merkle tree.** They serve as decoys for other
zero-denomination outputs (change dust, other pads). This is intentional — bin[0]
will be the largest bin, which is fine since no real value is at stake.

---

## Transaction Structure (v11)

```
TransactionPrefix:
  version: 2
  unlockTime: 0 (or deposit unlock height)
  inputs:  [UnifiedInput, ...]            // MLSAG-signed, per-bin ring
  outputs: [UnifiedOutput x 16]           // always 16 (padded)
  extra:   [txPubKey, 0xD5 encrypted amounts, ...]
  fee:     uint64_t                        // explicit (visible)

Signatures:
  For each input: MLSAG signature (c_0 + s[ring_size][2] * 32 bytes)
```

### Encrypted Amount Info (in tx_extra or dedicated field)

Recipient needs to learn the actual amount + mask to verify and later spend.
For each real output, encrypt (amount, mask) to the recipient's view key:

```
ecdhInfo[i] = chacha8(amount || mask, key=ECDH(txSecKey, recipientViewPub))
```

16 ecdhInfo entries (including dummies for padding outputs). Observer can't
tell which are real.

---

## Balance Proof

```
sum(input_pseudo_C[j]) = sum(output_C[k]) + fee * H

Where:
  input_pseudo_C[j] = MLSAG pseudo-commitment for input j
  output_C[k]       = Pedersen commitment for output k (including zero-pads)
  fee                = explicit fee field
  H                  = Pedersen generator
```

The wallet ensures:
```
sum(z_j for all inputs) = sum(mask_k for all outputs)
```

So the G-components cancel and the H-components prove:
```
sum(input_amounts) = sum(output_amounts) + fee
```

Zero-padded outputs contribute 0*H + mask*G — they don't affect the amount
balance, only the mask balance (which the wallet manages).

---

## Wallet Decomposition Algorithm

```python
def decompose(amount_atomic, denominations):
    """Greedy decomposition, largest-first."""
    outputs = []
    remaining = amount_atomic
    # Sort denominations descending (skip 0)
    for denom in sorted(denominations, reverse=True):
        if denom == 0:
            continue
        while remaining >= denom:
            outputs.append(denom)
            remaining -= denom
    assert remaining == 0, "Amount not exactly representable"
    return outputs

def build_outputs(amount, fee, denominations, pad_to=16):
    send_amount = amount - fee
    real_outputs = decompose(send_amount, denominations)
    assert len(real_outputs) <= pad_to
    padding = [0] * (pad_to - len(real_outputs))
    return real_outputs + padding
```

**Critical constraint:** `amount - fee` must be exactly representable as a sum of
denominations. Since denominations include 0.01, any amount with 2 decimal places
works. If Fuego uses more precision, the smallest denomination must match.

---

## Ring Pool Analysis

**With denominations + MLSAG + bin Merkles:**

Ring members are drawn from the same denomination bin. Each bin accumulates
all outputs of that denomination across the entire chain.

Compare Monero (arbitrary amounts, global pool):
Ring members drawn from all outputs, pseudo-commitment hides which matches.
Pool = all outputs ever. But outputs have arbitrary amounts so the commitment
layer does all the work.

Compare Fuego v10 (per-amount, simple ring sig):
Ring pool = outputs at one exact amount (e.g., 0.8 XFG). Very sparse pools
for uncommon amounts.

Compare Fuego v11 (per-denomination bin):
Ring pool = all outputs at denomination X. Since every tx decomposes into
the same 28 denominations, bins fill up much faster than per-amount pools.
A chain with 100K txs produces ~300-800K outputs across 28 bins, averaging
~10-30K per bin. Even low-volume denominations (0.0002, 50000) accumulate
from padding outputs.

**Denominations + bin Merkles = dense ring pools with natural Triptych domains.**

---

## Fee Handling

Fee is explicit and visible in the transaction. No denomination proof needed
for the fee — it's not an output, it's a scalar in the balance equation.

```
sum(pseudo_C) = sum(output_C) + fee * H
```

The wallet decomposes `amount - fee` into denominations. The fee is subtracted
from the total before decomposition.

No separate fee input needed. No fee-denomination gymnastics.

---

## Deposit-Specific Details

### Deposit creation (v11)
Same as regular transfer, but:
- Output key = commitKey (from depositSecret), not stealth address
- Output term > 0 (lock period in blocks)
- tx_extra includes 0xD5 encrypted deposit secret
- Amount decomposed into denominations like any other tx

A 800 XFG deposit becomes:
```
500 + 200 + 100 = 3 real outputs + 13 zero pads = 16 outputs
All outputs: same commitment + proof format
term > 0 on the 3 real ones; term = 0 on pads
```

Wait — term on padding outputs should also be > 0 (same as real deposit outputs)
to prevent observer from distinguishing padding. Use same term on all 16.

### Deposit withdrawal (v11)
- Input: UnifiedInput with MLSAG from global ring pool
- Outputs: regular transfer outputs (term = 0) or re-deposit (term > 0)
- Amount stays hidden in commitments throughout

### HEAT burns
- commitKey with discarded secret (permanent decoy, as before)
- term = FOREVER
- All 16 outputs use term = FOREVER
- Permanently enriches the global ring pool

---

## Migration Path

### v10 (current) → v11 (denominations + MLSAG)

1. v11 upgrade height: all new transactions must use unified outputs
2. Old v10 outputs (CommitmentOutput, KeyOutput): treated as having
   retroactive commitment C = amount*H + 0*G (mask=0, publicly computable)
3. v10 outputs can appear as ring members in v11 MLSAG rings
4. v10 CommitmentSpend inputs: still accepted (backward compat) using old
   per-amount ring resolution
5. Over time, v10 outputs age out of decoy selection (gamma distribution)

### v11 → v12+ (optional Bulletproofs)

If arbitrary amounts are ever needed:
1. Add Bulletproof as alternative range proof type
2. Verifier accepts either denomination proof or Bulletproof
3. Denomination outputs remain valid forever
4. No breaking change — purely additive

---

## Size Analysis

### The real cost: OUTPUTS dominate, not signatures

With denomination proofs, each output carries a 1-of-N membership proof.
At N=28, that's 1,792 bytes per output. With 16 fixed outputs, that's
28,672 bytes of proofs alone. Signatures (MLSAG/CLSAG) are a rounding error
by comparison.

### Per output (N=28):
```
key:        32 bytes
term:        4 bytes
commitment: 32 bytes
proof:    1792 bytes (28 * 64)
Total:    1860 bytes per output
```

### Per transaction (16 outputs, 2 inputs, ring size 9):
```
Outputs:   16 * 1860 = 29,760 bytes   ← 93% of tx size
Inputs:    2 * ~40   =     80 bytes
MLSAG:     2 * 640   =  1,280 bytes   ← 4% of tx size
ecdhInfo:  16 * 64   =  1,024 bytes
Fee:                        8 bytes
Extra:                    ~100 bytes
Total:                 ~32.3 KB
```

### CLSAG vs MLSAG savings in context:
```
                        MLSAG           CLSAG           Savings
Per input (ring=9):     640 bytes       320 bytes       320 bytes
2-input tx total:       1,280 bytes     640 bytes       640 bytes
% of 32 KB tx:          4.0%            2.0%            2.0%

Output proofs:          29,760 bytes    29,760 bytes    0 bytes (unchanged)
```

**CLSAG saves ~2% of total tx size.** The 16 outputs with 1792-byte proofs
each account for 93% of the tx. Signature optimization is not the bottleneck.

### Comparison:
```
Current Fuego simple transfer:      ~2 KB
Monero RingCT (2 outputs, BP+):     ~1.5 KB
Fuego denominations (N=28, pad=16): ~32 KB
```

~20x larger than Monero. This is the cost of avoiding Bulletproofs.
But: bandwidth is cheap, Fuego's block interval is 8 min (480s target), and typical
block sizes will be small (low-volume chain). The privacy gain is real.

### Mitigation strategies (if size becomes a problem):
1. **Reduce N**: fewer denominations = smaller proofs, but more outputs needed
   - N=14 (skip 0.0002/0.002/0.02/etc): proof=896 bytes, output=964 bytes, tx=~17 KB
2. **Aggregated proofs**: batch-verify all 16 denomination proofs (future optimization)
3. **Reduce pad count**: 8 instead of 16 (limits max decomposition, leaks output count info)
4. **Accept it**: 32 KB is fine for a 8-minute block with low tx volume

---

## Implementation Order

1. **MLSAG crypto** (mlsag.h/.cpp, ~800 LOC) — same regardless of denomination choice
2. **Unified output/input types** (CryptoNote.h, serialization) — merge CommitmentOutput + KeyOutput
3. **Denomination set** (consensus constant in CryptoNoteConfig.h, 28 values)
4. **Per-denomination bin Merkle trees** (nodes maintain 28 trees, updated per block)
5. **Wallet decomposition** (amount → denomination outputs, greedy largest-first)
6. **Commitment generation** (pedersen + 1-of-28 membership proof per output, existing code)
7. **MLSAG signing** (wallet builds ring from same-denomination bin, signs all inputs)
8. **Balance verification** (blockchain checks sum equation)
9. **ecdhInfo** (encrypt amount+mask per output for recipient)
10. **Backward compat** (v10 output retroactive commitments, old spend paths)
11. **Genesis reset** (new wire format, bump nonce)

---

## What We Already Have (reusable)

- pedersen.cpp: pedersen_commit, pedersen_verify, H generator — DONE
- tier_proof.cpp: generate_membership_proof, check_membership_proof — DONE
  (bump FUEGO_MEMBERSHIP_N from 4 to 28)
- deriveCommitmentKeys: commitKey, keyScalar, keyImage, amountMask — DONE
- 0xD5 encrypted deposit secret: encrypt/decrypt/add/get — DONE
- CommitmentIndex tracking — partially done (needs per-bin Merkle tree addition)
- TransactionOutputUnified / TransactionInputUnified — struct defined in CryptoNote.h

## What We Skip (and why)

### CLSAG (not worth the ROI)

CLSAG is a drop-in MLSAG replacement that halves signature size. But with the
denomination proof system, signatures are ~4% of tx size. CLSAG saves ~2% total.

```
MLSAG 2-input tx:  1,280 bytes sig in a 32,252 byte tx = 4.0%
CLSAG 2-input tx:    640 bytes sig in a 31,612 byte tx = 2.0%
Savings:              640 bytes                         = 2.0%
```

The engineering cost (~600 LOC, new fork, testing, audit) is not justified for
a 2% size reduction. The bottleneck is OUTPUT size (93% of tx), not INPUT sigs.

CLSAG becomes worthwhile ONLY if:
- We reduce N significantly (smaller proofs → sigs become larger % of tx)
- We adopt Bulletproofs (2 outputs instead of 16 → sigs dominate again)
- Neither is planned.

### Bulletproofs

Bulletproofs allow arbitrary amounts with small range proofs (~672 bytes for
64-bit range). This would eliminate the 16-output pad requirement entirely.
But Bulletproofs are ~3000 LOC of complex crypto (inner-product argument,
multi-exponentiation) and require audit.

The denomination approach trades tx size for implementation simplicity. At
Fuego's transaction volume, 32 KB txs are acceptable.
