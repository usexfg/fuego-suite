# Commitment Types Explained

## What Is a Commitment Type?

A commitment type is a **label that tells the circuit what kind of value a commitment represents**. It's metadata that prevents mixing up different kinds of commitments.

```
Think of it like currency denominations:

  A $100 bill and a $100 bond both represent "100 units of value"
  but you can't spend a bond at a grocery store.

  Similarly:
    C(1000 XFG as reserve) ≠ C(1000 XFG as LP shares)
    C(500 XFG as swap input) ≠ C(500 XFG as fee)

  The commitment value might be the same number,
  but what it MEANS is different.
```

## Why You Need Types

Without types, a malicious prover could:

```
ATTACK: Type Confusion

  Legitimate state:
    C_reserveA = Commit(10,000 XFG)
    C_lp_shares = Commit(7,071 shares)

  Malicious prover swaps them:
    C_reserveA = Commit(7,071 XFG)    ← wrong type, same commitment structure
    C_lp_shares = Commit(10,000 shares) ← wrong type

  Without type checking, the circuit would accept this.
  The prover just "moved" 2,929 XFG out of the pool.
```

## Common Commitment Types in an LP Pool

```
┌─────────────────────────────────────────────────────────────────────┐
│                    COMMITMENT TYPES                                 │
│                                                                     │
│  ┌──────────────────┬────────────────────────────────────────────┐  │
│  │  Type            │  What It Commits To                        │  │
│  ├──────────────────┼────────────────────────────────────────────┤  │
│  │  RESERVE_A       │  Pool's balance of token A                 │  │
│  │  RESERVE_B       │  Pool's balance of token B                 │  │
│  │  LP_SHARE        │  Total LP shares outstanding               │  │
│  │  LP_BALANCE      │  Individual LP's share balance             │  │
│  │  SWAP_INPUT      │  Amount user put into swap                 │  │
│  │  SWAP_OUTPUT     │  Amount user got from swap                 │  │
│  │  FEE             │  Fee collected from operation              │  │
│  │  FEE_ACCUMULATOR │  Total unclaimed fees                      │  │
│  │  FEE_CLAIM       │  Individual LP's claimable fees            │  │
│  │  NONCE           │  Random value for re-randomization         │  │
│  └──────────────────┴────────────────────────────────────────────┘  │
│                                                                     │
│  Each type has its own generator point H_type:                      │
│    C = r*G + value*H_type                                           │
│                                                                     │
│  Because H_type is different for each type, you CANNOT:             │
│    - Convert a RESERVE_A commitment to an LP_SHARE commitment       │
│    - Add a SWAP_INPUT to a RESERVE_B (different H, math fails)      │
│    - Claim fees from a fee accumulator without proper type          │
└─────────────────────────────────────────────────────────────────────┘
```

## How Types Work Mathematically

```
Standard Pedersen commitment:
  C = r*G + v*H

  Where:
    r = random blinding factor
    v = value being committed
    G = base point (fixed)
    H = hash-to-point (fixed)

With types, each type gets its own H:
  C_reserve = r*G + v*H_reserve
  C_lp_share = r*G + v*H_lp_share
  C_fee = r*G + v*H_fee

  H_reserve = HashToPoint("RESERVE")
  H_lp_share = HashToPoint("LP_SHARE")
  H_fee = HashToPoint("FEE")

  These H values are derived from domain-separated hashes.
  They are public, fixed, and known to everyone.

  Because H_reserve ≠ H_lp_share ≠ H_fee:
    - You can't convert between types
    - You can't add commitments of different types
    - The circuit enforces type consistency
```

## Type Enforcement in the Circuit

```
The circuit checks types at every operation:

  DEPOSIT:
    assert C_input_A.type == RESERVE_A
    assert C_input_B.type == RESERVE_B
    assert C_lp_minted.type == LP_SHARE
    C_reserve_A += C_input_A      // ✓ same type
    C_reserve_B += C_input_B      // ✓ same type
    C_lp_total += C_lp_minted     // ✓ same type

  SWAP:
    assert C_swap_in.type == RESERVE_A  // or RESERVE_B
    assert C_swap_out.type == RESERVE_B // or RESERVE_A
    assert C_fee.type == FEE
    C_reserve_A += C_swap_in     // ✓ same type
    C_reserve_B -= C_swap_out    // ✓ same type
    C_fee_accum += C_fee         // ✓ same type

  WITHDRAWAL:
    assert C_lp_burned.type == LP_SHARE
    assert C_output_A.type == RESERVE_A
    assert C_output_B.type == RESERVE_B
    C_lp_total -= C_lp_burned    // ✓ same type
    C_reserve_A -= C_output_A    // ✓ same type
    C_reserve_B -= C_output_B    // ✓ same type

  If any type mismatch: circuit fails, proof is invalid.
```

## In Our Existing Codebase

The fuego-prover already uses commitment types:

```rust
// From fuego-core/src/lib.rs (current):
pub enum CommitmentType {
    Heat = 0,           // For HEAT burn commitments
    Cold = 1,           // For COLD burn commitments
    ElderfierStaking = 2, // Being removed per your note
}

// For the LP pool, we'd add:
pub enum CommitmentType {
    Heat = 0,
    Cold = 1,
    // ElderfierStaking removed
    LpReserveA = 2,
    LpReserveB = 3,
    LpShare = 4,
    LpFee = 5,
    LpFeeAccumulator = 6,
}
```

## What Types Prevent

| Attack | Without Types | With Types |
|---|---|---|
| Swap reserve for LP shares | ✓ Possible | ✗ Blocked |
| Claim fees as swap output | ✓ Possible | ✗ Blocked |
| Mix token A reserve with token B | ✓ Possible | ✗ Blocked |
| Replay a fee commitment as deposit | ✓ Possible | ✗ Blocked |
| Steal by type confusion | ✓ Possible | ✗ Blocked |

## Summary

Commitment types are **not a separate cryptographic primitive** — they're just using different generator points (H values) for different kinds of values. The type is baked into the commitment itself through the choice of H, and the circuit enforces that commitments are only combined with other commitments of the same type.
