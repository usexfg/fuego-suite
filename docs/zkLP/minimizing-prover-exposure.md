# Minimizing Prover Exposure in LP Swap Pool

## The Core Problem

In a zkVM like SP1, the prover runs the entire program. They see:
- All inputs
- All intermediate computations  
- All outputs

If the program takes plaintext amounts, the prover sees everything.

**Goal:** Give the prover the MINIMUM information needed to generate a valid proof.

---

## Approach 1: Commitment-Based Proving (Recommended)

Instead of giving the prover plaintext values, give them **Pedersen commitments**.

```
┌─────────────────────────────────────────────────────────────────────┐
│                    PLAINTEXT PROVING (BAD)                          │
│                                                                     │
│  Prover sees:                                                       │
│    reserveA_before = 10,000 XFG                                     │
│    reserveB_before = 5,000 BTC                                      │
│    swap_input = 500 XFG                                             │
│    swap_output = 245 BTC                                            │
│    fee = 1.5 XFG                                                    │
│    reserveA_after = 10,498.5 XFG                                    │
│    reserveB_after = 4,755 BTC                                       │
│                                                                     │
│  Prover learns: exact amounts, pool depth, trade size, fees         │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                    COMMITMENT PROVING (GOOD)                        │
│                                                                     │
│  Prover sees:                                                       │
│    C_reserveA_before = 0x7a3f... (commitment to unknown value)      │
│    C_reserveB_before = 0x9b2e... (commitment to unknown value)      │
│    C_swap_input = 0x4c8d... (commitment to unknown value)           │
│    C_swap_output = 0x1e5a... (commitment to unknown value)          │
│    C_fee = 0x6f1b... (commitment to unknown value)                  │
│    C_reserveA_after = 0x3d7c... (commitment to unknown value)       │
│    C_reserveB_after = 0x8a4e... (commitment to unknown value)       │
│                                                                     │
│  Prover learns: commitments are consistent                          │
│  Prover CANNOT learn: actual amounts (discrete log is hard)         │
└─────────────────────────────────────────────────────────────────────┘
```

### How It Works

```
1. Pool state is stored as Pedersen commitments:
   C_reserveA = r*G + reserveA*H
   C_reserveB = r*G + reserveB*H
   (where r is a random blinding factor, G and H are generators)

2. Users submit swaps as commitments:
   C_input = r_input*G + inputAmount*H
   C_output = r_output*G + outputAmount*H

3. The prover processes commitments (not plaintext):
   C_reserveA_after = C_reserveA_before + C_input - C_fee
   C_reserveB_after = C_reserveB_before - C_output

4. The circuit proves:
   - Commitment arithmetic is correct
   - AMM invariant holds (using commitment-based verification)
   - All values are positive (range proofs)
   - Fee calculation is correct

5. The prover sees ONLY commitment values, not underlying amounts.
```

### What the Prover Still Learns

```
Even with commitments, the prover can observe:

✗ Same commitment appearing across epochs → same value
  (e.g., if C_fee is the same in two epochs, fee was the same)

✗ Relative sizes (if they know the commitment scheme parameters)
  (e.g., C_large > C_small in some commitment schemes)

✗ Correlation patterns
  (e.g., "this commitment always changes when that one does")

Mitigation: Per-epoch re-randomization of all commitments.
Each epoch, add a fresh blinding factor to every commitment:
  C' = C + r_new*G
This makes commitments unlinkable across epochs.
```

---

## Approach 2: Range Proofs Instead of Exact Values

Instead of revealing exact amounts, prove they're within valid bounds.

```
┌─────────────────────────────────────────────────────────────────────┐
│                    RANGE PROOF PROVING                              │
│                                                                     │
│  Prover sees:                                                       │
│    "reserveA is between 1,000 and 100,000"                          │
│    "swap_input is between 1 and 10,000"                             │
│    "fee is between 0.001 and 100"                                   │
│    "AMM invariant held"                                             │
│                                                                     │
│  Prover learns: bounds, but not exact values                        │
│  Prover CANNOT learn: exact amounts                                 │
│                                                                     │
│  Tradeoff: Wider bounds = more privacy, less precision              │
│            Narrower bounds = less privacy, more precision           │
└─────────────────────────────────────────────────────────────────────┘
```

This can be combined with commitments: the prover sees commitments AND range proofs that the committed values are within valid bounds.

---

## Approach 3: Oblivious State Updates

The prover doesn't know the actual pool state at all.

```
┌─────────────────────────────────────────────────────────────────────┐
│                    OBLIVIOUS PROVING                                │
│                                                                     │
│  Setup:                                                             │
│    - Pool state is encrypted to a key the prover doesn't have       │
│    - Users generate sub-proofs about their own swaps                │
│    - Sub-proof: "my swap is valid relative to SOME state"           │
│                                                                     │
│  Proving:                                                           │
│    - Prover collects sub-proofs                                     │
│    - Prover verifies each sub-proof                                 │
│    - Prover aggregates into single proof                            │
│    - Prover NEVER sees actual state or amounts                      │
│                                                                     │
│  Tradeoff: Complex, requires sophisticated ZK circuits              │
│            Each user must generate a sub-proof                      │
│            Sequential dependency (each sub-proof depends on last)   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Approach 4: Split Proving (Information Compartmentalization)

Different provers see different parts of the computation.

```
┌─────────────────────────────────────────────────────────────────────┐
│                    SPLIT PROVING                                    │
│                                                                     │
│  Prover A sees:                                                     │
│    - ReserveA commitments                                           │
│    - Swap input commitments                                         │
│    - Fee commitments                                                │
│    - Proves: "ReserveA update is valid"                             │
│                                                                     │
│  Prover B sees:                                                     │
│    - ReserveB commitments                                           │
│    - Swap output commitments                                        │
│    - Proves: "ReserveB update is valid"                             │
│                                                                     │
│  Prover C sees:                                                     │
│    - LP share commitments                                           │
│    - Proves: "LP share math is valid"                               │
│                                                                     │
│  Aggregate proof combines all three.                                │
│  No single prover sees the full picture.                            │
│                                                                     │
│  Tradeoff: Requires multiple provers, coordination overhead         │
│            Each prover still sees their slice of data               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Recommended: Commitment-Based + Per-Epoch Re-randomization

This is the most practical approach with current SP1 technology:

```
┌─────────────────────────────────────────────────────────────────────┐
│                    RECOMMENDED ARCHITECTURE                          │
│                                                                     │
│  Pool State (on-chain):                                             │
│    C_reserveA = r*G + reserveA*H                                    │
│    C_reserveB = r*G + reserveB*H                                    │
│    C_lp_shares = r*G + totalShares*H                                │
│    C_fees = r*G + feeAccumulator*H                                  │
│                                                                     │
│  User submits swap:                                                 │
│    C_input = r_input*G + inputAmount*H                              │
│    C_output = r_output*G + outputAmount*H                           │
│    + ZK sub-proof: "my swap is valid"                               │
│                                                                     │
│  Epoch boundary:                                                    │
│    1. Prover fetches all commitments                                │
│    2. Prover runs circuit that operates on commitments              │
│    3. Circuit verifies:                                             │
│       - C_reserveA_after = C_reserveA_before + C_input - C_fee     │
│       - C_reserveB_after = C_reserveB_before - C_output            │
│       - AMM invariant holds (commitment-based verification)         │
│       - Range proofs: all values > 0                               │
│       - Fee calculation: C_fee = C_input * feeBps / 10000          │
│    4. Prover publishes aggregate proof                              │
│    5. All commitments are re-randomized for next epoch:             │
│       C' = C + r_new*G                                             │
│                                                                     │
│  What prover sees:                                                  │
│    ✓ Commitment values (opaque, can't extract amounts)              │
│    ✓ That commitments are consistent                                │
│    ✓ Range bounds (if using range proofs)                           │
│                                                                     │
│  What prover CANNOT see:                                            │
│    ✗ Actual reserve amounts                                         │
│    ✗ Actual swap amounts                                            │
│    ✗ Actual fee amounts                                             │
│    ✗ LP share balances                                              │
│    ✗ Any correlation across epochs (re-randomized)                  │
└─────────────────────────────────────────────────────────────────────┘
```

### What This Hides

| Data | Plaintext Proving | Commitment Proving |
|---|---|---|
| Reserve amounts | ✗ Visible | ✓ Hidden |
| Swap amounts | ✗ Visible | ✓ Hidden |
| Fee amounts | ✗ Visible | ✓ Hidden |
| LP share balances | ✗ Visible | ✓ Hidden |
| Trade direction | ✗ Visible | ✓ Hidden (if C_input/C_output are separate) |
| Pool depth | ✗ Visible | ✓ Hidden |
| Trading volume | ✗ Visible | ✓ Hidden |

### What This Still Leaks

| Data | Why |
|---|---|
| Number of events per epoch | Prover counts commitments |
| Event types | Prover sees which commitments changed |
| Relative timing | Prover sees block order |
| Bounds (if using range proofs) | Prover sees valid ranges |

---

## Implementation Complexity

| Approach | Complexity | Prover Exposure | Practical? |
|---|---|---|---|
| Plaintext | Low | Maximum | Yes |
| Commitments | Medium | Minimal | Yes (recommended) |
| Range proofs | Medium-High | Low | Yes |
| Oblivious | Very High | Near-zero | No (too complex) |
| Split proving | High | Partial | Maybe |
| FHE | Extreme | Zero | No (too slow) |

**Commitment-based proving** is the sweet spot: significantly reduces prover exposure while remaining implementable with current SP1 technology.
