# Fuego TUI - TESTNET VERSION

A minimal terminal UI for controlling `fuegod` and `walletd` on **TESTNET**, with full support for **Burn (XFG→HEAT)** flows.

## Testnet Configuration
- **Testnet Node RPC**: 20808
- **Testnet Wallet RPC**: 28280  
- **Data Directory**: `~/.fuego-testnet` or `~/Library/Application Support/Fuego-testnet`
- **Network**: Testnet (use with testnet XFG only)

## Prerequisites
- **Go 1.20+** installed: https://go.dev/dl/
- Build the C++ binaries (`fuegod`, `walletd`) in `build/src` or have them on your PATH

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

## Features
- **Start/Stop Node** / **Start Wallet RPC** / **Create Wallet** / **Get Balance** / **Send Transaction**
- **Burn Menu** — Choose burn amount: 0.8 or 800 XFG, creates burn_deposit on chain

## Binary Detection
- TUI looks for binaries in `../build/src/` (development), falls back to `$PATH`
- Supported binaries: `fuegod`, `walletd`
