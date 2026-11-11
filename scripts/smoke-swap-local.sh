#!/usr/bin/env bash
# Local smoke: unit matrix + fuegod testnet RPC + xfg-swapd list
# Usage: from repo root, after build_wt is configured:
#   ./scripts/smoke-swap-local.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/build_wt/src"
DATA="${TMPDIR:-/tmp}/xfg-smoke-$$"
mkdir -p "$DATA/node" "$DATA/swapd"

pass=0; fail=0
run_unit() {
  local t="$1"
  if [[ -x "$BIN/$t" ]] && "$BIN/$t" >/dev/null 2>&1; then
    echo "PASS $t"; pass=$((pass+1))
  else
    echo "FAIL $t"; fail=$((fail+1))
  fi
}

echo "=== Unit matrix ==="
for t in \
  test_production_gates test_adaptor_roundtrip test_swap_hashlock \
  test_bch_htlc test_bch_chain_client test_bch_secret_reveal \
  test_swap_state_machine_spv test_xmr_sweep_sequence test_dcr_chain_client \
  test_spv_merkle test_sol_htlc_address test_price_oracle_arb \
  test_eth_protocol test_ring_collab test_xfg_spend_states
do
  run_unit "$t"
done

echo "=== fuegod testnet smoke ==="
if [[ ! -x "$BIN/fuegod" ]]; then
  echo "SKIP fuegod missing"; exit 1
fi
"$BIN/fuegod" --testnet --data-dir "$DATA/node" --rpc-bind-ip 127.0.0.1 \
  --rpc-bind-port 28281 --p2p-bind-port 20809 --log-file "$DATA/node.log" \
  --log-level 1 --no-console >"$DATA/node.stdout" 2>&1 &
NPID=$!
cleanup() { kill "$NPID" 2>/dev/null || true; }
trap cleanup EXIT
rpc_ok=0
for i in $(seq 1 60); do
  resp=$(curl -s -X POST http://127.0.0.1:28281/json_rpc \
    -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":"0","method":"getblockcount","params":{}}' 2>/dev/null || true)
  if echo "$resp" | grep -q '"count"'; then
    echo "PASS fuegod RPC getblockcount ($resp)"
    pass=$((pass+1))
    rpc_ok=1
    break
  fi
  sleep 0.5
done
if [[ $rpc_ok -eq 0 ]]; then
  echo "FAIL fuegod RPC"
  echo "--- node.stdout ---"; tail -40 "$DATA/node.stdout" 2>/dev/null || true
  echo "--- node.log ---"; tail -40 "$DATA/node.log" 2>/dev/null || true
  fail=$((fail+1))
fi

if [[ -x "$BIN/xfg-swapd" ]]; then
  out=$("$BIN/xfg-swapd" --testnet --data-dir "$DATA/swapd" \
    --fuegod-host 127.0.0.1 --fuegod-port 28281 list 2>&1 || true)
  if echo "$out" | grep -qi 'no swaps\|swap'; then
    echo "PASS xfg-swapd list against local fuegod"
    pass=$((pass+1))
  else
    echo "FAIL xfg-swapd list: $out"
    fail=$((fail+1))
  fi
fi

echo "=== SUMMARY pass=$pass fail=$fail ==="
exit $(( fail == 0 ? 0 : 1 ))
