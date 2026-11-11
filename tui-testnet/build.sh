#!/bin/bash

# Build script for Fuego Testnet TUI
set -e

echo "Building Fuego Testnet TUI..."
cd "$(dirname "$0")"

# Tidy dependencies
echo "Tidying dependencies..."
go mod tidy

# Build the binary
echo "Building binary..."
go build -o testnet-tui .

echo "Build successful!"
echo "Binary location: $(pwd)/testnet-tui"
echo ""
echo "To run:"
echo "  ./testnet-tui"
