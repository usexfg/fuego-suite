# v11 post-quantum shielded-note pool: design and delivery plan

**Status:** design proposal, 2026-09-29. No shielded-pool implementation or release is claimed here.

Implementation instructions: [P0 commitment keys and asset accounting](../developer/v11-p0-commitment-keys-and-asset-accounting-guide.md) and [shielded STARK pool](../developer/v11-pq-shielded-stark-implementation-guide.md).

## Decision and release boundary

Build one shared, full shielded-note pool for XFG and HEAT value, with a transparent, zero-knowledge STARK proving note spends and valid value transitions. Within the pool, hide which existing notes were spent, their amounts and asset types, and the linkage between inputs and new notes. Publish note commitments, nullifiers, an accepted pool root, encrypted note payloads, proof data, and the minimum public data needed for fees and protocol actions. Ring signatures, decoys, elliptic-curve Pedersen commitments, and Bulletproofs are not part of the new pool's spend path.

Target this as the **v11 design**, since v11 is not live. The current v11 code already schedules HEAT and Hearth at `UPGRADE_HEIGHT_V11 = 1111111` in [`CryptoNoteConfig.h`](../../src/CryptoNoteConfig.h). Its activation height and feature set must be revised together after the design and release gates below pass. Do not make the existing v12 unified Bulletproof/MLSAG output format the interim default. If the complete shielded protocol cannot pass the gates before the desired v11 launch, delay v11 or make a separately specified safety-only release; do not advertise an incomplete pool as full PQ privacy.

The plan covers all existing value paths that must either transact through the new pool or have an explicit, enforced bridge and disclosure policy. Development can be staged on testnets. Mainnet activation requires a coherent consensus and wallet release, not a transfer-only demonstration.

## Code baseline and urgent exposure

| Area | Current repository evidence | Required treatment |
| --- | --- | --- |
| Commitment-output ownership | [`deriveCommitmentKeys`](../../src/CryptoNoteCore/TransactionExtra.cpp) derives the signing scalar from a view-derived deposit secret. [`TransfersConsumer.cpp`](../../src/Transfers/TransfersConsumer.cpp) scans these outputs; [`WalletTransactionSender.cpp`](../../src/WalletLegacy/WalletTransactionSender.cpp) signs them. The Valise Rust copy is in `fuego-sdk/fuego-crypto/src/ring.rs`, with scanner and builder calls in its SDK. | Treat old commitment outputs as sender/view-key-spendable. Fix new legacy output creation immediately, distinguish derivation versions unambiguously, scan both, and provide a sweep. No wallet update can revoke an old sender's or view-key holder's existing authority. |
| Dormant unified types | [`CryptoNote.h`](../../include/CryptoNote.h) declares curve-based `TransactionOutputUnified` and MLSAG `TransactionInputUnified`; current accepted-block validation does not establish a live shielded-note verifier. | Preserve historical serialization. Define distinct, versioned shielded transaction and note types; do not reuse `Unified` as if it were PQ. |
| Merkle state | [`CommitmentIndex`](../../src/CryptoNoteCore/CommitmentIndex.h) tracks legacy banking commitments and a Merkle root. | Specify and build an independent, consensus-bound note tree, accepted-root history, nullifier set, persistence, and rollback journal. |
| Legacy asset accounting | [`Blockchain.cpp`](../../src/CryptoNoteCore/Blockchain.cpp) derives commitment input asset from a referenced ring member in `classifyInputAsset`; ring selection and term rules need a full invariant review. | Audit cross-asset and term mixing before enabling a bridge. Do not treat an unverified ring member as proof of a spent output's asset. |
| Prior PQ guides | [`PQ_PHASES_2_3_DEV_GUIDE.md`](PQ_PHASES_2_3_DEV_GUIDE.md) asserts deployed KEM support and fixed recursion/proof performance. The source scan for its claimed KEM tag and STARK verifier did not establish implementation. | Retain as historical proposals only. Replace performance claims with measurements and code-backed sign-off. |

The legacy ownership fix is **P0**, independent of the time needed for the STARK pool. A sender who knows `r`, or a holder of the recipient's view secret `a`, can derive the legacy commitment spend key from `rA = aR`. Existing affected outputs remain exposed until spent into correctly owned outputs. Protect both the suite wallets and Valise, including walletd/watch-only deployments.

## Protocol specification to freeze before coding consensus

### 1. Threat model and primitives

Define separately: anonymous spending, value confidentiality, recipient confidentiality, resistance to a future quantum attacker, and availability under hostile proofs. The new pool must contain no curve-dependent ownership or amount commitments, no elliptic-curve proof wrapper, and no visible output-tied spend signature. The final verifier, Fiat-Shamir transcript, Merkle/hash construction, recursion if any, and wallet delivery path must meet the claimed PQ security level together. Existing legacy outputs, chain history, outside chains, and network metadata do not become PQ private by adding the pool.

Write a versioned byte-level protocol specification covering chain/network IDs, canonical field encodings, domain-separated hashes/PRFs, nullifier derivation, leaf ordering, address format, ciphertext binding, output ordering, action types, and unknown-version rejection. Select concrete hash, field, security parameters, KEM, AEAD, and zero-knowledge blinding only after cryptographic review. Do not infer PQ security from output length alone. Include malicious-prover, malicious-sender, compromised view key, stolen device, old wallet, reorg, and future quantum attacker cases.

### 2. Notes, ownership, and delivery

A note has a version, asset, bounded value, recipient spend-authority commitment, fresh note randomness, and any policy state such as lock or CD terms. Commit to the canonical note with a hiding, binding hash construction and publish only its commitment. The sender uses a recipient address containing public view-delivery and spend-address material; it never receives the private spend secret. The spend proof establishes knowledge of the corresponding spend secret without revealing the secret, the selected leaf, or the private asset/value.

Use a reviewed PQ KEM, with ML-KEM-768 as a candidate under [NIST FIPS 203](https://csrc.nist.gov/pubs/fips/203/final), to deliver a symmetric key for authenticated encryption of note plaintext. Bind the ciphertext and output commitment to the network, protocol version, position or output index, and transaction action through associated data without a circular transaction-hash dependency. The receiver must verify that decrypted note data opens the published commitment; specify the response to unavailable or inconsistent ciphertext. View-only keys may decrypt incoming notes and amounts but cannot derive spend witnesses or nullifiers. Specify diversifiers/subaddresses, outgoing-payment recovery, wallet backup and seed migration, view-key rotation, malformed-key handling, and constant-time/entropy requirements. Scanning speed and KEM ciphertext overhead are measured rather than assumed.

### 3. Shielded-spend relation

For each input, the witness contains a note opening, private spend authorization, and a Merkle path to a public accepted root. The proof checks correct leaf commitment and membership, owner authorization, unspent-note nullifier derivation from a secret unavailable to the sender/view wallet, lock or maturity rules, and the relevant policy constraints. The selected leaf index and membership path remain hidden. For each output, it checks a well-formed, fresh note commitment to the intended asset/value/policy. Public inputs bind the chain ID, transaction version, anchor root, ordered nullifiers and output commitments, public fee, action digest, and any intentional public deposits or withdrawals. Every such byte must be covered by the proof statement or an equally strong consensus check.

Prove bounded nonnegative values and exact conservation **per asset**, with checked arithmetic wider than the maximum summed transaction value. Model authorized mint, burn, fee, and protocol state changes as explicit, typed exceptions whose amounts and authority are verified. Reject duplicate nullifiers both within a transaction and against chain/mempool state. Specify constraints for policy notes before claiming CD, LP, or swap support. Do not rely on a wallet-side balance calculation for consensus.

### 4. State and consensus integration

Create a deterministic append-only note tree and nullifier set, with consensus commitments to the resulting state and accepted-anchor rules. Define root history depth, confirmation/maturity, per-block append order, pruning/witness services, snapshot format, and rebuild from genesis. Connect the new verifier to both mempool admission and accepted-block validation, including replay, restart, reorg, and rollback. Check proof length, parsing, field elements, recursion depth, CPU/memory limits, transaction weight, fees, block limits, and disconnect behavior before expensive verification. Make malformed and unknown-version transactions fail closed.

Define one versioned shielded transaction family rather than overloading existing `TransactionInputUnified`/`TransactionOutputUnified`. Keep historical validation of legacy outputs. Prevent the same legacy value from being credited both to old accounting and to the new pool. Publish RPC/wallet witness data without exposing private notes, and make light-wallet trust assumptions explicit.

The proposed public transaction envelope has a version, anchor root, ordered nullifiers, ordered new note commitments and encrypted payloads, public fee, typed action/public-value fields, and a proof. Its exact encoding and proof input digest are frozen together. Input and output counts are observable unless padding is specified; ciphertext and KEM metadata must not expose a reusable recipient identifier.

## Value-path coverage and privacy limits

| Path | Consensus work before claiming v11 support | Observable limit |
| --- | --- | --- |
| XFG transfers and miner rewards | Shielded XFG notes, fee debit, reward issuance with auditable supply accounting, wallet send/receive/change. | Mining payout timing, fees, and public bridge deposits can narrow anonymity. |
| HEAT mint/burn | Bind HEAT issuance/redemption to exact XFG burn, supply caps, authorized Hearth price/TWAP and rounding; prove per-asset conservation across the action. | A public price or settlement amount can leak the action's value. Hiding it requires a private-state design. |
| Hearth AMM, limit orders, LP and pool reserves | Specify state commitments, swap execution, 1% fee and 70/30 routing, LP share issuance/redemption, order expiry/proceeds, treasury LP manager and reserve accounting. Prove all changes against the same accepted public or committed state. | Existing public reserves, spot price, depth, and trade events permit inference. Full private trading requires an additional private-AMM/order design and its own audit. |
| HEAT CDs and vaults | Encode deposit locks, rollover, claimed interest, bonus tiers, 69/11/20 atomic-swap fee routing, 0.1% CD creation burn/treasury credit, vault liabilities, and epoch accounting. Make reward claims single-use and backed. | Public epoch rates and treasury movements remain visible unless their state is redesigned. |
| Atomic swaps, aliases, DIGM and external actions | Map escrow, refund/claim, fee source, alias payment/recovery and any DIGM payout to explicit shielded entry/exit rules. Audit their authority keys. | Other chains' key systems and on-chain swap legs stay classical or public until independently migrated. Fuego's pool alone does not make end-to-end swaps PQ. |

This matrix is an implementation checklist, not a claim that the current code already handles these paths. If a path cannot safely enter the pool at launch, define a consensus-enforced bridge and state exactly which amounts and links it reveals; keep its UI from claiming full shielding. Existing public chain history cannot be hidden retroactively. The anonymity set grows with actual use and can be weakened by timing, unique amounts at the bridge, address reuse, and network observation.

## Migration from legacy outputs

1. Land the spend/view separation fix for **new legacy commitment outputs** in suite and Valise with an explicit derivation marker, cross-implementation vectors, old-output recognition, and a sweep path. Audit special pool-reserve and LP outputs before changing their key ownership; they may be protocol controlled.
2. Create a single-use bridge for old KeyOutput, commitment outputs, and any other supported legacy asset. Verify the legacy spend under its historical rules and create exactly the same net value in new shielded notes after explicit fees/burns. Carry lock, maturity, CD entitlement and pool ownership state across the bridge instead of converting locked value into liquid notes. Track old key images and new nullifiers with separate namespaces. Make bridge accounting survive replay and reorg.
3. Provide wallet recovery for old deposits and both derivation variants, user-visible exposure status, consolidation limits, and safe migration UX. A sweep can be raced by a sender or a compromised view-key holder; never promise guaranteed recovery.
4. Set a documented sunset for creating vulnerable or classical outputs after v11, while retaining whatever historical validation and emergency recovery consensus requires. Test initial-pool privacy and chain analysis around bridge traffic before launch.

## Proving-system selection gate

The existing guide's Winterfell and recursive-size assumptions cannot be release inputs. [Winterfell's own README](https://github.com/facebook/winterfell/blob/main/README.md) describes it as unaudited research software, not ready for production, and says the current implementation does not provide perfect zero knowledge for secret inputs. Select or build a STARK stack only after review of zero knowledge, knowledge soundness, implementation audit status, hash/field parameters, transcript binding, prover randomness, and denial-of-service bounds. Inspect the **final** on-chain verifier: a curve SNARK wrapper or classical signature gate would invalidate an end-to-end PQ claim for that path.

First make a real note-spend circuit with 1 and multiple inputs/outputs, XFG and HEAT, then benchmark proof generation on Valise target phones and desktops, verifier cost on supported nodes, proof/transaction size, sync/reorg cost, and worst-case adversarial proofs. Benchmark Hearth/CD actions too. Derive block limits and fee policy from measured worst cases. Recursion or aggregation is optional only after its verifier and ZK properties pass the same gates; do not assume a fixed 100 KB proof or a throughput improvement.

## Developer sequence and required tests

1. **Inventory and invariant:** trace creation, scanning, signing, serialization, mempool checks and accepted-block checks for every old output and value action. Record a per-asset supply equation that includes rewards, burns, vault movements and bridge deposits. Use [`Blockchain.cpp`](../../src/CryptoNoteCore/Blockchain.cpp), [`CryptoNote.h`](../../include/CryptoNote.h), [`WalletGreen.cpp`](../../src/Wallet/WalletGreen.cpp), [`WalletTransactionSender.cpp`](../../src/WalletLegacy/WalletTransactionSender.cpp), [`TransfersConsumer.cpp`](../../src/Transfers/TransfersConsumer.cpp), and Valise's `fuego-sdk/fuego-sdk/src/scanner.rs` and `transaction_builder.rs` as initial trace points. Resolve the first-ring-member asset classification before bridge work.
2. **Fix legacy ownership:** add sender/receiver-compatible, spend-key-bound derivation for new legacy commitment outputs; place the derivation version in a consensus-safe, unambiguous location; update both C++ wallets and Valise; retain old scanning/spending solely for migration. Test sender-only and view-only inability to spend new outputs, plus legacy detection and sweep with pool-reserve exceptions.
3. **Freeze the new spec:** write versioned byte layout, address/note format, action policy table, public-input digest, state transition, proof relation and test vectors before implementing separate wallets and nodes. Require independent review of the circuit statement itself, including hidden asset accounting and the zero-knowledge transform.
4. **Build consensus substrate:** add the new serialized transaction family, note tree, accepted-root history, nullifier index and undo data. Wire one verifier result into mempool, block, replay and reorg paths. If Rust is used behind C++, define a deterministic, panic-safe ABI with identical verification behavior on every supported platform; make verifier errors consensus failures.
5. **Build wallet substrate:** implement KEM delivery, note scan, witness retrieval/update, proving, spend key isolation, transaction rebroadcast/reorg handling, and backup recovery in the suite and Valise. Produce shared C++/Rust vectors for all note/action versions and malformed inputs.
6. **Implement all enabled policy actions:** add HEAT mint/burn, Hearth, LP, CDs, vaults, treasury, swaps and protocol payouts in dependency order, each with an action-specific proof and supply invariant. Preserve the 10:1 launch setting and current fee splits unless a separate economic decision changes them.
7. **Migrate and launch:** enable the old-to-new bridge, rehearse real chain-state replay and wallet upgrades, commission independent audits, run a multi-node adversarial testnet, measure devices and nodes, then set a future v11 activation height with a rollback/incident procedure.

Minimum adversarial tests: forged owner or Merkle path; duplicate or incorrectly derived nullifier; stale or unknown anchor; incorrect note opening; inconsistent encrypted payload; noncanonical field encoding; negative, overflowed or cross-asset value; unauthorized HEAT mint, reward, CD claim or vault debit; locked-note early spend; malformed proof resource exhaustion; chain-ID/action replay; mempool conflict; block replay/restart/reorg; and old-output double credit through a bridge. Every accepted-block test must compare fresh validation with cache rebuild and rollback state.

## Work packages and acceptance gates

| Priority | Work package / primary repo | Completion evidence |
| --- | --- | --- |
| P0 | Legacy commitment key fix and migration inventory; suite + Valise | Sender and view-only wallets cannot spend **new** owner-bound outputs; old outputs remain detectable; old/new vectors agree in C++ and Rust; special reserve paths audited; sweep race disclosed. |
| P0 | Legacy ring/asset accounting and bridge threat review; suite | Written per-asset supply invariant for every existing input/output/term and adversarial ring tests; no pool activation while cross-asset credit remains possible. |
| P1 | Protocol and cryptography specification; suite docs + independent reviewers | Frozen transaction/state encodings, circuit relation, PQ security claims and exclusions, hash/KEM/AEAD choices, formal conservation rules, concrete attack review. |
| P1 | Prover/verifier selection and circuit; shared Rust library if appropriate | Zero-knowledge and soundness review, deterministic cross-platform vectors, invalid-witness/property tests, no classical dependency in final spend verifier, resource benchmarks. |
| P1 | Consensus state and verifier; suite C++ | Accepted-block and mempool parity; exact replay, restart, reorg, snapshot, multi-node and sync tests; bounded proof parsing and verification. |
| P1 | Wallet stack; suite CLI, Valise Rust SDK/walletd/Flutter | KEM note delivery, spend-only authorization, view-only scanning, backup/rescan/subaddresses, output recovery, witness refresh, failed/reorged transaction handling, C++/Rust interop. |
| P1 | HEAT/Hearth/CD/LP/treasury/swap value integration; suite + Valise | Every supported action enforces per-asset conservation and the current fee/epoch rules, with an explicit privacy statement for public state. Test all failure, refund, expiry and rollover paths. |
| P2 | Migration and release; suite + Valise | Old-to-new bridge cannot duplicate supply; production-like testnet, adversarial audit, wallet rollout order, chain upgrade rehearsal, rollback/incident runbook, release sign-off. |

**Mainnet go/no-go:** no v11 activation until independent cryptographic and consensus audits, all enabled value paths, cross-wallet vectors, full rebuild/reorg tests, multi-node testnet, device benchmarks, and an actionable migration/recovery release are complete. A failed gate means delay or reduce the *explicitly named* release scope and privacy claim. It does not justify silently reverting to the old unified Bulletproof plan.

## Decisions to resolve at spec freeze

1. Do Hearth and limit orders get a private state transition in v11, or a deliberately public boundary with measured leakage? This sets the truthful extent of the privacy claim.
2. Which audited STARK implementation and zero-knowledge transform meet the target soundness, leakage, and device limits? Is recursion worth its extra risk?
3. Which policy positions are user-owned notes versus protocol-owned state (pool reserves, LP shares, vault funds, treasury, coinbase)? This determines spend authorization and supply accounting.
4. What are the exact anchor window, witness service, note tree shape, output padding policy, ciphertext layout, fee visibility, and bridge sunset?
5. What network height can activate v11 after the gates pass? The current constant is a placeholder for this plan, not a release commitment.

## Verification status and references

This document is a source-grounded plan. No source code, tests, audits, benchmarks, or network upgrades were completed by writing it. Relevant external primary sources are [NIST FIPS 203](https://csrc.nist.gov/pubs/fips/203/final), the [original STARK paper](https://eprint.iacr.org/2018/046), and the [Winterfell project status](https://github.com/facebook/winterfell/blob/main/README.md).
