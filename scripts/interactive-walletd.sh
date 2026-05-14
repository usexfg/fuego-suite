#!/bin/bash

# Interactive Fuego Wallet Daemon Script
# Provides an interactive interface to walletd RPC methods

WALLETD_HOST="127.0.0.1"
WALLETD_PORT="28283"
DAEMON_HOST="127.0.0.1"
DAEMON_PORT="28280"

echo "Fuego Interactive Wallet Daemon"
echo "=============================="
echo "Walletd RPC: http://$WALLETD_HOST:$WALLETD_PORT"
echo "Daemon: http://$DAEMON_HOST:$DAEMON_PORT"
echo ""

# Function to make RPC calls
rpc_call() {
    local method=$1
    local params=$2
    local id=${3:-"interactive"}

    if [ -z "$params" ]; then
        curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
            -d "{\"jsonrpc\":\"2.0\",\"id\":\"$id\",\"method\":\"$method\",\"params\":{}}" \
            -H 'Content-Type: application/json' | jq '.'
    else
        curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
            -d "{\"jsonrpc\":\"2.0\",\"id\":\"$id\",\"method\":\"$method\",\"params\":$params}" \
            -H 'Content-Type: application/json' | jq '.'
    fi
}

# Function to check wallet status
check_status() {
    echo "Checking wallet status..."
    rpc_call "getStatus"
}

# Function to get balance
get_balance() {
    echo "Getting wallet balance..."
    local address=$1
    if [ -z "$address" ]; then
        # Get first address
        local first_address=$(rpc_call "getAddresses" | jq -r '.result.addresses[0]')
        address=$first_address
    fi
    rpc_call "getBalance" "{\"address\":\"$address\"}"
}

# Function to create standard deposit
create_deposit() {
    local amount=$1
    local term=$2
    local address=$3

    if [ -z "$amount" ] || [ -z "$term" ]; then
        echo "Usage: create_deposit <amount> <term> [address]"
        return 1
    fi

    local params="{\"amount\":$amount,\"term\":$term}"
    if [ ! -z "$address" ]; then
        params="{\"amount\":$amount,\"term\":$term,\"sourceAddress\":\"$address\"}"
    fi

    echo "Creating deposit..."
    rpc_call "createDeposit" "$params"
}

# Function to create burn deposit
create_burn_deposit() {
    local amount=${1:-8000000}  # Default to 0.8 XFG
    local address=$2

    local params="{\"amount\":$amount}"
    if [ ! -z "$address" ]; then
        params="{\"amount\":$amount,\"sourceAddress\":\"$address\"}"
    fi

    echo "Creating burn deposit..."
    rpc_call "createBurnDeposit" "$params"
}

# Function to create burn deposit with proof
create_burn_deposit_with_proof() {
    local amount=${1:-8000000}  # Default to 0.8 XFG
    local recipient=$2
    local address=$3

    if [ -z "$recipient" ]; then
        echo "Usage: create_burn_deposit_with_proof <amount> <recipient_address> [source_address]"
        return 1
    fi

    local params="{\"amount\":$amount,\"recipientAddress\":\"$recipient\"}"
    if [ ! -z "$address" ]; then
        params="{\"amount\":$amount,\"recipientAddress\":\"$recipient\",\"sourceAddress\":\"$address\"}"
    fi

    echo "Creating burn deposit with proof..."
    rpc_call "createBurnDepositWithProof" "$params"
}

# Function to get deposit info
get_deposit() {
    local deposit_id=$1

    if [ -z "$deposit_id" ]; then
        echo "Usage: get_deposit <deposit_id>"
        return 1
    fi

    echo "Getting deposit info..."
    rpc_call "getDeposit" "{\"depositId\":$deposit_id}"
}

# Function to withdraw deposit
withdraw_deposit() {
    local deposit_id=$1

    if [ -z "$deposit_id" ]; then
        echo "Usage: withdraw_deposit <deposit_id>"
        return 1
    fi

    echo "Withdrawing deposit..."
    rpc_call "withdrawDeposit" "{\"depositId\":$deposit_id}"
}

# Function to list addresses
list_addresses() {
    echo "Listing wallet addresses..."
    rpc_call "getAddresses"
}

# Main interactive loop
while true; do
    echo ""
    echo "Available commands:"
    echo "  status                    - Check wallet status"
    echo "  balance [address]         - Get wallet balance"
    echo "  addresses                 - List wallet addresses"
    echo "  deposit <amount> <term> [address] - Create standard deposit"
    echo "  burn <amount> [address]   - Create burn deposit"
    echo "  burnproof <amount> <recipient> [address] - Create burn deposit with proof"
    echo "  getdep <id>               - Get deposit information"
    echo "  withdraw <id>             - Withdraw deposit"
    echo "  help                      - Show this help"
    echo "  quit                      - Exit"
    echo ""
    read -p "fuego-walletd> " command args

    case $command in
        "status")
            check_status
            ;;
        "balance")
            get_balance $args
            ;;
        "addresses")
            list_addresses
            ;;
        "deposit")
            set -- $args
            create_deposit $1 $2 $3
            ;;
        "burn")
            set -- $args
            create_burn_deposit $1 $2
            ;;
        "burnproof")
            set -- $args
            create_burn_deposit_with_proof $1 $2 $3
            ;;
        "getdep")
            get_deposit $args
            ;;
        "withdraw")
            withdraw_deposit $args
            ;;
        "help")
            echo "Interactive Fuego Wallet Daemon"
            echo "Commands:"
            echo "  status                    - Check wallet synchronization status"
            echo "  balance [address]         - Get balance for address (or first address)"
            echo "  addresses                 - List all wallet addresses"
            echo "  deposit <amount> <term> [address] - Create standard time-locked deposit"
            echo "  burn <amount> [address]   - Create burn deposit (default 0.8 XFG)"
            echo "  burnproof <amount> <recipient> [address] - Create burn deposit with cross-chain proof"
            echo "  getdep <id>               - Get information about specific deposit"
            echo "  withdraw <id>             - Withdraw mature deposit"
            echo "  help                      - Show this help message"
            echo "  quit                      - Exit the interactive shell"
            ;;
        "quit")
            echo "Exiting Fuego Interactive Wallet Daemon..."
            break
            ;;
        "")
            # Empty command, just show prompt again
            ;;
        *)
            echo "Unknown command: $command"
            echo "Type 'help' for available commands"
            ;;
    esac
done
