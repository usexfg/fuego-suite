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
go build -o fuego-tui main.go config.go

# Build the tview version
echo "Building tview version..."
go build -o fuego-tui-tview tview_main.go config.go

echo "Build successful!"
echo "Bubble Tea binary: $(pwd)/fuego-tui"
echo "tview binary: $(pwd)/fuego-tui-tview"
echo ""
echo "To run Bubble Tea version:"
echo "  ./fuego-tui"
echo ""
echo "To run tview version:"
echo "  ./fuego-tui-tview"
