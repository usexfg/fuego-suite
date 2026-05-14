# Fuego TUI

A minimal terminal UI for controlling `fuegod` and `walletd`, with full support for **Elderfier Staking** and **Burn2Mint (XFG→HEAT)** flows.

## Prerequisites

- **Go 1.20+** installed: https://go.dev/dl/
- Build the C++ binaries (`fuegod`, `walletd`) in `build/src` or have them on your PATH
- *Optional*: `xfg-stark` CLI for STARK proof generation (for Burn2Mint)

## Build

```bash
cd tui
go mod tidy
go build -o fuego-tui
```

## Run

```bash
./fuego-tui
```

Navigate with **arrow keys** or **j/k**, select with **Enter**, quit with **q** or **Ctrl+C**.

---

## Features

### 🔥 Node & Wallet Controls
- **Start/Stop Node** - Launch `fuegod` daemon with RPC
- **Start Wallet RPC** - Launch `walletd` for wallet operations
- **Create Wallet** - Generate new XFG wallet
- **Get Balance** - Query wallet balance
- **Send Transaction** - Transfer XFG to another address

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
- Minimum stake: **800 XFG**
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
  • Stake: 800.00 XFG
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

### 🔥➡️💎 Burn2Mint Menu
Complete **XFG → HEAT** minting flow:

#### Flow Steps:
1. **Burn XFG on Fuego**
   - Choose burn amount: **0.8 XFG** (minimum) or **800 XFG** (large)
   - Creates `burn_deposit` transaction on Fuego blockchain
   
2. **Wait for Confirmations**
   - Transaction must be confirmed on-chain (10+ blocks)
   
3. **Request Elderfier Consensus**
   - Queries Elder Council for **proof of burn transaction**
   - Elderfier nodes verify and sign the burn
   - Returns `eldernode_proof` used as STARK inputs
   
4. **Generate STARK Proof**
   - Calls `xfg-stark generate-proof` CLI with:
     - `--tx-hash` (burn transaction hash)
     - `--amount` (burned amount in atomic units)
     - `--eldernode-proof` (consensus proof from step 3)
   - Produces **XFG-STARK proof** for L2 verification
   
5. **Submit to Arbitrum L2**
   - User calls `claimHEAT()` on Arbitrum with:
     - STARK proof (from step 4)
     - Eldernode proof (from step 3)
     - L1 gas fees (msg.value: ~0.001-0.01 ETH)
   
6. **Receive HEAT on Ethereum L1**
   - Arbitrum relays transaction to L1
   - HEAT tokens minted on Ethereum mainnet

**RPC Endpoints Used:**
- `create_burn_deposit` - Creates burn transaction
- `request_elderfier_consensus` - Requests Elder Council proof

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
│  3. Request Elderfier Consensus                             │
│     └─> request_elderfier_consensus RPC                     │
│         Input:  tx_hash, amount                             │
│         Output: eldernode_proof ◄── CRITICAL INPUT          │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  4. Generate STARK Proof (xfg-stark CLI)                    │
│     └─> xfg-stark generate-proof \                          │
│           --tx-hash <hash> \                                │
│           --amount <atomic> \                               │
│           --eldernode-proof <proof>  ◄── FROM STEP 3        │
│         Output: STARK proof file                            │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  5. Submit to Arbitrum L2 (Manual/Web UI)                   │
│     └─> claimHEAT() with:                                   │
│         • STARK proof                                       │
│         • eldernode_proof                                   │
│         • msg.value (L1 gas: 0.001-0.01 ETH + 20% buffer)  │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  6. Arbitrum → Ethereum L1                                  │
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
- **IMPORTANT**: Elderfier consensus is required BEFORE STARK generation
- The `eldernode_proof` from consensus is a **required input** to `xfg-stark`
- Without Elderfier consensus, STARK proof cannot be generated
- L1 gas fees: Always include **20% buffer** to avoid restart costs

### Binary Detection
- TUI looks for binaries in `../build/src/` (development)
- Falls back to `$PATH` (production)
- Supported binaries: `fuegod`, `walletd`, `xfg-stark`

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
- `get_burn2mint_requests` - Lists pending Burn2Mint consensus requests
- `provide_consensus_proof` - Provides consensus proof for burn tx
- `request_elderfier_consensus` - **Critical**: Returns eldernode_proof for STARK

### Testing

```bash
# Start node and wallet
./fuego-tui
# Select: Start Node → Start Wallet RPC

# Test Elderfier menu
# Select: Elderfier Menu

# Test Burn2Mint (requires xfg-stark)
# Select: Burn2Mint Menu → Choose amount → Follow prompts
```

---

## Next Steps

- [ ] Implement missing RPC endpoints in `walletd`
- [ ] Build `xfg-stark` CLI for proof generation
- [ ] Create menubar/tray app for Elderfiers (read-only monitoring)
- [ ] Web UI for L2 submission with MetaMask integration
