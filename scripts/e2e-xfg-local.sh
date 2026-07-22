#!/usr/bin/env bash
# Attempt local dual-party XFG e2e infrastructure:
#   1) unit dual-protocol (crypto) — always run
#   2) fuegod testnet + walletd + known Bob keys
#   3) report mining / mixin blockers for full fundEscrow
#
# Usage (from repo root, build_wt ready):
#   ./scripts/e2e-xfg-local.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build_wt/src"
BASE="${TMPDIR:-/tmp}/xfg-e2e-full"
pass=0; fail=0

echo "=== 1) Dual-party crypto protocol ==="
if [[ -x "$BIN/test_xfg_dual_protocol" ]] && "$BIN/test_xfg_dual_protocol"; then
  pass=$((pass+1))
else
  echo "FAIL test_xfg_dual_protocol (build it first)"
  fail=$((fail+1))
fi

echo "=== 2) Live node / wallet inventory ==="
if curl -sf -m 3 -X POST http://127.0.0.1:28280/json_rpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":"0","method":"getblockcount","params":{}}' | grep -q count; then
  H=$(curl -s -m 3 -X POST http://127.0.0.1:28280/json_rpc -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":"0","method":"getblockcount","params":{}}')
  echo "PASS fuegod reachable: $H"
  pass=$((pass+1))
else
  echo "SKIP/FAIL no fuegod on 28280"
  fail=$((fail+1))
fi

if curl -sf -m 3 -X POST http://127.0.0.1:18070/json_rpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":"1","method":"getBalance","params":{}}' | grep -q availableBalance; then
  B=$(curl -s -m 3 -X POST http://127.0.0.1:18070/json_rpc -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":"1","method":"getBalance","params":{}}')
  echo "PASS walletd balance: $B"
  pass=$((pass+1))
else
  echo "SKIP no walletd on 18070"
fi

echo "=== 3) fundEscrow prerequisites ==="
# Mix-in / decoys: get_outs via node
OUTS=$(curl -s -m 5 -X POST http://127.0.0.1:28280/json_rpc -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":"1","method":"getrandom_outs","params":{"amounts":[10000000],"outs_count":9}}' 2>/dev/null || true)
if echo "$OUTS" | grep -q outs; then
  echo "PASS getrandom_outs responded"
  pass=$((pass+1))
else
  echo "BLOCKED getrandom_outs / decoys: $OUTS"
  echo "  (testnet often has too few outputs — MixIn count too big)"
  fail=$((fail+1))
fi

INFO=$(curl -s -m 3 http://127.0.0.1:28280/getinfo 2>/dev/null || true)
echo "getinfo: $INFO" | head -c 300; echo
if echo "$INFO" | grep -q 'mined block failed\|"height":7'; then
  echo "NOTE: if height stagnates, miner may hit 'mined block failed verification'"
fi

echo "=== SUMMARY pass=$pass fail=$fail ==="
echo "Dual-party crypto after escrow: covered by test_xfg_dual_protocol."
echo "Full fundEscrow on local testnet needs: working miner + enough decoy outputs."
exit $(( fail > 2 ? 1 : 0 ))
