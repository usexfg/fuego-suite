# Prover Privacy: Who Sees What in the LP Pool

## The Fundamental Tension

ZK proofs hide data from **chain observers** — but the prover MUST see plaintext to generate the proof.

```
┌─────────────────────────────────────────────────────────────────────┐
│                    THE PRIVACY BOUNDARY                              │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │                    OUTSIDE THE ZK BOUNDARY                     │ │
│  │                    (sees only proofs)                          │ │
│  │                                                               │ │
│  │  Chain observers, block explorers, analytics firms            │ │
│  │  → See: proof exists, state commitments valid                 │ │
│  │  → CANNOT see: amounts, participants, direction, reserves     │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                               ▲                                     │
│                               │                                     │
│                    ════════════════  ZK BOUNDARY  ════════════════   │
│                               │                                     │
│                               ▼                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │                    INSIDE THE ZK BOUNDARY                      │ │
│  │                    (sees plaintext)                            │ │
│  │                                                               │ │
│  │  Provers (whoever generates the proof)                        │ │
│  │  → See: ALL amounts, ALL participants, ALL directions         │ │
│  │  → See: pool reserves, fee amounts, LP share balances         │ │
│  │  → Can: leak this information, but CANNOT steal funds         │ │
│  │         (proof already committed, math is locked in)          │ │
│  └───────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

## The Reality: ZK Hides from Outside, Not from Provers

This is true for ALL ZK systems:

| System | Who sees plaintext? | Who sees only proofs? |
|---|---|---|
| **ZK Rollups** | Sequencer + Provers | Public |
| **Tornado Cash** | User (their own tx) | Public |
| **Aztec** | Proving key holder | Public |
| **Our LP Pool** | Provers | Public |

**The prover is the trusted party for privacy.** Not for correctness (ZK guarantees that), but for confidentiality.

---

## Decentralized Proving = More Eyes on Plaintext

```
┌─────────────────────────────────────────────────────────────────────┐
│                    CENTRALIZED PROVING                              │
│                                                                     │
│  Events ──► [ONE Prover] ──► Proof                                 │
│               ▲                                                      │
│               │                                                      │
│         Only ONE entity sees plaintext                              │
│         (the organizer)                                             │
│         Risk: if organizer is compromised, all data leaks           │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                    DECENTRALIZED PROVING                            │
│                                                                     │
│  Events ──► [Prover A] ──► Proof (winner)                           │
│           [Prover B] ──► (also decrypted, also saw everything)      │
│           [Prover C] ──► (also decrypted, also saw everything)      │
│               ▲                                                      │
│               │                                                      │
│         N entities see plaintext                                    │
│         (all competing provers)                                     │
│         Risk: more entities = higher probability of leak            │
└─────────────────────────────────────────────────────────────────────┘
```

**Decentralization improves availability and censorship resistance but WORSENS prover-side privacy.**

---

## What Can a Malicious Prover Actually Do?

```
┌─────────────────────────────────────────────────────────────────────┐
│                    THREAT MODEL: MALICIOUS PROVER                    │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  CAN DO (privacy harm):                                       │ │
│  │                                                               │ │
│  │  1. Leak swap data publicly                                   │ │
│  │     → "Alice swapped 500 XFG for 0.24 ETH at block 105"      │ │
│  │     → Harm: loss of privacy, potential targeting             │ │
│  │                                                               │ │
│  │  2. Sell data to analytics firms                              │ │
│  │     → Chainalysis-style profiling of pool users               │ │
│  │     → Harm: deanonymization of pool activity                  │ │
│  │                                                               │ │
│  │  3. Front-run FUTURE epochs                                   │ │
│  │     → If prover sees pending encrypted events                 │ │
│  │       (before epoch boundary, before key reveal)              │ │
│  │     → CANNOT do this — events are encrypted until            │ │
│  │       epoch boundary, by which time they're already           │ │
│  │       processed and proven                                    │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  CANNOT DO (financial harm):                                  │ │
│  │                                                               │ │
│  │  1. Steal funds                                               │ │
│  │     → Proof already committed on-chain                        │ │
│  │     → State transition is locked in                           │ │
│  │     → Leaking data doesn't change the math                    │ │
│  │                                                               │ │
│  │  2. Front-run CURRENT epoch                                   │ │
│  │     → Events are encrypted until epoch boundary               │ │
│  │     → By the time prover can decrypt, events are              │ │
│  │       already processed                                       │ │
│  │                                                               │ │
│  │  3. Censor transactions                                       │ │
│  │     → Events are on-chain in canonical order                  │ │
│  │     → If Prover A skips an event, the proof fails             │ │
│  │     → Prover B will include it and win                        │ │
│  │                                                               │ │
│  │  4. Submit invalid proof                                      │ │
│  │     → On-chain verification rejects it                        │ │
│  │     → Prover gets slashed (if bonded)                         │ │
│  └───────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

**Summary: A malicious prover can leak information but cannot steal funds or manipulate the pool.**

---

## The Real Question: Is Prover-Side Privacy Acceptable?

### Scenario 1: Centralized Organizer (Single Prover)

```
Trust model: You trust ONE entity (the organizer) not to leak data.

If organizer is honest: perfect privacy from everyone
If organizer is compromised: ALL data leaks
If organizer is malicious: can sell data, profile users

Risk: single point of failure for privacy
```

### Scenario 2: Decentralized Provers (Permissionless)

```
Trust model: You trust that AT LEAST ONE prover is honest
             (for availability) but you accept that ALL
             provers see your data.

If all provers are honest: data stays within prover set
If one prover leaks: data is exposed
If provers compete: more entities see data, higher leak probability

Risk: more eyes on plaintext, but no single point of failure
```

### Scenario 3: You as the Prover (Self-Proving)

```
Trust model: You trust YOURSELF.

You submit encrypted events, you decrypt at epoch boundary,
you generate the proof. No one else sees your data.

If you're the only prover: perfect privacy
If other provers exist: they also see your data (but you
  control your own proving, so you don't need to trust them)

Risk: you must run prover infrastructure
Benefit: you never expose your data to anyone
```

---

## Recommended: Self-Proving + Permissionless Fallback

```
┌─────────────────────────────────────────────────────────────────────┐
│                    HYBRID PRIVACY MODEL                              │
│                                                                     │
│  Default: User proves their own activity                            │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │                                                               │ │
│  │  User submits encrypted event                                 │ │
│  │  User waits for epoch boundary                                │ │
│  │  User decrypts (has the key)                                  │ │
│  │  User generates proof for THEIR OWN events                    │ │
│  │  User submits proof                                           │ │
│  │                                                               │ │
│  │  → Only the user sees their own data                          │ │
│  │  → Other users' data remains hidden from them                 │ │
│  │  → No prover sees anyone else's data                          │ │
│  │                                                               │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  Fallback: Permissionless proving (if user can't prove)             │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │                                                               │ │
│  │  If user doesn't submit proof by deadline:                    │ │
│  │  → Pool key is revealed                                       │ │
│  │  → Any prover can decrypt ALL events                          │ │
│  │  → Any prover can generate and submit proof                   │ │
│  │  → Pool continues (user's privacy is sacrificed for           │ │
│  │    availability)                                              │ │
│  │                                                               │ │
│  │  This is the tradeoff:                                        │ │
│  │  - User proves → perfect privacy, but user must act           │ │
│  │  - Prover proves → availability guaranteed, but privacy lost  │ │
│  │                                                               │ │
│  └───────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

## The Honest Answer

**There is no free lunch.** You pick two:

| | Privacy | Availability | Decentralization |
|---|---|---|---|
| **Centralized organizer** | ✓ (if honest) | ✗ (single point of failure) | ✗ |
| **Decentralized provers** | ✗ (all provers see) | ✓ | ✓ |
| **Self-proving** | ✓ | ✗ (user must act) | ✓ |

**For maximum privacy:** Self-proving. Users generate their own proofs. No one else sees their data.

**For maximum availability:** Decentralized provers. Someone always generates the proof, but all provers see all data.

**The hybrid approach** gives users the choice: prove yourself for privacy, or let the network prove for availability.
