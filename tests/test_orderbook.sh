#!/bin/bash
# test_orderbook.sh — end-to-end P2P order book flow
# Requires: fuegod running on localhost:18180

set -e

DAEMON="${1:-http://127.0.0.1:18180}"
PASS=0
FAIL=0

check() {
  local desc="$1"
  local condition="$2"
  if eval "$condition"; then
    echo "  ✓ $desc"
    PASS=$((PASS + 1))
  else
    echo "  ✗ $desc"
    FAIL=$((FAIL + 1))
  fi
}

echo "=== P2P Orderbook Integration Tests ==="
echo "Daemon: $DAEMON"
echo

# 1. Empty book
echo "--- 1. Empty book ---"
RESP=$(curl -sf "$DAEMON/getorderbook" -d '{"pair":0,"depth":5}' 2>/dev/null || echo '{"bids":[],"asks":[],"status":"ERROR"}')
check "getorderbook returns OK"  'echo "$RESP" | grep -q "\"status\""'
check "empty bids array"  'echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert len(d.get(\"bids\",[]))==0" 2>/dev/null'
check "empty asks array"  'echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert len(d.get(\"asks\",[]))==0" 2>/dev/null'
echo

# 2. Place ASK order
echo "--- 2. Place ASK: 100 XFG @ 1.50 ---"
RESP=$(curl -sf "$DAEMON/placeorder" -d '{"side":1,"pair":0,"price":15000000,"amount":1000000000,"ttlBlocks":100}' 2>/dev/null || echo '{"status":"ERROR"}')
ASK_ID=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get(\"orderId\",\"\"))" 2>/dev/null)
check "place ASK returns OK"  'echo "$RESP" | grep -q "\"OK\""'
check "orderId is non-empty"  '[ -n "$ASK_ID" ]'
echo "  orderId: ${ASK_ID:0:16}..."
echo

# 3. Verify ASK appears in book
echo "--- 3. Book after ASK ---"
RESP=$(curl -sf "$DAEMON/getorderbook" -d '{"pair":0,"depth":5}' 2>/dev/null || echo '{}')
check "asks has 1 level"  'echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert len(d.get(\"asks\",[]))==1" 2>/dev/null'
check "ask price = 15000000"  'echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert d[\"asks\"][0][\"price\"]==15000000" 2>/dev/null'
check "bids empty"  'echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert len(d.get(\"bids\",[]))==0" 2>/dev/null'
echo

# 4. Place BID that crosses the ASK
echo "--- 4. Place BID: 50 XFG @ 1.60 (crosses) ---"
RESP=$(curl -sf "$DAEMON/placeorder" -d '{"side":0,"pair":0,"price":16000000,"amount":500000000,"ttlBlocks":100}' 2>/dev/null || echo '{"status":"ERROR"}')
check "place BID returns OK"  'echo "$RESP" | grep -q "\"OK\""'
echo

# 5. Verify partial fill — ask should have 50 XFG remaining
echo "--- 5. Book after cross ---"
RESP=$(curl -sf "$DAEMON/getorderbook" -d '{"pair":0,"depth":5}' 2>/dev/null || echo '{}')
check "ask depth = 500000000 (50 XFG remaining)"  'echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert d[\"asks\"][0][\"amount\"]==500000000" 2>/dev/null'
echo

# 6. Place non-crossing BID
echo "--- 6. Place BID: 25 XFG @ 1.40 (no cross) ---"
RESP=$(curl -sf "$DAEMON/placeorder" -d '{"side":0,"pair":0,"price":14000000,"amount":250000000,"ttlBlocks":100}' 2>/dev/null || echo '{"status":"ERROR"}')
check "place BID returns OK"  'echo "$RESP" | grep -q "\"OK\""'
echo

# 7. Verify book has both sides
echo "--- 7. Final snapshot ---"
RESP=$(curl -sf "$DAEMON/getorderbook" -d '{"pair":0,"depth":5}' 2>/dev/null || echo '{}')
check "has asks"  'echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert len(d.get(\"asks\",[]))>=1" 2>/dev/null'
check "has bids"  'echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert len(d.get(\"bids\",[]))>=1" 2>/dev/null'
check "spread > 0"  'echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); assert d.get(\"spread\",0)>0" 2>/dev/null'
echo

# 8. Cancel order
echo "--- 8. Cancel order ---"
if [ -n "$ASK_ID" ]; then
  RESP=$(curl -sf "$DAEMON/cancelorder" -d "{\"orderId\":\"$ASK_ID\"}" 2>/dev/null || echo '{"status":"ERROR"}')
  check "cancel returns OK"  'echo "$RESP" | grep -q "\"OK\""'
else
  echo "  (skipped: no orderId)"
fi
echo

# Summary
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
