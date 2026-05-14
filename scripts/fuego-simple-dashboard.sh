#!/bin/bash

# Simple Fuego Interactive Dashboard
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
    echo -e "${BLUE}     Fuego Simple Dashboard${NC}"
    echo -e "${BLUE}================================${NC}"
    echo ""
}

# Check service status
check_status() {
    echo -e "${YELLOW}Checking Service Status...${NC}"

    # Check if we can connect to walletd
    if curl -s --connect-timeout 5 http://$WALLETD_HOST:$WALLETD_PORT/json_rpc >/dev/null 2>&1; then
        echo -e "Wallet Daemon: ${GREEN}Running${NC} (http://$WALLETD_HOST:$WALLETD_PORT)"
    else
        echo -e "Wallet Daemon: ${RED}Not accessible${NC} (http://$WALLETD_HOST:$WALLETD_PORT)"
    fi

    # Check if we can connect to daemon
    if curl -s --connect-timeout 5 http://$DAEMON_HOST:$DAEMON_PORT/getinfo >/dev/null 2>&1; then
        echo -e "Daemon: ${GREEN}Running${NC} (http://$DAEMON_HOST:$DAEMON_PORT)"
    else
        echo -e "Daemon: ${RED}Not accessible${NC} (http://$DAEMON_HOST:$DAEMON_PORT)"
    fi
    echo ""
}

# Get wallet status
get_wallet_status() {
    echo -e "${YELLOW}Wallet Status:${NC}"
    curl -s --connect-timeout 10 -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d '{"jsonrpc":"2.0","id":"0","method":"getStatus","params":{}}' \
        -H 'Content-Type: application/json'
    echo -e "\n"
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
    curl -s --connect-timeout 30 -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"createBurnDepositWithProof\",\"params\":{\"amount\":$amount,\"recipientAddress\":\"$recipient\"}}" \
        -H 'Content-Type: application/json'
    echo -e "\n"
}

# Test wallet connection
test_wallet_connection() {
    echo -e "${YELLOW}Testing Wallet Connection...${NC}"
    response=$(curl -s --connect-timeout 5 -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
        -d '{"jsonrpc":"2.0","id":"test","method":"getAddresses","params":{}}' \
        -H 'Content-Type: application/json')

    if [[ $response == *"result"* ]]; then
        echo -e "Connection Test: ${GREEN}SUCCESS${NC}"
        echo "Response: $response"
    else
        echo -e "Connection Test: ${RED}FAILED${NC}"
        echo "Response: $response"
    fi
    echo ""
}

# Main dashboard loop
main_dashboard() {
    while true; do
        clear_screen
        check_status

        echo "Main Menu:"
        echo "1. Test Wallet Connection"
        echo "2. Wallet Status"
        echo "3. Create Burn Deposit with Proof"
        echo "0. Exit"
        echo ""
        read -p "Select option: " choice

        case $choice in
            1)
                test_wallet_connection
                read -p "Press Enter to continue..."
                ;;
            2)
                get_wallet_status
                read -p "Press Enter to continue..."
                ;;
            3)
                create_burn_deposit_with_proof
                read -p "Press Enter to continue..."
                ;;
            0)
                echo -e "${GREEN}Exiting Fuego Dashboard...${NC}"
                exit 0
                ;;
            *)
                echo -e "${RED}Invalid option${NC}"
                sleep 2
                ;;
        esac
    done
}

# Run the dashboard
main_dashboard
