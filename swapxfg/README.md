# swapxfg — Fuego Cross-Chain Swap Terminal

Unified TUI for atomic swaps using adaptor signatures on the XFG side.

## Features

- **Multi-chain support**: SOL, ETH, XMR, BCH
- **Adaptor signatures**: Privacy-preserving atomic swaps
- **CD market**: Confidential Deposits trading
- **Orderbook & charts**: Real-time market data
- **Browser bridges**: MetaMask (ETH), Phantom (SOL)

## Usage

```bash
# Connect to local node
./swapxfg --daemon http://127.0.0.1:18180

# Testnet
./swapxfg --testnet

# With wallet (enables swap signing)
./swapxfg -d http://127.0.0.1:18180 -w http://127.0.0.1:18282

# With ETH bridge (MetaMask)
./swapxfg --bridge-port 8545
```

## Commands

| Command | Description |
|---------|-------------|
| `pair <name>` | Switch pair (sol, eth, xmr, bch, cd) |
| `c` | Jump to CD market |
| `connect metamask` | Open ETH bridge |
| `connect phantom` | Open SOL bridge |
| `connect bch` | Connect Electron Cash |
| `swap initiate <amt> <peer_key> <pair>` | Start atomic swap |
| `initiate <alias> <amt> <pair>` | Initiate via alias |
| `help` | Show all commands |

## Architecture

```
swapxfg/
├── main.go       # CLI entry
└── app/
    ├── app.go    # Main run loop
    ├── tui.go    # Bubble Tea UI
    ├── rpc.go    # Fuego RPC client
    ├── wallet.go # Wallet RPC client
    ├── bridge.go # MetaMask/Phantom bridge
    ├── pairs.go  # Pair definitions
    └── cd_*.go   # CD market components
```

## Protocol

Swaps use adaptor signatures on the XFG side:
- **Musig2** for 2-of-2 escrow
- **DLEQ proofs** for adaptor point verification
- **HTLC** on counterparty chains (ETH Solidity, BCH script, SOL program)

## Build

```bash
cd swapxfg
go build -o swapxfg .
```
