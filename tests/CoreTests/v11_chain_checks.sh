#!/bin/bash
# Restart and reorg equivalence on a real testnet chain driven by v11_chain_harness.
# Each harness call is its own process, so a restart is a real one.
# Chain L (65 blocks, past the v11 upgrade at 30 and the epoch boundaries at 40, 50 and 60) is
# the reference. R replays L and restarts at block 47. F shares L up to block 39 and runs
# to block 80 on a different miner. Y replays L, restarts, and takes F's blocks 40 to 80, so
# the reorg pops three epoch boundaries from the rollback journals the cache must carry.
# The fork sits on the block before a boundary on purpose: the per-block TWAP journal is not
# cached, so a restarted node cannot undo the TWAP of non-boundary blocks, and only the
# boundary block's snapshot restore corrects it.
# C3 builds an alternative chain long enough to force m_alternative_chains to rehash while
# it grows: main chain M to block 95 and then 125, alternative chain A from block 40 to 110.
# A node refuses alternative blocks more than CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW (60) below its
# tip, so A's first block sits 55 below M's tip and every later switch stays inside that window.
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

TIP=65; RESTART=47; FORK=39; FORK_TIP=80
run mine L $TIP 1 0 | tail -1
run export L 1 $TIP chain.bin
run import R chain.bin 1 $RESTART | tail -1
run import R chain.bin $((RESTART + 1)) $TIP | tail -1
run import F chain.bin 1 $FORK | tail -1
run mine F $FORK_TIP 2 3 | tail -1
run export F $((FORK + 1)) $FORK_TIP fork.bin
run import X chain.bin 1 $TIP fork.bin $((FORK + 1)) $FORK_TIP | tee import_X.txt | tail -1
run import Y chain.bin 1 $TIP | tail -1
run import Y fork.bin $((FORK + 1)) $FORK_TIP | tee import_Y.txt | tail -1
for n in L R F X Y; do state $n; done

echo "== activity check: the chain crossed epoch boundaries with the genesis-seeded pool"
grep -qE "^pool\.reserveXfg=[1-9]" state_L.txt && grep -qE "^journals\..*epochSnaps=[1-9]" state_L.txt \
  && echo "activity PASS" || { FAILED=1; echo "activity FAIL"; grep -E "^pool|^journals" state_L.txt; }
echo "== reorg check: X switched to the fork once"
grep -q "chain switches 1," import_X.txt && echo "switch PASS" || { FAILED=1; echo "switch FAIL"; cat import_X.txt; }
echo "== distinct chains: L and F differ"
diff state_L.txt state_F.txt > /dev/null && { FAILED=1; echo "distinct FAIL: L and F have the same state"; } || echo "distinct PASS"

same_state "C1 restart equivalence (L live vs R restarted at $RESTART)" L R
same_state "C2a live reorg (X) vs fork node (F)" F X
grep -q "chain switches 1," import_Y.txt && echo "C2b switch PASS" || { FAILED=1; echo "C2b switch FAIL"; cat import_Y.txt; }
same_state "C2b reorg across epoch boundaries 40, 50 and 60 after a restart (Y) vs fork node (F)" F Y

echo "== C3 alternative chain: switch to A, switch back to M, state equals a node that saw only M"
MAIN_TIP=95; ALT_TIP=110; WIN_TIP=125; ALT_FORK=40
run mine M $WIN_TIP 1 0 | tail -1
run export M 1 $WIN_TIP main.bin
run import A main.bin 1 $ALT_FORK | tail -1
run mine A $ALT_TIP 2 3 | tail -1
run export A $((ALT_FORK + 1)) $ALT_TIP alt.bin
run import S main.bin 1 $MAIN_TIP alt.bin $((ALT_FORK + 1)) $ALT_TIP main.bin $((MAIN_TIP + 1)) $WIN_TIP | tee import_S.txt | tail -1
run import W main.bin 1 $WIN_TIP | tail -1
for n in S W; do state $n; done
grep -q "chain switches 2," import_S.txt && echo "C3 switches PASS" || { FAILED=1; echo "C3 switches FAIL"; cat import_S.txt; }
ENTRIES=$(sed -n 's/.*peak alternative chain entries \([0-9]*\).*/\1/p' import_S.txt)
[ "${ENTRIES:-0}" -ge 60 ] && echo "C3 rehash volume PASS (peak $ENTRIES alternative entries)" || { FAILED=1; echo "C3 rehash volume FAIL ($ENTRIES)"; }
same_state "C3 state after two switches (S) vs winning chain only (W)" W S
exit $FAILED
