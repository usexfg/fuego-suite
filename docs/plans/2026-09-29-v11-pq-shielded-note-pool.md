# Post-Quantum Note Pool with Public Bill Amounts: Design, Developer Guide and Delivery Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Status:** design, rewritten 2026-11-15, amended 2026-11-17 (target v12; hidden HEAT amounts between wallets). It replaces the 2026-09-29 text of this file. Nothing here is implemented. No audit, benchmark or network upgrade is claimed.

**Goal:** Give XFG, HEAT and LP shares a spend path that a quantum computer cannot forge or trace. XFG and LP amounts stay public. HEAT amounts are hidden for transfers between wallets and public wherever HEAT meets a protocol action. Supply stays countable by anyone.

**Architecture:** Value is held as notes of fixed bill sizes. Each bill size of each asset has its own append-only hash tree. A spend publishes a nullifier and a hash-based proof that it owns some unspent leaf of one tree; it does not say which. XFG and LP amounts never enter the proof: consensus adds up public bill sizes in the clear. HEAT notes carry a hidden value, and the proof shows HEAT balance against public amounts entering and leaving hidden HEAT (section 2.8). Protocol state (pool reserves, vaults, CD totals, AuntPoL) stays public consensus state.

**Tech Stack:** C++17 daemon and wallets; one new Rust crate (`fuego-notepool/`) for hashing, note encryption, prover and verifier, linked through a C ABI as `fuego-swapd-adaptor/` is today; ML-KEM-768 ([NIST FIPS 203](https://csrc.nist.gov/pubs/fips/203/final)) for note delivery; a STARK ([original paper](https://eprint.iacr.org/2018/046)) for the spend proof.

**Spec (owner decisions):**
- 2026-11-14: XFG never enters a hidden-amount pool. The one-pool, hidden-amount design of 2026-09-29 is rejected.
- 2026-11-14: option A for XFG — a post-quantum pool with public amounts.
- 2026-11-15: bill sizes are 1 and 5 × 10^k. 2026-11-18: the smallest XFG bill is 0.0001 XFG. Fall back to 1, 2, 5 × 10^k only if the benchmark in Task N0-3 shows per-input proving cost dominates.
- 2026-11-19: XFG amounts stay public, confirmed after review of hiding them everywhere except at public pools.
- 2026-11-17: the pool is targeted at v12. HEAT transfers between wallets hide their amounts inside the pool, with a turnstile (section 2.8). XFG and LP stay public bills.

**Companion plans:** `docs/plans/2026-11-12-v11-readiness-fix-plan.md` (v11 blockers) and `docs/plans/2026-11-13-heat-cd-privacy-plan.md` (R1 rings for v11, option A for v12 CDs). Both land first. This pool is targeted at v12 (privacy plan Phase 3, Tasks V12-4 and V12-5). Its gates, not the version number, set its date: if they are not met, v12 ships without it.

## Global Constraints

- Every XFG and LP amount on chain is public. HEAT amounts are hidden only inside HEAT notes, and are public at every protocol boundary.
- Consensus checks XFG and LP balance in the clear. HEAT balance across hidden notes rests on the proof, bounded by the turnstile of section 2.8. No proof is trusted for an XFG or LP amount.
- No elliptic-curve assumption appears in note ownership, note delivery, the spend proof or the final verifier.
- Yield comes only from fees paid. Nothing in this plan changes fee routing, mint pricing or CD rules.
- `CHANGELOG.agent.md` entry before any source edit. No commit or push without the owner's go-ahead.
- Build ground truth `make -j$(nproc)`; each consensus rule proven by mutation (remove the rule, its test fails, restore, test passes).
- No activation on mainnet before the gates in Phase N10 pass.

## Review Focus

1. A spend proof for one bill class is never accepted for another class or another asset.
2. One leaf yields exactly one nullifier, whatever the spender does.
3. The public supply figure cannot rise through any spend, valid or forged.
4. A view-only wallet sees incoming notes and its own spends, and cannot spend.
5. A reorg restores every tree, root history, nullifier set and class counter exactly.
6. A legacy output credited through the bridge can never also be spent under the legacy rules.
7. No `HEAT_HIDDEN` transaction balances unless constraints 6–8 hold, and `hiddenHeatFloat` can never go negative.

---

## Part 1 — What changes from the 2026-09-29 text

| Topic | 2026-09-29 | This plan |
|---|---|---|
| Amounts | Hidden inside the proof | XFG and LP: public bills, balance checked in the clear. HEAT: hidden between wallets, public at protocol boundaries, turnstile |
| Asset type | Hidden | Public |
| Supply audit | Depends on proof soundness | Arithmetic over public counters; independent of the proof |
| Proof statement | Membership, ownership, nullifier, value conservation, range, per-asset balance, policy | Membership, ownership, nullifier only |
| Trees | One | One per bill class |
| Hearth, CDs, vaults | Open question: private or public state | Public state, decided |
| Release | "v11 design" | Targeted at v12; v11 ships on rings |
| Legacy commitment key fix (P0) | Urgent, open | `deriveCommitmentOutputKey` in `TransactionExtra.cpp` now binds the recipient spend key. Verify against `docs/developer/v11-p0-commitment-keys-and-asset-accounting-guide.md` before bridge work (Task N9-1) |

`docs/developer/v11-pq-shielded-stark-implementation-guide.md` and `docs/plans/PQ_PHASES_2_3_DEV_GUIDE.md` describe the hidden-amount design. They are historical once this plan is accepted; Task N0-1 marks them so.

---

## Part 2 — Design

### 2.1 Bill classes

A bill class is one (asset, size) pair. It has an id, a tree and two counters.

| Asset | Sizes | Classes | Smallest | Largest |
|---|---|---|---|---|
| XFG | 1, 5 × 10^k | 21 | 0.0001 XFG (10^3 atomic) | 1,000,000 XFG |
| HEAT | One hidden-amount class, `HEAT_HIDDEN` (section 2.8) | 1 | 1 atomic unit | 2^64 − 1 atomic units |
| LP shares | `LP_BILL_DENOMINATIONS` (privacy plan R1-d) | 14 | 10^5 shares | 5 × 10^11 shares |

- Max supply is about 8.0M XFG; a larger holding is several 1,000,000 bills.
- With bill inputs and bill outputs a fee is a multiple of 0.0001 XFG. Today's minimum fee, 0.0008 XFG, already is one, so it stays (decision N-6).
- A block reward is not a bill sum. The coinbase pays the largest bill sum not above the reward; the remainder below 0.0001 XFG is carried in consensus state to the next block's reward (Task N6-1).
- The v11 XFG bills (privacy plan Task V11-2) and LP bills have the same sizes as these classes, so they cross the bridge one for one. R1 HEAT bills enter `HEAT_HIDDEN` as notes of their public value.

### 2.2 Notes and keys

All hashes are one function `H` with a distinct domain tag per use. Outputs are 32 bytes. The hash is chosen in Task N0-2.

| Value | Definition | Who knows it |
|---|---|---|
| `sk` | 32 random bytes from the wallet seed | Spender only |
| `ak` | `H("fuego-np-ak" ‖ sk)` | Spender |
| `nk` | `H("fuego-np-nk" ‖ sk)` | Spender, full view key |
| KEM key pair `(dk_d, ek_d)` | ML-KEM-768, derived from the seed and diversifier index `d` | `dk_d`: incoming view key. `ek_d`: public |
| `ownerTag_d` | `H("fuego-np-owner" ‖ ak ‖ nk ‖ d)` | Sender, receiver |
| Address | `ownerTag_d ‖ ek_d` (32 + 1,184 bytes) | Public |
| `rho` | Note randomness, 32 bytes, derived from the KEM shared secret and the output's position in the transaction | Sender, receiver |
| Note commitment `cm` | `H("fuego-np-cm" ‖ classId ‖ ownerTag_d ‖ rho)` | Public |
| Nullifier `nf` | `H("fuego-np-nf" ‖ nk ‖ classId ‖ position ‖ cm)` | Public at spend |

- `ownerTag` commits to both `ak` and `nk`. A spender cannot choose a second `nk` for the same note, so a leaf has one nullifier.
- `position` is the leaf's index in its class tree, assigned by consensus. Two leaves with equal `cm` still have different nullifiers; consensus also rejects a `cm` already present in the class tree.
- Incoming view key = every `dk_d`: finds notes, cannot see spends. Full view key adds `nk`: sees spends, cannot spend. Spending needs `sk`.
- A sender never learns `sk`, `ak` or `nk`. A sender who knows `rho` cannot compute `nf`.

Costs to accept:
- An address is about 1.2 KB, against 95 characters today. It needs QR or file transfer, or an alias that resolves to it.
- Each subaddress is its own KEM key. Scanning cost rises with the number of subaddresses a wallet watches; there is no cheap diversified-address trick for ML-KEM.
- A KEM ciphertext is 1,088 bytes. The format uses one ciphertext per recipient per transaction, not one per bill: every bill's `rho` is derived from that one shared secret.

### 2.3 Spend statement

Public inputs: chain id, protocol version, `classId`, an accepted root of that class tree, `nf`, and `sighash` (the hash of the whole transaction without its proofs).

The prover knows `sk`, `d`, `rho`, `position` and a Merkle path, and proves:
1. `ak = H("fuego-np-ak" ‖ sk)` and `nk = H("fuego-np-nk" ‖ sk)`.
2. `ownerTag = H("fuego-np-owner" ‖ ak ‖ nk ‖ d)`.
3. `cm = H("fuego-np-cm" ‖ classId ‖ ownerTag ‖ rho)`.
4. The path from `cm` at `position` reaches the public root.
5. `nf = H("fuego-np-nf" ‖ nk ‖ classId ‖ position ‖ cm)`.

`sighash` is absorbed into the proof transcript, so the proof also authorizes the transaction; no separate signature exists. One proof covers all inputs of a transaction. For XFG and LP there is no amount, range or balance constraint: that is the whole statement. A transaction with HEAT notes adds constraints 6–8 of section 2.8.

### 2.4 Transaction format

A new transaction version, with new variant types. `TransactionInputUnified` and `TransactionOutputUnified` are not reused.

```cpp
struct TransactionInputNote  { uint16_t classId; Crypto::Hash nullifier; };
struct TransactionOutputNote { uint16_t classId; Crypto::Hash commitment; };
struct NoteDelivery          { std::array<uint8_t, 1088> kemCiphertext; };  // one per recipient
struct NoteAnchor            { uint16_t classId; Crypto::Hash root; };      // one per input class used
```

- A transaction carries its inputs, outputs, anchors, exactly two `NoteDelivery` entries (recipient and change; a self-send fills the second with a ciphertext to itself, so the count shows nothing), the public fee, any Hearth or protocol tag in `extra`, and one proof.
- Output order is shuffled by the wallet. A receiver finds its notes by decapsulating each delivery, deriving `rho_i` for each output index `i` and comparing the recomputed `cm` with the published one.
- Balance rule, checked by consensus with no proof involved: for each asset, the sum of input bill sizes plus protocol sources equals the sum of output bill sizes plus protocol sinks plus fee. This is today's `validateSettlement` with note sums in place of ring-input sums.

### 2.5 Consensus state

Per class: an incremental Merkle tree of depth 32 (frontier only), the last `NOTE_ANCHOR_DEPTH` roots, `created` and `spent` counters. Globally: the nullifier set and a per-block undo record. All of it is in the block cache and reversed by `popBlock`.

A proof verifies against any root within the last `NOTE_ANCHOR_DEPTH` blocks (proposed 100; decision N-5).

### 2.6 What stays public, by rule

| Item | Why it is public |
|---|---|
| Every XFG and LP note's size, at creation and at spend; the asset of every note | XFG and LP balance is checked in the clear |
| HEAT amounts entering or leaving hidden notes | Protocol actions move public totals (reserves, CDs, burns) |
| Pool reserves, total LP shares | Pricing and LP payouts are checked by every node |
| Total HEAT in CDs; each CD's size and term | Yield is fees divided by the total on deposit |
| `CD_APY_POOL`, Bonus Vault, fee inflows | Payouts must never exceed the vault |
| AuntPoL position and legs | Protocol-owned; no person's privacy is involved |

Hidden: which leaf a spend consumes, who owns any note, and the link between a payment's inputs and the notes that funded them.

### 2.7 What an observer still learns

- The size of every XFG and LP bill spent and created, and the transaction's note count. An XFG payment's total is visible. A HEAT payment between wallets shows no amount.
- Bills spent together in one transaction belong to one owner.
- Timing. A bill class with few leaves hides little; at launch every class is thin, and no design changes that.
- The node that first relays a transaction. The changelog records the Dandelion stem phase as disabled.
- A wallet that asks a remote node for a Merkle path names its note. Wallets must build class trees from block data themselves, or download whole subtree ranges (Task N8-3).
- Hearth tags, limit-order ids, CD outputs and LP adds are public actions. The pool hides which notes paid for them and which notes their proceeds become, not the actions.

### 2.8 Hidden HEAT amounts (owner, 2026-11-17)

HEAT transfers between wallets hide their amounts. XFG and LP shares stay public bills.

- One class, `HEAT_HIDDEN`, replaces the eight HEAT bill classes. Its notes carry a 64-bit value inside the commitment: `cm = H("fuego-np-cm" ‖ classId ‖ ownerTag_d ‖ value ‖ rho)`. Binding rests on the hash, not on discrete log, so a quantum computer cannot open a note to another value.
- The anonymity set of a HEAT spend is every HEAT note, not one bill size.
- Values are atomic HEAT. No bill unit applies inside the class, so the whole-step limit of v11 (privacy plan Task V11-7) does not apply inside the pool.
- Each transaction carries two public fields: `heatPublicIn`, the public HEAT entering hidden notes (mint output, Hearth or limit proceeds, LP removal, CD payout, R1 bills crossing the bridge), and `heatPublicOut`, the hidden HEAT leaving to a public sink (burn, Hearth input, limit deposit, LP add, CD deposit, treasury). Every protocol boundary keeps a public amount, as today.
- Consensus-created HEAT notes (CD payouts, mint outputs) have a value public at creation. It is hidden once spent, because a spend does not name its leaf. A CD pays out as one note of its exact value, with no sibling bills.
- Turnstile: consensus keeps `hiddenHeatFloat = Σ heatPublicIn − Σ heatPublicOut` and rejects a block that would make it negative. Public HEAT supply stays public mints minus public burns. The hidden float is the part of it whose integrity rests on the proof.

Statement additions (section 2.3) for a transaction with HEAT inputs or outputs:
6. Each HEAT input value and output value is below 2^64 (bit decomposition).
7. Each HEAT output commitment is computed in the proof from its value, owner tag and `rho`, so a sender cannot publish a commitment to a value it did not prove.
8. Σ HEAT input values + `heatPublicIn` = Σ HEAT output values + `heatPublicOut`.

What it costs:
- Measures F-2 and F-3 bound a forgery per bill size. For `HEAT_HIDDEN` the bound is the whole hidden float, which may be most HEAT in existence. A forger who breaks the proof can take hidden HEAT out through Hearth for real XFG; it shows only when honest exits would push the float below zero. F-6 is therefore required for this class (decision N-11).
- Proving cost rises by the range and sum constraints. Task N0-3 measures it.
- A wallet's HEAT balance and the amounts it sends are hidden from observers, but not from anyone holding its incoming or full view key.

---

## Part 3 — Forgery: what bills do, and what they cannot do

A forgery is a spend accepted for a note that does not exist or was already spent. It needs a flaw in the proof system, its implementation, the hash, or the consensus code around them.

### 3.1 The supply figure cannot be inflated

For each class, consensus keeps `created` and `spent`, and rejects a block in which `spent` would exceed `created`. The public supply of an asset is `Σ size × (created − spent)` over its classes, plus public protocol balances. Every term is public arithmetic.

- A forged spend raises `spent` by one and creates outputs worth the same. The supply figure does not move.
- What the forger takes is a place in the queue. The class now has more real notes than `created − spent` allows, and the last honest holders of that size are refused.
- So a forgery in an XFG or LP class is theft from the holders of one bill size, bounded by that size's unspent float. It never raises the public supply figure. Under hidden amounts without a turnstile the same flaw is unbounded and invisible.
- In `HEAT_HIDDEN` the turnstile gives the same kind of bound, but over the whole hidden float, not one bill size (section 2.8).
- Precedent for the class of flaw: the CryptoNote key-image flaw disclosed in 2017, which allowed outputs to be spent more than once on affected chains.

### 3.2 Measures, what each stops, what each costs

| # | Measure | Stops | Cost | Status |
|---|---|---|---|---|
| F-1 | Balance outside the proof (2.4) | Every value-arithmetic forgery in XFG and LP: overflow, negative value, wrong asset, unbalanced transaction. These are the flaws behind most known counterfeiting bugs. For `HEAT_HIDDEN` the balance is inside the proof; the turnstile bounds what a flaw can take | None | Core rule |
| F-2 | One tree per class; `classId` in the leaf hash, the nullifier and the public inputs | A proof for a 0.0001 bill presented as a 1,000,000 bill. The size is not a witness, so nothing in the proof can misstate it. Does not apply to `HEAT_HIDDEN`, whose value is a witness | 36 trees | Core rule |
| F-3 | Per-class counters with rejection at `spent > created` (3.1) | Inflation of supply. Caps theft at one class's float | Two integers per class | Core rule |
| F-4 | One nullifier per leaf: `nk` bound in `ownerTag`, `position` and `cm` in the nullifier, duplicate `cm` rejected | Double-spending a real note by re-deriving a nullifier; duplicate-commitment attacks | None | Core rule |
| F-5 | Leaves are appended only by consensus from accepted outputs; proofs verify only against stored recent roots | Membership in a tree the forger built | Root history | Core rule |
| F-6 | Two independent proofs of the same statement, from two proof systems with different hash functions and separate code. A spend needs both | A forgery now needs two unrelated breaks. The statement is five hashes and a path, which is what makes a second proof affordable | Roughly double the proof size and proving time; two audits | Recommended for mainnet if the benchmark allows (decision N-3) |
| F-7 | Two independently written verifiers for each proof; a block is valid only if both accept | A bug in one verifier that accepts an invalid proof. Does not stop a flaw in the proof system itself | A second verifier and its audit; any disagreement is a rejected block, never a split | Minimum for mainnet |
| F-8 | Conservative parameters: soundness from proven bounds, not conjectured ones; 256-bit hash output; a hash with long public cryptanalysis (Keccak is already in this codebase) over a newer circuit-friendly hash | Parameter-level and hash-level breaks | Proving is slower with a non-algebraic hash, possibly by an order of magnitude. Decided by benchmark (decision N-2) | Recommended |
| F-9 | Generations: each class tree is closed every `W` blocks and a spend names its generation; counters are per class and generation | Caps a forgery at one generation's float, and narrows where it happened | The anonymity set shrinks to one generation of one class. `W` is the dial | Optional; off at launch (decision N-4) |
| F-10 | Alarm when any class's `created − spent` falls below a threshold, exposed on `/note_pool_status` | Nothing; it shortens the time to notice exhaustion | None | Core rule |
| F-11 | Legacy bridge inputs keep the prime-order subgroup check on key images (`Blockchain.cpp`, comment "M-2: subgroup check") | The 2017 double-spend class at the bridge | None | Verify in Task N9-2 |

Stated plainly:
- No measure makes forgery impossible if every proof layer in use is broken. F-1 to F-5 remove the forgery classes that need no proof break. F-6 raises the bar to two independent breaks. F-3 turns what remains from inflation into bounded theft.
- A forgery under F-3 is silent until a class runs out. F-9 is the only measure that narrows it sooner, and it costs anonymity.
- Under-constrained circuits are the usual real-world failure of proof-based money. The statement here is small enough to specify line by line and to test with a negative case per constraint (Task N3-2). That is a direct benefit of public bills.

---

## Part 4 — Value paths

| Path | In the pool | Publicly visible |
|---|---|---|
| XFG payments | Notes in, notes out, public fee | Bill sizes and counts |
| HEAT payments between wallets | Hidden HEAT notes in and out, public XFG fee | Note counts only |
| Miner reward | Coinbase creates XFG notes to the miner's address; sub-bill remainder carried | Reward size, as today |
| HEAT mint and burn | XFG notes in, burn and mint tag, HEAT notes out (and the reverse) | Amounts and price, as today |
| Hearth swap, limit deposit and withdraw | Notes in; tag; reserves or escrow move as today; notes out | The action and its amounts. A limit withdraw names its order, so it links to its deposit |
| LP add and remove | Asset notes in, LP notes out; LP notes in, asset notes out | Add and remove sizes. The pool replaces the ring of 16 with the whole LP class |
| CDs (option A) | Hidden HEAT notes in with a public `heatPublicOut`, public CD output; at maturity consensus creates one hidden HEAT note of the exact payout value for the CD's `ownerTag`, with `rho = H("fuego-np-cd" ‖ cdId)` | CD size, term, yield and its payout note's value at creation. A CD needs its own diversifier |
| Vaults, AuntPoL, reserves | Not notes. Public balances in consensus state | Everything |
| Atomic swaps | Escrow stays a public output with classical adaptor signatures; funded from notes, paid out to notes | The escrow. It is exposed to a quantum attacker for its lifetime, and the other chain's leg is outside this plan |
| Aliases, DIGM, dev-fund fee | Paid from notes to a public address's notes | The payment size |

---

## Part 5 — Legacy outputs and the quantum timeline

- Every legacy output (ed25519 key outputs, commitment outputs) is spent once through a bridge transaction: legacy inputs under their historical rules, note outputs, public amounts. From pool activation, no transaction may create a legacy output.
- Legacy digit amounts are re-cut into bills at the bridge (7 XFG becomes 5 + 1 + 1). Anything below 0.0001 XFG goes to the fee.
- A bridge transaction is as traceable as a legacy spend. Privacy starts with the first spend inside the pool.
- Chain history already recorded with rings is traced by a quantum computer, whatever is done later. This cannot be repaired.
- An unmigrated legacy output can be stolen once discrete logs can be solved. In CryptoNote the wallet seed is the spend secret itself, so no proof of an earlier hash preimage can rescue an owner afterwards (to confirm against the wallet's key derivation in Task N9-1). Migration before that day is the only protection.
- Decision N-7: whether a later fork may freeze unmigrated legacy outputs at an announced height. Freezing stops theft and also locks out late owners. This plan does not decide it; it requires the bridge to stay open until the owner decides.
- HEAT and LP under R1 rings and CD payout keys under option A are classical too. They are protected only after they cross the bridge.

---

## Part 6 — Implementation

Line numbers are not given where they were not read for this rewrite; locate each site before editing. Each task ends with build, tests, mutation, changelog sign-off.

### Phase N0 — Specification and benchmark gate

**Task N0-1: Changelog and document status**
- [ ] `CHANGELOG.agent.md` section "PQ note pool", all tasks below as TODO, with the sign-off table.
- [ ] Add a first line to `docs/developer/v11-pq-shielded-stark-implementation-guide.md` and `docs/plans/PQ_PHASES_2_3_DEV_GUIDE.md`: "Historical. Superseded by `docs/plans/2026-09-29-v11-pq-shielded-note-pool.md` as rewritten 2026-11-15."

**Task N0-2: Byte-level specification**
- [ ] Create `docs/specs/note-pool-v1.md`: chain and network ids; every domain tag; field and byte encodings; class id table; address, note, nullifier, delivery and transaction layouts; sighash definition; anchor rule; unknown-version rejection.
- [ ] Choose the hash for trees and notes and record the reason (F-8).
- [ ] Publish test vectors as JSON under `tests/Vectors/notepool/`: key derivation, `ownerTag`, `cm`, `nf`, tree roots for 1, 2 and 1,000 leaves, one full transaction.
- [ ] Independent review of the statement in 2.3 before any circuit code.

**Task N0-3: Benchmark gate**
- [ ] Implement the Task N3-1 circuit first, then measure: proving time and memory for 1, 4, 8 and 16 inputs on the reference phone and desktop; verification time on the reference node; proof size; the same with a second proof (F-6), with each candidate hash, and with all inputs and outputs in `HEAT_HIDDEN` (constraints 6–8).
- [ ] Record results in the spec. Gates (proposed; owner confirms, decision N-1): 16 inputs prove within 20 s on the reference phone; a transaction verifies within 50 ms; a 16-input transaction is at most 250 KB.
- [ ] If per-input proving dominates and the phone gate fails: switch XFG to 1, 2, 5 × 10^k (31 classes) and re-measure. If it still fails, stop and report; do not weaken parameters to pass.

### Phase N1 — Shared definitions (C++)

**Task N1-1: Bill classes**

**Files:** create `src/CryptoNoteCore/NotePoolRules.h` and `.cpp`; modify `src/CryptoNoteConfig.h`.

**Interfaces (produced):**
- `enum class NoteAsset : uint8_t { Xfg, Heat, Lp };`
- `const std::vector<uint64_t> XFG_BILL_DENOMINATIONS;` — 21 sizes, atomic units, ascending.
- `uint16_t noteClassId(NoteAsset asset, uint64_t size);` `bool noteClassOf(uint16_t classId, NoteAsset& asset, uint64_t& size);` — ids are assigned once in the spec and never reused.
- `std::vector<uint64_t> splitIntoBills(NoteAsset asset, uint64_t amount);` `uint64_t floorToBillUnit(NoteAsset asset, uint64_t amount);`
- `const uint32_t NOTE_TREE_DEPTH = 32;` `const uint32_t NOTE_ANCHOR_DEPTH = 100;` `const size_t NOTE_MAX_INPUTS = 16;` `const size_t NOTE_MAX_OUTPUTS = 32;`

- [ ] Unit tests `tests/UnitTests/NotePoolRulesTests.cpp`: `ClassIdsAreUniqueAndStable`, `SplitReturnsOnlyBills`, `SplitSumsToInput`, `UnknownClassIdRejected`, `XfgAndLpClassesMatchV11Bills`, `HeatHiddenIsOneClass`.

### Phase N2 — Cryptography crate (Rust)

**Task N2-1: Hashing, keys, notes**

**Files:** create `fuego-notepool/Cargo.toml`, `src/lib.rs`, `src/keys.rs`, `src/note.rs`, `src/tree.rs`, `src/ffi.rs`; add the crate to the CMake build beside `fuego-swapd-adaptor`.

**Interfaces (produced, C ABI):**
- `int np_derive_keys(const uint8_t seed[32], uint32_t diversifier, np_keys* out);`
- `int np_note_commitment(uint16_t class_id, const uint8_t owner_tag[32], const uint8_t rho[32], uint8_t cm[32]);`
- `int np_nullifier(const uint8_t nk[32], uint16_t class_id, uint64_t position, const uint8_t cm[32], uint8_t nf[32]);`
- `int np_tree_append(np_frontier* f, const uint8_t cm[32], uint8_t new_root[32]);`

- [ ] Every function returns an error code; no panic crosses the ABI (`catch_unwind` at each entry). Secret-dependent code is constant time. Secrets are zeroized on drop.
- [ ] Every new dependency goes through the dependency-vetting check before it is added.
- [ ] Tests: the Task N0-2 vectors pass in Rust and through the C ABI from a C++ test; `NullifierDiffersByPosition`; `NullifierDiffersByClass`; `OwnerTagBindsNk`.

**Task N2-2: Note delivery**

**Interfaces (produced):**
- `int np_encapsulate(const uint8_t ek[1184], const uint8_t tx_binding[32], uint8_t ct[1088], uint8_t shared[32]);`
- `int np_decapsulate(const uint8_t dk[], const uint8_t ct[1088], const uint8_t tx_binding[32], uint8_t shared[32]);`
- `int np_rho(const uint8_t shared[32], uint32_t output_index, uint8_t rho[32]);`

- [ ] `tx_binding` is the hash of chain id, version and the transaction's input nullifiers, so a ciphertext cannot be replayed into another transaction.
- [ ] Malformed keys and ciphertexts fail closed. ML-KEM decapsulation of a wrong ciphertext returns a pseudorandom secret; the wallet then finds no matching `cm` and moves on.
- [ ] Tests: `RecipientRecomputesEveryCommitment`, `WrongRecipientFindsNothing`, `CiphertextBoundToTransaction`, known-answer vectors for ML-KEM-768.

### Phase N3 — Prover and verifier

**Task N3-1: Spend circuit**

**Files:** `fuego-notepool/src/circuit.rs`, `src/prover.rs`, `src/verifier.rs`.

**Interfaces (produced):**
- `int np_prove(const np_spend_witness* inputs, size_t n, const uint8_t sighash[32], uint8_t* proof, size_t* proof_len);`
- `int np_verify(const np_public_input* inputs, size_t n, const uint8_t sighash[32], const uint8_t* proof, size_t proof_len);`

- [ ] The proving stack is selected only after review of its zero-knowledge property, soundness bound, transcript binding, prover randomness and audit status. [Winterfell's README](https://github.com/facebook/winterfell/blob/main/README.md) describes that library as unaudited research software without perfect zero knowledge for secret inputs; it is not a release input as it stands.
- [ ] The final verifier contains no elliptic-curve step and no curve-based wrapper.
- [ ] `np_verify` checks proof length, element encodings and parameter fields before any expensive work, and bounds its own time and memory.
- [ ] Verification is deterministic and identical on every supported platform; a cross-platform vector suite runs in CI.

**Task N3-2: One negative test per constraint**
- [ ] For each of the five constraints in 2.3 and each public input: a witness or input that violates only that item, and a test that the proof fails or cannot be produced: `WrongSkRejected`, `WrongNkRejected`, `WrongDiversifierRejected`, `WrongRhoRejected`, `WrongPathRejected`, `WrongPositionRejected`, `WrongClassRejected`, `StaleRootRejected`, `ChangedSighashRejected`, `NullifierFromOtherLeafRejected`; for `HEAT_HIDDEN`: `HeatValueAboveRangeRejected`, `HeatSumMismatchRejected`, `HeatOutputCommitmentWrongValueRejected`, `HeatPublicInOutChangedRejected`.
- [ ] Mutation: remove each constraint from the circuit in turn; its test must fail. A constraint whose removal breaks no test is a defect in the test set.

**Task N3-3: Second verifier, and second proof if chosen (F-7, F-6)**
- [ ] A second verifier written from the spec by a different author, in a separate module. Consensus calls both and accepts only if both accept.
- [ ] Differential fuzzing: random and mutated proofs through both verifiers; any disagreement is a finding.
- [ ] If decision N-3 selects F-6: a second proof system for the same statement, with its own prover, verifier, vectors and the Task N3-2 tests.

### Phase N4 — Consensus state

**Task N4-1: Trees, roots, nullifiers, counters**

**Files:** create `src/CryptoNoteCore/NotePoolState.h` and `.cpp`; modify `src/CryptoNoteCore/Blockchain.h` and `Blockchain.cpp` (block cache serializer, `popBlock`).

**Interfaces (produced):**
```cpp
struct NoteClassState {
  std::array<Crypto::Hash, NOTE_TREE_DEPTH> frontier;
  uint64_t created;
  uint64_t spent;
  std::deque<Crypto::Hash> recentRoots;   // one per block, NOTE_ANCHOR_DEPTH kept
};
struct NoteBlockUndo {
  std::vector<std::pair<uint16_t, NoteClassState>> classesBefore;
  std::vector<Crypto::Hash> nullifiersAdded;
};
bool NotePoolState::isAcceptedRoot(uint16_t classId, const Crypto::Hash& root) const;
bool NotePoolState::hasNullifier(const Crypto::Hash& nf) const;
bool NotePoolState::hasCommitment(uint16_t classId, const Crypto::Hash& cm) const;
uint64_t NotePoolState::unspentCount(uint16_t classId) const;
```

- [ ] Leaves are appended in block order, transaction order, output order. The order is in the spec.
- [ ] `spent > created` for any class rejects the block (F-3).
- [ ] The commitment index used by `hasCommitment` is persisted; it cannot be rebuilt from the frontier.
- [ ] Undo records are kept for `MAX_ROLLBACK_HISTORY` blocks. State rebuilt from genesis equals state loaded from cache.
- [ ] `uint64_t hiddenHeatFloat` in the block cache and undo record; a block that would make it negative is rejected.
- [ ] Tests: `HiddenHeatFloatNeverNegative`, `PopBlockRestoresHiddenHeatFloat`, `AppendChangesRootDeterministically`, `RootHistoryKeepsAnchorDepth`, `DuplicateCommitmentRejected`, `DuplicateNullifierRejected`, `SpentAboveCreatedRejectsBlock`, `PopBlockRestoresEveryClass`, `RebuildFromGenesisMatchesCache`.
- [ ] Mutation: skip the counter check; `SpentAboveCreatedRejectsBlock` must fail.

**Task N4-2: Supply identity**
- [ ] `uint64_t Blockchain::publicSupply(NoteAsset asset) const;` — `Σ size × (created − spent)` plus public protocol balances plus unmigrated legacy outputs.
- [ ] After every block in debug and test builds, assert it equals issuance minus burns for that asset. In release builds a mismatch is logged at ERROR and exposed on `/note_pool_status`.
- [ ] Tests: `SupplyIdentityHoldsAcrossEveryActionType`, `ForgedSpendDoesNotRaiseSupply` (a test hook marks one extra nullifier as spent; the supply figure is unchanged and the class's last honest spend is refused).

### Phase N5 — Transaction family

**Task N5-1: Types and serialization**

**Files:** `include/CryptoNote.h` (variants), `src/CryptoNoteCore/CryptoNoteSerialization.cpp`, `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp` (sighash).

- [ ] Add the 2.4 types under a new transaction version. Older versions serialize and validate exactly as before.
- [ ] Sighash covers every byte except the proofs.
- [ ] Tests: `NoteTransactionRoundTrips`, `UnknownTransactionVersionRejected`, `LegacyTransactionsReserializeIdentically`.

**Task N5-2: Validation in mempool and block**

**Files:** `src/CryptoNoteCore/Blockchain.cpp` (`checkTransactionInputs`, `check_tx_outputs`, `validateSettlement`, `pushTransaction`, `popTransaction`), `src/CryptoNoteCore/Core.cpp`, `src/CryptoNoteCore/TransactionPool.cpp`.

**Interfaces (produced):** `bool Blockchain::checkNoteTransaction(const Transaction& tx, uint32_t height, std::string& error) const;` — one function, called by the mempool and by block validation.

Order of checks, cheapest first:
1. Counts within `NOTE_MAX_INPUTS` and `NOTE_MAX_OUTPUTS`; exactly two deliveries; every `classId` known.
2. No nullifier repeated in the transaction, in the chain, or (mempool) in another pooled transaction.
3. No output commitment already in its class tree or repeated in the transaction.
4. One anchor per input class, each an accepted root.
5. Balance per asset in the clear, including Hearth and protocol flows, with 128-bit sums.
6. Fee at least the minimum and a multiple of 0.0001 XFG.
7. Both verifiers accept the proof (and the second proof, if F-6).

- [ ] The mempool fee policy charges per input and per proof byte, set from the Task N0-3 measurements.
- [ ] Tests: one per numbered check above, each rejecting a transaction that fails only that check; `MempoolAndBlockAgree` (the same transaction set gives the same verdicts on both paths); `ReorgReturnsNotesToUnspent`.
- [ ] Mutation: for each check, remove it; its test must fail.

### Phase N6 — Coinbase

**Task N6-1: Rewards as notes**

**Files:** `src/CryptoNoteCore/Currency.cpp` (`constructMinerTx`, the reward check), `src/CryptoNoteCore/Blockchain.cpp`, `src/CryptoNoteCore/Core.cpp` (`get_block_template`).

- [ ] The coinbase carries `TransactionOutputNote` outputs to the miner's address and one `NoteDelivery`. It has no inputs and no proof.
- [ ] `uint64_t m_rewardDustCarry` (block cache, in the undo record): the reward paid is `floorToBillUnit(reward + carry)`; the rest becomes the new carry.
- [ ] Coinbase notes are spendable after `minedMoneyUnlockWindow`: the wallet may not use an anchor older than the note's unlock height, and consensus rejects an anchor root recorded before it. Because a spend does not name its leaf, this is enforced by placing coinbase leaves into the class tree only at their unlock height.
- [ ] Tests: `CoinbasePaysOnlyBills`, `RewardDustCarriesToNextBlock`, `PopBlockRestoresDustCarry`, `CoinbaseLeafAppendedAtUnlockHeight`, `TotalRewardsPaidPlusCarryEqualsEmission`.

### Phase N7 — Protocol actions

One task per action, in this order, each with its own supply test and each reusing `checkNoteTransaction` for the note side: N7-1 HEAT mint and burn; N7-2 Hearth swap; N7-3 limit deposit and withdraw; N7-4 LP add and remove; N7-5 CD creation and coinbase payout notes; N7-6 treasury fund and dev-fund fee; N7-7 swap escrow fund, claim and refund.

For each:
- [ ] The tag and its consensus effect are unchanged. Only the value legs change from ring inputs and commitment or key outputs to note inputs and note outputs.
- [ ] Consensus-computed payouts (limit proceeds, LP removal, CD payout) are floored to the bill unit; dust goes where the privacy plan's R1-g and A-e already send it.
- [ ] Tests: the action confirms with notes on both sides; `heatInExistence` and `publicSupply` are unchanged across it except by its defined mint or burn; the action's reversal in `popBlock` restores trees, counters, reserves and vaults.
- [ ] N7-5 only: a CD output carries an `ownerTag` and a KEM key from a diversifier used for that CD alone. The payout is one `HEAT_HIDDEN` note of the exact payout value, with the public `rho` of Part 4. Test `CdPayoutNotesAreSpendableOnlyByCdOwner` and `WalletRestoredFromSeedFindsCdPayoutNotes`.

### Phase N8 — Wallets

**Task N8-1: Keys, addresses, backup**

**Files:** `src/Wallet/WalletGreen.cpp`, `src/WalletLegacy/`, `src/SimpleWallet/`, the Valise SDK.

- [ ] Derive `sk` and every KEM key pair from the existing seed with domain-separated hashes, so an existing mnemonic restores pool funds. The legacy keys stay derivable for the bridge.
- [ ] Address encoding with a checksum and a network prefix distinct from legacy addresses. A legacy address is rejected as a pool recipient.
- [ ] Export of incoming view key and full view key as separate files.
- [ ] Tests: `SeedRestoresPoolKeys`, `ViewOnlyWalletCannotBuildSpend`, `FullViewWalletSeesSpends`, `IncomingViewWalletDoesNotSeeSpends`, vectors shared between C++ and the Valise Rust SDK.

**Task N8-2: Scanning**

**Files:** `src/Transfers/TransfersConsumer.cpp`, `src/Transfers/TransfersContainer.cpp`.

- [ ] For each transaction: decapsulate both deliveries with each watched `dk_d`; derive `rho_i` per output index; record outputs whose recomputed `cm` matches. Record the leaf position from the block's append order.
- [ ] Mark a note spent when its `nf` appears on chain.
- [ ] Tests: `ScannerFindsOwnNotesAmongOthers`, `ScannerIgnoresMismatchedCommitment`, `RescanFromSeedFindsAllNotesAndSpends`, `ReorgUnmarksSpentNotes`.

**Task N8-3: Local trees and witnesses**
- [ ] The wallet appends every published commitment to its own copy of each class frontier and keeps the path for each note it owns, updating paths as leaves arrive.
- [ ] New RPC, read only: `/get_note_commitments` returns all commitments for a height range and class. No endpoint takes a single note or position as input.
- [ ] Tests: `LocalRootMatchesNodeRoot`, `WitnessStaysValidAfterNewLeaves`, `NoRpcRevealsWhichNoteTheWalletOwns`.

**Task N8-4: Building transactions**
- [ ] `chooseBills` selects inputs to cover amount plus fee with the fewest notes, prefers classes with more leaves, and never selects a class whose tree has fewer than `NOTE_MIN_CLASS_LEAVES` leaves unless the user confirms a warning (proposed 1,000; decision N-8).
- [ ] The anchor is a root from a few blocks back, chosen from a fixed distribution so it does not mark the wallet.
- [ ] Outputs are shuffled. The second delivery is always present.
- [ ] Proving runs off the UI thread with progress and cancellation. Secrets are wiped after proving.
- [ ] Tests: `WalletNeverEmitsNonBill`, `SelfSendLooksLikeTwoPartyPayment`, `ThinClassTriggersWarning`, `FailedProofLeavesWalletStateUnchanged`.

### Phase N9 — Legacy bridge

**Task N9-1: Preconditions**
- [ ] Confirm the P0 commitment-key fix against its guide: a sender-only and a view-only wallet cannot spend a commitment output created under the current derivation.
- [ ] Confirm from the wallet code how the spend secret is derived from the seed, and correct Part 5 if a hash step exists that a later rescue could use.
- [ ] The R1 ring and asset rules of the privacy plan are live: a ring member's term decides its asset, and no cross-asset credit is possible.

**Task N9-2: Bridge transaction**

**Files:** `src/CryptoNoteCore/Blockchain.cpp`, wallets.

- [ ] A bridge transaction has legacy inputs (validated under the rules of their creation version, including the key-image subgroup check) and only note outputs. Mixed legacy and note inputs are rejected.
- [ ] From pool activation, `check_tx_outputs` rejects key outputs and commitment outputs, except the public outputs Part 4 keeps (CD outputs, swap escrow).
- [ ] Key images and nullifiers are separate sets. A legacy output's key image, once recorded, blocks any second bridge of it.
- [ ] Locked legacy value keeps its lock: a bridged output's leaf is appended at its unlock height, as coinbase leaves are.
- [ ] Wallet: a sweep that bridges in several transactions at random intervals and in bill-sized pieces, and shows which funds are still classical.
- [ ] Tests: `BridgeCreditsExactValueMinusFee`, `BridgedOutputCannotBeSpentAsLegacy`, `LegacyOutputCannotBeBridgedTwice`, `NoNewLegacyOutputsAfterActivation`, `LockedLegacyValueStaysLocked`, `SupplyIdentityHoldsAcrossBridge`.
- [ ] Mutation: skip the key-image check on bridge inputs; `LegacyOutputCannotBeBridgedTwice` must fail.

### Phase N10 — Audits, testnet gate, activation

- [ ] Independent cryptographic review of the spec, the statement and the parameters.
- [ ] Independent audit of the circuit, both verifiers, the consensus integration and the wallet key handling.
- [ ] Multi-node testnet on mainnet parameters, reset for this version: every Part 4 path confirms; a reorg across each path restores state; a wallet restored from seed recovers everything; mempool and block validation agree on a hostile transaction corpus.
- [ ] Adversarial run: malformed and oversized proofs, proof floods, duplicate nullifiers across competing blocks, stale anchors, bridge double-credit attempts.
- [ ] Device and node measurements meet the Task N0-3 gates.
- [ ] A written incident procedure: what operators do on a `/note_pool_status` alarm, and how a class can be halted by a signed-off fork.
- [ ] Only then an activation height. A failed gate means delay, never a reduced parameter.

---

## Part 7 — Recommendations and other points

- **Order of work.** v11 with R1 rings, the v11 privacy tasks and the readiness fixes; then v12 with CDs under option A and, if its gates pass, this pool with hidden HEAT. The pool needs nothing from v11 or v12 to be redone: their bills, public CD outputs and coinbase payouts were shaped to carry over.
- **Start now, in parallel:** Tasks N0-2 and N0-3. The benchmark decides the hash, the second proof and the XFG bill set; everything else waits on it.
- **Do not claim more than is built.** Until the bridge is used, funds are classical. Wallets should show pool and classical balances separately.
- **Network layer.** A post-quantum spend proof does not hide the sender's IP address. Restoring the Dandelion stem phase is separate work and matters as much for practical privacy.
- **Thin classes.** Large bills will be rare for a long time. A 1,000,000 XFG note hides among very few. Holders of large sizes get more cover by holding several smaller bills, at a higher transaction cost.
- **Transaction size and fees.** Proofs are far larger than ring signatures. Block size limits, the reward penalty for block size and the fee floor all need re-deriving from Task N0-3. Expect higher fees per transaction than today.
- **Chain growth.** Nullifiers and commitments are never pruned. Proofs can be pruned from old blocks once nodes trust a checkpoint; that policy is not designed here.
- **Light wallets.** Scanning needs every transaction's two ciphertexts, about 2.2 KB each. A scanning service that holds a view key learns that wallet's incoming notes. There is no private light-wallet scan in this design.
- **Hardware wallets.** Proving on a hardware device is not practical. The usual split applies: the device holds `sk` and authorizes; a host proves. That needs a statement where the host cannot spend on its own, which is extra design work not in this plan.
- **Hidden HEAT is the riskiest part of this plan.** The XFG and LP classes keep supply independent of the proof. `HEAT_HIDDEN` does not: a proof flaw lets a forger take up to the whole hidden float out through Hearth for real XFG, and it shows only when exits would exceed the float. That is why F-6 is required for this class (decision N-11).

## Decisions for the owner

| # | Decision | Position |
|---|---|---|
| N-1 | Benchmark gates | Proposed: 16 inputs in 20 s on the reference phone; 50 ms verification; 250 KB per 16-input transaction. Reference devices to be named |
| N-2 | Tree and note hash | Keccak-family if it passes N-1; a circuit-friendly hash only with independent cryptanalytic review |
| N-3 | Second independent proof (F-6) | Recommended if N-1 still passes with it. Two verifiers (F-7) are the minimum either way |
| N-4 | Generations (F-9) | Off at launch. Revisit when classes hold enough leaves that a generation is still a large set |
| N-5 | Anchor depth | 100 blocks |
| N-6 | Minimum fee | 0.0008 XFG, today's floor; fees are multiples of the 0.0001 XFG bill (owner, 2026-11-18) |
| N-7 | Freeze of unmigrated legacy outputs | Open. Bridge stays open until decided |
| N-8 | Thin-class warning threshold | 1,000 leaves |
| N-9 | XFG bill set | Chosen 2026-11-15: 1, 5 × 10^k. Fallback 1, 2, 5 × 10^k by benchmark only |
| N-10 | Version number for the pool | Targeted at v12 (owner, 2026-11-17). If the pool activates after the v12 CD height, it needs its own number; `BLOCK_MAJOR_VERSION_13` is marked deprecated in `CryptoNoteConfig.h` and should not be reused |
| N-11 | Second independent proof for `HEAT_HIDDEN` | Required (F-6), not optional: a flaw in this class is bounded only by the whole hidden float |
| N-12 | LP notes | Public LP bills as written. Hidden LP amounts would add a second turnstile class; not proposed |
| N-13 | DIGM in the pool | Open. DIGM is not minted until a later release; its pool path is designed with it |

## Verification status and references

This is a plan. Writing it completed no source code, test, audit, benchmark or network upgrade. Code references were read on 2026-11-13 to 2026-11-15 and not executed. External primary sources: [NIST FIPS 203](https://csrc.nist.gov/pubs/fips/203/final), the [original STARK paper](https://eprint.iacr.org/2018/046), and the [Winterfell project status](https://github.com/facebook/winterfell/blob/main/README.md).
