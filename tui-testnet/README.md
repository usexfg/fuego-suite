# Fuego TUI - TESTNET VERSION

A minimal terminal UI for controlling `fuegod` and `walletd` on **TESTNET**, with full support for **Elderfier Staking** and **Burn2Mint (XFG→HEAT)** flows.

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

### 👑 Elderfier Menu (READ/WRITE)
Full Elderfyre Staking dashboard with voting and consensus powers.

#### For Non-Stakers (No EFdeposit)
Shows instructions and **Elderfyre Stayking** wizard:
- **Start Elderfyre Stayking Process** - Interactive 3-step wizard
- **Check Stake Status** - View any pending stakes

#### For Active Elderfiers (Confirmed EFdeposit)
Full Elder Council access with read/write capabilities:
- **View Consensus Requests** - All pending consensus items
- **Vote on Pending Items** - Submit approve/reject votes on proposals
- **Review Burn2Mint Requests** - Approve/deny burn consensus proofs
- **Manage Stake** - View stake details, increase stake amount
- **Update ENindex Keys** - Modify Elderfier ID registration

### Elderfyre Stayking Process

**3-Step Interactive Wizard:**

**Step 1: Create Stake Deposit**
- Minimum stake: **10,000 XFG**
- Creates `elderfier_stake` transaction type
- Returns transaction hash for tracking

**Step 2: Generate Elderfier ID**
- Must be exactly **8 characters** (alphanumeric)
- Unique identifier across the network
- Examples: `ELDER001`, `FIRENODE`, `FUEGO777`

**Step 3: Register to ENindex**
- Links Elderfier ID to wallet address
- Registers stake tx hash in ENindex
- Enables Elder Council voting powers
- Requires 10 block confirmations

**Completion Summary:**
```
🎉 ELDERFYRE STAYKING COMPLETE!

Summary:
  • Stake: 10000.00 XFG
  • Elderfier ID: ELDER001
  • TX Hash: abc123...
  • ENindex: Registered

You can now access Elder Council Inbox
once your stake is confirmed (10 blocks)
```

**RPC Endpoints Used:**
- `get_stake_status` - Check stake deposit status
- `create_stake_deposit` - Create Elderfier stake transaction
- `getAddresses` - Get wallet public address
- `register_to_enindex` - Register to ENindex with ID
- `increase_stake` - Add more XFG to stake
- `get_elder_inbox` - Get Elder Council messages
- `get_consensus_requests` - List pending consensus items
- `get_pending_votes` - List items requiring votes
- `submit_vote` - Submit approve/reject vote
- `get_burn2mint_requests` - List pending Burn2Mint consensus
- `provide_consensus_proof` - Provide consensus for burns
- `update_enindex` - Update ENindex registration

**All RPC requests use testnet ports: 28081 (node) and 28082 (wallet)**

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

### Elderfier Menu
- Full **read/write** access for staked Elderfiers
- Non-stakers can complete Elderfyre Stayking wizard
- Requires **10,000 XFG minimum stake**
- Elderfier ID must be exactly **8 alphanumeric characters**
- Elder Council access unlocked after 10 block confirmations
- No rewards for staking - voting power only
- Perfect for menubar/tray app integration

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

**Elderfier Staking:**
- `get_stake_status` - Returns stake deposit info and confirmation status
- `create_stake_deposit` - Creates Elderfier stake transaction (min 10000 XFG)
- `getAddresses` - Returns wallet public address
- `register_to_enindex` - Registers Elderfier ID + stake to ENindex
- `update_enindex` - Updates ENindex registration
- `increase_stake` - Adds more XFG to existing stake

**Elder Council Inbox:**
- `get_elder_inbox` - Returns Elder Council messages and proposals
- `get_consensus_requests` - Lists all pending consensus items
- `get_pending_votes` - Lists items requiring Elderfier votes
- `submit_vote` - Submits approve/reject vote on an item

**Burn2Mint Consensus:**
- `create_burn_deposit` - Creates burn transaction


### Testing (Testnet)

```bash
# Start node and wallet in testnet mode
./fuego-tui-testnet
# Select: Start Node → Start Wallet RPC

# Test Elderfier menu
# Select: Elderfier Menu

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
- [ ] Create menubar/tray app for Elderfiers (read-only monitoring)
- [ ] Web UI for L2 submission with MetaMask integration
