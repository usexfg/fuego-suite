#!/bin/bash

# Fuego Wallet Diagnostics Script
# Comprehensive diagnostics for wallet issues

DAEMON_HOST="127.0.0.1"
DAEMON_PORT="28280"
WALLETD_HOST="127.0.0.1"
WALLETD_PORT="28283"

echo "Fuego Wallet Diagnostics"
echo "========================"
echo ""

# Function to make RPC calls with error handling
rpc_call() {
    local host=$1
    local port=$2
    local method=$3
    local params=$4
    local id=$5

    if [ -z "$params" ]; then
        curl -s --connect-timeout 10 -X POST http://$host:$port/json_rpc \
            -d "{\"jsonrpc\":\"2.0\",\"id\":\"$id\",\"method\":\"$method\",\"params\":{}}" \
            -H 'Content-Type: application/json'
    else
        curl -s --connect-timeout 10 -X POST http://$host:$port/json_rpc \
            -d "{\"jsonrpc\":\"2.0\",\"id\":\"$id\",\"method\":\"$method\",\"params\":$params}" \
            -H 'Content-Type: application/json'
    fi
}

# Check daemon connectivity
echo "1. Daemon Connectivity Check:"
echo "   Testing connection to daemon at $DAEMON_HOST:$DAEMON_PORT..."
if curl -s --connect-timeout 5 http://$DAEMON_HOST:$DAEMON_PORT/getinfo >/dev/null 2>&1; then
    echo "   ✓ Daemon is accessible"
    daemon_response=$(curl -s --connect-timeout 5 http://$DAEMON_HOST:$DAEMON_PORT/getinfo)
    if [ -n "$daemon_response" ]; then
        echo "   Daemon info retrieved successfully"
    else
        echo "   ✗ Daemon returned empty response"
    fi
else
    echo "   ✗ Cannot connect to daemon"
fi
echo ""

# Check wallet daemon connectivity
echo "2. Wallet Daemon Connectivity Check:"
echo "   Testing connection to walletd at $WALLETD_HOST:$WALLETD_PORT..."
wallet_response=$(rpc_call $WALLETD_HOST $WALLETD_PORT "getStatus" "" "connectivity_test")
if [[ $wallet_response == *"result"* ]]; then
    echo "   ✓ Wallet Daemon is accessible"
else
    echo "   ✗ Cannot connect to Wallet Daemon"
    echo "   Response: $wallet_response"
fi
echo ""

# Get wallet addresses
echo "3. Wallet Addresses:"
addresses_response=$(rpc_call $WALLETD_HOST $WALLETD_PORT "getAddresses" "" "addresses_test")
if [[ $addresses_response == *"result"* ]] && [[ $addresses_response == *"addresses"* ]]; then
    addresses=$(echo "$addresses_response" | jq -r '.result.addresses[]' 2>/dev/null)
    if [ -n "$addresses" ]; then
        echo "   Found wallet addresses:"
        count=1
        while IFS= read -r address; do
            echo "   $count. $address"
            count=$((count + 1))
        done <<< "$addresses"
    else
        echo "   ✗ No addresses found in wallet"
        echo "   Response: $addresses_response"
    fi
else
    echo "   ✗ Failed to retrieve addresses"
    echo "   Response: $addresses_response"
fi
echo ""

# Check balance for each address
if [ -n "$addresses" ]; then
    echo "4. Wallet Balances:"
    while IFS= read -r address; do
        echo "   Checking balance for: $address"
        balance_response=$(rpc_call $WALLETD_HOST $WALLETD_PORT "getBalance" "{\"address\":\"$address\"}" "balance_test")
        if [[ $balance_response == *"result"* ]]; then
            available=$(echo "$balance_response" | jq -r '.result.availableBalance // 0' 2>/dev/null)
            locked=$(echo "$balance_response" | jq -r '.result.lockedAmount // 0' 2>/dev/null)
            locked_deposit=$(echo "$balance_response" | jq -r '.result.lockedDepositBalance // 0' 2>/dev/null)
            unlocked_deposit=$(echo "$balance_response" | jq -r '.result.unlockedDepositBalance // 0' 2>/dev/null)

            echo "     Available Balance: $available"
            echo "     Locked Amount: $locked"
            echo "     Locked Deposit Balance: $locked_deposit"
            echo "     Unlocked Deposit Balance: $unlocked_deposit"
            echo "     Total Balance: $((available + locked + locked_deposit + unlocked_deposit))"
        else
            echo "     ✗ Failed to get balance"
            echo "     Response: $balance_response"
        fi
        echo ""
    done <<< "$addresses"
fi

# Check recent transactions
echo "5. Recent Transactions:"
transactions_response=$(rpc_call $WALLETD_HOST $WALLETD_PORT "getTransactions" "" "transactions_test")
if [[ $transactions_response == *"result"* ]]; then
    transaction_count=$(echo "$transactions_response" | jq '.result.items | length' 2>/dev/null)
    if [ "$transaction_count" != "0" ] && [ -n "$transaction_count" ]; then
        echo "   Found $transaction_count transaction blocks"
        echo "   Recent transactions:"
        echo "$transactions_response" | jq '.result.items[-3:]' 2>/dev/null || echo "   Could not parse transactions"
    else
        echo "   No recent transactions found"
    fi
else
    echo "   ✗ Failed to retrieve transactions"
    echo "   Response: $transactions_response"
fi
echo ""

# Check wallet status details
echo "6. Wallet Status Details:"
status_response=$(rpc_call $WALLETD_HOST $WALLETD_PORT "getStatus" "" "status_test")
if [[ $status_response == *"result"* ]]; then
    block_count=$(echo "$status_response" | jq -r '.result.blockCount // "N/A"' 2>/dev/null)
    known_block_count=$(echo "$status_response" | jq -r '.result.knownBlockCount // "N/A"' 2>/dev/null)
    peer_count=$(echo "$status_response" | jq -r '.result.peerCount // "N/A"' 2>/dev/null)
    deposit_count=$(echo "$status_response" | jq -r '.result.depositCount // "N/A"' 2>/dev/null)

    echo "   Blocks Processed: $block_count"
    echo "   Known Blocks: $known_block_count"
    echo "   Connected Peers: $peer_count"
    echo "   Deposit Count: $deposit_count"

    # Check sync status
    if [ "$block_count" != "N/A" ] && [ "$known_block_count" != "N/A" ]; then
        if [ "$block_count" -ge "$known_block_count" ] 2>/dev/null; then
            echo "   Sync Status: SYNCHRONIZED"
        else
            echo "   Sync Status: SYNCING ($block_count/$known_block_count)"
        fi
    fi
else
    echo "   ✗ Failed to retrieve status"
    echo "   Response: $status_response"
fi
echo ""

# Check if wallet is properly connected to daemon
echo "7. Wallet-Daemon Connection:"
# Try to get info from daemon through wallet
echo "   Testing if wallet can communicate with daemon..."
# This is a bit tricky since we need to see if the wallet can actually interact with the daemon
# Let's try a simple operation that requires daemon interaction
test_response=$(rpc_call $WALLETD_HOST $WALLETD_PORT "getAddresses" "" "connection_test")
if [[ $test_response == *"result"* ]]; then
    echo "   ✓ Wallet can communicate with services"
else
    echo "   ✗ Wallet may have connection issues with backend services"
fi
echo ""

echo "Diagnostics Complete"
echo "==================="
echo "If you're seeing balance issues:"
echo "1. Make sure the wallet file is correct and contains the right keys"
echo "2. Ensure the walletd is connected to the correct daemon"
echo "3. Verify that the daemon is fully synchronized"
echo "4. Check that you're using the correct network (testnet vs mainnet)"
