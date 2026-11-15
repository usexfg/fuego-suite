#!/bin/bash
# Restart and reorg equivalence on a real testnet chain driven by v11_chain_harness.
# Chain L runs the production wallet's seedboot: a HEAT mint at the launch
# ratio, the first LP deposit and an LP removal. The fork point is the block
# before the first deposit, so F carries L's transactions at the same heights
# (a node refuses a reorg that drops one). The mint stays below the fork: its
# ring decoys reference no later block, but the deposit's XFG ring would
# reference L's blocks after a fork placed before the mint, and those cannot
# exist on F. C1 restarts between the deposit and the removal; C2 reorgs the
# deposit, the removal and the AuntPoL epoch work away and replays F's. The L
# tip stays within the 60-block reorg window (CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW).
# Usage: v11_chain_checks.sh <path-to-v11_chain_harness> <empty-workdir>
set -u
BIN=$(realpath "$1"); W=$2
if [ -e "$W" ] && [ -n "$(ls -A "$W")" ]; then echo "workdir must be empty: $W"; exit 2; fi
mkdir -p "$W"; cd "$W"
FAILED=0
q() { "$BIN" "$@" 2>&1 | grep -v SwappedVector; }
st() { "$BIN" state "$1" 2>&1 | grep -v SwappedVector; }
q mine L 210 1 0 | tail -1
echo "== wallet activity on L"
q seedboot L 211 > seedboot_L.txt
grep -q "SEEDBOOT PASS" seedboot_L.txt && echo "seedboot PASS" || { FAILED=1; echo "seedboot FAIL"; grep FAIL seedboot_L.txt; }
ADD_H=$(sed -n 's/.*LP add record confirmed at its block: \([0-9]*\).*/\1/p' seedboot_L.txt)
[ -n "$ADD_H" ] && [ "$ADD_H" -lt 1000000 ] || { echo "no confirmed LP add height in seedboot output"; exit 1; }
FORK=$((ADD_H - 1)); RESTART=$((ADD_H + 2)); TIP=$((FORK + 45)); FORK_TIP=$((TIP + 15))
echo "LP add at $ADD_H, fork $FORK, restart $RESTART, tip $TIP, fork tip $FORK_TIP"
q mine L $TIP 1 0 | tail -1
q export L 1 $TIP chain.bin
q import R chain.bin 1 $RESTART >/dev/null; q import R chain.bin $((RESTART + 1)) $TIP >/dev/null
q import F chain.bin 1 $FORK >/dev/null; q mine F $FORK_TIP 2 3 chain.bin; q export F $((FORK + 1)) $FORK_TIP fork.bin
q import2 X chain.bin 1 $TIP fork.bin $((FORK + 1)) $FORK_TIP >/dev/null
q import Y chain.bin 1 $TIP >/dev/null; q import Y fork.bin $((FORK + 1)) $FORK_TIP
for n in L R F X Y; do st $n | grep -v cacheBytes > state_$n.txt; done
echo "== activity check: L's pool and HEAT supply are non-empty"
grep -qE "^heatSupply=[1-9]" state_L.txt && echo "activity PASS" || { FAILED=1; echo "activity FAIL: L carries no HEAT"; }
echo "== C1 restart equivalence (L live vs R restarted at $RESTART)"
diff state_L.txt state_R.txt > /dev/null && echo "C1 PASS: identical consensus state" || { FAILED=1; echo "C1 FAIL"; diff state_L.txt state_R.txt; }
echo "== C2a live reorg (X) vs fork node (F)"
diff state_F.txt state_X.txt > /dev/null && echo "C2a PASS" || { FAILED=1; echo "C2a FAIL"; diff state_F.txt state_X.txt; }
echo "== C2b reorg after restart (Y) vs fork node (F)"
diff state_F.txt state_Y.txt > /dev/null && echo "C2b PASS" || { FAILED=1; echo "C2b FAIL"; diff state_F.txt state_Y.txt; }
exit $FAILED
