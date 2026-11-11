# Fuego Suite v1.11.00 — HEATWAVE Release

| | |
|---|---|
| **Codename** | HEATWAVE |
| **Block Version** | `BLOCK_MAJOR_VERSION_11` |
| **Activation Height** | 1,111,111 |
| **Release Version** | v1.11.00 |
| **Testnet Activation** | Block 25 |
| **Previous** | v1.10.02 Wildfire (height 1,003,000) |
| **Next** | v1.12.00 SILENTFIRE (height 2,666,666) — HEAT CDs + unified outputs |

HEAT flatcoin, the Hearth on-chain AMM, the Fuego Cost Index, treasury LP
management, a fee-routing rework, and a security pass closing the
treasury-vault drain primitive.

---

## HEAT Flatcoin (colored coin)

- HEAT targets purchasing power rather than a fiat peg, and it adjusts for inflation and CPI.
- Minting burns XFG at the Hearth pool TWAP price. The launch ratio is 1 HEAT = 10 XFG.
- From v11, every mint carries a 1% premium (`HEAT_MINT_PREMIUM_BPS = 100`). The premium is a haircut inside the declared burn, not a surcharge: the withheld premium XFG is credited to the AuntPoL XFG leg (`m_treasuryLpPendingXfg`) and is not burned, while the HEAT it buys is minted to the AuntPoL HEAT leg (`m_treasuryHeatReserve`). Total HEAT created still equals the HEAT value of the full burn.
- The launch premium is otherwise 0%, and the PI controller has been removed.
- The Hearthstone reference is $1.58 USD at launch (`FCI_HEARTHSTONE_INITIAL_CENTS = 158`), re-based quarterly from the on-chain Fuego Cost Index.

---

## Hearth Exchange (call auction + AMM)

- One inventory, two uses: constant-product AMM (`reserveXfg × reserveHeat`) for
  direct swaps and the post-auction backstop; the same reserves quoted as
  depth-band makers in the per-block call auction. Not one or the other.
- User limits are tx-extra-backed `TransactionExtraLimitDeposit` commitments (no
  gossip, no unbacked orders). Partial fills credit `proceedsXfg`/`proceedsHeat`,
  claimable via `TransactionExtraLimitWithdraw`.
- Per block after bootstrap: regenerate pool depth-band orders, run the call
  auction (user deposits + pool makers), then fill leftover user deposits on
  the AMM curve (`HEARTH_BACKSTOP_MAX_BPS` cap).
- Pool depth band is executed: `generatePoolOrders()` 0xF0-prefixed makers
  (adaptive spread 30–300 bps, `HEARTH_DEPTH_BAND_PCT` of reserves). Auction
  fills of those ids move pool reserves. They are not user escrow.
- Height-based expiry; expired orders keep deposit + proceeds claimable.
- Balanced LP deposits only (single-sided mints no shares — no dilution).
- The taker pays a 1% fee (`HEARTH_FEE_BPS = 100`). The split is versioned so that reorg replay recomputes the split the block settled. At v11, 30% accrues in the HEAT-denominated `cdHearthFeeAccumulator` and is minted into `CD_APY_POOL` at the epoch boundary, while 70% is the maker rebate paid to auction makers (or to LPs when the pool is the counterparty). At v12 the split flips to 70% CD and 30% maker.
- Consumed XFG is burned 50/50 between the Eternal Flame and the SWF.
- The bootstrap window runs 144 blocks of AMM-only operation after the upgrade.
- The **Treasury LP Manager (AuntPoL)** maintains the protocol's own ratio-paired LP position. **Both legs are funded by the 1% mint premium**: the withheld premium XFG is the XFG leg, and the HEAT it buys is the HEAT leg (plus HEAT donations). The two legs are never cross-converted, and no SWF transfer is involved. The position compounds and drives bootstrap repayment through the two-leg owned-reserves check.

---

## Fuego Cost Index (FCI)

Miner-voted consumer basket, priced in integer cents (power in milli-USD/kWh).

- Six categories: power, gas, rice, milk, bread, eggs.
- Weights: power 30, gas 25, rice 15, milk 10, bread 10, eggs 10 (sum 100).
- Baselines (cents): power 120 milli-USD/kWh, gas 350, rice 50, milk 100, bread 250, eggs 300.
- Miners vote via `TX_EXTRA_MINER_BASKET_VOTE`; trimmed median per category.
- Quarterly re-base gate: 3 of 6 categories must clear quorum within the trailing
  quarter (`FCI_MIN_FRESH_QUORUM = 3`); 5.04% max quarterly move.
- Per-category last-quorum epochs carried in the reorg snapshot.
- Rice appended as mask bit 5 (bits 0–4 unchanged; five-category votes serialize
  byte-identically to pre-rice format).
- Oracle tool: `tools/fci-oracle/` (BLS public API only).
- Miner how-to: `docs/developer/fuego-cost-index.mdx`.

---

## Certificates of Deposit (CD) — v12 activation

- V12 activation (`CD_ACTIVATION_HEIGHT = 2,666,666`) introduces epoch-term-locked HEAT that earns protocol revenue yield.
- From v11, atomic swap fees split 80% to the CD Yield Pool, 20% to the Bonus Vault, and 0% to the Treasury. Blocks before v11 settled 69% CD, 11% Bonus Vault, and 20% Treasury, and reorg replay keeps that split. The treasury leg was retired because the Treasury LP Manager is now funded by the mint premium itself — the withheld premium XFG is the XFG leg and the HEAT it buys is the HEAT leg — rather than by a swap-fee cut.
- The Bonus Vault pays tier-based loyalty bonuses of 2.5× for 72 epochs, 2× for 36 epochs, 1.5× for 18 epochs, and 1.25× for 6 epochs.
- There are exactly two protocol earnings streams: the Hearth swap fee (1% taker, versioned 30/70 at v11 and 70/30 at v12) and the atomic swap fee (1% + 1% = 2%, split 80/20 at v11+).
- The 1% mint premium is not revenue. It is a haircut inside the mint burn that funds both AuntPoL legs: the withheld XFG is the XFG leg and the HEAT it buys is the HEAT leg.
- The 0.1% CD creation fee is not revenue either. It is charged in XFG as an output to `FUEGO_DEV_FUND_ADDRESS` when the CD is created.

---

## Public Audit API

- Typed `Blockchain::getPublicAuditSnapshot()` reading all counters/partitions/pool/tip
  under one state lock.
- RPC: `COMMAND_RPC_GET_PUBLIC_AUDIT` / `/get_public_audit` (public read, no API key).
- Gateway: `GET /v1/node/audit/balances` proxy; OpenAPI specs updated.
- SDK method added to all 10 client packages (JS, Python, Rust hand-written + 7
  generated). All monetary amounts are decimal strings (no 2^53 precision loss).
- `public_audit_tests`: 5/5 passed.

---

## Security

### Critical fix — R2-1 (treasury vault drain)
- Treasury vault spend key was derivable from public data (`hash_to_scalar(keccak(genesisHash ‖ VAULT_KEY_SEED))`).
- Fixed: `pushTransaction` now rejects any transaction naming a vault key image
  unconditionally; vault operations continue via internal `spendUtxos`/`mintVaultChangeUtxo`.
- Vault non-drainable via transactions.

### Audit round 1 + 2 (all CRITICAL/HIGH/MEDIUM fixed)
- H-1: uncaught `boost::bad_get` in `pushTransaction` rollback → remote daemon crash (FIXED + 12 tests)
- M-1: ignored `pushTransaction` return, partial connect state (FIXED)
- M-2: `SwapEscrow` key image missing Ed25519 domain check (FIXED)
- M-3: path traversal in `exportWallet`/`exportWalletKeys` (FIXED)
- L-2: ChaCha8 → ChaCha20-Poly1305 for swap secret-at-rest (FIXED, v1 read path retained)
- L-3: Rust `AdaptorSecret` `Debug` removed (FIXED)
- L-5: wallet key buffers wiped with store the optimizer must keep (FIXED)
- I-1: dead OAES exports deleted (FIXED)
- R2-1 CRITICAL: vault spend via tx rejected (FIXED)

### Deferred (LOW/INFO, documented)
- L-2b: wallet-file cipher upgrade (container format change, deferred by owner)
- R2-3: epoch fee-split integer dust (floor division; still present in the 80/20 v11 split)
- R2-5: early return from mutating epoch work
- I-2: `popen("which tor")` PATH-hijack caveat (optional)
- I-5: full epoch-mint dual paths (deferred to round 3)

### False positives (do not "fix")
- `sc_check`/`signum`/`sc_mulsub` timing claims rejected against source.
- Dev-fund view secret in source is view-only (cannot spend); classified as a
  transparency feature, not a vulnerability.

---

## Cross-Chain Atomic Swaps (SwapXFG) — continued

- SwapXFG TUI adopts the swap daemon's dynamic pair catalog (46 ids: 25 ACTIVE,
  17 ADAPTER, 4 STAGED) with per-pair readiness guards; legacy 7-pair table is
  fallback when swapd is unreachable.
- Gate A: local escrow funding sufficiency check (fails closed).
- Headless daemon mode (`--service`) + loopback JSON status endpoint.
- Adaptor-secret at-rest encryption upgraded to ChaCha20-Poly1305.
- Rust `AdaptorSecret` no longer derives `Debug`.

---

## Privacy

- Dandelion++ stem-fluff transaction relay (continued from v10).
- Alias registration privacy (per-tx sub-address rotation, ring mixin).

---

## Infrastructure & tooling

- `IndexManager` delegates blockchain indices; async `rebuildCache`.
- FCI oracle tool (`tools/fci-oracle/`, BLS public API only).
- MCP server (`mcp/fuego-mcp-server/`) for AI-agent integration.
- Solidity `PointTimelock` contract (15/15 tests pass).
- Docs: FCI reference + miner how-to (`docs/developer/fuego-cost-index.mdx`).

---

## Build & test summary

- C++ daemon: builds clean; ctest 53/55 (2 pre-existing live-e2e harnesses).
- Go: tui, swapxfg, dashboard — build + vet + test clean.
- Rust adaptor: 6/6 tests pass.
- Solidity: 15/15 tests pass.
- MCP: builds (tsc exit 0).

---

## Commit scope

Major areas since v1.10.02: HEAT + Hearth AMM, FCI (six categories, quarterly gate),
treasury LP manager, fee-routing rework (1% mint premium funds both AuntPoL legs,
80/20 atomic swap split, versioned Hearth 30/70 split,
CD creation fee in XFG to the dev fund), public audit API, R2-1 vault fix, swap TUI
dynamic catalog, FCI oracle, MCP server, security audit remediation.

*Previous: v1.10.02 Wildfire (height 1,003,000)*
*Next: v1.12.00 SILENTFIRE (height 2,666,666) — HEAT CDs + unified outputs + hidden amounts*
