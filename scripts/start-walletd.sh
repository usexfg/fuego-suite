#!/bin/bash

# Fuego Wallet Daemon Startup Script

# Configuration
WALLET_FILE="gmoney"
WALLET_PASSWORD="gg"
DAEMON_ADDRESS="127.0.0.1:28280"
BIND_PORT="28283"
LOG_LEVEL="3"
TESTNET="--testnet"

# Create logs directory
mkdir -p /home/ar/fuego/logs

echo "Starting Fuego Wallet Daemon..."
echo "Wallet File: $WALLET_FILE"
echo "Daemon Address: $DAEMON_ADDRESS"
echo "Bind Port: $BIND_PORT"
echo "Testnet: $TESTNET"
echo ""

# Start walletd with logging
/home/ar/fuego/build/release/src/walletd/fuego-walletd \
    $TESTNET \
    -w="$WALLET_FILE" \
    -p="$WALLET_PASSWORD" \
    --daemon-address=$DAEMON_ADDRESS \
    --bind-port=$BIND_PORT \
    -d \
    --log-level=$LOG_LEVEL \
    > /home/ar/fuego/logs/walletd.log 2>&1 &

WALLETD_PID=$!

echo "Wallet Daemon started with PID: $WALLETD_PID"
echo "Logs: /home/ar/fuego/logs/walletd.log"
echo "RPC Interface: http://127.0.0.1:$BIND_PORT/json_rpc"
echo ""
echo "To stop walletd: kill $WALLETD_PID"
echo "To view logs: tail -f /home/ar/fuego/logs/walletd.log"
