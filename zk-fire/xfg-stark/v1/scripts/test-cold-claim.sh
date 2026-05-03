#!/bin/bash
# COLD Deposits Testnet Claim Test Script
# Tests CD claim flow for different tiers

set -e

echo "🧪 COLD Deposits Claim Test"
echo "============================"
echo ""

# Check environment variables
if [ -z "$PRIVATE_KEY" ]; then
    echo "❌ Error: PRIVATE_KEY not set"
    exit 1
fi

if [ -z "$SEPOLIA_RPC" ]; then
    echo "❌ Error: SEPOLIA_RPC not set"
    exit 1
fi

if [ -z "$ARB_SEPOLIA_RPC" ]; then
    echo "❌ Error: ARB_SEPOLIA_RPC not set"
    exit 1
fi

# Read deployment file
if [ -z "$1" ]; then
    DEPLOYMENT_FILE=$(ls -t deployments/testnet-*.json | head -1)
    echo "📂 Using latest deployment: $DEPLOYMENT_FILE"
else
    DEPLOYMENT_FILE=$1
fi

if [ ! -f "$DEPLOYMENT_FILE" ]; then
    echo "❌ Error: Deployment file not found"
    exit 1
fi

# Extract addresses
CD_TOKEN=$(jq -r '.contracts.ethereum_sepolia.FuegoCOLDAOToken' $DEPLOYMENT_FILE)
COLD_VERIFIER=$(jq -r '.contracts.arbitrum_sepolia.COLDDepositProofVerifier' $DEPLOYMENT_FILE)
RECIPIENT=$(cast wallet address --private-key $PRIVATE_KEY)

echo "🔑 Recipient: $RECIPIENT"
echo "📋 CD Token: $CD_TOKEN"
echo "📋 COLD Verifier: $COLD_VERIFIER"
echo ""

# Prompt user for tier to test
echo "Select tier to test:"
echo "  0 - 0.8 XFG × 3mo @ 8%   (640,000 atomic units)"
echo "  1 - 0.8 XFG × 12mo @ 21% (1,680,000 atomic units)"
echo "  2 - 800 XFG × 3mo @ 33%  (2,640,000,000 atomic units)"
echo "  3 - 800 XFG × 12mo @ 69% (5,520,000,000 atomic units)"
echo ""
read -p "Enter tier (0-3): " TIER

# Validate tier
if ! [[ "$TIER" =~ ^[0-3]$ ]]; then
    echo "❌ Error: Invalid tier. Must be 0-3."
    exit 1
fi

# Prompt for legacy flag
echo ""
read -p "Is this a legacy deposit (before 2026-01-01)? (y/n): " LEGACY_INPUT
if [[ "$LEGACY_INPUT" == "y" || "$LEGACY_INPUT" == "Y" ]]; then
    DEPOSIT_TIMESTAMP=1735000000  # Before 2026-01-01
    LEGACY_FLAG=true
    echo "✅ Using legacy timestamp: $DEPOSIT_TIMESTAMP (pre-2026)"
else
    DEPOSIT_TIMESTAMP=$(date +%s)  # Current timestamp
    LEGACY_FLAG=false
    echo "✅ Using current timestamp: $DEPOSIT_TIMESTAMP (post-2026)"
fi

echo ""

# Generate random nullifier and commitment for testing
NULLIFIER=$(cast keccak "$(openssl rand -hex 32)")
COMMITMENT=$(cast keccak "$(openssl rand -hex 32)")
NETWORK_ID=112015110234323138517908755257434054688  # Fuego testnet

echo "📝 Test parameters:"
echo "   Tier: $TIER"
echo "   Nullifier: $NULLIFIER"
echo "   Commitment: $COMMITMENT"
echo "   Network ID: $NETWORK_ID"
echo "   Deposit Timestamp: $DEPOSIT_TIMESTAMP"
echo "   Legacy: $LEGACY_FLAG"
echo ""

# Get tier info
echo "📊 Fetching tier info..."
TIER_INFO=$(cast call $COLD_VERIFIER \
    "getTierInfo(uint8,bool)" \
    $TIER \
    $LEGACY_FLAG \
    --rpc-url $ARB_SEPOLIA_RPC)

echo "✅ Tier info retrieved"
echo ""

# Estimate gas fee
echo "⛽ Estimating L1 gas fee..."
GAS_ESTIMATE=$(cast call $COLD_VERIFIER \
    "getRecommendedGasFee(address,uint8,bool)" \
    $RECIPIENT \
    $TIER \
    $LEGACY_FLAG \
    --rpc-url $ARB_SEPOLIA_RPC)

GAS_ESTIMATE_DEC=$((16#${GAS_ESTIMATE:2}))
GAS_ETH=$(echo "scale=6; $GAS_ESTIMATE_DEC / 1000000000000000000" | bc)

echo "✅ Recommended gas fee: $GAS_ETH ETH ($GAS_ESTIMATE_DEC wei)"
echo ""

# Confirm before proceeding
read -p "Proceed with claim? This will cost ~$GAS_ETH ETH in gas. (y/n): " CONFIRM
if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
    echo "❌ Aborted by user"
    exit 0
fi

echo ""
echo "🚀 Submitting claim transaction..."

# Submit claim (as API verifier)
TX_HASH=$(cast send $COLD_VERIFIER \
    "claimCD(address,uint8,bytes32,bytes32,uint256,uint256)" \
    $RECIPIENT \
    $TIER \
    $NULLIFIER \
    $COMMITMENT \
    $NETWORK_ID \
    $DEPOSIT_TIMESTAMP \
    --value $GAS_ESTIMATE \
    --private-key $PRIVATE_KEY \
    --rpc-url $ARB_SEPOLIA_RPC \
    --json | jq -r '.transactionHash')

echo "✅ Claim submitted: $TX_HASH"
echo "🔗 View on Arbiscan: https://sepolia.arbiscan.io/tx/$TX_HASH"
echo ""

# Wait for confirmation
echo "⏳ Waiting for L2 confirmation..."
sleep 10

# Check statistics
echo "📊 Checking updated statistics..."
STATS=$(cast call $COLD_VERIFIER "getStatistics()" --rpc-url $ARB_SEPOLIA_RPC)
echo "   Contract stats: $STATS"
echo ""

echo "⏳ Waiting for L2→L1 message relay (~10 minutes)..."
echo "   After relay completes, check CD balance on L1:"
echo "   cast call $CD_TOKEN \"balanceOf(address,uint256)\" $RECIPIENT 0 --rpc-url \$SEPOLIA_RPC"
echo ""

echo "✅ Test complete!"
echo ""
echo "📝 Next steps:"
echo "   1. Wait ~10 minutes for L2→L1 relay"
echo "   2. Check CD balance on Ethereum Sepolia"
echo "   3. Verify nullifier is marked as used"
echo "   4. Test additional tiers"
echo ""
