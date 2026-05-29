# Plan: Add Arbitrum atomic swap option (with EIP-1559 upgrade)

## Context

Fuego currently supports atomic swaps with SOL (0), ETH (1), XMR (2), BCH (3) via the `IChainClient` interface added in the recent chain-interface refactor. Adding Arbitrum extends the swap surface to an EVM L2 with ~$5B TVL, lower fees, faster confirmation, and an established stablecoin ecosystem (USDC/USDT depth on Aerodrome/Uniswap V3). This also feeds **higher-quality price data into the swapxfg TWAP** that drives HEAT's algorithmic peg — Arbitrum-listed assets have tight DEX price discovery.

Because Arbitrum's gas economics differ materially from Ethereum mainnet (base fees in the millionths of a gwei range, L1 calldata posting fees), the current EthRpcClient's hardcoded 20 gwei legacy gas pricing would cause massive overpayment. The user has decided to **upgrade EthRpcClient to EIP-1559 + dynamic gas estimation** in the same plan. This benefits both ETH and ARB adapters.

The HEAT flatcoin work (Mode 0 + real CPI oracle) is **out of scope** — separate plan to follow.

## Decisions captured

- **Pair semantics**: `ARB` represents ETH locked on the Arbitrum One L2 (chainId 42161). The HTLC contract holds ETH on Arbitrum. Seed rate matches ETH ($2,140) since the underlying asset is identical. ARB token (the Arbitrum governance token) is *not* the swap asset; same `HashedTimelock.sol` bytecode is reused on Arbitrum.
- **Gas model**: Upgrade `EthRpcClient` to EIP-1559 (type-2 tx) with dynamic fee estimation via `eth_feeHistory` and `eth_maxPriorityFeePerGas`. Legacy type-0 tx path retained for fallback. Chain-specific config selects the mode.
- **Adapter reuse**: No new `ArbChainClient` class. Add a configurable `chainName` parameter to existing `EthChainClient` so a single class serves both ETH and ARB instances, registered separately in `ChainRegistry`.
- **Out of scope here**: HEAT flatcoin formula change, real CPI oracle integration, HTLC contract deployment to Arbitrum (operations work, not code).

## Workstream A — EIP-1559 upgrade to EthRpcClient

**Files to modify:**
- `src/SwapDaemon/Ethereum/EthRpcClient.h`
- `src/SwapDaemon/Ethereum/EthRpcClient.cpp`

**Changes:**

1. **New RPC methods** to add (alongside existing eth_getTransactionCount, eth_sendRawTransaction):
   - `eth_feeHistory(blockCount, newest, percentiles[])` — returns base fee history + priority fee percentiles
   - `eth_maxPriorityFeePerGas` — returns suggested tip
   - `eth_getBlockByNumber("latest", false)` — read latest base fee from header
   - `eth_estimateGas` — replace hardcoded 800k gas limit with dynamic estimate
   - `eth_chainId` — sanity check the configured chainId matches the RPC

2. **New TxType enum** in the header:
   ```cpp
   enum class EthTxType { Legacy = 0, Eip1559 = 2 };
   ```
   Add `m_txType` member (default `Eip1559`).

3. **Rewrite `buildSignedTx()`** to handle both types:
   - **Legacy (type 0)**: existing RLP encoding `[nonce, gasPrice, gas, to, value, data, v, r, s]` with EIP-155 v = chainId * 2 + 35 + recId. Retained for chains that don't support 1559.
   - **EIP-1559 (type 2)**: signing payload = `0x02 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList])`. Hash via keccak256. After signing, prepend `0x02` to RLP of full tuple including v/r/s. AccessList is empty array.

4. **Dynamic fee estimation** in `signAndSend()`:
   - Query `eth_feeHistory(20, "latest", [50])` → take median of priority fees as suggested tip
   - Query latest block base fee
   - `maxPriorityFeePerGas = max(suggested_tip, 1_gwei)` (floor at 1 gwei)
   - `maxFeePerGas = base_fee * 2 + maxPriorityFeePerGas` (2x headroom for next blocks)
   - Replace hardcoded `20000000000ULL` (20 gwei) at EthRpcClient.cpp:461, 530
   - Replace hardcoded gas limit `800000` at EthRpcClient.cpp:531 with `eth_estimateGas` result + 20% buffer

5. **Keccak-256 helper** — needed for EIP-1559 signing hash. Check if already present in the codebase (Solidity contract deployment likely uses it); if not, port from existing eth signing code path.

6. **RLP encoder** — existing legacy path already has RLP; extend to handle access list (empty list = `0xc0`).

7. **Backward compatibility constructor**: Existing 2-arg and 5-arg constructors retained. Add optional `txType` parameter at the end (default `Eip1559`). Old call sites unaffected.

**Test plan (manual)**:
- Point at Sepolia testnet, run a small swap with `Eip1559`. Confirm tx lands with type-2 envelope (check via etherscan).
- Repeat against Arbitrum Sepolia (chainId 421614), confirm fees are sub-cent instead of ~$1.

## Workstream B — Arbitrum integration

**Files to modify:**

| File | Change |
|---|---|
| `src/SwapDaemon/SwapTypes.h` | Add `ARB = 4` to `SwapPair` enum |
| `src/SwapDaemon/SwapTypes.cpp` | Add `ARB` cases to `swapPairToString()` and both `swapPairFromString()` overloads |
| `src/SwapDaemon/SwapDaemon.h` | Add `ChainClientConfig::arbHost`, `arbPort`, `arbPrivKeyHex`, `arbAddress`, `arbChainId` (default `42161`), `arbHtlcBinPath` fields. Mirror existing ETH block |
| `src/SwapDaemon/SwapDaemon.cpp` | Add Arbitrum registration block in the `ChainClientConfig` constructor, parallel to the existing ETH block at lines 120-134 |
| `src/SwapDaemon/Ethereum/EthChainClient.h` | Add `chainName` ctor parameter (default `"ETH"`), private `m_chainName` member |
| `src/SwapDaemon/Ethereum/EthChainClient.cpp` | Update ctor body; `chainName()` returns `m_chainName` |
| `src/CryptoNoteCore/SwapOfferRelay.cpp` | Add ARB to `getSeedRate()` map: `{4, 214000.0}` (matches ETH since same asset). Add to `getCtrUsdPrice()` map: `{4, 2140.0}`. Bump `validateOffer` check from `> 3` to `> 4`. Bump `getNativeXfgPrice()` loop from `<= 3` to `<= 4` |
| `src/CryptoNoteCore/SwapOfferRelay.h` | Update pair comments (lines 41, 70): add `4=ARB` |
| `src/SwapDaemon/SwapDaemon.cpp` | `handleSwapRequest()` loop bump from `pair <= 3` to `pair <= 4` |
| `src/SwapDaemon/ChainClientConfig.cpp` | Add JSON keys for arbHost/arbPort/etc. mirroring the ETH section |
| `src/CMakeLists.txt` | No new files (reusing `EthChainClient.cpp`). No CMake changes needed. |

**Registration block to add in SwapDaemon.cpp** (parallel to ETH at lines 120-134):
```cpp
if (!chainCfg.arbHost.empty()) {
  std::unique_ptr<EthRpcClient> rpc;
  if (!chainCfg.arbPrivKeyHex.empty() && !chainCfg.arbAddress.empty()) {
    rpc = std::make_unique<EthRpcClient>(
        chainCfg.arbHost, chainCfg.arbPort,
        chainCfg.arbPrivKeyHex, chainCfg.arbAddress, chainCfg.arbChainId,
        EthTxType::Eip1559);
  } else {
    rpc = std::make_unique<EthRpcClient>(chainCfg.arbHost, chainCfg.arbPort);
  }
  m_chainRegistry.registerChain(SwapPair::ARB,
      std::make_unique<EthChainClient>(std::move(rpc), chainCfg.arbAddress, "ARB"));
  m_logger(Logging::INFO) << "ARB chain client registered: "
    << chainCfg.arbHost << ":" << chainCfg.arbPort << " (chainId=" << chainCfg.arbChainId << ")";
}
```

## Workstream C — Tests

**Files to modify:**
- `tests/SwapDaemon/ChainRegistryTests.cpp` — add `RegisterArbAndDispatch` case: register two `TestChainClient` instances (one as ETH, one as ARB), verify dispatch routes correctly to each.
- Optionally `tests/SwapDaemon/Eip1559Tests.cpp` (new) — unit tests for the EIP-1559 RLP encoding and tx signing path (no live RPC; uses test vectors from EIP-1559 spec).

Use the existing `TestChainClient` pattern from `tests/SwapDaemon/TestChainClient.h` — no need for a new test scaffold.

## Files reused (no changes)

- `src/SwapDaemon/Ethereum/HashedTimelock.sol` — same bytecode deploys to Arbitrum (EVM-portable; verified in exploration).
- `src/SwapDaemon/IChainClient.h`, `ChainClientResult.h`, `ChainRegistry.{h,cpp}` — the recent refactor handles polymorphic dispatch correctly; no changes.
- `tests/SwapDaemon/TestChainClient.h` — reusable mock.

## Verification

1. **Build**: `cd build && make -j$(sysctl -n hw.ncpu) SwapDaemon SwapDaemonLib` — must succeed clean (no warnings on the changed files).
2. **Existing tests pass**: Re-run `ChainRegistryTests` — all 4 existing cases must still pass.
3. **New tests pass**: `RegisterArbAndDispatch` and any EIP-1559 unit tests must pass.
4. **Static check**: `XfgSwap::swapPairFromString("ARB", out)` returns true; `swapPairToString(SwapPair::ARB)` returns `"ARB"`.
5. **End-to-end (testnet)**:
   - Deploy `HashedTimelock.sol` to Arbitrum Sepolia (chainId 421614) — operations task, not code.
   - Configure SwapDaemon with `arbHost=https://sepolia-rollup.arbitrum.io/rpc`, `arbChainId=421614`, signer key + address.
   - Run `simplewallet swap initiate ARB <amount>` (or whatever the SimpleWallet CLI is).
   - Observe daemon logs: `ARB chain client registered`, `ARB locked`, `ARB claimed`.
   - Confirm tx on arbiscan: type=2 (1559), gas paid << $0.05.
6. **Gas check**: tx receipt's `effectiveGasPrice` should be in the sub-gwei range, not the 20 gwei legacy hardcode. Confirms dynamic gas estimation worked.

## Risks and open questions

| Risk | Mitigation |
|---|---|
| EIP-1559 RLP encoding bugs | Use published test vectors from EIP-1559 spec; cross-verify against ethers.js reference values |
| Arbitrum's L1 calldata surcharge unaccounted for | `eth_estimateGas` on Arbitrum already includes L1 component; using its result avoids the issue |
| `eth_feeHistory` not implemented by some RPC providers | Fall back to `eth_maxPriorityFeePerGas` + latest block base fee. Both are standard on Alchemy/Infura/public Arbitrum RPC |
| Existing ETH mainnet swap regressions from EIP-1559 switch | Run a Sepolia mainnet tx with both `Legacy` and `Eip1559` modes before flipping default; keep `Legacy` switch available per chain |
| `eth_chainId` mismatch silently mints bad txs | Verify chainId on first RPC call; fatal-error if it doesn't match config |
| HTLC contract not yet deployed on Arbitrum | Not a code risk — operations task. Document the deployment address as a config field, abort registration if empty |

## Estimated effort

| Workstream | Time |
|---|---|
| A — EIP-1559 upgrade | 2-3 days |
| B — Arbitrum integration on top | 0.5-1 day |
| C — Tests | 0.5-1 day |
| Manual testnet verification | 0.5 day |
| **Total** | **3.5-5.5 days** |

## Follow-up (separate plan)

After this lands, the next plan covers HEAT flatcoin:
- Flip `HEAT_STABILITY_MODE` from `2` to `0`
- Wire real CPI oracle (Truflation primary, Pyth basket sanity, BLS monthly anchor)
- Implement `truflationFetchThread()`, `pythCommodityBasketThread()`, `blsFetchThread()` in `SwapOfferRelay` mirroring `exbitronFetchThread()`
- Median-of-sources for `cpiCurrentValue`
- Set `m_piState.cpiOracleActive = true` once first valid CPI reading is in
- Per-epoch CPI snapshot for `m_piState.cpiLaunchValue` on first epoch with valid oracle
- Manipulation defenses: change-rate cap, divergence alerting, staleness gating
