# Atomic Swap — Security & Privacy Fix Plan

Companion remediation plan for the audit of the atomic-swap framework (`src/SwapDaemon/`, `src/crypto/adaptor.cpp`, `src/crypto/dleq.cpp`, swap RPC, swap P2P). Findings are referenced by ID (C/H/M/L).

Scope note: this plan is **separate** from the AFK feature plan (`/Users/aejt/.claude/plans/create-a-dev-plan-prancy-moonbeam.md`). M6 (reserve-proof signature verification) overlaps that plan's Chunk 2 — do it once, there.

## Severity Summary

| ID | Sev | Title |
|----|-----|-------|
| C1 | CRITICAL | Escrow secret (`adaptorSecret`) persisted in plaintext; encryption path is dead code |
| H1 | HIGH | Even when enabled, encryption key stored in same record as ciphertext |
| H2 | HIGH | `deriveKey` is a homebrew XOR, not a KDF; most key bytes become public constants |
| H3 | HIGH | Cross-chain timelock-ordering check compares incomparable units / skipped on CLI path |
| M1 | MED | ChaCha**8** used (not ChaCha20 as named); unauthenticated (no MAC) at rest |
| M2 | MED | P2P messages unauthenticated; first-wins KEY_EXCHANGE enables swap griefing |
| M3 | MED | P2P accept loop single-threaded w/ 30s recv timeout → slowloris DoS |
| M4 | MED | P2P `m_pendingMessages` unbounded → memory-exhaustion DoS |
| M5 | MED | P2P binds `INADDR_ANY` → swap port exposed on all interfaces |
| M6 | MED | Reserve-proof signature unverified on ETH/SOL/BCH (see AFK plan Chunk 2) |
| L1 | LOW | No zeroization of secrets in memory (`adaptorSecret`, derived key) |
| L2 | LOW | No non-identity/small-order point checks in DLEQ/adaptor inputs |
| L3 | LOW | TWAP oracle manipulable within 50% floor with ≥5 self-swaps |
| L4 | LOW | Swap DB files written with default umask (not 0600) |

---

## Chunk A: Secret-at-rest (C1, H1, H2, M1, L1, L4)

The escrow-controlling 32-byte `adaptorSecret` must be confidential at rest. Today it is plaintext and the "encryption" is both dead and broken-by-design. Fix the whole storage path coherently rather than patching pieces.

### Task A.1: Decide and wire the key source

**Files:** `src/SwapDaemon/SwapDaemon.cpp`, `src/SwapDaemon/SwapDatabase.cpp`, `src/SwapDaemon/SwapStateMachine.{h,cpp}`

- [ ] Source the encryption key from a secret **not stored in the DB**: derive from the wallet's spend secret key (already available to the daemon via RPC/keys) or a daemon-supplied `--swap-secret-key`/keyfile. The key must never be written into the swap record.
- [ ] Call `setEncryptionKey(...)` on every `SwapStateMachine` before `serialize()` and after `deserialize()` (currently never called → C1). Add it to the DB load/save path in `SwapDatabase` so it cannot be forgotten.
- [ ] Remove the `encKey` field from `serialize()`/`deserialize()` entirely (H1) — [SwapStateMachine.cpp:246-248, 356-357](src/SwapDaemon/SwapStateMachine.cpp:246).

### Task A.2: Replace the homebrew KDF and cipher

**Files:** `src/SwapDaemon/SwapSecretEncryption.{h,cpp}`

- [ ] Replace `deriveKey` (XOR loop, [SwapSecretEncryption.cpp:23-39](src/SwapDaemon/SwapSecretEncryption.cpp:23)) with a real KDF over the key + a per-record random salt. Use an in-tree KDF (e.g. the Keccak/`cn_fast_hash` chain or PBKDF2 if available); never the `i*0x9E+0x47` constant fill (H2).
- [ ] Use **authenticated** encryption: either switch to an AEAD, or append a MAC (`keyed cn_fast_hash`/HMAC over nonce‖ciphertext) and verify on decrypt (M1). Reject on MAC mismatch.
- [ ] If staying with the `chacha` family, call ChaCha20 and rename the misleading `CHACHA20_*`/`chacha8` mismatch so the name matches the rounds (M1). Persist the real nonce length used.
- [ ] Store a random per-record salt alongside nonce+ciphertext (not the key).

### Task A.3: Zeroize and lock down files

**Files:** `src/SwapDaemon/SwapSecretEncryption.cpp`, `src/SwapDaemon/SwapStateMachine.cpp`, `src/SwapDaemon/SwapDatabase.cpp`

- [ ] Zeroize the derived key `std::string` and any plaintext-secret buffers after use (`L1`). Prefer a `memwipe`/`explicit_bzero` helper; avoid leaving the secret in a `std::string` that reallocates.
- [ ] `chmod` the swap DB files to `0600` on create (and the swaps dir to `0700`) — the temp-write/rename path at [SwapDatabase.cpp:53](src/SwapDaemon/SwapDatabase.cpp:53) currently inherits umask (L4). `fchmod` the temp fd before rename.

### Task A.4: Migration + test

- [ ] On load, detect legacy plaintext records and re-encrypt-on-save (one-time migration); never crash on old format.
- [ ] Test (standalone `int main()` per `test_adaptor_roundtrip.cpp`): encrypt→decrypt round-trip; tamper ciphertext byte → decrypt rejects (MAC); confirm the serialized record contains **no** key material (grep the JSON for the key); confirm decrypt fails with the wrong key.
- [ ] Commit: `fix(swap): encrypt escrow secret at rest with real KDF+AEAD, key never persisted`

---

## Chunk B: Timelock safety (H3)

The atomic-swap invariant "XFG refund window must outlast the counterparty timeout (in wall-clock time)" is not correctly enforced: [SwapDaemon.cpp:335,431](src/SwapDaemon/SwapDaemon.cpp:335) compares `xfgTimeoutHeight` (XFG height) against `ctrTimeoutBlock` (raw foreign height/slot), and the check is skipped when `ctrTimeoutBlock==0`.

### Task B.1: Compare in a common time base

**Files:** `src/SwapDaemon/SwapDaemon.cpp`, `src/SwapDaemon/PriceOracle.h` or a new `SwapTimelock.{h,cpp}`

- [ ] Convert both timeouts to **wall-clock seconds** before comparison: `xfg_deadline = now + (xfgTimeoutHeight - currentHeight) * DIFFICULTY_TARGET`; `ctr_deadline = now + (ctrTimeout - ctrCurrentHeight) * <per-chain block time>`. Add a per-`SwapPair` block-time constant table (SOL ~0.4s, ETH ~12s, XMR ~120s, BCH ~600s, XFG 480s).
- [ ] Enforce `xfg_deadline >= ctr_deadline + SAFETY_MARGIN` (e.g. margin ≥ several counterparty confirmations). Reject otherwise.
- [ ] Make the check **mandatory**: when `ctrTimeoutBlock==0`, derive it from the configured swap timeout rather than skipping the guard (H3 second half). No swap should fund escrow without a validated ordering.

### Task B.2: Test

- [ ] Unit test the comparator with per-chain fixtures: a SOL slot value that previously passed the raw `<=` check but is unsafe in wall-clock → now rejected; a correctly-ordered case → accepted.
- [ ] Commit: `fix(swap): enforce cross-chain timelock ordering in wall-clock time`

---

## Chunk C: P2P hardening (M2, M3, M4, M5)

`src/SwapDaemon/SwapP2P.cpp` + the handler at [SwapDaemon.cpp:1268-1340](src/SwapDaemon/SwapDaemon.cpp:1268).

### Task C.1: Per-message authentication (M2)

- [ ] Require every `PeerMessage` to be signed by the swap's established peer key (`peerSwapPubKey`). For `KEY_EXCHANGE`, bind acceptance to the expected peer identity from the offer/handshake rather than first-come-first-served (the current gate only blocks a *second* key, not an unauthenticated first one). Reject messages whose signature doesn't match the bound peer key.
- [ ] Keep the existing DLEQ/partial-sig crypto checks (already fail-secure) as defense in depth.

### Task C.2: DoS resistance (M3, M4)

- [ ] Handle each accepted connection on a worker thread or with non-blocking I/O so one idle/slow peer can't stall the single accept loop for 30s (M3). Cap concurrent inbound connections; raise `listen()` backlog from 5.
- [ ] Bound `m_pendingMessages` (drop oldest / reject when over a cap) and prune messages already delivered to the callback (M4) — [SwapP2P.cpp:166-170](src/SwapDaemon/SwapP2P.cpp:166).

### Task C.3: Bind scope (M5)

- [ ] Default the swap P2P listener to loopback or a configurable bind address instead of `INADDR_ANY` ([SwapP2P.cpp:78](src/SwapDaemon/SwapP2P.cpp:78)); document the exposure if a user opts into a public bind.
- [ ] Commit: `fix(swap/p2p): authenticate peer messages, bound queues, worker accept, scoped bind`

---

## Chunk D: Reserve-proof signature verification (M6)

Covered by the AFK feature plan **Chunk 2** (BCH `verifymessage`, ETH ecrecover, SOL ed25519). Do it there; cross-referenced here for completeness. Until then, griefing protection on ETH/SOL/BCH is bypassable by naming any rich address.

---

## Chunk E: Lower-priority hardening (L2, L3)

### Task E.1: Point validation (L2)

**Files:** `src/crypto/adaptor.cpp`, `src/crypto/dleq.cpp`

- [ ] Reject identity / small-order points for adaptor `T`, and DLEQ `A`/`B`/`P` inputs before use (defense in depth; `extract_adaptor_secret` already rejects a zero secret). Add `ge_is_identity`/cofactor checks after `ge_frombytes_vartime`.

### Task E.2: Oracle manipulation dampening (L3)

**Files:** `src/SwapDaemon/PriceOracle.cpp`

- [ ] Tighten the accept band (currently reject only `< 50%` of TWAP — [PriceOracle.cpp:51](src/SwapDaemon/PriceOracle.cpp:51)) and/or require more trades / longer window before TWAP supersedes seed, to raise the cost of self-swap price manipulation. Consider a per-epoch move cap.
- [ ] Commit: `hardening(swap): point validation + tighter oracle bounds`

---

## Verification (end-to-end)

1. **C1/H1/H2/M1:** Inspect an on-disk swap record — confirm no key material present, ciphertext is authenticated, and decrypt fails under wrong key / tampered byte. Confirm `setEncryptionKey` is exercised on every save/load.
2. **H3:** Attempt a swap whose counterparty timeout outlasts the XFG refund window in wall-clock terms → rejected; a safe ordering → accepted. Verify across all four pairs.
3. **M2-M5:** Forge a peer message without the peer signature → rejected. Open many idle connections → swap messaging keeps flowing. Flood junk messages → memory stays bounded. Confirm default bind is loopback.
4. **L2/L3:** Identity-point inputs rejected; oracle rejects out-of-band prices.

## Notes

- Cross-chain adaptor escrow uses a plain `KeyOutput` to the Musig2 joint key — **no swap-identifying tx_extra tag** — so on-chain privacy for the XFG leg is sound. (The `TX_EXTRA_AMM_SWAP` 0xF0 tag applies only to on-chain AMM pools, a separate subsystem.)
- Offer/cancel relay messages are properly signed (`check_signature` on maker pubkey) — no spoofing issue there.
- RPC is hard-gated on `--rpc-user`/`--rpc-password` (refuses to start empty) — fund-affecting swap RPCs are not anonymously reachable.
