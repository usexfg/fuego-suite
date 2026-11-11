#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  Fuego Testnet Suite — Milæsandra Launch
#  Starts: testnetd + xfg-swapd(testnet) + test_wallet
# ═══════════════════════════════════════════════════════════════════
set -e

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; NC='\033[0m'
log()  { echo -e "${BLUE}[$(date +%H:%M:%S)]${NC} $1"; }
ok()   { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err()  { echo -e "${RED}[ERR]${NC} $1"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}/.."
BUILD_DIR="${PROJECT_DIR}/build"
DATA_DIR="${PROJECT_DIR}/testnet-data"
LOG_DIR="${DATA_DIR}/logs"

TESTNET_PORT=28280
TESTNET_P2P=28282
WALLET_PORT=8070
SWAP_PORT=5001
SWAP_P2P_PORT=5002

PIDS=()

cleanup() {
    log "Shutting down Milæsandra..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    log "All services stopped."
    exit 0
}
trap cleanup INT TERM

# ── Setup ──
mkdir -p "$DATA_DIR" "$LOG_DIR"

# ── Check binaries ──
for bin in testnetd xfg-swapd test_wallet; do
    if [ ! -f "${BUILD_DIR}/src/${bin}" ]; then
        err "Binary not found: ${BUILD_DIR}/src/${bin}"
        err "Run build first: cmake --build ${BUILD_DIR} -- -j4"
        exit 1
    fi
done
ok "All binaries found"

# ── Clean previous data (fresh testnet) ──
rm -rf "$DATA_DIR"/*
mkdir -p "$LOG_DIR"

# ══════════════════════════════════════════════════════════════
#  1. Start testnetd
# ══════════════════════════════════════════════════════════════
log "Starting testnet daemon (port ${TESTNET_PORT}, p2p ${TESTNET_P2P})..."
"${BUILD_DIR}/src/testnetd" \
    --data-dir "${DATA_DIR}/daemon" \
    --log-file "${LOG_DIR}/testnetd.log" \
    --rpc-bind-port ${TESTNET_PORT} \
    --p2p-bind-port ${TESTNET_P2P} \
    > "${LOG_DIR}/testnetd.out" 2>&1 &
PIDS+=($!)

# Wait for daemon
log "Waiting for daemon to start..."
for i in $(seq 1 60); do
    if curl -s http://localhost:${TESTNET_PORT}/getinfo > /dev/null 2>&1; then
        ok "Daemon ready (${i}s)"
        break
    fi
    sleep 1
done

# Get daemon info
HEIGHT=$(curl -s http://localhost:${TESTNET_PORT}/getinfo | python3 -c "import sys,json; print(json.load(sys.stdin).get('height','?'))" 2>/dev/null || echo "?")
log "  Height: ${HEIGHT} | RPC: http://localhost:${TESTNET_PORT}"

# ══════════════════════════════════════════════════════════════
#  2. Start xfg-swapd (testnet mode)
# ══════════════════════════════════════════════════════════════
log "Starting xfg-swapd daemon (testnet, port ${SWAP_PORT})..."
"${BUILD_DIR}/src/xfg-swapd" \
    --testnet \
    --rpc-bind-port ${SWAP_PORT} \
    --p2p-bind-port ${SWAP_P2P_PORT} \
    --fuego-rpc-port ${TESTNET_PORT} \
    --data-dir "${DATA_DIR}/swap" \
    > "${LOG_DIR}/xfg-swapd.out" 2>&1 &
PIDS+=($!)

sleep 2
if curl -s http://localhost:${SWAP_PORT}/getinfo > /dev/null 2>&1; then
    ok "xfg-swapd ready"
else
    warn "xfg-swapd may not be fully ready (retrying)"
fi
log "  Swap daemon: http://localhost:${SWAP_PORT}"

# ══════════════════════════════════════════════════════════════
#  3. Start test_wallet
# ══════════════════════════════════════════════════════════════
log "Starting test_wallet (port ${WALLET_PORT})..."
"${BUILD_DIR}/src/test_wallet" \
    --daemon-port ${TESTNET_PORT} \
    --rpc-bind-port ${WALLET_PORT} \
    --data-dir "${DATA_DIR}/wallet" \
    > "${LOG_DIR}/test_wallet.out" 2>&1 &
PIDS+=($!)

sleep 2
if curl -s http://localhost:${WALLET_PORT}/getinfo > /dev/null 2>&1; then
    ok "Wallet ready"
else
    warn "Wallet may need initialization"
fi
log "  Wallet RPC: http://localhost:${WALLET_PORT}"

# ══════════════════════════════════════════════════════════════
#  Summary
# ══════════════════════════════════════════════════════════════
echo ""
echo "══════════════════════════════════════════════════════"
echo "  ${CYAN}Milæsandra Testnet Suite — Online${NC}"
echo "══════════════════════════════════════════════════════"
echo ""
echo "  Services:"
echo "    testnetd:     http://localhost:${TESTNET_PORT}"
echo "    xfg-swapd:     http://localhost:${SWAP_PORT}"
echo "    test_wallet:  http://localhost:${WALLET_PORT}"
echo ""
echo "  Logs: ${LOG_DIR}/"
echo "  Data: ${DATA_DIR}/"
echo ""
echo "  Test Milæsandra RPC:"
echo "    curl http://localhost:${TESTNET_PORT}/heat_metrics"
echo "    curl http://localhost:${TESTNET_PORT}/amm_pool_info"
echo "    curl http://localhost:${TESTNET_PORT}/amm_quote -d '{\"input_amount\":100,\"direction\":0}'"
echo ""
echo "  Press Ctrl+C to stop all services"
echo "══════════════════════════════════════════════════════"

# Launch dashboard if python3 available
if command -v python3 &> /dev/null; then
    log "Starting Milæsandra dashboard..."
    "${SCRIPT_DIR}/milaesandra-dashboard.py" &
    PIDS+=($!)
fi

# Wait for any process to exit
wait
