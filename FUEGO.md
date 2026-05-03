# FUEGO.md - Comprehensive Agent Reference

Fuego is a privacy-focused blockchain with Certificate of Deposit (CD) yield, atomic swaps, and the DIGM music platform.

---

## Architecture Overview

```
/src/
├── CryptoNoteCore/      # Core blockchain logic (90 files)
│   ├── Currency.h/cpp         # CD interest calculations
│   ├── CommitmentIndex.h/cpp  # Fee pool tracking (80/20 split)
│   ├── BankingIndex.h/cpp    # Deposit tracking by block
│   ├── BlockChain.h/cpp    # Main blockchain state
│   └── Transaction.h/cpp   # Transaction types
├── crypto/            # Cryptographic primitives (38 files)
│   ├── crypto.h/cpp         # Ed25519 keys, signatures
│   ├── mlsag.h/cpp       # Ring signatures
│   ├── pedersen.h/cpp     # Pedersen commitments
│   └── musig2.h/cpp    # MuSig2 (atomic swaps)
├── P2p/              # P2P networking (30 files)
│   ├── P2pProtocolDefinitions.h  # Commands 1001-1015
│   └── P2pConnections.h/cpp # Peer management
├── SwapDaemon/        # Atomic swaps (69 files)
│   ├── SwapTypes.h         # Swap states, fee rates
│   └── SwapStateMachine.cpp # State transitions
├── Miner/             # Mining (10 files)
├── Daemon/             # Node daemon (3 files)
└── WalletLegacy/       # CLI wallet

/swapxfg/              # Atomic swap CLI daemon
/digm-platform/       # Music platform integration
│   └── fuego-core/    # Embedded fuego core
/tui/                 # Terminal UI wallet
```

---

## CD Interest Calculation

**Key Source:** `src/CryptoNoteCore/Currency.cpp:calculateCdInterest()`

**Formula:**
```
interest = amount × Σ (epoch_fee_rate[i] / total_locked_cd[i])
```
where i ranges from creation_epoch to current_epoch

**APY Estimate:**
```
APY = (0.8 × epoch_swap_fees × 73) / total_cd_locked × 100%
```

**Epoch Parameters:**
- Duration: 900 blocks (~5 days)
- Min term: 16440 blocks (3 months)
- Fee split: 80% CD holders, 20% treasury

**Key Files:**
| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/Currency.cpp` | `calculateCdInterest()` entry point |
| `src/CryptoNoteCore/CommitmentIndex.cpp` | `recordEpochFeeRate()` |
| `src/CryptoNoteCore/BankingIndex.cpp` | `depositAmountAtHeight()` |

---

## Atomic Swap Mechanics

**Swap States (active states):**
```
ADAPTOR_KEYS_EXCHANGED = 10    # Keys distributed
ADAPTOR_ESCROW_FUNDED = 11    # XFG locked
ADAPTOR_PRESIGS_READY = 12    # Presignatures ready
ADAPTOR_CTR_LOCKED = 13       # Counterparty locked
ADAPTOR_SECRET_REVEALED = 14  # Secret revealed
ADAPTOR_XFG_SPENT = 15       # XFG claimed
ADAPTOR_REFUNDED = 16         # Refund on timeout
```

**Swap Pairs:** SOL=0, ETH=1, XMR=2, BCH=3

**Fee:** 1% of claim/refund amount (100 bps)

**Key Files:**
| File | Purpose |
|------|---------|
| `src/SwapDaemon/SwapTypes.h` | State enum, fee constants |
| `src/SwapDaemon/SwapStateMachine.cpp` | State transitions |
| `src/crypto/musig2.h` | MuSig2 adaptor signatures |

---

## P2P Protocol

**Command IDs:**
| ID | Command | Purpose |
|----|---------|---------|
| 1001 | COMMAND_HANDSHAKE | Node handshake + peers |
| 1002 | COMMAND_TIMED_SYNC | Time sync + height |
| 1003 | COMMAND_PING | Connection alive |
| 1013 | COMMAND_SWAP_OFFER | Swap offer gossip |
| 1014 | COMMAND_SWAP_CANCEL | Cancel offer |
| 1015 | COMMAND_SWAP_TRADE | Completed swap |

**Network Config:**
- Target connections: 8
- Max packet size: configurable
- Handshake interval: configurable

---

## Cryptographic Primitives

| Algorithm | File | Purpose |
|-----------|------|---------|
| Ed25519 | `crypto/crypto.h` | Key generation, signing |
| MLSAG | `crypto/mlsag.h` | Ring signatures (RingCT) |
| Pedersen | `crypto/pedersen.h` | Commitment scheme |
| MuSig2 | `crypto/musig2.h` | Aggregated keys (swaps) |
| DLEQ | `crypto/dleq.h` | Discrete log equality |
| ChaCha8 | `crypto/chacha8.h` | Deposit secret encryption |
| CN-FastHash | `crypto/hash.h` | Primary hash function |

---

## Configuration Constants

From `src/CryptoNoteConfig.h`:

| Constant | Value | Meaning |
|----------|-------|---------|
| `CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX` | 1753191 | "fire" prefix |
| `MONEY_SUPPLY` | 80000088000008 | 8M8 max supply |
| `COIN` | 10000000 | 10^7 atomic units |
| `DIFFICULTY_TARGET` | 480 | 8 minutes/block |
| `EPOCH_DURATION_BLOCKS` | 900 | ~5 days |
| `SWAP_FEE_RATE_BPS` | 100 | 1% swap fee |
| `SWAP_FEE_CD_SHARE_PCT` | 80 | 80%→CD pool |
| `DEPOSIT_MIN_TERM` | 16440 | 3 months |
| `MINIMUM_FEE` | 8000 | 0.0008 XFG |

---

## DIGM Platform Outline

**Purpose:** Music platform integration with Fuego blockchain

**Key Integration Points:**
- `digm-platform/fuego-core/` - Embedded Fuego core library
- Rewards for music streaming, interactions
- Artist tipping via blockchain transactions

**Note:** For deep diving into DIGM, load `fuego-blockchain-specialist` skill for mechanics analysis.

---

## Code Search Reference

| Need | Method |
|------|--------|
| Find function | Load `fuego-codebase-mapper` skill, use `search_functions` |
| Find file | Load `fuego-codebase-mapper` skill, use `search_files` |
| Understand CD logic | Load `fuego-blockchain-specialist` skill |
| Analyze swap mechanics | Load `fuego-blockchain-specialist` skill |
| Search by keywords | Load `fuego-rag` skill |
| Extract formulas | Load `fuego-code-analyzer` skill |
| Navigate deps | Read `.dsp/` directory |

---

## Skills Reference

**Multi-Agent System:**

Load `fuego-orchestrator` - Central router that auto-analyzes queries and routes to specialized domain agents.

**Specialized Agents:**
| Agent | Scope | Triggers |
|-------|-------|----------|
| `fuego-currency` | CD interest, deposits, fee pool | "interest", "APY", "deposit", "yield" |
| `fuego-swaps` | Atomic swaps, LP pools | "swap", "atomic swap", "LP pool" |
| `fuego-crypto` | Ed25519, MLSAG, Pedersen, MuSig2 | "crypto", "ed25519", "signature" |
| `fuego-network` | P2P protocol, peers | "p2p", "peer", "network" |
| `fuego-tx` | Transactions, RingCT | "transaction", "ringct" |
| `fuego-wallet` | Wallet operations | "wallet", "address", "balance" |
| `fuego-miner` | Mining, difficulty | "mine", "difficulty", "hashrate" |

**Utilities (called by agents):**
- `fuego-rag` - Semantic code search
- `fuego-codebase-mapper` - File/function search

**Parallel Mode:** Multi-domain queries activate multiple agents simultaneously
**Sequential Mode:** Single domain queries use one agent (lowest token usage)

---

## Build & Test

**Build:**
```bash
make  # → binaries in build/release/src/
```

**Run daemon:**
```bash
./build/release/src/fuegodd
```

**Run wallet:**
```bash
./build/release/src/SimpleWallet
```

---

## Important Notes

1. **Source of truth:** Code in `src/` directory
2. **Skills enhance, never replace:** Always verify against source
3. **Config changes:** Primary config in `src/CryptoNoteConfig.h`
4. **Privacy:** RingCT with MLSAG, Pedersen commitments enabled
5. **Fee handling:** 1% swap fee → 80% CD pool, 20% treasury

---

*Last updated: 2026-04-26*
*Source: src/CryptoNoteConfig.h, src/CryptoNoteCore/, src/SwapDaemon/, src/crypto/*