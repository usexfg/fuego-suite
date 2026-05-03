# Fuego Blockchain - Agent Context

## Quick Reference

**Build:** `make` (from root) → binaries in `build/release/src/`

**Key Paths:**
- `/src/CryptoNoteCore/` - Core blockchain, CD interest, deposits
- `/src/SwapDaemon/` - Atomic swaps (SOL, ETH, XMR, BCH)
- `/src/crypto/` - Ed25519, MLSAG, MuSig2, Pedersen
- `/src/P2p/` - P2P networking (commands 1001-1015)
- `/swapxfg/` - CLI swap daemon
- `/digm-platform/` - Music platform integration

**Config Constants (src/CryptoNoteConfig.h):**
- `CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX = 1753191` ("fire")
- `MONEY_SUPPLY = 80000088000008` (8M8 max supply)
- `COIN = 10000000` (10^7 atomic units)
- `DIFFICULTY_TARGET = 480` (seconds per block)
- `EPOCH_DURATION_BLOCKS = 900` (5 days)
- `SWAP_FEE_RATE_BPS = 100` (1% swap fee)
- `SWAP_FEE_CD_SHARE_PCT = 80` (80%→CD, 20%→treasury)
- `DEPOSIT_MIN_TERM = 16440` (3 months)
- `MINIMUM_FEE = 8000` (0.0008 XFG)

**How to Use This Memory**

| Task | Action |
|------|-------|
| General fuego query | **Load `fuego-orchestrator`** - auto-routes to specialist |
| CD interest / deposits | Load `fuego-currency` skill |
| Atomic swaps / LP pools | Load `fuego-swaps` skill |
| Crypto primitives | Load `fuego-crypto` skill |
| P2P network | Load `fuego-network` skill |
| Transactions | Load `fuego-tx` skill |
| Wallet operations | Load `fuego-wallet` skill |
| Mining / difficulty | Load `fuego-miner` skill |
| Find files / functions | Load `fuego-codebase-mapper` utility |
| Code search / docs | Load `fuego-rag` utility |

**Multi-Agent System:**
Load `fuego-orchestrator` - Central router that auto-analyzes queries and routes to specialized domain agents.

**All Skills:**
- `fuego-orchestrator` - Router + aggregation
- `fuego-currency` - CD interest, deposits, fee pool
- `fuego-swaps` - Atomic swaps, LP pools
- `fuego-crypto` - Ed25519, MLSAG, Pedersen, MuSig2
- `fuego-network` - P2P protocol, peers
- `fuego-tx` - Transactions, RingCT
- `fuego-wallet` - Wallet, addresses, keys
- `fuego-miner` - Mining, difficulty
- `fuego-rag` - Semantic code search (utility)
- `fuego-codebase-mapper` - File/function search (utility)

**Note:** Code in `src/` is the source of truth. Use skills to enhance understanding, but verify against source.