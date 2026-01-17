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

# Build the tview version
echo "Building tview version..."
go build -o fuego_suite_tview tview_main.go config.go

echo "Build successful!"
echo "Bubble Tea binary: $(pwd)/fuego_suite"
echo "tview binary: $(pwd)/fuego_suite_tview"
echo ""
echo "To run Bubble Tea version:"
echo "  ./fuego_suite"
echo ""
echo "To run tview version:"
echo "  ./fuego_suite_tview"
