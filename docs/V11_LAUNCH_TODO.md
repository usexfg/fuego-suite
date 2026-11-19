# V11 Launch To-Do (HEATWAVE, mainnet height 1,111,111)

Owner, 2026-11-19: make v11 production ready first, then v12. Each item says what must change or be shown, and how it is verified. Source changes need a `CHANGELOG.agent.md` entry.

`docs/RELEASE_READINESS_CHECK.md` (2026-10-22) called the core solid from unit tests alone. On a live testnet no HEAT could be spent (see `docs/V12_LAUNCH_TODO.md` item 0). Readiness is shown on running nodes, not by unit tests.

## Must do

### 1. Testnet must run v11 rules long enough to test them (owner decision)
Testnet v11 starts after height 30, the 72-block AMM-only bootstrap (`TESTNET_BOOTSTRAP_BLOCKS`) ends at 102, and v12 starts after 180. So the v11 call auction and backstop run for 78 blocks (about 13 minutes at 10 s). An FCI quarter on testnet is 180 blocks (18 epochs × 10), so no quarterly re-base can happen under v11 on testnet.
Options:
- Raise the testnet v12 height (for example to 1,000). This is a consensus change for testnet: any public testnet node already past 180 forks off, and its chain must be reset. The cloud session cannot reach the testnet seeds, so it cannot check whether they are live.
- Add a testnet-only daemon option for private test nets. The public testnet keeps 180, but this adds a flag that a misconfigured node can fork itself with.
- Keep 180, test in the 78-block window, and cover the FCI re-base by unit tests only.
Verify: `testCdActivationHeight` and the testnet-chain core tests pass at the chosen height.

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
Two sends started close together could select the same XFG input: inputs were chosen when the request was made and marked used only when the transaction was built, after the decoy round trip. Seen on testnet (auto-withdraw against a manual `cancel_order`). Every send now reserves its selected inputs when its transaction record is created (`claimSelectedInputs`); the reservation moves to the unconfirmed set when the transaction is built and is dropped when the send fails. Reservations are in memory only: a wallet restarted mid-send releases them, the same as an unbuilt send today. Verified by `wallet_input_reservation_tests` (3 tests; each fails with the reservation or the release removed).

### 6. Release documents
Correct `docs/releasenotes/RELEASE_NOTES_V11.md` (testnet v11 height is 30, not 25; the fixes in PR #83) and replace `docs/RELEASE_READINESS_CHECK.md`'s verdict with evidence from items 2–4.

### 7. CI
All jobs on PR #83 green, including sanitizers, macOS and Windows. `master` CI does not compile (boost::get static assertion in `Blockchain.cpp`, fixed on this branch), so tests on `master` have not run since that break. First run on PR #83: Mintlify parse error in `docs/PQ-MIGRATION-PLAN.md` and 5 stale `commitment_owner_bound_tests` cases, both fixed (2026-11-19). Sanitizers, macOS (15, 26), Windows and the transport gates passed on db891918.
Consequence of the legacy-form removal (33e787a5): a commitment output created with the legacy key (any node before that commit, including early testnet blocks) is neither tracked nor spendable by the wallets. The commit states none exist on mainnet; this session cannot check mainnet.

### 8. Earlier release blockers (owner decisions)
From `docs/RELEASE_READINESS_CHECK.md`: MCP npm vulnerabilities; the dev-fund view-secret "LAUNCH BLOCKER" comment against the audit's non-vulnerability classification.
