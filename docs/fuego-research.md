# Fuego Blockchain Codebase Research

## 1. Proof of Work Algorithm

**Algorithm: CryptoNight (Monero lineage), with variant switching**

- Core implementation: `src/crypto/slow-hash.c` — the classic CryptoNight hash function
- Uses **Keccak1600** (SHA-3 candidate) as the underlying hash: Keccak → 2MB scratchpad → AES-like iterations → Keccak output
- Variant selection at `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp:574-577`:
  - `variant 0` = original CryptoNight (block major version < 5, i.e. pre-`Ironborn`)
  - `variant 1` = CryptoNight variant 1 (anti-ASIC, CNv7) (v5 blocks)
  - `variant 2` = CryptoNight variant 2 (CNv8) (v6+ blocks)
- Light mode enabled from block major version 9+ (`Godflame` upgrade) — `light=1` skips the 2MB scratchpad
- Fast hashes (tx hashing, etc.): `cn_fast_hash()` = Keccak1600 → 32 bytes
- Additional hash functions available: Blake-256, Groestl, JH, Skein (`hash-ops.h:70-73`)
- AES-NI heavily optimized for x86_64; Apple Silicon uses software AES with heap allocation (`CMakeLists.txt:175`)

## 2. Block Chaining & Validation

**Standard CryptoNote chain structure** with block major version-based upgrades:

- Block structure: `Block { majorVersion, minorVersion, timestamp, nonce, baseTransaction, transactionHashes, parentBlock }`
- Parent block contains the merkle tree branch linking to the previous block
- Block hash = `cn_slow_hash()` of the serialized block blob
- Transaction tree: merkle tree of tx hashes, root stored in block header (`CryptoNoteFormatUtils.cpp:600`)
- Validation pipeline (`src/CryptoNoteCore/Blockchain.cpp`):
  1. Pre-validate miner transaction (height, unlock time, outputs)
  2. Validate miner reward against emission formula
  3. Check ring signatures (MLSAG for v10+, classic CLSAG/boring-ring signatures before)
  4. Verify transaction inputs are unspent (key image check via `parallel_flat_hash_map`)
  5. Check block size against median of last 100 blocks (penalty system)
  6. Timestamp check against median of window
  7. Difficulty check via DMWDA (Dynamic Multi-Window Difficulty Algorithm)
- Difficulty algorithms: Original CryptoNote → Zawy v1.0 → Zawy-LWMA → DMWDA (current)
- DMWDA parameters: short window 15, medium 45, long 120 blocks, emergency window 5
- Checkpoints: hardcoded list at `CryptoNoteConfig.h:396-464`

## 3. P2P Networking Protocol & Serialization

**Protocol: CryptoNote P2P (Levin-based)**

- Serialization: **KV Binary format** (key-value binary, not JSON)
  - Uses `KVBinaryInputStreamSerializer` / `KVBinaryOutputStreamSerializer` (`LevinProtocol.h:87-88`)
  - Each message is serialized via `serialize(value, serializer)` — custom field-level KV_MEMBER macros
- Transport: TCP, no UDP/multicast
- Messages are framed with Levin headers (command ID, response flag, return code)
- Command IDs (defined in `P2pProtocolDefinitions.h`):
  - `COMMAND_HANDSHAKE` (1001) — node handshake, exchange peer lists
  - `COMMAND_TIMED_SYNC` (1002) — periodic sync of heights/timestamps
  - `COMMAND_PING` (1003) — keepalive
  - `NOTIFY_NEW_BLOCK` — new block propagation
  - `NOTIFY_NEW_TRANSACTIONS` — transaction pool propagation
  - `COMMAND_SWAP_OFFER` (1013), `COMMAND_SWAP_CANCEL` (1014), `COMMAND_SWAP_REQUEST` (1015), `COMMAND_SWAP_TRADE` (1016) — atomic swap relay
- Peer management: white list (1000 peers) + gray list (5000 peers), 8 default connections
- Meshtastic directory present at `src/P2p/Meshtastic/` (experimental mesh networking layer)
- Tor support: FuegoTor subdirectory, disabled by default (`ENABLE_FUEGOTOR` CMake option)
- Network ID: derived from keccak hash of network string (`Currency.cpp:1495-1509`)

## 4. Hybrid / Future-Proofing Mechanisms

**Version-gated upgrade path via block major version:**

- 11 block versions defined: V1 (genesis) through V11 (HearthAMM+HEAT)
- Key upgrades:
  - V5 (Ironborn): CNv7 anti-ASIC PoW variant
  - V6 (Ice&Fire): CNv8 PoW variant
  - V7 (Apotheosis / Fango): Deposits introduced
  - V8 (Dragonborne): New emission speed factor (19), deposit refinements
  - V9 (Godflame): New emission speed factor (20), light PoW mode
  - V10 (ÆzorAhai): Fire aliases, dynamic mixin, CD (Certificates of Deposit), SwapXFG
  - V11 (HearthAMM): AMM pool, HEAT stablecoin, PI controller, CD yield pipeline
- All future upgrades are height-gated; a new `BLOCK_MAJOR_VERSION_12` can be added at any height
- No explicit hybrid PoW/PoS or DAG structure — classic Nakamoto-style chain

## 5. Database / Blockchain Storage Format

**Custom file-based storage (not LMDB/RocksDB)**

Three files per blockchain (defined in `CryptoNoteConfig.h:280-285`):
1. `blocks.dat` — raw block data, binary-serialized via `SwappedVector<BlockEntry>`
2. `blockindexes.dat` — offset index file for `SwappedVector`
3. `blockscache.dat` — cached block metadata (serialized via `BinaryOutputStreamSerializer`)
4. `blockchainindices.dat` — transaction-to-block mapping indices
5. `poolstate.bin` — mempool serialization

- `SwappedVector<T>` (`src/CryptoNoteCore/SwappedVector.h`): a custom LRU-cached, file-backed vector
  - Blocks stored as `BlockEntry` struct in `blocks.dat`
  - Index file stores offset table for O(1) random access
  - In-memory LRU cache (default pool of items), cache misses trigger file seeks and deserialization
  - Elements serialized using the `ISerializer` binary framework
- `parallel_flat_hash_map` (from `external/parallel_hashmap/`) for key image lookup (spent key set)
- No SQL layer, no B-tree, no KV store — purely sequential file + offset index

## 6. Wallet Key Storage

**File-mapped, ChaCha8-encrypted containers**

- Wallet files use `FileMappedVector<EncryptedWalletRecord>` (`src/Common/FileMappedVector.h`)
  - Memory-mapped file of pod-type records (no serialization overhead for keys)
- Key encryption: `chacha8` stream cipher
  - Derivation: password → `chacha8_key` via `generateKey()` (stretches password)
  - Each spend key pair (public + secret) encrypted as `EncryptedWalletRecord { chacha8_iv, uint8_t data[] }`
  - Encryption: `chacha8(serializedKeys, key, iv, ciphertext)` (`WalletGreen.cpp:1259`)
- Container structure:
  - Prefix: version info, next IV counter
  - Body: array of encrypted spend key records (one per address in wallet)
  - Suffix: ChaCha8-encrypted wallet state (transactions, balances, transfers, etc.), integrity-checked with keccak256 hash
- Wallet file loading: `loadWalletCache()` → decrypt prefix → check keccak256 integrity → deserialize wallet cache
- Legacy V1 wallet files also supported with separate loader path

## 7. Build System

**CMake (primary) + Makefile (convenience wrapper)**

- `CMakeLists.txt` at root: CMake v3.16 minimum, C++17 standard
- `Makefile` at root: convenience wrapper (likely targets for `release`, `debug`, `test`)
- `arm.cmake` at root: cross-compilation toolchain for ARM
- Static library targets: `Crypto`, `Common`, `System`, `CryptoNoteCore`, `P2P`, `Serialization`, `Logging`, `Wallet`, `Transfers`, `BlockchainExplorer`, `Rpc`, `Http`, `JsonRpcServer`, `NodeRpcProxy`
- External dependencies:
  - Boost 1.86+ (algorithm, math, lexical_cast, uUID, foreach — uses `io_context`, `executor_work_guard`)
  - OpenSSL 3.x/4.x (SwapDaemon TLS, RPC)
  - secp256k1 (external/secp256k1 or system package) — MuSig2, ECDH, ElligatorSwift
  - parallel_hashmap (header-only, for key image lookup)
  - Google Test (external/gtest) — for tests
  - RapidJSON / JsonCpp (JSON parsing for RPC)
- Platform-specific: AppleClang/Clang/GCC for Unix, MSVC for Windows
- AES-NI auto-detection for x86_64; Apple Silicon uses software AES + heap allocation

## 8. Quantum Resistance Related Code

**Near zero — only one passing mention.**

- `docs/design/group-aliases-plan.md:632`: a checklist item that says `[ ] Signature verification is quantum-resistant (based on discrete log)` — **unchecked**, aspirational only
- Curve is **Ed25519** (edwards25519) for all signing (`src/crypto/crypto-ops.h`, `ge_p3`, etc.) — **broken by Shor's algorithm**
- Ring signatures use MLSAG (Multi-layered Linkable Spontaneous Anonymous Group signatures) — also discrete-log based
- secp256k1 library present for MuSig2 multi-signatures and ECDH — also ECDLP-based
- **No ML-KEM, ML-DSA, SLH-DSA, Falcon, Dilithium, Kyber, or any NIST PQC candidate code**

## 9. Fee Structure & Emission

**Supply: 80,000,088,000,008 atomic units (~8,000,008.8 XFG)**

Emission formula (`Currency.cpp:246-267`):
```
baseReward = (MONEY_SUPPLY - coinsGenerated) >> emissionSpeedFactor
```

Three emission speed factors by version:
- V1-V7: `EMISSION_SPEED_FACTOR = 18` (÷262,144)
- V8 (Fango): `EMISSION_SPEED_FACTOR_FANGO = 19` (÷524,288)
- V9+ (Fuego): `EMISSION_SPEED_FACTOR_FUEGO = 20` (÷1,048,576)

V10+ burn-adjusted emission:
- Track total burned coins (`eternalFlame` / `Osavvirsak`)
- `baseReward = (MONEY_SUPPLY - (alreadyGeneratedCoins - eternalFlame)) >> speedFactor`
- This re-mints burned coins back into circulation (reissuance of destroyed supply)

Minimum fees (per-byte penalty system):
- V1-V7: `MINIMUM_FEE_V1 = 0.08 XFG` (800,000 ħ)
- V8-V9: `MINIMUM_FEE_V2 = 0.008 XFG` (80,000 ħ)
- V10+: `MINIMUM_FEE_8KH = 0.0008 XFG` (8,000 ħ)

Swap fees: 1% of atomic swap claim/refund
- Split: 69% CD yield pool / 21% Treasury / 10% Rollover vault
- Epoch duration: 900 blocks (~5 days mainnet)

Banking fees (tiered): 0.1% per tier (0.8/8/80/800 XFG burns → fee 8K/80K/800K/8M ħ)

## 10. Governance for Protocol Upgrades

**Voting-based upgrade mechanism** via `UpgradeDetector.h`:

- Each upgrade has a `targetVersion` (new block major version) and an activation height
- Two modes:
  1. **Hardcoded height** (e.g., `UPGRADE_HEIGHT_V5 = 324819`): activates at that exact height regardless of miner voting
  2. **Miner voting**: if `upgradeHeight()` returns `UNDEF_HEIGHT`, it uses miner votes
- Voting mechanism:
  - Blocks can signal support by setting `minorVersion = BLOCK_MINOR_VERSION_1`
  - Voting window: `UPGRADE_VOTING_WINDOW` (~180 blocks, one day)
  - Threshold: `UPGRADE_VOTING_THRESHOLD = 90%` of blocks in window must signal
  - Once threshold met at `votingCompleteHeight`, the upgrade activates after **maxUpgradeDistance()** additional blocks
- Parameters: inflation controlled by emission speed factor constants in code
- **No on-chain proposal system** — upgrades are either hard-fork heights pre-set in `CryptoNoteConfig.h` or miner signalled
- Checkpoints provide additional security against reorg attacks at known heights
- The `UpgradeDetector` tracks voting in real-time with `blockPushed()` / `blockPopped()` callbacks
