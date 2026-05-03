#!/bin/bash

# XFG STARK Proof Example Workflow
# This script demonstrates the complete workflow from template to proof

set -e

echo "🚀 XFG STARK Proof Example Workflow"
echo "====================================="
echo ""

# Check if CLI tool exists
if [ ! -f "target/release/xfg-stark-cli" ]; then
    echo "❌ CLI tool not found. Please run ./scripts/build-cli.sh first."
    exit 1
fi

# Create working directory
WORK_DIR="example_workflow"
mkdir -p $WORK_DIR
cd $WORK_DIR

echo "📁 Working directory: $WORK_DIR"
echo ""

# Step 1: Create template
echo "📝 Step 1: Creating standard burn template..."
../target/release/xfg-stark-cli create-template standard -o standard_template.json
echo "✅ Template created: standard_template.json"
echo ""

# Step 2: Create data package
echo "📦 Step 2: Creating data package..."
../target/release/xfg-stark-cli create-package \
  --template standard_template.json \
  --burn-amount 0.8 \
  --txn-hash 0x7D0725F8E03021B99560ADD456C596FEA7D8DF23529E23765E56923B73236E4D \
  --recipient 0x742d35Cc6634C0532925a3b8D4C9db96C4b4d8b6 \
  --secret "my-super-secret-key-12345" \
  --network fuego-testnet \
  --output my_burn_package.json

echo "✅ Data package created: my_burn_package.json"
echo ""

# Step 3: Edit package (add block height and timestamp)
echo "✏️  Step 3: Editing package with real data..."
cat > my_burn_package.json << 'EOF'
{
  "metadata": {
    "version": "1.0.0",
    "created_at": "2024-01-15T10:30:00Z",
    "description": "STARK proof for 0.8 XFG burn",
    "network": "fuego-testnet"
  },
  "burn_transaction": {
    "transaction_hash": "0x7D0725F8E03021B99560ADD456C596FEA7D8DF23529E23765E56923B73236E4D",
    "burn_amount_xfg": "0.8",
    "burn_amount_atomic": 8000000,
    "block_height": 1234567,
    "timestamp": 1705312200,
    "network_id": "fuego-testnet"
  },
  "recipient": {
    "ethereum_address": "0x742d35Cc6634C0532925a3b8D4C9db96C4b4d8b6",
    "ens_name": "alice.eth",
    "label": "Alice's HEAT wallet"
  },
  "secret": {
    "secret_key": "my-super-secret-key-12345",
    "salt": "random-salt-67890",
    "hint": "Remember: my favorite color + birth year"
  },
  "additional_data": {
    "burn_reason": "HEAT accumulation",
    "priority": "high"
  }
}
EOF

echo "✅ Package updated with real data"
echo ""

# Step 4: Validate package
echo "🔍 Step 4: Validating data package..."
../target/release/xfg-stark-cli validate -i my_burn_package.json
echo ""

# Step 5: Generate STARK proof
echo "⚡ Step 5: Generating STARK proof..."
../target/release/xfg-stark-cli generate -i my_burn_package.json -o proof.json
echo ""

# Step 6: Show results
echo "📊 Results Summary:"
echo "==================="
echo "📁 Template: standard_template.json"
echo "📦 Package: my_burn_package.json"
echo "🔐 Proof: proof.json"
echo ""

echo "📏 Proof file size: $(wc -c < proof.json) bytes"
echo ""

# Step 7: Show proof structure
echo "🔍 Proof file structure:"
head -20 proof.json
echo "..."
echo ""

echo "🎉 Workflow completed successfully!"
echo ""
echo "💡 Next steps:"
echo "   1. Review the generated files in $WORK_DIR/"
echo "   2. Submit proof.json to the HEAT mint contract"
echo "   3. Include Eldernode validation proof for on-chain verification"
echo ""
echo "📚 For more information, see docs/STARK_PROOF_USER_GUIDE.md"
