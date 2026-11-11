#!/bin/bash

# Build script for Fuego TUI
# This script builds the Go-based Terminal User Interface

set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TUI_DIR="$SCRIPT_DIR/../tui"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if Go is installed
check_go() {
    if command -v go &> /dev/null; then
        GO_VERSION=$(go version | awk '{print $3}')
        print_status "Go is installed: $GO_VERSION"
        return 0
    else
        print_error "Go is not installed."
        print_error "Please install Go 1.20 or higher from https://go.dev/dl/"
        print_error "Or run: $SCRIPT_DIR/install-go-deps.sh"
        return 1
    fi
}

# Check Go version
check_go_version() {
    GO_VERSION_OUTPUT=$(go version)
    GO_VERSION=$(echo "$GO_VERSION_OUTPUT" | awk '{print $3}')

    # Extract version number (e.g., go1.21.5 -> 1.21.5)
    VERSION_NUMBER=$(echo "$GO_VERSION" | sed 's/go//')

    # Check if version is at least 1.20
    if python3 -c "
import sys
version = '$VERSION_NUMBER'.split('.')
major, minor = int(version[0]), int(version[1])
if major < 1 or (major == 1 and minor < 20):
    sys.exit(1)
else:
    sys.exit(0)
    " 2>/dev/null; then
        print_status "Go version is compatible: $GO_VERSION"
        return 0
    else
        print_error "Go version is too old: $GO_VERSION"
        print_error "Please update to Go 1.20 or higher."
        return 1
    fi
}

# Build the TUI
build_tui() {
    print_status "Building Fuego TUI..."

    # Create build directory if it doesn't exist
    mkdir -p "$TUI_DIR/build"

    # Change to the TUI directory
    cd "$TUI_DIR"

    # Download dependencies
    print_status "Downloading Go modules..."
    go mod tidy

    # Build the TUI
    print_status "Compiling TUI..."
    go build -o build/fuego-tui

    if [ -f "$TUI_DIR/build/fuego-tui" ]; then
        print_status "TUI built successfully!"
        print_status "Run with: $TUI_DIR/build/fuego-tui"
        return 0
    else
        print_error "Build failed. TUI executable not found."
        return 1
    fi
}

# Main function
main() {
    print_status "Starting Fuego TUI build process..."

    # Check if Go is installed
    if ! check_go; then
        exit 1
    fi

    # Check Go version
    if ! check_go_version; then
        exit 1
    fi

    # Build the TUI
    if build_tui; then
        print_status "Build completed successfully!"
    else
        print_error "Build failed!"
        exit 1
    fi
}

# Run main function
main "$@"
