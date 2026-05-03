# Fuego HEAT Claim Tools

Interactive TUI and CLI tools for claiming HEAT tokens using STARK + Merkle proof bundles.

## Quick Start

### 1. Build All Tools

```bash
./setup_provers.sh
```

This script will:
- Detect your OS (Linux/macOS/Windows)
- Install Rust if not present
- Build `xfg-stark-cli` (STARK prover)
- Build `fuego-prover-cli` (Merkle prover)
- Build `claim-tui` (Interactive TUI)

### 2. Claim HEAT

#### Option A: Interactive TUI (Recommended)

```bash
cd zk-fire
cargo run -p claim-tui --release
```

The TUI will guide you through:
1. Entering your Fuego transaction hash
2. Entering your burn secret
3. Entering the burn amount
4. Providing your Ethereum recipient address
5. Setting the RPC URL

#### Option B: CLI One-Liner

```bash
./claim.sh <txn_hash> <secret> <amount_atomic> <recipient_address> <rpc_url>
```

**Example:**
```bash
./claim.sh a1b2c3d4... 0xsecret... 8000000 0xRecipientETH... http://localhost:18180
```

### 3. Submit Bundle

The generated `bundle.json` can be submitted to the `HEATClaimer` contract on your target chain.

## Requirements

- **Rust**: 1.70+ (installed automatically by `setup_provers.sh`)
- **Python 3**: For the CLI bundler script
- **Fuego Daemon**: Running and accessible via RPC

## Keyboard Shortcuts (TUI)

| Key | Action |
|-----|--------|
| `Enter` | Proceed to next step / Confirm |
| `e` | Edit Transaction Hash |
| `s` | Edit Secret |
| `a` | Edit Amount |
| `r` | Edit Recipient |
| `c` | Edit RPC URL |
| `g` | Generate Bundle |
| `q` | Quit |
| `Esc` | Cancel editing |

## Output

After successful generation, you'll find:
- `bundle.json` - Contains STARK proof, Merkle proof, commitment, nullifier, and all data needed for claiming

## Troubleshooting

### "Prover directories not found"
Run the script from the project root where `zk-fire/` or `xfg-stark/` directories exist.

### "Rust not found" on Windows
Install Rust manually from https://rustup.rs/ then re-run the script.

### "Daemon not reachable"
Ensure `fuegod` is running and accessible at the specified RPC URL.
