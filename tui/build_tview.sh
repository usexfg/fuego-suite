#!/bin/bash

# Compatibility build entry point for the supported Fuego TUI.
set -euo pipefail

echo "Building Fuego TUI..."

# Check if Go is installed
if ! command -v go &> /dev/null
then
    echo "Error: Go is not installed. Please install Go 1.20 or higher."
    exit 1
fi

# Get the directory of this script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# Change to the script directory
cd "$SCRIPT_DIR"

go build -mod=vendor -o fuego_suite_tview .

echo "Build successful! Binary created: fuego_suite_tview"
echo ""
echo "To run the TUI:"
echo "  ./fuego_suite_tview"
echo ""
echo "Make sure the Fuego node and wallet binaries are built in ../build/src/"
echo "or available in PATH."
