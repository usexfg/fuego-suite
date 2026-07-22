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

### Hearth Exchange (block-discrete on-chain CLOB/AMM hybrid)
- Batch-clearing once per block at volume-weighted average price (VWAP)
- Two-phase: match → settle (Phase 1 fully implemented)
- Adaptive-spread pool liquidity band: HEARTH AMM reserves injected as limit orders on both sides, regenerated each block
- Spread: base 30 bps, adapts to volatility and band consumption, capped at 300 bps
- `PoolOrderOrchestrator` decides regeneration timing (price delta, reserve delta, consumption rate)
- User orders take priority over pool orders at same price level (last-in-time for pool)
- `MIN_DISTINCT_PARTIES = 2` anti-manipulation floor
- Bootstrap window: 144 blocks of AMM-only operation after upgrade

### HEAT Flatcoin (colored coin)
- Not fiat based, peg targets purchasing power only; adjusts for inflation/CPI
- Mint by burning XFG at hearth pool price with 0% launch premium (hardcoded, PI controller removed)
- Launch ratio: 1 HEAT = 10 XFG (10:1 XFG per HEAT)
- Peg reference: $1.58 USD (HEAT_PEG_USD)  adjusted for inflation starting Q1|2009 1 USD 
- Certificates of Deposit (CD): V12 activation, epoch-term-locked HEAT earning protocol revenue yield

### Hearth Exchange — Data Flow Per Block
1. `OrderbookMempool::expireOrders()`
2. `PoolOrderOrchestrator`: record price, decide regeneration, compute adaptive spread
3. `generatePoolOrders()` — new band from AMM reserves, replace pool orders
4. Clone mempool → `OrderbookIndex` (snapshot)
5. `OrderbookMatcher::match()` — batch match, compute VWAP P_clear, filter, enforce parties minimum
6. Write P_clear + depth stats to block header

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
- Boost 1.86 max (io_service compat), ARM64 uses `boost@1.85` via Homebrew
- External dependency: `secp256k1` (submodule at `external/secp256k1`)
- No KDF/MM2/AtomicDEX integration (removed)
- TUI disabled in CMake (`add_subdirectory(tui)` commented out), built separately via `make build-tui`

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

## OKOC Skill Chain

When working in this repo, the default skill chain auto-loads:
1. `okoc` — behavioral baseline
2. `graphify` — codebase navigation
3. `fuego-guardian` — recursive multi-agent code verification (auto on C++/crypto changes)
4. `007` — security audit (auto on sensitive operations)
5. `prompt-library` — persona generation
6. `code-reviewer` — code quality
