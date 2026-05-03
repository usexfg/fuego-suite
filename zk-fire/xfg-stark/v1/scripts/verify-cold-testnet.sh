#!/bin/bash
# COLD Deposits Testnet Verification Script
# Verifies all contracts on block explorers

set -e

echo "🔍 COLD Deposits Contract Verification"
echo "======================================="
echo ""

# Check environment variables
if [ -z "$ETHERSCAN_API_KEY" ]; then
    echo "❌ Error: ETHERSCAN_API_KEY not set"
    exit 1
fi

if [ -z "$ARBISCAN_API_KEY" ]; then
    echo "❌ Error: ARBISCAN_API_KEY not set"
    exit 1
fi

# Read deployment file (use latest if not specified)
if [ -z "$1" ]; then
    DEPLOYMENT_FILE=$(ls -t deployments/testnet-*.json | head -1)
    echo "📂 Using latest deployment: $DEPLOYMENT_FILE"
else
    DEPLOYMENT_FILE=$1
    echo "📂 Using deployment: $DEPLOYMENT_FILE"
fi

if [ ! -f "$DEPLOYMENT_FILE" ]; then
    echo "❌ Error: Deployment file not found"
    exit 1
fi

# Extract addresses from deployment file
CD_TOKEN=$(jq -r '.contracts.ethereum_sepolia.FuegoCOLDAOToken' $DEPLOYMENT_FILE)
GOVERNOR=$(jq -r '.contracts.ethereum_sepolia.COLDAOGovernor' $DEPLOYMENT_FILE)
COLD_VERIFIER=$(jq -r '.contracts.arbitrum_sepolia.COLDDepositProofVerifier' $DEPLOYMENT_FILE)
DEPLOYER=$(jq -r '.deployer' $DEPLOYMENT_FILE)

echo ""
echo "📋 Verifying contracts..."
echo ""

# Verify FuegoCOLDAOToken on Ethereum Sepolia
echo "1️⃣  Verifying FuegoCOLDAOToken on Ethereum Sepolia..."
forge verify-contract \
    $CD_TOKEN \
    FuegoCOLDAOToken \
    --constructor-args $(cast abi-encode "constructor(address,address,address)" \
        "0x0000000000000000000000000000000000000001" \
        "0x0000000000000000000000000000000000000002" \
        "$DEPLOYER") \
    --chain sepolia \
    --etherscan-api-key $ETHERSCAN_API_KEY \
    --watch

echo "✅ FuegoCOLDAOToken verified"
echo ""

sleep 3

# Verify COLDAOGovernor on Ethereum Sepolia
echo "2️⃣  Verifying COLDAOGovernor on Ethereum Sepolia..."
forge verify-contract \
    $GOVERNOR \
    COLDAOGovernor \
    --constructor-args $(cast abi-encode "constructor(address,uint256,address)" \
        "$CD_TOKEN" \
        800 \
        "$DEPLOYER") \
    --chain sepolia \
    --etherscan-api-key $ETHERSCAN_API_KEY \
    --watch

echo "✅ COLDAOGovernor verified"
echo ""

sleep 3

# Verify COLDDepositProofVerifier on Arbitrum Sepolia
echo "3️⃣  Verifying COLDDepositProofVerifier on Arbitrum Sepolia..."
forge verify-contract \
    $COLD_VERIFIER \
    COLDDepositProofVerifier \
    --constructor-args $(cast abi-encode "constructor(address,address,address)" \
        "$CD_TOKEN" \
        "$DEPLOYER" \
        "$DEPLOYER") \
    --chain arbitrum-sepolia \
    --etherscan-api-key $ARBISCAN_API_KEY \
    --watch

echo "✅ COLDDepositProofVerifier verified"
echo ""

echo "🎉 All contracts verified!"
echo ""
echo "🔗 View on explorers:"
echo "   https://sepolia.etherscan.io/address/$CD_TOKEN#code"
echo "   https://sepolia.etherscan.io/address/$GOVERNOR#code"
echo "   https://sepolia.arbiscan.io/address/$COLD_VERIFIER#code"
echo ""
