#!/bin/bash
# Restart and reorg equivalence on a real testnet chain driven by v11_chain_harness.
# Each harness call is its own process, so a restart is a real one.
# Chain L (65 blocks, past the v11 upgrade at 30 and the epoch boundaries at 40, 50 and 60) is
# the reference. R replays L and restarts at block 47. F shares L up to block 35 and runs
# to block 80 on a different miner.
# Usage: v11_chain_checks.sh <path-to-v11_chain_harness> <empty-workdir>
set -u
BIN=$(realpath "$1"); W=$2
if [ -e "$W" ] && [ -n "$(ls -A "$W")" ]; then echo "workdir must be empty: $W"; exit 2; fi
mkdir -p "$W"; cd "$W"
FAILED=0
run() { "$BIN" "$@" 2>&1 || { echo "harness failed: $*"; exit 1; }; }
state() { run state "$1" > "state_$1.txt"; }
same_state() {
  local name=$1 expected=$2 actual=$3
  if diff "state_$expected.txt" "state_$actual.txt" > /dev/null; then echo "$name PASS"
  else FAILED=1; echo "$name FAIL"; diff "state_$expected.txt" "state_$actual.txt"; fi
}

TIP=65; RESTART=47; FORK=35; FORK_TIP=80
run mine L $TIP 1 0 | tail -1
run export L 1 $TIP chain.bin
run import R chain.bin 1 $RESTART | tail -1
run import R chain.bin $((RESTART + 1)) $TIP | tail -1
run import F chain.bin 1 $FORK | tail -1
run mine F $FORK_TIP 2 3 | tail -1
run export F $((FORK + 1)) $FORK_TIP fork.bin
run import X chain.bin 1 $TIP fork.bin $((FORK + 1)) $FORK_TIP | tee import_X.txt | tail -1
for n in L R F X; do state $n; done

echo "== activity check: the chain crossed the epoch boundaries and L's HEAT supply is non-zero"
grep -qE "^heatSupply=[1-9]" state_L.txt && grep -qE "^journals\..*epochSnaps=[1-9]" state_L.txt \
  && echo "activity PASS" || { FAILED=1; echo "activity FAIL"; grep -E "^heatSupply|^journals" state_L.txt; }
echo "== reorg check: X switched to the fork once"
grep -q "chain switches 1," import_X.txt && echo "switch PASS" || { FAILED=1; echo "switch FAIL"; cat import_X.txt; }
echo "== distinct chains: L and F differ"
diff state_L.txt state_F.txt > /dev/null && { FAILED=1; echo "distinct FAIL: L and F have the same state"; } || echo "distinct PASS"

same_state "C1 restart equivalence (L live vs R restarted at $RESTART)" L R
same_state "C2a live reorg (X) vs fork node (F)" F X
exit $FAILED
