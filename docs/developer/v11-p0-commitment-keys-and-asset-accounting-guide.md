# v11 P0 implementation guide: commitment ownership and asset accounting

**Status:** implementation instructions based on the 2026-09-29 checkout. Findings below distinguish observed code behavior from exploit claims that still need an adversarial transaction. See the [v11 pool plan](../plans/2026-09-29-v11-pq-shielded-note-pool.md) and the [STARK implementation guide](v11-pq-shielded-stark-implementation-guide.md). v11 is not live; historical validation must remain deterministic.

## Required security invariants

1. For a newly created **user-owned** commitment output, knowledge of the sender's transaction secret `r`, the recipient's view secret `a`, or both never supplies its spend scalar. Spending requires the recipient's spend secret `b` (the matching subaddress secret for a subaddress). A view-only wallet can detect and read the output but cannot sign or derive its key image.
2. Legacy commitment outputs retain their original derivation and risk. The wallet identifies their derivation by matching the actual output key, marks them exposed, and supports spending them into owner-bound outputs. A migration cannot revoke a sender's or view-key holder's existing ability to race the owner.
3. Every accepted transaction conserves each asset and each protocol-held balance under a single checked equation. A ring cannot change the asset assigned to its true spend by changing decoy order. A transaction must never get an XFG credit from a HEAT or LP input, or vice versa.
4. The validator, state connection, disconnection, mempool, fresh replay and cache rebuild agree on asset classification, fee, mint/burn, vault, pool and locked principal deltas. Pre-v11 blocks retain their historical rules.

## A. Commitment-key flaw: evidence and construction

[`TransactionExtra.cpp`](../../src/CryptoNoteCore/TransactionExtra.cpp) currently computes `keyScalar = Hs("fuego_commit_key" || depositSecret)` and `commitKey = keyScalar*G`. The sender and recipient derive `depositSecret = H(D || outputIndex_LE32)` from `D = rA = aR`; see [`WalletTransactionSender.cpp`](../../src/WalletLegacy/WalletTransactionSender.cpp) and [`TransfersConsumer.cpp`](../../src/Transfers/TransfersConsumer.cpp). Thus the sender and anyone with `a` can obtain the old signing scalar. [Valise's Rust crypto copy](/Users/aejt/DEXFG/fuego-flutter-wallet/rust-fuego-wallet/fuego-sdk/fuego-crypto/src/ring.rs) reproduces it, and its [scanner](/Users/aejt/DEXFG/fuego-flutter-wallet/rust-fuego-wallet/fuego-sdk/fuego-sdk/src/scanner.rs) stores that scalar.

### A1. Owner-bound derivation for new user outputs

Use the **existing matching CryptoNote public/secret derivation pair** in [`crypto.cpp`](../../src/crypto/crypto.cpp), with the absolute transaction output index `i` and recipient spend public key `B = bG`:

```text
D_sender   = generate_key_derivation(A_recipient, r)
D_receiver = generate_key_derivation(R, a_recipient)       // R = rG
t          = derivation_to_scalar(D, i)                    // existing varint-index rule
P          = derive_public_key(D, i, B) = B + tG           // sender, scanner
x          = derive_secret_key(D, i, b) = b + t            // recipient signer only
I          = generate_key_image(P, x)                      // recipient signer only
depositSecret = cn_fast_hash(D || i_LE32)                  // retain for amount mask
amountMask    = Hs("fuego_amount_mask" || depositSecret)  // unchanged
```

Use the exact existing `derivation_to_scalar` encoding, which is `D || CryptoNote-varint(i)`, **not** the old `depositSecret` hash or fixed LE32 index. The two derivations serve different purposes. On a subaddress use `B_sub = B + mG` as the recipient spend public key and `b_sub = b + m` as its spend secret; Fuego's [`subaddress.cpp`](../../src/crypto/subaddress.cpp) keeps the master view key for the view derivation. Require `xG == P` and a valid key image before signing. Check public-point validity and scalar canonicality at every boundary. No sender-side API for the owner-bound path should return `x` or `I`.

Keep the old derivation under an explicit name such as `deriveLegacyCommitmentKeys` for historical rescans and sweeps. Add separately named functions for owner-bound public creation, view-only recognition, and spend-key-only signing. Do not silently replace every `deriveCommitmentKeys` call: pool reserve outputs, vault operations, throwaway burns, and dead code have different authority rules. Inventory each reachable call site before modifying it.

### A2. Scanner, signing and wallet storage

For each `TransactionOutputCommitment`, derive `D` and `i`, then:

1. Try the new public derivation against each registered recipient spend public key. Use `underive_public_key(D, i, P)` to identify the **matching** primary or subaddress key; do not assign every matching commitment to the first element of `spendKeys`. The current loop in [`TransfersConsumer.cpp`](../../src/Transfers/TransfersConsumer.cpp) does that and must be replaced.
2. If no new match, compare `P` with the old `deriveLegacyCommitmentKeys(depositSecret).commitKey`. Record `legacy_exposed`. If both forms appear to match, reject as ambiguous rather than guessing.
3. Persist output derivation kind, recipient account/subaddress index, amount-mask data, and provenance. Derive the new spend scalar and key image only in the signing context with `b`/`d_sub`; never cache them in view-only state. Old key images can still be derived by a view-only wallet, but that ability is evidence of the old exposure.
4. Update spent-output tracking, balances, transfer selection, key-image import/export, rescan and backup. A new watch-only wallet cannot calculate new-output key images by itself and may not know whether such an output was spent until it receives key images from the spending wallet. Display that limitation accurately.

The C++ scan and spend paths include [`TransfersConsumer.cpp`](../../src/Transfers/TransfersConsumer.cpp), [`WalletGreen.cpp`](../../src/Wallet/WalletGreen.cpp), and [`WalletTransactionSender.cpp`](../../src/WalletLegacy/WalletTransactionSender.cpp). Port the same bytes and curve operations to [Valise `ring.rs`](/Users/aejt/DEXFG/fuego-flutter-wallet/rust-fuego-wallet/fuego-sdk/fuego-crypto/src/ring.rs), [`scanner.rs`](/Users/aejt/DEXFG/fuego-flutter-wallet/rust-fuego-wallet/fuego-sdk/fuego-sdk/src/scanner.rs), and [`transaction_builder.rs`](/Users/aejt/DEXFG/fuego-flutter-wallet/rust-fuego-wallet/fuego-sdk/fuego-sdk/src/transaction_builder.rs). Audit walletd and Flutter models for persisted `key_scalar` and misleading watch-only balances.

### A3. Creation policy, rollout and old funds

Map every active creation path to **recipient address, output index, authority class, and later spend path**. Include XFG/HEAT mint and transfers, CD create/rollover/withdraw, LP share issue/redeem, swap receive and change, and DIGM if enabled. User-owned outputs use `P = B+tG`; protocol-owned `POOL_XFG`, `POOL_HEAT`, vault and intentionally unspendable outputs need their own reviewed rule. Do not change them merely because they call the old helper. Eliminate unreachable code only under a separate reviewed change, without rewriting historical serialization.

New wallets should scan **both** forms before they create the new form. Suite and Valise sender/receiver vectors must ship together. Gate owner-bound output creation at the v11 wallet rollout, and preserve old-output scanning indefinitely. The public output key itself distinguishes the two forms to an upgraded scanner; a new `tx_extra` tag is unnecessary for recognition. An old scanner will miss a new-form payment until updated. Nodes cannot infer a recipient's `B` from an output key, so the existing transaction format cannot force a malicious sender to use owner-bound derivation. A malicious old-form payment must be shown as exposed and swept, never presented as securely received.

Produce byte-for-byte C++/Rust vectors for primary and subaddresses, output indices `0`, `1`, `127`, `128` and a large index, old/new forms, amount mask, key image, wrong view key, wrong spend key, invalid public key, and two recipients in one transaction. Include full send → scan → sign → node verification and rescan after wallet restart. Test view-only recognition without signing authority. Test a sender attempt to ring-sign a new output using only `r`, `A`, `B` and public chain data; it must fail.

## B. Legacy asset-accounting audit: observed risks

| Source path | Observed behavior | P0 test or decision |
| --- | --- | --- |
| [`Blockchain.cpp::checkCommitmentSpendInput`](../../src/CryptoNoteCore/Blockchain.cpp) | Resolves commitment ring members by **amount**; different terms can share a ring. | Construct a valid mixed-asset ring with the true signer at different positions; verify that no decoy order changes accepted value. |
| [`Blockchain.cpp::classifyInputAsset`](../../src/CryptoNoteCore/Blockchain.cpp) | Uses the ring's first referenced output as the asset. Missing/invalid references fall back to XFG. | Remove first-member and fallback authority for v11; test actual signer on every ring position. This is a code-level trust flaw; a working exploit still needs a built transaction and node replay. |
| `TransactionInputCommitmentTransfer` in [`Blockchain.cpp`](../../src/CryptoNoteCore/Blockchain.cpp) | Classification always returns XFG. The input verifier checks a ring signature and maturity but does not bind `newTerm` to a replacement commitment output. | Attempt HEAT/LP to XFG conversion and ordinary-output early release. If transfer cannot be made safe before v11, reject this input type after the v11 gate while preserving old-block validation. |
| [`Currency.cpp::classifyOutputAsset`](../../src/CryptoNoteCore/Currency.cpp) and [`AssetType.h`](../../src/CryptoNoteCore/AssetType.h) | Finite commitment terms and `term=0` classify as HEAT; `DIGM_TERM` also falls through to HEAT. The input classifier maps `term=0` to XFG, a direct inconsistency. The enum has only XFG/HEAT/LP. | Derive historical asset from **creation height plus output semantics**. Resolve term-0 consistently or reject new term-0 outputs at v11; treat DIGM as a distinct asset if activated. Verify pre-v11 finite-term history from actual accepted outputs. |
| [`DigmMintEngine.cpp`](../../src/CryptoNoteCore/DigmMintEngine.cpp) | Sums every commitment-spend input as if it were HEAT; it does not know the true ring member's asset. | Disable DIGM mint at v11 until authenticated HEAT debits, exact peg arithmetic, DIGM supply and lock collateral are proven in accepted-block tests. |
| [`Currency.cpp`](../../src/CryptoNoteCore/Currency.cpp), [`Core.cpp`](../../src/CryptoNoteCore/Core.cpp), [`Blockchain.cpp`](../../src/CryptoNoteCore/Blockchain.cpp) | Aggregate and per-asset `uint64_t` sums and several `out + fee + burn` expressions can wrap; `check_money_overflow` does not include claimed interest. Fee fallback uses combined assets. | Use checked wide intermediates and typed per-asset deltas at the v11 gate. Test `UINT64_MAX`, multiple inputs, interest, fees and all conversion tags. |
| [`Blockchain.cpp`](../../src/CryptoNoteCore/Blockchain.cpp) connect/pop | Recomputes HEAT supply and AMM deltas from input classification, and reverses on pop. | Rebuild/pop must produce the same classified deltas and roots as validation; no partial state mutation on failure. |

This table is an audit lead list, **not** proof that a particular chain exploit succeeded. Do not publish a severity or loss estimate until an adversarial transaction demonstrates the preconditions and accepted-block result.

### B1. Canonical asset and conservation rules

Create a single v11 classifier for every referenced output, returning either a verified `(asset, policyClass, creationHeight)` or an error. Never default an unresolved input to XFG. Build the historical mapping from chain data and rules at each creation height. The initial table to verify is: `KeyOutput`/legacy multisig → XFG; `HEAT_TERM` → HEAT; post-v11 finite CD → HEAT; `DEPOSIT_TERM_LP` → LP; `POOL_XFG`/`POOL_HEAT` → the matching reserved asset; `SWAP_RECEIVE_XFG` → XFG; `DIGM_TERM` → separate DIGM; pre-v11 finite commitments and `term=0` → **verify against historical creation paths before assigning**. A term constant alone is insufficient if its meaning changed across upgrades.

For a v11 commitment ring, classify **every** member and require one asset. Apply the same asset to the input amount independently of member order. If policy classes differ, require a common spend rule valid for all members; interest already needs a finite-CD-only ring, and pool-owned/unspendable members cannot serve as legal decoys. This narrows the decoy set by asset, a real privacy cost to disclose until the shielded pool supersedes rings. Extend the same rule to transfer inputs or reject them after the v11 gate. Keep the historical branch unchanged for old heights.

Define a typed `TransactionValueDelta` from the verified transaction, pre-state and block height. It must account for XFG, HEAT, LP and DIGM if enabled, plus pool reserves, CD principal, CD_APY_POOL, BONUS_VAULT, treasury, pending orders and public fee. For each asset, require exact equality in checked wide arithmetic:

```text
user inputs + authorized issuance + authorized release from protocol state
  = user outputs + authorized burns + newly locked protocol state + fee
```

Every term is explicit and bounded by the pre-state. HEAT mint uses the accepted TWAP and exact rounding; Hearth swaps use actual debit/credit and the 1% fee with the 70/30 routing; atomic-swap fees use 69/11/20. If CD creation's 0.1% charge is activated in v11, model its burn and Treasury LP credit explicitly. Fees charged to miners are XFG unless a separately specified consensus rule says otherwise. The same canonical delta must drive admission, connection, disconnection and supply counters. Avoid recomputing from an attacker-controlled first ring member or mutable external RPC state. Source comments and project docs disagree on some v11/v12 feature timing; freeze the activation matrix before this delta is coded.

### B2. Fix order and tests

1. Build an accepted-chain inventory by height and term, including output origin, amount, current asset classifier, key image, and all transaction-extra action tags. Compare current balances and supply counters after full replay and after restart. Do not write private keys or deposit secrets into the report.
2. Add adversarial transactions to the production verifier test harness: mixed XFG/HEAT and HEAT/LP rings with each real position; first-member permutations; all-legacy and mixed-era finite terms; transfer input with ordinary XFG output; DIGM with non-HEAT input; duplicate or malformed action tags; interest/fee overflow; reserve and vault shortfall. Distinguish rejected at mempool, rejected at block, and accepted at historical height.
3. Freeze the historical asset table and v11 gate. Add checked classification and wide arithmetic in one canonical value-delta path. Reject unknown class, mixed-asset ring, unsupported transfer, unmatched action, and overflow before state mutation. Review every validation branch in `Blockchain::pushBlock` and `Core` against the same rule.
4. Feed the verified delta to connect and pop/undo. Make rollback an exact inverse, including HEAT/DIGM supply, LP shares, locked CDs, bonus/vault UTXOs, limit deposits and AMM reserves. Compare sequential replay with rebuild and restart over an upgrade-boundary chain.
5. Run focused unit and integration tests, then a multi-node fork/reorg test with mixed asset transactions. Require independent security review before v11 activation or an old-to-shielded bridge.

**P0 sign-off:** new owner-bound outputs cannot be signed with sender/view-only material; both wallets agree on keys and amounts; old outputs remain discoverable; every accepted v11 transaction satisfies exact per-asset conservation; adversarial ring/transfer/DIGM cases fail; historical replay, restart, reorg and multi-node results agree. Source changes require the repo's `CHANGELOG.agent.md` task/sign-off entry.
