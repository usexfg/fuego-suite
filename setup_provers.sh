#!/bin/bash
set -e

echo "🔥 Fuego ZK Prover Setup"
echo "=========================================="

# 1. Detect OS
detect_os() {
    case "$(uname -s)" in
        Linux*)     echo "linux";;
        Darwin*)    echo "macos";;
        CYGWIN*)    echo "windows";;
        MINGW*)     echo "windows";;
        *)          echo "unknown";;
    esac
}

OS=$(detect_os)
echo "📌 Detected OS: $OS"

# 2. Install Rust/Cargo if not present
if ! command -v cargo &> /dev/null; then
    echo "📦 Rust not found. Installing via rustup..."
    if [ "$OS" = "windows" ]; then
        echo "⚠️  Windows detected. Please install Rust manually from https://rustup.rs/"
        echo "    Then re-run this script."
        exit 1
    else
        curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
        source "$HOME/.cargo/env"
    fi
else
    echo "✅ Rust is already installed ($(cargo --version))."
fi

# 3. Ensure we are in the workspace root
# Check for zk-fire workspace (new structure)
if [ -d "zk-fire" ]; then
    echo "📂 Found zk-fire workspace."
    cd zk-fire
elif [ ! -d "xfg-stark" ] && [ ! -d "fuego-prover" ]; then
    echo "❌ Error: Prover directories not found."
    echo "    Please run this script from the project root (where zk-fire/ or xfg-stark/ exists)."
    exit 1
fi

# 4. Build STARK Prover
echo ""
echo "⚡ Building xfg-stark-cli..."
if [ -d "xfg-stark" ]; then
    cargo build -p xfg-stark-cli --release || {
        echo "⚠️  xfg-stark-cli not found as a package, trying xfg-stark..."
        cargo build --release
    }
else
    echo "⚠️  xfg-stark directory not found, skipping..."
fi

# 5. Build Fuego Prover
echo ""
echo "⚡ Building fuego-prover-cli..."
if [ -d "fuego-prover" ]; then
    cargo build -p fuego-prover-cli --release
else
    echo "⚠️  fuego-prover directory not found, skipping..."
fi

# 6. Build TUI
echo ""
echo "⚡ Building claim-tui (Interactive CLI)..."
if [ -d "fuego-prover/claim-tui" ]; then
    cargo build -p claim-tui --release
    echo "✅ TUI built successfully!"
    echo "   Run with: cargo run -p claim-tui --release"
else
    echo "⚠️  claim-tui directory not found, skipping..."
fi

# 7. Summary
echo ""
echo "=========================================="
echo "✅ All provers built successfully!"
echo "=========================================="
echo ""
echo "📦 Binaries location: target/release/"
echo ""
echo "🚀 Usage:"
echo "   Interactive TUI:  cargo run -p claim-tui --release"
echo "   CLI (one-liner):  ./claim.sh <txn_hash> <secret> <amount> <recipient> <rpc>"
echo ""
echo "   If in root directory (not zk-fire/):"
echo "   Interactive TUI:  cargo run -p claim-tui --manifest-path zk-fire/fuego-prover/claim-tui/Cargo.toml --release"
echo "   CLI (one-liner):  ./claim.sh ..."
echo "=========================================="
