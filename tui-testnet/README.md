# Fuego TUI - TESTNET VERSION

A minimal terminal UI for controlling `fuegod` and `walletd` on **TESTNET**, with full support for **Burn2Mint (XFG→HEAT)** flows.

## Testnet Configuration
- **Testnet Node RPC**: 20808
- **Testnet Wallet RPC**: 28280  
- **Data Directory**: `~/.fuego-testnet` or `~/Library/Application Support/Fuego-testnet`
- **Network**: Testnet (use with testnet XFG only)

## Prerequisites

- **Go 1.20+** installed: https://go.dev/dl/
- Build the C++ binaries (`fuegod`, `walletd`) in `build/src` or have them on your PATH
- *Optional*: `xfg-stark` CLI for STARK proof generation (for Burn2Mint)

## Build

```bash
cd tui-testnet
go mod tidy
go build -o fuego-tui-testnet
```

## Run

```bash
./fuego-tui-testnet
```

Navigate with **arrow keys** or **j/k**, select with **Enter**, quit with **q** or **Ctrl+C**.

---

## Features

### 🔥 Node & Wallet Controls
- **Start/Stop Node** - Launch `testnetd` daemon with **testnet** RPC on port 28081
- **Start Wallet RPC** - Launch `walletd` with **testnet** config on port 28082
- **Create Wallet** - Generate new testnet XFG wallet
- **Get Balance** - Query wallet balance (testnet)
- **Send Transaction** - Transfer testnet XFG to another address

### 🔥➡️💎 Burn2Mint Menu
Complete **XFG → HEAT** minting flow:

#### Flow Steps:
1. **Burn XFG on Fuego**
   - Choose burn amount: **0.8 XFG** (minimum) or **800 XFG** (large)
   - Creates `burn_deposit` transaction on Fuego blockchain
   
2. **Wait for Confirmations**
   - Transaction must be confirmed on-chain (10+ blocks)
   
3. **Generate STARK Proof**
   - Calls `fuego-prover-cli prove` with:
     - Fuego RPC endpoint
     - Start and end heights
     - Checkpoint hash
   - Followed by `fuego-prover-cli claim` with:
     - `--commitment` (hex)
     - `--preimage` (hex)
     - `--recipient` (eth_addr)
   - Produces **XFG-STARK proof** and claim data for L2 verification
   
4. **Submit to Arbitrum L2**
   - User calls `claimHEAT()` on Arbitrum with:
     - STARK proof (from step 3)
     - Preimage and Merkle Proof data
     - L1 gas fees (msg.value: ~0.001-0.01 ETH)
   
5. **Receive HEAT on Ethereum L1**
   - Arbitrum relays transaction to L1
   - HEAT tokens minted on Ethereum mainnet

**RPC Endpoints Used:**
- `create_burn_deposit` - Creates burn transaction on testnet
- **Uses testnet ports: 28081 (node) and 28082 (wallet)**

**External Tools:**
- `xfg-stark` CLI - Generates zk-STARK proof

---

## Architecture

### Burn2Mint Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│  1. Burn XFG on Fuego (0.8 or 800 XFG)                      │
│     └─> create_burn_deposit RPC → tx_hash                   │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  2. Wait for Confirmations (10+ blocks)                     │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  3. Generate STARK Proof (fuego-prover-cli)                 │
│     └─> fuego-prover prove & claim                          │
│         Output: STARK proof file & claim data               │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  4. Submit to Arbitrum L2 (Manual/Web UI)                   │
│     └─> claimHEAT() with:                                   │
│         • STARK proof                                       │
│         • preimage & merkleProof                            │
│         • msg.value (L1 gas: 0.001-0.01 ETH + 20% buffer)  │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  5. Arbitrum → Ethereum L1                                  │
│     └─> HEAT minted on Ethereum mainnet                     │
│         Leftover gas fees automatically refunded            │
└─────────────────────────────────────────────────────────────┘
```

---

## Usage Notes

### Burn2Mint Menu
- **IMPORTANT**: Use `fuego-prover-cli` (the new ZK bridge) instead of EFier consensus for proof generation.
- L1 gas fees: Always include **20% buffer** to avoid restart costs

### Binary Detection
- TUI looks for binaries in `../build/src/` (development)
- Falls back to `$PATH` (production)
- Supported binaries: `fuegod`, `walletd`, `xfg-stark`
- **Testnet modes use `~/.fuego-testnet` data directory**

---

## Development

### RPC Endpoints to Implement

The following RPC methods are called by the TUI but may need implementation:

**Burn2Mint:**
- `create_burn_deposit` - Creates burn transaction


### Testing (Testnet)

```bash
# Start node and wallet in testnet mode
./fuego-tui-testnet
# Select: Start Node → Start Wallet RPC

# Test Burn2Mint (requires xfg-stark)
# Select: Burn2Mint Menu → Choose amount → Follow prompts

# Check testnet data
ls -la ~/.fuego-testnet/
```

## Testnet Specific Notes

- **IMPORTANT**: All operations use testnet XFG only
- Testnet RPC ports are 28081 and 28082 (different from mainnet)
- Separate data directory prevents mixing testnet/mainnet data
- Testnet nodes connect to testnet peers only
- Testnet wallets use testnet addresses (different prefix)

---

## Next Steps

- [ ] Implement missing RPC endpoints in `walletd`
- [ ] Build `fuego-prover-cli` for ZK bridge proof generation
- [ ] Web UI for L2 submission with MetaMask integration
