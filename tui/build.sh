#!/bin/bash

# Build script for Fuego TUI
set -euo pipefail

echo "Building Fuego TUI..."
cd "$(dirname "$0")"

echo "Building Bubble Tea version..."
go build -mod=vendor -o fuego_suite .

echo "Build successful!"
echo "TUI binary: $(pwd)/fuego_suite"
echo ""
echo "To run Mainnet version:"
echo "  ./fuego_suite"
echo ""
echo "To run Testnet version:"
echo "  ./fuego_suite --testnet"
