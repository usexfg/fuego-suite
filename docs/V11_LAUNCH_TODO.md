# V11 Launch To-Do (HEATWAVE, mainnet height 1,111,111)

Owner, 2026-11-19: make v11 production ready first, then v12. Each item says what must change or be shown, and how it is verified. Source changes need a `CHANGELOG.agent.md` entry.

`docs/RELEASE_READINESS_CHECK.md` (2026-10-22) called the core solid from unit tests alone. On a live testnet no HEAT could be spent (see `docs/V12_LAUNCH_TODO.md` item 0). Readiness is shown on running nodes, not by unit tests.

## Must do

### 1. Testnet runs v11 rules long enough to test them — DONE (2026-11-19, owner decision)
Before: v11 after 30, the 72-block bootstrap ended at 102 and v12 started after 180, so the call auction ran for 79 blocks (about 13 minutes at 10 s). Testnet v12 now starts after 1000 (`TESTNET_UPGRADE_HEIGHT_V12`), giving about 900 auction blocks (about 2.5 hours) and five re-mint quarters under v11. CDs open at block 1001.
- The testnet schedule is named constants in `CryptoNoteConfig.h`, applied by `CurrencyBuilder::testnet(true)` (tests can build other schedules). `testTestnetRunsV11PastTheBootstrap` pins the values.
- Testnet nodes now use their own network id (`CRYPTONOTE_NETWORK_TESTNET`), so nodes on the old schedule and the new one do not connect.
- A node started on data written under another schedule rolls back to the last block the current schedule accepts (`Blockchain::firstBlockWithWrongMajorVersion`, `testStoredChainFromAnotherScheduleRollsBack`). If that block is more than 1000 blocks below the tip (the undo history), the node refuses to start and asks for a wipe, because HEAT, Hearth and alias state could not be rolled back that far.
- Relaunch: every testnet node, the four seeds included, deletes its `testnet_*` blockchain, index, cache and pool files before starting the new binary. Testnet peers come only from command-line options (`SEED_NODES_TESTNET` is not used by the node), so each operator's peers must run the new binary.
- `testnetd` uses the same `testnet(true)` schedule.
- FCI under v11 (owner decision 2026-11-25): votes are recorded and every step runs except the hearthstone re-base and DIGM rewards to FCI-voting blocks, both reserved for v12. The code matches (`FuegoCostIndex.cpp`, re-base gated on `BLOCK_MAJOR_VERSION_12`; no DIGM vote reward exists yet). Docs corrected: `RELEASE_NOTES_V11.md`, `fuego-cost-index.mdx`, `what-is-heat.mdx`, `AGENTS.md`.

### 2. Live testnet pass at v11 rules, both wallets
SimpleWallet and walletd, each step mined and checked against node state (`/heat_metrics`, `/amm_pool_info`, `/get_limit_orders`, `/get_public_audit`):
- XFG send (regression), HEAT mint (premium to both AuntPoL legs), send HEAT, sell HEAT and buy HEAT on Hearth.
- LP add and LP remove.
- Limit orders, both sides: auction fills against another wallet and the pool band, backstop fills, stop, withdraw, auto-withdraw.
- Alias registration (re-mint burn), `dev_donate`, the quarterly re-mint coinbase output.
- FCI basket votes and a quarterly re-base.
- AuntPoL epoch work.
- funsa subaddress send and receive.

### 3. Two nodes, reorg and resync
Two testnet nodes mining against each other: blocks and v11 transactions propagate; a reorg across blocks holding HEAT, Hearth and LP transactions leaves both nodes on the same Hearth state hash. A third node syncs from genesis, and a node restarted without its cache (`rebuildCache`) reaches the same state hash and `/get_public_audit` totals.

### 4. Mainnet replay
A v11 binary syncs mainnet (v1–v10) from genesis to the tip and keeps up with new blocks. Not possible from the cloud session (the mainnet seed refused a P2P connection; only an HTTPS proxy is available): needs a machine with P2P access or a CI job.

### 5. WalletLegacy concurrent sends — DONE (2026-11-19)
Two sends started close together could select the same XFG input: inputs were chosen when the request was made and marked used only when the transaction was built, after the decoy round trip. Seen on testnet (auto-withdraw against a manual `cancel_order`). Every send now reserves its selected inputs when its transaction record is created (`claimSelectedInputs`); the reservation moves to the unconfirmed set when the transaction is built and is dropped when the send fails. Reservations are in memory only: a wallet restarted mid-send releases its XFG reservations, and on load a send that was never built is marked failed and releases its HEAT deposits. Guardian review (2026-11-19) fixes: a held output cannot be claimed by a second send, fusion skips held outputs, a completed send releases its reservation, `withdrawDeposits` refuses a deposit an in-flight spend holds, a commitment spend's spent-deposit record is erased with its unconfirmed record, a node's decoy answer must hold one set per selected input for that input's amount (a short answer read past the end of the vector). Guardian pass 2 fixes: a send is refused before its record exists if an input is held; a deposit spend the node dropped expires after the live time and releases its deposits (it used to stay held until a rescan); `RpcServer` no longer aborts debug builds on an amount with no decoys; a dropped unconfirmed spend no longer aborts debug wallets. Verified by `wallet_input_reservation_tests` (10 tests; each fix fails its test when reverted).

### 6. Release documents
Correct `docs/releasenotes/RELEASE_NOTES_V11.md` (testnet v11 height is 30, not 25; the fixes in PR #83) and replace `docs/RELEASE_READINESS_CHECK.md`'s verdict with evidence from items 2–4.

### 7. CI
All jobs on PR #83 green, including sanitizers, macOS and Windows. `master` CI does not compile (boost::get static assertion in `Blockchain.cpp`, fixed on this branch), so tests on `master` have not run since that break. First run on PR #83: Mintlify parse error in `docs/PQ-MIGRATION-PLAN.md` and 5 stale `commitment_owner_bound_tests` cases, both fixed (2026-11-19). Sanitizers, macOS (15, 26), Windows and the transport gates passed on db891918.
Consequence of the legacy-form removal (33e787a5): a commitment output created with the legacy key (any node before that commit, including early testnet blocks) is neither tracked nor spendable by the wallets. The commit states none exist on mainnet; this session cannot check mainnet.

### 8. Earlier release blockers (owner decisions)
From `docs/RELEASE_READINESS_CHECK.md`: MCP npm vulnerabilities; the dev-fund view-secret "LAUNCH BLOCKER" comment against the audit's non-vulnerability classification.

### 9. Tests that catch what the 2026-10-22 check missed, and features added later
Why it was missed: every HEAT test exercised one layer with handcrafted input. Consensus tests built transactions by hand; wallet code was never run against a node; the transfers container never received a HEAT output. No test took HEAT from a wallet, through a node and a block, back into a wallet. Each layer passed while the path between them was broken.

9.1 Regression audit. For every defect in `docs/V12_LAUNCH_TODO.md` item 0 and the testnet runs of 2026-11-19, the automated test that fails without the fix (audited 2026-11-19):

| Defect | Test | Status |
|---|---|---|
| 0.1 post-v11 HEAT rings rejected | `testHeatRingsAfterHeatwave` | covered |
| 0.2 node decoys not ring-valid | `testNodeDecoysAreRingValid` | covered |
| 0.3 container dropped HEAT | `HeatIsListedOnlyUnderTheHeatType` | covered |
| 0.3 WalletGreen available balance counted CDs and HEAT | none | gap: walletd scenario |
| 0.3 WalletLegacy reserve proof counted HEAT | none | gap |
| 0.4 WalletLegacy HEAT records (own and external) | `heat_e2e_tests` mint, send, reopen | covered |
| 0.4 WalletGreen deposit index unique per (tx, output) | none | gap: walletd scenario |
| 0.5 WalletLegacy send HEAT, sell, LP add/remove | `heat_e2e_tests` | covered |
| 0.5 WalletLegacy CD open, HEAT-escrow order, CD withdraw | none (testnet run only) | gap: CD and order scenarios |
| 0.5 signing keys wiped | none | gap: not observable from outside; review only |
| 0.6 marker-term maturity | `MarkerTermOutputsBecomeSpendable`, `testMarkerTermRingsAfterHeatwave` | covered |
| 0.6 DIGM in a HEAT ring | `testMarkerTermRingsAfterHeatwave` | covered |
| 0.6 LP remove pays declared amounts | `testLpRemovePaysTheDeclaredAmounts`, `heat_e2e_tests` LP | covered |
| 0.6 LP add declared wrong shares | `heat_e2e_tests` LP | covered |
| 0.6 sell proceeds to a commitment | `heat_e2e_tests` sell (WalletLegacy) | WalletGreen gap |
| 0.6 CD withdraw paid XFG | none | gap: CD scenario |
| 0.7 `popTransactions` cleared indexes | `testBlockPopKeepsEarlierOutputs` | covered |
| 0.7 commitment spend could not connect | `testCommitmentSpendConnectsOnce` | covered |
| 0.7 output check used mainnet term range | `testOutputCheckUsesTheNetworkTermRange` | covered |
| 0.7 v11 payouts hit the aggregate amount check | `heat_e2e_tests` LP remove | covered |
| 0.7 `get_limit_orders` uninitialised fields | none | gap: reading an uninitialised field is undefined behaviour, so only a sanitizer (MSan) or an order scenario can catch it |
| 0.7 WalletLegacy reload lost hashes and deposit data | `heat_e2e_tests` reopen | covered |
| 0.7 failed send kept deposits spent | `FailedSendReleasesItsInputs`, `UnbuiltSendReleasesItsDeposit` | covered |
| 0.7 `reset` kept stale deposit records | `heat_e2e_tests` reset | covered |
| 0.7 console dropped exceptions | none | gap |
| Concurrent sends picked the same input | `wallet_input_reservation_tests` | covered |
| Stale `commitment_owner_bound_tests` | the suite itself | covered |

9.2 In-process end-to-end scenarios. `tests/IntegrationTestLib` (in-process node with real core and P2P, WalletLegacy driver) is built again (`IntegrationTestLib` target; three interface fixes). `tests/IntegrationTests` stays unbuilt: its `main.cpp` does not parse and `Node.cpp` uses a removed WalletGreen API. New `heat_e2e_tests` (`tests/E2E/HeatWalletScenarios.cpp`, in ctest) mines real proof-of-work blocks and checks every result against node state:
- DONE 2026-11-21: mint (exact supply delta = minted HEAT + premium HEAT), send HEAT to a second wallet, sell HEAT on Hearth (exact XFG received), LP add and remove (exact provider shares, HEAT and XFG back), save and reopen without rescan, reset and rescan after a HEAT send. Wallets price operations through the same `HearthQuote` functions SimpleWallet uses (`quoteHeatMint`, `quoteSwapOutput`, LP quotes). 6 scenarios, about 40 s.
- Found by the rescan scenario and fixed: the transfers container never marked a spent commitment output (HEAT, CD, LP shares) as spent. Outputs were indexed under a `Commitment` spend descriptor and inputs looked them up under a `Key` one, which never compare equal; CD transfer inputs fell into an assert. A wallet's balance was right only from its own send bookkeeping, so a rescan, a restore from keys or another wallet on the same keys counted spent HEAT as spendable and would try to spend it. Both wallets use this container. Transfers caches written before the fix (storage version below 4) that hold commitment outputs are refused on load, so those wallets rescan once on their own (`CommitmentSpendMarksTheOutputSpent`, `CommitmentTransferMarksTheOutputSpent`, `ResetWalletRescansToTheSameBalances`).
- Fixed 2026-11-21: against an in-process node, WalletLegacy's synchronizer reported progress at the tip in a tight loop and never reported sync completed. `InProcessNode::getLastKnownBlockHeight` returned the observed height minus one, which wrapped to the largest height with no peers; it now falls back to the node's own tip. NodeRpcProxy reads the daemon's height and is not affected.
- The in-process test node set no testnet flag on its P2P config, so it dialled mainnet seeds under the mainnet network id; fixed.
- To do: buy HEAT, limit orders (both sides, auction fill, backstop, stop, withdraw, auto-withdraw), CD open and claim (needs a currency with an earlier v12), alias, `dev_donate`, funsa send and receive, walletd (WalletGreen) for each, and two-node propagation, reorg and resync.

9.3 Inventory gate for features added later. A test reads the feature lists the code already keeps and fails when an entry has no end-to-end scenario, or a scenario names an entry that no longer exists:
- transaction extra tags (`TX_EXTRA_*`),
- commitment terms (`AssetType`),
- SimpleWallet console commands (handler registrations),
- wallet RPC, walletd and daemon RPC method tables,
- `INode` methods.
A new tag, command or RPC method then fails CI until it has a scenario; a removed one fails until its scenario is removed. The list is read from the code, not kept by hand, so it cannot fall behind.

9.4 Release gate. `docs/releasenotes/RELEASE_NOTES_V<n>.md` lists each feature with the scenario that proves it; the gate fails a release whose notes name a feature without one. The readiness verdict is the gate's output, not a written opinion.

Limits: 9.3 sees only features that pass through one of those lists. A behaviour change inside an existing command (for example a new fee split) needs its own scenario assertion. In-process nodes do not exercise P2P over real sockets, real timing, or mainnet history; items 3 and 4 still apply.

### 10. Transaction extra and privacy follow-ups (2026-11-25)
10.1 Market-sell tag — DONE. `addMarketSellAuthToExtra` wrote fixed 8-byte values while consensus parses varints. No wallet writes the tag (market sells go through the Hearth swap), but the extra re-serializer used the same writer, so any re-serialized sell tag (the F8 janus seal re-serializes) parsed to other amounts. It now writes varints, through the same helper as the limit-deposit tag (`testMarketSellAuthRoundTrips`).

10.2 Deposit secret 0xD5 — leftover COLD code. Its only writer is `migrate_legacy_deposit`, and only `burn_info`/`cd_info` read it; their console commands are commented out, and the tag is not in the extra variant, so it never parses. HEAT CDs, Hearth LP and limit orders do not use it: their outputs carry owner-bound commitment keys derived from the transaction key. Proposed: remove the 0xD5 writer, reader, RPC check and the three dead commands (owner approval needed; nothing on chain depends on them).

10.3 HEAT to a funsa subaddress is recognisable — DONE (2026-11-26). A HEAT send to a subaddress used per-output keys (0x06), which marked it. `ITransaction::sendToOneSubaddress` gives a transaction paying one subaddress the single-key form the XFG planner uses (tx key r·D, r·G as the one janus base, change derived from the sender's view key); `outputDerivation` serves every key and commitment output, and refuses any destination other than that subaddress or the sender's change. WalletLegacy `buildHeatTransfer` and WalletGreen `sendHeatV10` use it. The scanner already accepted the form. Tests: `single_subaddress_transaction_tests`, e2e `FunsaSubaddressReceivesAndSpendsXfgAndHeat` and `WalletdHeatToFunsaReadsLikeAnyPayment` (no 0x06, one janus base, recipient credited, sender keeps its change). Still visible: HEAT amounts are public in v11 (the send tag and the commitment outputs carry them), so the payment output is identifiable by amount until hidden amounts at v12.

10.4 HEAT received on a funsa subaddress — DONE (functional, not the 10.3 privacy fix). WalletLegacy left it out of the HEAT balance and could not spend it: the sender looked up commitment outputs only in the main container and signed them with the master spend key. Every HEAT operation also signed XFG inputs taken from a subaddress with the master keys, so the node rejected it whenever the wallet picked a subaddress output. Both fixed; e2e `FunsaSubaddressReceivesAndSpendsXfgAndHeat`, `FunsaHeldXfgPaysForMintSwapAndOrders`.

10.5 Wallet defects found by the 2026-11-25 e2e scenarios — DONE. `register_alias` always failed (empty owner address). `WalletLegacy::sendTransaction` replaced every fee with the minimum, so `dev_donate` and a mainnet alias fee never carried their re-mint burn. Order withdraw and treasury donation used a fixed maximum ring with no testnet bootstrap size.

10.6 Alias front-running — DONE (owner decision 2026-11-26: burn only when registration succeeds; consensus, v11). A registration that would fail is invalid: `Blockchain::aliasRegistrationsSucceed` in `validateSettlement` checks it against the alias index (`AliasIndex::canRegister`, the rule `registerAlias` applies) and against earlier claims in the same block; the block template takes one claim per alias name and owner. Losing a race costs nothing; the losing transaction never confirms and leaves the pool at its next cleanup. Alias releases and transfers that would not apply are invalid too, each alias tag claims its name (and a transfer its new owner) in the block, the coinbase may carry no alias tag, and a registration's owner hash must be the hash of its owner address. Testnet nodes and miners must run this build (already required by the testnet reset).

10.7 `/get_alias_by_address` — DONE. It parsed with the standard-only parser, so a funsa owner fell back to a string hash that never matched. It now hashes the parsed spend and view keys of either form. The console's master-address pre-check is gone; `register_alias` checks each derived subaddress instead. walletd `register_alias` now builds through the same planner (it had never succeeded: no change address, and its owner hash was the master's). Both wallets put alias owners at subaddresses (1, minor) that are not in the wallet and own no alias (walletd takes the first free minor, the console the one after its highest); payment subaddresses stay under major 0 and the console refuses major 1 for them. Open: a restored wallet does not recreate its alias owners by itself; they can be found by recreating (1, 1..) and matching the node's alias list. Not changed: reserved aliases store the hash of the dev-fund address string, so a lookup by that address does not return them.

