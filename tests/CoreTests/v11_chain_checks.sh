#!/bin/bash
# Restart and reorg equivalence on a real testnet chain driven by v11_chain_harness.
# Usage: v11_chain_checks.sh <path-to-v11_chain_harness> <empty-workdir>
set -u
BIN=$(realpath "$1"); W=$2
if [ -e "$W" ] && [ -n "$(ls -A "$W")" ]; then echo "workdir must be empty: $W"; exit 2; fi
mkdir -p "$W"; cd "$W"
FAILED=0
q() { "$BIN" "$@" 2>&1 | grep -v SwappedVector; }
st() { "$BIN" state "$1" 2>&1 | grep -v SwappedVector; }
q mine L 240 1 0 | tail -1
q export L 1 240 chain.bin
q import R chain.bin 1 200 >/dev/null; q import R chain.bin 201 240 >/dev/null
q import F chain.bin 1 210 >/dev/null; q mine F 250 2 3 >/dev/null; q export F 211 250 fork.bin
q import2 X chain.bin 1 240 fork.bin 211 250 >/dev/null
q import Y chain.bin 1 240 >/dev/null; q import Y fork.bin 211 250
for n in L R F X Y; do st $n | grep -v cacheBytes > state_$n.txt; done
echo "== C1 restart equivalence (L live vs R restarted at 200)"
diff state_L.txt state_R.txt > /dev/null && echo "C1 PASS: identical consensus state" || { FAILED=1; echo "C1 FAIL"; diff state_L.txt state_R.txt; }
echo "== C2a live reorg (X) vs fork node (F)"
diff state_F.txt state_X.txt > /dev/null && echo "C2a PASS" || { FAILED=1; echo "C2a FAIL"; diff state_F.txt state_X.txt; }
echo "== C2b reorg after restart (Y) vs fork node (F)"
diff state_F.txt state_Y.txt > /dev/null && echo "C2b PASS" || { FAILED=1; echo "C2b FAIL"; diff state_F.txt state_Y.txt; }
exit $FAILED
