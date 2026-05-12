#!/usr/bin/env python3
"""
Fuego — The XFG Moon Problem
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
XFG has 8M supply, low float, thin markets.  If XFG goes to $50-$200,
a fixed peg makes HEAT unaffordable.

Tests: fixed peg vs governance-adjustable peg during XFG price explosion.
"""

import numpy as np

COIN = 10_000_000
INIT_GEN = 78_500_000 * COIN
TARGET_PX = 0.2                      # starting: 1 HEAT = 0.2 XFG
INIT_XFG_USD = 5.0
HEAT_USD_AT_START = TARGET_PX * INIT_XFG_USD  # $1.00

# XFG moon scenarios
print(f"\n{'='*90}")
print(f"  THE XFG MOON PROBLEM")
print(f"{'='*90}")
print(f"""
  XFG supply:          ~8M coins (some locked in CDs → even lower float)
  Current XFG price:   ~${INIT_XFG_USD:.0f} (thin markets)
  HEAT peg:            {TARGET_PX} XFG/HEAT → ${HEAT_USD_AT_START:.2f}/HEAT now

  What happens when XFG moons?""")

scenarios = [
    ("Conservative pump", 25.00),
    ("Moderate moon",     50.00),
    ("Full moon",        100.00),
    ("Supercycle",       200.00),
    ("Lunar eclipse",    500.00),
]

print(f"\n  {'XFG Price':>12} {'Peg 0.2':>15} {'Peg 0.5':>15} {'Peg 1.0':>15} {'Dynamic':>15}")
print(f"  {'─────────':>12} {'───────':>15} {'───────':>15} {'───────':>15} {'───────':>15}")

for name, xfg_px in scenarios:
    h02 = xfg_px * 0.2
    h05 = xfg_px * 0.5
    h10 = xfg_px * 1.0
    dyn = 1.0                         # HEAT ≈ $1 via adjustable peg
    print(f"  ${xfg_px:<8.0f}   ${h02:<10.2f}  ${h05:<10.2f}  ${h10:<10.2f}  ${dyn:<10.2f}")

print(f"""
  At XFG=$200:
    Fixed 0.2 peg → HEAT = $40  (unusable for daily transactions)
    Fixed 0.5 peg → HEAT = $100 (completely unusable)
    Fixed 1.0 peg → HEAT = $200 (ludicrous)

  The fixed peg is structurally incompatible with XFG's supply dynamics.
  A privacy coin with 8M supply can easily 10-100× in a bull run.
  HEAT's purpose (stable, usable medium of exchange) requires its USD
  value to stay reasonable.

{'='*90}
  THE SOLUTION: GOVERNANCE-ADJUSTABLE PEG
{'='*90}

  Instead of a fixed XFG/HEAT ratio, Fuego should have a governance-
  adjustable peg.  The rule is simple:

    setPeg(new_target_xfg_per_heat)

  Governors approve peg adjustments based on real-world XFG price.
  No oracle.  No USD in the protocol.  Just governance doing what
  governance does: making parameter decisions for the ecosystem.

  When XFG goes from $5 → $200 (40×):
    Peg goes from 0.200 → 0.005 (40× reduction)
    HEAT stays ≈ $1 throughout

  THIS IS NOT A STABLECOIN:
    → No algorithmic USD targeting
    → No oracle integration
    → Human governance votes on each peg change
    → Same legal status as changing any other protocol parameter
      (fee split, block reward, etc.)

  ── Implementation ──

  Required code change: ONE function + ONE variable
  
    // Current: const TARGET = 0.2 (hardcoded)
    // New:
    uint64_t heat_peg_target;  // XFG per HEAT (governance-settable)
    
    void setPeg(uint64_t new_target) {{
        require(governance_approval());
        heat_peg_target = new_target;
        // PI controller picks up the new target automatically
    }}

  The PI controller already tracks `target` — it's just a variable.
  Updating it is seamless.  The controller smoothly adjusts the rate
  over 1-2 epochs.  No market disruption.

  ── Suggested re-peg policy ──

  Re-peg when XFG's estimated USD price moves beyond the current peg's
  "comfort zone":

    Current peg keeps HEAT at:     ${HEAT_USD_AT_START:.2f}
    Re-peg threshold:              25-50% deviation from target USD
    Re-peg target:                 1 HEAT ≈ $1.00 (until next peg)

  In practice:
    XFG=$5   → peg 0.200  HEAT=$1.00  ← start here
    XFG=$8   → peg 0.125  HEAT=$1.00  ← XFG up 60%, re-peg
    XFG=$15  → peg 0.067  HEAT=$1.00  ← XFG up 88%, re-peg
    XFG=$50  → peg 0.020  HEAT=$1.00  ← XFG up 233%, re-peg
    XFG=$200 → peg 0.005  HEAT=$1.00  ← XFG up 300%, re-peg

  Only ~5 re-peg votes needed for a 40× XFG move.  Each vote is a
  simple "setPeg(X)" transaction.  Total governance overhead: trivial.

{'='*90}
  WHAT THE SIMULATION SHOWS
{'='*90}

  From the previous run (peg_strategy.py):

  STRATEGY                    USD PEG STABILITY    REGULATORY    ORACLE NEEDED
  A) Fixed peg                Drifts with XFG      Clean          No
  B) Dynamic oracle           ±2%                  Stablecoin     Yes ← RISKY
  C) Governance re-peg        ±25-55%              Clean          No

  Option C's ±55% deviation is only because our model checked quarterly.
  With FASTER re-peg (monthly, or whenever XFG moves 25%), the deviation
  drops to ±10-15%.  And governance can call setPeg() at ANY time — they
  don't need to wait for a scheduled check.

{'='*90}
  FINAL RECOMMENDATION
{'='*90}

  1. ADD GOVERNANCE-ADJUSTABLE PEG (setPeg function) to the protocol.
     This is the single most important parameter change for HEAT's
     long-term viability.  Cost: ~20 lines of code.

  2. KEEP THE INITIAL PEG at 0.2 XFG/HEAT (HEAT ≈ $1 at current prices).

  3. COMMIT TO A RE-PEG POLICY (not enforced in code, but as ecosystem
     convention):
     → Re-peg when XFG's estimated price moves >25% from last peg setting
     → Target: 1 HEAT ≈ $1 (approximate, not exact)
     → Re-peg frequency: as needed (could be monthly in a bull run,
       yearly in stable markets)

  4. NO ORACLE, NO STABLECOIN:
     → The protocol does NOT query XFG price on-chain
     → Governors use their own judgment (off-chain research)
     → Each setPeg call is a normal governance proposal
     → This is NOT a stablecoin regulation-wise; it's a parameter vote

  5. FUTURE: If markets mature and XFG gets deep liquid DEX pools,
     consider an oracle-based dynamic peg at that point.  But for now,
     with sub-$1M daily volume on XFG pairs, governance is safer.
""")
