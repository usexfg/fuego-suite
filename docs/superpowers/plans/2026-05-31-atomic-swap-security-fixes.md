# Atomic Swap Security Fixes — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking. Execute chunks in severity order: A → B → C → E (D is cross-referenced to the AFK plan).

**Goal:** Remediate the atomic-swap audit findings — make the escrow secret confidential at rest, enforce the cross-chain timelock invariant correctly, authenticate and harden swap P2P, and add defensive point/oracle checks.

**Architecture:** Keep changes inside `src/SwapDaemon/` + `src/crypto/`. Reuse in-tree crypto (`Crypto::generate_chacha8_key` = CryptoNight memory-hard KDF; `cn_fast_hash` = Keccak for a keyed MAC; `Crypto::generate_signature`/`check_signature` for P2P auth). The escrow encryption key is derived once at daemon startup from the wallet spend secret and held only in memory — never serialized.

**Tech Stack:** C++17, `src/crypto/chacha8.h`, `src/crypto/hash.h` (Keccak `cn_fast_hash`), `src/crypto/crypto.h` (Ed25519 sign/verify, `ge_*` ops), standalone `int main()` test exes (per `src/SwapDaemon/tests/test_adaptor_roundtrip.cpp`; no gtest).

**Source findings:** `docs/review/atomic-swap-security-fix-plan.md`

---

## Context

Audit of the atomic-swap framework found fund-loss-class issues. Headlines:
- **C1:** `setEncryptionKey()` is never called → the escrow-controlling `adaptorSecret` is written to the swap DB in **plaintext** ([SwapStateMachine.cpp:216](src/SwapDaemon/SwapStateMachine.cpp:216)), in a default-umask file, never zeroized.
- **H1/H2/M1:** Even if enabled, the key is stored in the same record (`encKey`), the KDF is a homebrew XOR, and the cipher is unauthenticated ChaCha8 mislabelled ChaCha20.
- **H3:** The timelock-ordering guard compares XFG height to a raw foreign height/slot ([SwapDaemon.cpp:335](src/SwapDaemon/SwapDaemon.cpp:335)) and is skipped when `ctrTimeoutBlock==0` — the atomic-swap safety invariant is unenforced.
- **M2–M5:** Swap P2P messages are unauthenticated; the accept loop is a slowloris target; the pending-message queue is unbounded; it binds `INADDR_ANY`.
- **L2/L3:** No identity/small-order point checks; TWAP manipulable within a wide floor.

Verified sound (do not touch): adaptor-sig + DLEQ math, signed offer relay, RPC auth gate, plain-`KeyOutput` escrow (no on-chain swap tag → good privacy).

---

## File Structure

| File | Responsibility | Change |
|------|----------------|--------|
| `src/SwapDaemon/SwapSecretEncryption.{h,cpp}` | KDF + authenticated encryption of the 32-byte secret | Rewrite: real KDF, AEAD-style MAC, zeroize |
| `src/SwapDaemon/SwapStateMachine.{h,cpp}` | Serialize/deserialize swap record | Remove `encKey`; new encrypted-secret format; migration |
| `src/SwapDaemon/SwapDatabase.{h,cpp}` | Persist records to disk | Hold in-mem key, `setEncryptionKey` on save/load, `chmod 0600`/`0700` |
| `src/SwapDaemon/SwapDaemon.{h,cpp}` | Daemon wiring | Derive enc key at startup; mandatory timelock check |
| `src/SwapDaemon/SwapTimelock.{h,cpp}` (new) | Cross-chain timelock comparison in wall-clock | New |
| `src/SwapDaemon/SwapP2P.{h,cpp}` | Swap transport | Worker accept, bounded queue, scoped bind |
| `src/SwapDaemon/SwapPeerProtocol.{h,cpp}` | Peer message (de)serialization | Add signature field + signing/verify helpers |
| `src/crypto/adaptor.cpp`, `src/crypto/dleq.cpp` | Adaptor/DLEQ primitives | Reject identity/small-order points |
| `src/SwapDaemon/PriceOracle.cpp` | TWAP/seed pricing | Tighten manipulation bounds |
| `src/SwapDaemon/tests/*.cpp` | Standalone tests | New per task |

---

## Chunk A: Secret-at-rest (C1, H1, H2, M1, L1, L4)

Make the escrow secret confidential and tamper-evident at rest, with a key that is never written to disk.

### Task A1: Rewrite SwapSecretEncryption with real KDF + MAC

**Files:**
- Modify: `src/SwapDaemon/SwapSecretEncryption.h`
- Modify: `src/SwapDaemon/SwapSecretEncryption.cpp`
- Test: `src/SwapDaemon/tests/test_swap_secret_encryption.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// test_swap_secret_encryption.cpp — standalone, mirrors test_adaptor_roundtrip.cpp
#include "SwapDaemon/SwapSecretEncryption.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace XfgSwap;

int main() {
  Crypto::SecretKey secret; for (int i=0;i<32;i++) secret.data[i] = (uint8_t)(i*7+1);
  std::string key = "correct horse battery staple";

  SwapSecretEncryption::EncryptedSecret blob;
  assert(SwapSecretEncryption::encrypt(secret, key, blob));

  // round-trip
  Crypto::SecretKey out;
  assert(SwapSecretEncryption::decrypt(blob, key, out));
  assert(std::memcmp(out.data, secret.data, 32) == 0);

  // wrong key fails (MAC)
  Crypto::SecretKey bad;
  assert(!SwapSecretEncryption::decrypt(blob, "wrong key", bad));

  // tampered ciphertext fails (MAC)
  auto tampered = blob; tampered.ciphertext[0] ^= 0x01;
  Crypto::SecretKey bad2;
  assert(!SwapSecretEncryption::decrypt(tampered, key, bad2));

  // determinism of KDF independent of platform: two encrypts decrypt with same key
  SwapSecretEncryption::EncryptedSecret blob2;
  assert(SwapSecretEncryption::encrypt(secret, key, blob2));
  Crypto::SecretKey out2;
  assert(SwapSecretEncryption::decrypt(blob2, key, out2));
  assert(std::memcmp(out2.data, secret.data, 32) == 0);

  std::cout << "test_swap_secret_encryption PASS\n";
  return 0;
}
```

- [ ] **Step 2: Run to verify it fails** — build the swap tests target; expect FAIL (MAC field absent / wrong-key currently "succeeds").

- [ ] **Step 3: Rewrite the header** — `SwapSecretEncryption.h`: extend `EncryptedSecret` to `{ array<uint8_t,8> nonce; array<uint8_t,16> salt; vector<uint8_t> ciphertext; array<uint8_t,32> tag; }`. Rename the misleading `CHACHA20_*` constants to `CHACHA8_*` (M1). Keep the static `encrypt`/`decrypt` signatures.

- [ ] **Step 4: Rewrite the impl** — `SwapSecretEncryption.cpp`:
  - **KDF (replaces homebrew XOR, H2):** derive a 64-byte block from `encryptionKey || salt` using the in-tree memory-hard KDF. Use `Crypto::cn_context ctx; Crypto::generate_chacha8_key(ctx, encryptionKey + saltStr, cipherKey);` for the 32-byte cipher key, and derive a separate 32-byte `macKey = cn_fast_hash(cipherKey || salt || "swap-mac")`. Generate `salt` randomly per-encrypt.
  - **Encrypt:** random 8-byte nonce; `chacha8(plaintext, 32, cipherKey, nonce, ciphertext)`; `tag = cn_fast_hash(macKey || nonce || salt || ciphertext)` (M1 — authenticated).
  - **Decrypt:** recompute `tag'` and compare in constant time; **return false on mismatch** (reject wrong key / tamper); only then decrypt.
  - **Zeroize (L1):** `memwipe` cipherKey, macKey, and any derived buffers before return (add a small `secureZero` helper that the compiler won't elide — `volatile` write loop or `sodium_memzero` if available).

- [ ] **Step 5: Run to verify it passes** — build + run `test_swap_secret_encryption`; expect PASS.

- [ ] **Step 6: Commit**
```bash
git add src/SwapDaemon/SwapSecretEncryption.h src/SwapDaemon/SwapSecretEncryption.cpp src/SwapDaemon/tests/test_swap_secret_encryption.cpp
git commit -m "fix(swap): real KDF + authenticated encryption for escrow secret at rest"
```

### Task A2: Stop persisting the key; new on-disk format; migration

**Files:**
- Modify: `src/SwapDaemon/SwapStateMachine.cpp` (`serialize`/`deserialize`)
- Test: `src/SwapDaemon/tests/test_swapsm_secret_roundtrip.cpp`

- [ ] **Step 1: Write the failing test** — construct a `SwapStateMachine` with a known `adaptorSecret`, `setEncryptionKey("k")`, `serialize()`; assert the JSON string contains **neither** the raw secret hex **nor** an `encKey` field. Then `deserialize()`, `setEncryptionKey("k")` is applied by the loader, assert the secret round-trips. Add a legacy-record case: a v1 JSON with a 64-hex plaintext `adaptorSecret` and no `encKey` → loads, secret recovered (migration), and a subsequent `serialize()` no longer contains the plaintext.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement** —
  - In `serialize()`: drop the `encKey` insert (H1) at [SwapStateMachine.cpp:246-248](src/SwapDaemon/SwapStateMachine.cpp:246). Always encrypt when `hasEncryptionKey()`; store `nonce||salt||ciphertext||tag` as hex under `adaptorSecretEnc` and bump `serVersion` to 3. Do **not** write plaintext fallback (remove the [line 213-215](src/SwapDaemon/SwapStateMachine.cpp:213) fallback; if encryption fails, fail the save).
  - In `deserialize()`: if `adaptorSecretEnc` present → decrypt with the key set by the loader (Task A3). Else if legacy `adaptorSecret` present → treat as plaintext (64 hex) and load (migration path; no `encKey` dependency). Drop the old 88-hex + `encKey` branch ([lines 290-300](src/SwapDaemon/SwapStateMachine.cpp:290)).

- [ ] **Step 4: Run to verify it passes.**

- [ ] **Step 5: Commit**
```bash
git add src/SwapDaemon/SwapStateMachine.cpp src/SwapDaemon/tests/test_swapsm_secret_roundtrip.cpp
git commit -m "fix(swap): remove key-with-ciphertext storage; v3 encrypted secret + legacy migration"
```

### Task A3: Wire a non-persisted key through the DB load/save path

**Files:**
- Modify: `src/SwapDaemon/SwapDatabase.h` (hold key; setter)
- Modify: `src/SwapDaemon/SwapDatabase.cpp` (`saveSwapLocked`/`loadSwapLocked`)
- Modify: `src/SwapDaemon/SwapDaemon.cpp` (derive key at startup)

- [ ] **Step 1:** Add `void setEncryptionKey(std::string)` + `std::string m_encKey` to `SwapDatabase`. In `saveSwapLocked`, call `const_cast<SwapStateMachine&>(sm).setEncryptionKey(m_encKey)` before `serialize()`. In `loadSwapLocked`, call `sm.setEncryptionKey(m_encKey)` immediately after `deserialize()` so the secret can be decrypted on access (and re-saved in new format).
- [ ] **Step 2:** In `SwapDaemon` startup, obtain the key once and never persist it: derive from the wallet spend secret fetched via the authenticated wallet RPC (preferred), or accept `--swap-enc-key`/keyfile. Pass it to `m_db.setEncryptionKey(...)`. Hold only in memory.
- [ ] **Step 3:** Confirm `setEncryptionKey` is now reachable on every save/load (fixes C1 — previously never called). Add an assert/log if the key is empty at save time for an active (non-terminal) swap.
- [ ] **Step 4: Build** the daemon; run A1+A2 tests; expect PASS.
- [ ] **Step 5: Commit**
```bash
git add src/SwapDaemon/SwapDatabase.h src/SwapDaemon/SwapDatabase.cpp src/SwapDaemon/SwapDaemon.cpp
git commit -m "fix(swap): derive escrow enc key at startup, apply on every DB save/load (never persisted)"
```

### Task A4: Restrict DB file permissions (L4)

**Files:** Modify `src/SwapDaemon/SwapDatabase.cpp`

- [ ] **Step 1:** After creating the swaps dir, `mkdir`/`chmod` it to `0700`. In `saveSwapLocked`, after writing the temp file and before `std::rename`, `chmod(tmpPath.c_str(), S_IRUSR|S_IWUSR)` (0600). (`std::ofstream` can't set mode; `chmod` the path right after open, or use `::open(...,O_CREAT|O_EXCL,0600)`.)
- [ ] **Step 2: Manual check:** run the daemon, create a swap, `ls -l <dataDir>/swaps/*.json` → mode `-rw-------`; dir `drwx------`.
- [ ] **Step 3: Commit**
```bash
git add src/SwapDaemon/SwapDatabase.cpp
git commit -m "fix(swap): chmod swap DB files 0600 and swaps dir 0700"
```

---

## Chunk B: Cross-chain timelock ordering (H3)

Enforce, in wall-clock time, that the XFG refund window outlasts the counterparty timeout by a safety margin — for every pair, mandatorily.

### Task B1: Wall-clock timelock comparator

**Files:**
- Create: `src/SwapDaemon/SwapTimelock.h`, `src/SwapDaemon/SwapTimelock.cpp`
- Test: `src/SwapDaemon/tests/test_swap_timelock.cpp`

- [ ] **Step 1: Write the failing test**
```cpp
#include "SwapDaemon/SwapTimelock.h"
#include <cassert>
using namespace XfgSwap;
int main() {
  // XFG 480s/block. xfgTimeout = now-ish + 180 blocks = ~24h.
  // SOL slot ~0.4s. A SOL slot delta that is numerically HUGE but tiny in wall-clock:
  // ctr delta 200000 slots * 0.4s = 80000s (~22h). Margin 1h.
  // Old raw check (xfgHeight <= ctrSlot) would REJECT (height ~1e6 <= slot ~2.5e8).
  // Wall-clock: xfg 24h vs ctr 22h+1h margin=23h -> 24h >= 23h -> ACCEPT.
  assert(timelockOrderingOk(SwapPair::SOL,
      /*xfgCurrentH*/1000000, /*xfgTimeoutH*/1000180,
      /*ctrCurrent*/250000000, /*ctrTimeout*/250200000, /*marginSec*/3600));
  // Unsafe: ctr outlasts xfg in wall-clock -> REJECT
  assert(!timelockOrderingOk(SwapPair::ETH,
      1000000, 1000180,            // xfg ~24h
      19000000, 19000000 + 9000,   // ETH 9000 blocks * 12s = 30h
      3600));
  return 0;
}
```
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement** — `SwapTimelock.cpp`: a `secondsPerBlock(SwapPair)` table — XFG 480, SOL ≈ 0.4 (use ms internally to avoid 0), ETH 12, XMR 120, BCH 600. `timelockOrderingOk(pair, xfgCurH, xfgTimeoutH, ctrCur, ctrTimeout, marginSec)` computes `xfgDeadlineSec = (xfgTimeoutH - xfgCurH) * 480` and `ctrDeadlineSec = (ctrTimeout - ctrCur) * secondsPerBlock(pair)`, returns `xfgDeadlineSec >= ctrDeadlineSec + marginSec`. Guard against underflow when timeout <= current.
- [ ] **Step 4: Run to verify it passes.**
- [ ] **Step 5: Commit**
```bash
git add src/SwapDaemon/SwapTimelock.* src/SwapDaemon/tests/test_swap_timelock.cpp
git commit -m "feat(swap): wall-clock cross-chain timelock comparator with per-pair block times"
```

### Task B2: Make the check mandatory in the swap flow

**Files:** Modify `src/SwapDaemon/SwapDaemon.cpp` ([~329-342](src/SwapDaemon/SwapDaemon.cpp:329) and [~431-436](src/SwapDaemon/SwapDaemon.cpp:431))

- [ ] **Step 1:** Replace both raw `xfgTimeoutHeight <= ctrTimeoutBlock` comparisons with `timelockOrderingOk(params.pair, currentHeight, params.xfgTimeoutHeight, ctrCurrentHeight, params.ctrTimeoutBlock, SAFETY_MARGIN_SEC)`. Fetch `ctrCurrentHeight` from the relevant `IChainClient` (current block/slot).
- [ ] **Step 2:** Remove the `ctrTimeoutBlock != 0` precondition that currently skips the check; when `ctrTimeoutBlock==0`, derive it from the configured counterparty timeout before validating (H3 second half). No escrow funding without a passing check.
- [ ] **Step 3: Build**; run B1 test; expect PASS. Add a focused daemon-level test or manual log check that a mis-ordered SOL/ETH swap is now rejected.
- [ ] **Step 4: Commit**
```bash
git add src/SwapDaemon/SwapDaemon.cpp
git commit -m "fix(swap): enforce mandatory wall-clock timelock ordering before escrow funding"
```

---

## Chunk C: P2P hardening (M2, M3, M4, M5)

### Task C1: Authenticate peer messages (M2)

**Files:**
- Modify: `src/SwapDaemon/SwapPeerProtocol.{h,cpp}` (add `Crypto::Signature signature` to `PeerMessage`; sign/verify helpers over a canonical digest of `type||swapId||payload`)
- Modify: `src/SwapDaemon/SwapDaemon.cpp` `handlePeerMessage` ([1268-1340](src/SwapDaemon/SwapDaemon.cpp:1268))
- Test: `src/SwapDaemon/tests/test_peer_msg_auth.cpp`

- [ ] **Step 1: Write the failing test** — build a `PeerMessage`, sign with a sender keypair, verify true; flip a payload byte → verify false; verify with a different pubkey → false.
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement** — add `signPeerMessage(msg, secretKey)` and `verifyPeerMessage(msg, pubKey)` using `Crypto::generate_signature`/`check_signature` over `cn_fast_hash(type||swapId||payload)`. In `handlePeerMessage`: for `KEY_EXCHANGE`, bind the accepted key to the expected peer identity from the offer/handshake (not first-come); for all other types, `verifyPeerMessage(msg, params.peerSwapPubKey)` and reject on failure. Keep existing DLEQ/partial-sig checks.
- [ ] **Step 4: Run to verify it passes.**
- [ ] **Step 5: Commit**
```bash
git add src/SwapDaemon/SwapPeerProtocol.* src/SwapDaemon/SwapDaemon.cpp src/SwapDaemon/tests/test_peer_msg_auth.cpp
git commit -m "fix(swap/p2p): require per-message signatures bound to peer key"
```

### Task C2: DoS resistance — worker accept + bounded queue (M3, M4)

**Files:** Modify `src/SwapDaemon/SwapP2P.cpp` ([acceptLoop 126-177](src/SwapDaemon/SwapP2P.cpp:126), [queue push 166-170](src/SwapDaemon/SwapP2P.cpp:166))

- [ ] **Step 1:** Handle each accepted socket on a worker thread (bounded pool or detached with a `std::atomic` concurrency cap, e.g. 64) so one idle peer can't stall the accept loop for 30s. Raise `listen()` backlog from 5 to e.g. 64.
- [ ] **Step 2:** Bound `m_pendingMessages` (e.g. cap 1024; drop oldest on overflow) and erase messages already delivered to the callback so the queue can't grow unbounded.
- [ ] **Step 3: Manual check:** open 100 idle connections (`for i in $(seq 100); do nc -q0 127.0.0.1 <port> </dev/null & done`) and confirm legitimate messages still process; flood junk and confirm RSS stays bounded.
- [ ] **Step 4: Commit**
```bash
git add src/SwapDaemon/SwapP2P.cpp
git commit -m "fix(swap/p2p): worker-thread accept, larger backlog, bounded pending queue"
```

### Task C3: Scope the listener bind (M5)

**Files:** Modify `src/SwapDaemon/SwapP2P.cpp` ([78](src/SwapDaemon/SwapP2P.cpp:78)), `src/SwapDaemon/SwapP2P.h`, `src/SwapDaemon/main.cpp`

- [ ] **Step 1:** Add a bind-address parameter (default `127.0.0.1`); replace `INADDR_ANY` with `inet_pton` of the configured address. Add `--swap-p2p-bind` to `main.cpp` for users who intentionally expose it; document the exposure.
- [ ] **Step 2: Manual check:** `ss -ltn` shows the port bound to `127.0.0.1` by default, not `0.0.0.0`.
- [ ] **Step 3: Commit**
```bash
git add src/SwapDaemon/SwapP2P.* src/SwapDaemon/main.cpp
git commit -m "fix(swap/p2p): bind loopback by default, opt-in public bind"
```

---

## Chunk D: Reserve-proof signature verification (M6)

**Cross-reference — do not duplicate.** Implemented in the AFK feature plan `/Users/aejt/.claude/plans/create-a-dev-plan-prancy-moonbeam.md` **Chunk 2** (BCH `verifymessage`, ETH ecrecover, SOL ed25519). Until that lands, ETH/SOL/BCH griefing protection is bypassable by naming any rich address. If executing this security plan first, pull Chunk 2 forward from that plan.

---

## Chunk E: Defensive hardening (L2, L3)

### Task E1: Reject identity / small-order points (L2)

**Files:** Modify `src/crypto/adaptor.cpp`, `src/crypto/dleq.cpp`; Test `src/SwapDaemon/tests/test_point_validation.cpp`

- [ ] **Step 1: Write the failing test** — feed the identity point (encoded) and a known small-order point as `adaptor_point` to `check_adaptor_signature` and as `A`/`B`/`P` to `check_dleq_proof`; assert all return false.
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement** — add a `point_is_valid` helper (after `ge_frombytes_vartime`, reject identity and small-order: multiply by the group cofactor `8` and check non-identity, or compare against the 8 known small-order encodings). Apply in `generate_adaptor_signature`/`check_adaptor_signature` for `T`, and in `check_dleq_proof` for `A`,`B`,`P`.
- [ ] **Step 4: Run to verify it passes.**
- [ ] **Step 5: Commit**
```bash
git add src/crypto/adaptor.cpp src/crypto/dleq.cpp src/SwapDaemon/tests/test_point_validation.cpp
git commit -m "hardening(crypto): reject identity/small-order points in adaptor + DLEQ"
```

### Task E2: Tighten oracle manipulation bounds (L3)

**Files:** Modify `src/SwapDaemon/PriceOracle.cpp` ([floorThreshold 51](src/SwapDaemon/PriceOracle.cpp:51), `TWAP_MIN_TRADES`)

- [ ] **Step 1:** Raise the accept band (e.g. `m_floorThreshold = 0.8` and an upper cap), raise `TWAP_MIN_TRADES`/window so TWAP supersedes seed only with more history, and add a per-epoch max-move cap. Keep changes config-driven where the codebase already exposes config.
- [ ] **Step 2:** Add/extend a `PriceOracle` test asserting out-of-band prices are rejected and that a single large self-swap can't move the accepted rate beyond the cap.
- [ ] **Step 3: Commit**
```bash
git add src/SwapDaemon/PriceOracle.cpp src/SwapDaemon/tests/*
git commit -m "hardening(swap): tighter oracle price bands + per-epoch move cap"
```

---

## Verification (end-to-end)

1. **Chunk A:** Create a swap; inspect `<dataDir>/swaps/<id>.json` — contains `adaptorSecretEnc` only, **no** raw secret hex and **no** `encKey`; file mode `0600`. Wrong-key/tampered decrypt rejected (A1 test). Restart daemon → secret recovers and in-flight swap resumes. Drop in a legacy plaintext record → loads, then re-saves encrypted.
2. **Chunk B:** Attempt SOL and ETH swaps with a counterparty timeout that outlasts the XFG window in wall-clock → rejected; correctly-ordered → accepted. Confirm `ctrTimeoutBlock==0` no longer bypasses the check.
3. **Chunk C:** Unsigned/forged peer message → rejected. 100 idle connections → messaging unaffected. Junk flood → bounded RSS. `ss -ltn` shows loopback bind.
4. **Chunk E:** Identity/small-order point inputs rejected; out-of-band oracle prices rejected.

## Notes / sequencing

- Land **A** and **B** before resuming AFK feature work — both are fund-loss-class.
- **A1→A2→A3** are ordered (format depends on cipher; wiring depends on format). A4 is independent.
- Chunk D is owned by the AFK plan; only pull forward if this plan executes first.
- No on-chain format changes here — escrow stays a plain `KeyOutput`; privacy posture unchanged.
