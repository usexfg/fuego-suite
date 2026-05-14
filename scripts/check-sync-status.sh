#!/bin/bash

# Fuego Synchronization Status Checker
# Checks synchronization status of both daemon and walletd

DAEMON_HOST="127.0.0.1"
DAEMON_PORT="28280"
WALLETD_HOST="127.0.0.1"
WALLETD_PORT="28283"

echo "Fuego Synchronization Status Checker"
echo "===================================="
echo ""

# Check daemon sync status
echo "Checking Daemon Status (http://$DAEMON_HOST:$DAEMON_PORT):"
if curl -s --connect-timeout 5 http://$DAEMON_HOST:$DAEMON_PORT/getinfo >/dev/null 2>&1; then
    daemon_info=$(curl -s --connect-timeout 5 http://$DAEMON_HOST:$DAEMON_PORT/getinfo)
    if [ -n "$daemon_info" ]; then
        echo "Daemon Response:"
        echo "$daemon_info" | jq '.' 2>/dev/null || echo "$daemon_info"
    else
        echo "Daemon is running but returned no info"
    fi
else
    echo "Cannot connect to daemon"
fi
echo ""

# Check walletd sync status
echo "Checking Wallet Daemon Status (http://$WALLETD_HOST:$WALLETD_PORT):"
wallet_status=$(curl -s --connect-timeout 10 -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
    -d '{"jsonrpc":"2.0","id":"sync_check","method":"getStatus","params":{}}' \
    -H 'Content-Type: application/json')

if [[ $wallet_status == *"result"* ]]; then
    echo "Wallet Daemon Response:"
    echo "$wallet_status" | jq '.' 2>/dev/null || echo "$wallet_status"

    # Extract sync info if available
    block_count=$(echo "$wallet_status" | jq -r '.result.blockCount // empty' 2>/dev/null)
    known_block_count=$(echo "$wallet_status" | jq -r '.result.knownBlockCount // empty' 2>/dev/null)

    if [ -n "$block_count" ] && [ -n "$known_block_count" ]; then
        echo ""
        echo "Sync Progress: $block_count / $known_block_count"
        if [ "$block_count" -ge "$known_block_count" ] 2>/dev/null; then
            echo "Status: ${GREEN}SYNCHRONIZED${NC}"
        else
            echo "Status: SYNCING"
        fi
    fi
else
    echo "Wallet Daemon Response:"
    echo "$wallet_status" | jq '.' 2>/dev/null || echo "$wallet_status"
fi
echo ""

# Check if walletd thinks daemon is synced
echo "Checking Walletd's View of Daemon Sync Status:"
sync_status=$(curl -s --connect-timeout 10 -X POST http://$WALLETD_HOST:$WALLETD_PORT/json_rpc \
    -d '{"jsonrpc":"2.0","id":"sync_status","method":"getStatus","params":{}}' \
    -H 'Content-Type: application/json')

if [[ $sync_status == *"result"* ]]; then
    peer_count=$(echo "$sync_status" | jq -r '.result.peerCount // 0' 2>/dev/null)
    echo "Connected Peers: $peer_count"

    if [ "$peer_count" -gt 0 ] 2>/dev/null; then
        echo "Network: CONNECTED"
    else
        echo "Network: DISCONNECTED"
    fi
else
    echo "Could not retrieve sync status"
fi
