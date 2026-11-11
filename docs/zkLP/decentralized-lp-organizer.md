# Decentralizing the LP Pool Organizer

## The Problem

A single organizer is a **single point of failure**:


```
CENTRALIZED ORGANIZER (BAD):
  ┌─────────────┐
  │  Organizer  │  ← If this goes down, pool freezes
  │  (one node) │  ← If this is malicious, users must wait for proof timeout
  └──────┬──────┘  ← If this censors, some txns never get proven
         │
         ▼
  ┌─────────────┐
  │  ZK Proof   │
  └─────────────┘
```

The ZK proof guarantees **correctness** but doesn't guarantee **availability**.
We need the organizer to be decentralized so the pool keeps running even if
some organizers fail, go offline, or try to censor.

---

## The Key Insight

**The ZK proof eliminates the need for trust in correctness.**
The organizer doesn't need to be trusted — it just needs to be **incentivized to produce proofs**.

If anyone can verify the proof, then anyone can run the prover.
The organizer is just a **computation service**, not a trusted party.

---

## Decentralization Model: Permissionless Proving

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    PERMISSIONLESS PROVING ARCHITECTURE                   │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                        ON-CHAIN EVENT LOG                          │ │
│  │                                                                   │ │
│  │  Block 100: EncryptedDeposit(from=A, amountA=10000, amountB=5)   │ │
│  │  Block 105: EncryptedSwap(from=B, swapAforB=true, input=500)     │ │
│  │  Block 112: EncryptedSwap(from=C, swapAforB=false, input=2)     │ │
│  │  Block 140: EncryptedWithdrawal(from=A, shares=7071)             │ │
│  │  ...                                                              │ │
│  │                                                                   │ │
│  │  Events are ordered by block height + tx index.                   │ │
│  │  Canonical ordering = no disputes about "what happened."          │ │
│  └───────────────────────────┬───────────────────────────────────────┘ │
│                              │                                          │
│         ┌────────────────────┼────────────────────┐                    │
│         │                    │                    │                     │
│         ▼                    ▼                    ▼                     │
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐              │
│  │  Prover 1   │     │  Prover 2   │     │  Prover 3   │              │
│  │  (anyone)   │     │  (anyone)   │     │  (anyone)   │              │
│  │             │     │             │     │             │              │
│  │  Fetches    │     │  Fetches    │     │  Fetches    │              │
│  │  events     │     │  events     │     │  events     │              │
│  │  Decrypts   │     │  Decrypts   │     │  Decrypts   │              │
│  │  Processes  │     │  Processes  │     │  Processes  │              │
│  │  Generates  │     │  Generates  │     │  Generates  │              │
│  │  proof      │     │  proof      │     │  proof      │              │
│  └──────┬──────┘     └──────┬──────┘     └──────┬──────┘              │
│         │                   │                   │                       │
│         │    ┌──────────────┘                   │                       │
│         │    │    ┌─────────────────────────────┘                       │
│         ▼    ▼    ▼                                                      │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                    PROOF SUBMISSION (race)                         │ │
│  │                                                                   │ │
│  │  First valid proof submitted to chain WINS.                       │ │
│  │  Late proofs are rejected (epoch already closed).                 │ │
│  │                                                                   │ │
│  │  Winner gets:                                                     │ │
│  │    - Protocol reward (if any)                                     │ │
│  │    - Reputation / future selection priority                       │ │
│  │                                                                   │ │
│  └───────────────────────────────────────────────────────────────────┘ │
│                              │                                          │
│                              ▼                                          │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                    ON-CHAIN VERIFICATION                           │ │
│  │                                                                   │ │
│  │  1. Verify SP1 proof (fast, milliseconds)                         │ │
│  │  2. Check epoch boundaries match                                  │ │
│  │  3. Check prev_state_commitment matches last epoch                │ │
│  │  4. If valid → accept, update pool state, reward prover           │ │
│  │  5. If invalid → reject, slash prover (if bonded)                 │ │
│  │                                                                   │ │
│  └───────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## How Decryption Works (The Hard Part)

For permissionless proving, **any prover must be able to decrypt events**.
This means events can't be encrypted to a single organizer's key.

### Option A: Deterministic Encryption (Recommended)

```
┌─────────────────────────────────────────────────────────────────────┐
│                DETERMINISTIC EVENT ENCRYPTION                        │
│                                                                     │
│  When user submits a pool event:                                    │
│                                                                     │
│  1. User encrypts their data with a SHARED POOL KEY                 │
│     (derived from pool ID + epoch number)                           │
│                                                                     │
│  2. The pool key is revealed at epoch boundary                      │
│     (like a commit-reveal scheme)                                   │
│                                                                     │
│  3. ANY prover can now decrypt all events in that epoch             │
│     and generate the proof                                          │
│                                                                     │
│  Timeline:                                                          │
│                                                                     │
│  Epoch N:                                                           │
│    Events encrypted with pool_key(N)                                │
│    Provers CANNOT decrypt yet (key not revealed)                    │
│    Events accumulate on-chain                                       │
│                                                                     │
│  Epoch N boundary:                                                  │
│    pool_key(N) is revealed on-chain                                 │
│    ALL provers can now decrypt epoch N events                       │
│    Race to generate and submit proof                                │
│    First valid proof wins                                           │
│    pool_key(N+1) is already known for next epoch                    │
│                                                                     │
│  Privacy guarantee:                                                 │
│    - During the epoch: events are hidden from everyone              │
│    - After the epoch: events are decryptable by anyone              │
│      (but the ZK proof already committed the state transition)      │
│      (and the proof doesn't reveal individual event details)        │
│                                                                     │
│  This is acceptable because:                                        │
│    - The proof already guarantees correctness                       │
│    - Post-hoc decryption doesn't enable front-running               │
│      (events are already processed and proven)                      │
│    - It enables permissionless proving                              │
└─────────────────────────────────────────────────────────────────────┘
```

### Option B: ZK Event Submission (More Private, More Expensive)

```
┌─────────────────────────────────────────────────────────────────────┐
│                  ZK EVENT SUBMISSION                                 │
│                                                                     │
│  When user submits a pool event:                                    │
│                                                                     │
│  1. User generates a mini-ZK proof that their event is valid        │
│     (e.g., "I have enough shares to withdraw" or                   │
│      "this swap amount is consistent with reserves")                │
│                                                                     │
│  2. The event is submitted encrypted + with the mini-proof           │
│                                                                     │
│  3. The organizer (anyone) collects events, aggregates them,        │
│     and generates the epoch proof                                   │
│                                                                     │
│  4. The epoch proof verifies:                                       │
│     - All mini-proofs are valid                                     │
│     - The aggregate state transition is correct                     │
│                                                                     │
│  Pros: Events stay private even after epoch boundary                │
│  Cons: Each user pays for a mini-proof (expensive per-txn)          │
│        Adds latency to user submission                              │
└─────────────────────────────────────────────────────────────────────┘
```

### Option C: Trusted Execution Environment (TEE)

```
┌─────────────────────────────────────────────────────────────────────┐
│                  TEE-BASED PROVING                                   │
│                                                                     │
│  1. Events are encrypted to a TEE enclave's public key              │
│  2. Only the TEE can decrypt and process events                     │
│  3. The TEE generates the ZK proof inside the enclave               │
│  4. The proof is published with an attestation                      │
│                                                                     │
│  Pros: Events stay private, no key revelation needed                │
│  Cons: Requires TEE hardware (Intel SGX, AMD SEV)                   │
│        TEE attestation is its own trust model                       │
│        Not truly permissionless (need TEE hardware)                 │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Incentive Structure: Why Would Anyone Run a Prover?

```
┌─────────────────────────────────────────────────────────────────────┐
│                    PROVER INCENTIVES                                 │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Model 1: Protocol Reward                                     │ │
│  │                                                               │ │
│  │  - Protocol mints a small reward per valid proof              │ │
│  │  - First valid proof submitted gets the reward                │ │
│  │  - Reward funded by a small tax on pool volume                │ │
│  │                                                               │ │
│  │  Pros: Simple, permissionless                                 │ │
│  │  Cons: Requires token emission                                │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Model 2: Bonded Prover with Slashing                         │ │
│  │                                                               │ │
│  │  - Provers must bond XFG to participate                       │ │
│  │  - Valid proof → bond returned + small fee from pool          │ │
│  │  - Invalid proof → bond slashed                               │ │
│  │  - No proof by deadline → bond slashed (partial)              │ │
│  │                                                               │ │
│  │  Pros: Economic security, no token emission                   │ │
│  │  Cons: Capital requirement limits participation               │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Model 3: Self-Proving (Simplest)                             │ │
│  │                                                               │ │
│  │  - The last user to interact with the pool in an epoch        │ │
│  │    is incentivized to generate the proof                      │ │
│  │    (because the pool freezes without it)                      │ │
│  │  - No separate prover role needed                             │ │
│  │  - Anyone CAN prove, but the last user has the most           │ │
│  │    incentive to do so                                         │ │
│  │                                                               │ │
│  │  Pros: No additional actors, no bonding, no rewards           │ │
│  │  Cons: Relies on user incentive alignment                     │ │
│  │        Last user might not have proving infrastructure        │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Model 4: LP-Prover (Recommended)                             │ │
│  │                                                               │ │
│  │  - LP providers are the natural provers                       │ │
│  │  - They have the most to lose if the pool is compromised      │ │
│  │  - They already run nodes (to monitor their positions)        │ │
│  │  - First LP to submit valid proof gets a small bonus          │ │
│  │    from the fee accumulator                                   │ │
│  │                                                               │ │
│  │  Pros: Aligned incentives, no new actors                      │ │
│  │        LPs already have skin in the game                      │ │
│  │  Cons: Requires LPs to run prover software                    │ │
│  └───────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Failure Modes in Decentralized Model

```
┌─────────────────────────────────────────────────────────────────────┐
│                    FAILURE MODES                                     │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Scenario: No prover submits a proof by deadline              │ │
│  │                                                               │ │
│  │  → Pool enters FROZEN state                                   │ │
│  │  → No new swaps accepted                                      │ │
│  │  → LPs can withdraw using last valid state                    │ │
│  │  → Emergency: any user can trigger a "force withdraw" mode    │ │
│  │    where the pool distributes reserves proportionally         │ │
│  │    to LP share holders (using last valid Merkle root)         │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Scenario: Prover submits INVALID proof                       │ │
│  │                                                               │ │
│  │  → On-chain verification fails                                │ │
│  │  → Proof rejected                                             │ │
│  │  → Prover slashed (if bonded)                                 │ │
│  │  → Other provers still have time to submit valid proof        │ │
│  │  → Pool continues normally                                    │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Scenario: Prover CENSORS specific events                     │ │
│  │                                                               │ │
│  │  → Censorship is impossible in this model                     │ │
│  │  → Events are on-chain in a canonical order                   │ │
│  │  → The proof MUST include ALL events (circuit verifies        │ │
│  │    that all events in the epoch range were processed)         │ │
│  │  → If a prover skips an event, the proof will be invalid      │ │
│  │    (state won't match)                                        │ │
│  → Another prover will include it and win the race              │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Scenario: Two provers submit valid proofs simultaneously     │ │
│  │                                                               │ │
│  │  → First one on-chain wins (standard blockchain ordering)     │ │
│  │  → Second proof is rejected (epoch already closed)            │ │
│  │  → No harm done                                               │ │
│  │  → Wasted computation for the loser (but that's the race)     │ │
│  └───────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Comparison: Centralized vs Decentralized

```
┌──────────────────────┬─────────────────────┬─────────────────────────┐
│  Property            │  Centralized        │  Decentralized          │
├──────────────────────┼─────────────────────┼─────────────────────────┤
│  Correctness         │  ZK guaranteed      │  ZK guaranteed          │
│  Availability        │  Single point of    │  Multiple provers,      │
│                      │  failure            │  race to submit         │
│  Censorship          │  Organizer can      │  Impossible — events    │
│                      │  censor events      │  are on-chain, proof    │
│                      │                     │  must include all       │
│  Trust               │  Trust organizer    │  Trust math only        │
│                      │  to be honest       │                         │
│  Setup               │  One node           │  Anyone can run prover  │
│  Incentive           │  Organizer must     │  Economic incentive     │
│                      │  be paid separately │  (reward/bond/LP-fee)   │
│  Complexity          │  Low                │  Medium                 │
│  Cost                │  One prover         │  N provers compete      │
│                      │                     │  (wasted work is the    │
│                      │                     │   cost of decentralization)│
└──────────────────────┴─────────────────────┴─────────────────────────┘
```

---

## Recommended Path: LP-Prover Model

```
┌─────────────────────────────────────────────────────────────────────┐
│                    RECOMMENDED: LP-PROVER MODEL                      │
│                                                                     │
│  1. Events are encrypted with deterministic pool key                │
│     (revealed at epoch boundary)                                    │
│                                                                     │
│  2. ANYONE can run a prover (permissionless)                        │
│                                                                     │
│  3. LPs have the strongest incentive to prove                       │
│     (their funds are at stake)                                      │
│                                                                     │
│  4. First valid proof submitted wins                                │
│     - Gets a small bonus from fee accumulator                       │
│     - Bonded provers get bond back + bonus                          │
│     - Invalid proofs → slash                                        │
│                                                                     │
│  5. If no proof by deadline:                                        │
│     - Pool freezes                                                  │
│     - LPs can force-withdraw using last valid Merkle root           │
│     - Any user can trigger emergency mode                           │
│                                                                     │
│  This gives us:                                                     │
│  ✓ Privacy (ZK hides amounts, participants, direction)              │
│  ✓ Correctness (ZK guarantees AMM math is right)                    │
│  ✓ Decentralization (anyone can prove, no single point of failure)  │
│  ✓ Censorship resistance (events on-chain, proof must include all)  │
│  ✓ Availability (multiple provers, race ensures at least one)       │
│  ✓ Economic security (bonding + slashing for bad behavior)          │
└─────────────────────────────────────────────────────────────────────┘
```
