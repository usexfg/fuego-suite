#!/bin/bash

# XFG STARK CLI Build Script — v3 EFier relay model
# Builds xfg-stark-cli: the relay that bundles STARK proof + merkle proof
# + Elderfier signatures for direct L2 contract submission (no API needed).

set -e

# ── 1. Rust toolchain check ────────────────────────────────────────────────

if ! command -v cargo &>/dev/null; then
    echo "Rust/cargo not found. Installing via rustup..."
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
    # Source the env so cargo is available in this shell session
    # shellcheck source=/dev/null
    source "$HOME/.cargo/env"
fi

if ! command -v cargo &>/dev/null; then
    echo "ERROR: cargo still not found after rustup install."
    echo "Open a new terminal (or run: source ~/.cargo/env) then retry."
    exit 1
fi

echo "Rust toolchain: $(rustc --version)"

# ── 2. Directory check ─────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CRATE_DIR="$(dirname "$SCRIPT_DIR")"

if [ ! -f "$CRATE_DIR/Cargo.toml" ]; then
    echo "ERROR: Cargo.toml not found at $CRATE_DIR"
    exit 1
fi

cd "$CRATE_DIR"

# ── 3. Build ───────────────────────────────────────────────────────────────

echo "Building xfg-stark-cli (release)..."
cargo build --release --bin xfg-stark-cli

BIN="$CRATE_DIR/target/release/xfg-stark-cli"

if [ ! -f "$BIN" ]; then
    echo "ERROR: build failed — binary not found at $BIN"
    exit 1
fi

echo "Built: $BIN"
echo ""
"$BIN" --help

# ── 4. Next steps ─────────────────────────────────────────────────────────

echo ""
echo "─────────────────────────────────────────────────────────"
echo " xfg-stark-cli v3 relay — quick start"
echo "─────────────────────────────────────────────────────────"
echo ""
echo "Install globally:"
echo "  sudo cp $BIN /usr/local/bin/"
echo ""
echo "The CLI auto-connects to your local Fuego daemon"
echo "(localhost:18180 mainnet / localhost:28280 testnet)."
echo "Use --daemon HOST:PORT to override, --testnet for testnet."
echo ""
echo "Typical relay flow:"
echo ""
echo "  # 1. Check daemon + EFier consensus"
echo "  xfg-stark-cli daemon-status"
echo ""
echo "  # 2. Confirm your burn/deposit commitment exists on-chain"
echo "  xfg-stark-cli verify-commitment --commitment <COMMITMENT_HASH>"
echo ""
echo "  # 3. Fetch merkle proof + EFier signatures only (optional step)"
echo "  xfg-stark-cli fetch-proof --commitment <COMMITMENT_HASH>"
echo ""
echo "  # 4. Generate STARK proof + bundle everything into CompleteProofPackage"
echo "  xfg-stark-cli bundle \\"
echo "    --commitment <COMMITMENT_HASH> \\"
echo "    --recipient  <ETH_ADDRESS> \\"
echo "    --secret     <0xD5_DERIVED_SECRET_HEX> \\"
echo "    --output     proof_bundle.json"
echo ""
echo "  # 5. Submit proof_bundle.json directly to the L2 contract"
echo "  #    HEATBurnProofVerifier  (HEAT — ERC-20 gas token)"
echo "  #    COLDDepositProofVerifier (COLD — ERC-1155 CD interest token)"
echo "─────────────────────────────────────────────────────────"
