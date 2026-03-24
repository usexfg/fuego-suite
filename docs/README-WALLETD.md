# Fuego Interactive Wallet Daemon (walletd)

The Fuego Interactive Wallet Daemon (`walletd`) is a powerful service that manages cryptocurrency wallets, deposits, and transactions for the Fuego network. This improved version includes an interactive command-line interface that makes it easier to use while maintaining full RPC functionality.

## Features

- **Interactive CLI Mode**: User-friendly command-line interface for direct wallet management
- **Full RPC Server**: JSON-RPC API for application integration
- **Container Management**: Secure storage of wallets and transaction history
- **Deposit Support**: Specialized handling of Fuego's burn deposits for banking functions
- **Background Service**: Can run as a daemon/service for continuous operation

## Quick Start

### Using the Startup Script (Recommended)

```bash
# Make sure you're in the Fuego project root
cd /path/to/fuego

# Run the interactive startup script
./scripts/start-walletd-interactive.sh -w mywallet.wallet -p mypassword
```

### Manual Launch

```bash
# Generate a new wallet container (if needed)
./build/release/bin/walletd --generate-container --container-file=mywallet.wallet --container-password=mypassword

# Start interactive wallet daemon
./build/release/bin/walletd --container-file=mywallet.wallet --container-password=mypassword --interactive
```

## Command-Line Options

### Basic Options
- `-w, --container-file FILE`     Wallet container file (stores your wallets and transactions)
- `-p, --container-password PASS` Password to unlock the wallet container
- `-i, --interactive`             Run in interactive mode with CLI interface
- `-d, --daemon`                  Run as background daemon/service

### Network Options
- `--bind-address ADDRESS`        Payment service bind address (default: 0.0.0.0)
- `--bind-port PORT`              Payment service bind port (default: 8070)
- `--rpc-user USER`               Username for RPC authentication
- `--rpc-password PASS`           Password for RPC authentication

### Advanced Options
- `--generate-container, -g`      Generate new wallet container file with one wallet and exit
- `--view-key KEY`                Generate a wallet container with this secret view key
- `--spend-key KEY`               Generate a wallet container with this secret spend key
- `--log-file FILE`               Log file path
- `--log-level LEVEL`             Log level (0-4, default: 3)
- `--server-root DIR`             Server root directory
- `--address`                     Print wallet addresses and exit

## Interactive Commands

When running in interactive mode, you can use these commands:

- `help`, `h`          - Show help message
- `status`, `st`       - Show wallet synchronization status
- `balance`, `bal`     - Show wallet balances
- `address`, `addr`    - Show wallet addresses
- `deposits`, `dep`    - Show deposit information (Fuego specific)
- `quit`, `exit`, `q`  - Stop the daemon and exit

## Wallet Containers Explained

In Fuego, a "container" is a secure file that holds:

1. **Wallet Keys**: Encrypted public/private key pairs for your addresses
2. **Transaction History**: Records of all transactions related to your wallets
3. **Balance Information**: Current and pending balances
4. **Deposit Data**: Special Fuego banking deposit information

The container terminology emphasizes the secure, organized storage of complex wallet data rather than just a simple configuration file.

## Integration with Applications

The wallet daemon also provides a full JSON-RPC API for application integration. The interactive mode runs the same RPC server in the background, so you can connect external applications while using the CLI.

### Example RPC Call
```bash
curl -X POST http://localhost:8070/json_rpc \
  -H 'Content-Type: application/json' \
  -d '{
    "jsonrpc": "2.0",
    "id": "0",
    "method": "getBalance"
  }'
```

## Fuego Banking Functions

Fuego includes specialized deposit functionality for banking operations:

1. **Burn Deposits**: Users can create deposits by burning coins, which are then tracked by the Eldernode network
2. **Deposit Management**: View and track deposit status through the wallet
3. **Interest-like Rewards**: Deposits may qualify for network rewards (implementation dependent)

## Security Best Practices

1. **Strong Passwords**: Always use strong, unique passwords for wallet containers
2. **Secure Storage**: Keep wallet container files in secure locations with restricted access
3. **Backup Containers**: Regularly backup wallet container files
4. **Network Security**: When running RPC server, use authentication and restrict bind addresses when possible

## Troubleshooting

### Common Issues

1. **"Container file not found"**: Make sure the path to your wallet file is correct
2. **"Wrong password"**: Verify your container password is correct
3. **"Port already in use"**: Change the bind port or stop the conflicting service
4. **"Failed to start daemon"**: Check log files for detailed error information

### Getting Help

For issues not covered in this document:

1. Check the log file (default: `payment_gate.log`)
2. Visit the Fuego community forums
3. File an issue on the GitHub repository