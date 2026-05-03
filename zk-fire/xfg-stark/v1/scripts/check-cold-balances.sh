#!/bin/bash
# COLD Deposits Balance Checker
# Checks CD balances and contract stats

set -e

echo "💰 COLD Deposits Balance Checker"
echo "================================="
echo ""

# Check environment variables
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

echo "📋 CD Token: $CD_TOKEN"
echo "📋 COLD Verifier: $COLD_VERIFIER"
echo ""

# Prompt for address
read -p "Enter address to check (or press Enter to skip): " ADDRESS

if [ -n "$ADDRESS" ]; then
    echo ""
    echo "🔍 Checking balances for: $ADDRESS"
    echo ""

    # Check CD balance for edition 0
    BALANCE_0=$(cast call $CD_TOKEN \
        "balanceOf(address,uint256)" \
        $ADDRESS \
        0 \
        --rpc-url $SEPOLIA_RPC)

    BALANCE_0_DEC=$((16#${BALANCE_0:2}))
    BALANCE_0_READABLE=$(echo "scale=12; $BALANCE_0_DEC / 1000000000000" | bc)

    echo "📊 CD Balance (Edition 0):"
    echo "   Atomic units: $BALANCE_0_DEC"
    echo "   Readable: $BALANCE_0_READABLE CD"
    echo ""

    # Get voting power
    VOTING_POWER=$(cast call $CD_TOKEN \
        "getVotingPower(address)" \
        $ADDRESS \
        --rpc-url $SEPOLIA_RPC)

    VOTING_POWER_DEC=$((16#${VOTING_POWER:2}))
    VOTING_POWER_READABLE=$(echo "scale=12; $VOTING_POWER_DEC / 1000000000000" | bc)

    echo "🗳️  Voting Power:"
    echo "   Atomic units: $VOTING_POWER_DEC"
    echo "   Readable: $VOTING_POWER_READABLE CD"
    echo ""

    # Get deposit info
    DEPOSIT_INFO=$(cast call $CD_TOKEN \
        "getDepositInfo(address)" \
        $ADDRESS \
        --rpc-url $SEPOLIA_RPC)

    echo "📝 Deposit Info:"
    echo "   Raw: $DEPOSIT_INFO"
    echo ""
fi

# Check contract statistics
echo "📊 Contract Statistics"
echo "======================"
echo ""

# L2 Statistics
echo "🔹 L2 (Arbitrum Sepolia):"
STATS=$(cast call $COLD_VERIFIER "getStatistics()" --rpc-url $ARB_SEPOLIA_RPC)
echo "   Raw stats: $STATS"

# Parse stats array [totalProofs, totalCD, totalClaims]
TOTAL_PROOFS=$(echo $STATS | cut -d',' -f1 | tr -d '[]')
TOTAL_CD=$(echo $STATS | cut -d',' -f2)
TOTAL_CLAIMS=$(echo $STATS | cut -d',' -f3 | tr -d '[]')

TOTAL_PROOFS_DEC=$((16#${TOTAL_PROOFS:2}))
TOTAL_CD_DEC=$((16#${TOTAL_CD:2}))
TOTAL_CLAIMS_DEC=$((16#${TOTAL_CLAIMS:2}))

echo "   Total proofs verified: $TOTAL_PROOFS_DEC"
echo "   Total CD minted: $TOTAL_CD_DEC atomic units"
echo "   Total claims: $TOTAL_CLAIMS_DEC"
echo ""

MAX_TIER=$(cast call $COLD_VERIFIER "getMaxTierIndex()" --rpc-url $ARB_SEPOLIA_RPC)
MAX_TIER_DEC=$((16#${MAX_TIER:2}))
echo "   Max tier index: $MAX_TIER_DEC"
echo ""

# L1 Statistics
echo "🔹 L1 (Ethereum Sepolia):"
TOTAL_CD_L1=$(cast call $CD_TOKEN "getTotalSupply()" --rpc-url $SEPOLIA_RPC)
TOTAL_CD_L1_DEC=$((16#${TOTAL_CD_L1:2}))
TOTAL_CD_L1_READABLE=$(echo "scale=12; $TOTAL_CD_L1_DEC / 1000000000000" | bc)

echo "   Total CD supply: $TOTAL_CD_L1_DEC atomic units ($TOTAL_CD_L1_READABLE CD)"

CURRENT_EDITION=$(cast call $CD_TOKEN "currentEditionId()" --rpc-url $SEPOLIA_RPC)
CURRENT_EDITION_DEC=$((16#${CURRENT_EDITION:2}))
echo "   Current edition ID: $CURRENT_EDITION_DEC"

AVAILABLE_SUPPLY=$(cast call $CD_TOKEN "getAvailableSupply()" --rpc-url $SEPOLIA_RPC)
AVAILABLE_SUPPLY_DEC=$((16#${AVAILABLE_SUPPLY:2}))
AVAILABLE_SUPPLY_READABLE=$(echo "scale=12; $AVAILABLE_SUPPLY_DEC / 1000000000000" | bc)
echo "   Available supply: $AVAILABLE_SUPPLY_DEC atomic units ($AVAILABLE_SUPPLY_READABLE CD)"
echo ""

# Tier Information
echo "📊 Tier Information"
echo "==================="
echo ""

for TIER in 0 1 2 3; do
    echo "Tier $TIER:"

    # Standard tier info
    TIER_INFO=$(cast call $COLD_VERIFIER \
        "getTierInfo(uint8,bool)" \
        $TIER \
        false \
        --rpc-url $ARB_SEPOLIA_RPC)

    echo "   Standard: $TIER_INFO"

    # Legacy tier info (only for tier 2-3)
    if [ $TIER -ge 2 ]; then
        LEGACY_INFO=$(cast call $COLD_VERIFIER \
            "getTierInfo(uint8,bool)" \
            $TIER \
            true \
            --rpc-url $ARB_SEPOLIA_RPC)

        echo "   Legacy: $LEGACY_INFO"
    fi

    echo ""
done

echo "✅ Balance check complete!"
echo ""
