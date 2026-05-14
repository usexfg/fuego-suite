#!/bin/bash

# Fuego Wallet Daemon Stop Script

echo "Stopping Fuego Wallet Daemon..."

# Find and kill walletd processes
WALLETD_PIDS=$(pgrep -f "fuego-walletd")

if [ -z "$WALLETD_PIDS" ]; then
    echo "No walletd processes found."
else
    echo "Found walletd processes: $WALLETD_PIDS"
    kill $WALLETD_PIDS
    echo "Sent kill signal to walletd processes."

    # Wait a moment for graceful shutdown
    sleep 2

    # Check if processes are still running
    STILL_RUNNING=$(pgrep -f "fuego-walletd")
    if [ ! -z "$STILL_RUNNING" ]; then
        echo "Some processes still running, forcing kill..."
        kill -9 $STILL_RUNNING
    fi
fi

echo "Wallet Daemon stopped."
