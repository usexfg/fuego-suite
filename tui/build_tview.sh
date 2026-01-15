#!/bin/bash

# Build script for tview-based Fuego TUI

echo "Building tview-based Fuego TUI..."

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

# Clean any previous builds
echo "Cleaning previous builds..."
rm -f fuego-tui-tview

# Update dependencies
echo "Updating dependencies..."
go mod tidy

# Build the tview-based TUI
echo "Building tview-based TUI..."
go build -o fuego-tui-tview tview_main.go config.go

if [ $? -eq 0 ]; then
    echo "Build successful! Binary created: fuego-tui-tview"
    echo ""
    echo "To run the tview-based TUI:"
    echo "  ./fuego-tui-tview"
    echo ""
    echo "Note: Make sure the Fuego node and wallet binaries are built in ../build/src/"
    echo "      or available in your PATH"
else
    echo "Build failed!"
    exit 1
fi
