# Backlog — stashed fluff (do not forget)

Everything below is **deliberately deferred**. Foundations (F0–F4, Phases A–C) are shipped and verified. Pick from here when scheduling next work.

## Protocol / consensus

- [ ] **Post-only orders** — a deposit flag that never crosses (adds depth without taking). Needs a new tx-extra tag or a v11.1 field (current `TransactionExtraLimitDeposit` format is full).
- [ ] **Maker-rebate fee credits** — for fee-less maker rebates (rebates currently only flow when a taker pays in the same block). A fee-credit accumulator per maker would let makers earn on blocks with no taker.
- [ ] **Bonus vault payout** — the 11% HEAT accrues (`m_bonusVaultBalance`) with no payout path; loyalty tiers currently multiply from the general CD pool. Wire tier payouts from the vault.
- [ ] **Quarterly CPI mechanism** — `HEAT_PEG_USD` is a launch constant; see `docs/plans/heat-cpi-peg-quarterly.md` (PEG-Q: BLS + Truflation zkTLS, governance-free, V13).
- [ ] **Wallet donation command** — consensus supports the `TreasuryFund` burn tag; expose `donate <xfg|heat> <amount>` in wallet/RPC.
- [ ] **Legacy bond retirement** — the pre-2026 bond claim path stays functional until bonds mature, then remove.
- [ ] **Auto-roll (v13)** — CD auto-roll/compounding, currently `#if 0` with TODO(v13) markers.
- [ ] **Maker-rebate documentation update in skill** — fuego-heat-and-hearth skill references post-v11 updates (done in this pass).
- [ ] **Block-serving RPC hang investigation** — `queryblockslite.bin` / `queryblocks.bin` / `getblocks.bin` hang on every binary (pre-existing, blocks live sync/send e2e only).

## Wallet / UX

- [ ] **Mint slippage tolerance** — wallet TWAP quote vs consensus validation can reject on fast moves; add tolerance or re-quote.
- [ ] **TWAP-over-RPC** — RPC-mode wallets currently fall back to pool spot (two-step floor); expose the rolling TWAP endpoint and use it.
- [ ] **Per-epoch interest display** — show accrued-so-far / epoch rates / maturity in wallet + RPC.
- [ ] **LP status in wallet** — deposit/withdraw UX parity with the RPC path.

## RPC / observability

- [ ] **`/heat_metrics` consumer updates** — TUI and swapd still parse pre-v11 fields (spot_price scale, treasury_swap_fee_xfg removed); refresh consumers to the new fields (`total_burned_xfg`, `treasury_counter_xfg`, etc.).
- [ ] **`m_bonusVaultBalance` pre-v11 XFG migration note** — the counter was XFG pre-v11 and is HEAT post-v11; a payout path must handle the legacy component.
- [ ] **`m_lastTwapVersion` snapshot restore** — defense-in-depth: persist/restore across reorgs (rolling-window clear is not pop-reversible — pre-existing pattern).

## Tests / CI

- [ ] **Full-consensus integration tests** — epoch split → convert → burn → claim → reorg on both testnet/mainnet modes (blocked partially by the RPC hang; harness work).
- [ ] **`tests/CMakeLists.txt` wiring** — gtest-based CoreTests (HeatMintTest, FeePoolInterestCapTest, AmmPoolTest, FixedPointTest, TestCurrency) still not in the build; wire a gtest target or port to the custom harness.
- [ ] **SwapOrderbookTests defaults fix** — 4/61 pre-existing failures from uninitialized `SwapOrder` defaults.
- [ ] **Un-`#if 0` Phase5 adversarial tests** — rewrite for the auction model, or retire the file.

## Docs

- [ ] **`DEPOSIT_ARCHITECTURE.md` refresh** — still describes pre-v11 deposit/treasury flows; sync with the re-gated v11 model.
- [ ] **fuego-currency / fuego-swaps skills** — check for stale fee-split and orderbook references beyond the hearth skill.

## Notes

- `ammMintLpShares` single-sided branches removed (return 0); keep the formula audit in mind if LP economics are revisited.
- The burn tally (`total_burned_xfg`) tracks 100% pre-split amounts; EF bucket is the 50% share — don't re-mix these in reporting.
- Watchdog (`/tmp/watchdog_bc.sh`) auto-restores `Blockchain.cpp` from `/tmp/bc_known_good.cpp` if the phantom truncator returns — keep both files around until the root cause is found.
