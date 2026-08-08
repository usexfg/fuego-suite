# Polkadot Compatibility Plan

This directory tracks the design for adding Polkadot (DOT) and CLV Parachain
(a.k.a. "Robinhood Chain") support to Fuego SwapXFG.

## Background

Polkadot is a heterogeneous multi-chain (parachain) network using:
- **Consensus**: Nominated Proof of Stake (NPoS)
- **Runtime**: Substrate framework
- **Transaction Format**: SCALE-encoded extrinsics (NOT EVM-style)
- **Address Format**: SS58 (bech32 variant with curve prefix)
- **HTLC Support**: Supported via Substrate's `pallet-hashTimeLock` (if runtime includes it)

## Why Polkadot is Different

Polkadot chains (including CLV Parachain) use the **Substrate** framework, not
Cosmos SDK or Evmos. The transaction model is fundamentally different:
- No Ethereum-style signed transactions
- No `eth_*` RPC methods
- No Solidity-based HTLC contracts
- Uses SCALE encoding for all RPC calls

This means Polkadot requires a **custom chain client** — it cannot use the
header-only inheritance pattern from `EthChainClient`.

## Chains to Integrate

| Chain | Chain ID | Type | Notes |
|-------|----------|------|-------|
| **CLV Parachain** (a.k.a. "Robinhood") | 1024 | Substrate parachain | Has EVM module (dual EVM/Substrate) |
| **Polkadot** (Relay Chain) | N/A (no chain ID; uses SS58 prefix 0) | Substrate relay chain | Parent of all parachains |

## CLV Parachain (1024) — Hybrid Approach

The CLV Parachain is unique: it supports **both** Substrate-style extrinsics
(SS58 addresses) AND Ethereum-style EVM calls (0x addresses). This means we
could potentially use the existing `EthChainClient` for the EVM side, with
the following adaptations:

- **EVM Address**: Uses standard 0x Ethereum addresses on EVM module
- **Cosmos/Substrate Address**: Uses bech32 (CLV prefix) for native transfers
- **HTLC**: Can deploy Solidity HTLC on EVM module (same as ETH/BSC/Polygon)
- **Gas**: Payable in CLV tokens, EVM gas via EIP-1559

### Implementation Approach for CLV

Since CLV has an EVM module, we can use the **header-only inheritance** pattern
(the same as Polygon/BSC). The key adaptation is that CLV's EVM module requires
special handling for gas payment in CLV (not ETH).

**Approach**: Create a modified `EthChainClient` subclass that handles
CLV-specific gas payment, but still uses standard EVM HTLC contracts.

### Files to Create

```
src/SwapDaemon/CLV/CLVChainClient.h       ← Header inheriting EthChainClient
src/SwapDaemon/Polkadot/SubstrateRpcClient.h   ← Substrate JSON-RPC client (future)
src/SwapDaemon/Polkadot/SubstrateRpcClient.cpp ← SCALE-encoded RPC calls (future)
```

## Polkadot Relay Chain — Full Substrate Client (Future)

Polkadot itself does NOT have an EVM module on the relay chain. To integrate
the Polkadot relay chain for swaps, we need:

1. **Substrate JSON-RPC client** — SCALE-encoded RPC calls (NOT HTTP JSON like Ethereum)
2. **SS58 address handling** — Different bech32 variant with chain-specific prefixes
3. **Custom HTLC logic** — Substrate's `pallet-hashTimeLock` (if runtime supports it)
4. **XCM format** — Cross-consensus messaging for multi-chain asset transfers

### Substrate RPC Methods (Key Differences from Ethereum)

| Ethereum RPC | Substrate RPC | Notes |
|--------------|---------------|-------|
| `eth_chainId` | `system_chainId` / `system_chain` | Returns genesis hash, not EIP-155 number |
| `eth_getBalance` | `system_accountBalance` | SCALE-encoded response |
| `eth_sendRawTransaction` | `author_submitExtrinsic` | SCALE-encoded extrinsic |
| `eth_call` | `state_call` (with `EthCallRuntimeApi`) | If EVM runtime module is present |
| `eth_getBlockByNumber` | `chain_getBlockByNumber` | SCALE-encoded block |

### Files to Create (Polkadot Relay Chain)

```
src/SwapDaemon/Polkadot/
├── SubstrateRpcClient.h      ← SCALE JSON-RPC client
├── SubstrateRpcClient.cpp    ← SCALE encoding/decoding
├── PolkadotChainClient.h     ← IChainClient implementation
├── PolkadotChainClient.cpp   ← Swap method implementations
├── Ss58Address.h             ← SS58 address encoding/decoding
├── Ss58Address.cpp           ← SS58 encoding with Polkadot prefix
└── tests/                    ← Unit tests
```

## Estimated Effort

| Component | Effort | Notes |
|-----------|--------|-------|
| **CLV (EVM)** | 2-3 days | Header-only inheritance, same as Polygon |
| **Polkadot Relay** | 10-14 days | Full Substrate client, SCALE encoding, custom HTLC |
| **Testing** | 5-7 days each | Integration testing |
| **Total (CLV + Polkadot)** | 15-21 days | Staged approach recommended |

## Staging Sequence

1. **Phase 1**: CLV Parachain (EVM side) — quick win via header-only pattern
2. **Phase 2**: Polkadot Relay Chain — full Substrate client implementation

## Dependencies

- Substrate SDK documentation (`@polkadot/api`) for SCALE encoding reference
- CLV Parachain documentation (EVM module parameters)
- `pallet-hashTimeLock` runtime support verification
- XCM v3 format documentation (for cross-chain swaps)