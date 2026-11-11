# MLSAG Development Plan — Fuego Hidden Deposit Amounts
# Branch: HE4T | Date: 2026-03-07

---

## What is MLSAG and why do we need it

**Problem:** Ring signatures prove "I know the key for ONE of N members" — but they
say nothing about amounts. With visible amounts, ring pools must be per-amount
(otherwise: inflation attack). Per-amount pools are small and leak the amount.

**Solution:** MLSAG (Multi-Layered Linkable Spontaneous Ad-hoc Group) extends ring
signatures to prove TWO things simultaneously:
1. "I know the spend key for one ring member" (layer 0 — same as today)
2. "The commitment difference for that member is zero" (layer 1 — balance proof)

Layer 2 is the key: it proves the spender's pseudo-commitment hides the SAME amount
as the real ring member's commitment, without revealing which member or what amount.

**Result:** Amounts hidden. Global ring pool. Larger anonymity set. No inflation.

---

## The Pseudo-Output Trick (core concept)

At deposit time, the wallet creates: `C_real = amount*H + mask*G`
The mask is fixed (derived from depositSecret). The balance equation needs
`sum(input_masks) = sum(output_masks)` — but input masks are fixed.

**Solution: pseudo-output commitments.**

For each withdrawal input, the spender creates a FRESH commitment:
```
C_pseudo = amount*H + z*G     (z chosen freely by spender)
```

The MLSAG proves C_pseudo commits to the same amount as C_real:
```
C_pseudo - C_real = (z - mask)*G     <- spender knows this discrete log
```

For fake ring members with different amounts:
```
C_pseudo - C_fake = (amount_real - amount_fake)*H + (z - mask_fake)*G
                    ^^^ unknown DL (can't separate H and G)
```

The spender chooses z values so that:
```
sum(z_inputs) = sum(mask_outputs)
```
This makes the balance equation work:
```
sum(C_pseudo) - sum(C_output) - fee*H = 0
```

C_pseudo goes on-chain (32 bytes per input). The z values are ephemeral.

---

## MLSAG Algorithm (Fuego-specific, M=2 layers)

### Key Matrix (N ring members, 2 layers)

```
Row i:  [ commitKey[i],  C_pseudo - C[i] ]
         ^layer 0        ^layer 1

For real member pi:
  layer 0 secret: keyScalar        (from depositSecret)
  layer 1 secret: z - amountMask   (spender knows both)

For fake member i != pi:
  layer 0: no secret (random commitKey from chain)
  layer 1: no secret (commitment difference has unknown DL)
```

### Sign(message, ring, pi, secrets)

```
// Layer 0 has key image; layer 1 does not
I = keyScalar * H_p(commitKey[pi])    // already computed at deposit time

// Step 1: Start at real index
alpha_0, alpha_1 = random scalars
L[pi][0] = alpha_0 * G
R[pi]    = alpha_0 * H_p(commitKey[pi])
L[pi][1] = alpha_1 * G

c[pi+1] = H_s(message || L[pi][0] || R[pi] || L[pi][1])

// Step 2: Loop through fake members (pi+1 .. pi-1 mod N)
for i = (pi+1) to (pi-1) mod N:
  s[i][0] = random scalar
  s[i][1] = random scalar
  L[i][0] = s[i][0]*G + c[i]*commitKey[i]
  R[i]    = s[i][0]*H_p(commitKey[i]) + c[i]*I
  L[i][1] = s[i][1]*G + c[i]*(C_pseudo - C[i])
  c[i+1]  = H_s(message || L[i][0] || R[i] || L[i][1])

// Step 3: Close the ring
s[pi][0] = alpha_0 - c[pi] * keyScalar          (sc_mulsub)
s[pi][1] = alpha_1 - c[pi] * (z - amountMask)   (sc_mulsub)

Output: { c_0, s[0..N-1][0..1] }
```

### Verify(message, ring, I, c_0, s[][])

```
for i = 0 to N-1:
  L[i][0] = s[i][0]*G + c[i]*commitKey[i]
  R[i]    = s[i][0]*H_p(commitKey[i]) + c[i]*I
  L[i][1] = s[i][1]*G + c[i]*(C_pseudo - C[i])
  c[i+1]  = H_s(message || L[i][0] || R[i] || L[i][1])

Check: c[N] == c_0
Check: I not in spent key images
```

### Signature Size

Per CommitmentSpend input with ring size N:
```
c_0:        32 bytes
s[N][2]:    N * 2 * 32 bytes
C_pseudo:   32 bytes
Total:      64 + 64N bytes

Ring size 9 (mixin 8): 64 + 576 = 640 bytes
vs current simple ring sig: 9 * 64 = 576 bytes
Delta: +64 bytes per input (the pseudo-commitment + one extra scalar column)
```

---

## Concern #1: Inflation Attack

**Attack:** Spender creates C_pseudo committing to a larger amount than deposited,
claims more coins on output.

**Why it fails:** MLSAG layer 1 requires the signer to know the discrete log of
`C_pseudo - C_real`. If C_pseudo commits to a different amount:
```
C_pseudo - C_real = (amount_fake - amount_real)*H + (z - mask)*G
```
This has unknown DL (H and G are independent generators). The signer can't compute
`s[pi][1]`, and the ring won't close. MLSAG verification fails.

**Additional guard:** 1-of-4 tier proof on output commitments. Even if MLSAG had a
bug, the output can only commit to one of 4 valid tier amounts. Limits damage to
at most the highest tier.

---

## Concern #2: Mixing v10 Outputs (No Commitment) with v11 Outputs

**Problem:** v10 commitment outputs have visible amount, no amountCommitment on-chain.
v11 outputs have amountCommitment. How do they coexist in the same ring?

**Solution: Retroactive commitment for v10 outputs.**

v10 outputs have `amount` visible. Treat them as having:
```
C_v10 = amount * H + 0 * G     (mask = 0, publicly computable)
```

When a v10 output appears in a v11 MLSAG ring:
- Ring construction uses this retroactive C_v10
- If the v10 output is the REAL member being spent, layer 1 secret = `z - 0 = z`
- If it's a fake member, commitment difference has unknown DL (different amount)

**Privacy implication:** v10 outputs in a ring are identifiable (visible amount, no
commitment field). An observer could eliminate v10 decoys whose amount doesn't match.
This is acceptable during the transition period — as v11 outputs accumulate, v10
outputs will naturally age out of decoy selection (gamma distribution biases recent).

**Mitigation:** After sufficient v11 outputs exist, wallet should prefer v11 decoys.
Don't exclude v10 entirely (reduces pool), but weight toward v11.

---

## Concern #3: Balance Verification

**Equation the verifier checks:**
```
sum(C_pseudo[j] for each input j) - sum(C_output[k] for each output k) - fee*H = 0
```

This is a point equality check. All values are on-chain:
- C_pseudo: in the CommitmentSpend input (new field)
- C_output: amountCommitment of each output
- fee: explicit field in TransactionPrefix (visible)
- H: the Pedersen generator (fixed, known to all)

**Implementation:**
```cpp
bool checkCommitmentBalance(const Transaction& tx) {
  ge_p3 sum = identity;
  // Add all input pseudo-commitments
  for (auto& input : tx.inputs) {
    if (input.type() == CommitmentSpend)
      point_add(sum, input.pseudoCommitment);
  }
  // Subtract all output commitments
  for (auto& output : tx.outputs) {
    if (output.target.type() == CommitmentOutput)
      point_sub(sum, output.amountCommitment);
  }
  // Subtract fee*H
  point_sub(sum, scalar_mult(fee, H));
  return sum == identity;
}
```

**Edge case: mixed transactions (KeyInput + CommitmentSpend)**

If a transaction has both KeyInput (regular transfer) and CommitmentSpend (deposit
withdrawal), the KeyInput amounts are visible. Treat them as:
```
C_key_input = keyInput.amount * H + 0*G
```
Add to the balance equation. Same for regular KeyOutput outputs.

Actually — mixed transactions should NOT be allowed in v11. A transaction is either:
- All regular (KeyInput -> KeyOutput, amounts visible, simple ring sigs)
- All commitment (CommitmentSpend -> commitment outputs, amounts hidden, MLSAG)

This avoids complexity and prevents amount leakage through mixing types. If a user
needs to convert regular XFG to a deposit, that's a deposit creation tx (separate tx).

---

## Concern #4: Fee Handling

Fee must be visible (miners verify it). Two approaches:

**Approach A: Explicit fee field (recommended)**
```
TransactionPrefix already has no fee field — fee is inferred from
  sum(input amounts) - sum(output amounts)
With hidden amounts, this inference is impossible.
Add `uint64_t fee` to transaction for v11+. 
Verifier checks `fee >= minimumFee` and uses it in balance equation.

---

## Concern #5: Change Outputs

When withdrawing a deposit, the full amount goes to one or more outputs. If there's
no change (exact spend), that's fine. If there's change:

```
Deposit: 800 XFG (tier 3)
Fee: 0.01 XFG
Output 1: 799.99 XFG to recipient (or back to self)
```

But 799.99 is not a valid tier amount. This output can't have a tier proof.

**Solution:** Withdrawal outputs are regular KeyOutputs, not commitment outputs.
Only DEPOSIT CREATION produces commitment outputs (with tier proofs). Withdrawal
takes hidden-amount commitment inputs and produces visible-amount regular outputs.

```
Withdrawal tx:
  Input:  CommitmentSpend (MLSAG, hidden amount, from deposit)
  Output: KeyOutput (visible amount, standard one-time key)
```

**Privacy implication:** The output amounts ARE visible on the withdrawal tx. Observer
sees "someone withdrew X coins." But they don't know WHICH deposit was spent (ring sig)
and the deposit creation tx had a hidden amount (commitment + tier proof).

**Is this acceptable?** Yes — it's the same model as Monero before RingCT applied to
all outputs. The deposit amount is hidden at creation. The withdrawal reveals the amount
but not the source. Full amount privacy on ALL outputs requires Bulletproofs (Phase 6).

**Alternative (future):** Withdrawal outputs could also be commitment outputs with tier
proofs, keeping amounts hidden end-to-end. But this requires the recipient to also
understand commitment outputs, complicating the wallet. Defer to post-MLSAG.

---

## Concern #6: Global Ring Pool Migration

**Current:** `m_commitmentOutputs` is `map<uint64_t, vector<CommitmentOutputRef>>` (per-amount).

**Target:** flat `vector<CommitmentOutputRef>` (global index, amount-agnostic).

**Migration plan:**
1. Keep the per-amount map for v10 compatibility (simple ring sig CommitmentSpend still needs it)
2. Add `vector<CommitmentOutputRef> m_allCommitmentOutputs` alongside it
3. Every commitment output gets pushed to BOTH containers
4. v10 CommitmentSpend: resolved against per-amount map (as today)
5. v11 CommitmentSpend: resolved against global flat vector
6. RPC: new endpoint `getRandomCommitmentOuts(count)` (no amount param)
7. Old endpoint `getRandomCommitmentOutsForAmount` kept for v10 compat

**Index encoding:** outputIndexes in TransactionInputCommitmentSpend are relative
offsets into the flat global vector (not per-amount indices). Verifier decodes to
absolute indices, looks up in `m_allCommitmentOutputs`.

---

## Concern #7: Degenerate Ring Guard

**Problem:** If ALL ring members are HEAT burns (key discarded, permanently
unspendable), no valid real spend exists. Currently guarded by `hasNonForever` check.

**With global pool:** Ring members have mixed amounts. But HEAT burns still have
`term = FOREVER`. Guard still works: reject if all ring members are FOREVER-term.

**Refinement for v11:** Since amount is hidden, the wallet can't trivially determine
if a ring member is a HEAT burn by amount. But term is visible. Keep the FOREVER check.

---

## Concern #8: Transaction Size Impact

Per commitment output (deposit creation):
```
v10 current:  commitKey(32) + term(4) = 36 bytes
v11 target:   commitKey(32) + term(4) + amountCommitment(32) + amountProof(256) = 324 bytes
Delta: +288 bytes per deposit output
```

Per CommitmentSpend input (withdrawal):
```
v10 current:  amount(8) + outputIndexes(~40) + keyImage(32) + sigs(576) = ~656 bytes
v11 target:   outputIndexes(~40) + keyImage(32) + pseudoCommitment(32) + MLSAG_sigs(640) = ~744 bytes
Delta: +88 bytes per withdrawal input (pseudo-commitment + extra scalar column)
              amount field REMOVED (no longer needed — hidden in commitment)
```

**Total per deposit lifecycle (create + withdraw):** +376 bytes
At scale, this is acceptable for privacy. Monero's RingCT adds similar overhead.

---

## Concern #9: CLSAG → Triptych Upgrade Path (future)

**v12: CLSAG** — Drop-in replacement for MLSAG:
- Half the signature size: N+1 scalars instead of 2N+1
- Ring size 9: ~320 bytes vs ~640 bytes
- Same security guarantees, more compact

**v13: Triptych** — Replaces CLSAG with logarithmic ring proofs:
- Proof size: O(log M) where M = denomination bin size
- Anonymity set: entire denomination bin (all outputs ever of that value)
- No toxic decoys: every output in the bin is a valid candidate
- Built on one-out-of-many proofs (Groth-Kohlweiss) + Merkle trees per bin
- Requires: multiexp (Pippenger), per-denomination Merkle trees, GK prover/verifier
- Paper: Noether & Goodell 2020 — "Triptych: Logarithmic-sized Linkable Ring Signatures"

Denomination bins (from DENOMINATION_SYSTEM.md) are the natural Merkle tree domain.
Each bin accumulates all outputs of that denomination. Triptych proves membership in
the current bin root without revealing which output is being spent.

**Recommendation:** MLSAG first → CLSAG when stable → Triptych as the long-term
privacy endgame. CLSAG is the size win; Triptych is the anonymity set win.

---

## Concern #10: Crypto Primitives Gap

**Need for MLSAG that we don't have:**

1. `hash_to_p3(key)` — hash a public key to a ge_p3 point (for key image in MLSAG)
   We have `generate_key_image` which does this internally, but we need the raw
   `H_p(P)` function exposed for computing R[i] = s*H_p(K) + c*I in verify.
   **Solution:** Extract from crypto.cpp, expose as `hash_to_ec(key, result)`.
   Already exists internally — just needs a public declaration.

2. `ge_p3` point arithmetic (add, sub) returning ge_p3 (not ge_p2).
   We have `ge_sub` producing ge_p1p1, then `ge_p1p1_to_p3`. Chain them.
   **Solution:** Helper `point_sub_p3(A, B) -> C` that does the conversion.

3. `ge_double_scalarmult_precomp_vartime` for `a*A + b*B` where B is NOT G.
   Needed for L[i][1] = s*G + c*(C_pseudo - C[i]) — but we can decompose this:
   L[i][1] = s*G + c*D[i] where D[i] = C_pseudo - C[i] (precomputed).
   `ge_double_scalarmult_base_vartime` does a*A + b*G. We need a*A + b*B.
   **Solution:** We DO have `ge_double_scalarmult_precomp_vartime(r, a, A, b, Bi)`
   where Bi is a precomp table. Precompute table for each D[i].
   OR: compute separately: `s*G` + `c*D[i]` and add. Slower but works.

4. Point equality check (for balance verification).
   **Solution:** `ge_p3_tobytes` both, `memcmp`. Or subtract and check identity.

5. Identity point constant.
   **Solution:** `ge_p3 identity = {0, 1, 1, 0}` (neutral element of Ed25519).

**None of these are hard.** All are wrappers around existing crypto-ops primitives.
Estimated: ~100 LOC of helper functions.

---

## Concern #11: Wallet Key Management

**At deposit creation (v11):**
```
1. Generate depositSecret (32 random bytes)
2. deriveCommitmentKeys(depositSecret) -> commitKey, keyScalar, keyImage, amountMask
3. Generate amountCommitment: pedersen_commit(amount, amountMask)
4. Generate amountProof: generate_membership_proof(commitment, amount, amountMask, tiers)
5. Encrypt depositSecret in 0xD5 tag (chacha8, ECDH)
6. Build TransactionOutputCommitment { commitKey, term, amountCommitment, amountProof }
```

**At withdrawal (v11):**
```
1. Recover depositSecret from 0xD5 (decrypt with view key)
2. deriveCommitmentKeys(depositSecret) -> keyScalar, amountMask, ...
3. Fetch N-1 random commitment outputs from daemon (global pool, no amount filter)
4. For each input:
   a. Choose random z_i (except last: z_last = sum(output_masks) - sum(z_other))
   b. Compute C_pseudo_i = amount*H + z_i*G
   c. Build ring: K[j] = (commitKey[j], C_pseudo_i - C[j]) for each ring member j
   d. Sign MLSAG: secrets = (keyScalar, z_i - amountMask)
5. Build outputs (visible-amount KeyOutputs for now)
6. Attach fee (explicit field)
```

**Key point:** amountMask is the ONLY new secret (beyond existing keyScalar).
It's derived deterministically from depositSecret. No new storage needed.

---

## Concern #12: Blockchain Validation Changes

### On deposit creation (v11+ block):
```
1. Check amountCommitment is a valid Ed25519 point
2. Verify amountProof: check_membership_proof(proof, amountCommitment, tier_amounts)
3. If dual-bookkeeping (TransactionOutput.amount still visible):
   Verify pedersen_verify(amountCommitment, amount, ???) — but we don't know the mask!
   -> Can't verify this. The commitment is unforgeable due to tier proof.
   -> Tier proof guarantees committed amount is one of {TIER_0..TIER_3}.
   -> This is sufficient for consensus.
```

### On withdrawal (v11+ CommitmentSpend):
```
1. Resolve ring members from global index (m_allCommitmentOutputs)
2. For each ring member: get commitKey and amountCommitment (or retroactive C for v10)
3. Verify MLSAG:
   a. Reconstruct key matrix: row[i] = (commitKey[i], C_pseudo - C[i])
   b. Run MLSAG verify with c_0 and s[][] from signature
   c. Check key image not in spent set
4. Verify balance:
   sum(C_pseudo) - sum(C_output) - fee*H == identity point
5. Check fee >= minimumFee
6. Check ring size >= minMixin + 1
7. Check all ring members exist and at least one is non-FOREVER (degenerate guard)
8. For ring members with visible term: check term has expired (current_height >= creation + term)
```

---

## Implementation Phases

### Phase 5a: MLSAG Crypto (~800 LOC)
**File: `src/crypto/mlsag.h`, `src/crypto/mlsag.cpp`**
```
- mlsag_sign(message, key_matrix, real_index, secrets, key_image) -> (c_0, s[][])
- mlsag_verify(message, key_matrix, key_image, c_0, s[][]) -> bool
- Helper: hash_to_p3(key) -> ge_p3 (expose existing internal)
- Helper: point_sub_p3(A, B) -> ge_p3
- Helper: commitment_to_p3(C) -> ge_p3 (deserialize EllipticCurvePoint to ge_p3)
```

### Phase 5b: Transaction Format Changes
**Files: `CryptoNote.h`, `CryptoNoteSerialization.cpp`**
```
- Remove `amount` from TransactionInputCommitmentSpend (amount is hidden)
- Add `pseudoCommitment` (EllipticCurvePoint) to TransactionInputCommitmentSpend
- Add `fee` to TransactionPrefix (v11+ only)
- amountCommitment + amountProof already in TransactionOutputCommitment (currently serialized)
- Signature format: change from vector<Signature> to MLSAG blob for CommitmentSpend inputs
```

### Phase 5c: Global Ring Pool
**Files: `Blockchain.h/.cpp`, `CommitmentIndex.h/.cpp`**
```
- Add m_allCommitmentOutputs flat vector alongside existing per-amount map
- Push to both on deposit creation
- v11 CommitmentSpend: resolve ring from global vector
- v10 CommitmentSpend: resolve ring from per-amount map (backward compat)
```

### Phase 5d: Balance + MLSAG Verification
**Files: `Blockchain.cpp`**
```
- checkCommitmentBalance(): verify sum(C_pseudo) = sum(C_output) + fee*H
- checkCommitmentSpendInput(): replace check_ring_signature with mlsag_verify for v11
- Verify amountProof on deposit outputs (1-of-4 tier proof)
- Retroactive commitment for v10 ring members: C = amount*H + 0*G
```

### Phase 5e: Wallet Integration
**Files: `WalletGreen.cpp`, `WalletTransactionSender.cpp`**
```
- Deposit creation: generate amountCommitment + amountProof (call pedersen + tier_proof code)
- Withdrawal: build MLSAG ring, compute pseudo-commitments, sign with mlsag_sign
- Blinding factor management: z values, last-input adjustment for balance
- RPC: call new getRandomCommitmentOuts (global, no amount filter)
```

### Phase 5f: RPC Changes
**Files: `RpcServer.cpp`, `CoreRpcServerCommandsDefinitions.h`**
```
- New: /getrandom_commitment_outs.bin (no amount param)
  Request:  { count: 9 }
  Response: { outs: [{ global_index, commit_key, amount_commitment }] }
- Keep old /getrandom_commitment_outs_for_amount.bin for v10 compat
- Strip unlockHeight from response (privacy: don't reveal when deposits expire)
```

### Phase 5g: Genesis Reset + v11 Upgrade Height
```
- Bump testnet genesis nonce (chain reset for new wire format)
- Define TESTNET_BLOCK_V11_HEIGHT
- Gate all MLSAG code paths on blockMajorVersion >= 11
```

---

## Testing Strategy

1. **Unit tests for MLSAG sign/verify** (crypto-level)
   - Correct: sign with real secrets, verify passes
   - Wrong secret: sign with bad keyScalar, verify fails
   - Wrong commitment: sign with inflated C_pseudo, verify fails
   - Key image reuse: second spend with same key image rejected

2. **Balance verification tests**
   - Correct balance: sum equation holds, passes
   - Inflation: output > input, balance check fails
   - Fee mismatch: wrong fee, balance check fails

3. **Integration tests (testnet)**
   - Create v11 deposit with amountCommitment + amountProof
   - Withdraw with MLSAG ring sig (global pool)
   - Verify chain accepts the block
   - Verify another node syncs and validates independently

4. **Backward compatibility tests**
   - v10 deposits still withdrawable with simple ring sig
   - v10 outputs usable as ring members in v11 MLSAG rings
   - Mixed chain (v10 + v11 blocks) syncs correctly

5. **Adversarial tests**
   - Attempt inflation via wrong pseudo-commitment
   - Attempt double-spend with reused key image
   - Attempt ring with all FOREVER-term members
   - Attempt ring referencing non-existent global indices

---

## File Change Summary

| File | Changes |
|------|---------|
| `src/crypto/mlsag.h` | NEW: mlsag_sign, mlsag_verify, helpers |
| `src/crypto/mlsag.cpp` | NEW: ~800 LOC core MLSAG implementation |
| `include/CryptoNote.h` | Remove amount from CommitmentSpend, add pseudoCommitment, add fee to TxPrefix |
| `src/CryptoNoteCore/CryptoNoteSerialization.cpp` | Serialize new fields, MLSAG sig format |
| `src/CryptoNoteCore/Blockchain.h` | Add m_allCommitmentOutputs, v11 validation methods |
| `src/CryptoNoteCore/Blockchain.cpp` | MLSAG verify, balance check, global ring resolution |
| `src/CryptoNoteCore/CommitmentIndex.h/.cpp` | Global output vector, new query methods |
| `src/Wallet/WalletGreen.cpp` | MLSAG ring build, pseudo-commitment, amountCommitment gen |
| `src/WalletLegacy/WalletTransactionSender.cpp` | Same for legacy wallet path |
| `src/Rpc/RpcServer.cpp` | New global commitment outs endpoint |
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | New RPC types |
| `src/CryptoNoteCore/Currency.cpp` | Testnet genesis nonce bump, v11 upgrade height |

**Estimated total: ~3000-4000 LOC new/modified**

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|-----------|
| Inflation bug in balance check | CRITICAL | Tier proofs cap amounts to 4 values; balance check is simple point arithmetic; test exhaustively |
| MLSAG implementation bug | CRITICAL | Port from Monero reference (battle-tested since 2017); unit test every code path |
| v10/v11 transition breaks existing deposits | HIGH | v10 CommitmentSpend path preserved; v11 gated by block version |
| Retroactive commitment for v10 outputs wrong | HIGH | Simple: C = amount*H, mask=0. Verify with pedersen_verify. |
| Global ring pool too small at launch | MEDIUM | Keep per-amount as fallback; v10 outputs pad the pool |
| Performance regression (2x slower ring verify) | LOW | 18 vs 9 scalar mults for ring size 9; still <10ms |
| Wire format change requires genesis reset | LOW | Testnet only; mainnet gets a proper upgrade height |

---

## What This Does NOT Solve (explicitly out of scope)

- **Bulletproofs** — NOT NEEDED. Denomination 1-of-N proofs replace range proofs.
  Fixed denominations constrain committed values without Bulletproofs.
- **Term privacy** — term stays visible. Not needed (see PRIVACY_ROADMAP.md).
- **CLSAG optimization** — half the sig size (v12). Do after MLSAG is stable.
- **Triptych** — logarithmic ring proofs over denomination bin Merkle trees (v13).
  Anonymity set grows to entire bin. Do after CLSAG. See PRIVACY_ROADMAP.md Phase 7.
- **Dandelion++** — network-layer privacy. Independent of MLSAG. Do in parallel.
