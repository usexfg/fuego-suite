#!/usr/bin/env bash
# Checklist for dual xfg-swapd + fundEscrow on a fresh local testnet.
# Does NOT fail hard when services are down — reports status and next commands.
#
# Usage (from repo root):
#   ./scripts/when-testnet-ready.sh
# Env overrides:
#   FUEGOD_RPC_HOST / FUEGOD_RPC_PORT   (default 127.0.0.1:28280)
#   WALLETD_HOST / WALLETD_PORT         (default 127.0.0.1:18070  Bob)
#   WALLETD_ALICE_PORT                  (default 18071)
#   BIN                                 (default build_wt/src)
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$ROOT/build_wt/src}"
FUEGOD_RPC_HOST="${FUEGOD_RPC_HOST:-127.0.0.1}"
FUEGOD_RPC_PORT="${FUEGOD_RPC_PORT:-28280}"
WALLETD_HOST="${WALLETD_HOST:-127.0.0.1}"
WALLETD_PORT="${WALLETD_PORT:-18070}"
WALLETD_ALICE_PORT="${WALLETD_ALICE_PORT:-18071}"
BOB_STATUS_PORT="${BOB_STATUS_PORT:-18900}"
BOB_P2P_PORT="${BOB_P2P_PORT:-18901}"
ALICE_STATUS_PORT="${ALICE_STATUS_PORT:-18910}"
ALICE_P2P_PORT="${ALICE_P2P_PORT:-18911}"
ETH_RPC_PORT="${ETH_RPC_PORT:-8545}"
SOL_RPC_PORT="${SOL_RPC_PORT:-8899}"
CFG_BOB="${CFG_BOB:-$ROOT/scripts/templates/swap-config-bob.json}"
CFG_ALICE="${CFG_ALICE:-$ROOT/scripts/templates/swap-config-alice.json}"
DATA_BOB="${DATA_BOB:-${TMPDIR:-/tmp}/xfg-swapd-bob}"
DATA_ALICE="${DATA_ALICE:-${TMPDIR:-/tmp}/xfg-swapd-alice}"

ok=0; warn=0; fail=0
note() { echo "  $*"; }
pass() { echo "PASS  $*"; ok=$((ok+1)); }
soft() { echo "WARN  $*"; warn=$((warn+1)); }
miss() { echo "MISS  $*"; fail=$((fail+1)); }

echo "=== Dual swapd / fundEscrow readiness (soft checklist) ==="
echo "Repo: $ROOT"
echo

# ── binaries ──────────────────────────────────────────────────────────────
echo "--- Binaries ---"
if [[ -x "$BIN/fuegod" ]]; then pass "fuegod: $BIN/fuegod"; else miss "fuegod missing at $BIN/fuegod"; fi
if [[ -x "$BIN/xfg-swapd" ]]; then pass "xfg-swapd: $BIN/xfg-swapd"; else miss "xfg-swapd missing at $BIN/xfg-swapd"; fi
# walletd name varies by tree
WALLETD_BIN=""
for c in "$BIN/walletd" "$BIN/fuego-walletd" "$BIN/walletd/fuego-walletd"; do
  if [[ -x "$c" ]]; then WALLETD_BIN="$c"; break; fi
done
if [[ -n "$WALLETD_BIN" ]]; then pass "walletd: $WALLETD_BIN"; else soft "walletd binary not found under $BIN (start your own)"; fi
echo

# ── ports reference ───────────────────────────────────────────────────────
echo "--- Required / recommended ports (document only) ---"
cat <<EOF
  fuegod RPC (testnet)     ${FUEGOD_RPC_HOST}:${FUEGOD_RPC_PORT}
  Bob   walletd RPC        ${WALLETD_HOST}:${WALLETD_PORT}
  Alice walletd RPC        ${WALLETD_HOST}:${WALLETD_ALICE_PORT}
  Bob   xfg-swapd status   127.0.0.1:${BOB_STATUS_PORT}
  Bob   xfg-swapd P2P      127.0.0.1:${BOB_P2P_PORT}
  Alice xfg-swapd status   127.0.0.1:${ALICE_STATUS_PORT}
  Alice xfg-swapd P2P      127.0.0.1:${ALICE_P2P_PORT}
  ETH   anvil/local RPC    127.0.0.1:${ETH_RPC_PORT}
  SOL   test-validator     127.0.0.1:${SOL_RPC_PORT}
EOF
echo

# ── fuegod height ─────────────────────────────────────────────────────────
echo "--- fuegod RPC height ---"
FUEGO_URL="http://${FUEGOD_RPC_HOST}:${FUEGOD_RPC_PORT}"
resp=$(curl -s -m 3 -X POST "${FUEGO_URL}/json_rpc" \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":"0","method":"getblockcount","params":{}}' 2>/dev/null || true)
if echo "$resp" | grep -q '"count"'; then
  pass "fuegod getblockcount: $resp"
  info=$(curl -s -m 3 "${FUEGO_URL}/getinfo" 2>/dev/null || true)
  if [[ -n "$info" ]]; then note "getinfo: $(echo "$info" | head -c 240)"; fi
else
  soft "fuegod not reachable at ${FUEGO_URL} (start: $BIN/fuegod --testnet --rpc-bind-port ${FUEGOD_RPC_PORT} ...)"
  note "response: ${resp:-<empty>}"
fi
echo

# ── walletd balance (Bob) ─────────────────────────────────────────────────
echo "--- walletd balance (Bob @ ${WALLETD_PORT}) ---"
WURL="http://${WALLETD_HOST}:${WALLETD_PORT}/json_rpc"
bal=$(curl -s -m 3 -X POST "$WURL" \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":"1","method":"getBalance","params":{}}' 2>/dev/null || true)
if echo "$bal" | grep -q 'availableBalance\|lockedAmount\|balance'; then
  pass "Bob walletd getBalance: $bal"
else
  soft "Bob walletd not reachable at $WURL"
  note "response: ${bal:-<empty>}"
  note "fundEscrow needs xfg_wallet_rpc_* in Bob swap-config pointing here"
fi

echo "--- walletd balance (Alice @ ${WALLETD_ALICE_PORT}, optional) ---"
AURL="http://${WALLETD_HOST}:${WALLETD_ALICE_PORT}/json_rpc"
abal=$(curl -s -m 3 -X POST "$AURL" \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":"1","method":"getBalance","params":{}}' 2>/dev/null || true)
if echo "$abal" | grep -q 'availableBalance\|lockedAmount\|balance'; then
  pass "Alice walletd getBalance: $abal"
else
  soft "Alice walletd not on ${WALLETD_ALICE_PORT} (optional until claim path needs it)"
fi
echo

# ── configs ───────────────────────────────────────────────────────────────
echo "--- Swap configs ---"
if [[ -f "$CFG_BOB" ]]; then
  pass "Bob config present: $CFG_BOB"
else
  soft "Bob config missing — copy example:"
  note "cp $ROOT/scripts/templates/swap-config-bob.json.example $CFG_BOB"
  note "then fill xfg_secret_key, xfg_view_key, xfg_wallet_rpc_*"
fi
if [[ -f "$CFG_ALICE" ]]; then
  pass "Alice config present: $CFG_ALICE"
else
  soft "Alice config missing — copy example:"
  note "cp $ROOT/scripts/templates/swap-config-alice.json.example $CFG_ALICE"
  note "then fill CTR keys (eth_* / sol_*) + optional xfg_*"
fi
echo

# ── next commands ─────────────────────────────────────────────────────────
echo "=== Next commands (do not auto-run) ==="
cat <<EOF

# 0) Materialize configs (once)
cp -n $ROOT/scripts/templates/swap-config-bob.json.example   ${CFG_BOB}
cp -n $ROOT/scripts/templates/swap-config-alice.json.example ${CFG_ALICE}
# edit: xfg_secret_key, xfg_view_key, xfg_wallet_rpc_*, eth_*/sol_*

# 1) Start / verify testnet node (if not already running)
# $BIN/fuegod --testnet --data-dir /tmp/fuego-testnet \\
#   --rpc-bind-ip 127.0.0.1 --rpc-bind-port ${FUEGOD_RPC_PORT} \\
#   --p2p-bind-port 20808 --log-level 1

# 2) Start Bob + Alice walletd against fuegod (ports ${WALLETD_PORT} / ${WALLETD_ALICE_PORT})
# Ensure Bob has spendable balance (mine or transfer). fundEscrow calls optimize + ring spend.

# 3) Dual xfg-swapd (service mode, separate data dirs + P2P ports)
mkdir -p "${DATA_BOB}" "${DATA_ALICE}"

# Bob (initiator / XFG funder):
$BIN/xfg-swapd --testnet \\
  --fuegod-host ${FUEGOD_RPC_HOST} --fuegod-port ${FUEGOD_RPC_PORT} \\
  --data-dir "${DATA_BOB}" \\
  --swap-config "${CFG_BOB}" \\
  --status-port ${BOB_STATUS_PORT} \\
  --swap-p2p-port ${BOB_P2P_PORT} --swap-p2p-bind 127.0.0.1 \\
  --service

# Alice (CTR locker / acceptor) — second terminal:
$BIN/xfg-swapd --testnet \\
  --fuegod-host ${FUEGOD_RPC_HOST} --fuegod-port ${FUEGOD_RPC_PORT} \\
  --data-dir "${DATA_ALICE}" \\
  --swap-config "${CFG_ALICE}" \\
  --status-port ${ALICE_STATUS_PORT} \\
  --swap-p2p-port ${ALICE_P2P_PORT} --swap-p2p-bind 127.0.0.1 \\
  --service

# 4) Initiate from Bob (amounts are atomic: 1 XFG = 10000000)
# Peer is Alice swap P2P endpoint. Pair examples: ETH | SOL
$BIN/xfg-swapd --testnet \\
  --fuegod-host ${FUEGOD_RPC_HOST} --fuegod-port ${FUEGOD_RPC_PORT} \\
  --data-dir "${DATA_BOB}" \\
  --swap-config "${CFG_BOB}" \\
  initiate ETH 10000000 1000000000000000 127.0.0.1:${ALICE_P2P_PORT}

# 5) Alice accepts (use swap_id from initiate / list)
$BIN/xfg-swapd --testnet \\
  --fuegod-host ${FUEGOD_RPC_HOST} --fuegod-port ${FUEGOD_RPC_PORT} \\
  --data-dir "${DATA_ALICE}" \\
  --swap-config "${CFG_ALICE}" \\
  accept <swap_id>

# 6) Watch progress (service ticks drive fundEscrow / lock / claim)
$BIN/xfg-swapd --testnet --data-dir "${DATA_BOB}"   --swap-config "${CFG_BOB}"   list
$BIN/xfg-swapd --testnet --data-dir "${DATA_ALICE}" --swap-config "${CFG_ALICE}" list

# Notes for fundEscrow:
#   - Bob config MUST set xfg_secret_key + xfg_view_key + xfg_wallet_rpc_host/port
#   - Empty chain *_rpc_host means that chain client is not registered
#   - initiate queries counterparty height — set eth_rpc_host or sol_rpc_host for that pair
#   - Fresh testnet needs enough outputs/decoys (or mixIn=0 testnet build) + spendable balance
#   - Keys JSON keys (exact): xfg_secret_key, xfg_view_key,
#       xfg_wallet_rpc_host, xfg_wallet_rpc_port, xfg_wallet_rpc_user, xfg_wallet_rpc_pass,
#       eth_rpc_host, eth_rpc_port, eth_priv_key, eth_address, eth_chain_id,
#       eth_htlc_bin_path, eth_htlc_registry,
#       sol_rpc_host, sol_rpc_port, sol_program_id, sol_keypair_path
EOF

echo
echo "=== SUMMARY pass=$ok warn=$warn miss=$fail ==="
echo "Soft checklist always exits 0 so it can be used while services are still starting."
exit 0
