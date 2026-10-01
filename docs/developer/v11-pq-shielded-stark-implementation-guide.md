# v11 implementation guide: full shielded-note pool with a STARK proof

**Status:** build specification and acceptance gates, 2026-09-29. The [P0 key/accounting guide](v11-p0-commitment-keys-and-asset-accounting-guide.md) is a prerequisite. The [v11 architecture plan](../plans/2026-09-29-v11-pq-shielded-note-pool.md) records the release decision. No current Fuego type or guide is evidence that this pool exists.

## 1. Define the claim precisely

The new transaction family spends notes from **one shared Merkle tree**. On-chain observers see a recent accepted root, nullifiers, new note commitments, encrypted output payloads, the proof, public fee and typed public action data. They do not learn which tree leaves were spent, the private note asset/amount, or the input-to-output mapping for an ordinary internal transfer. There are no ring signatures or decoys in this spend path. Input/output counts, timing, fees, network origin and deliberately public action state remain observable unless separately hidden. The anonymity set is the set of plausible notes at the selected root, not an unlimited or automatic property.

The target is resistance to a future quantum attacker across spend authorization, note commitments, nullifiers, proof verification **and** recipient encryption. This does not protect preexisting curve outputs or external atomic-swap chains, and it cannot erase historical linkage. Public Hearth reserves, spot price and settlement events can reveal action amounts. A claim of private trading needs a private AMM and order-state design, not merely a private note input.

## 2. Freeze cryptography and wire rules before consensus code

Write a normative protocol document and independent reference vectors for:

- chain/network and transaction version separation; canonical byte order, lengths, field-element representation, maximum counts and unknown-version rejection;
- note/owner/nullifier/Merkle/transaction transcript domains, collision and preimage targets under quantum attack, hash-to-field rejection rules and selected hash implementation;
- STARK field and extension, AIR/constraint code hash, soundness parameters, FRI queries/blowup/grinding, Fiat-Shamir transcript, proof parser limits and zero-knowledge randomization;
- address encoding, diversifier/subaddress derivation, independent spend/view keys, a reviewed PQ KEM and authenticated note encryption;
- anchor-root age, note maturity, public action encoding, fee denomination, asset identifiers, note-policy identifiers, tx weight and state-root placement.

The original [STARK construction](https://eprint.iacr.org/2018/046) is a research basis, not a ready Fuego verifier. [Winterfell's README](https://github.com/facebook/winterfell/blob/main/README.md) currently calls its implementation unaudited research software and says it lacks perfect zero knowledge for secret inputs; it is therefore **not** a production default for private notes. A curve SNARK wrapper, curve owner key, or classical signature required to accept a spend breaks the end-to-end PQ claim. Proving size, speed and memory must be measured; no fixed recursive-proof size from the older PQ guide is a release assumption.

### Library choice and release gate

**Recommended engineering base: [Plonky3](https://github.com/Plonky3/Plonky3), using `p3-air` + `p3-uni-stark` + `p3-fri::HidingFriPcs`.** Its [architecture](https://github.com/Plonky3/Plonky3/blob/main/docs/architecture.md) identifies this as the univariate STARK path with a hiding FRI backend and explicit zero-knowledge paths. The default `TwoAdicFriPcs`, `CirclePcs`, STIR PCS and binary PCS do not supply the same hiding property. Plonky3 is a toolkit; it does **not** supply the Fuego note circuit, asset conservation rules, consensus verifier or an audited end-to-end private transaction protocol.

Build the note relation in section 4 as an AIR, use a cryptographic hash-based challenger and Merkle commitment stack, require fresh private randomness for every proof, and bind the exact chain, circuit, proof configuration and public inputs into the transcript. The [Plonky3 caller obligations](https://github.com/Plonky3/Plonky3/blob/main/docs/caller-obligations.md) include transcript order, witness alphabet, parameter and digest-width duties that the library cannot enforce for Fuego. Record a reviewed security calculation for the selected field, extension, FRI queries, blowup, grinding, hash and quantum adversary model; reject test or benchmark parameters in consensus. Freeze one reviewed source revision and byte-level proof format for v11; no floating dependency or proof-version fallback in block validation.

The [2024 Least Authority audit](https://leastauthority.com/wp-content/uploads/2024/11/Updated_071124_Polygon_Plonky3_Final_Audit_Report.pdf) examined older revisions and marked several verifier input, Fiat-Shamir and panic issues only partly resolved. Plonky3 also has [published security advisories](https://github.com/Plonky3/Plonky3/security/advisories), including native FRI verifier and transcript defects. Before v11 activation, review the chosen revision against every applicable advisory, audit the complete hiding FRI composition and Fuego AIR, and fuzz the final native verifier under strict memory, time and proof-size limits. A hiding PCS alone is not proof that the transaction protocol hides all witnesses; the [architecture](https://github.com/Plonky3/Plonky3/blob/main/docs/architecture.md) says this explicitly. Run cross-platform prover/verifier vectors through the actual node boundary.

[RISC Zero](https://github.com/risc0/risc0/blob/main/README.md) is the fallback if implementing and auditing a dedicated AIR proves infeasible: it documents zero knowledge and a native STARK receipt, but its stated default is 98 bits of conjectured security and its stack also offers a Groth16 layer. Fuego would need a reviewed stronger parameter profile, STARK-only consensus verification and measured zkVM costs. [Stwo](https://github.com/starkware-libs/stwo/blob/dev/README.md) may merit reevaluation when its private-witness hiding path and external audits are documented; its current README does not establish those properties for this use. None of these choices replaces the separate PQ spend-key, note-hash and recipient-encryption reviews.

Use [NIST FIPS 203](https://csrc.nist.gov/pubs/fips/203/final) ML-KEM-768 as a candidate **key-delivery** component, not spend authorization. The KEM establishes an encryption key; an audited AEAD protects each note plaintext. Final algorithm, parameter and implementation choices are recorded with independent cryptographic sign-off. Derive spend secret, nullifier key and viewing/KEM seed under separate domains; a view secret must never reconstruct a spend secret.

## 3. Canonical note and transaction model

The following is the relation to implement. `H`, `KDF`, encodings, field and output lengths remain named parameters until the previous gate freezes their exact algorithms. This is not permission to use a dummy hash in consensus.

```text
recipient address: network || diversifier || spend_authority || view_kem_public
spend_authority = H("Fuego/SpendAuth/v1", spend_secret, diversifier)
nullifier_key   = KDF("Fuego/NullifierKey/v1", spend_secret)

note = (version, asset_id, value, spend_authority, rho, policy_id, policy_data)
cm   = H("Fuego/Note/v1", chain_id, Encode(note))
nf   = H("Fuego/Nullifier/v1", chain_id, nullifier_key, leaf_position, rho)

shielded_tx = (version, anchor_root, anchor_height, ordered_nullifiers,
               ordered_note_commitments, ordered_encrypted_outputs,
               public_fee, typed_action, public_action_values, proof)
```

`rho` is fresh high-entropy note randomness; it hides repeated address material and values inside the published `cm`. The sender receives only public address material. A recipient can decrypt and check that note plaintext opens `cm`. If ciphertext is missing or inconsistent, the wallet must flag the payment as unrecoverable, and no wallet may count it as received. If delivery consistency is to be a consensus promise, add and cost a proof of encryption consistency; an AEAD tag alone binds ciphertext to associated data but does not prove its plaintext equals a note opening.

Bind AEAD associated data to chain, version, output position, `cm` and an action/public-data digest without hashing a transaction identifier that itself contains the ciphertext. The proof's public-input digest must bind the **ordered** nullifiers and commitments, all public action data, ciphertext hashes, fee and anchor. Specify whether duplicate commitments may be appended; duplicate nullifiers are always invalid. Public output count remains visible unless padding is specified. Avoid reusable public view tags that identify a recipient across transactions.

## 4. Exact STARK spend statement

For each real input, private witness data is the note opening, spend secret, Merkle leaf index and sibling path, plus required policy secrets. For each real output, the witness includes its note opening. Dummy slots, if used to hide counts, need a proved zero-value/inactive flag and cannot produce a spendable note or nullifier. The public inputs are the chain/protocol version, anchor root and height, nullifiers, new commitments, public fee, action digest and public flow/state commitments.

The AIR or equivalent STARK program must enforce **all** of these checks, with each public field bound into the proof statement:

| Constraint group | Relation and failure case |
| --- | --- |
| Note opening | Recompute each old and new `cm` from canonical note fields; reject a changed asset, value, owner, policy or randomness. |
| Membership | Recompute the accepted Merkle root from each old `cm`, private index bits and sibling path; reject false paths, invalid index depth and roots outside the consensus anchor window. Hide the selected leaf. |
| Owner and nullifier | Recompute spend authority and nullifier key from a private spend secret; match the old note; derive `nf` from that key, leaf position and note randomness. Reject a view key, sender-only derivation, forged `nf`, or a spend key for another note. |
| Value ranges | Constrain each value to the specified unsigned bit width, asset/policy code to its allowed set, and sums with carried limbs wide enough for the maximum inputs and outputs. Never equate values only modulo the STARK field. |
| Per-asset conservation | For each enabled asset, prove `sum(inputs) + authorized_issue + protocol_release = sum(outputs) + authorized_burn + protocol_lock + fee`. Use private asset selectors inside the proof. The public fee is charged to its specified asset; no asset silently substitutes for another. |
| Maturity and policy | Prove `lock_until <= anchor_height` for spendable notes, and exact CD/LP/vault action constraints against authenticated policy state. For interest, use the accepted epoch/rate state and exact rounding. Do not let a transfer erase a lock. |
| Action binding | Bind typed HEAT mint/burn, swap, order, LP, CD, reward, vault, treasury or bridge public values to the appropriate state transition and authority. Reject unrecognized or multiple incompatible action classes. |

The node additionally verifies that the stated anchor exists and is allowed at the current height, each `nf` is unique in the transaction/block and absent from the spent set, the proof verifies for the **exact** allowed circuit/protocol ID, the fee and weight policy pass, and the resulting note append/root transition is correct. These are consensus checks, not wallet conventions. Binding maturity to an accepted anchor height permits a valid proof to remain usable within the root window; CD accrual and any action that needs a later epoch must bind the corresponding authenticated state and claim epoch.

Do not put a visible ML-DSA public key or output identifier into the spend authorization path. Do not treat KEM decapsulation as ownership proof. Reject any proposed circuit that verifies only arithmetic while trusting wallet-provided membership, nullifiers, asset IDs or value totals.

## 5. Prover, verifier and state implementation order

| Step | Code boundary and deliverable | Exit test |
| --- | --- | --- |
| 5.1 Protocol package | New versioned note/transaction types and canonical encoding independent of the existing curve-based `TransactionOutputUnified`/`TransactionInputUnified` in [`CryptoNote.h`](../../include/CryptoNote.h); update [`CryptoNoteSerialization`](../../src/CryptoNoteCore/CryptoNoteSerialization.h) and hashes. | C++/Rust vectors match for every field, empty/maximum lists and rejected noncanonical bytes. Historical transactions deserialize exactly as before. |
| 5.2 Hash and AIR gadgets | Implement selected hash/PRF, range/carry, Merkle membership, owner/nullifier and note-opening constraints. Pin the circuit/program ID and verifier parameters. | Positive witness proves and verifies; every single-field mutation and false witness fails with the production verifier. Inspect ZK trace masking and transcript code. |
| 5.3 Transaction proof | Add multi-input/output private per-asset sums, public fee and typed action commitment. Implement real proofs for XFG and HEAT transfers before policy actions. | Overmint, underflow, cross-asset conversion, wrong fee, duplicated note/nullifier, wrong owner and wrong root all fail at proof or consensus check. |
| 5.4 Consensus state | New note tree, accepted-root history, nullifier index, append journal, block commitment, witness query and snapshot/rebuild logic. Do not repurpose legacy [`CommitmentIndex`](../../src/CryptoNoteCore/CommitmentIndex.h). | Identical root/nullifier state after fresh sync, restart, cache rebuild, rollback, reorg and snapshot restore. |
| 5.5 Admission and connection | Integrate the same final verifier into [`Core.cpp`](../../src/CryptoNoteCore/Core.cpp) mempool and [`Blockchain.cpp`](../../src/CryptoNoteCore/Blockchain.cpp) accepted-block paths, with transaction limits before expensive verification. If using a Rust library through C++, pin the ABI and make panics/errors deterministic rejection. | Two nodes with different platforms accept/reject the same corpus and reach identical roots. Malformed proofs remain within resource bounds. |
| 5.6 Wallets | Implement address/KEM, authenticated note delivery, scan, witness maintenance, local proving, signing-key isolation, spend journal and recovery in suite wallets and Valise Rust SDK/walletd/Flutter. | Suite↔Valise send/receive/spend/rescan; watch-only sees notes but cannot spend; backup/reorg and subaddress tests pass. |
| 5.7 Policy actions | Bind every enabled HEAT, Hearth, LP, CD, vault, treasury, reward, DIGM and swap action to an exact proof and public or committed state delta. | Per-asset supply, fee routing, reserve/liability and undo invariants hold under accepted-block tests. An unsupported action fails closed. |
| 5.8 Legacy bridge | Verify old spend under historical rules, record old key image, issue one exact shielded credit with preserved lock/policy, and forbid replay. | No double credit across two bridge transactions, reorg or old/new rule boundaries. |

The block-state transition should be atomic: check all proofs and nullifiers against a read-only pre-state, compute an ordered delta, then commit nullifiers and note leaves with the block. Revert all of them on disconnect. Specify whether two transactions in one block can anchor to the same pre-block root; if yes, prevent cross-transaction nullifier collisions before applying either. Transaction order, root history and witness RPC responses must be deterministic.

## 6. Economic and privacy boundary for every action

| Action | Proof/state requirement | Disclosure decision before v11 |
| --- | --- | --- |
| XFG reward and ordinary transfers | Authorized block reward, exact public fee, private input/output note amounts, total supply equation. | Reward amount and block timing are public; decide miner payout note policy. |
| HEAT mint/redemption | Prove XFG burn and HEAT issue at the accepted TWAP, supply bounds, rounding and zero launch premium. | If amount/price/reserve delta is public, it can reveal the trade. |
| Hearth AMM and limit orders | Prove reserves, pending deposits, fills, expiry, claim, 1% taker fee, 70/30 CD/maker split and no duplicate settlement. | Existing public pool state reveals swaps. Private trading requires a separate committed-state AMM and price-discovery design. |
| LP, treasury and protocol reserve | Prove ratio-paired LP issuance, share burn/redemption, two-leg ownership, reserve debits and treasury LP manager movements. | Public reserve and share totals may reveal amounts. |
| HEAT CDs and Bonus Vault | Preserve lock terms, backed interest, epoch floor/bonus rules, CD_APY_POOL and BONUS_VAULT liabilities. Include the 0.1% creation burn/treasury credit if activated in v11. | Public epoch and vault changes can narrow note linkage. |
| Atomic swaps, aliases and DIGM | Explicit escrow/refund/claim, authorization, fees, asset/collateral accounting and bridge rules. | Other chains and existing aliases remain visible/classical. DIGM must first pass the P0 four-asset accounting decision. |

An action that cannot meet its proof and accounting rule must be disabled at the v11 gate or routed through an explicitly documented public bridge. The wallet must state the resulting disclosure. A shared tree alone does not make the existing public AMM or cross-chain swap private.

## 7. Wallet recovery, viewing and migration

Use independent spend and ML-KEM view keys from a versioned seed. The sender encapsulates for a recipient's public view key, derives an AEAD key and encrypts the complete note opening needed for scanning; the recipient checks the opening against `cm`. Define outgoing-note recovery without publishing a sender identifier. Backups must restore spend/view keys, diversifiers, note discovery range and any key-image/nullifier history. A view-only export contains decapsulation material but no spend or nullifier secret; it cannot author a proof. Protect decrypted notes and witness caches at rest, and specify recovery when the wallet loses a witness or a note falls outside the accepted anchor window.

The bridge accepts historical KeyOutput and commitment outputs only after the P0 classifier is fixed. Legacy sender/view-key-spendable outputs remain exposed until spent. Exact bridge value, asset, lock and entitlement must be checked against the historical output and current policy. Do not convert a locked CD into liquid notes or count both legacy and shielded supply. Initial bridge transactions can strongly link old and new value even when subsequent internal pool spends are shielded.

## 8. Verification and activation gate

Maintain independent vectors for encoding, hashes, KEM/AEAD, Merkle roots, nullifiers, all STARK constraints and accepted-block state. Required negative cases include: false membership, wrong owner, changed asset/value, duplicate nullifier in one tx or block, stale root, public-input/proof mismatch, action replay, amount overflow, hidden cross-asset mint, early CD spend, vault shortfall, malformed proof and proof-resource exhaustion. Test mempool admission, block acceptance, full replay, restart, snapshot and multi-node reorg with the same corpus. Include suite and Valise wallet interop and key-recovery tests.

Benchmark the final non-recursive proof first on supported phones/desktops and node targets: proving time and peak memory, proof/ciphertext bytes, verification time and peak memory, throughput, disk growth, witness bandwidth and sync/reorg cost. Set measured limits and fees in the protocol spec. Add aggregation only if its final verifier, ZK proof and PQ assumptions pass the same review; never wrap a transparent STARK in an unreviewed classical proof to reach a size target.

**Mainnet gate:** independent cryptography and consensus audits; fixed P0 flaws; exact per-asset accounting; all enabled policy actions; deterministic multi-node replay/reorg; cross-wallet recovery; production resource measurements; and an upgrade/incident runbook. v11 activation height is chosen after these results. If a gate fails, delay activation or explicitly remove that feature and its privacy claim from v11. Source work in this repo must carry the required `CHANGELOG.agent.md` task list and sign-off.
