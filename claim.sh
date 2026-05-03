#!/bin/bash

# Fuego HEAT Claim One-Liner
# Usage: ./claim.sh <txn_hash> <secret> <amount_atomic> <recipient_address> <rpc_url>

set -e

if [ "$#" -ne 5 ]; then
    echo "Usage: $0 <txn_hash> <secret> <amount_atomic> <recipient_address> <rpc_url>"
    echo ""
    echo "Alternative: Use the interactive TUI"
    echo "  cargo run -p fuego-tui --release"
    echo "  (Run from zk-fire/ directory)"
    exit 1
fi

TXN_HASH=$1
SECRET=$2
AMOUNT=$3
RECIPIENT=$4
RPC_URL=$5

echo "🚀 Starting HEAT Claim Process..."

# Determine workspace location
WORKSPACE_DIR="."
if [ -d "zk-fire" ]; then
    WORKSPACE_DIR="zk-fire"
fi

# 1. Generate Bundle using Python bundler
if [ -f "scripts/claim_bundler.py" ]; then
    python3 scripts/claim_bundler.py \
        --txn-hash "$TXN_HASH" \
        --secret "$SECRET" \
        --amount "$AMOUNT" \
        --recipient "$RECIPIENT" \
        --rpc "$RPC_URL" \
        --output bundle.json
else
    echo "❌ claim_bundler.py not found. Make sure you're in the project root."
    exit 1
fi

if [ $? -eq 0 ]; then
    echo "✅ Bundle created: bundle.json"
    echo "📦 You can now submit this bundle to the HEATClaimer contract."
else
    echo "❌ Failed to create claim bundle."
    exit 1
fi
