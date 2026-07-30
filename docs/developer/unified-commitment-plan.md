# Unified Commitment Output Plan

## Problem

The codebase has 4 output types, 3 input types, 2 ring selection paths, and 2 wallet scanning paths — all doing essentially the same thing (prove ownership of an output and transfer value). The output type discriminates the asset (KeyOutput = XFG, TransactionOutputCommitment with magic term = HEAT/LP/pool, finite term = CD) but this fragmentation leaks information and adds complexity.

## Insight

`TransactionOutputCommitment` already has everything needed to replace ALL output types:

- `commitKey` — ring-sig spend key (equivalent to KeyOutput's stealth address)
- `term` — 0 = regular transfer, magic values = special types, positive = timelocked CD
- `amountCommitment` — Pedersen C = amount*H + mask*G (currently serialized, zeroed)
- `amountProof` — 1-of-4 membership proof (currently serialized, zeroed)

And `TransactionInputCommitmentSpend` already replaces `KeyInput`:
- Ring sig over global commitment output indices
- Key image for double-spend prevention
- No need for per-amount ring pools once amounts are hidden

Making XFG also use `TransactionOutputCommitment` (term=0) collapses everything into one type system.

## Target Architecture

### Outputs (one type)

```
TransactionOutputCommitment {
  commitKey                           // spend key (stealth address equivalent)
  term                                // 0=XFG transfer, FOREVER=HEAT, finite=CD, magic=pools
  amountCommitment                    // Pedersen commitment (hidden amount)
  amountProof                         // 1-of-4 tier proof
}
```

| term value | Meaning |
|---|---|
| `0` | Regular XFG transfer (no lock) |
| `≥ 1` ≤ `MAX_TERM` | XFG CD (timelocked epochs) |
| `FOREVER` (0xFFFFFFFF) | HEAT stablecoin (permanent circulation) |
| `LP` (0xFFFFFFFD) | Liquidity pool share |
| `POOL_XFG` (0x504F4C58) | AMM pool XFG reserve |
| `POOL_HEAT` (0x504F4C48) | AMM pool HEAT reserve |
| `SWAP_RECEIVE_XFG` (0x53575258) | Swap output XFG |

### Inputs (one type)

```
TransactionInputCommitmentSpend {
  outputIndexes                       // global commitment output ring indices
  keyImage                            // H_p(commitKey) * keyScalar
  claimedInterest                     // for CD withdrawals
}
```

Ring sig (v10) or MLSAG (v11) proves spend authority. `amount` removed from input (amount hidden in commitment balance proof).

### Decoy Pool (one pool)

`m_allCommitmentOutputs: vector<CommitmentOutputRef>` — flat global index. All outputs are eligible decoys regardless of term or amount.

---

## Phase 0: Constrain Cleanup (immediate)

**Goal:** Rename `DEPOSIT_TERM_FOREVER` → `HEAT_TERM` and define `TERM_REGULAR = 0`.

### Changes
1. `src/CryptoNoteConfig.h`:
   - Rename `DEPOSIT_TERM_FOREVER` → `HEAT_TERM`
   - Remove alias `DEPOSIT_TERM_BURN` (same value, unused)
   - Add `const uint32_t TERM_REGULAR = 0;`
   - Keep `DEPOSIT_TERM_LP`, `DEPOSIT_TERM_POOL_XFG`, `DEPOSIT_TERM_POOL_HEAT`, `DEPOSIT_TERM_SWAP_RECEIVE_XFG` as-is (or rename if desired)

2. Full codebase rename: `DEPOSIT_TERM_FOREVER` → `HEAT_TERM` across ~50 files

3. Add `TERM_REGULAR` usage in:
   - `Currency::classifyOutputAsset()` — term==0 → AssetType::XFG
   - `Blockchain::pushToBankingIndex()` — skip term==0 outputs in deposit/burn tracking

### Verification
- `grep -rn 'DEPOSIT_TERM_FOREVER\|DEPOSIT_TERM_BURN' src/` returns 0
- All 15 build targets pass

---

## Phase 1: Bridge — XFG Uses Commitment Outputs (dual-write)

**Goal:** All new XFG sends emit BOTH a `KeyOutput` (for legacy wallets) AND a `TransactionOutputCommitment` with `term=0`. Commitment output goes into the global decoy pool. Legacy code ignores it. New wallets can already scan and spend it.

### Changes

#### 1a. WalletTransactionSender: dual-output on every XFG send

Each regular XFG `addOutput(amount, m_keys.address)` (KeyOutput) gets paired with a `TransactionOutputCommitment` at `term=0` with matching amount:

```
// Current:
void TransactionSender::sendXfg(...) {
  for (auto& out : outputs) {
    auto keys = deriveOneTimeKey(recipient, ecdh, index);
    transaction->addOutput(out.amount, KeyOutput{keys.pubKey});  // KeyOutput
  }
}

// New:
void TransactionSender::sendXfg(...) {
  for (auto& out : outputs) {
    // Legacy KeyOutput (for old wallets)
    auto keys = deriveOneTimeKey(recipient, ecdh, index);
    transaction->addOutput(out.amount, KeyOutput{keys.pubKey});

    // Commitment output (term=0, for new wallets + decoy pool)
    auto commitKeys = deriveCommitmentKeys(
      H(ecdh || outputIndex)  // same ECDH the recipient can compute
    );
    TransactionOutputCommitment co;
    co.commitKey = commitKeys.commitKey;
    co.term = 0;
    // amountCommitment + amountProof remain zeroed (Phase 2)
    transaction->addOutput(out.amount, co);
  }
}
```

Recipient can scan both: KeyOutput via standard CryptoNote scan, commitment via ECDH + commitKey derivation.

#### 1b. CommitmentIndex: accept term=0 outputs

`CommitmentIndex::add()` already handles all terms. Ensure term=0 outputs are indexed in `m_allCommitmentOutputs` (flat vector for decoy selection).

#### 1c. Wallet scan: detect term=0 commitment outputs

`WalletLegacy::processTransaction()` currently skips commitment outputs that aren't FOREVER or finite-term deposits. Add detection for term=0 (ECDH + commitKey match).

#### 1d. Ring selection: add term=0 decoys

`Blockchain::getRandomCommitmentOutputs()` already selects from commitment outputs. Ensure term=0 outputs are eligible as decoys (they are naturally — the per-amount pool already includes them by amount).

### Wallet Changes Detail

For the wallet to recover term=0 commitment outputs:

```
For each output in transaction:
  if output is TransactionOutputCommitment with term == 0:
    ecdh = viewSecretKey * txPublicKey         // same as KeyOutput scan
    for i in 0..outputCount:
      depositSecret = H(ecdh || i)             // commitment derivation
      commitKeys = deriveCommitmentKeys(depositSecret)
      if output.commitKey == commitKeys.commitKey:
        // This output belongs to us
        balance += output.amount
```

This is identical to how COLD deposit recovery works — the ECDH is the same shared secret.

### Verification
- `test_send_xfg` creates both output types, both spendable
- `test_scan_term0` recovers wallet from seed, finds term=0 outputs
- Ring selection includes term=0 outputs as decoys
- Legacy wallet ignores term=0 outputs (graceful degradation)

---

## Phase 2: Pedersen Commitments + Range Proofs (hidden amounts)

**Goal:** Every `TransactionOutputCommitment` carries a real Pedersen commitment and a range proof. `TransactionOutput.amount` set to 0 on the wire — amount lives only in the commitment. One output per logical transfer (no decomposition).

### Why Not Denominations

The original design used 1-of-4 membership proofs (`amountProof`) for fixed tier denominations. This was intended to avoid implementing range proofs. But it forces amount decomposition and fixed-value outputs — exactly the wrong thing for a note-based system.

**The commitment output IS the note.** One output = one Pedersen commitment = one amount, hidden. The proof only needs to guarantee the committed amount is valid (non-negative, within range). That's a range proof, not a membership proof.

| Approach | Amount granularity | Proof size | Complexity |
|---|---|---|---|
| Membership (1-of-N tiers) | Fixed denominations | ~256 bytes (N=4) | Simple |
| Range proof (Bulletproofs+) | Arbitrary | ~672 bytes (aggregated) | ~2500 LOC |
| Membership via finer tiers | 0.008 granularity | ~512 bytes (N=8) | Medium |

**Recommendation: Bulletproofs+ (range proofs).** One commitment, any amount, one proof. No decomposition, no denominations, no remainder dust. This is the Monero approach and the correct model for a privacy coin.

The membership proof infra (`tier_proof.h/.cpp`) stays for **CD inputs only** — CDs need tier enforcement for banking interest calculation. General transfers (term=0 XFG, FOREVER HEAT) use range proofs.

### Prerequisites

- `pedersen.h/.cpp` — compiles, no callers
- `deriveCommitmentKeys()` — already derives `amountMask`
- Need: Bulletproofs+ (`bulletproofs_plus.h/.cpp`) — port from Monero rct/bulletproofs_plus.cc
- Need: Multi-scalar multiplication (`multiexp.cpp`) — port from Monero common/multiexp.cc

### Changes

#### 2a. Wallet: generate Pedersen commitment + range proof

```
// One commitment per output. No decomposition.
auto commitment = pedersen_commit(out.amount, commitKeys.amountMask);
auto rangeProof = bulletproofs_plus_generate(
    {commitment},           // single output
    {out.amount},           // amounts
    {commitKeys.amountMask} // blinding factors
);
output.amountCommitment = commitment;
// output.amountProof repurposed or replaced as range proof blob
output.amount = 0;          // amount hidden on wire
```

#### 2b. Wallet: MLSAG signing with pseudo-commitments

```
For each input i:
  C_pseudo[i] = pedersen_commit(amount, random_z_i)
  // MLSAG layer 1: prove C_pseudo[i] - C_input[i] = (z_i - mask_i)*G

For the last input to balance:
  z_last = sum(output_masks) - sum(z_i for other inputs) + input_mask_last
  C_pseudo[last] = pedersen_commit(input_amount, z_last)

// MLSAG sign with key_matrix[i] = (commitKey[i], C_pseudo - C_ring[i])
```

#### 2c. Blockchain: verify range proof + MLSAG balance

```
// On every commitment output:
if (output is commitment) {
  point_validate(output.amountCommitment);        // valid ed25519 point
  bulletproofs_plus_verify({output.amountCommitment}, rangeProof);
}

// On every commitment spend input (MLSAG):
mlsag_verify(message, key_matrix, keyImage, c0, sigs);
check_key_image_unspent(keyImage);

// Global balance:
sum(C_pseudo inputs) - sum(C_output) - fee*H == identity
```

#### 2d. Global ring pool

Single flat `vector<CommitmentOutputRef>` holding all commitment outputs regardless of amount, term, or asset type. The decoy pool is every output on the chain.

RPC: `getRandomCommitmentOuts(count)` — no amount filter. Returns random commitment outputs with their commitment points (not amounts — amounts are hidden).

Per-amount index `m_outputs` (KeyOutput ring pool) can be deprecated in Phase 4.

### Verification
- `out.amount == 0` on wire for commitment outputs
- Range proof verifies committed amount is valid
- MLSAG balance proof: sum(C_pseudo) - sum(C_output) - fee*H == 0
- Inflation attempt fails at balance check
- Ring selection from global pool (no per-amount filter)
- Legacy v10 outputs with visible amount still spendable (retroactive commitment C = amount*H + 0*G)

---

## Phase 3: Fully Shielded (MLSAG + BP+ live)

**Goal:** All transactions use hidden amounts. `KeyOutput` only exists in historical blocks. Every new output is a commitment.

### Changes

#### 3a. Remove legacy ring sig code path

v10 `check_ring_signature` only used for historical block validation. New txs always use MLSAG.

#### 3b. Hide term for non-CD outputs

term=0 (regular XFG) and term=FOREVER (HEAT) outputs can optionally hide their term via the commitment. CDs still need visible term for timelock enforcement.

(Low priority — term visibility doesn't meaningfully reduce the decoy pool.)

#### 3c. Wallet: default MLSAG

All wallet spend operations use MLSAG by default. Simple ring sigs are a fallback for spending pre-v11 outputs observed by the wallet.

### Verification
- All new txs have zero visible amounts
- All new outputs carry range proofs
- All new inputs use MLSAG
- Old blocks still sync and validate

---

## Phase 4: Cleanup — Deprecate Legacy Types

**Goal:** Remove `KeyOutput`, `KeyInput`, `MultisignatureOutput`, `TransactionOutputUnified` from the codebase. Every output is `TransactionOutputCommitment`. Every input is `TransactionInputCommitmentSpend` (or `TransactionInputUnified` if MLSAG).

### Changes

#### 4a. Remove KeyOutput creation

All wallet `addOutput(amount, m_keys.address)` → `TransactionOutputCommitment` with `term=0`.

#### 4b. Remove KeyOutput consumption

All wallet `KeyInput` → `TransactionInputCommitmentSpend`.

#### 4c. Remove legacy scanning paths

Wallet scan only checks `TransactionOutputCommitment` outputs. Recover with ECDH → depositSecret → commitKey derivation.

#### 4d. Remove legacy serializers

- `serialize(KeyOutput&)` — remove
- `serialize(MultisignatureOutput&)` — remove
- `TransactionOutputUnified` (never used) — remove
- `TransactionInputUnified` (subsumed by MLSAG on CommitmentSpend) — remove

#### 4e. Remove per-amount ring pools

`m_outputs` (per-amount key output index) can be removed. Only `m_allCommitmentOutputs` remains.

### Verification
- No `KeyOutput`, `KeyInput`, `MultisignatureOutput` in any transaction
- All wallet operations create/consume commitment outputs
- Block validation doesn't reference legacy types
- Serialization code doesn't reference legacy tags
- 100% backward compatible for chain sync (historical blocks still have legacy types — skip during scan, never create)

---

## Migration Timeline

| Phase | Fork? | Blocks to accumulate | Description |
|---|---|---|---|
| 0 | No | 0 | Rename constants |
| 1 | No | ~10k | Dual-write XFG commitments alongside KeyOutput |
| 2 | Maybe | ~50k | Populate amountCommitment + amountProof; verify on-chain |
| 3 | Yes (v12) | ~100k | MLSAG activation; hidden amounts fully live |
| 4 | No | ~200k | Code cleanup; remove dead types |

Phase 1 and 2 don't require a hard fork — they're additive (new field population, new verification that degrades gracefully for unpopulated fields).

Phase 3 requires a hard fork because the wire format for `TransactionInputCommitmentSpend` changes (`amount` removed, `pseudoCommitment` added) and MLSAG verification replaces ring sig verification.

---

## Key Risks

| Risk | Mitigation |
|---|---|---|
| Phase 1 doubles output count (KeyOutput + commitment) | 2x tx size for 10k blocks; acceptable. Commitment outputs are 36 bytes (KeyOutput is 32). |
| term=0 outputs confuse existing wallet logic | Gate on block version. Old wallets ignore unknown output types. |
| Bulletproofs+ port from Monero has bugs | Exhaustive unit tests; reference test vectors from Monero; cap to testnet-only until audited. |
| MLSAG implement bug causes inflation | Range proof caps committed value to [0, 2^64). Balance proof is simple point arithmetic. Two independent defenses. |
| Range proof size (~672 bytes aggregated) | Acceptable for v12. CLSAG in v13 cuts tx size further. |

## References

- `docs/MLSAG_DEV_PLAN.md` — MLSAG algorithm, crypto primitives, integration
- `docs/PRIVACY_ROADMAP.md` — long-term privacy evolution (CLSAG, BP+, Triptych)
- `docs/commitment-types.md` — commitment type discrimination design
- `docs/hearth-amm-commitment-plan.md` — AMM pool commitment integration
