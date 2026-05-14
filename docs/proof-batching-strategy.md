# Unified Proof Batching Strategy

## The Problem

We have **two different fees** flowing through the system:

```
Banking Transaction (e.g. deposit/withdraw)
├── 0.3% LP swap fee → goes to LP providers (PoolOrganizer)
└── 0.1% Elderfier banking fee → goes to fuego-prover (ZK proof cost)
```

**Privacy risk:** If we generate a proof every time a banking fee is collected,
an observer can correlate proof timestamps with banking transactions → deanonymization.

**Resource risk:** Proving every transaction wastes CPU. SP1 proofs are expensive.

---

## The Solution: Epoch-Based Batch Proving

```
Time ──────────────────────────────────────────────────────────────►

       Epoch 1              Epoch 2              Epoch 3
    [1000 blocks]        [1000 blocks]        [1000 blocks]
    ┌────────────┐       ┌────────────┐       ┌────────────┐
    │            │       │            │       │            │
    ▼            ▼       ▼            ▼       ▼            ▼
  ┌───┐  ┌───┐  ┌───┐  ┌───┐  ┌───┐  ┌───┐  ┌───┐  ┌───┐
  │ B │  │ B │  │ B │  │ B │  │ B │  │ B │  │ B │  │ B │  ← Blocks
  └───┘  └───┘  └───┘  └───┘  └───┘  └───┘  └───┘  └───┘
    │      │      │      │      │      │      │      │
    │  💰  │      │  💰  │      │  💰  │      │  💰  │  ← Banking txns (0.1% fee)
    │      │      │      │      │      │      │      │
    │  🔄  │      │      │  🔄  │      │  🔄  │      │  ← LP swaps (0.3% fee)
    │      │      │      │      │      │      │      │
    └──────┴──────┘      └──────┴──────┘      └──────┴──────┘
         │                      │                      │
         ▼                      ▼                      ▼
    ┌─────────┐            ┌─────────┐            ┌─────────┐
    │ PROOF 1 │            │ PROOF 2 │            │ PROOF 3 │
    │ (batch) │            │ (batch) │            │ (batch) │
    └─────────┘            └─────────┘            └─────────┘
```

**Key idea:** Proofs are generated at **fixed block intervals** (epochs),
regardless of how many or how few banking transactions occurred.

- 0 banking txns in epoch → proof still generated (proves "nothing happened")
- 100 banking txns in epoch → same single proof (proves "all 100 were correct")
- Observer cannot tell which blocks had banking activity

---

## Two-Proof Architecture Per Epoch

```
┌────────────────────────────────────────────────────────────────────────┐
│                          EPOCH BOUNDARY                                │
│                    (every N blocks, e.g. 1000)                         │
│                                                                        │
│  ┌──────────────────────────┐    ┌──────────────────────────────────┐  │
│  │   PROOF A: HEAT          │    │   PROOF B: LP Pool State         │  │
│  │   (fuego-circuit)        │    │   (fuego-lp-circuit)             │  │
│  │                          │    │                                  │  │
│  │  Scans ALL blocks in     │    │  Processes ALL pool events       │  │
│  │  epoch for tag 0x08      │    │  in epoch:                       │  │
│  │                          │    │    - deposits                    │  │
│  │  Proves commitments      │    │    - withdrawals                 │  │
│  │  exist on-chain          │    │    - swaps                       │  │
│  │                          │    │    - fee claims                  │  │
│  │  Public output:          │    │                                  │  │
│  │    new_merkle_root       │    │  Proves AMM math is correct      │  │
│  │    new_checkpoint_hash   │    │                                  │  │
│  │    height_end            │    │  Public output:                  │  │
│  └──────────┬───────────────┘    │    lp_merkle_root                │  │
│             │                    │    fee_merkle_root               │  │
│             │                    │    total_banking_fees_collected  │  │
│             │                    │    height_end                    │  │
│             │                    └──────────┬───────────────────────┘  │
│             │                               │                          │
│             │         ┌─────────────────────┤                          │
│             │         │                     │                          │
│             ▼         ▼                     ▼                          │
│  ┌────────────────────────────────────────────────────────────┐        │
│  │              VERIFICATION LAYER                             │        │
│  │                                                             │        │
│  │  1. Verify Proof A (HEAT commitments valid)                │        │
│  │  2. Verify Proof B (LP pool math correct)                  │        │
│  │  3. Cross-check: banking fees in Proof B match             │        │
│  │     commitment count in Proof A                            │        │
│  │     (optional: proves fees correspond to real txns)        │        │
│  └────────────────────────────────────────────────────────────┘        │
└────────────────────────────────────────────────────────────────────────┘
```

---

## Privacy: Why This Works

```
┌──────────────────────────────────────────────────────────────────┐
│                    PRIVACY ANALYSIS                              │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ❌ BAD: Proof-per-transaction                                   │
│                                                                  │
│     txn ──► proof ──► txn ──► proof ──► txn ──► proof           │
│      ↑       ↑        ↑       ↑        ↑       ↑                 │
│     observer can link each proof to a specific txn               │
│     = deanonymization                                            │
│                                                                  │
│  ✅ GOOD: Epoch-batch proving                                    │
│                                                                  │
│     ┌─ epoch (1000 blocks) ─┐   ┌─ epoch ─┐                     │
│     │ txn  txn     txn  txn │   │  (empty)│                     │
│     └───────────┬───────────┘   └────┬────┘                     │
│                 │                     │                          │
│              ONE proof            ONE proof                      │
│                                                                  │
│     Observer sees:                                               │
│     - Proof at epoch boundary (predictable, no info leaked)      │
│     - Cannot tell if epoch had 0 txns or 100 txns                │
│     - Cannot link any specific txn to a proof                    │
│                                                                  │
│  🔒 EVEN BETTER: Dummy epochs                                    │
│                                                                  │
│     If an epoch has ZERO banking txns, the LP proof still        │
│     runs and outputs "0 fees collected" — indistinguishable      │
│     from an epoch with fees that happen to net to zero.          │
│     The proof structure is identical either way.                 │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

---

## Prover Privacy in Payment Routing

```
┌──────────────────────────────────────────────────────────────────┐
│               PROVING FEE PAYMENT FLOW                           │
│                                                                  │
│  The 0.1% Elderfier banking fee needs to pay the prover.        │
│  But we don't want to reveal WHO the prover is.                 │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Approach: Fee Pool (Anonymous Prover Payment)           │   │
│  │                                                          │   │
│  │  1. Each epoch, 0.1% fees accumulate in a fee pool      │   │
│  │     (on-chain commitment, not a real wallet)             │   │
│  │                                                          │   │
│  │  2. ANY prover can submit a valid proof                  │   │
│  │     (not just "the" prover)                              │   │
│  │                                                          │   │
│  │  3. First valid proof submitted → prover gets paid       │   │
│  │     from the fee pool                                    │   │
│  │                                                          │   │
│  │  4. Prover identity is hidden:                          │   │
│  │     - Payment goes to a one-time address                 │   │
│  │     - Or through a mixing/stealth address scheme         │   │
│  │     - Or the prover is the banking txn initiator         │   │
│  │       (self-proving, no payment routing needed)          │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Approach: Self-Proving (Simplest)                       │   │
│  │                                                          │   │
│  │  The user who initiates the banking transaction           │   │
│  │  ALSO generates the proof. The 0.1% fee covers their     │   │
│  │  own proving cost. No payment routing at all.            │   │
│  │                                                          │   │
│  │  Pros: Zero payment routing, zero prover identity leak   │   │
│  │  Cons: User must wait for proof before txn completes     │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Approach: Deferred Batch Payment                        │   │
│  │                                                          │   │
│  │  Fees accumulate over many epochs (e.g. 100 epochs).     │   │
│  │  Prover claims payment in a single batch transaction     │   │
│  │  that mixes with many other payments.                    │   │
│  │                                                          │   │
│  │  Pros: Amortized payment overhead, better mixing         │   │
│  │  Cons: Prover must front-proof costs                     │   │
│  └──────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

---

## Epoch Configuration

```
┌──────────────────────────────────────────────────────────────────┐
│                   EPOCH SIZE TRADEOFFS                           │
├──────────────┬──────────────────────┬───────────────────────────┤
│  Epoch Size  │  Privacy             │  Resource Efficiency      │
├──────────────┼──────────────────────┼───────────────────────────┤
│              │                      │                           │
│  100 blocks  │  Good                │  Proof generated often    │
│  (~3 hours)  │  Small batches       │  Higher CPU cost          │
│              │  Less mixing         │  Lower latency for fee    │
│              │                      │  collection               │
│              │                      │                           │
│  1000 blocks │  Better              │  Good balance             │
│  (~1 day)    │  More txns per batch │  Reasonable CPU cost      │
│              │  Good mixing         │  Reasonable latency       │
│              │                      │                           │
│  10000 blocks│  Best                │  Very efficient           │
│  (~10 days)  │  Maximum mixing      │  Lowest CPU cost per txn  │
│              │  Hard to correlate   │  High latency for fee     │
│              │                      │  collection               │
│              │                      │                           │
├──────────────┴──────────────────────┴───────────────────────────┤
│                                                                  │
│  RECOMMENDED: 1000 blocks (~1 day for Fuego)                    │
│                                                                  │
│  - Long enough to batch many txns together                      │
│  - Short enough that proving cost per epoch is manageable       │
│  - Predictable schedule (no timing side-channel)                │
│  - Matches natural blockchain checkpoint rhythm                 │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

---

## Complete Flow: One Epoch End-to-End

```
Block 1000 (epoch start)
    │
    │  ┌─ Banking txn: Alice deposits 1000 XFG
    │  │  └── 0.1% fee = 1 XFG → fee accumulator
    │  │
    │  ┌─ LP swap: Bob swaps 50 XFG → BTC
    │  │  └── 0.3% fee = 0.15 XFG → LP providers
    │  │
    │  ┌─ Banking txn: Charlie withdraws 500 XFG
    │  │  └── 0.1% fee = 0.5 XFG → fee accumulator
    │  │
    │  (no txn for 200 blocks...)
    │  │
    │  ┌─ LP deposit: Dave adds liquidity
    │     └── No fee (deposits are free)
    │
Block 2000 (epoch end)
    │
    ├── PoolOrganizer generates checkpoint:
    │   - LP share merkle root
    │   - Fee record merkle root
    │   - Total banking fees: 1.5 XFG
    │
    ├── fuego-lp-circuit proves:
    │   - All swaps used correct AMM math
    │   - All fees calculated correctly
    │   - Merkle roots are valid
    │
    ├── fuego-circuit proves:
    │   - All banking txns have valid HEAT commitments
    │   - Commitment merkle root is valid
    │
    ├── Cross-verify:
    │   - Banking fee count (from LP proof)
    │     matches commitment count (from HEAT proof)
    │
    └── Fee distribution:
        - 1.5 XFG → prover payment pool
        - 0.15 XFG → LP providers (already distributed)
        - New epoch begins
```

---

## Key Design Decisions

```
1. FIXED EPOCH INTERVALS (not event-triggered)
   → No timing side-channel
   → Empty epochs produce identical proof structure
   → Observer cannot infer transaction volume

2. BATCH PROVING (not per-txn)
   → Amortized proving cost
   → Better privacy through mixing
   → Fewer proofs to store/verify

3. TWO SEPARATE PROOFS PER EPOCH
   → HEAT proof: chain commitments are valid
   → LP proof: pool math is correct
   → Cross-verify links them without merging

4. PROVING FEE = 0.1% OF BANKING TXN
   → Covers SP1 proving cost
   → Paid from fee pool, not direct routing
   → Prover identity hidden

5. DUMMY-PROOF COMPATIBLE
   → Epochs with zero txns still produce proofs
   → Proof structure is identical either way
   → No information leakage from proof presence/absence
```
