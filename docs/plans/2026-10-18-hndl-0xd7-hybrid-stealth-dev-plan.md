# 0xD7 Hybrid Stealth Addressing — Implementation Dev Plan

Date: 2026-10-18
Status: PROPOSED — awaiting owner approval. Nothing in this plan is implemented today.
Design basis: [`HNDL_PHASE1_DEV_GUIDE.md`](HNDL_PHASE1_DEV_GUIDE.md) (design spec). This document is the build plan: files, order, tests, acceptance.
Direction context: CryptoNote architecture stays (rings, key images, one-time addresses, UTXO accounting). This is a primitive swap on the HNDL surface only. Spend signatures (MLSAG) and commitments are out of scope.

## 0. What "done" means

New `fire_pq` addresses receive outputs whose on-chain data is useless to a harvest-now-decrypt-later adversary: recovering the ephemeral ECDH scalar via Shor's yields only D_ec, and the output keys/amount masks derive from D_hybrid = KDF(D_ec ‖ ss_kem), where ss_kem is a lattice secret. Legacy `fire...` addresses keep working, fully exposed. No hard fork.

## 1. Primitive: ML-KEM-768 (FIPS 203)

- Vendor PQClean `ml_kem-768` (clean C, no deps, CC0/MIT) under `src/crypto/pq/mlkem768/` rather than pulling liboqs (avoid the dependency weight).
- New wrapper `src/crypto/pq_kem.{h,cpp}`:
  - `pq_keypair_seed(seed64) -> (pk[1184], sk[2400])` — seeded, deterministic keygen (FIPS 203 §: d/z from seed) so wallet restore/rescan reproduces keys.
  - `pq_encaps(pk) -> (ct[1088], ss[32])`, `pq_decaps(sk, ct) -> ss[32]` (implicit rejection handled silently).
  - `hybrid_derivation(D_ec, ss) -> D_hybrid` = SHA3-256(D_ec ‖ ss_kem ‖ "FUEGO_PQ_HYBRID_STEALTH_V1").
  - `view_tag(D_hybrid, out_idx) -> uint8` for fast scan reject.
- SHA3-256 from the PQClean/KAT-adjacent dependency (not cn_fast_hash) so the KDF matches the standard, reviewable construction.
- KATs: NIST ACVP ML-KEM-768 known-answer vectors in `tests/UnitTests/PqKemTest.cpp`.

## 2. Wire format: TX_EXTRA_PQ_KEM (0xD7)

`src/CryptoNoteCore/TransactionExtra.{h,cpp}`:

- `#define TX_EXTRA_PQ_KEM 0xD7` (0xD5/0xD6 in use, 0xF0 next band; 0xD7 is free).
- `struct TransactionExtraPqKem { std::vector<uint8_t> ciphertexts; }` — ONE 0xD7 field per tx, holding k concatenated 1,088-byte ciphertexts in output order (output i of a PQ target uses ciphertext i). Rationale: per-output encapsulation (each output's D_hybrid is independent), single field for parse simplicity.
- Serialization + `parseTransactionExtra` case. New nodes soft-validate: length must be a positive multiple of 1,088; else ignore the field (never reject — old behavior must be preserved for relay).
- Size budget: `MAX_TX_EXTRA_SIZE = 4096` (CryptoNoteConfig.h:35, enforced in TransactionPool.cpp:174). k ≤ 3 ciphertexts (3,264 B) leaves ~800 B for pubkey tag and existing fields. Decision: do NOT bump the cap; wallets split txs with >3 PQ outputs. Fee accounting automatically charges the extra bytes; no rule change needed.

## 3. Address format: fire_pq

- `AccountPublicAddress` (include/CryptoNote.h:211) gains `Crypto::PqPublicKey kemPublicKey;` (1,184 B) + `bool hasPq` — do not change the existing serialization; new addresses use a new Base58 prefix (new magic bytes → `fire_pq...`).
- `Currency::accountAddressAsString` / `parseAddress` (Currency.cpp:904+): new prefix parses into the extended struct; legacy prefix keeps exactly today's behavior.
- Wallet secrets: `KeysStorage` (src/WalletLegacy/KeysStorage.h) gains the PQ seed (32 B) alongside view/spend secrets, encrypted at rest with the existing wallet cache encryption.
- Subaddresses (`src/crypto/subaddress.{h,cpp}`): subaddress KEM keypair at index (i,j) = `pq_keypair_seed(H("FUEGO_PQ_SEED" ‖ master_pq_seed ‖ i_LE32 ‖ j_LE32))`. Scan stays bounded by the max-used index, same rule as today's subaddress scan.

## 4. Sender path

Files: `src/WalletLegacy/WalletTransactionSender.cpp`, `src/Wallet/WalletGreen.cpp`, `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp`, `src/CryptoNoteCore/Transaction.cpp`.

Per PQ-target output i:
1. Ephemeral (r_i, R_i) as today; R stays in extra unchanged.
2. `pq_encaps(recipient.kemPublicKey)` → (ct_i, ss_i).
3. D_ec_i = generate_key_derivation(r_i, A) (existing crypto.cpp:110 path).
4. D_hybrid_i = hybrid_derivation(D_ec_i, ss_i); output key P_i = Hs(D_hybrid_i ‖ i)·G + B; amount mask from D_hybrid_i (same layout as today's depositSecret derivation at TransactionExtra.cpp, swapping the preimage).
5. Append ct_i to the 0xD7 field in output order.

Legacy outputs in the same tx use D_ec only, unchanged. Mixed txs are valid. Swap-daemon and vault/protocol receive paths (`src/SwapDaemon/SwapDaemon.cpp` and treasury) stay legacy for now — swap partners and cross-chain peers can't parse 0xD7; revisit at v12.

## 5. Recipient scan

Files: `src/Transfers/TransfersConsumer.cpp` (findMyOutputs / createTransfers), `src/Wallet/WalletGreen.cpp`.

- Stage 1 unchanged: classical derivation, view-tag fast path for legacy outputs.
- If extra has 0xD7: for each ct_i, `pq_decaps(a_kem, ct_i)` → ss_i → D_hybrid_i → derive + compare output keys. Bound decaps attempts by max-used subaddress index (master first, then subaddresses in use). Decaps is ~tens of µs each; perf acceptance below.
- View-only wallets: sharing a_kem is required for them to see incoming PQ funds (same trust level as the view key). Document; do not block on it.
- Key image on scan (TransfersConsumer createTransfers) for owner-bound commitment outputs already requires the spend secret; PQ changes only the D input to the existing derivations, not that logic.

## 6. RPC / wallet API

- `transfer` accepts `fire_pq` addresses; wallet picks hybrid automatically when the target address carries a KEM key.
- `getaddress`/address book exposes the PQ pubkey; export/import includes the PQ seed.
- RpcServer changes are mechanical (address parsing + wallet command plumbing); no daemon consensus changes.

## 7. Compatibility rules

- Old nodes: parseTransactionExtra already skips unknown tags — add an explicit test proving a pre-0xD7 binary accepts and wallets-scans a 0xD7 tx (forward compat is the whole soft-fork story).
- No fork: 0xD7 is inert data to consensus. Nothing in check_tx_semantic/checkTransactionInputs reads it.
- Pre-0xD7 outputs remain spendable by today's rules forever (and remain harvestable forever — migration of value into PQ outputs is the only protection, per the HNDL guide).

## 8. Tests & acceptance

Unit (`tests/UnitTests/`):
- `PqKemTest`: FIPS 203 ACVP KATs; seeded-keygen determinism across platforms.
- Hybrid round-trip: sender and recipient derive identical D_hybrid; legacy wallet cannot (D_ec alone ≠ D_hybrid).
- Tampered ct: decaps implicit rejection → no match, no crash.
- Legacy↔PQ cross-send both directions; both spendable.
- Address encode/parse round-trip for fire_pq incl. subaddress (i,j).
- 0xD7 parse: valid, wrong-multiple length (ignored, not rejected), k=1..3, k=4 rejected at wallet build (split rule).
Integration:
- Old binary (pre-0xD7 build) relays, validates, and wallet-scans a 0xD7 tx.
- Full send → scan → spend cycle on testnet for fire_pq master + subaddress.
Perf gates:
- KEM ops: encaps < 100 µs, decaps < 100 µs (commodity x86-64).
- Scan: block with 3 PQ cts adds ≤ 10 ms over baseline scan.
Docs gate (process): CI grep — no shipped doc may claim 0xD7/ML-KEM is "deployed/live on wire" unless `TX_EXTRA_PQ_KEM` exists in src/ (prevents a repeat of the Oct 14 finding).

## 9. Build order & effort

PR-sized workstreams, each independently mergeable behind no flags (wallet-side only):
a. Vendored ML-KEM + wrapper + KAT tests — 1-2 d
b. 0xD7 extra field + parse/serialize + tests — 1-2 d
c. fire_pq address format + seeded keygen + subaddress KEM derivation + storage — 2-3 d
d. Sender path (a+b+c prerequisites) — 2-3 d
e. Recipient scan — 2-3 d
f. RPC/wallet API + docs gate — 2-3 d
g. Testnet rollout + adoption metrics (% outputs carrying 0xD7) — 2-3 d
Total: roughly 2-3 weeks of focused engineering; a-c can proceed in parallel with the KAT harness.

## 10. Open items needing an owner call

- k ≤ 3 PQ outputs per tx (wallet splits beyond that) — confirm acceptable UX vs bumping MAX_TX_EXTRA_SIZE at a future fork.
- Swap receive paths stay legacy at v11; adopting hybrid for SwapDaemon escrow receive outputs — defer to v12 review.
- Whether auditors get a_kem (view-only PQ visibility) or PQ outputs are spend-wallet-only until then.
