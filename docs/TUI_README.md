# Fuego TUI Applications

This directory contains two fully functional Terminal User Interface (TUI) applications for interacting with the Fuego blockchain:

1. **fuego-tui** - Mainnet TUI application
2. **testnet-tui** - Testnet TUI application

## Prerequisites

- Go 1.20 or higher
- Fuego node binaries (`fuegod` and `walletd`) built in `build/src/` directory
- Optional: `xfg-stark` CLI for Burn2Mint STARK proof generation

## Building

### Option 1: Using the unified build script (recommended)

```bash
cd /home/ar/fuego
./build-tui.sh
```

### Option 2: Building individually

```bash
# Build mainnet TUI
cd tui
./build.sh

# Build testnet TUI
cd ../tui-testnet
./build.sh
```

### Option 3: Manual build

```bash
# Build mainnet TUI
cd tui
go mod tidy
go build -o fuego-tui

# Build testnet TUI
cd ../tui-testnet
go mod tidy
go build -o testnet-tui
```

## Running

### Mainnet TUI
```bash
cd tui
./fuego-tui
```

### Testnet TUI
```bash
cd tui-testnet
./testnet-tui
```

## Features

Both TUI applications provide full functionality for:

### 🔥 Node & Wallet Controls
- **Start/Stop Node** - Launch `fuegod` daemon with RPC
- **Start Wallet RPC** - Launch `walletd` for wallet operations
- **Create Wallet** - Generate new wallet
- **Get Balance** - Query wallet balance
- **Send Transaction** - Transfer coins to another address

### 👑 Elderfier Menu (READ/WRITE)
Full Elderfyre Staking dashboard with voting and consensus powers.

#### For Non-Stakers
- **Start Elderfyre Stayking Process** - Interactive 3-step wizard
- **Check Stake Status** - View any pending stakes

#### For Active Elderfiers
- **View Consensus Requests** - All pending consensus items
- **Vote on Pending Items** - Submit approve/reject votes on proposals
- **Review Burn2Mint Requests** - Approve/deny burn consensus proofs
- **Manage Stake** - View stake details, increase stake amount
- **Update ENindex Keys** - Modify Elderfier ID registration

### 🔥➡️💎 Burn2Mint Menu
Complete **XFG → HEAT** minting flow:

1. **Burn XFG** on Fuego (0.8 XFG minimum or 800 XFG large)
2. **Wait for Confirmations** (10+ blocks)
3. **Request Elderfier Consensus** for proof generation
4. **Generate STARK Proof** using `xfg-stark` CLI
5. **Submit to Arbitrum L2** (manual step)
6. **Receive HEAT** on Ethereum L1

## Key Differences

| Feature | Mainnet TUI | Testnet TUI |
|---------|-------------|-------------|
| Binary Name | `fuego-tui` | `testnet-tui` |
| Data Directory | `~/.fuego` | `~/.fuego-testnet` |
| Node RPC Port | 18180 | 28280 |
| Wallet RPC Port | 18183 | 28283 |
| Node P2P Port | 10808 | 20808 |
| Coin Name | XFG | TEST |
| Minimum Stake | 800 XFG | 80 XFG |
| Coin Units | 10,000,000 atomic units | 10,000,000 atomic units |

## Navigation

- **Arrow Keys** or **j/k** - Navigate menu items
- **Enter** - Select menu item
- **q** or **Ctrl+C** - Quit application

## Troubleshooting

### Binary Not Found
If you get "binary not found" errors:
1. Ensure `fuegod` and `walletd` are built in `build/src/`
2. Or ensure they're available in your system PATH

### Connection Issues
- Make sure no other instances of the node/wallet are running
- Check that the required ports are available
- Verify firewall settings if connecting remotely

### RPC Endpoints
The TUI uses the following RPC endpoints:
- Node: `http://127.0.0.1:<port>/get_info`
- Wallet: `http://127.0.0.1:<port>/json_rpc`

### Debugging RPC Issues
If you encounter RPC connection errors like "invalid character 'R' looking for beginning of value", this typically means the node is returning an unexpected response format. The TUI now includes enhanced error handling that will show more detailed information about what the node is actually returning.

Common causes and solutions:
1. **Node not running**: Make sure the Fuego node is started and listening on the correct port
2. **Wrong port**: Verify the node is running on the expected port (18180 for mainnet, 28280 for testnet)
3. **CORS/HTTP issues**: The node might be returning an error page instead of JSON
4. **Version mismatch**: The RPC API might have changed in newer versions

To diagnose RPC issues, you can check what the node is actually returning:
- Look at the detailed error messages in the TUI logs
- Use curl to test the endpoint directly:
  ```bash
  curl -v http://127.0.0.1:18180/get_info  # for mainnet
  curl -v http://127.0.0.1:28280/get_info  # for testnet
  ```

## Development

### Updating Dependencies
```bash
# In tui or tui-testnet directory
go mod tidy
```

### Code Structure
- `main.go` - Main application logic
- `go.mod` - Go module dependencies
- `build.sh` - Build script
- `README.md` - Documentation

### Required RPC Methods
The TUI expects the following RPC methods to be implemented:
- `get_info` (node)
- `create_address` (wallet)
- `get_balance` (wallet)
- `send_transaction` (wallet)
- `create_stake_deposit` (wallet)
- `get_stake_status` (wallet)
- `get_addresses` (wallet)
- `register_to_enindex` (wallet)
- `get_consensus_requests` (wallet)
- `get_pending_votes` (wallet)
- `submit_vote` (wallet)
- `create_burn_deposit` (wallet)
- `request_elderfier_consensus` (wallet)
- `get_burn2mint_requests` (wallet)
- `provide_consensus_proof` (wallet)
- `increase_stake` (wallet)
- `update_enindex` (wallet)

## Support

For issues with the TUI applications, please check:
1. That all dependencies are properly installed
2. That the Fuego node binaries are correctly built
3. That required ports are available