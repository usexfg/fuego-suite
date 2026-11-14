# AGENTS.md — Fuego Suite

## Project Identity

- **Name**: Fuego (XFG) / Fuego Suite
- **Repository**: `github.com/usexfg/fuego-suite`
- **Language**: C++17 (daemon), Go (TUI), Rust (adaptor sigs, swap daemon)
- **Build**: CMake 3.16+, Make, Go 1.24+
- **Protocol**: CryptoNote (RingCT, ring signatures, subaddresses)

## Architecture

```
fuego-suite/
├── src/CryptoNoteCore/     # Consensus, blockchain, HEAT mint, Hearth orderbook
├── src/crypto/             # MLSAG, Pedersen, MuSig2, DLEQ, adaptor sigs
├── src/P2p/                # P2P networking, Levin protocol
├── src/Rpc/                # JSON-RPC server
├── src/Wallet*/            # CLI wallet (fire_wallet)
├── src/SwapDaemon/         # Cross-chain atomic swap daemon
├── src/SimpleWallet/       # Wallet launcher + swapxfg TUI launcher
├── tui/                    # Go TUI (fuego_suite terminal UI)
├── fuego-swapd-adaptor/    # Rust — adaptor signature lib (Schnorr+MuSig2+DLEQ)
├── contracts/              # Solidity/Rust HTLC contracts
├── mcp/                    # MCP server for AI-agent integration
├── docs/                   # Documentation (Mintlify MDX)
└── graphify-out/           # Knowledge graph (auto-generated)
```

## Key Subsystems

### Hearth Exchange (call auction + AMM, v11+)
- **Both mechanisms share one inventory.** `m_ammPool` is the constant-product balance sheet (`reserveXfg × reserveHeat`). Direct AMM swaps and the post-auction backstop price on that curve. The depth band is not a second book: it is the pool quoting that same inventory as makers in the call auction.
- **User limits** are tx-extra-backed `TransactionExtraLimitDeposit` commitments (no gossip, no unbacked orders). Partial fills credit `proceedsXfg/proceedsHeat`, claimable via `TransactionExtraLimitWithdraw`. Expiry is height-based; expired orders keep deposit + proceeds claimable.
- **Per block (after 144-block AMM-only bootstrap):** (1) regenerate pool depth-band orders from reserves, (2) call auction of user deposits **plus** those pool orders, (3) AMM backstop fills leftover user deposits on the constant-product curve, volume-capped.
- **Pool depth band is real.** `generatePoolOrders()` builds 0xF0-prefixed makers (adaptive spread 30–300 bps, `HEARTH_DEPTH_BAND_PCT` of reserves). Auction fills with `orderId.data[0] == 0xF0` move `reserveXfg`/`reserveHeat`. They are not user escrow (`m_limitDeposits`); user orders win same-price ties.
- **Balanced LP deposits only** — single-sided deposits mint no shares (no dilution); shares are fair pro-rata at the pool ratio
- Taker pays the 1% fee; the split is versioned (`hearthCdSharePct`): **v11 pays 30% CD / 70% maker**, **v12 flips to 70% CD / 30% maker**. The CD share is debited into the HEAT-denominated `cdHearthFeeAccumulator` → minted into `CD_APY_POOL` at the epoch boundary; the maker share is the rebate (auction makers, or LPs on pool fills); consumed XFG burned 50/50 (EF/SWF)
- The gossiped in-band CLOB matcher, `MIN_DISTINCT_PARTIES`, and unbacked orders are **retired**
- **Treasury LP Manager (AuntPoL)**: the protocol's own ratio-paired LP position. **Both legs are funded by the 1% mint premium**: the withheld premium XFG is the **XFG leg** and the HEAT it buys is the **HEAT leg** (plus HEAT donations). The two pair at the pool ratio; no SWF transfer and no cross-conversion. Compounds and drives bootstrap repayment (two-leg owned-reserves check)

### HEAT Flatcoin (colored coin)
- Not fiat based, peg targets purchasing power only; adjusts for inflation/CPI
- Mint by burning XFG at hearth pool price. No launch premium (PI controller removed); from v11 a 1% **mint premium** (`HEAT_MINT_PREMIUM_BPS = 100`) is a haircut inside the burn that mints the AuntPoL HEAT leg
- Launch ratio: 1 HEAT = 8 XFG (8:1 XFG per HEAT); genesis Hearth seed 8,000 XFG / 1,000 HEAT
- Hearthstone reference: $1.58 USD at launch (`FCI_HEARTHSTONE_INITIAL_CENTS = 158`), re-based quarterly from the on-chain Fuego Cost Index (see below)
- Fuego Cost Index (FCI): miners vote a 6-item consumer basket in integer cents (power, gas, rice, milk, bread, eggs) via `TX_EXTRA_MINER_BASKET_VOTE`; trimmed median per category, 3-of-6 fresh-quorum gate per quarter, 5.04% max quarterly move
- Certificates of Deposit (CD): V12 activation, epoch-term-locked HEAT earning protocol revenue yield
- **SWAP FEE ROUTING (v11+)**: 80% CD Yield Pool / 20% Bonus Vault / 0% Treasury. Pre-v11 blocks settled 69/11/20 and reorg replay keeps that split (`SWAP_FEE_*_PRE_V11`).
- **BONUS VAULT**: 20% of swap fees (v11+). Pays only the yield-floor top-up: in an epoch whose CD yield is below `CD_YIELD_FLOOR_RATE`, it covers the shortfall, limited by its balance. No term tiers or multipliers.
- **DEPOSIT MODEL**: See `DEPOSIT_ARCHITECTURE.md` for full details. Key points:
  - "HEAT" has TWO meanings in code: current flatcoin (dynamic TWAP pricing) vs legacy Embers_Heat (fixed 10M:1 ratio)
  - **Two-step process**: burn XFG → mint HEAT → deposit HEAT as CD (you CANNOT deposit a deposit)
  - Legacy 10M:1 `convertXfgToHeat()`/`convertHeatToXfg()` removed from the codebase — do not reintroduce
  - `mintHeatV10()` uses Hearth pool TWAP — this is ACTUAL mint pricing (canonical scale HEAT/XFG × COIN)
  - **DEPRECATED (DO NOT REINTRODUCE)**: COLD deposits (0xCD tag), Embers_Heat deposits, XFG deposits
  - **GUARDRAILS**: APY from protocol fees (not inflation), deposit secret stored locally, different term lengths don't hurt privacy

### Protocol Earnings (currently two — more are welcome)
These are the streams that exist **today**, not a closed list. New revenue streams
are actively wanted: if you can see one, propose it. What follows are the two
implemented, and then the hard rules on what may never fund yield.

1. **Hearth swap fee — 1% taker fee** (`HEARTH_FEE_BPS = 100`), on-chain Hearth fills. Split **30% → CD yield / 70% → maker rebate** at v11 (auction makers; the pool/LPs when it is the counterparty). **v12 flips to 70% CD / 30% maker.**
2. **Atomic swap fee — 1% + 1% = 2% per cross-chain swap** (`SWAP_FEE_RATE_BPS = 100` initiation + claim), via SwapXFG TUI or DeXFG tab in Fuego-Wallet GUI. Split **80% CD Yield / 20% Bonus Vault** at v11+ (pre-v11 replay: 69/11/20, treasury leg now 0).

**Hard rule — what may NEVER fund CD yield or any interest payment:**
- **Burn-derived value.** A mint burn is already fully claimed by the HEAT issued
  against it, so paying yield out of it issues a second claim on spent collateral.
  This covers `bankedBurn`, the Eternal Flame share, and the SWF burn tally.
- **The CD creation fee.** It is a dev-fund payment, not revenue.
- **Inflation.** Yield comes from fees earned, never from minting HEAT that nothing
  backs. A new stream must be value someone actually paid the protocol.

Any genuinely new stream is fine — and should be considered — provided it clears
that bar: real value received, with no HEAT claim already issued against it.

NOT protocol earnings:
- **Mint premium**: 1% of the declared burn (`HEAT_MINT_PREMIUM_BPS = 100`, v11+). It is a haircut **inside** the burn, not a surcharge: the withheld `premiumXfg` is the **AuntPoL XFG leg** (`m_treasuryLpPendingXfg`, not burned), and the `heat(premiumXfg)` it buys is the **AuntPoL HEAT leg** (`m_treasuryHeatReserve`). Not revenue — it is the protocol's own LP inventory.
- **CD creation fee**: 0.1% of the HEAT being deposited, charged in **XFG** as a KeyOutput to `FUEGO_DEV_FUND_ADDRESS`. NOT protocol revenue, NOT CD yield, NOT a treasury reserve credit.

### Hearth Exchange — Data Flow Per Block (v11+)
1. `PoolOrderOrchestrator`: record pool spot (volatility), decide regeneration, compute adaptive spread
2. `generatePoolOrders()` → `setPoolOrders()` — depth-band makers from AMM reserves (executed, not display-only)
3. Height-based expiry on `m_limitDeposits` (expired stay claimable, skip this block's fills)
4. Call auction: user deposits + 0xF0 pool orders; user-vs-user fills move escrow/proceeds; pool fills move `reserve*`
5. AMM backstop: unfilled user deposits vs constant-product curve at live spot, capped by `HEARTH_BACKSTOP_MAX_BPS`
6. Write P_clear + depth stats to block header; Treasury LP Manager epoch work when the epoch closes

### Cryptographic Primitives
- Ring signatures: MLSAG with OSPEAD decoy selection
- Commitments: Pedersen (RingCT)
- Key aggregation: MuSig2
- Adaptor signatures: Schnorr + discrete log tweak for atomic swaps
- DLEQ proofs for commitment verification
- Hash functions: CryptoNight, Keccak, Blake, Skein, JH, Groestl
- Curve: secp256k1 (via libsecp256k1)

## Naming Conventions

| Convention | Usage |
|-----------|-------|
| `snake_case` | Variables, functions, file names |
| `PascalCase` | Class/struct names |
| `camelCase` | Method names |
| `UPPER_SNAKE` | Constants, macros, enums |
| `m_` prefix | Class member variables |
| `COMMAND_RPC_*` | RPC request/response structs |

## Build Notes

- C++17 required, C99 for C files
- Boost 1.86+ (uses `io_context`, `executor_work_guard` — no legacy `io_service`)
- OpenSSL 3.x/4.x
- External dependency: `secp256k1` (submodule at `external/secp256k1` or system package)
- CMake flag: `-DUSE_VENDORED_SECP256K1=OFF` to link against system secp256k1
- No KDF/MM2/AtomicDEX integration (removed)
- TUI disabled in CMake (`add_subdirectory(tui)` commented out), built separately via `make build-tui`
- **Deposit architecture**: See `DEPOSIT_ARCHITECTURE.md` for current vs deprecated deposit systems

## CI/CD

- GitHub Actions: macOS, Ubuntu 22/24, Windows, AppImage, Docker, Termux, Raspberry Pi
- No longer building `swapxfgui` or `mm2_xfg` (removed)

## RPC Endpoints

| Path | Auth | Purpose |
|------|------|---------|
| `/json_rpc` | varies | JSON-RPC methods (balance, transfers, etc.) |
| `/heat_metrics` | open | HEAT supply, redemption price, treasury |
| `/amm_quote` | open | Quote a swap amount |
| `/amm_pool_info` | open | Pool reserves, spot price |
| `/addswapfee` | restricted | Fee configuration |

## Known Issues

- **Block-serving RPC hang (pre-existing, not DIGM-related).** Every binary tested — including the month-old Jul-10 daemon on 18180 while idle and fully synced — hangs on `queryblockslite.bin` / `queryblocks.bin` / `getblocks.bin`. This blocks live sync/send e2e testing only; verification is done against the C++ production code directly (unit/integration harnesses), not live RPC.

## Deferred Design Notes

- **CD output size (v12, option A).** `CD_MAX_OUTPUT_AMOUNT` is planned at 5,000 HEAT per CD output. It is a payout-size bound, not an economic limit: the largest HEAT bill is 500, so a larger CD would pay out in more coinbase outputs than one block should carry. Larger deposits are split across several CD outputs. Raise the bound later by adding larger denominations to `HEAT_BILL_DENOMINATIONS`, or by letting one payout span several blocks. Not needed for launch. See `docs/plans/2026-11-13-heat-cd-privacy-plan.md`, decisions P-7 and P-11.

## Graphify

- Graph: `graphify-out/` — 21,757 nodes, 35,228 edges, 2,183 communities
- Update after code changes: `graphify update .`
- Navigation: read `graphify-out/GRAPH_REPORT.md` first
- Query: `graphify query "<question>"`, `graphify path "<A>" "<B>"`

## Claude Code automations (project)

| Kind | Path |
|------|------|
| Entry | `CLAUDE.md` |
| Settings / hooks | `.claude/settings.json`, `.claude/hooks/` |
| Skills | `.claude/skills/fuego-build`, `.claude/skills/crypto-change-gate` |
| Agents | `.claude/agents/build-doctor.md`, `.claude/agents/crypto-security-reviewer.md` |
| MCP | `.mcp.json` (`fuego-mcp`, `github`) |

Hooks block edits to `.env` / wallets / keys. Build ground truth is `make -j$(nproc)`.

## Source Change Log (mandatory)

Every feature or fix that touches source code requires a `CHANGELOG.agent.md` file at the repo root. Each entry contains:
- **Feature name** and start date
- **Task list** with numbered tasks, owner (agent/model name), date, and status (TODO / IN PROGRESS / DONE)
- **Sign-off table** marking gate completion (build compiles, tests pass, all tasks done)

Agents sign off with their name and date when completing tasks. No source changes land without a corresponding task list entry. The log persists across sessions so any agent can pick up where another left off.

## OKOC Skill Chain

When working in this repo, the default skill chain auto-loads:
1. `okoc` — behavioral baseline
2. `graphify` — codebase navigation
3. `fuego-guardian` — recursive multi-agent code verification (auto on C++/crypto changes)
4. `007` — security audit (auto on sensitive operations)
5. `prompt-library` — persona generation
6. `code-reviewer` — code quality
