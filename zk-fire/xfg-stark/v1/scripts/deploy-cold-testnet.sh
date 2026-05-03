#!/bin/bash
# COLD Deposits Testnet Deployment Script
# Networks: Ethereum Sepolia + Arbitrum Sepolia
# Date: 2026-01-18

set -e

echo "❄️  COLD Deposits Testnet Deployment"
echo "======================================"
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

# Get deployer address
DEPLOYER=$(cast wallet address --private-key $PRIVATE_KEY)
echo "🔑 Deployer: $DEPLOYER"
echo ""

# Step 1: Deploy FuegoCOLDAOToken on Ethereum Sepolia
echo "📝 Step 1: Deploying FuegoCOLDAOToken on Ethereum Sepolia..."
echo "⏳ This will take a moment..."

CD_TOKEN=$(forge create FuegoCOLDAOToken \
    --constructor-args \
        "0x0000000000000000000000000000000000000001" \
        "0x0000000000000000000000000000000000000002" \
        "$DEPLOYER" \
    --private-key $PRIVATE_KEY \
    --rpc-url $SEPOLIA_RPC \
    --json | jq -r '.deployedTo')

echo "✅ FuegoCOLDAOToken deployed: $CD_TOKEN"
echo ""

# Wait for confirmation
sleep 5

# Step 2: Deploy COLDAOGovernor on Ethereum Sepolia
echo "📝 Step 2: Deploying COLDAOGovernor on Ethereum Sepolia..."

GOVERNOR=$(forge create COLDAOGovernor \
    --constructor-args \
        "$CD_TOKEN" \
        800 \
        "$DEPLOYER" \
    --private-key $PRIVATE_KEY \
    --rpc-url $SEPOLIA_RPC \
    --json | jq -r '.deployedTo')

echo "✅ COLDAOGovernor deployed: $GOVERNOR"
echo ""

# Wait for confirmation
sleep 5

# Step 3: Deploy COLDDepositProofVerifier on Arbitrum Sepolia
echo "📝 Step 3: Deploying COLDDepositProofVerifier on Arbitrum Sepolia..."

# Use deployer as initial API verifier for testing
COLD_VERIFIER=$(forge create COLDDepositProofVerifier \
    --constructor-args \
        "$CD_TOKEN" \
        "$DEPLOYER" \
        "$DEPLOYER" \
    --private-key $PRIVATE_KEY \
    --rpc-url $ARB_SEPOLIA_RPC \
    --json | jq -r '.deployedTo')

echo "✅ COLDDepositProofVerifier deployed: $COLD_VERIFIER"
echo ""

# Wait for confirmation
sleep 5

# Step 4: Configure FuegoCOLDAOToken
echo "📝 Step 4: Configuring FuegoCOLDAOToken..."

# Update COLDAO governor
echo "  → Setting COLDAO governor to $GOVERNOR..."
cast send $CD_TOKEN \
    "updateCOLDAOGovernor(address)" \
    $GOVERNOR \
    --private-key $PRIVATE_KEY \
    --rpc-url $SEPOLIA_RPC \
    --json > /dev/null

sleep 3

# Authorize COLD verifier as minter
echo "  → Authorizing COLD verifier as minter..."
cast send $CD_TOKEN \
    "addAuthorizedMinter(address)" \
    $COLD_VERIFIER \
    --private-key $PRIVATE_KEY \
    --rpc-url $SEPOLIA_RPC \
    --json > /dev/null

echo "✅ FuegoCOLDAOToken configured"
echo ""

# Step 5: Verify deployments
echo "📝 Step 5: Verifying deployments..."

# Check CD token configuration
CURRENT_EDITION=$(cast call $CD_TOKEN "currentEditionId()" --rpc-url $SEPOLIA_RPC)
CURRENT_EDITION_DEC=$((16#${CURRENT_EDITION:2}))
echo "  → CD Token current edition: $CURRENT_EDITION_DEC"

IS_AUTHORIZED=$(cast call $CD_TOKEN "isAuthorizedMinter(address)(bool)" $COLD_VERIFIER --rpc-url $SEPOLIA_RPC)
echo "  → COLD Verifier authorized: $IS_AUTHORIZED"

# Check COLD verifier configuration
MAX_TIER=$(cast call $COLD_VERIFIER "getMaxTierIndex()" --rpc-url $ARB_SEPOLIA_RPC)
MAX_TIER_DEC=$((16#${MAX_TIER:2}))
echo "  → COLD Verifier max tier: $MAX_TIER_DEC"

API_VERIFIER=$(cast call $COLD_VERIFIER "apiVerifier()" --rpc-url $ARB_SEPOLIA_RPC)
echo "  → API Verifier address: $API_VERIFIER"

echo ""
echo "✅ Deployment verified!"
echo ""

# Step 6: Save deployment addresses
echo "📝 Step 6: Saving deployment addresses..."

DEPLOYMENT_FILE="deployments/testnet-$(date +%Y%m%d-%H%M%S).json"
mkdir -p deployments

cat > $DEPLOYMENT_FILE << EOF
{
  "network": "testnet",
  "deployer": "$DEPLOYER",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "contracts": {
    "ethereum_sepolia": {
      "FuegoCOLDAOToken": "$CD_TOKEN",
      "COLDAOGovernor": "$GOVERNOR"
    },
    "arbitrum_sepolia": {
      "COLDDepositProofVerifier": "$COLD_VERIFIER"
    }
  },
  "configuration": {
    "apiVerifier": "$DEPLOYER",
    "initialAPY": "800",
    "maxTierIndex": "$MAX_TIER_DEC",
    "currentEdition": "$CURRENT_EDITION_DEC"
  }
}
EOF

echo "✅ Deployment saved to $DEPLOYMENT_FILE"
echo ""

# Step 7: Display summary
echo "🎉 COLD Deposits Testnet Deployment Complete!"
echo "=============================================="
echo ""
echo "📋 Contract Addresses:"
echo "   Ethereum Sepolia:"
echo "     FuegoCOLDAOToken:     $CD_TOKEN"
echo "     COLDAOGovernor:       $GOVERNOR"
echo ""
echo "   Arbitrum Sepolia:"
echo "     COLDDepositProofVerifier: $COLD_VERIFIER"
echo ""
echo "🔗 Explorers:"
echo "   Ethereum Sepolia:"
echo "     https://sepolia.etherscan.io/address/$CD_TOKEN"
echo "     https://sepolia.etherscan.io/address/$GOVERNOR"
echo ""
echo "   Arbitrum Sepolia:"
echo "     https://sepolia.arbiscan.io/address/$COLD_VERIFIER"
echo ""
echo "📝 Next Steps:"
echo "   1. Verify contracts on explorers"
echo "   2. Update API verifier address when ready"
echo "   3. Test tier 0 (0.8 XFG × 3mo)"
echo "   4. Test tier 1 (0.8 XFG × 12mo)"
echo "   5. Test tier 2 (800 XFG × 3mo)"
echo "   6. Test tier 3 (800 XFG × 12mo)"
echo "   7. Test legacy deposits (pre-2026)"
echo ""
echo "❄️  Winter is coming."
