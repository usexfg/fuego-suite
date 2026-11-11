# TON (The Open Network) Integration Plan

## Overview

TON (The Open Network) is a Layer 1 blockchain originally developed by Telegram
(now maintained by the TON Foundation). It has a massive user base (~900M+
Telegram users) and uses a unique Proof-of-Stake consensus with sharding.
TON's account model and smart contract platform (TVM — TON Virtual Machine)
differ significantly from UTXO and EVM chains, requiring a custom implementation.

## Technical Fit

- **Model**: Account-based with sharding (unique architecture)
- **Smart Contract Platform**: TVM (TON Virtual Machine), not EVM
- **HTLC Support**: TON supports smart contracts with hashlock/timelock logic
- **Address Format**: 64-char base64 (user-friendly)
- **Block Time**: ~5 seconds
- **RPC Port**: 2990 (lite server)
- **Chain ID**: N/A (TON uses workchain IDs)

## Implementation Approach

**Template**: Copy `Solana/` directory as starting point.
Like Solana, TON is a non-UTXO, non-EVM chain with a custom account model.
The implementation will be the most complex of all new chains.

### Files to Create

| File | Source Template | Notes |
|------|----------------|-------|
| `TonChainClient.h` | `Solana/SolChainClient.h` | Adapted for TON account model |
| `TonChainClient.cpp` | `Solana/SolChainClient.cpp` | TON-specific logic |
| `TonRpcClient.h` | `Solana/SolRpcClient.h` | TON lite server RPC |
| `TonRpcClient.cpp` | `Solana/SolRpcClient.cpp` | TON JSON-RPC implementation |
| `TonHtlcScript.h` | New file | TON-specific HTLC contract |
| `TonHtlcScript.cpp` | New file | TON TVM bytecode for HTLC |
| `tests/` | `Solana/tests/` (adapted) | Adapt test parameters |

### Key Differences from Solana

| Parameter | Solana | TON |
|-----------|--------|-----|
| Account model | Program-derived | Global state + individual accounts |
| Smart contract | Rust/SBF | FunC / TVM bytecode |
| Block time | ~400ms | ~5s |
| RPC endpoint | 8899 | 2990 (lite) / 443 (full) |
| Address format | Base58 (pubkey) | Base64 (64-char) |
| HTLC approach | Program account | Smart contract (TVM) |
| Fee model | Rent + lamports | Gas (GRAM) |
| Workchains | N/A | 0 (master), -1 (validators) |

## Integration Points

### SwapTypes.h
```cpp
TON = 12,  // after POLYGON = 11
```

### SwapTimelock.cpp
```cpp
case SwapPair::TON: return 5000;  // 5 seconds per block
```

### AdaptorSwap.cpp
```cpp
case SwapPair::TON: {
    // TON-specific hashlock (different from SHA-256 and Keccak)
    // TON uses its own hash function (SHA-256 variant)
}
```

### PriceOracle.cpp
```cpp
case SwapPair::TON: return SEED_TON_USD / xfgUsd;
// Add seed rate: static const double SEED_TON_USD = 5.50;
```

### SwapDaemon.cpp
```cpp
// Register TON chain client
```

### SwapDaemon.h (ChainClientConfig)
```cpp
// TON
std::string tonHost;
uint16_t    tonPort     = 2990;
std::string tonRpcUser;
std::string tonRpcPass;
std::string tonWalletKey;  // TON wallet private key (hex)
std::string tonHtlcAddress; // Deployed HTLC contract address
int         tonWorkchain  = 0;  // TON workchain ID
```

### swapxfg.html
```html
<option value="TON">TON (TON)</option>
```

### swapxfg.js CHAIN_INFO
```javascript
TON: { icon: '/coin-icons/ton.png', color: '#0098ea', ticker: 'TON', name: 'TON' }
```

## Icon Status

- `dashboard/static/coin-icons/ton.png` — ✅ Already exists

## Estimated Effort

- **Implementation**: 10-14 days (custom account model, TVM HTLC contract, unique RPC)
- **Testing**: 5-7 days (TON testnet deployment, TVM bytecode testing)
- **Total**: 15-21 days

## Dependencies

- TON lite server or full node running
- TON wallet with sufficient GRAM for gas fees
- HTLC smart contract deployed on TON testnet/mainnet
- TVM/FunC compiler toolchain for HTLC contract development
- TON SDK for contract interaction