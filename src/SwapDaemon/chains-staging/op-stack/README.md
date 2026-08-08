# OP Stack Integration Plan

This directory tracks OP Stack-compatible chains for integration into Fuego SwapXFG.

## Background

The **OP Stack** is a standardized, open-source framework for building L2 blockchains
on Ethereum. Chains built with the OP Stack share a common architecture:
- Uses the same EVM execution (via `ethrex` or `op-geth`)
- Uses Optimism's custom `EIP-1559` fee model
- Shares the same JSON-RPC interface as Ethereum Layer 1
- Uses `eth_` namespace methods + Optimism-specific extensions (`optimism_*`)

## Chains

| Chain | Chain ID | Status | Notes |
|-------|----------|--------|-------|
| **Optimism** | 10 | Active | Original OP Stack chain |
| **Monad** | 185 | Mainnet (2025) | New high-performance L1 built on OP Stack |
| **Base** | 8453 | Active | ✅ ALREADY IMPLEMENTED |

## Implementation Approach

Since both Optimism and Monad are EVM-compatible, they use the **exact same
header-only inheritance pattern** as the existing `PolygonChainClient`:

```cpp
class OptimismChainClient : public EthChainClient {
  OptimismChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address)
    : EthChainClient(std::move(rpc), address, "OPTIMISM") {}
};
```

The key difference from existing EVM chains is in **gas estimation**:
- OP Stack chains support `eth_maxPriorityFeePerGas` (EIP-1559)
- Gas prices are typically much lower than Ethereum L1
- Transactions finalize in ~2 seconds (vs 12+ seconds for Ethereum)

## OP Stack Specific Optimizations

1. **Gas Price Fetching**: Use `eth_maxPriorityFeePerGas` instead of `eth_gasPrice`
2. **Block Time**: ~2 seconds for Optimism, ~0.5 seconds for Monad
3. **Batch Submission**: OP Stack chains batch transactions, affecting finality guarantees

## Chain Parameters

| Chain | Chain ID | RPC Port | Block Time | Type |
|-------|----------|----------|------------|------|
| OPTIMISM | 10 | 8545 | ~2s | EVM (OP Stack L2) |
| MONAD | 185 | 8545 | ~1s | EVM (OP Stack L1) |

## Files to Create

```
src/SwapDaemon/Optimism/OptimismChainClient.h     ← 14 lines
src/SwapDaemon/Monad/MonadChainClient.h           ← 14 lines
```

No `.cpp` files needed — inherits everything from `EthChainClient`.

## Estimated Effort

- **Implementation**: 1 day (2 header files)
- **Integration**: 1 day (config, registration, oracle, HTML/JS)
- **Testing**: 1 day
- **Total per chain**: 3 days