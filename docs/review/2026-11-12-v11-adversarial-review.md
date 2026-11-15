# v11 Adversarial Review — findings, fixes, recommendations

**Date:** 2026-11-12 (fixes and docs 2026-11-13; real-chain checks and wallet findings 2026-11-14/15)
**Reviewer:** Claude Code (claude-opus-5-5 / claude-sonnet-5-5)
**Branch:** `claude/brave-meitner-r8wmlt`
**Method:** a third-party review of the Hearth / HEAT code was checked claim by claim. Where possible the production code was compiled and run (harnesses linked against the project's own `-O2` build, valgrind, mutation of each fix). Everything else is code reading and is labelled as such.
**Activation context:** mainnet `UPGRADE_HEIGHT_V11 = 1111111`; highest checkpoint 999999. Every finding below becomes live when the chain reaches v11. Testnet is already at v11. The current mainnet height is not known from this environment.

## Findings and status

| ID | Finding | Severity | Evidence | Status |
|----|---------|----------|----------|--------|
| D | A v11 coinbase could carry commitment outputs (HEAT, CD principal, LP shares). The coinbase skips `check_tx_outputs` and `validateSettlement`, so nothing was burned for them. A miner-keyed coinbase `HEAT_TERM` output ringed with any spendable HEAT decoy was accepted as a spend. | Critical | Run: `check_outs_valid` accepts the outputs; spend check accepted the mixed ring | **Fixed** |
| A | `generatePoolOrders` never set `targetPrice`, which the auction reads. The pool market maker's prices were indeterminate: 77 at `-O2`, 139849386448320 at `-O0`. Nodes built differently would split on every post-bootstrap auction. | Critical | Run on the project build; valgrind | **Fixed** |
| E | LP-share and swap-receive outputs were read as lock lengths (`memberHeight + 0xFFFFFFFD` wraps `uint32`), so LP shares could never be withdrawn. | High | Arithmetic run; code reading of both spend paths | **Fixed** |
| B | Orderbook state (P_clear, bootstrap countdown, regeneration inputs, spread history, pool band) lived only in process globals. A clean restart re-entered bootstrap and skipped auctions other nodes ran. | High | Reload test (red, then green) | **Fixed** |
| B2 | Per-block rollback journals (order fills, LP removals, swap fees, epoch distributions, TWAP contributions, orderbook snapshots) were not in the cache, so a reorg after a clean restart could not reverse earlier blocks. | High | Reload test; mutation | **Fixed** |
| C | `makePoolId` hashed 56 bytes but wrote 48. | Low | valgrind | **Fixed** |
| DIGM | DIGM-term outputs were creatable at v11 and carried no usable lock. | Medium | Code reading; gate test with mutation | **Fixed** (admitted only from v13; mint stays rejected from v12) |
| B3 | `m_blockEpochDistributions` gets an entry every block but loses one only when an epoch-boundary block is popped. After a reorg over ordinary blocks, popping a boundary block reverses the wrong entry and skips the treasury reversal. The v11 treasury share is 0, so at v11 the wrong entry reverses 0. | Medium (reorg only) | Real-chain reorg check with the fix reverted | **Fixed** |
| T | An exact tie in the call auction has no taker and so pays no 1% fee, including a user order sized exactly to a pool order level. At launch the price tick (8% of spot) is wider than the pool spread, so the walk the review described does not run; above about 0.7 HEAT/XFG it does, but slowly (+2% over 200 blocks in the harness). | Medium | Run | **Open** |
| 1 | The review's High (mint price freezes if the pool empties): the mechanism exists (`twapBefore` has no age limit and no spot is recorded while the pool is empty). The protocol seed is gone; the first deposit now locks 1,000 shares that no output holds, so no removal empties the pool. | Low | Real-chain removal of all held shares leaves the locked shares | **Open** (cheap guard recommended) |
| 3 | The mint is priced only from the pool's own 8-block spot TWAP, with no deviation cap and no external reference. | Medium | Code reading; cost of manipulation not modelled | **Open** |
| 2 | The commitment-index RPCs return an empty index (no writers) and skip the restricted-mode check. | Low | Code reading | **Open** |
| 4 | `rebuildCache` replays blocks without `validateSettlement`; the swap reserve subtraction has no bounds check. Needs a rule change plus forced replay, or a corrupted local database. | Low | Code reading | **Open** |
| K | The pool marker key (`computePoolCommitKey`) is derived from a public seed, so its secret scalar is public. Pool markers are protected only by the consensus term guard. The transfer path had no such guard until this change. | Medium | Code reading | **Open** (guard fixed; key not) |
| 6 | Documentation contradicted the code (VWAP clearing, spot fallback, CPI peg, 10:1 launch ratio, two stale code comments). | Medium | Code reading | **Fixed** |
| IX | `popTransactions` cleared all four output indexes, and the alternative-chain insert kept iterators the hash map invalidates. A reorg read freed memory and wiped the output index. | Critical (reorg) | Real-chain reorg check | **Fixed** |
| HS | Every wallet HEAT output carries `HEAT_TERM`, and the all-FOREVER guard read that term as a burn marker, so no HEAT spend could pass at v11. | Critical | Spend check test | **Fixed** (spendable from v11; no `HEAT_TERM` output exists before v11) |
| SB | The protocol created 8,000 XFG / 1,000 HEAT of pool reserves with no emission or burn behind them. | High | Code reading | **Fixed** (removed; seedboot by a normal first deposit at the 8:1 launch ratio, 1,000 shares locked) |
| CS | `pushTransaction` claims each commitment key image, then a second pass asked whether the same key image was spent and found its own claim. Since 3e75733 (2026-10-07, also on `master`) every block carrying a HEAT, CD, LP or swap commitment spend fails at push while the mempool accepts the tx, so miners keep building failing blocks. | Critical (liveness) | Real chain: first LP add block refused | **Fixed** |
| PC | The core pre-check refused any tx whose outputs exceed its inputs unless it carried a mint or swap tag, so LP removals never reached the pool. | High | Real chain | **Fixed** (from v11 settlement-tagged txs are balanced per asset by the pool's settlement check) |
| RS | During a chain switch the pool refused the disconnected chain's LP removal (its exact payout no longer settles), so the switch failed, and the rollback could not reload the same tx: the node stayed truncated below its original tip ("PANIC!!! failed to add block (again)"). | High (reorg) | Real-chain reorg of an LP add and removal | **Fixed** (kept-by-block txs stay pooled as unverifiable; the template re-checks settlement) |
| ND | Cache bytes and vault indexes depended on history: `CommitmentOutputRef::commitment_ec` wrote stack memory to disk; the vault UTXO counter was not rewound on pop. Neither is read by consensus today. | Low | Real-chain restart and reorg digests | **Fixed** |
| W1 | WalletLegacy never showed HEAT: the container's deposit filter excludes `HEAT_TERM`, and HEAT records never unlocked. | High | Real chain | **Fixed** |
| W2 | Nine wallet send paths stored no tx hash, so every mint, swap, LP and HEAT send stayed "unconfirmed" and the confirmed tx appeared as a second record. | Medium | Real chain | **Fixed** |
| W3 | The wallet never saw its own commitment spends (descriptor type mismatch), so spent HEAT and LP stayed in the balance. | High | Real chain | **Fixed** |
| W4 | Wallet LP add always failed (key decoys requested for the HEAT amount); wallet LP removal wrote the retired legacy tag, paid nothing out and returned unburned shares as XFG. | High | Real chain | **Fixed** (both rebuilt; mint → first deposit → removal passes end to end) |
| W5 | The wallet read every commitment term as a lock length: HEAT and LP unlocked only through `uint32` wrap, swap-receive never. | Medium | Code reading; real chain | **Fixed** |
| RG | A node refuses any reorg that drops a main-chain tx. A ring tx whose decoys reference blocks the other branch replaces can never be carried there, so such reorgs are refused outright. | Medium (policy) | Real chain | **Open** (inherited rule; decision) |
| VK | Every vault UTXO registers the same key image (derived from the constant `m_vaultKeys.viewPub`), so `isVaultKeyImage` maps it to the newest index only. | Medium | Code reading | **Open** |
| TN | Testnet runs mainnet rules in three places: the orderbook bootstrap uses `BOOTSTRAP_BLOCKS` (144), the reorg Poisson check uses mainnet `DIFFICULTY_TARGET`, and pre-v10 difficulty reaches 0 on a fast testnet chain. | Low | Real chain | **Open** |

## What "pool outputs unspendable" means

- **Pool markers** (`POLX` / `POLH` terms) are receipts created when a user pays into the pool (LP add, swap input, limit deposit). The value lives in the `m_ammPool` counters. The marker only records that the transaction paid in. Nobody can spend one.
- **User LP shares** are separate `DEPOSIT_TERM_LP` outputs owned by the depositor. They are spendable (this is what fix E restored) and burned through `LpRemoveAuth`. Withdrawals and swap proceeds are new ordinary outputs, never the marker.
- **AuntPoL (protocol LP)** has no outputs at all. Its position is the counter `m_protocolLpShares` plus treasury reserve counters and a separate vault UTXO set. It is unaffected by this rule.

## Recommendations

Ordered by what should happen before mainnet reaches v11.

1. **Before activation (blocking):**
   - Decide **T**: when no side is rationed, make the non-pool order the taker, or the later order (`laterOrder` already exists), so a fill against pool liquidity always pays the fee. Model the wash-trade incentive first.
   - Scan the existing mainnet chain for coinbase transactions carrying non-key outputs. This is a single decisive check. The v11-only guard does not cover earlier versions, and pre-v11 impact was not assessed.
   - Run a public testnet v11 soak with scripted restarts and reorgs. The in-process harness (`tests/CoreTests/v11_chain_checks.sh`) now does this on one machine with real wallet activity (C1 restart, C2a/C2b reorg, all PASS); it is not in CI.
   - Ship **CS** to every node before anyone broadcasts a commitment spend: a node without it fails every block that carries one.
2. **Cheap defenses to add with the above:**
   - **1:** make `twapBefore` return 0 when the newest recorded spot is older than a fixed number of blocks, so mints refuse instead of using a stale price.
   - **4:** add an explicit `reserveOut >= out` guard in swap settlement that fails the block, like LP removal already does.
   - **2:** return an explicit "retired" status from the commitment RPCs instead of a zero root, and delete the commented-out writers with the maintainer's approval.
3. **Economic hardening (needs a model before choosing numbers):**
   - **3:** bound the mint price against a longer TWAP (for example epoch-length) and cap mint size per block relative to reserves. The 8,000 XFG / 1,000 HEAT seed is shallow, so the cap matters most early.
4. **Next fork (changes outputs wallets already create):**
   - **K:** replace the pool marker key with a hash-to-point key whose discrete log nobody knows. Until then the term guard in `checkCommitmentSpendInput` and `checkCommitmentTransferInput` is the only protection, and any new spend path must repeat it.
5. **DIGM (v13 or later):** before lifting both gates (`DIGM_ACTIVATION_MAJOR_VERSION` and the `isDigmMint` rejection), make `DigmMintEngine::validateMint` resolve each input's ring term so only HEAT is debited, enforce the supply cap, and add a mint test on a real chain.
6. **Engineering hygiene:**
   - Run `core_tests` under valgrind (or MSan) in CI. Finding A was invisible to the existing tests because every test was green on indeterminate data.
   - Add a golden vector for pool order prices and IDs so cross-compiler drift fails a test.
   - Keep the new `TreasuryCoreTests.cpp` regression tests; each was confirmed to fail without its fix.
   - Run `v11_chain_harness seedboot` and `v11_chain_checks.sh` in CI. Fixes CS, PC, RS, ND and W1–W5 have no other regression test: `core_tests` has no block-push path.
   - Decide **RG** and **VK**; fix **TN** so testnet exercises its own parameters.
   - Generate constants tables in docs from `CryptoNoteConfig.h` or add a doc check; four of the doc contradictions were constants and formulas.

## Operational notes

- Cache archive version moved 17 → 19. Every node performs one full replay on upgrade.
- Wallets that synced HEAT before fix W1 hold no HEAT records and need a rescan. No mainnet HEAT exists before v11.
- `graphify update .` was not run: `graphify` is not installed in the review environment.
- `AGENTS.md` still states that HEAT "adjusts for inflation/CPI". It was not edited because it is the project's agent instruction file; the maintainer should reword it to match the corrected README.
- Run on an in-process testnet chain (production `core`, WalletLegacy on `InProcessNode`): mint at the launch ratio, first deposit, full LP removal, restart mid-activity, and reorg of the deposit and removal.
- Not run: a coinbase HEAT spend on a real chain; anything on a networked daemon (the block-serving RPC hang blocks it); the economics of the tie-fee walk and of mint manipulation; the mainnet coinbase scan.
