#!/bin/bash

# Install Go dependencies for Fuego TUI
# This script checks if Go is installed and installs it if necessary

set -e

GO_VERSION="1.21.5"
GO_INSTALL_DIR="/usr/local"

# Function to detect OS
detect_os() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo "linux"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "darwin"
    elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
        echo "windows"
    else
        echo "unknown"
    fi
}

# Function to install Go on Linux
install_go_linux() {
    echo "Installing Go $GO_VERSION on Linux..."

    if [ "$(id -u)" -ne 0 ]; then
        echo "This script requires root privileges to install Go."
        echo "Please run with sudo:"
        echo "sudo $0"
        exit 1
    fi

    ARCH=$(uname -m)
    case $ARCH in
        x86_64) GO_ARCH="amd64" ;;
        aarch64) GO_ARCH="arm64" ;;
        armv7l) GO_ARCH="armv6l" ;;
        *) echo "Unsupported architecture: $ARCH"; exit 1 ;;
    esac

    wget -q https://go.dev/dl/go${GO_VERSION}.linux-${GO_ARCH}.tar.gz
    rm -rf $GO_INSTALL_DIR/go
    tar -C $GO_INSTALL_DIR -xzf go${GO_VERSION}.linux-${GO_ARCH}.tar.gz
    rm go${GO_VERSION}.linux-${GO_ARCH}.tar.gz

    # Add Go to PATH permanently
    echo 'export PATH=$PATH:/usr/local/go/bin' >> /etc/profile
    echo 'export GOPATH=$HOME/go' >> /etc/profile
    echo 'export PATH=$PATH:$GOPATH/bin' >> /etc/profile

    echo "Go installed successfully. Please run 'source /etc/profile' or open a new terminal."
}

# Function to install Go on macOS
install_go_macos() {
    echo "Installing Go $GO_VERSION on macOS..."

    if command -v brew &> /dev/null; then
        echo "Installing Go via Homebrew..."
        brew install go
    else
        echo "Homebrew not found. Please install Homebrew first:"
        echo "/bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        echo "Then run this script again."
        exit 1
    fi
}

# Function to install Go on Windows
install_go_windows() {
    echo "Please download and install Go manually from https://go.dev/dl/"
    echo "Choose the Windows installer for Go $GO_VERSION"
    echo "After installation, restart your terminal or command prompt."
}

# Function to check if Go is installed
check_go() {
    if command -v go &> /dev/null; then
        echo "Go is installed: $(go version)"
        return 0
    else
        echo "Go is not installed."
        return 1
    fi
}

# Function to install TUI dependencies
install_tui_deps() {
    echo "Installing TUI dependencies..."

    # Change to the TUI directory
    cd "$(dirname "$0")/../tui"

    # Install Go dependencies
    go mod tidy

    echo "TUI dependencies installed successfully."

    # Build the TUI
    go build -o build/fuego-tui

    echo "TUI built successfully. Run with: ./build/fuego-tui"
}

# Main script
main() {
    if ! check_go; then
        OS=$(detect_os)
        case $OS in
            linux) install_go_linux ;;
            darwin) install_go_macos ;;
            windows) install_go_windows ;;
            *) echo "Unsupported OS: $OSTYPE"; exit 1 ;;
        esac

        # Check if Go was installed successfully
        if ! check_go; then
            echo "Failed to install Go. Please install manually from https://go.dev/dl/"
            exit 1
        fi
    fi

    install_tui_deps
}

main "$@"
