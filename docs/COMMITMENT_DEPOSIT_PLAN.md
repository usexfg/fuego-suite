# Commitment-Output Deposits
# Enabling Ring-Signature Withdrawals — Implementation Roadmap
# Block Major Version: BLOCK_MAJOR_VERSION_10
# Branch: HE4T
# Last updated: 2026-02-20

---

## Background / Why

Current deposit withdrawals use `MultisignatureInput` which directly names the spent
deposit via a single `outputIndex`. This makes ring signatures mathematically impossible.

Goal: Replace with commitment-key outputs that are indistinguishable from regular key
outputs to observers, with withdrawals using standard CryptoNote ring signatures over a
set of commitment outputs. Double-spending prevented by a nullifier set.

Scope for now: Amount is PUBLIC (no Pedersen commitments / Bulletproofs needed yet).
This gives ring-sig unlinkability for WHICH deposit is spent, while keeping amounts visible.

---

## Phase 1 — New Data Structures

### Files: `include/CryptoNote.h`, `src/CryptoNoteCore/CryptoNoteSerialization.cpp`

#### 1a. New output type — `TransactionOutputCommitment`
```cpp
struct TransactionOutputCommitment {
  uint64_t amount;             // Deposit principal (visible)
  uint32_t term;               // Lock term in blocks
  uint64_t unlockHeight;       // Block height when withdrawal is allowed (0 = unconfirmed)
  Crypto::PublicKey commitKey; // = scalar(H(depositSecret||"commit_key")) * G
};
```

#### 1b. New input type — `TransactionInputCommitmentSpend`
```cpp
struct TransactionInputCommitmentSpend {
  uint64_t amount;                      // Must match the commitment output being spent
  uint32_t term;                        // Must match the commitment output term
  std::vector<uint32_t> outputIndexes;  // Global commitment output indices (ring members)
  Crypto::KeyImage keyImage;            // H_p(commitKey) * keyScalar — standard CryptoNote
  Crypto::Hash nullifier;               // H(depositSecret||"nullifier") — explicit double-spend guard
};
```

#### 1c. Update variant types in `CryptoNote.h`
- Add `TransactionOutputCommitment` to `TransactionOutput` variant
- Add `TransactionInputCommitmentSpend` to `TransactionInput` variant

#### 1d. Serialization in `CryptoNoteSerialization.cpp`
- New tag byte for each type (choose values not yet used):
  - `TransactionOutputCommitment`: tag `0x03` (after KeyOutput=0x02, MultisigOutput was 0x03 — check)
  - `TransactionInputCommitmentSpend`: tag `0x04` (after MultisignatureInput=0x03 — check)
- Follow exact same pattern as existing MultisignatureOutput/Input serialization

#### 1e. `CryptoNoteFormatUtils.cpp`
- Update `getInputAmount()` to handle `TransactionInputCommitmentSpend`
- Update `getOutputAmount()` to handle `TransactionOutputCommitment`
- Update `getTransactionInputsAmount()`, `getTransactionOutputsAmount()`
- Update `checkInputsTypes()` / any input-type visitors
- Update `is_tx_spendtime_unlocked()` to handle commitment outputs

---

## Phase 2 — Key Derivation Helpers

### File: `src/CryptoNoteCore/TransactionExtra.h/.cpp`

Add to `TransactionExtra.h`:
```cpp
// Commitment key derivation for ring-sig deposit outputs
// depositSecret: random 32 bytes, encrypted to own view key in tx_extra
struct DepositCommitmentKeys {
  Crypto::PublicKey  commitKey;    // = keyScalar * G (on-chain public key)
  Crypto::SecretKey  keyScalar;    // = H(secret||"commit_key") mod l  (spend key)
  Crypto::KeyImage   keyImage;     // = H_p(commitKey) * keyScalar
  Crypto::Hash       nullifier;    // = H(secret||"nullifier")
};

DepositCommitmentKeys deriveCommitmentKeys(const std::array<uint8_t, 32>& depositSecret);
```

Add to `TransactionExtra.cpp`:
```cpp
DepositCommitmentKeys deriveCommitmentKeys(const std::array<uint8_t, 32>& depositSecret) {
  DepositCommitmentKeys keys;

  // keyScalar = H("commit_key" || depositSecret) interpreted as Ed25519 scalar
  uint8_t keyPreimage[42];
  memcpy(keyPreimage, "commit_key", 10);
  memcpy(keyPreimage + 10, depositSecret.data(), 32);
  Crypto::Hash keyHash = cn_fast_hash(keyPreimage, 42);
  // Convert hash to scalar (reduce mod l)
  memcpy(keys.keyScalar.data, keyHash.data, 32);
  // Clamp to valid Ed25519 scalar (sc_reduce32)
  Crypto::secret_key_to_public_key(keys.keyScalar, keys.commitKey);

  // nullifier = H("nullifier" || depositSecret)
  uint8_t nullPreimage[40];
  memcpy(nullPreimage, "nullifier", 9);
  memcpy(nullPreimage + 9, depositSecret.data(), 32);
  // pad to 40 bytes
  memset(nullPreimage + 41, 0, 0); // already 41 bytes fine
  keys.nullifier = cn_fast_hash(nullPreimage, 41);

  // keyImage = H_p(commitKey) * keyScalar
  Crypto::generate_key_image(keys.commitKey, keys.keyScalar, keys.keyImage);

  return keys;
}
```

Also add: `TX_EXTRA_DEPOSIT_SECRET` tag (new tag, e.g. `0xD5`) for encrypted deposit secret:
```cpp
struct TransactionExtraDepositSecret {
  std::vector<uint8_t> encryptedSecret;  // depositSecret XOR-encrypted with view key derivation
  Crypto::Hash commitment;               // H(depositSecret) for verification
};
```
Secret encryption: use `generate_key_derivation(walletViewPub, ephemeralSecKey)` → derive
32-byte mask → XOR with depositSecret. Same pattern as tx_extra message encryption.

---

## Phase 3 — Deposit Creation Changes

### File: `src/WalletLegacy/WalletTransactionSender.cpp`

#### 3a. `makeDepositRequest()` changes

Gate on block version ≥ v10:
```cpp
// Check if we should use commitment outputs (v10+ blocks)
bool useCommitmentOutput = (currentBlockVersion >= BLOCK_MAJOR_VERSION_10);
```

For commitment path:
1. Generate `depositSecret = random_bytes(32)`
2. Call `deriveCommitmentKeys(depositSecret)` → get `commitKey`, `keyImage`, `nullifier`
3. Build `TransactionOutputCommitment { amount, term, unlockHeight=0, commitKey }`
4. Encrypt `depositSecret` with wallet's view key → `TX_EXTRA_DEPOSIT_SECRET` in tx_extra
5. Also keep existing `TX_EXTRA_HEAT_COMMITMENT` / `TX_EXTRA_ELDERFIER_DEPOSIT` tags as appropriate
6. Add commitment output to transaction instead of multisig output

For COLD/YIELD deposits: same pattern — commitment output instead of multisig.
For HEAT burns: also use TransactionOutputCommitment. Generate random depositSecret,
derive commitKey, then discard the secret (no TX_EXTRA_DEPOSIT_SECRET in tx_extra).
HEAT burns never withdraw but serve as ring decoys, bulking up the decoy pool.

#### 3b. `doSendDepositWithdrawTransaction()` changes

For commitment path:
1. Scan wallet's transactions for the deposit tx
2. Recover `depositSecret` from tx_extra `TX_EXTRA_DEPOSIT_SECRET` (decrypt with view key)
3. Re-derive `commitKey`, `keyScalar`, `keyImage`, `nullifier`
4. Call new RPC `get_commitment_outputs` to fetch ring members
5. Build `TransactionInputCommitmentSpend` with ring
6. Generate ring signature: `generate_ring_signature(prefixHash, keyImage, pubs[], keyScalar, realIndex)`

#### 3c. `makeGetRandomOutsRequest()` / new parallel function
Add `makeGetCommitmentOutsRequest()` that calls `/get_commitment_outputs` RPC.

---

## Phase 4 — Nullifier Set + Blockchain Validation

### File: `src/CryptoNoteCore/Blockchain.h/.cpp`

#### 4a. `Blockchain.h` — add nullifier set
```cpp
// Spent commitment nullifiers (prevents double-spend without naming the specific output)
std::unordered_set<Crypto::Hash> m_spentNullifiers;
```
Persist alongside `m_spentKeys` (same DB or serialized file).

#### 4b. `check_tx_inputs()` — new case for commitment spends
```cpp
// For TransactionInputCommitmentSpend:
// 1. Check blockMajorVersion >= BLOCK_MAJOR_VERSION_10
// 2. Check nullifier not in m_spentNullifiers
// 3. Check keyImage not in m_spentKeys (reuse existing key image set)
// 4. Check mixin = outputIndexes.size() - 1 >= minMixin(blockMajorVersion)
// 5. Verify each outputIndex refers to a valid, unlocked TransactionOutputCommitment
//    of matching amount and term
// 6. Verify ring signature over {commitmentOutput.commitKey for each outputIndex}
```

Ring signature verification: use existing `check_ring_signature()` — same algorithm
as for `KeyInput`, just over commitment keys instead of stealth addresses.

#### 4c. `pushBlock()` — record spent nullifiers
```cpp
// After block is accepted:
for each TransactionInputCommitmentSpend in block:
  m_spentNullifiers.insert(input.nullifier);
  m_spentKeys.insert(input.keyImage);  // also track key image
```

#### 4d. `popBlock()` (for chain reorg) — remove nullifiers
```cpp
for each TransactionInputCommitmentSpend in popped block:
  m_spentNullifiers.erase(input.nullifier);
  m_spentKeys.erase(input.keyImage);
```

#### 4e. Rebuild on `init()` (for fresh load)
Walk blockchain and re-populate `m_spentNullifiers` from confirmed commitment spends.

#### 4f. `check_tx_mixin()` — extend for new input type
```cpp
// Count mixin for TransactionInputCommitmentSpend same as KeyInput:
txMixin = input.outputIndexes.size() - 1;
// Apply same minimum mixin rules
```

---

## Phase 5 — CommitmentIndex Global Output Index

### Files: `src/CryptoNoteCore/CommitmentIndex.h/.cpp`

#### 5a. New struct `CommitmentOutputRef`
```cpp
struct CommitmentOutputRef {
  uint64_t globalIndex;          // Global index across all commitment outputs
  Crypto::PublicKey commitKey;   // The on-chain public key
  uint64_t amount;               // Deposit amount
  uint32_t term;                 // Deposit term (blocks)
  uint64_t unlockHeight;         // When this output becomes spendable
  Crypto::Hash txHash;           // Source transaction hash
  uint32_t outputIndexInTx;      // Index within the source transaction
  bool spent;                    // True after withdrawal confirmed
};
```

#### 5b. New storage + methods in `CommitmentIndex`
```cpp
// In CommitmentIndex private:
std::vector<CommitmentOutputRef> m_commitmentOutputs;  // global index → output
uint64_t m_nextCommitmentGlobalIndex = 0;

// New public methods:
uint64_t addCommitmentOutput(const CommitmentOutputRef& ref);  // returns globalIndex
CommitmentOutputRef getCommitmentOutput(uint64_t globalIndex) const;
void markCommitmentOutputSpent(uint64_t globalIndex);

// For ring member selection — same amount only (not term), not spent:
std::vector<CommitmentOutputRef> getRandomCommitmentOutputs(
    uint64_t amount,
    size_t count,
    uint64_t excludeGlobalIndex  // exclude the real one
) const;

uint64_t getCommitmentOutputCount() const;
uint64_t getCommitmentOutputCount(uint64_t amount) const;
```

#### 5c. Wire into `Blockchain::pushToBankingIndex()`
When a `TransactionOutputCommitment` is found in a confirmed transaction:
```cpp
CommitmentOutputRef ref;
ref.commitKey = output.commitKey;
ref.amount = output.amount;
ref.term = output.term;
ref.unlockHeight = blockHeight + output.term;
ref.txHash = txHash;
ref.outputIndexInTx = outputIdx;
ref.spent = false;
m_commitmentIndex.addCommitmentOutput(ref);
```

When a `TransactionInputCommitmentSpend` is found:
```cpp
// Find the output being spent by keyImage match and mark spent
m_commitmentIndex.markCommitmentOutputSpentByKeyImage(input.keyImage);
```

#### 5d. Serialization
Persist `m_commitmentOutputs` to disk alongside other CommitmentIndex data.

---

## Phase 6 — RPC (deferred, but needed for wallet to build rings)

### File: `src/Rpc/RpcServer.cpp`, `CoreRpcServerCommandsDefinitions.h`

#### New endpoint: `GET /get_commitment_outputs`
```
Request:
  { "amount": 800000000000, "term": 259200, "count": 9 }

Response:
  { "outputs": [
      { "global_index": 42, "commit_key": "abcd...", "amount": 800000000000, "term": 259200 },
      ...
    ]
  }
```
Used by wallet when building withdrawal ring members.

---

## Key Constants / Tags (verify against existing before using)

```
TX_EXTRA_DEPOSIT_SECRET  = 0xD5   // encrypted deposit secret for commitment deposits
Output tag for TransactionOutputCommitment      = check CryptoNoteSerialization for free slot
Input tag for TransactionInputCommitmentSpend   = check CryptoNoteSerialization for free slot

Block version gate: BLOCK_MAJOR_VERSION_10 / TRANSACTION_VERSION_2
```

---

## File Change Summary

| File | Changes |
|---|---|
| `include/CryptoNote.h` | Add `TransactionOutputCommitment`, `TransactionInputCommitmentSpend` to variant types |
| `src/CryptoNoteCore/CryptoNoteSerialization.cpp` | Serialize/deserialize new types |
| `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp` | Amount getters, input type checks for new types |
| `src/CryptoNoteCore/TransactionExtra.h` | Add `DepositCommitmentKeys`, `TransactionExtraDepositSecret`, `TX_EXTRA_DEPOSIT_SECRET` |
| `src/CryptoNoteCore/TransactionExtra.cpp` | `deriveCommitmentKeys()`, secret encrypt/decrypt |
| `src/CryptoNoteCore/Blockchain.h` | `m_spentNullifiers` set |
| `src/CryptoNoteCore/Blockchain.cpp` | `check_tx_inputs()`, `pushBlock()`, `popBlock()`, `init()` |
| `src/CryptoNoteCore/CommitmentIndex.h` | `CommitmentOutputRef`, new query methods |
| `src/CryptoNoteCore/CommitmentIndex.cpp` | Global commitment output index impl |
| `src/WalletLegacy/WalletTransactionSender.cpp` | `makeDepositRequest()`, `doSendDepositWithdrawTransaction()` |
| `src/Rpc/RpcServer.cpp` | `/get_commitment_outputs` endpoint (Phase 6) |
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | New RPC types (Phase 6) |

---

## Backward Compatibility Notes

- Old `MultisignatureOutput` deposits: remain valid, withdrawable via old path
- Old `MultisignatureInput` withdrawal: still accepted at any block version
- New commitment outputs: only created in v10+/TXNv2 blocks
- New commitment spends: only valid in v10+/TXNv2 blocks
- No migration of old deposits needed — they expire naturally as users withdraw
- Wallet must detect output type when scanning: if `TransactionOutputCommitment` found,
  save `depositSecret` from `TX_EXTRA_DEPOSIT_SECRET`, not the old multisig approach

---

## Implementation Order (start next session here)

1. Read `include/CryptoNote.h` fully — understand current variant definitions
2. Read `CryptoNoteSerialization.cpp` MultisignatureInput/Output sections — copy pattern
3. Read `CryptoNoteFormatUtils.cpp` amount getter and input checker sections
4. Read `Blockchain.cpp` `check_tx_inputs()` and `m_spentKeys` usage
5. Implement Phase 1 (new types + serialization) — build test
6. Implement Phase 2 (key derivation helpers) — build test
7. Implement Phase 5 (CommitmentIndex global index) — isolated, can be done independently
8. Implement Phase 4 (Blockchain nullifier set + validation) — build test
9. Implement Phase 3 (Wallet deposit/withdraw changes) — build test
10. Implement Phase 6 (RPC) — build test

---

## Open Questions / Decisions Before Coding

Q1: What output/input tag bytes are free in CryptoNoteSerialization?
    (Check existing: KeyOutput=0x02, KeyInput=0xFF or similar, MultisigOutput=0x03, MultisigInput)
    → Need to verify exact current values before assigning new ones.

Q2: How is `unlockHeight` set in the commitment output?
    Option A: Set to 0 at creation, updated by CommitmentIndex when term elapses
    Option B: Compute `unlockHeight = confirmationBlock + term` when indexing
    → Option B preferred: unlockHeight computed at index time, not stored in tx.

Q3: Ring member selection — same amount AND term, or just same amount?
    → DECIDED: same amount only (not term).
    → All TransactionOutputCommitment outputs of matching amount are eligible decoys
      regardless of term. HEAT burns (term=FOREVER) naturally bulk up every ring
      of matching amount without special-casing.

Q4: `TX_EXTRA_DEPOSIT_SECRET` encryption scheme
    → Use `generate_key_derivation(walletViewPub, txSecretKey)` → derive 32-byte mask
    → XOR mask with depositSecret → store in tx_extra
    → Wallet recovers: `generate_key_derivation(txPubKey, walletViewSecret)` → same mask → XOR back
    → This reuses the existing CryptoNote stealth address key derivation pattern exactly

Q5: For EF/COLD/YIELD/HEAT deposits — same commitment output type for all?
    → DECIDED: Yes. TransactionOutputCommitment used for ALL deposit types including HEAT burns.
    → HEAT burns: generate random commitKey, discard secret, never withdraw.
      term=FOREVER (0xFFFFFFFF) stored in output but irrelevant for ring selection (amount-only).

---

## Notes from Previous Session

- `makeDepositRequest()` already enforces mixin ≥ 8 for input side (fixed this session)
- `check_tx_mixin()` already has `&& txMixin != 1` exception removed (fixed this session)
- `parseTransactionExtra` stream bug already fixed (previous session)
- Elderfier commitment (one-way H(spendPub||ephemeralPub)) already implemented this session
- Alias ownerAddress privacy (pubhash reverse lookup) already implemented this session

---
