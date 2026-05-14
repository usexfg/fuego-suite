#!/bin/bash

# Simple Burn Deposit Script for Fuego Wallet Daemon
# Interactive script to create burn deposits with proof generation

WALLETD_HOST="127.0.0.1"
WALLETD_PORT="28283"

echo "Fuego Burn Deposit Creator"
echo "========================="
echo "Wallet Daemon: http://$WALLETD_HOST:$WALLETD_PORT"
echo ""

# Function to make RPC calls
make_burn_deposit() {
    local amount=$1
    local recipient=$2
    local source_address=$3

    if [ -z "$amount" ] || [ -z "$recipient" ]; then
        echo "Error: Amount and recipient address are required"
        return 1
    fi

    echo "Creating burn deposit..."
    echo "Amount: $amount atomic units ($(echo "scale=7; $amount/10000000" | bc) XFG)"
    echo "Recipient: $recipient"
    if [ ! -z "$source_address" ]; then
        echo "Source Address: $source_address"
    fi

    local params="{\"amount\":$amount,\"recipientAddress\":\"$recipient\"}"
    if [ ! -z "$source_address" ]; then
        params="{\"amount\":$amount,\"recipientAddress\":\"$recipient\",\"sourceAddress\":\"$source_address\"}"
    fi

    curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d "{\"jsonrpc\":\"2.0\",\"id\":\"burn_deposit\",\"method\":\"createBurnDepositWithProof\",\"params\":$params}" \
        -H 'Content-Type: application/json' | jq '.'
}

# Interactive input
echo "Enter burn deposit details:"
read -p "Amount in atomic units (8000000 for 0.8 XFG): " amount
read -p "Arbitrum recipient address: " recipient
read -p "Source wallet address (optional, press Enter to skip): " source_address

if [ -z "$amount" ]; then
    amount="8000000"  # Default to 0.8 XFG
fi

echo ""
make_burn_deposit $amount $recipient $source_address
