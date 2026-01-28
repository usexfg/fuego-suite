#!/bin/bash

# Fuego Interactive Dashboard
# Main control interface for Fuego daemon and wallet operations

DAEMON_HOST="127.0.0.1"
DAEMON_PORT="28280"
WALLETD_HOST="127.0.0.1"
WALLETD_PORT="28283"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Clear screen function
clear_screen() {
    clear
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}     Fuego Interactive Dashboard${NC}"
    echo -e "${BLUE}================================${NC}"
    echo ""
}

# Check service status
check_status() {
    echo -e "${YELLOW}Checking Service Status...${NC}"

    # Check daemon
    if curl -s http://$DAEMON_HOST:$DAEMON_PORT/getinfo >/dev/null; then
        echo -e "Daemon: ${GREEN}Running${NC} (http://$DAEMON_HOST:$DAEMON_PORT)"
    else
        echo -e "Daemon: ${RED}Not accessible${NC} (http://$DAEMON_HOST:$DAEMON_PORT)"
    fi

    # Check walletd
    if curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc -d '{"jsonrpc":"2.0","id":"0","method":"getStatus","params":{}}' -H 'Content-Type: application/json' >/dev/null; then
        echo -e "Wallet Daemon: ${GREEN}Running${NC} (http://$WALLETD_HOST:$WALLETD_PORT)"
    else
        echo -e "Wallet Daemon: ${RED}Not accessible${NC} (http://$WALLETD_HOST:$WALLETD_PORT)"
    fi
    echo ""
}

# Get wallet status
get_wallet_status() {
    echo -e "${YELLOW}Wallet Status:${NC}"
    curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d '{"jsonrpc":"2.0","id":"0","method":"getStatus","params":{}}' \
        -H 'Content-Type: application/json' | jq '.'
    echo ""
}

# Get wallet balance
get_wallet_balance() {
    echo -e "${YELLOW}Wallet Balance:${NC}"
    # Get first address
    local address=$(curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d '{"jsonrpc":"2.0","id":"0","method":"getAddresses","params":{}}' \
        -H 'Content-Type: application/json' | jq -r '.result.addresses[0]')

    if [ "$address" != "null" ]; then
        echo "Address: $address"
        curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
            -d "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"getBalance\",\"params\":{\"address\":\"$address\"}}" \
            -H 'Content-Type: application/json' | jq '.'
    else
        echo "No addresses found"
    fi
    echo ""
}

# List wallet addresses
list_addresses() {
    echo -e "${YELLOW}Wallet Addresses:${NC}"
    curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d '{"jsonrpc":"2.0","id":"0","method":"getAddresses","params":{}}' \
        -H 'Content-Type: application/json' | jq '.'
    echo ""
}

# Create standard deposit
create_standard_deposit() {
    echo -e "${YELLOW}Create Standard Deposit${NC}"
    read -p "Amount (in atomic units): " amount
    read -p "Term (in blocks): " term

    if [ -z "$amount" ] || [ -z "$term" ]; then
        echo -e "${RED}Amount and term are required${NC}"
        return 1
    fi

    echo "Creating deposit of $amount atomic units for $term blocks..."
    curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"createDeposit\",\"params\":{\"amount\":$amount,\"term\":$term}}" \
        -H 'Content-Type: application/json' | jq '.'
    echo ""
}

# Create burn deposit
create_burn_deposit() {
    echo -e "${YELLOW}Create Burn Deposit${NC}"
    echo "Standard burn amount is 8000000 atomic units (0.8 XFG)"
    read -p "Amount (press Enter for default 8000000): " amount

    if [ -z "$amount" ]; then
        amount="8000000"
    fi

    echo "Creating burn deposit of $amount atomic units..."
    curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"createBurnDeposit\",\"params\":{\"amount\":$amount}}" \
        -H 'Content-Type: application/json' | jq '.'
    echo ""
}

# Create burn deposit with proof
create_burn_deposit_with_proof() {
    echo -e "${YELLOW}Create Burn Deposit with Proof${NC}"
    echo "Standard burn amount is 8000000 atomic units (0.8 XFG)"
    read -p "Amount (press Enter for default 8000000): " amount
    read -p "Arbitrum recipient address: " recipient

    if [ -z "$amount" ]; then
        amount="8000000"
    fi

    if [ -z "$recipient" ]; then
        echo -e "${RED}Recipient address is required${NC}"
        return 1
    fi

    echo "Creating burn deposit with proof..."
    curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"createBurnDepositWithProof\",\"params\":{\"amount\":$amount,\"recipientAddress\":\"$recipient\"}}" \
        -H 'Content-Type: application/json' | jq '.'
    echo ""
}

# Get deposit information
get_deposit_info() {
    echo -e "${YELLOW}Get Deposit Information${NC}"
    read -p "Deposit ID: " deposit_id

    if [ -z "$deposit_id" ]; then
        echo -e "${RED}Deposit ID is required${NC}"
        return 1
    fi

    echo "Getting deposit information..."
    curl -s -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"getDeposit\",\"params\":{\"depositId\":$deposit_id}}" \
        -H 'Content-Type: application/json' | jq '.'
    echo ""
}

# Main dashboard loop
main_dashboard() {
    while true; do
        clear_screen
        check_status

        echo "Main Menu:"
        echo "1. Wallet Status"
        echo "2. Wallet Balance"
        echo "3. List Addresses"
        echo "4. Create Standard Deposit"
        echo "5. Create Burn Deposit"
        echo "6. Create Burn Deposit with Proof"
        echo "7. Get Deposit Information"
        echo "8. Refresh Status"
        echo "0. Exit"
        echo ""
        read -p "Select option: " choice

        case $choice in
            1)
                get_wallet_status
                read -p "Press Enter to continue..."
                ;;
            2)
                get_wallet_balance
                read -p "Press Enter to continue..."
                ;;
            3)
                list_addresses
                read -p "Press Enter to continue..."
                ;;
            4)
                create_standard_deposit
                read -p "Press Enter to continue..."
                ;;
            5)
                create_burn_deposit
                read -p "Press Enter to continue..."
                ;;
            6)
                create_burn_deposit_with_proof
                read -p "Press Enter to continue..."
                ;;
            7)
                get_deposit_info
                read -p "Press Enter to continue..."
                ;;
            8)
                # Just refresh
                ;;
            0)
                echo -e "${GREEN}Exiting Fuego Dashboard...${NC}"
                exit 0
                ;;
            *)
                echo -e "${RED}Invalid option${NC}"
                read -p "Press Enter to continue..."
                ;;
        esac
    done
}

# Run the dashboard
main_dashboard
