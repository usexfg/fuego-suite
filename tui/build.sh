#!/bin/bash

# Build script for Fuego TUI
set -e

echo "Building Fuego TUI..."
cd "$(dirname "$0")"

# Tidy dependencies
echo "Tidying dependencies..."
go mod tidy

# Build the Bubble Tea version
echo "Building Bubble Tea version..."
go build -o fuego_suite main.go config.go

echo "Build successful!"
echo "TUI binary: $(pwd)/fuego_suite"
echo ""
echo "To run Mainnet version:"
echo "  ./fuego_suite"
echo ""
echo "To run Testnet version:"
echo "  ./fuego_suite --testnet"
