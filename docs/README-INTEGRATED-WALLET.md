# Fuego Integrated Wallet

The Fuego Integrated Wallet combines the simplicity of xfg_wallet (SimpleWallet) with the powerful RPC capabilities of walletd, creating a unified wallet experience for Fuego's banking and deposit functions.

## Features

- **Unified Interface**: Single executable that provides both CLI and RPC functionality
- **Easy Deployment**: No need to run separate walletd service
- **Full Compatibility**: Supports all Fuego wallet operations including deposits
- **Flexible Configuration**: Can be run with or without RPC server

## Quick Start

### Basic Usage (CLI Only)
```bash
# Start wallet in interactive CLI mode only
./xfg_wallet --wallet-file=mywallet.wallet --password=mypassword
```

### With Integrated RPC Server
```bash
# Start wallet with integrated RPC server
./xfg_wallet --wallet-file=mywallet.wallet --password=mypassword --rpc-enable

# Or specify custom RPC settings
./xfg_wallet --wallet-file=mywallet.wallet --password=mypassword \
  --rpc-enable --rpc-bind-ip=0.0.0.0 --rpc-bind-port=8070
```

## Command Line Options

### Basic Wallet Options
- `--wallet-file FILE`     : Wallet container file
- `--password PASS`        : Wallet password
- `--daemon-address ADDR`  : Daemon address (host:port)

### RPC Server Options
- `--rpc-enable`           : Enable integrated RPC server
- `--rpc-bind-ip IP`       : RPC server bind IP (default: 127.0.0.1)
- `--rpc-bind-port PORT`   : RPC server bind port (default: 8070)
- `--rpc-user USER`        : RPC server username (optional)
- `--rpc-password PASS`    : RPC server password (optional)

## Integrated Commands

When running the wallet, you have access to both traditional CLI commands and new RPC management commands:

### Traditional Wallet Commands
- `balance`                : Show current wallet balance
- `address`                : Show wallet address
- `transfer`               : Send funds
- `list_transfers`         : Show transaction history
- And many more...

### New RPC Management Commands
- `start_rpc [ip] [port]`  : Start RPC server (optionally with custom IP/port)
- `stop_rpc`               : Stop RPC server
- `rpc_status`             : Show RPC server status

## Example Usage

### Starting the Wallet with RPC
```bash
# Start wallet with RPC server on default settings
./xfg_wallet --wallet-file=mywallet.wallet --password=secure123 --rpc-enable

# Wallet starts in CLI mode with RPC server running in background
# You can now use both CLI commands and make RPC calls to localhost:8070
```

### Managing RPC Server from CLI
```
[wallet abc123]: rpc_status
RPC server is running
  Bind IP: 127.0.0.1
  Bind Port: 8070

[wallet abc123]: stop_rpc
RPC server stopped

[wallet abc123]: start_rpc 0.0.0.0 9000
Starting RPC server on 0.0.0.0:9000
RPC server started successfully

[wallet abc123]: rpc_status
RPC server is running
  Bind IP: 0.0.0.0
  Bind Port: 9000
```

## RPC API Usage

With the integrated RPC server running, you can make JSON-RPC calls to the same port:

```bash
# Get wallet balance via RPC
curl -X POST http://localhost:8070/json_rpc \
  -H 'Content-Type: application/json' \
  -d '{
    "jsonrpc": "2.0",
    "id": "0",
    "method": "getbalance"
  }'
```

## Fuego Banking Functions

The integrated wallet fully supports Fuego's specialized banking features:

1. **Burn Deposits**: Create and manage burn deposits through both CLI and RPC
2. **COLD Secrets**: Manage COLD deposit secrets with `create_COLD_secret` command
3. **Proof Generation**: Generate proofs for deposits with `generate_proof` command
4. **Integrated Addresses**: Create payment ID integrated addresses

## Security Best Practices

1. **Network Binding**: When running RPC server, bind to localhost (127.0.0.1) unless external access is required
2. **Authentication**: Use RPC username/password for external access
3. **Firewall**: Restrict access to RPC port when exposed externally
4. **Wallet Protection**: Always use strong passwords for wallet containers

## Integration Benefits

### Simplified Deployment
- Single executable instead of managing both xfg_wallet and walletd
- No need to synchronize two separate processes
- Reduced system resources usage

### Enhanced Usability
- Access to both CLI and RPC functionality from one process
- Real-time status information about RPC server
- Easy start/stop management of RPC service

### Full Compatibility
- All existing xfg_wallet functionality preserved
- Full walletd RPC API compatibility
- No changes required for existing applications using walletd RPC

## Troubleshooting

### Common Issues

1. **"Port already in use"**: Change the RPC bind port or stop conflicting service
2. **"RPC server failed to start"**: Check that wallet is loaded before starting RPC
3. **"Connection refused"**: Verify RPC server is running and binding to correct IP

### Logs
Check the console output for detailed information about RPC server status and errors.
```

Now you have a fully integrated wallet solution that combines the best of both xfg_wallet and walletd! The integration provides:

1. **Single Executable**: No need to manage separate processes
2. **Flexible Deployment**: Can run CLI-only or with integrated RPC
3. **Unified Management**: Control RPC server directly from wallet CLI
4. **Full Compatibility**: Maintains all existing functionality
5. **Easy Configuration**: Simple command-line options to enable RPC

The integration makes walletd functionality feel like a natural part of the wallet rather than an unintegrated piece of software, while preserving all the power and flexibility of the original walletd RPC server.