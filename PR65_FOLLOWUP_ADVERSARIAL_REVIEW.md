# PR #65 follow-up: adversarial review

Reviewed against merged PR #65 (`524454dd`) and the recovered swap integration
checkout on 2026-09-25. No swaps have been live, per the user. This permits
new-swap version 2 to reject the old cooperative escrow path; it does not
substitute for a testnet fund-handling exercise.

## Qodo findings

| Finding | Verdict and current handling |
|---|---|
| 1 and 6: dashboard IDs 24–28 | Confirmed in the old static table. The dashboard now consumes the daemon's append-only pair catalog, with ZANO=24, MONAD=25, OPTIMISM=26, TON=27, DOT=28. Its numeric mapping is exercised by the Node test. |
| 2: PulseChain fields | Confirmed. The six C++ config fields and their loader/daemon references now use `snake_case`. |
| 3: PulseChain header | Confirmed. The header is `pulse_chain_client.h` and the include follows it. |
| 4: PowerShell variable | Confirmed. `$err_log` is used consistently. |
| 5: GLEEC registry | Confirmed unsafe for new swaps. The client stays registered, while readiness requires a valid deployed HTLC registry and blocks offer publication/initiation when absent. |
| 7: PulseChain example | Confirmed. The example keys match the loader; the config test parses them. |
| 8: staged offer pairs | Confirmed for SIA/ZANO/TON/DOT. Relay admission and local publication now use executable-pair checks; local initiation also needs a configured, ready client. XMR, DCR, DOGE, DASH, ZEC, BTC, BCH, LTC and KMD are additionally held from new swaps because their end-to-end claim, proof, or lock submission paths are incomplete. |

The additional claims that `MAX_PAIR_INDEX` was still 11 and the offer scans
stopped at ZANO were stale against merged PR #65: it had already expanded
those bounds. The catalog integration has since extended storage to index 45.
Bounds remain separate from executable admission. `m_takerHistory` already had
an expiry/size cap; the follow-up also bounds incoming taker identity data.

## Fund-handling changes verified in source

- Bob's direct XFG refund is independent of counterparty-client registration.
  It saves one signed refund transaction before broadcast, retries that same
  transaction, and waits for six Fuego confirmations. A known or ambiguous
  counterparty claim holds the refund for reconciliation.
- Direct XFG claims similarly save the signed transaction before send, recheck
  the confirmed counterparty claim before a first send or retry, and retain the
  swap until six Fuego confirmations. New version-2 swaps cannot fall through
  to the older cooperative path that marked a swap terminal on broadcast.
- Alice's counterparty refund stores intent and transaction ID. A confirmed
  claim can supersede an attempted refund when the chain client independently
  reveals and validates the secret. EVM reads historical claimed state;
  Solana reads finalized HTLC state. The UTXO SPV verifier requires a merkle
  proof and depth, but these pairs are now closed to new swaps because their
  SPV transports cannot submit locks and their header store omits
  chain-specific difficulty validation. Their full-node modes lack
  independent spend discovery.
- Electrum raw transaction lookup and broadcast now consume the connection
  layer's unwrapped string results, check a locally calculated TXID, and
  reject malformed proof response types rather than throwing. The in-process
  test server now shuts down its accepted socket before joining its thread.
- Direct refund derives the treasury key before constructing the output. Fee
  reporting now uses the actual treasury output (`nominal fee - network fee`),
  rather than the larger nominal deduction from the user's escrow.

## Remaining release gates

1. `/addswapfee` is a non-idempotent off-chain RPC. The daemon persists an
   attempt marker before one call to avoid double accrual, but a crash between
   those operations can permanently undercount. A failed or timed-out call
   is ambiguous. This needs a durable idempotency key and atomic accounting
   design, or fee derivation from confirmed chain transactions, before fees
   are considered exact. The existing blockchain fee accumulator also needs
   a consensus review; a local RPC contribution is not itself a chain event.
2. No adversarial two-daemon, two-chain testnet run has exercised concurrent
   claim/refund, crash/restart between save and broadcast, reorg after proof,
   or Fuego confirmation polling. Source tests do not establish this behavior.
3. The Fuego `f_transaction_json` confirmation response used by the new direct
   path has compiled and unit-level coverage, but no live RPC validation.
4. BTC/BCH/LTC/KMD need a transport that can submit both sides' transactions,
   verify network identity and difficulty transitions, and discover confirmed
   claims independently. A merkle proof against a low-difficulty chain served
   by Electrum is insufficient for a fund-release decision.
5. The recovered edits are saved in the managed worktree
   `/Users/aejt/.codex/worktrees/pr65-recovered/xfgo` on
   `codex/pr65-recovered`. The main checkout was reset during this task and
   contains unrelated work. The review worktree also includes separate EVM
   catalog and dashboard parity changes from the recovery copy; split or
   review that combined scope before a merge or PR.

## Verification in the recovered checkout

- `ninja -C /private/tmp/xfgo-integration-build -j1 xfg-swapd test_spv_electrum`
  linked successfully after the latest source changes.
- `test_spv_config_wiring`: all cases passed.
- `test_eth_protocol`: 21/21 passed; `test_swap_pair_catalog` passed.
- `test_swap_state_machine_spv`: 12/12 passed.
- `test_spv_electrum`: all connection and SPV client cases passed outside the
  sandbox, including raw TXID and broadcast identity checks. The sandbox
  blocks the test's local loopback listener.
- `node dashboard/tests/swapxfg_pairs.test.cjs`: 2/2 passed after changing the
  test to exercise the runtime catalog mapping and disabled options.
- After the final BTC/BCH/LTC/KMD gate, `clang++ -fsyntax-only` passed for the
  relay predicate and both affected C++ test sources. The full linked tests
  have not yet been rerun after that last gate: the Release Ninja attempt
  stopped while compiling `P2p/NetNode.cpp` because the shared disk ran out of
  space. The generated build directory was removed after the failure.
- `git diff --check`: clean before this report was written; rerun at handoff.

This is an in-progress security hardening branch, not a production-ready swap
release.
