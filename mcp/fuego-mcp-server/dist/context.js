export const ARCHITECTURE_OVERVIEW = `# Fuego Cryptocurrency — Architecture Overview

## What Is Fuego?
Fuego (XFG) is an open-source, peer-to-peer, privacy-focused cryptocurrency built on the CryptoNote protocol.
Core value props: privacy store-of-value, native atomic swaps, on-chain yield via Commitment Deposits (CDs),
and DIGM — a music streaming platform that drives swap volume and CD yield.

## Technology Stack
- Core blockchain: C++17, CryptoNote protocol, Boost 1.86, LevelDB
- Cryptography: secp256k1 (Bitcoin Core fork), Pedersen commitments, ring signatures
- Build: CMake 3.16+, multi-platform (Linux/macOS/Windows/Android/ARM64/Termux)
- TUI / swaps: Go 1.20+, Bubble Tea (terminal UI framework)
- Wallet: C++ daemon (walletd) with JSON-RPC API + interactive CLI mode
- Cross-chain swaps: adaptor signatures (ETH, SOL, XMR, BCH)
- Tests: Google Test (gtest)

## Top-Level Directory Map
\`\`\`
fuego/
├── src/                    # All C++ source
│   ├── CryptoNoteCore/     # Blockchain core: consensus, transactions, deposits
│   ├── crypto/             # Cryptographic primitives
│   ├── P2p/                # Peer-to-peer networking
│   ├── Rpc/                # JSON-RPC server and command definitions
│   ├── Wallet/             # Wallet logic, key derivation, UTXO scanning
│   ├── SimpleWallet/       # CLI wallet binary entry point
│   ├── Miner/              # CPU mining loop
│   ├── CryptoNoteProtocol/ # Network protocol handler (block/tx relay)
│   ├── Platform/           # OS-specific abstractions (Linux/macOS/Windows/Android/FreeBSD)
│   ├── System/             # Platform abstraction layer
│   ├── Common/             # String utilities, logging helpers
│   ├── Logging/            # Centralized logging
│   ├── Serialization/      # Binary + JSON serialization
│   ├── Transfers/          # Higher-level transaction construction
│   ├── Http/               # HTTP server/client
│   ├── JsonRpcServer/      # JSON-RPC handler abstraction
│   ├── NodeRpcProxy/       # RPC client for remote node
│   ├── BlockchainExplorer/ # Block explorer indexing
│   └── PaymentGateService/ # Payment gateway integration
├── tui/                    # Go TUI (node control, wallet, Elderfier staking)
├── tui-testnet/            # Dedicated testnet TUI build
├── swapxfg/                # Go atomic swap terminal (Bubble Tea)
├── tests/                  # C++ gtest suite
├── external/               # Submodules: secp256k1, gtest, httplib
├── docs/                   # Architecture and protocol documentation
├── docker/                 # Docker build files
└── scripts/                # Build helper scripts
\`\`\`

## Key Binaries
| Binary       | Language | Purpose                                    | RPC Port |
|--------------|----------|--------------------------------------------|----------|
| fuegod       | C++      | Full node, consensus, JSON-RPC server      | 18180    |
| walletd      | C++      | Wallet RPC daemon + interactive CLI        | 18282    |
| fuego-tui    | Go       | Node/wallet control, Elderfier, Burn2Mint  | —        |
| swapxfg      | Go       | Cross-chain atomic swap trading terminal   | —        |
| fuego-miner  | C++      | Standalone CPU miner                       | —        |

## Core Architectural Patterns
1. CryptoNote protocol — ring signatures + stealth addresses (one-time keys)
2. Commitment-based Deposits — Pedersen commitments hide amounts (v10); MLSAG+BP+ planned (v11)
3. Per-amount ring selection — decoy pool segregated by amount tier (prevents amount-linkage)
4. HEAT Burn — one-way burns feed the global decoy pool and enable L2 HEAT minting
5. Adaptor signatures — trustless cross-chain atomic swaps
6. DIGM — application layer settling to L1 via Merkle anchors
7. Wallet = user identity — no separate account layer (sybil defense)
8. Elderfier consensus — staked voters approve burn2mint → STARK proof → L2 HEAT mint

## Transaction Version History
- v1-v9: Basic CryptoNote (stealth addresses, ring sigs, key images)
- v10: Commitment Deposits (locked XFG, Pedersen commitments, membership proofs, interest)
- v11 (planned): MLSAG + Bulletproofs+ + unified output type (amounts fully hidden)
`;
export const MODULE_CONTEXT = {
    CryptoNoteCore: `# Module: CryptoNoteCore (src/CryptoNoteCore/)
The heart of the blockchain. Contains consensus rules, block/transaction validation,
mempool, deposit logic, chain storage indices, and upgrade detection.

## Key Files
- Core.h/cpp                   — Main blockchain state machine, consensus rules
- Blockchain.h/cpp             — Block storage, chain validation, reorg handling
- CryptoNoteBasic.h/cpp        — Base types: Block, Transaction, AccountKeys
- Currency.h/cpp               — Coin parameters, emission schedule, fee rules
- TransactionPool.cpp          — Mempool management, fee ordering
- BlockchainIndices.h/cpp      — Fast lookup structures (by key, paymentID, etc.)
- CommitmentIndex.h/cpp        — Per-amount decoy pool (ring signature anonymity set)
- BankingIndex.h/cpp           — Deposit tracking and interest calculation
- InvestmentIndex.h/cpp        — CD statistics and queries
- ProofStructures.h/cpp        — Burn proofs, deposit proofs, DIGM data
- UpgradeDetector.h            — Hard fork detection and version negotiation
- TransactionUtils.h/cpp       — Transaction validation helpers
- TransactionExtra.h           — Extra field parsing (deposit proofs, paymentIDs, burn proofs)

## Transaction Input Types
- BaseInput                      — Coinbase/mining reward
- KeyInput                       — Regular stealth address transfer
- MultisignatureInput            — Legacy multisig (deprecated)
- TransactionInputCommitmentSpend — CD withdrawal (v10+)
- TransactionInputCommitmentTransfer — CD transfer between addresses (v11)
- TransactionInputUnified        — MLSAG-based unified input (v11, planned)

## Transaction Output Types
- KeyOutput                      — Regular stealth address output
- MultisignatureOutput           — Legacy multisig (deprecated)
- TransactionOutputCommitment    — Commitment deposit creation (v10+)
- TransactionOutputUnified       — All outputs merged, amounts hidden (v11)

## Deposit Extra Flags (tx_extra byte)
- 0x08 — COLD deposit (regular commitment deposit)
- 0xCD — HEAT burn (one-way, feeds global decoy pool)
- 0xEF — Elderfier stake (voting power collateral, 800+ XFG)
- 0xD5 — Encrypted deposit secret (wallet recovery data)
`,
    Crypto: `# Module: Crypto (src/crypto/)
All cryptographic primitives. No blockchain logic here — pure crypto math.

## Key Files
- crypto.h/cpp        — Core ops: key generation, Diffie-Hellman, ring signatures, key images
- hash.h              — Keccak/SHA3 hash functions
- pedersen.h/cpp      — Pedersen commitments: C = amount*H + mask*G
- tier_proof.h/cpp    — 1-of-4 OR membership proofs (proves amount ∈ {TIER_0, TIER_1, TIER_2, TIER_3})
- ring_signature.cpp  — Ring signature implementation (mixin support, LSAG)
- mlsag_dev_plan.md   — Multi-Layered Linkable Spontaneous Ad-hoc Group signatures (roadmap)

## Amount Tiers (Commitment Deposit Denominations)
Deposits must be in fixed denominations for ring anonymity:
- TIER_0: smallest denomination
- TIER_1, TIER_2, TIER_3: larger denominations
Each tier has its own decoy pool via CommitmentIndex.

## Key Crypto Types
\`\`\`cpp
Crypto::PublicKey        // 32-byte compressed EC point
Crypto::SecretKey        // 32-byte scalar
Crypto::KeyImage         // Unique per-spend tag preventing double-spend
Crypto::Hash             // 32-byte Keccak hash
Crypto::Signature        // (c, r) ring signature component
Crypto::EllipticCurvePoint  // Generic EC point
Crypto::EllipticCurveScalar // Generic scalar
Crypto::MembershipProof     // 1-of-4 OR proof for amount tiers
\`\`\`
`,
    P2P: `# Module: P2P (src/P2p/ + src/CryptoNoteProtocol/)
Peer-to-peer networking stack. Handles peer discovery, block/tx relay, and sync.

## Key Files
- P2p.h/cpp            — Core P2P network stack, connection management
- NetNodeCommon.h      — Network messages and event handling
- Meshtastic/          — IoT mesh network integration
- CryptoNoteProtocol/  — Protocol handler: block propagation, chain sync, peer queries

## Network Ports
- Mainnet P2P: 10808
- Testnet P2P: 20808

## Key Operations
- Peer discovery via fixed seeds + DHT-like exchange
- Block propagation: new blocks broadcast to connected peers
- Transaction relay: mempool tx gossip
- Chain sync: \`get_blocks_fast\` bulk download with sparse chain traversal
`,
    Rpc: `# Module: RPC (src/Rpc/)
JSON-RPC HTTP server for both daemon (fuegod) and wallet (walletd).

## Key Files
- CoreRpcServerCommandsDefinitions.h — All daemon RPC method structs
- RpcServer.h/cpp                    — HTTP server, request routing
- JsonRpc.h                          — JSON-RPC 2.0 protocol implementation
- JsonRpcServer/                     — Reusable JSON-RPC handler base

## Daemon RPC (fuegod, port 18180)
All requests POST to http://host:18180/json_rpc

Core queries:
- get_height             — Current blockchain height
- get_info               — Daemon status, difficulty, peer count, sync status
- get_block_header_by_hash / get_block_header_by_height
- get_blocks_fast        — Bulk block download for wallet sync
- get_transactions       — Look up transactions by hash
- get_deposits           — Query deposits by address or global index
- send_raw_transaction   — Submit a signed transaction to mempool
- get_o_indexes          — Get global output indices for ring selection

## Wallet RPC (walletd, port 18282)
- getBalance             — Total and unlocked balance
- getAddresses           — All addresses in container
- createTransaction      — Build unsigned transaction
- sendTransaction        — Sign and broadcast transaction
- getTransactionHistory  — Query by address/height range
- getDeposits            — Deposits belonging to addresses
- getUnconfirmedTransactionHashes

## TUI RPC Extensions (walletd special methods)
For Elderfier staking and Burn2Mint flow:
- create_stake_deposit        — Create elderfier_stake transaction
- register_to_enindex         — Register Elderfier ID on-chain
- get_stake_status            — Check if stake is confirmed
- get_elder_inbox             — Get pending proposals for this Elderfier
- submit_vote                 — Vote on a burn2mint proposal
- create_burn_deposit         — Create HEAT burn transaction
- request_elderfier_consensus — Request Elderfier proof for burn
- get_burn2mint_requests      — Pending burns awaiting consensus
`,
    Wallet: `# Module: Wallet (src/Wallet/ + src/SimpleWallet/)
Wallet key derivation, UTXO scanning, balance tracking, and transaction construction.

## Key Files
- Wallet.h/cpp              — Core wallet: key derivation, output scanning, balance
- WalletGreen.h/cpp         — Enhanced wallet with deposit awareness
- SimpleWallet/main.cpp     — CLI wallet binary entry point
- TransactionBuilder.h      — Ring signature construction, commitment creation
- WalletSerialization.h     — Container format, encryption

## Wallet Model
A wallet container holds one or more accounts. Each account has:
- View key pair (viewSecretKey + viewPublicKey) — scans blockchain for owned outputs
- Spend key pair (spendSecretKey + spendPublicKey) — authorizes spending

Wallet files (.w) are encrypted with ChaCha20 using the container password.

## Key Account Types
\`\`\`cpp
struct AccountPublicAddress {
  Crypto::PublicKey spendPublicKey;
  Crypto::PublicKey viewPublicKey;
};
struct AccountKeys {
  AccountPublicAddress address;
  Crypto::SecretKey spendSecretKey;
  Crypto::SecretKey viewSecretKey;
};
\`\`\`

## UTXO Scanning
The view key can derive shared secrets from all transaction outputs using ECDH.
If a derivation matches, the output belongs to this wallet. The spend key is
required to actually sign inputs (can be kept offline/cold).

## Deposit Tracking
WalletGreen additionally scans for:
- TransactionOutputCommitment outputs (v10+)
- Tracks deposit maturity (lock term in blocks)
- Calculates claimable interest from fee pool
`,
    TUI: `# Module: TUI (tui/ + tui-testnet/)
Go-based terminal UI using the Bubble Tea framework for node/wallet control and staking.

## Key Files
- tui/main.go.bubbletea     — Main TUI application (Bubble Tea model)
- tui/config.go             — Mainnet/testnet config: ports, coin units, stake thresholds
- tui/go.mod                — Go module dependencies (bubbletea, lipgloss)
- tui-testnet/              — Separate binary for testnet with testnet defaults

## Features
1. Node control — Start/stop fuegod daemon
2. Wallet management — Create wallet, get balance, send transactions
3. Elderfier staking — 3-step wizard:
   a. Create stake deposit (800+ XFG locked)
   b. Register Elderfier ID on-chain
   c. Monitor stake confirmation
4. Burn2Mint flow:
   a. Create HEAT burn transaction
   b. Wait for Elderfier consensus (voter approval)
   c. Generate STARK proof
   d. Mint HEAT on L2 (Ethereum/Solana)

## Config Constants (config.go)
\`\`\`go
const (
  DaemonPort     = 18180
  WalletPort     = 18282
  MinStakeAmount = 800   // XFG required for Elderfier status
  CoinUnit       = 1e8   // atomic units per XFG
)
\`\`\`
`,
    SwapXFG: `# Module: SwapXFG (swapxfg/)
Go-based cross-chain atomic swap terminal with real-time orderbook and charting.
Uses adaptor signatures for trustless P2P swaps (no intermediary).

## Key Files
- swapxfg/app/app.go          — Main event loop, UI state machine
- swapxfg/app/rpc.go          — Fuego daemon RPC client
- swapxfg/app/wallet.go       — Wallet RPC integration
- swapxfg/app/bridge_eth.go   — MetaMask browser bridge (HTTP server on localhost)
- swapxfg/app/bridge_sol.go   — Phantom browser bridge
- swapxfg/app/pairs.go        — Trading pair definitions
- swapxfg/app/orderbook.go    — Order matching, charting, price history
- swapxfg/app/cd_market.go    — Commitment Deposit specific orderbook

## Supported Trading Pairs
- SOL/XFG — Solana ↔ Fuego
- ETH/XFG — Ethereum ↔ Fuego
- XMR/XFG — Monero ↔ Fuego
- BCH/XFG — Bitcoin Cash ↔ Fuego
- CD/XFG  — Commitment Deposit ↔ Fuego (secondary market for CDs)

## Swap Flow (Adaptor Signatures)
1. Maker creates order with adaptor public key
2. Taker sees order, constructs adaptor signature
3. Maker verifies adaptor sig, reveals adaptor secret to claim Fuego side
4. Secret revealed on-chain allows taker to claim other-chain funds
5. Atomic: either both sides complete or neither does
`,
    Miner: `# Module: Miner (src/Miner/)
CPU mining implementation with multi-threaded nonce search.

## Key Files
- Miner.h/cpp         — Mining loop, nonce iteration, block template management
- MinerManager.h/cpp  — Thread pool for parallel mining
- MiningConfig.h/cpp  — Difficulty algorithm, block template generation

## Mining Algorithm
Fuego uses CryptoNight variant (inherited from CryptoNote protocol).
Difficulty adjusts dynamically. Block time target: ~2 minutes.

## Block Reward Emission
Defined in Currency.h. Follows a smooth emission curve typical of CryptoNote coins.
Block reward decreases as total supply approaches max supply.
`,
    SwapDaemon: `# Module: SwapDaemon (src/SwapDaemon/)
C++ backend implementing adaptor signature cryptography for atomic swaps.

## Key Files
- Crypto/             — DLEQ proofs, adaptor signature math
- Ethereum/           — ETH-specific adaptor signature construction
- Solana/             — SOL-specific adaptor signature construction
- Monero/             — XMR-specific adaptor signature construction
- BitcoinCash/        — BCH-specific adaptor signature construction
- pool_v11/           — Liquidity pool mechanism logic

## Adaptor Signature Math
An adaptor signature is a partial signature that can be completed by revealing
a secret (the adaptor). The process:
1. Alice generates key pair (x, X) where X = x*G
2. Alice creates adaptor sig: s' = partial_sign(msg, x, Y) where Y is Bob's adaptor point
3. Bob verifies s' is a valid adaptor sig for Y
4. When Alice claims funds, she publishes complete sig s, revealing y = s - s'
5. Bob uses y to claim his side — trustless atomic swap

DLEQ (Discrete Logarithm Equality) proofs ensure the adaptor key Y is
correctly formed without revealing the secret y.
`,
};
export const RPC_REFERENCE = `# Fuego RPC API Reference

## Daemon RPC (fuegod, default port 18180)
All requests are HTTP POST to \`http://host:18180/json_rpc\`
Request format: \`{"jsonrpc":"2.0","method":"METHOD","params":{...},"id":1}\`

### Chain Queries
\`\`\`
get_height
  → Response: { height: number, status: string }

get_info
  → Response: { height, difficulty, tx_count, tx_pool_size, alt_blocks_count,
                outgoing_connections_count, incoming_connections_count,
                white_peerlist_size, grey_peerlist_size, last_known_block_index,
                network_height, status }

get_block_header_by_hash
  → Params: { hash: string }
  → Response: { block_header: BlockHeader }

get_block_header_by_height
  → Params: { height: number }
  → Response: { block_header: BlockHeader }

get_blocks_fast
  → Params: { block_ids: string[] }  // sparse chain fingerprint
  → Response: { blocks: Block[], start_height, current_height, status }

get_transactions
  → Params: { txs_hashes: string[] }
  → Response: { txs_as_hex: string[], missed_tx: string[], status }

get_o_indexes
  → Params: { txid: string }
  → Response: { o_indexes: number[], status }

get_deposits
  → Params: { address?: string, from_height?: number, to_height?: number }
  → Response: { deposits: DepositInfo[], status }

send_raw_transaction
  → Params: { tx_as_hex: string }
  → Response: { status: string }
\`\`\`

### Mining
\`\`\`
get_block_template
  → Params: { wallet_address: string, reserve_size: number }
  → Response: { blocktemplate_blob: string, difficulty: number, height: number, status }

submit_block
  → Params: { blockblob: string[] }
  → Response: { status: string }
\`\`\`

---

## Wallet RPC (walletd, default port 18282)
All requests POST to \`http://host:18282/json_rpc\`

### Balance & Addresses
\`\`\`
getBalance
  → Params: { address?: string }
  → Response: { availableBalance: number, lockedAmount: number }

getAddresses
  → Response: { addresses: string[] }

createAddress
  → Response: { address: string }
\`\`\`

### Transactions
\`\`\`
createTransaction
  → Params: { transfers: [{address, amount}], fee, mixin?, unlockTime? }
  → Response: { transactionHash: string }

sendTransaction
  → Params: { transactionHash: string }
  → Response: { transactionHash: string }

getTransaction
  → Params: { transactionHash: string }
  → Response: { transaction: TransactionDetails }

getTransactionHistory
  → Params: { address?: string, firstBlockIndex?: number, blockCount?: number }
  → Response: { items: TxHistoryItem[] }

getUnconfirmedTransactionHashes
  → Params: { addresses?: string[] }
  → Response: { transactionHashes: string[] }
\`\`\`

### Deposits
\`\`\`
getDeposits
  → Params: { addresses?: string[] }
  → Response: { deposits: DepositInfo[] }
\`\`\`

---

## TUI/Elderfier RPC Extensions (walletd special methods)
\`\`\`
create_stake_deposit
  → Params: { address: string, amount: number, term: number }
  → Response: { txHash: string }

register_to_enindex
  → Params: { elderfier_id: string, address: string }
  → Response: { txHash: string }

get_stake_status
  → Params: { address: string }
  → Response: { is_confirmed: bool, confirmations: number, required: number }

get_elder_inbox
  → Params: { elderfier_id: string }
  → Response: { proposals: BurnProposal[] }

submit_vote
  → Params: { proposal_id: string, vote: bool, elderfier_id: string }
  → Response: { txHash: string }

create_burn_deposit
  → Params: { address: string, amount: number }
  → Response: { txHash: string, burn_id: string }

request_elderfier_consensus
  → Params: { burn_id: string }
  → Response: { consensus_proof: string, signatures: string[] }

get_burn2mint_requests
  → Params: { address: string }
  → Response: { requests: BurnRequest[] }
\`\`\`
`;
export const DATA_TYPES = `# Fuego Key Data Types

## Account Types (src/CryptoNoteCore/CryptoNoteBasic.h)
\`\`\`cpp
struct AccountPublicAddress {
  Crypto::PublicKey spendPublicKey;  // Controls spending outputs
  Crypto::PublicKey viewPublicKey;   // Scans blockchain for owned outputs
};

struct AccountKeys {
  AccountPublicAddress address;
  Crypto::SecretKey spendSecretKey;  // Signs transactions
  Crypto::SecretKey viewSecretKey;   // Derives shared secrets for output detection
};
\`\`\`

## Block Types (src/CryptoNoteCore/CryptoNoteBasic.h)
\`\`\`cpp
struct BlockHeader {
  uint8_t majorVersion;    // Protocol version (determines tx format rules)
  uint8_t minorVersion;
  uint32_t nonce;          // PoW nonce
  uint64_t timestamp;      // Unix timestamp
  Crypto::Hash previousBlockHash;
};

struct Block : public BlockHeader {
  ParentBlock parentBlock;                        // Merged mining parent
  Transaction baseTransaction;                    // Coinbase reward
  std::vector<Crypto::Hash> transactionHashes;   // All tx hashes in this block
};
\`\`\`

## Transaction Types (src/CryptoNoteCore/CryptoNoteBasic.h)
\`\`\`cpp
struct TransactionPrefix {
  uint8_t version;                         // 1-11, determines input/output format
  uint64_t unlockTime;                     // Block height before output spendable
  std::vector<TransactionInput> inputs;   // Variant type: Base/Key/Commitment
  std::vector<TransactionOutput> outputs; // Variant type: Key/Commitment/Unified
  std::vector<uint8_t> extra;             // Metadata: paymentID, deposit proofs
};

struct Transaction : public TransactionPrefix {
  // Signatures: one ring per input, each ring has mixin+1 components
  std::vector<std::vector<Crypto::Signature>> signatures;
};
\`\`\`

## Input Variants (src/CryptoNoteCore/CryptoNoteBasic.h)
\`\`\`cpp
struct BaseInput {
  uint32_t blockIndex;  // Coinbase: claims block reward
};

struct KeyInput {
  uint64_t amount;
  std::vector<uint32_t> outputIndexes;  // Global ring member indices
  Crypto::KeyImage keyImage;            // Prevents double-spend
};

struct TransactionInputCommitmentSpend {  // v10+: CD withdrawal
  uint64_t amount;
  std::vector<uint32_t> outputIndexes;  // Ring of commitment outputs
  Crypto::KeyImage keyImage;
  uint64_t claimedInterest;             // Interest earned from fee pool
};
\`\`\`

## Output Variants (src/CryptoNoteCore/CryptoNoteBasic.h)
\`\`\`cpp
struct KeyOutput {
  Crypto::PublicKey key;  // Stealth one-time public key
};

struct TransactionOutputCommitment {  // v10+: creates a CD
  Crypto::PublicKey commitKey;                    // Ring-sig spend key
  uint32_t term;                                  // Lock term in blocks
  Crypto::EllipticCurvePoint amountCommitment;   // C = amount*H + mask*G
  Crypto::MembershipProof amountProof;            // Proves amount in {TIER_0..TIER_3}
};

struct TransactionOutput {
  uint64_t amount;           // 0 for commitment outputs (amount is hidden)
  TransactionOutputTarget target;  // Variant: KeyOutput or TransactionOutputCommitment
};
\`\`\`

## Deposit Info (used in RPC responses)
\`\`\`cpp
struct DepositInfo {
  Crypto::Hash creatingTransactionHash;
  uint32_t outputInTransaction;       // Index within that transaction
  uint64_t amount;                    // Deposited amount
  uint64_t term;                      // Lock period in blocks
  uint64_t interest;                  // Earned interest (from fee pool)
  uint64_t height;                    // Block height when created
  bool unlocked;                      // Whether maturity reached
  uint8_t depositType;                // 0x08=COLD, 0xCD=HEAT, 0xEF=STAKE
};
\`\`\`

## Transaction Extra Fields (src/CryptoNoteCore/TransactionExtra.h)
\`\`\`cpp
// Parsed from tx.extra byte vector:
struct TransactionExtraPublicKey { Crypto::PublicKey publicKey; };
struct TransactionExtraNonce { std::vector<uint8_t> nonce; };  // paymentID embedded here
struct TransactionExtraDepositProof {
  Crypto::EllipticCurvePoint commitment;
  Crypto::EllipticCurveScalar mask;
  uint8_t depositType;  // 0x08/0xCD/0xEF
};
struct TransactionExtraBurnProof {
  Crypto::Hash burnId;
  uint64_t amount;
};
\`\`\`
`;
export const CRYPTO_EXPLAINER = `# Fuego Cryptographic Primitives

## 1. Ring Signatures (LSAG)
Used in all standard KeyInput spends. Hides which of the ring members is the actual signer.

**How it works:**
- Transaction includes references to N "ring members" (other outputs with same amount)
- One ring member is the actual output being spent; others are decoys
- Ring signature proves "signer is one of these N keys" without revealing which
- Key image (I = x * H_p(P)) uniquely identifies spent output without revealing it
- Same key image appearing twice = double spend attempt, rejected by consensus

**Parameters:**
- Mixin count ≥ 8 (per consensus rules, dynamically adjusted via OSPEAD algorithm)
- Ring size = mixin + 1 (mixin decoys + 1 real spend)

## 2. Stealth Addresses
Prevents external observers from linking outputs to the recipient's public address.

**How it works:**
- Sender generates random r, computes R = r*G (tx public key, goes in tx.extra)
- Shared secret: ss = H(r * ViewKey) = H(viewSecretKey * R) — both sides can compute
- One-time output key: P = H(ss)*G + SpendPublicKey
- Only the owner with viewSecretKey can detect: check if P matches any output
- Only the owner with spendSecretKey can spend: x = H(ss) + spendSecretKey

## 3. Pedersen Commitments (v10+ deposits)
Hides the deposited amount while still allowing consensus validation.

**Math:** C = amount * H + mask * G
- H and G are independent generator points (H = H_p(G))
- amount: the actual value being committed to (kept secret)
- mask: random blinding factor (kept secret by depositor)
- C: the commitment (publicly visible on-chain)

**Properties:**
- Hiding: C reveals nothing about amount (mask is random)
- Binding: can't change amount without changing mask (computationally infeasible)
- Homomorphic: C1 + C2 commits to amount1 + amount2 (useful for balance proofs)

## 4. Membership / Tier Proofs (1-of-4 OR proofs)
Proves a commitment contains a value in an allowed set without revealing which.

**Why needed:** CDs must be in fixed denominations (tiers) for ring anonymity.
A membership proof proves amount ∈ {TIER_0, TIER_1, TIER_2, TIER_3} without
revealing which tier.

**Construction:** Sigma protocol OR composition:
- 4 parallel Schnorr proofs, one real, three simulated
- Verifier cannot distinguish which branch was real
- Implemented in src/crypto/tier_proof.h/cpp

## 5. Adaptor Signatures (atomic swaps)
Enables trustless cross-chain swaps without a hash time-locked contract.

**Key insight:** An adaptor signature s' = s - y can be "completed" by adding y.
Publishing s reveals y = s - s'.

**Swap flow:**
1. Alice (Fuego side) creates adaptor sig s'_A for adaptor point Y = y*G
2. Bob (other chain) creates adaptor sig s'_B for same Y
3. Alice verifies s'_B, then publishes completed sig s_A on Fuego chain
4. This reveals y (since y = s_A - s'_A is visible on-chain)
5. Bob uses y to complete and publish s_B on his chain
6. Atomic: if Alice publishes s_A, Bob gets y and can claim. If not, neither claims.

**DLEQ Proofs:** Discrete Log Equality proofs ensure Alice's adaptor key Y
is consistent between the two chains (prevents Alice from using different y).

## 6. MLSAG (Planned v11)
Multi-Layered Linkable Spontaneous Ad-hoc Group signatures. Extends ring signatures
to cover all inputs + outputs in a single proof, enabling hidden amounts.

**Key improvements over v10:**
- Ring signature covers pseudo-output commitments (input side commitments)
- Balance proof: sum(input commitments) = sum(output commitments) + fee commitment
- No fixed denominations needed — any amount works
- All outputs become indistinguishable (no amount leakage)
- Combined with Bulletproofs+ for compact range proofs (amount ≥ 0 without overflow)

## 7. OSPEAD (Dynamic Ring Size)
Algorithm for dynamically adjusting mixin count based on chain age and anonymity set size.
As the blockchain grows and more historical outputs accumulate, the optimal mixin
count changes to maximize privacy while minimizing transaction size.
`;
export const TOKEN_MODEL = `# Fuego Token & Economic Model

## Native Assets
| Token | Type     | Description                                           |
|-------|----------|-------------------------------------------------------|
| XFG   | Coin     | Native Fuego coin. Store-of-value, buys services/albums |
| CD    | Locked   | Commitment Deposit — XFG locked for a term, earns yield |
| HEAT  | L2 Token | Minted on Ethereum/Solana via Burn2Mint process         |

## DIGM Platform Tokens
| Token | Type        | How Acquired                              | Use                            |
|-------|-------------|-------------------------------------------|--------------------------------|
| DIGM  | Colored coin | Purchased with XFG                        | Artist publishing rights, anti-spam |
| PARA  | Earned       | Proof-of-listening rewards                | Stake in album pools, vote for albums |
| VOX   | Transmuted   | When staked PARA album reaches #1         | Cosmetic unlocks, burn for CURA |
| CURA  | Curator      | Burn VOX                                   | Create/manage playlists          |
| nfVOX | Non-fungible | Auto-minted when album first hits #1      | Permanent achievement record     |
| TOP   | Singleton    | Held by #1 album holder                   | Transferred when #1 changes      |

## Commitment Deposit (CD) Mechanics
1. Lock XFG for a fixed term (in blocks)
2. Amount hidden with Pedersen commitment (v10+)
3. Interest accrues from swap fee pool (volume-based yield)
4. At maturity, spend TransactionInputCommitmentSpend to unlock + claim interest
5. Early withdrawal not possible (by consensus rule)
6. CDs can be traded on secondary market via swapxfg CD/XFG orderbook

## HEAT / Burn2Mint Flow
1. User creates HEAT burn transaction (marks XFG as burned, irreversible)
2. Burn feeds global decoy pool (increases anonymity set for all users)
3. Elderfier validators vote to approve the burn
4. Once ≥ threshold of Elders approve, STARK proof is generated off-chain
5. STARK proof submitted to HEAT smart contract on ETH/SOL
6. HEAT tokens minted at 1:1 ratio with burned XFG amount

## Elderfier System
- Elders: accounts with ≥800 XFG staked (locked as stake deposit type 0xEF)
- Role: governance voting (approve burn2mint, fee parameter changes)
- Reward: portion of swap fees distributed to active Elders
- Slashing: (planned) malicious votes reduce stake

## Swap Fee Distribution
Fees from swapxfg trades flow into the fee pool:
- % distributed to CD holders as yield (proportional to locked amount × term)
- % distributed to Elderfier voters
- % burned (deflationary pressure)
`;
export const BUILD_GUIDE = `# Fuego Build Guide

## Prerequisites
\`\`\`bash
# Ubuntu 22.04 / 24.04
sudo apt-get install build-essential git cmake libboost-all-dev libjsoncpp-dev libssl-dev

# macOS (Apple Silicon or Intel)
brew install cmake boost openssl

# Go (for TUI + swapxfg)
# Install Go 1.20+ from https://go.dev/dl/
\`\`\`

## Build Commands
\`\`\`bash
# Full build (C++ core)
mkdir -p build/release && cd build/release
cmake ../..
make -j$(nproc)   # or make -j8 on macOS

# Using Makefile shortcuts from project root:
make              # Build all (C++ + TUI if Go available)
make build-release  # Just C++ release build
make build-debug    # Debug build with symbols
make build-tui      # Just Go TUI binary
make test-release   # Build + run C++ tests

# ARM64 / Raspberry Pi
cmake -DCMAKE_TOOLCHAIN_FILE=../../arm.cmake ../..
make -j4
\`\`\`

## Output Binaries (after build)
\`\`\`
build/release/src/fuegod           # Full node daemon
build/release/src/walletd          # Wallet daemon
build/release/src/fuego-miner      # CPU miner
build/release/tui/fuego-tui        # Terminal UI (Go)
\`\`\`

## Running
\`\`\`bash
# Start full node (mainnet)
./fuegod --data-dir ~/.fuego

# Start full node (testnet)
./fuegod --testnet --data-dir ~/.fuego-testnet

# Create wallet
./walletd --generate-container --container-file=wallet.w --container-password=secret

# Start wallet RPC daemon
./walletd -w wallet.w -p secret --bind-address 127.0.0.1 --bind-port 18282

# Start wallet interactive mode
./walletd -w wallet.w -p secret --interactive

# Launch TUI
./fuego-tui   # Connects to fuegod:18180 + walletd:18282 by default

# Launch swap terminal
./swapxfg --daemon http://127.0.0.1:18180 --wallet http://127.0.0.1:18282
\`\`\`

## Docker
\`\`\`bash
# See docker/ directory for compose files
docker-compose -f docker/docker-compose.yml up

# Build image
docker build -f docker/Dockerfile -t fuego:latest .
\`\`\`

## Testing
\`\`\`bash
# C++ unit tests (gtest)
cd build/release && ctest --verbose

# Or via Makefile:
make test-release

# Go TUI tests
cd tui && go test ./...

# Go swap tests
cd swapxfg && go test ./...
\`\`\`

## Key Build Flags
\`\`\`cmake
-DCMAKE_BUILD_TYPE=Release     # Optimized build
-DCMAKE_BUILD_TYPE=Debug       # Debug symbols, assertions
-DSTATIC=ON                    # Static linking (for distribution)
-DARCH=native                  # CPU-specific optimizations
\`\`\`

## Data Directories
- Mainnet: \`~/.fuego/\`
- Testnet: \`~/.fuego-testnet/\`
- Wallet files: \`.w\` extension, user-specified path
`;
//# sourceMappingURL=context.js.map