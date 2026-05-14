#!/bin/bash

# 🔥 Fuego CLI Utility Script
# Easy command-line interface for Fuego Docker services

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
NODE_RPC_URL="http://localhost:28180"
WALLET_RPC_URL="http://localhost:8070"

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check if service is running
check_service() {
    local service=$1
    local url=$2
    
    if curl -s "$url/getinfo" >/dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

# Function to make RPC call
rpc_call() {
    local url=$1
    local method=$2
    local params=${3:-"{}"}
    
    curl -s -X POST "$url/json_rpc" \
        -H "Content-Type: application/json" \
        -d "{
            \"jsonrpc\": \"2.0\",
            \"id\": \"test\",
            \"method\": \"$method\",
            \"params\": $params
        }" | jq -r '.result // .error // .'
}

# Function to make simple HTTP call
http_call() {
    local url=$1
    local endpoint=$2
    
    curl -s "$url/$endpoint" | jq -r '.' 2>/dev/null || curl -s "$url/$endpoint"
}

# Function to show node information
show_node_info() {
    print_status "Fetching node information..."
    
    if ! check_service "node" "$NODE_RPC_URL"; then
        print_error "Node service is not running or not accessible"
        return 1
    fi
    
    echo ""
    echo "=== Node Information ==="
    http_call "$NODE_RPC_URL" "getinfo"
    
    echo ""
    echo "=== Blockchain Info ==="
    http_call "$NODE_RPC_URL" "getblockcount"
    
    echo ""
    echo "=== Network Info ==="
    http_call "$NODE_RPC_URL" "getpeerlist"
}

# Function to show wallet information
show_wallet_info() {
    print_status "Fetching wallet information..."
    
    if ! check_service "wallet" "$WALLET_RPC_URL"; then
        print_error "Wallet service is not running or not accessible"
        return 1
    fi
    
    echo ""
    echo "=== Wallet Information ==="
    http_call "$WALLET_RPC_URL" "getinfo"
    
    echo ""
    echo "=== Balance ==="
    http_call "$WALLET_RPC_URL" "getbalance"
    
    echo ""
    echo "=== Addresses ==="
    http_call "$WALLET_RPC_URL" "getaddresses"
}

# Function to create wallet
create_wallet() {
    local wallet_name=${1:-"my_wallet"}
    local password=${2:-""}
    
    print_status "Creating wallet: $wallet_name"
    
    if ! check_service "wallet" "$WALLET_RPC_URL"; then
        print_error "Wallet service is not running"
        return 1
    fi
    
    local params="{\"filename\": \"$wallet_name\""
    if [[ -n "$password" ]]; then
        params="$params, \"password\": \"$password\""
    fi
    params="$params}"
    
    rpc_call "$WALLET_RPC_URL" "create_wallet" "$params"
}

# Function to open wallet
open_wallet() {
    local wallet_name=${1:-"my_wallet"}
    local password=${2:-""}
    
    print_status "Opening wallet: $wallet_name"
    
    if ! check_service "wallet" "$WALLET_RPC_URL"; then
        print_error "Wallet service is not running"
        return 1
    fi
    
    local params="{\"filename\": \"$wallet_name\""
    if [[ -n "$password" ]]; then
        params="$params, \"password\": \"$password\""
    fi
    params="$params}"
    
    rpc_call "$WALLET_RPC_URL" "open_wallet" "$params"
}

# Function to create address
create_address() {
    local label=${1:-""}
    
    print_status "Creating new address"
    
    if ! check_service "wallet" "$WALLET_RPC_URL"; then
        print_error "Wallet service is not running"
        return 1
    fi
    
    local params="{}"
    if [[ -n "$label" ]]; then
        params="{\"label\": \"$label\"}"
    fi
    
    rpc_call "$WALLET_RPC_URL" "create_address" "$params"
}

# Function to send transaction
send_transaction() {
    local address=$1
    local amount=$2
    local payment_id=${3:-""}
    
    if [[ -z "$address" || -z "$amount" ]]; then
        print_error "Usage: send <address> <amount> [payment_id]"
        return 1
    fi
    
    print_status "Sending $amount XFG to $address"
    
    if ! check_service "wallet" "$WALLET_RPC_URL"; then
        print_error "Wallet service is not running"
        return 1
    fi
    
    local params="{\"destinations\": [{\"amount\": $amount, \"address\": \"$address\"}]"
    if [[ -n "$payment_id" ]]; then
        params="$params, \"payment_id\": \"$payment_id\""
    fi
    params="$params}"
    
    rpc_call "$WALLET_RPC_URL" "transfer" "$params"
}

# Function to show transactions
show_transactions() {
    local account_index=${1:-0}
    local tx_type=${2:-"all"}
    
    print_status "Fetching transactions..."
    
    if ! check_service "wallet" "$WALLET_RPC_URL"; then
        print_error "Wallet service is not running"
        return 1
    fi
    
    local params="{\"account_index\": $account_index, \"tx_type\": \"$tx_type\"}"
    rpc_call "$WALLET_RPC_URL" "get_transfers" "$params"
}

# Function to show sync status
show_sync_status() {
    print_status "Checking sync status..."
    
    echo ""
    echo "=== Node Sync Status ==="
    if check_service "node" "$NODE_RPC_URL"; then
        local node_info=$(http_call "$NODE_RPC_URL" "getinfo")
        local height=$(echo "$node_info" | jq -r '.height // "N/A"')
        local target_height=$(echo "$node_info" | jq -r '.target_height // "N/A"')
        
        echo "Current Height: $height"
        echo "Target Height: $target_height"
        
        if [[ "$height" != "N/A" && "$target_height" != "N/A" ]]; then
            local sync_percentage=$(echo "scale=2; $height * 100 / $target_height" | bc -l 2>/dev/null || echo "N/A")
            echo "Sync Progress: ${sync_percentage}%"
        fi
    else
        print_error "Node service is not running"
    fi
    
    echo ""
    echo "=== Wallet Sync Status ==="
    if check_service "wallet" "$WALLET_RPC_URL"; then
        local wallet_info=$(http_call "$WALLET_RPC_URL" "getinfo")
        local wallet_height=$(echo "$wallet_info" | jq -r '.blockchain_height // "N/A"')
        local node_height=$(echo "$node_info" | jq -r '.height // "N/A"')
        
        echo "Wallet Height: $wallet_height"
        echo "Node Height: $node_height"
        
        if [[ "$wallet_height" != "N/A" && "$node_height" != "N/A" ]]; then
            local wallet_sync_percentage=$(echo "scale=2; $wallet_height * 100 / $node_height" | bc -l 2>/dev/null || echo "N/A")
            echo "Wallet Sync Progress: ${wallet_sync_percentage}%"
        fi
    else
        print_error "Wallet service is not running"
    fi
}

# Function to show network status
show_network_status() {
    print_status "Checking network status..."
    
    if ! check_service "node" "$NODE_RPC_URL"; then
        print_error "Node service is not running"
        return 1
    fi
    
    echo ""
    echo "=== Network Status ==="
    local node_info=$(http_call "$NODE_RPC_URL" "getinfo")
    local peers=$(echo "$node_info" | jq -r '.outgoing_connections_count // 0')
    local incoming=$(echo "$node_info" | jq -r '.incoming_connections_count // 0')
    local total_peers=$((peers + incoming))
    
    echo "Total Peers: $total_peers"
    echo "Outgoing Connections: $peers"
    echo "Incoming Connections: $incoming"
    
    echo ""
    echo "=== Peer List ==="
    http_call "$NODE_RPC_URL" "getpeerlist"
}

# Function to show usage
show_usage() {
    echo "🔥 Fuego CLI Utility"
    echo ""
    echo "Usage: $0 [COMMAND] [ARGS...]"
    echo ""
    echo "Commands:"
    echo "  info                    Show node information"
    echo "  wallet                  Show wallet information"
    echo "  sync                    Show sync status"
    echo "  network                 Show network status"
    echo "  create-wallet [name]    Create new wallet"
    echo "  open-wallet [name]      Open existing wallet"
    echo "  create-address [label]  Create new address"
    echo "  send <addr> <amount>    Send transaction"
    echo "  transactions [type]     Show transactions"
    echo "  help                    Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 info"
    echo "  $0 wallet"
    echo "  $0 sync"
    echo "  $0 create-wallet my_wallet"
    echo "  $0 send XFG123... 10.5"
    echo "  $0 transactions all"
}

# Function to check dependencies
check_dependencies() {
    if ! command -v curl >/dev/null 2>&1; then
        print_error "curl is required but not installed"
        exit 1
    fi
    
    if ! command -v jq >/dev/null 2>&1; then
        print_warning "jq is not installed. JSON output will be raw."
        print_warning "Install jq: sudo apt-get install jq"
    fi
}

# Main script logic
main() {
    local command=${1:-"help"}
    
    check_dependencies
    
    case $command in
        "info")
            show_node_info
            ;;
        "wallet")
            show_wallet_info
            ;;
        "sync")
            show_sync_status
            ;;
        "network")
            show_network_status
            ;;
        "create-wallet")
            create_wallet "$2" "$3"
            ;;
        "open-wallet")
            open_wallet "$2" "$3"
            ;;
        "create-address")
            create_address "$2"
            ;;
        "send")
            send_transaction "$2" "$3" "$4"
            ;;
        "transactions")
            show_transactions "$2" "$3"
            ;;
        "help"|"-h"|"--help")
            show_usage
            ;;
        *)
            print_error "Unknown command: $command"
            show_usage
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"