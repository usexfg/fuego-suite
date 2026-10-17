# Fuego PQ Migration Plan (draft v0.1 — Sept 27, 2026)

Status: DRAFT for review. Not consensus. Decisions pending: STARK backend, activation model, legacy-pool sunset.

## 1. Threat model

Attacker: a cryptographically relevant quantum computer (CRQC) running Shor's algorithm. Two attack classes:

- **Direct theft (steal-now-later).** The chain stores tx public keys, one-time output keys, and key images forever. Once ECDLP falls, a recorded copy of today's chain is enough to derive spend keys for every unspent legacy output and spend it. This is a funds risk, not a privacy risk, and it cannot be fixed retroactively: every UTXO still in the legacy pool at CRQC time is lost.
- **HNDL (harvest now, decrypt later).** Anything confidential transmitted or stored today under non-PQ protection is recorded and opened later. In Fuego this means: ECDH-derived transaction key material, wallet payload encryption, P2p node traffic, RPC exchanges, any future memo/payment-channel data. Mitigation window: HNDL protections must ship years before CRQC, because the harvest is happening now.

Per NIST guidance, plan against a 10-15 year migration horizon and assume we are already late.

## 2. Agreed direction

1. Privacy model: full unified shielded pool (replaces MLSAG ring signatures). One commitment tree, all value lives in it, spend via zk proof + nullifier.
2. Proof system: STARKs (hash-based, transparent, no trusted setup, post-quantum). No Groth16/PLonK/pairing SNARKs anywhere in the spend path.
3. Addresses: diversified addresses derived by pure hashing from the seed (no scalar-multiplication diversification à la Sapling), with PQ note encryption.
4. Acceptance rule for this plan: nothing enters the new protocol that re-introduces ECDLP, pairings, or RSA/DH-style assumptions in a security-critical role.

## 3. Inventory of quantum-vulnerable call sites (from Sept 2026 audits)

Core CryptoNote (Ed25519):
- Key derivation / ECDH: `src/CryptoNoteCore/Currency.cpp` (miner output derivation, ~:765), `src/CryptoNoteCore/TransactionExtra.cpp`, `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp`, `src/CryptoNoteCore/Transaction.cpp`, `src/CryptoNoteCore/Blockchain.cpp`, `src/WalletLegacy/` (send paths).
- MLSAG ring signatures + key images: `src/CryptoNoteCore/`, `src/crypto/crypto.cpp`.
- CDS 1-of-N OR proofs (tier proofs): `src/crypto/tier_proof.cpp` — Fiat-Shamir on Ed25519.
- Subaddress derivation (B + m*G): `src/crypto/subaddress.cpp` — superseded by diversifiers.
- Wallet key storage/serialization: `src/WalletLegacy/KeysStorage.cpp`.

Swap system (secp256k1):
- Adaptor signatures: `src/crypto/adaptor.cpp`, `src/SwapDaemon/AdaptorSwap.cpp`.
- MuSig2 2-of-2 escrow: `src/crypto/musig2.h`.
- Cross-curve DLEQ: `src/crypto/cross_dleq.h` — already disabled (`kCrossCurveDleqAvailable = false`).
- HTLC path (hash-based) is PQ-safe and becomes the primary fallback.

Infrastructure:
- P2p handshake signatures: `src/P2p/NetNode.cpp:1544` (Ed25519).
- Treasury vault keys / checkpoints: `src/Treasury/VaultKeys.cpp`, checkpoint verification in `src/CryptoNoteCore/` — Ed25519.
- Docs: `docs/aliases/group-aliases-plan.md:631` contains the false claim "quantum-resistant (based on discrete log)" — fix before any implementation work starts there.

## 4. Phase plan

### Phase 0 — Lock the threat model (week 1)
- Review/approve this doc; commit as `docs/PQ-MIGRATION-PLAN.md`.
- Fix the group-aliases false PQ claim.
- Add a CI check: a script that greps src/ for new introductions of ECDH/pairing/RSA primitives outside approved modules (crypto-agility tripwire).
- Acceptance: plan merged, doc claim fixed, tripwire green in CI.

### Phase 1 — HNDL stopgaps on transports (weeks 2-4, parallel with Phase 2)
What we can protect now without consensus changes: everything off-chain.
- Wallet/RPC/P2p payloads: hybrid encryption X25519 + ML-KEM-768 (FIPS 203), KEK-wrapped. Classical half keeps interoperability; PQ half neutralizes HNDL.
- P2p handshake: keep Ed25519 but add optional ML-DSA authentication; prefer hybrid over replacement while the network is classical.
- Classify all egress paths (payment IDs, memos, seed backups, node-to-node) and mark each HNDL-clean or fixed.
- Explicitly document what we cannot fix retroactively: on-chain tx public keys and one-time keys of legacy outputs. The only fix is migrating value into the pool (Phase 4).
- Acceptance: no secret leaves a wallet through a path that is not at least hybrid-PQ; classification table committed.

### Phase 2 — STARK pool core (months 1-3)
- Backend spike: evaluate Rust backends (Winterfell / Circle STARKs, Stwo) vs C++ integration cost; pick one. Circuit-friendly hash: Poseidon2 or Rescue (tree + circuit), BLAKE3 acceptable for outer hashing.
- Note model: note = (asset, amount, diversifier, note_nonce, enc data); commitments in a 4-ary Merkle tree; nullifier = H(spend_auth_secret || note_nonce), uniqueness enforced by the pool's nullifier set.
- Circuits v1: spend circuit (Merkle membership + nullifier uniqueness + value conservation), receive/decrypt not in circuit (done client-side via KEM keys).
- Proof model for Phase 2: per-transaction proofs, verified individually by nodes. Keep the verify-time budget per block agreed up front (target: <10ms per tx on commodity hardware, revisit with data). Aggregation is deliberately deferred to Phase 4 so the base path is stable before recursion is added.
- Deliverable: offline testnet chain accepting pool transactions with a dummy wallet.

### Phase 3 — PQ addressing and wallet (months 2-4, overlaps Phase 2)
- Diversified addresses: seed → BLAKE3/SHA-3 hash chain → diversifier index → (div address tag, note-encryption public key). No scalar multiplication anywhere in derivation. Max-index scanning bound (Monero-style) for sync cost.
- Note encryption: hybrid ML-KEM-768 + X25519 per-note (the classical half is not security-critical once the pool is live; keep until pool activation is certain, then evaluate dropping).
- Spend authority: proof-of-knowledge inside the STARK (hash preimage) + optional ML-DSA or SLH-DSA signature for non-repudiation outside the circuit where needed (e.g. wallet API auth). Default: smallest PQ footprint that works.
- Wallet migration UX: import existing seed → derive PQ identity → transfer funds to pool.
- Deliverable: wallet that generates PQ addresses, sends/receives/scans in bounded time, passes test vectors.

### Phase 4 — Protocol integration and activation (months 4-6)
- New transaction types in `src/CryptoNoteCore/`: pool transfer (shield), pool spend (unshield), pool-to-pool. Consensus rules for nullifier set, tree state, and STARK verification in the block validation path.
- Fee/size accounting: STARK proof size and verify cost priced into fees; batching across the block.
- Aggregation layer (agreed Oct 1): permissionless per-block recursive folding, added in Phase 4 only. Individual wallets keep generating their own spend proofs (witness never leaves the sender's device; miners/aggregators never see secrets). Anyone (miner, pool, node operator, third-party service) may fold the proofs of the txs they include into one recursive STARK at block-build time; full nodes verify the single aggregate at block validation. Do NOT fold in the mempool (tx churn makes pre-folded bundles stale). Keep the individual proofs in the block alongside the aggregate until the aggregation layer has soaked (fallback verification, debugging, DoS-resistance); drop them in a later upgrade.
- Checkpoints/treasury: ML-DSA (or threshold ML-DSA) for checkpoint and vault keys; re-derive VaultKeys with PQ scheme.
- Swaps: HTLC (hash-based, PQ-safe) promoted to primary for cross-chain during transition; PTLC/adaptor privacy deferred pending lattice adaptor signatures or STARK-based swap proofs. Keep `kCrossCurveDleqAvailable = false`.
- Activation: feature-flag on testnet, soak period (target: 3+ months), then mainnet via network-upgrade height. Migration window with a legacy-pool freeze/sunset height parameterized but not scheduled until CRQC forecasts firm up.
- Deliverable: testnet end-to-end (wallet → pool → swap → unshield), upgrade RFC.

### Phase 5 — Assurance (continuous, gate before mainnet)
- Circuit test vectors + fuzzing; third-party audit of the STARK circuits and the nullifier scheme before mainnet activation.
- Crypto-agility: all PQ primitives behind swappable traits (KEM, sig, hash) so FIPS 203/204 parameter changes are one-line.
- Tabletop: simulate "CRQC announced" runbook — legacy pool freeze, comms, wallet action.

## 5. Risks

- STARK proof size/verify cost vs CryptoNote throughput → mitigate with recursion + batching; set the budget early and measure, not assume.
- Consensus complexity → everything behind feature flags, long testnet soak.
- Swap privacy regression (HTLC over PTLC) → accepted for now; lattice adaptor research tracked as follow-up.
- Wallet scanning cost with many diversifiers → max-index bound; re-benchmark at 10k diversifiers.
- Library maturity: PQ libs are young; pin versions, watch for security advisories, keep behind traits.

## 6. This week (starter tasks)

1. Review this plan; merge and commit it.
2. Spike A: minimal spend circuit (Merkle membership + nullifier) proven with the chosen STARK backend, measured for time/size.
3. Spike B: ML-KEM-768 hybrid note-encryption PoC wired into a WalletLegacy branch.
4. Fix `group-aliases-plan.md` false PQ claim.
