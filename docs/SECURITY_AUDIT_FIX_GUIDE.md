# Fuego Security Audit — Developer Fix Guide

- **Audit scope:** `origin/master` @ `c2a968b9e` (audited 2026-10-08, source-only, no build/test run)
- **Method:** Trail of Bits skill disciplines (static c-review portions, constant-time source scan, zeroize scope, sharp-edges, fp-check triage) + Fuego domain invariants (`cryptonote-privacy-audit`, `atomic-swap-security`)
- **Repo rule:** per `AGENTS.md`, every source fix needs a `CHANGELOG.agent.md` entry (feature name, task list, owner/date/status, sign-off table). Add one entry per finding below as you land it.
- **Do NOT "fix":** §6 lists three prior claims that were FP-rejected against source. Leave that code alone.

Severity scale: HIGH = remotely triggerable crash / chain halt · MEDIUM = state corruption or authenticated file write / fund-freeze · LOW = hardening with limited reachability · INFO = dead code / notes.

**Suggested fix order:** H-1 → M-3 → M-1 → M-2 → L-4 → L-3 → L-2 → L-1 → INFO batch.

## 1. Findings register

| ID | Sev | Title | Location | Status |
|----|-----|-------|----------|--------|
| H-1 | HIGH | Uncaught `boost::bad_get` in `pushTransaction` KeyInput rollback → remote daemon crash | `src/CryptoNoteCore/Blockchain.cpp:5951-5968` (+ validation gap `2546-2661`) | TODO |
| M-1 | MEDIUM | `pushTransaction` return ignored; partial connect state never rolled back | `Blockchain.cpp:3890`, `5969-6158` | TODO |
| M-2 | MEDIUM | `SwapEscrow` key image missing Ed25519 domain check | `Blockchain.cpp:6984-7076` | TODO |
| M-3 | MEDIUM | Path traversal / absolute-path escape in walletd `exportWallet` + `exportWalletKeys` (plaintext key write to arbitrary path) | `src/PaymentGate/WalletService.cpp:727-798` | TODO |
| L-1 | LOW | `TransactionInputUnified` rejected by validation but referenced by push (dead-type hazard) | `Blockchain.cpp:2653-2657`, `5993-5997` | TODO |
| L-2 | LOW | ChaCha8 (reduced-round) for secret-at-rest encryption | `src/SwapDaemon/SwapSecretEncryption.cpp` | TODO |
| L-3 | LOW | Rust `AdaptorSecret` derives `Debug` (preimage logging footgun) | `fuego-swapd-adaptor/src/lib.rs:48` | TODO |
| L-4 | LOW | No working secure-zero primitive (`sodium_memzero` declared, never defined/used) | `src/crypto/crypto-util.h:31` | TODO |
| I-1 | INFO | `oaes_sprintf` / `oaes_key_gen_*` / weak `oaes_get_seed` — all dead/unreachable | `src/crypto/oaes_lib.c:433-529,621-680` | TODO |
| I-2 | INFO | `popen("which tor")` etc. — fixed strings, no injection; PATH-hijack caveat only | `src/FuegoTor/.../Fuegotor.cpp:329-361`, `src/FuegoI2P/.../Fuegoi2p.cpp:320-349` | TODO |
| I-3 | INFO | `PointTimelock.sol` notes: permissionless `refund()`, id-squat, on-chain secret by design | `contracts/point-timelock/src/PointTimelock.sol` | TODO |
| I-4 | INFO | Mempool accepts intra-tx duplicate key images (same root cause as H-1) | `src/CryptoNoteCore/TransactionPool.cpp:335` | fixed by H-1 |

## 2. Fix recipes

### H-1 — Intra-tx duplicate key image crashes daemon in rollback

**Root cause (two parts).**
1. `checkTransactionInputs` (`Blockchain.cpp:2546-2661`) tests every input only against *chain* state (`have_tx_keyimg_as_spent`). A tx carrying the same key image twice (e.g. `[KeyInput(k), SwapEscrow, KeyInput(k)]`, validly signed and balanced) passes validation.
2. At connect, the second `KeyInput(k)` insert fails (`5955`), and the rollback loop (`5960-5963`) erases `inputs[i-1-j]` with an **unconditional** `boost::get<KeyInput>` → `boost::bad_get` on the non-`KeyInput` element → uncaught (no `try/catch` on the connect path) → `std::terminate`. Reachable via P2P block relay or mempool → miner inclusion.

**Patch (do all three).**

(a) Intra-tx duplicate rejection in `checkTransactionInputs`. Add before the input loop (`~2546`):
```cpp
std::set<std::string> seenKeyImages;
auto noteKeyImage = [&](const Crypto::KeyImage& ki) -> bool {
  std::string k(reinterpret_cast<const char*>(&ki), sizeof(ki));
  return seenKeyImages.insert(k).second; // false = intra-tx duplicate
};
```
Then in *each* key-image branch (`KeyInput` ~2560, `CommitmentSpend` ~2615, `CommitmentTransfer` ~2639), after the global spent check passes:
```cpp
if (!noteKeyImage(in_to_key.keyImage)) {
  logger(ERROR, BRIGHT_RED) << "Duplicate key image within transaction " << transactionHash;
  return false;
}
```
Also add the missing global+intra checks to the `MultisignatureInput`/`SwapEscrow` path consistently (multisig is covered by `checkMultisignatureInputsDiff` + `isUsed`; SwapEscrow by usage entry — keep those, just add the intra-tx set for any key-image-carrying type, including `TransactionInputSwapEscrow`).

(b) Type-aware rollback (`5960-5963`). Replace the blind `get<KeyInput>` loop:
```cpp
for (size_t j = 0; j < i; ++j) {
  const auto& prev = transaction.tx.inputs[j];
  if (prev.type() == typeid(KeyInput))
    m_indexManager.spentKeys().erase(::boost::get<KeyInput>(prev).keyImage);
  else if (prev.type() == typeid(TransactionInputSwapEscrow))
    m_indexManager.spentKeys().erase(::boost::get<TransactionInputSwapEscrow>(prev).keyImage);
}
```
(c) Belt-and-braces: wrap the per-tx connect body in `pushTransaction` (or the `pushTransaction` call at `3890`) in `try/catch (const std::exception&)` → log, return `false` (with M-1's propagation, the block is then rejected instead of crashing).

**Tests to add** (`tests/` + `src/SwapDaemon/tests/` style):
- Unit: mixed-type tx with duplicated `KeyInput` image → `checkTransactionInputs` returns `false` (no throw).
- Unit: all-`KeyInput` tx with duplicated image → `false`.
- Integration: connect a block containing such a tx → block rejected (`m_verification_failed`), daemon alive, `spentKeys` unpolluted (re-connect a legitimate tx using the same image succeeds).
- Regression: valid mixed-type block still connects.

**Verify:** `git diff` shows only `Blockchain.cpp` (+ tests); full `CoreTests` green; crafted-block harness exits 0 with rejection logged.

### M-1 — Ignored `pushTransaction` return + no per-tx connect rollback

**Root cause.** Line `3890` discards the `bool`. The failure exits at `5972` (SwapEscrow), `6070`/`6152` (Commitment types), `6004` (vault gate), `6019` (bonus parse) erase only `transactionMap` and return `false`, leaving `spentKeys` inserts, `multisigOutputs[].isUsed = true` (`6029`), escrow `usage.isUsed = true` (`6057`), and vault draws in place — while the block is still accepted.

**Patch.**
```cpp
// line ~3889-3890, mirror the existing invalid-tx path at 3880-3887:
++transactionIndex.transaction;
if (!pushTransaction(block, tx_id, transactionIndex)) {
  logger(INFO, BRIGHT_WHITE) << "Block " << blockHash << " tx failed to connect: " << tx_id;
  bvc.m_verification_failed = true;
  block.transactions.pop_back();
  popTransactions(block, minerTransactionHash);
  return false;
}
```
Then add a per-tx undo inside `pushTransaction` failure exits: record every mutation (spentKeys inserts, `isUsed` flippings, vault spend record indices — `vaultSpendRecord` already exists for the CD path; extend the pattern) and reverse them before each `return false`. Minimum viable: on any failure after the first insert, erase exactly the key images this call inserted (track in a local `vector<KeyImage> insertedHere`) and reset any `isUsed` flags this call set.

**Tests:** connect tx that fails vault gate after partial insert → assert `spentKeys` size unchanged, `isUsed` flags unchanged, block rejected.
**Verify:** `CoreTests` + a reorg test (connect, pop, re-connect) showing identical state.

### M-2 — `SwapEscrow` domain check

**Root cause.** `validateSwapEscrowInput` (`6984-7076`) never applies the `scalarmultKey(keyImage, L) == I` gate the other four spendable types have (`2714`, `2760`, `3031`, `Core.cpp:544`). Currently contained by dual marking (spentKeys + usage entry) — defense-in-depth.

**Patch** (insert after the `mode`/`signatures.size()` checks, ~6993; `scalarmultKey`, `I`, `L` pattern already used in-file):
```cpp
static const Crypto::KeyImage I = { { 0x01, 0x00 /* ... full 32 bytes as at 2712 ... */ } };
static const Crypto::KeyImage L = { { 0xed, 0xd3 /* ... full 32 bytes as at 2713 ... */ } };
if (!(scalarmultKey(input.keyImage, L) == I)) {
  logger(ERROR) << "Swap escrow key image not in valid Ed25519 domain";
  return false;
}
```
(Copy the exact `I`/`L` initializers from `2712-2713`; better: hoist one shared file-static pair and reuse at all four sites.)

**Tests:** escrow input with small-order key image → rejected at validation.
**Verify:** escrow claim/refund happy-path tests still pass.

### M-3 — Path traversal in `exportWallet` / `exportWalletKeys`

**Root cause.** `WalletService.cpp:741-747`, `778-784`: RPC-supplied `fileName` goes unvalidated into `walletPath.parent_path() / fileName`. `../` segments escape the wallet dir at open time; an **absolute** `fileName` discards the base entirely (Boost `operator/` semantics). `exportWalletKeys` writes plaintext secrets, giving authenticated RPC callers an arbitrary-path key-exfil channel.

**Patch.** Add a shared gate (both functions, before path join):
```cpp
static bool isConfinedFileName(const std::string& name) {
  if (name.empty() || name.size() > 255) return false;
  boost::filesystem::path p(name);
  if (p.is_absolute()) return false;
  for (const auto& part : p) {
    if (part == ".." || part == "." ) return false;
    const std::string s = part.string();
    if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos) return false;
  }
  return true;
}
// in exportWallet / exportWalletKeys, after the inited check:
if (!isConfinedFileName(fileName)) {
  logger(Logging::WARNING) << "Export rejected: filename escapes wallet directory";
  return make_error_code(CryptoNote::error::BAD_ADDRESS); // or closest INVALID_PARAMETER code in-tree
}
```
Stronger alternative (if symlinks in wallet dir are a concern): `canonical()` the joined path and enforce prefix-of `canonical(parent_path)`; fall back to the lexical gate when the target doesn't exist yet.

**Tests:** `exportFilename` = `../../../../tmp/x`, `/tmp/x`, `a/../../x`, `""` → all rejected, no file created; `backup-1` → succeeds in wallet dir.
**Verify:** existing `PaymentGate` wallet tests green; manual RPC probe on testnet walletd.

### L-1 — `TransactionInputUnified` dead arm

**Patch (pick one):** either add an explicit `Unified` branch in `checkTransactionInputs` returning `false` with a clear log ("Unified inputs not yet supported"), or delete the push-side reference (`5993-5997`) and the `CryptoNoteSerialization` tag (`0x5`) until the type is designed. Do not leave validation and connect disagreeing.

### L-2 — ChaCha8 → ChaCha20-Poly1305

**Patch path:** `SwapSecretEncryption.{h,cpp}` is the only secret-at-rest cipher (callers: `SwapStateMachine` pack/unpack). Swap the `chacha8` + manual HMAC construction for ChaCha20-Poly1305 (OpenSSL EVP is already linked: `evp.h`/`hmac.h` in use), bumping `CHACHA8_NONCE_SIZE` 8→12 and keeping the `nonce||salt||ciphertext||tag` envelope with a version byte for backward decrypt of old records. If deferred, record risk acceptance (8-round margin, low data volumes per key) in the changelog entry instead.

### L-3 — Rust `Debug` on secret

**Patch** (`fuego-swapd-adaptor/src/lib.rs:48`): remove `Debug` from `AdaptorSecret`'s derive list (keep `Clone, PartialEq, Eq`); if debug output is needed, hand-impl `fmt::Debug` printing `[redacted]`. `grep` for `{:?}` on secret-carrying types to confirm no existing leak.

### L-4 — Real secure-zero primitive

**Patch:** define `sodium_memzero` in `src/crypto/` (volatile-pointer loop, mirroring `SwapSecretEncryption::secureZero`) and route key-material cleanup through it: MuSig2 `sec_nonce` (`musig2.cpp:369`), MLSAG alphas, `secp_adaptor.cpp:223`, adaptor/MuSig2 secrets in `SwapDaemon`, wallet key buffers. Add a compile-guard test (e.g. `-Werror`, O2 build + disassembly spot-check that the wipe survives).

### INFO batch (I-1…I-4)

- **I-1:** delete `oaes_sprintf` (`oaes_lib.c:433`, `oaes_lib.h:204`), `oaes_key_gen*` (`621-680`, header `171-175`); optionally replace the weak `oaes_get_seed` with `generate_random_bytes` if the code is ever revived. No behavior change (all dead).
- **I-2:** optional: replace `popen("which …")` probes with `execvp`-free `PATH` search + `stat(X_OK)` (kills shell + PATH-hijack in one move). No urgency.
- **I-3:** `PointTimelock.sol` — consider `require(msg.sender == c.sender)` on `refund()` (frontrunning hygiene) and a nonce in `contractId` (re-lock UX). Both optional; document decision.
- **I-4:** fixed automatically by H-1(a) (same check runs on the mempool path via `TransactionPool.cpp:335`).

## 3. Per-AGENTS.md changelog entries

For each landed finding, append to root `CHANGELOG.agent.md`:
`finding ID + one-line title`, start date, numbered task list (owner = your agent/model name, date, TODO→IN PROGRESS→DONE), and sign-off row (build compiles, tests pass, tasks done). Group the INFO batch as one entry.

## 4. Final verification checklist (run before declaring done)

- [ ] `cmake` build clean (`-DUSE_VENDORED_SECP256K1=OFF` path per AGENTS.md) + full `ctest`/`CoreTests` green
- [ ] Crafted-block harness: H-1 PoC tx rejected, daemon alive, state unpolluted
- [ ] walletd RPC probe: traversal filenames rejected, legit export works
- [ ] `git status`/`git diff` reviewed; only intended files staged; no secrets committed (hooks block `.env`/wallets/keys — do not bypass)
- [ ] No new compiler warnings in touched files; `sodium_memzero` wipe survives `-O2` (spot-check)
- [ ] CHANGELOG.agent.md entries complete with sign-offs

## 5. Explicitly out of scope for this round (residual risks)

OSPEAD bias · MLSAG/RingCT math · full epoch-mint trace (dual paths `Blockchain.cpp:5428/5478`, testnet gate `5431`) · difficulty/emission arithmetic · `popTransactions` completeness per accumulator · deep-reorg-beyond-history divergence (logged, by design) · wallet-file/mnemonic/PaymentGate auth · Go `swapxfg/app`+tui fee-split logic · Rust↔C++ FFI wiring · vendored deps · any dynamic/fuzz testing. Recommend a round 2 scoped to consensus economics + wallet file handling.
