#!/bin/bash
# Restart and reorg equivalence on a real testnet chain driven by v11_chain_harness.
# Each harness call is its own process, so a restart is a real one. A "live" state is printed by
# the process that imported the blocks; a "reloaded" state by a fresh process that read the cache.
#
# Chain L (65 blocks, past the v11 upgrade at 30 and the epoch boundaries at 40, 50 and 60) is
# the reference. R replays L and restarts at block 47. F shares L up to block 39 and runs to
# block 80 on a different miner.
#   C1  L and R, reloaded: a restart in the middle changes nothing.
#   C2a X imports L then F's blocks 40 to 80 in one process; reloaded, it equals F.
#   C2b Y imports L, restarts, and takes F's blocks 40 to 80; live, it equals X live. The reorg
#       pops three epoch boundaries from rollback journals the cache must carry. The fork sits
#       on the block before a boundary on purpose: the per-block TWAP journal is not cached, so
#       a restarted node cannot undo the TWAP of non-boundary blocks, and only the boundary
#       block's snapshot restore corrects it. For the same reason C2b leaves the TWAP journal's
#       size and digest out of the comparison.
#   C3  m_alternative_chains rehashes while an alternative chain fills it, and the iterators
#       handle_alternative_block holds across the insert go stale. Main chain M runs to block 95;
#       five competing chains A1..A5 fork from block 40, each one block longer than the last, so
#       the node switches five times; M then grows to block 125 and wins back. The final state
#       must equal a node that only saw M. A node refuses alternative blocks more than
#       CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW (60) below its tip, which caps the depth of the forks.
# A harness process that exits non-zero (a sanitizer abort, a rejected block) fails the run, and
# the script goes on, so one broken check does not hide the others.
# Usage: v11_chain_checks.sh <path-to-v11_chain_harness> <empty-workdir>
set -u
BIN=$(realpath "$1"); W=$2
if [ -e "$W" ] && [ -n "$(ls -A "$W")" ]; then echo "workdir must be empty: $W"; exit 2; fi
mkdir -p "$W"; cd "$W"
FAILED=0

step() {
  "$BIN" "$@" > step.out 2>&1 || { FAILED=1; echo "harness failed: $*"; tail -40 step.out; return 0; }
  tail -1 step.out
}
capture() {
  local name=$1; shift
  : > "raw_$name.txt"; : > "report_$name.txt"
  "$BIN" state "$@" > "raw_$name.txt" 2> "report_$name.txt" || { FAILED=1; echo "harness failed: state $*"; tail -40 "report_$name.txt"; }
  grep -Ev '^[0-9]{4}-[A-Z][a-z]{2}-[0-9]{2} |^SwappedVector ' "raw_$name.txt" > "state_$name.txt"
}
same_state() {
  local name=$1 expected=$2 actual=$3 ignore=${4:-'^$'}
  if ! grep -q '^height=' "state_$expected.txt" || ! grep -q '^height=' "state_$actual.txt"; then
    FAILED=1; echo "$name FAIL: a state is missing"
  elif diff <(grep -Ev "$ignore" "state_$expected.txt") <(grep -Ev "$ignore" "state_$actual.txt") > /dev/null; then echo "$name PASS"
  else FAILED=1; echo "$name FAIL"; diff <(grep -Ev "$ignore" "state_$expected.txt") <(grep -Ev "$ignore" "state_$actual.txt"); fi
}
report_has() {
  local name=$1 pattern=$2
  if grep -q "$pattern" "report_$name.txt"; then echo "$name PASS"
  else FAILED=1; echo "$name FAIL: expected '$pattern'"; cat "report_$name.txt"; fi
}

TIP=65; RESTART=47; FORK=39; FORK_TIP=80
step mine L $TIP 1 0
step export L 1 $TIP chain.bin
step import R chain.bin 1 $RESTART
step import R chain.bin $((RESTART + 1)) $TIP
step import F chain.bin 1 $FORK
step mine F $FORK_TIP 2 3
step export F $((FORK + 1)) $FORK_TIP fork.bin
capture Xlive X chain.bin 1 $TIP fork.bin $((FORK + 1)) $FORK_TIP
step import Y chain.bin 1 $TIP
capture Ylive Y fork.bin $((FORK + 1)) $FORK_TIP
capture L L
capture R R
capture F F
capture X X

echo "== activity: the chain crossed epoch boundaries with the genesis-seeded pool"
grep -qE "^pool\.reserveXfg=[1-9]" state_L.txt && grep -qE "^journals\..*epochSnaps=[1-9]" state_L.txt \
  && echo "activity PASS" || { FAILED=1; echo "activity FAIL"; grep -E "^pool|^journals" state_L.txt; }
echo "== distinct chains: L and F differ"
diff state_L.txt state_F.txt > /dev/null && { FAILED=1; echo "distinct FAIL: L and F have the same state"; } || echo "distinct PASS"
report_has Xlive "chain switches 1,"
same_state "C1 restart equivalence (L vs R restarted at $RESTART)" L R
same_state "C2a live reorg (X reloaded) vs fork node (F)" F X
report_has Ylive "chain switches 1,"
same_state "C2b reorg after a restart (Y live) vs no restart (X live)" Xlive Ylive '^journals\.|^digest\.twapJournal='


echo "== C3 alternative chains: five switches, then back to M; state equals a node that saw only M"
MAIN_TIP=95; ALT_FORK=40; WIN_TIP=125; ALT_COUNT=5
step mine M $WIN_TIP 1 0
step export M 1 $WIN_TIP main.bin
SEGMENTS="main.bin 1 $MAIN_TIP"
for k in $(seq 1 $ALT_COUNT); do
  ALT_TIP=$((MAIN_TIP + k))
  step import A$k main.bin 1 $ALT_FORK
  step mine A$k $ALT_TIP 2 $((k + 2))
  step export A$k $((ALT_FORK + 1)) $ALT_TIP alt$k.bin
  SEGMENTS="$SEGMENTS alt$k.bin $((ALT_FORK + 1)) $ALT_TIP"
done
capture Slive S $SEGMENTS main.bin $((MAIN_TIP + 1)) $WIN_TIP
capture Wlive W main.bin 1 $WIN_TIP
report_has Slive "chain switches $((ALT_COUNT + 1)),"
PEAK=$(sed -n 's/.*peak alternative chain entries \([0-9]*\).*/\1/p' report_Slive.txt)
[ "${PEAK:-0}" -ge 60 ] && echo "C3 rehash volume PASS (peak $PEAK alternative entries)" || { FAILED=1; echo "C3 rehash volume FAIL ($PEAK)"; }
same_state "C3 state after the switches (S live) vs the winning chain only (W live)" Wlive Slive
exit $FAILED
