#!/bin/bash
set -e

echo "🚀 COLD Testnet Quick Start"
echo "=============================="
echo ""

# 1. Check environment
echo "1️⃣  Checking environment variables..."
[ -z "$FUEGO_TESTNET_RPC" ] && echo "ERROR: FUEGO_TESTNET_RPC not set" && exit 1
[ -z "$ARB_SEPOLIA_RPC" ] && echo "ERROR: ARB_SEPOLIA_RPC not set" && exit 1
[ -z "$SEPOLIA_RPC" ] && echo "ERROR: SEPOLIA_RPC not set" && exit 1
[ -z "$PRIVATE_KEY" ] && echo "ERROR: PRIVATE_KEY not set" && exit 1
echo "✅ Environment variables set"
echo ""

# 2. Verify Fuego testnet connectivity
echo "2️⃣  Verifying Fuego testnet RPC..."
curl -s -X POST "$FUEGO_TESTNET_RPC" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"0","method":"getheight","params":{}}' | grep -q result
echo "✅ Fuego testnet RPC working"
echo ""

# 3. Deploy contracts
echo "3️⃣  Deploying contracts to Sepolia..."
cd xfg-stark
bash scripts/deploy-cold-testnet.sh
echo "✅ Contracts deployed"
echo ""

# 4. Start API backend
echo "4️⃣  Starting API backend..."
cd api
npm install > /dev/null 2>&1
npm run dev &
API_PID=$!
echo "✅ API running (PID: $API_PID)"
echo ""

# 5. Start frontend
echo "5️⃣  Starting frontend..."
cd ../frontend
npm install > /dev/null 2>&1
npm run dev &
FRONTEND_PID=$!
echo "✅ Frontend running (PID: $FRONTEND_PID)"
echo ""

echo "🎉 Testnet setup complete!"
echo ""
echo "Access:"
echo "  Frontend: http://localhost:3000"
echo "  API: http://localhost:3001"
echo ""
echo "Next steps:"
echo "1. Create deposit on Fuego testnet (0xCD tag)"
echo "2. Open http://localhost:3000 in browser"
echo "3. Connect MetaMask to Arbitrum Sepolia"
echo "4. Submit claim with deposit commitment"
echo "5. Receive CD tokens on Arbitrum Sepolia"
echo ""
echo "Press Ctrl+C to stop services"
wait
