# Add TON + Wire Monad & Optimism

## Goal
Add TON (The Open Network) as chain #26. Wire the already-scaffolded Monad and Optimism EVM headers (chains #27, #28) through the full registration stack.

---

## Part A: TON Implementation (Custom — ~1700 LOC)

TON is **not** EVM. TVM (TON Virtual Machine), FunC contracts, account-based, base64 addresses. Closest template: Solana (custom non-standard chain).

### A1: Core Types
**Files:** `SwapTypes.h`, `SwapTypes.cpp`
- Add `TON = 25` to `SwapPair` enum (after `ZANO = 24`)
- Add `"TON"` cases to `swapPairFromString()` and `swapPairToString()`

### A2: Config
**Files:** `SwapDaemon.h`, `ChainClientConfig.cpp`
- Add to `ChainClientConfig` struct:
  ```
  std::string tonHost;
  uint16_t    tonPort       = 2990;   // lite server default
  std::string tonRpcUser;
  std::string tonRpcPass;
  std::string tonWalletKey;           // hex-encoded Ed25519 seed
  std::string tonHtlcAddress;         // deployed HTLC contract address
  int         tonWorkchain   = 0;
  ```
- Add JSON parsing in `ChainClientConfig.cpp`:
  ```
  ton_rpc_host, ton_rpc_port, ton_rpc_user, ton_rpc_pass,
  ton_wallet_key, ton_htlc_address, ton_workchain
  ```

### A3: TON RPC Client
**Files:** `src/SwapDaemon/Ton/TonRpcClient.h`, `TonRpcClient.cpp`
- HTTP/HTTPS transport to TON lite server (port 2990 or HTTPS on 443)
- Methods needed:
  - `getMasterchainInfo()` — get latest block seqno
  - `getBlockState()` — get account states
  - `getAccountState(address)` — query account/code/data
  - `sendBoc(bocBase64)` — broadcast serialized cell
  - `estimateFee(address, amount, payload)` — gas estimation
  - `sendMessage(address, amount, payload)` — simple transfer
  - `runGetMethod(address, method, args)` — run TVM method (view call)
  - `getTransactions(address, lt, hash)` — transaction history
- Serialization: TON uses Cells (not RLP/ABI). Need Cell builder/parser for BOC (Bag of Cells) format.
- Reference: https://ton.org/docs/develop/dapps/apis/

### A4: TON Chain Client
**Files:** `src/SwapDaemon/Ton/TonChainClient.h`, `TonChainClient.cpp`
- Implements `IChainClient`
- Constructor: `TonChainClient(unique_ptr<TonRpcClient> rpc, const std::string& walletKey)`
- `chainName()` → `"TON"`
- `lock()` — deploy/call HTLC contract with hashlock + timelock params
- `verifyLock()` — run `getContract` or `getDetails` view on HTLC address
- `claim()` — call `claim` method with preimage
- `refund()` — call `refund` method after timeout
- `verifyReserveProof()` — Ed25519 signature verify + balance check
- `tryExtractClaimedSecret()` — read preimage from contract state
- `getCurrentHeight()` — get masterchain seqno

### A5: TON HTLC Contract
**Files:** `src/SwapDaemon/Ton/TonHtlcContract.fc` (FunC source), `TonHtlcContract.boc` (compiled)
- FunC implementation of HashedTimelock contract
- Storage: `{hashlock, timelock, sender, recipient, amount, claimed, preimage}`
- Methods:
  - `claim(int preimage)` — verify hash(preimage) == hashlock, transfer to recipient
  - `refund()` — verify now() > timelock, return to sender
  - `get_details()` — view method returning contract state
- TON uses `cell` serialization, not ABI encoding
- Reference HTLC pattern from https://github.com/ton-blockchain/func-contracts

### A6: SwapDaemon Integration
**Files:** `SwapTimelock.cpp`, `PriceOracle.cpp`, `SwapDaemon.cpp`
- `SwapTimelock.cpp`: `case SwapPair::TON: return 5000;` (~5s block time)
- `PriceOracle.cpp`: seed rate `SEED_TON_USD = 5.50` (adjust at launch), `ctrDivisor = 1e9` (TON uses 9 decimals)
- `SwapDaemon.cpp`: `#include "Ton/TonChainClient.h"`, registration block:
  ```cpp
  if (!chainCfg.tonHost.empty()) {
    auto rpc = make_unique<TonRpcClient>(chainCfg.tonHost, chainCfg.tonPort, ...);
    registerChain(SwapPair::TON, make_unique<TonChainClient>(move(rpc), chainCfg.tonWalletKey));
  }
  ```

### A7: Dashboard
**Files:** `swapxfg.html`, `swapxfg.js`, `coin-icons/ton.png`
- Add `<option value="TON">TON</option>` to chain selector
- Add `CHAIN_INFO.TON = { icon: '/coin-icons/ton.png', color: '#0098ea', ticker: 'TON', name: 'TON' }`
- Icon already exists at `dashboard/static/coin-icons/ton.png`

### A8: Tests
**Files:** `src/SwapDaemon/Ton/tests/test_ton_htlc.cpp`
- Unit test: Cell builder roundtrip
- Unit test: HTLC claim/refund flow (mocked RPC)
- Unit test: BOC serialization

---

## Part B: Monad + Optimism Wiring (Each ~50 LOC)

Both chains already have header-only `EthChainClient` subclasses. Just need plumbing — zero new C++ source files.

### B1: Monad
**Files touched:**
- `SwapTypes.h` → `MONAD = 26`
- `SwapTypes.cpp` → string mapping
- `SwapDaemon.h` → config fields: `monadHost`, `monadPort=8545`, `monadPrivKeyHex`, `monadAddress`, `monadChainId=185`, `monadHtlcBinPath`
- `ChainClientConfig.cpp` → JSON parsing
- `SwapDaemon.cpp` → `#include "Monad/MonadChainClient.h"` + registration block
- `SwapTimelock.cpp` → `case SwapPair::MONAD: return 500;` (~0.5s)
- `PriceOracle.cpp` → seed rate + cases (MON ~$0.00? check current)
- `swapxfg.html` + `swapxfg.js` → chain selector + CHAIN_INFO
- `coin-icons/monad.png` → generate 128x128

### B2: Optimism
**Files touched:** (identical pattern)
- `SwapTypes.h` → `OPTIMISM = 27`
- `SwapTypes.cpp` → string mapping
- `SwapDaemon.h` → config: `opHost`, `opPort=8545`, `opPrivKeyHex`, `opAddress`, `opChainId=10`, `opHtlcBinPath`
- `ChainClientConfig.cpp` → JSON parsing
- `SwapDaemon.cpp` → `#include "Optimism/OptimismChainClient.h"` + registration
- `SwapTimelock.cpp` → `case SwapPair::OPTIMISM: return 2000;` (~2s)
- `PriceOracle.cpp` → seed rate (OP ~$1.50? check)
- `swapxfg.html` + `swapxfg.js`
- `coin-icons/op.png` → generate 128x128

---

## Part C: OP/EVM Adapter Research

### Current State
All 18 existing EVM chains share a **single adapter**: `EthChainClient` + `EthRpcClient`. The adapter:
- Uses JSON-RPC 2.0 (`eth_*` namespace)
- EIP-1559 (type 2) transactions via `EthTxType::Eip1559`
- `HashedTimelock.sol` registry contract (Solidity)
- `ContractAbi` for ABI encoding/decoding
- Keccak-256 for hashlock (default path in `AdaptorSwap.cpp`)

### Monad (OP Stack L1)
- **EVM-compatible**: Yes (100% EVM, runs EVM bytecode)
- **OP Stack**: Monad uses the OP Stack architecture but presents a standard EVM RPC interface
- **Adapter needed**: **NONE** — the existing `EthChainClient` + `EthRpcClient` works as-is
- **Chain ID**: 185
- **Gas**: EIP-1559 compatible
- **HTLC contract**: Same `HashedTimelock.sol` deployed on Monad
- **Effort**: Header-only wiring (done in Part B1)

### Optimism (OP Stack L2)
- **EVM-compatible**: Yes (100% EVM, OP Stack L2)
- **OP Stack**: Native OP Stack chain (the reference implementation)
- **Adapter needed**: **NONE** — existing adapter works
- **Chain ID**: 10
- **Gas**: EIP-1559 + L1 data fee overhead (but RPC handles this transparently)
- **HTLC contract**: Same `HashedTimelock.sol` on Optimism
- **Effort**: Header-only wiring (done in Part B2)

### Other OP Stack Chains (if needed later)
Any OP Stack chain (Base, Worldchain, Zora, Mantle, etc.) can be added as another header-only `EthChainClient` subclass. The pattern is identical — no adapter work needed. Just:
1. Header file (3 lines inheriting `EthChainClient`)
2. Wire into registration stack (config + enum + oracle)

### When Would a Custom EVM Adapter Be Needed?
Only if a chain **deviates** from standard EVM JSON-RPC. Examples:
- Pre-compile differences (e.g., custom system contracts)
- Non-standard gas model (e.g., Solana VM wrapping EVM)
- Custom transaction types beyond EIP-1559
- Modified `eth_call` semantics

None of the current or planned chains require this.

---

## Execution Order

1. **B1: Monad wiring** (15 min — pure plumbing, no new code)
2. **B2: Optimism wiring** (15 min — identical to B1)
3. **A1-A2: TON types + config** (15 min)
4. **A3: TonRpcClient** (2-3 days — Cell/BOC serialization, HTTP transport, TVM method calls)
5. **A4: TonChainClient** (1-2 days — IChainClient implementation)
6. **A5: TonHtlcContract** (1-2 days — FunC contract + BOC compile)
7. **A6: SwapDaemon integration** (30 min — config wiring)
8. **A7: Dashboard** (10 min — option + JS + icon)
9. **A8: Tests** (1 day)
10. **Build + test everything** (30 min)

---

## Files Modified (complete list)

### New files
| File | Lines |
|------|-------|
| `src/SwapDaemon/Ton/TonRpcClient.h` | ~150 |
| `src/SwapDaemon/Ton/TonRpcClient.cpp` | ~900 |
| `src/SwapDaemon/Ton/TonChainClient.h` | ~40 |
| `src/SwapDaemon/Ton/TonChainClient.cpp` | ~200 |
| `src/SwapDaemon/Ton/TonHtlcContract.fc` | ~80 |
| `src/SwapDaemon/Ton/tests/test_ton_htlc.cpp` | ~150 |
| `dashboard/static/coin-icons/monad.png` | (binary) |
| `dashboard/static/coin-icons/op.png` | (binary) |

### Modified files
| File | Change |
|------|--------|
| `src/SwapDaemon/SwapTypes.h` | +3 enum values (TON=25, MONAD=26, OPTIMISM=27) |
| `src/SwapDaemon/SwapTypes.cpp` | +6 switch cases (2 per chain × 3 chains) |
| `src/SwapDaemon/SwapDaemon.h` | +21 config fields (7 per chain × 3) |
| `src/SwapDaemon/ChainClientConfig.cpp` | +21 JSON keys + 1 helper |
| `src/SwapDaemon/SwapDaemon.cpp` | +3 includes + 3 registration blocks |
| `src/SwapDaemon/SwapTimelock.cpp` | +3 cases |
| `src/SwapDaemon/PriceOracle.cpp` | +3 seed rates + 9 switch cases |
| `src/SwapDaemon/src/CMakeLists.txt` | +3 Ton source files |
| `dashboard/static/swapxfg.html` | +3 options |
| `dashboard/static/js/swapxfg.js` | +3 CHAIN_INFO entries |

## Done When
- [ ] Monad and Optimism appear in chain selector, have icons, and `SwapDaemon` builds
- [ ] TON appears in chain selector, has icon, `SwapDaemon` builds
- [ ] `TonRpcClient` compiles and connects to TON testnet
- [ ] `TonChainClient` implements full IChainClient lifecycle (lock/claim/refund/verify)
- [ ] `TonHtlcContract.fc` compiles to valid BOC
- [ ] All 3 new chains' unit tests pass
- [ ] `cmake --build build --target SwapDaemonLib` succeeds
- [ ] `cmake --build build --target SwapDaemon` links clean
- [ ] Existing 28 tests still pass
