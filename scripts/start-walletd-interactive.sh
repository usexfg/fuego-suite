#!/bin/bash

# Fuego Interactive Wallet Daemon Startup Script
# This script makes it easier to launch walletd in interactive mode

# Get the directory where the script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Default values
WALLET_FILE=""
WALLET_PASSWORD=""
BIND_PORT="8070"
LOG_LEVEL="3"  # INFO level
DAEMON_NAME="walletd"

# Function to show usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Start Fuego Interactive Wallet Daemon"
    echo ""
    echo "Options:"
    echo "  -w, --wallet-file FILE     Wallet container file"
    echo "  -p, --password PASS        Wallet password"
    echo "  --port PORT                RPC bind port (default: 8070)"
    echo "  --log-level LEVEL          Log level 0-4 (default: 3)"
    echo "  -h, --help                 Show this help message"
    echo ""
    echo "Example:"
    echo "  $0 -w mywallet.wallet -p mypassword"
    echo ""
    echo "If no wallet file is specified, you'll be prompted to create one."
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -w|--wallet-file)
            WALLET_FILE="$2"
            shift 2
            ;;
        -p|--password)
            WALLET_PASSWORD="$2"
            shift 2
            ;;
        --port)
            BIND_PORT="$2"
            shift 2
            ;;
        --log-level)
            LOG_LEVEL="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Check if walletd exists
WALLETD_BIN="$PROJECT_ROOT/build/release/bin/$DAEMON_NAME"
if [ ! -f "$WALLETD_BIN" ]; then
    echo "Error: $DAEMON_NAME binary not found at $WALLETD_BIN"
    echo "Please build Fuego first:"
    echo "  cd $PROJECT_ROOT"
    echo "  mkdir -p build && cd build"
    echo "  cmake .."
    echo "  make -j$(nproc)"
    exit 1
fi

# If no wallet file specified, prompt user
if [ -z "$WALLET_FILE" ]; then
    echo "No wallet file specified."
    read -p "Enter path to wallet file (or press Enter to create a new one): " WALLET_FILE

    if [ -z "$WALLET_FILE" ]; then
        read -p "Enter name for new wallet file: " WALLET_FILE
        if [ -z "$WALLET_FILE" ]; then
            WALLET_FILE="fuego-wallet.wallet"
        fi

        # Generate new container
        echo "Generating new wallet container..."
        read -s -p "Enter password for new wallet: " WALLET_PASSWORD
        echo
        read -s -p "Confirm password: " WALLET_PASSWORD_CONFIRM
        echo

        if [ "$WALLET_PASSWORD" != "$WALLET_PASSWORD_CONFIRM" ]; then
            echo "Passwords do not match!"
            exit 1
        fi

        "$WALLETD_BIN" --generate-container --container-file="$WALLET_FILE" --container-password="$WALLET_PASSWORD"
    fi
fi

# Check if wallet file exists
if [ ! -f "$WALLET_FILE" ]; then
    echo "Error: Wallet file '$WALLET_FILE' not found!"
    exit 1
fi

# If no password specified, prompt for it
if [ -z "$WALLET_PASSWORD" ]; then
    read -s -p "Enter wallet password: " WALLET_PASSWORD
    echo
fi

# Start the interactive wallet daemon
echo "Starting Fuego Interactive Wallet Daemon..."
echo "Wallet file: $WALLET_FILE"
echo "RPC Port: $BIND_PORT"
echo "Log Level: $LOG_LEVEL"
echo ""

"$WALLETD_BIN" \
    --container-file="$WALLET_FILE" \
    --container-password="$WALLET_PASSWORD" \
    --bind-port="$BIND_PORT" \
    --log-level="$LOG_LEVEL" \
    --interactive

echo "Fuego Interactive Wallet Daemon stopped."
