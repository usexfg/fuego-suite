# Private LP Swap Pool with ZK Proofs

## Why ZK for the LP Pool?

Even though counterparty chains (ETH, SOL, BCH) are transparent, the **XFG-side must remain private**. Without ZK:

```
TRANSPARENT POOL (BAD):
  XFG chain shows: Alice(pubkey) → deposited 10,000 XFG → swapped 500 XFG → withdrew 10,150 XFG
  Observer learns: who trades, direction, amounts, profit, pool depth
  Result: front-running, targeted attacks, link analysis to bridge transactions
```

With ZK, the XFG chain only shows **proofs that everything is correct** — without revealing **what** happened.

---

## What the ZK Proof Proves

The proof does **two things simultaneously**:

```
┌─────────────────────────────────────────────────────────────────────┐
│                     WHAT THE ZK PROOF PROVES                         │
│                                                                     │
│  ┌─────────────────────────┐    ┌──────────────────────────────┐   │
│  │   CORRECTNESS (public)  │    │   PRIVACY (hidden)           │   │
│  │                         │    │                              │   │
│  │ ✓ AMM invariant held    │    │ ✗ Swap amounts               │   │
│  │   (x*y=k maintained)    │    │ ✗ Swap direction (A→B or B→A)│   │
│  │                         │    │ ✗ Trader identities           │   │
│  │ ✓ Fees calculated       │    │ ✗ LP deposit amounts          │   │
│  │   correctly per feeBps  │    │ ✗ LP withdrawal amounts       │   │
│  │                         │    │ ✗ Pool reserve levels         │   │
│  │ ✓ LP shares minted/     │    │ ✗ LP provider identities      │   │
│  │   burned correctly      │    │ ✗ Individual fee claims       │   │
│  │                         │    │ ✗ Number of swaps in epoch    │   │
│  │ ✓ Fee distribution      │    │                              │   │
│  │   to LPs is fair        │    │                              │   │
│  │                         │    │                              │   │
│  │ ✓ Merkle roots valid    │    │                              │   │
│  │   (LP share tree +      │    │                              │   │
│  │    fee record tree)     │    │                              │   │
│  └─────────────────────────┘    └──────────────────────────────┘   │
│                                                                     │
│  The proof is a single artifact that guarantees correctness         │
│  while revealing NOTHING about the underlying data.                 │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Why 5 Days (900 Blocks) Is Too Long

```
┌─────────────────────────────────────────────────────────────────────┐
│                    THE 5-DAY PROBLEM                                │
│                                                                     │
│  Day 0: Epoch starts, pool state is valid                          │
│                                                                     │
│  Day 1: Malicious operator manipulates reserves                    │
│         → Users can't tell (no proof yet)                          │
│                                                                     │
│  Day 2: Operator front-runs a large swap                           │
│         → Users can't tell (no proof yet)                          │
│                                                                     │
│  Day 3: LP tries to withdraw, gets wrong amount                    │
│         → No proof to dispute against                              │
│                                                                     │
│  Day 4: More manipulation, pool is significantly off               │
│         → Still no proof                                           │
│                                                                     │
│  Day 5: Epoch ends, proof generated — TOO LATE                     │
│         → Damage already done                                      │
│         → Funds already lost                                       │
│         → Can't retroactively fix                                  │
│                                                                     │
│  CONCLUSION: 5 days is an eternity in DeFi.                        │
│  Users need to be able to verify pool correctness                   │
│  within hours, not days.                                            │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Recommended: 100-Block Epochs (~13 Hours)

```
┌─────────────────────────────────────────────────────────────────────┐
│                    EPOCH SIZE TRADEOFFS                             │
│                                                                     │
│  ┌──────────────┬───────────────┬───────────────┬───────────────┐  │
│  │  Epoch Size  │  Time         │  Detection    │  Proving Cost │  │
│  └──────────────┼───────────────┼───────────────┼───────────────┘  │
│                 │               │               │                  │
│  10 blocks      │  ~80 min      │  Fast         │  High (frequent)│
│                 │               │               │  Low per epoch   │  │
│                 │               │               │                  │
│  50 blocks      │  ~6.5 hours   │  Good         │  Moderate       │
│                 │               │               │  Moderate        │  │
│                 │               │               │                  │
│  100 blocks     │  ~13 hours    │  Good         │  Balanced       │  │
│                 │               │               │  ← RECOMMENDED   │  │
│                 │               │               │                  │
│  500 blocks     │  ~2.7 days    │  Slow         │  Low (infrequent)│
│                 │               │               │  High per epoch  │  │
│                 │               │               │                  │
│  900 blocks     │  5 days       │  Too slow     │  Lowest          │
│                 │               │               │  Highest per epoch│  │
│                 │               │               │                  │
├─────────────────┴───────────────┴───────────────┴──────────────────┤
│                                                                     │
│  RECOMMENDED: 100 blocks (~13 hours)                               │
│                                                                     │
│  - Fraud detected within half a day                                 │
│  - Proving cost is amortized over enough txns to be efficient       │
│  - Fits naturally into daily workflow (proof twice per day)         │
│  - Small enough that proof generation doesn't bottleneck the pool   │
│  - Large enough that empty epochs are rare                          │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Architecture: How It Works

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     PRIVATE LP POOL ARCHITECTURE                        │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                        USER ACTIONS                                │ │
│  │                                                                   │ │
│  │   LP Deposit          Swap              LP Withdrawal             │ │
│  │   ┌─────────┐        ┌─────────┐        ┌─────────┐              │ │
│  │   │ Encrypt  │        │ Encrypt  │        │ Encrypt  │             │ │
│  │   │ amounts  │        │ amounts  │        │ amounts  │             │ │
│  │   │ + submit │        │ + submit │        │ + submit │             │ │
│  │   └────┬────┘        └────┬────┘        └────┬────┘              │ │
│  │        │                  │                  │                     │ │
│  └────────┼──────────────────┼──────────────────┼─────────────────────┘ │
│           │                  │                  │                        │
│           ▼                  ▼                  ▼                        │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                    POOL ORGANIZER (daemon)                         │ │
│  │                                                                   │ │
│  │  ┌─────────────────────────────────────────────────────────────┐ │ │
│  │  │  Event Buffer (accumulates encrypted events per epoch)      │ │ │
│  │  │                                                             │ │ │
│  │  │  Block 100:  [encrypted deposit]                            │ │ │
│  │  │  Block 105:  [encrypted swap]                               │ │ │
│  │  │  Block 112:  [encrypted swap]                               │ │ │
│  │  │  Block 140:  [encrypted withdrawal]                         │ │ │
│  │  │  ...                                                        │ │ │
│  │  │  Block 199:  [encrypted swap]                               │ │ │
│  │  └─────────────────────────────────────────────────────────────┘ │ │
│  │                           │                                       │ │
│  │                           ▼                                       │ │
│  │  ┌─────────────────────────────────────────────────────────────┐ │ │
│  │  │  Epoch Boundary (every 100 blocks)                          │ │ │
│  │  │                                                             │ │ │
│  │  │  1. Decrypt events (organizer has keys)                     │ │ │
│  │  │  2. Process all events in order                             │ │ │
│  │  │  3. Update pool state (reserves, shares, fees)              │ │ │
│  │  │  4. Build Merkle trees (LP shares + fee records)            │ │ │
│  │  │  5. Generate ZK proof via SP1                               │ │ │
│  │  │  6. Publish proof to chain                                  │ │ │
│  │  │  7. Clear event buffer, start new epoch                     │ │ │
│  │  └─────────────────────────────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────────────────────────────┘ │
│           │                                                             │
│           ▼                                                             │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                        ON-CHAIN                                   │ │
│  │                                                                   │ │
│  │  Block 200: ┌───────────────────────────────────────────────┐    │ │
│  │             │  ZK Proof (published to chain)                 │    │ │
│  │             │                                                │    │ │
│  │             │  Public inputs (visible):                      │    │ │
│  │             │    - prev_state_commitment: 0x7a3f...          │    │ │
│  │             │    - new_state_commitment: 0x9b2e...           │    │ │
│  │             │    - lp_merkle_root: 0x4c8d...                 │    │ │
│  │             │    - fee_merkle_root: 0x1e5a...                │    │ │
│  │             │    - epoch_start: 100                          │    │ │
│  │             │    - epoch_end: 200                            │    │ │
│  │             │                                                │    │ │
│  │             │  Hidden (inside proof):                        │    │ │
│  │             │    - All swap amounts                          │    │ │
│  │             │    - All participants                          │    │ │
│  │             │    - Pool reserves                             │    │ │
│  │             │    - Fee amounts                               │    │ │
│  │             │                                                │    │ │
│  │             │  Verified:                                     │    │ │
│  │             │    ✓ AMM invariant held                        │    │ │
│  │             │    ✓ All math correct                          │    │ │
│  │             │    ✓ Merkle roots valid                        │    │ │
│  │             └───────────────────────────────────────────────┘    │ │
│  └───────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Privacy Model: What Each Party Sees

```
┌─────────────────────────────────────────────────────────────────────┐
│                        VISIBILITY MATRIX                            │
│                                                                     │
│  ┌──────────────────┬──────────┬──────────┬──────────┬───────────┐ │
│  │  Information     │ Trader   │ LP       │ Public   │ Organizer │ │
│  ├──────────────────┼──────────┼──────────┼──────────┼───────────┤ │
│  │ My own txns      │  SEE     │  SEE     │  HIDE    │  SEE      │ │
│  │ My own amounts   │  SEE     │  SEE     │  HIDE    │  SEE      │ │
│  │ Other txns       │  HIDE    │  HIDE    │  HIDE    │  SEE      │ │
│  │ Other amounts    │  HIDE    │  HIDE    │  HIDE    │  SEE      │ │
│  │ Pool reserves    │  HIDE    │  SEE*    │  HIDE    │  SEE      │ │
│  │ My LP shares     │  N/A     │  SEE     │  HIDE    │  SEE      │
│  │ Total LP shares  │  HIDE    │  HIDE    │  HIDE    │  SEE      │
│  │ Fee claims       │  HIDE    │  SEE*    │  HIDE    │  SEE      │
│  │ Proof validity   │  VERIFY  │  VERIFY  │  VERIFY  │  GENERATE │
│  └──────────────────┴──────────┴──────────┴──────────┴───────────┘ │
│                                                                     │
│  * LPs can verify their own shares via Merkle proof                 │
│    but cannot see other LPs' shares or total pool state             │
│                                                                     │
│  Organizer sees everything (required to process events)             │
│  but cannot cheat — the ZK proof guarantees correctness             │
│  and anyone can verify the proof independently.                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Who Runs the Prover?

```
┌─────────────────────────────────────────────────────────────────────┐
│                    PROVER DEPLOYMENT                                 │
│                                                                     │
│  The prover runs as an AUTOMATIC DAEMON SERVICE                     │
│  managed by the PoolOrganizer.                                      │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │                                                               │ │
│  │  PoolOrganizer daemon:                                        │ │
│  │  ┌─────────────────────────────────────────────────────────┐ │ │
│  │  │  1. Listen for encrypted pool events                    │ │ │
│  │  │  2. Buffer events until epoch boundary                  │ │ │
│  │  │  3. At epoch boundary:                                  │ │ │
│  │  │     a. Process all buffered events                      │ │ │
│  │  │     b. Compute new pool state                           │ │ │
│  │  │     c. Build Merkle trees                               │ │ │
│  │  │     d. Run SP1 prover (~5-30 min for 100-block epoch)  │ │ │
│  │  │     e. Publish proof to chain                           │ │ │
│  │  │     f. Clear buffer, start new epoch                    │ │ │
│  │  └─────────────────────────────────────────────────────────┘ │ │
│  │                                                               │ │
│  │  Users do NOT need to run anything.                           │ │
│  │  Users CAN verify proofs independently if they want.          │ │
│  │                                                               │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  Trust model:                                                       │
│  - Users TRUST the organizer to process events correctly            │
│  - Users DO NOT TRUST the organizer to be honest                    │
│  - The ZK proof GUARANTEES correctness regardless of trust          │
│  - If the organizer fails to produce a proof, users know            │
│    something is wrong and can withdraw                              │
│  - If the organizer produces an invalid proof, it's rejected        │
│    on-chain                                                         │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Epoch Timeline: 100 Blocks (~13 Hours)

```
Time ─────────────────────────────────────────────────────────────────►

Block 100          Block 200          Block 300
  │                  │                  │
  │  ┌─ txn ─┐      │  ┌─ txn ─┐      │  ┌─ txn ─┐
  │  │ D     │      │  │ S     │      │  │ W     │
  │  │   S   │      │  │   S   │      │  │   D   │
  │  │     S │      │  │     D │      │  │     S │
  │  └───────┘      │  └───────┘      │  └───────┘
  │        │        │        │        │        │
  │        ▼        │        ▼        │        ▼
  │   PROOF 1       │   PROOF 2       │   PROOF 3
  │   (~15 min)     │   (~15 min)     │   (~15 min)
  │        │        │        │        │        │
  │        ▼        │        ▼        │        ▼
  │   Published     │   Published     │   Published
  │   on-chain      │   on-chain      │   on-chain
  │                  │                  │
  │  Anyone can      │  Anyone can      │  Anyone can
  │  verify          │  verify          │  verify
  │                  │                  │
  │  If no proof     │  If no proof     │  If no proof
  │  by block 215,   │  by block 315,   │  by block 415,
  │  pool is frozen  │  pool is frozen  │  pool is frozen

  Detection window: ~2.5 hours after epoch end
  (15 min proof generation + buffer for chain propagation)
```

---

## Circuit Design: `fuego-lp-circuit`

```
┌─────────────────────────────────────────────────────────────────────┐
│                    ZK CIRCUIT (fuego-lp-circuit)                     │
│                                                                     │
│  INPUT (private, inside the proof):                                 │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  prev_state:                                                  │ │
│  │    reserveA: u64          // hidden                           │ │
│  │    reserveB: u64          // hidden                           │ │
│  │    totalLPShares: u64     // hidden                           │ │
│  │    feeAccumulatorA: u64   // hidden                           │ │
│  │    feeAccumulatorB: u64   // hidden                           │ │
│  │                                                               │ │
│  │  events: Vec<EncryptedPoolEvent>  // hidden                   │ │
│  │    - type: Deposit | Withdrawal | Swap | FeeClaim             │ │
│  │    - amounts: u64                                             │ │
│  │    - participants: PublicKey                                  │ │
│  │                                                               │ │
│  │  prev_lp_merkle_tree: Vec<Hash>     // hidden                 │ │
│  │  prev_fee_merkle_tree: Vec<Hash>    // hidden                 │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                               │                                     │
│                               ▼                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  CIRCUIT LOGIC:                                               │ │
│  │                                                               │ │
│  │  for event in events:                                         │ │
│  │    match event.type:                                          │ │
│  │      Deposit:                                                 │ │
│  │        assert amountsA > 0 && amountsB > 0                    │ │
│  │        shares = sqrt(amountsA * amountsB)                     │ │
│  │        reserveA += amountsA                                   │ │
│  │        reserveB += amountsB                                   │ │
│  │        totalLPShares += shares                                │ │
│  │        add_to_lp_tree(owner, shares)                          │ │
│  │                                                               │ │
│  │      Swap:                                                    │ │
│  │        fee = inputAmount * feeBps / 10000                     │ │
│  │        inputAfterFee = inputAmount - fee                      │ │
│  │        output = apply_amm(reserveIn, reserveOut, inputAfterFee)│ │
│  │        assert output > 0                                      │ │
│  │        reserveIn += inputAmount                               │ │
│  │        reserveOut -= output                                   │ │
│  │        feeAccumulator += fee                                  │ │
│  │                                                               │ │
│  │      Withdrawal:                                              │ │
│  │        assert shares <= owner.shares                          │ │
│  │        amounts = proportional(reserves, shares, totalLPShares) │ │
│  │        reserveA -= amountsA                                   │ │
│  │        reserveB -= amountsB                                   │ │
│  │        totalLPShares -= shares                                │ │
│  │        remove_from_lp_tree(owner, shares)                     │ │
│  │                                                               │ │
│  │      FeeClaim:                                                │ │
│  │        claimable = proportional(feeAccumulator, owner.shares)  │ │
│  │        feeAccumulator -= claimable                            │ │
│  │        add_to_fee_tree(owner, claimable)                      │ │
│  │                                                               │ │
│  │  // Final assertions                                          │ │
│  │  assert reserveA > MIN_LIQUIDITY                              │ │
│  │  assert reserveB > MIN_LIQUIDITY                              │ │
│  │  assert totalLPShares > 0                                     │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                               │                                     │
│                               ▼                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  OUTPUT (public, committed to chain):                         │ │
│  │                                                               │ │
│  │    prev_state_commitment: Hash   // links to previous proof   │ │
│  │    new_state_commitment: Hash    // new pool state hash       │ │
│  │    lp_merkle_root: Hash          // LP share tree root        │ │
│  │    fee_merkle_root: Hash         // fee record tree root      │ │
│  │    epoch_start: u32            // block height                │ │
│  │    epoch_end: u32              // block height                │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  The proof guarantees:                                             │
│  - All events were processed correctly                             │
│  - AMM invariant was maintained throughout                         │
│  - All math is correct (fees, shares, reserves)                    │
│  - Merkle trees are valid                                          │
│  - State transitions are valid                                     │
│                                                                     │
│  The proof reveals:                                                │
│  - NOTHING about amounts, participants, or directions              │
│  - Only that "something valid happened"                            │
└─────────────────────────────────────────────────────────────────────┘
```

---

## What Happens If the Organizer Fails?

```
┌─────────────────────────────────────────────────────────────────────┐
│                    FAILURE MODES                                    │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Scenario: Organizer crashes, no proof by deadline            │ │
│  │                                                               │ │
│  │  → Pool enters FROZEN state                                   │ │
│  │  → No new swaps accepted                                      │ │
│  │  → LPs can still withdraw (using last valid proof as basis)   │ │
│  │  → Anyone can take over as organizer (permissionless)         │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Scenario: Organizer publishes INVALID proof                  │ │
│  │                                                               │ │
│  │  → Proof verification fails on-chain                          │ │
│  │  → Proof is rejected                                          │ │
│  │  → Pool enters FROZEN state                                   │ │
│  │  → Organizer is slashed (if bonded)                           │ │
│  │  → Anyone can take over as organizer                          │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Scenario: Organizer tries to steal funds                     │ │
│  │                                                               │ │
│  │  → Cannot produce valid proof (math won't check out)          │ │
│  │  → Pool freezes                                               │ │
│  │  → LPs withdraw using last valid state                        │ │
│  │  → Organizer is slashed                                       │ │
│  │  → Funds are safe                                             │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  Key insight: The ZK proof is a BOND.                              │
│  The organizer cannot cheat without getting caught.                 │
│  And if they fail (malicious or accidental), users are protected.   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Summary

| | Transparent Pool | ZK Private Pool |
|---|---|---|
| **Swap privacy** | None — all visible on XFG chain | Full — amounts, direction, participants hidden |
| **LP privacy** | None — deposits/withdrawals visible | Full — only Merkle root visible |
| **Correctness** | Auditable but manual | Cryptographically guaranteed |
| **Fraud detection** | Manual, after the fact | Automatic, within ~15 hours |
| **User trust** | Must trust organizer | Trustless — proof guarantees correctness |
| **Complexity** | Low | Medium (SP1 prover daemon) |
| **Cost** | Free | ~0.01-0.1% of pool volume (proving cost) |

**Recommendation: 100-block epochs (~13 hours), automatic daemon prover, ZK for both correctness AND privacy.**
