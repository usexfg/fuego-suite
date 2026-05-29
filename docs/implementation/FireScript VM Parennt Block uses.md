# FuegoScript VM — Ecosystem Use Cases & Meshtastic Tag Reservation

**Status:** Design draft.
**Date:** 2026-05-26.
**Builds on:** [`docs/superpowers/specs/2026-05-18-fuegoscript-dao-w1-design.md`](2026-05-18-fuegoscript-dao-w1-design.md) (W1 spec), [`docs/PARENT_BLOCK_V11_COMMITMENT_SCHEME.md`](../../PARENT_BLOCK_V11_COMMITMENT_SCHEME.md) (V11 inner tag catalog).
**Scope:** (1) Reserve parent bundle inner tag `0x0C` for Meshtastic anchoring. (2) Catalog and deep-dive on FuegoScript use cases across the Fuego/DIGM ecosystem beyond DAO governance.

---

## 0. Executive Summary

The V14 FuegoScript VM (W1 spec) ships as a DAO substrate, but its 45-opcode predicate engine is general enough to serve the broader Fuego ecosystem. This document catalogs 17 use cases spanning bonds, AMM, Paradio, audio seeding, atomic swaps, and governance extensions, with deep dives on the 6 highest-impact applications.

Separately, parent bundle inner tag `0x0C` is reserved for future Meshtastic mesh-network anchoring (MSH). No V14 semantics are defined; this is a namespace reservation only.

---

## 1. Inner Tag Reservation: MSH (`0x0C`)

### 1.1 Motivation

Meshtastic LoRa mesh networks can extend Fuego's reach to off-grid environments — users submitting transactions via mesh relay, lightweight block summaries broadcast over LoRa, or mesh topology commitments anchored on-chain. The parent bundle inner tag namespace is the natural home for per-block environmental commitments that miners assert.

### 1.2 Reservation

| inner_tag | name | status | description |
|---|---|---|---|
| `0x0C` | MSH | **reserved** | Meshtastic mesh-network anchor. Semantics TBD in a future spec. |

This follows the precedent of `0x07 SCA` (sidechain/L2 anchoring, reserved in V11 for V12+).

### 1.3 Candidate semantics (for future design)

| Candidate | What it commits | Who writes it | Size estimate |
|---|---|---|---|
| Mesh-relayed tx attestation | Merkle root of txs received via LoRa mesh during this block interval | Gateway node (miner or relay) | 32-36 B |
| Mesh topology snapshot | Hash of (node_count, relay_hops, coverage_area) at block time | Miner with mesh access | 48-64 B |
| LoRa block summary | Compact block header suitable for LoRa relay (hash + height + fee_rate) | Miner | 40-44 B |

### 1.4 Tag vs PCM script

**Decision: dedicated tag, not a PCM script.** Meshtastic data is environmental (comes from hardware/network state, not chain state). A pure-function PCM script cannot produce it — it needs miner-asserted data, like PBK (`0x06`) or DMR (`0x05`). PCM scripts could *verify* the data downstream (e.g., check a signature from a mesh gateway), but the raw commitment needs its own inner tag slot.

### 1.5 V14 action

Add `0x0C MSH` as reserved in the inner tag catalog. No semantics, no validation, no parser code. The byte is claimed to prevent future conflicts.

### 1.6 Updated inner tag catalog

For reference, the full inner tag namespace after this reservation:

| inner_tag | name | version | description |
|---|---|---|---|
| `0x01` | FRC | V11 | HearthAMM fee rate + controller step |
| `0x02` | HSD | V11 | HEAT supply delta |
| `0x03` | PIC | V11 | PI controller state |
| `0x04` | APS | V11 (optional) | AMM pool state merkle root |
| `0x05` | DMR | V11 (epoch) | DIGM merkle root + signature |
| `0x06` | PBK | V11 (optional) | Pool/miner bookkeeping (opaque) |
| `0x07` | SCA | reserved | Sidechain/L2 anchoring |
| `0x08` | PVT | V12 | Paradio hashrate-vote tally |
| `0x09` | CCP | V12 | Chain checkpoint proof (note: `0x09` also exists in the top-level tx_extra namespace as `TX_EXTRA_PARENT_BUNDLE_COMMITMENT`; these are separate namespaces per V11 §5.4) |
| `0x0A` | MCR | V13 | Anti-MEV commit-reveal |
| `0x0B` | PCM | V14 | Programmable commitment (FuegoScript) |
| `0x0C` | MSH | reserved | Meshtastic mesh-network anchor |
| `0x0D`-`0xFF` | — | — | unallocated |

---

## 2. VM Use-Case Catalog

Each use case lists what FuegoScript enables, which opcodes it primarily uses, and whether it's achievable with the V14 opcode set or needs future additions.

| # | Use case | What the VM enables | Key opcodes | V14 ready? |
|---|---|---|---|---|
| 1 | **Bond (YEM) predicates** | Maturity verification, coupon schedules, pricing curves | `OP_BLOCK_HEIGHT`, `OP_MUL`, `OP_DIV`, `OP_VERIFY` | Yes |
| 2 | **HearthAMM custom curves** | Programmable bonding curves beyond built-in constant-product | `OP_MUL`, `OP_DIV`, `OP_LOOP`, `OP_HASH256` | Yes |
| 3 | **LP incentive programs** | Verify LP participation qualifies for reward distribution | `OP_HEAT_CD_BAL`, `OP_BLOCK_HEIGHT`, `OP_VERIFY_SIG` | Yes |
| 4 | **Paradio hashrate requests** | Verify miner's hashrate weight for song request priority | `OP_VERIFY_SIG`, `OP_BLOCK_HASH`, `OP_MUL` | Yes (with PVT V12) |
| 5 | **Paradio royalty splits** | Automated artist/curator/listener revenue distribution | `OP_DIV`, `OP_MUL`, `OP_VERIFY_MULTISIG_K_OF_N` | Yes |
| 6 | **Audio seeding proof-of-relay** | Verify a seeder relayed content chunks via merkle proof | `OP_MERKLE_VERIFY`, `OP_HASH256`, `OP_VERIFY_SIG` | Yes |
| 7 | **Atomic swap SPV proofs** | Verify external chain block headers / tx inclusion in-script | `OP_HASH256`, `OP_MERKLE_VERIFY`, `OP_VERIFY` | Partial (ETH only; others need `OP_SHA256`) |
| 8 | **DAO quadratic voting** | Tally with `sqrt(weight)` instead of linear | `OP_VOTE_TALLY`, `OP_LOOP` (Newton's method) | Yes |
| 9 | **DAO conviction voting** | Time-weighted vote accumulation (longer lock = more weight) | `OP_XFG_LOCKED`, `OP_BLOCK_HEIGHT`, `OP_MUL` | Yes |
| 10 | **DAO delegated voting** | Verify delegation chain (A delegates to B via signed message) | `OP_VERIFY_SIG`, `OP_XFG_LOCKED` | Yes |
| 11 | **CURA burn-to-mint gate** | Verify VOX burn prerequisites before CURA mint authorization | `OP_CURA_BAL`, `OP_DIGM_BAL`, `OP_VERIFY` | Yes |
| 12 | **CD yield curves** | Custom yield calculation beyond built-in linear model | `OP_HEAT_CD_BAL`, `OP_MUL`, `OP_DIV`, `OP_LOOP` | Yes |
| 13 | **Meshtastic anchor verification** | Verify mesh gateway signature on relayed data | `OP_VERIFY_SIG`, `OP_MERKLE_VERIFY` | After MSH tag lands |
| 14 | **Custom vesting schedules** | Time-locked release with cliff, linear, or step curves | `OP_BLOCK_HEIGHT`, `OP_DIV`, `OP_MIN`, `OP_MAX` | Yes |
| 15 | **Multi-party escrow** | Release funds when M-of-N parties sign + conditions met | `OP_VERIFY_MULTISIG_K_OF_N`, `OP_BLOCK_HEIGHT`, `OP_VERIFY` | Yes |
| 16 | **Oracle-free price feeds** | Use on-chain AMM pool state as price reference for scripts | `OP_HASH256` + APS data | V14 if `OP_AMM_RESERVES` added |
| 17 | **Content licensing predicates** | DIGM listen-rights transfer conditions (region, time, count) | `OP_DIGM_BAL`, `OP_BLOCK_HEIGHT`, `OP_VERIFY_SIG` | Yes |

### 2.1 Opcode coverage analysis

Of the 17 use cases:
- **14 are fully expressible** with the V14 opcode set (45 opcodes)
- **2 would benefit from new V14.x opcodes** (`OP_SHA256` for non-ETH SPV proofs, `OP_AMM_RESERVES` for direct pool reads)
- **1 depends on a future inner tag** (Meshtastic anchor verification needs MSH `0x0C`)

The V14 opcode set is well-designed for the current ecosystem. The two potential additions (`OP_SHA256`, `OP_AMM_RESERVES`) are each a single opcode that could land in V14.1 without VM redesign — they fit in the reserved opcode gaps (`0x46` and `0x64` respectively).

---

## 3. Deep Dive: Bonds (YEM)

### 3.1 Concept

YEM bonds are fixed-term instruments where users lock XFG and receive a yield at maturity. Unlike CDs (which are HEAT-denominated with a built-in yield curve), YEM bonds are XFG-denominated with programmable terms: custom duration, coupon schedules, early withdrawal penalties, and pricing curves — all verifiable on-chain via PCM scripts.

### 3.2 Bond lifecycle

```
Height H:          User locks XFG via TX_EXTRA_XFG_LOCK (lock_purpose = 1, bond)
                   Bond terms committed: maturity_height, face_value, coupon_rate
Heights H..H+M:   Bond is active; XFG is locked, user earns coupons per epoch
Height H+M:        PCM tally script commits maturity status
                   User unlocks XFG + accumulated coupon yield
Early exit:        User can exit before maturity at a penalty (graduated: less penalty
                   as maturity approaches)
```

### 3.3 What the VM computes

A bond PCM script commits one of three states per block for a given bond:

1. **Active** — bond hasn't matured, no payout due
2. **Coupon due** — current height is at a coupon epoch boundary, yield is payable
3. **Matured** — bond has reached maturity, full face value + final coupon is payable

The script also handles early withdrawal: if the user has spent the locked output (breaking the lock), it computes the penalty and commits the reduced payout.

### 3.4 Example script: maturity + graduated penalty (~180 B)

```
OP_BLOCK_HEIGHT                    # current height
OP_PUSH32 <maturity_height>        # when bond matures
OP_GE                              # has matured?
OP_IF
  # Full payout: face value + coupon
  OP_PUSH32 <bond_amount>          # face value (atomic units)
  OP_PUSH32 <coupon_rate>          # e.g., 500 = 5.00%
  OP_MUL OP_PUSH8 100 OP_DIV      # bond_amount * rate / 100
  OP_ADD                           # face + coupon = total payout
OP_ELSE
  # Early exit: graduated penalty
  # penalty = (remaining / total) * max_penalty
  OP_BLOCK_HEIGHT
  OP_PUSH32 <start_height>
  OP_SUB                           # elapsed = current - start
  OP_PUSH32 <total_duration>       # maturity - start
  OP_SWAP                          # stack: total, elapsed
  OP_SUB                           # remaining = total - elapsed
  OP_PUSH8 10                      # max_penalty = 10%
  OP_MUL
  OP_PUSH32 <total_duration>
  OP_DIV                           # penalty_pct = remaining/total * 10
  OP_PUSH32 <bond_amount>
  OP_DUP                           # stack: ..., amount, amount
  OP_PICK 2                        # stack: ..., amount, amount, penalty_pct
  OP_MUL OP_PUSH8 100 OP_DIV      # penalty_amount = amount * pct / 100
  OP_SUB                           # payout = amount - penalty
OP_ENDIF
OP_RETURN                          # commit payout amount
```

### 3.5 Coupon schedules (advanced, ~250 B)

Periodic coupon payments can be verified by checking `current_height mod coupon_epoch == 0`:

```
OP_BLOCK_HEIGHT
OP_PUSH32 <coupon_epoch>           # e.g., 900 blocks (~5 days)
OP_MOD
OP_PUSH8 0
OP_EQ                              # is this a coupon boundary?
OP_IF
  OP_PUSH32 <bond_amount>
  OP_PUSH32 <per_epoch_rate>       # e.g., 50 = 0.50% per epoch
  OP_MUL OP_PUSH8 100 OP_DIV      # coupon amount
OP_ELSE
  OP_PUSH8 0                       # no coupon due
OP_ENDIF
OP_RETURN
```

### 3.6 Prerequisites

- Reuses `TX_EXTRA_XFG_LOCK` with `lock_purpose = 1` (bond) to distinguish from DAO voting locks (`lock_purpose = 0`)
- Bond terms (maturity height, coupon rate, face value) are encoded in the PCM script bytecode itself — no additional on-chain state needed
- A separate `TX_EXTRA_YEM_BOND` tag could be added for richer metadata, but `lock_purpose = 1` is sufficient for V14

### 3.7 New opcodes needed

None. Fully expressible with V14 arithmetic + control flow.

---

## 4. Deep Dive: HearthAMM Extensions

### 4.1 Concept

HearthAMM ships with a built-in constant-product (`x * y = k`) curve. The VM lets anyone define custom bonding curves, LP reward programs, and oracle-free price references as PCM scripts — without consensus changes per curve type.

### 4.2 Custom bonding curves

Beyond `x * y = k`, useful curve shapes include:

**Concentrated liquidity** — higher capital efficiency in a target price range. The script verifies that a proposed swap price falls within the active range and uses a steeper curve within that range:

```
# Piecewise linear approximation of concentrated liquidity
OP_PUSH32 <current_price>          # from reserve ratio
OP_DUP
OP_PUSH32 <range_low>
OP_GE OP_VERIFY                    # price >= range_low
OP_PUSH32 <range_high>
OP_LE OP_VERIFY                    # price <= range_high
# Within range: use concentrated slope
OP_PUSH32 <concentrated_k>        # tighter k for this range
OP_RETURN
```

**Sigmoid curve for stablecoin pairs** — price stays flat near peg, steepens at boundaries. Approximated via 3-segment piecewise linear:

```
OP_PUSH32 <reserve_ratio>
OP_DUP
OP_PUSH32 <low_threshold>          # e.g., 0.95 peg
OP_LT
OP_IF
  OP_PUSH32 <low_slope>            # flat bottom: heavy sell pressure
  OP_MUL
OP_ELSE
  OP_DUP
  OP_PUSH32 <high_threshold>       # e.g., 1.05 peg
  OP_GT
  OP_IF
    OP_DROP
    OP_PUSH32 <price_ceiling>      # flat top: heavy buy pressure
  OP_ELSE
    OP_PUSH32 <mid_slope>          # steep middle: normal trading
    OP_MUL
  OP_ENDIF
OP_ENDIF
OP_RETURN
```

### 4.3 Oracle-free price feeds

Since the APS inner tag (`0x04`) commits the AMM pool state merkle root every block, scripts can derive prices from on-chain reserve ratios without external oracles. Any script that needs a price reference (bond pricing, liquidation thresholds, swap rate verification) can read the AMM state.

**Current limitation:** reading APS requires merkle-proving pool reserves from the APS root, which is verbose in bytecode. A dedicated `OP_AMM_RESERVES` opcode (`0x64`, gas 20) that directly returns `(reserve_a, reserve_b)` for a given pair ID would make this practical:

```
OP_PUSH8 <pair_id>                 # e.g., 0 = XFG/SOL
OP_AMM_RESERVES                    # push reserve_a, reserve_b
OP_DIV                             # price = reserve_a / reserve_b
```

**Recommendation:** add `OP_AMM_RESERVES` to the V14.x opcode expansion list. It slots into the chain-state gap at `0x64` (between `OP_CURA_BAL` at `0x63` and `OP_PROPOSAL_GET` at `0x70`).

### 4.4 LP incentive verification

A PCM script can verify that an LP position qualifies for bonus rewards:

```
# Check LP has been in pool for at least min_duration
OP_PUSH32 <lp_entry_height>        # when LP deposited
OP_BLOCK_HEIGHT
OP_SWAP OP_SUB                     # blocks_in_pool = current - entry
OP_PUSH32 <min_duration>           # e.g., 900 blocks (1 epoch)
OP_GE OP_VERIFY                    # must have been in pool long enough

# Check LP amount meets minimum
OP_PUSH32 <lp_amount>
OP_PUSH32 <min_lp_amount>
OP_GE OP_VERIFY

# Compute bonus: amount * bonus_rate * duration_multiplier
OP_PUSH32 <lp_amount>
OP_PUSH32 <bonus_rate_bps>         # basis points
OP_MUL
OP_PUSH8 100 OP_MUL               # scale bps to match
OP_PUSH32 <blocks_in_pool>
OP_MUL
OP_PUSH32 <max_duration>
OP_DIV                             # duration multiplier
OP_RETURN                          # commit bonus amount
```

---

## 5. Deep Dive: Paradio

### 5.1 Concept

Paradio is DIGM's gamified radio (architecture guide §4a). Miners vote on the next track via hashrate, users burn VOX for song requests, artists earn PARA per stream, and curators earn a revenue share for successful picks. The VM can verify request priority, enforce royalty distribution, and connect hashrate voting to on-chain governance.

### 5.2 Hashrate-weighted song requests

A miner's request priority is weighted by their recent block production. A PCM script verifies:

1. The requester actually mined recent blocks (signature over block hash)
2. Their priority score reflects blocks mined in the last N blocks
3. VOX burn amount adds additional weight

```
# Verify requester mined a recent block
OP_PUSH32 <requester_pubkey>
OP_PUSH32 <request_sig_lo>
OP_PUSH32 <request_sig_hi>
OP_PUSH8 3                         # check block at height-3
OP_BLOCK_HASH                      # hash of block[h-3]
# sig message = block_hash (the sig proves requester produced that block)
OP_VERIFY_SIG                      # trap if requester didn't mine it

# Priority = 1 (base for miners) + vox_burned
OP_PUSH8 1                         # base mining priority
OP_PUSH32 <vox_burned>             # VOX burn adds weight
OP_ADD                              # total priority score
OP_RETURN                          # commit priority
```

Gas cost: ~105 (1 `OP_VERIFY_SIG` at 100 + arithmetic). Well within budget.

After V12 lands PVT (tag `0x08`), a future `OP_PVT_WEIGHT` opcode could directly read a miner's hashrate weight from the rolling 64-block window, replacing the manual block-hash signature check.

### 5.3 Royalty distribution predicates

When a song plays, the correct revenue split must be enforced: artist X%, curator Y%, listener reward pool Z%. A PCM script verifies a proposed distribution:

```
# Verify split adds up to 100%
OP_PUSH8 <artist_pct>              # e.g., 40
OP_PUSH8 <curator_pct>             # e.g., 30
OP_ADD
OP_PUSH8 <listener_pool_pct>       # e.g., 30
OP_ADD
OP_PUSH8 100
OP_EQ OP_VERIFY                    # must sum to 100

# Verify artist gets at least minimum
OP_PUSH8 <artist_pct>
OP_PUSH8 <min_artist_pct>          # e.g., 30 (floor)
OP_GE OP_VERIFY

# Verify curator has CURA balance (earned curation rights)
OP_PUSH32 <curator_pubkey>
OP_CURA_BAL
OP_PUSH8 0
OP_GT OP_VERIFY                    # curator must hold CURA

# Commit the verified split
OP_PUSH8 <artist_pct>
OP_PUSH8 <curator_pct>
OP_PUSH8 <listener_pool_pct>
OP_HASH256                         # commitment = hash(split)
OP_RETURN
```

### 5.4 Dynasty / reign-tax logic

The architecture guide §4a describes the "stays #1" problem. A PCM script can implement the dynasty + ride-the-wave model:

- **Reign-tax:** the longer an album stays #1, the higher the tax on its staking pool (reducing effective yield, incentivizing rotation)
- **Late-entry stake adjustment:** stakes placed after an album is already #1 get reduced weight

These are naturally expressed as `OP_BLOCK_HEIGHT` arithmetic: compute epochs_at_top, apply tax multiplier via `OP_MUL` / `OP_DIV`.

---

## 6. Deep Dive: Audio Seeding Proof-of-Relay

### 6.1 Concept

DIGM §7 defines audio seeding — P2P content distribution where nodes relay audio chunks. Currently seeders do not earn PARA (architecture guide decision). The VM could change this by enabling verifiable proof-of-relay: seeders who demonstrably served content get on-chain credit, opening the door to future seeder incentives.

### 6.2 What the VM verifies

A proof-of-relay consists of three claims:

1. **Content integrity** — the relayed chunk is part of the track's content tree (merkle proof)
2. **Listener receipt** — a listener signed an acknowledgment that they received the chunk from this seeder
3. **Seeder identity** — the seeder signed a relay attestation

### 6.3 Example script: single-relay proof (~300 B)

```
# 1. Verify chunk is part of track's content tree
OP_PUSH32 <chunk_hash>
OP_PUSH32 <merkle_sibling_1>
OP_PUSH32 <merkle_sibling_2>
OP_PUSH32 <content_root>           # track's content merkle root (from DMR subtree)
OP_MERKLE_VERIFY                   # trap if chunk isn't in tree

# 2. Verify listener acknowledged receipt
#    ack_msg = hash(chunk_hash || seeder_pubkey || timestamp)
OP_PUSH32 <ack_msg_hash>
OP_PUSH32 <listener_sig_lo>
OP_PUSH32 <listener_sig_hi>
OP_PUSH32 <listener_pubkey>
OP_VERIFY_SIG                      # listener confirms receipt

# 3. Verify seeder identity
#    relay_msg = hash(chunk_hash || listener_pubkey || timestamp)
OP_PUSH32 <relay_msg_hash>
OP_PUSH32 <seeder_sig_lo>
OP_PUSH32 <seeder_sig_hi>
OP_PUSH32 <seeder_pubkey>
OP_VERIFY_SIG                      # seeder confirms relay

# Commit: seeder proved relay of one chunk
OP_PUSH32 <seeder_pubkey>
OP_PUSH32 <chunk_hash>
OP_HASH256                         # commitment = hash(seeder || chunk)
OP_RETURN
```

### 6.4 Gas analysis

| Operation | Gas |
|---|---|
| `OP_MERKLE_VERIFY` | 30 |
| `OP_VERIFY_SIG` x2 | 200 |
| `OP_HASH256` | 30 |
| Stack ops + pushes | ~20 |
| **Total** | **~280** |

A single relay proof uses ~280 gas out of 10,000 — leaving room for ~35 relay proofs in one script execution. But bytecode size is the real constraint: each proof needs ~300 B of push data (hashes, signatures, keys), so a single proof fills most of the 1024 B bytecode cap.

### 6.5 Scaling path

| Approach | V14 | V15 (Cairo) |
|---|---|---|
| Single relay proof per block | Yes (280 gas, ~300 B) | Yes |
| Batch: 10 relay proofs | No (exceeds 1024 B bytecode) | Yes (script registry + larger bytecode) |
| ZK aggregate: prove 1000 relays | No | Yes (STARK proves N verifications in one proof) |

**Recommendation:** V14 ships single-relay verification as a proof of concept. The real payoff is V15 Cairo: a single STARK proves 1000 relay verifications, making seeder incentives economically viable. This is one of the strongest arguments for the Cairo migration path.

### 6.6 Impact on seeder incentive decision

The architecture guide decided "seeders do not earn PARA" because there was no way to verify relay without trusting the seeder. With proof-of-relay:

- V14: seeders can earn **reputation** (on-chain verified relay count), even if not PARA
- V15: seeders can earn **PARA** (batch ZK proofs make per-chunk verification cheap enough for incentives)

This doesn't override the architecture guide decision — it provides new technical capability that could inform a future revision.

---

## 7. Deep Dive: Atomic Swaps / Cross-Chain

### 7.1 Concept

Fuego currently does atomic swaps via COMIT + MuSig2 adaptor signatures across SOL, ETH, XMR, and BCH. The swap state machine (SwapDaemon) handles the protocol off-chain. The VM can add on-chain verification layers that reduce trust assumptions.

### 7.2 What the VM enables

**SPV proof verification** — verify that a counterparty's chain actually locked funds by checking a block header + tx inclusion merkle proof inside a FuegoScript. Currently, swaps trust the SwapDaemon to monitor counterparty chains. SPV proofs make this verification reproducible and auditable on-chain.

**Conditional swap predicates** — complex swap conditions beyond simple time-locks:
- "Release XFG only if ETH was locked within 5% of the agreed exchange rate" (price protection)
- "Release XFG only if the counterparty lock has at least N confirmations on their chain"
- "Refuse swap if AMM price diverges too far from the swap rate" (anti-arbitrage)

**Swap fee enforcement** — verify the 1% swap fee (`SWAP_FEE_RATE_BPS = 100`) and 80/20 CD/treasury split are correctly applied:

```
OP_PUSH32 <swap_amount>
OP_PUSH8 100                       # fee rate BPS
OP_MUL
OP_PUSH32 <10000 as u256>           # BPS denominator (>255, needs PUSH32)
OP_DIV                             # fee = amount * 100 / 10000

# Verify CD share = 80%
OP_DUP
OP_PUSH8 80 OP_MUL
OP_PUSH8 100 OP_DIV
OP_PUSH32 <claimed_cd_share>
OP_EQ OP_VERIFY

# Verify treasury share = 20%
OP_PUSH8 20 OP_MUL
OP_PUSH8 100 OP_DIV
OP_PUSH32 <claimed_treasury_share>
OP_EQ OP_VERIFY
```

### 7.3 ETH SPV proof verification (~400 B, illustrative)

Ethereum uses keccak-256, which matches Fuego's `cn_fast_hash` (both are keccak-256). This means `OP_HASH256` can hash ETH data natively.

**Important caveat:** Ethereum's transaction trie is an RLP-encoded Modified Merkle Patricia Trie, not a simple binary merkle tree. The `OP_MERKLE_VERIFY` opcode assumes standard binary merkle proofs. Full ETH tx inclusion verification would require either: (a) a simplified proof format produced by an off-chain prover that reduces the Patricia trie proof to a binary merkle proof, (b) a future `OP_MPT_VERIFY` opcode for Patricia trie proofs, or (c) V15 Cairo which can implement arbitrary trie traversal. The script below is **illustrative** — it shows the verification flow assuming a pre-processed binary proof:

```
# Verify ETH block header hash
OP_PUSH32 <eth_block_header_hash>
OP_PUSH32 <expected_eth_block_hash>  # from swap agreement
OP_EQ OP_VERIFY

# Verify tx inclusion in ETH block via merkle proof
OP_PUSH32 <eth_tx_hash>
OP_PUSH32 <merkle_path_1>
OP_PUSH32 <merkle_path_2>
OP_PUSH32 <eth_tx_root>             # txRoot from ETH block header
OP_MERKLE_VERIFY

# Verify lock amount matches agreement
OP_PUSH32 <locked_eth_amount>
OP_PUSH32 <agreed_eth_amount>
OP_GE OP_VERIFY                      # locked >= agreed

# Verify swap is still within time window
OP_BLOCK_HEIGHT
OP_PUSH32 <swap_deadline_height>
OP_LT OP_VERIFY

# Commit swap verification result
OP_PUSH32 <swap_id>
OP_HASH256
OP_RETURN
```

### 7.4 Chain compatibility matrix

| Chain | Hash function | V14 `OP_HASH256` compatible? | SPV feasible? |
|---|---|---|---|
| ETH | keccak-256 | **Yes** (exact match) | **Partial, V14** — hash matches, but Patricia trie needs simplified proof or `OP_MPT_VERIFY` |
| SOL | SHA-256 | No | V14.x with `OP_SHA256` |
| BCH | double-SHA-256 | No | V14.x with `OP_SHA256` |
| XMR | CryptoNote (ring sigs) | N/A | **No** — CryptoNote tx structure doesn't support standard SPV inclusion proofs |

### 7.5 Recommended new opcode: `OP_SHA256`

To support SOL and BCH SPV proofs, add `OP_SHA256` at opcode byte `0x46` (next slot after `OP_MERKLE_VERIFY` at `0x45`), gas cost 30 (same as `OP_HASH256`).

This is a single-opcode addition for V14.x that unlocks 2 of the 4 swap chains for on-chain SPV verification.

### 7.6 XMR exception

XMR atomic swaps cannot benefit from SPV proof verification because CryptoNote ring signatures make transaction inclusion proofs fundamentally different from transparent chains. The XMR swap path remains trust-the-SwapDaemon. This is an inherent CryptoNote privacy trade-off, not a VM limitation.

---

## 8. Deep Dive: DAO Governance Extensions

### 8.1 Concept

V14 ships linear voting (weight = locked XFG). The VM makes alternative voting models possible as custom tally scripts — each proposal can specify its own tally bytecode in the PCM tag. No consensus changes needed per voting model.

### 8.2 Quadratic voting

**What:** `vote_weight = sqrt(locked_amount)`. Reduces plutocracy. A whale with 1,000,000 XFG gets 1,000 votes (not 1,000,000). A user with 100 XFG gets 10 votes. The ratio between them is 100x instead of 10,000x.

**Implementation:** Newton's method square root via `OP_LOOP`:

```
# Newton's method sqrt(n):
#   guess = n / 2
#   iterate: guess = (guess + n / guess) / 2
#   10 iterations converges for any u256

OP_PUSH32 <locked_amount>          # n
OP_DUP
OP_PUSH8 2 OP_DIV                  # initial guess = n / 2

OP_LOOP 10                         # 10 iterations
  # stack: n, guess
  OP_SWAP                          # stack: guess, n
  OP_DUP                           # stack: guess, n, n
  OP_PICK 2                        # stack: guess, n, n, guess
  OP_DIV                           # stack: guess, n, n/guess
  OP_PICK 2                        # stack: guess, n, n/guess, guess
  OP_ADD                           # stack: guess, n, (n/guess + guess)
  OP_PUSH8 2 OP_DIV               # stack: guess, n, new_guess
  OP_SWAP OP_DROP OP_SWAP          # stack: new_guess, n
OP_ENDLOOP

OP_DROP                            # drop n, keep sqrt
```

Gas cost: ~147 gas total (5 for setup + `1 + 10 * 14 = 141` for the loop + 1 for cleanup). Well within budget.

A quadratic tally script aggregates `sqrt(weight)` per voter instead of raw weight, then checks quorum and threshold as usual.

### 8.3 Conviction voting

**What:** Time-weighted vote accumulation. The longer your XFG-Lock has been active, the more weight your vote carries. This rewards long-term commitment over flash-locking.

**Formula:**

```
conviction_weight = amount * time_factor
time_factor = (lock_until_height - current_height) / MAX_LOCK_DURATION
```

A voter who locked for 8,000 blocks (nearly the full `MAX_VOTING_WINDOW`) gets nearly full weight. A voter who locked for 100 blocks gets 1/80th weight.

**Script snippet:**

```
# Read voter's lock weight
OP_PUSH32 <voter_pubkey>
OP_XFG_LOCKED                      # raw amount

# Compute time factor
OP_PUSH32 <lock_until_height>
OP_BLOCK_HEIGHT
OP_SUB                             # remaining = lock_end - now
OP_PUSH32 <MAX_LOCK_DURATION>      # e.g., 8192
OP_MIN                             # cap at max
OP_MUL                             # amount * remaining
OP_PUSH32 <MAX_LOCK_DURATION>
OP_DIV                             # conviction_weight

OP_RETURN
```

### 8.4 Delegated voting

**What:** Voter A signs a delegation message authorizing voter B to vote with A's weight. The tally script verifies the delegation chain.

**Delegation message format:**

```
delegate_msg = "fuego-dao-delegate-v1"
            || u32_le(network_id)
            || proposal_id (32 B)
            || delegator_pubkey (32 B)
            || delegate_pubkey (32 B)
```

**Script verifies:**

```
# Verify A delegated to B
OP_PUSH32 <delegate_msg_hash>
OP_PUSH32 <delegator_sig_lo>
OP_PUSH32 <delegator_sig_hi>
OP_PUSH32 <delegator_pubkey>       # A
OP_VERIFY_SIG                      # A signed the delegation

# Read A's locked weight
OP_PUSH32 <delegator_pubkey>
OP_XFG_LOCKED                      # A's weight

# Add to B's tally (script pushes combined weight)
OP_PUSH32 <delegate_pubkey>
OP_XFG_LOCKED                      # B's own weight
OP_ADD                             # combined = A's + B's
OP_RETURN
```

Gas cost: 100 (sig verify) + 20 + 20 (two XFG_LOCKED reads) + misc = ~150 gas per delegation hop.

**Depth cap:** with 10K gas budget, delegation chains can be ~50 hops deep (150 gas each). In practice, cap at 3-5 hops for sanity.

### 8.5 Voting model comparison

| Model | Sybil resistance | Plutocracy resistance | Complexity | Gas cost |
|---|---|---|---|---|
| Linear (V14 default) | Amount-based | None | Trivial | ~50 |
| Quadratic | Amount-based | Strong | Loop (Newton) | ~200 |
| Conviction | Amount + time | Moderate | Arithmetic | ~100 |
| Delegated | Amount-based | None (transfers power) | Sig verify chain | ~150/hop |

All four are PCM-script tally functions. A proposal creator chooses which model to use by specifying different bytecode in their PCM tag — no governance-level changes needed.

---

## 9. Potential V14.x Opcode Additions

Based on the use-case analysis, two opcodes would significantly expand the VM's reach without requiring a hard fork (they fill reserved gaps in the opcode byte space):

| Opcode | Byte | Gas | Description | Unlocks |
|---|---|---|---|---|
| `OP_SHA256` | `0x46` | 30 | SHA-256 hash (pop input, push hash) | SOL + BCH SPV proofs (§7) |
| `OP_AMM_RESERVES` | `0x64` | 20 | Pop pair_id, push (reserve_a, reserve_b) | Oracle-free price feeds (§4), bond pricing (§3) |

These are **soft-fork compatible** if introduced as new opcodes that were previously invalid (trapping). Existing scripts don't use these bytes; new scripts opt in. Whether this counts as a soft fork or requires V14.1 depends on how strictly the "unknown opcode = trap = block rejected" rule is applied — if a V14.0 node encounters `0x46` it traps, rejecting the block, so V14.1 nodes must be a majority before scripts using these opcodes are safe to deploy.

---

## 10. Open Questions

1. **YEM bond denomination** — are bonds XFG-denominated (this doc assumes yes) or could they be HEAT-denominated or multi-asset?
2. **Seeder incentive policy** — does proof-of-relay in V14 change the "seeders don't earn PARA" decision? Or is reputation-only the right V14 stance?
3. **`OP_SHA256` activation** — soft fork or hard fork? Depends on network upgrade policy for new opcodes in reserved slots.
4. **`OP_AMM_RESERVES` interface** — does it return two u256 values (reserve_a, reserve_b) or a single packed u256? Two values means the opcode pushes twice, which is unusual but more ergonomic.
5. **Delegation depth cap** — should this be a consensus parameter or per-script configurable? Per-script is more flexible but harder to reason about gas limits.
6. **Meshtastic semantics** — which candidate semantic (§1.3) to pursue? Needs input from the mesh networking side.
7. **PVT opcode** — after V12 PVT tag lands, is `OP_PVT_WEIGHT` worth adding as a chain-state opcode, or should Paradio scripts verify hashrate manually via block-hash signatures?

---

## 11. References

Note: specs marked *(branch)* live on `claude/nifty-wing-af39df`, not yet merged to `hearth`.

- [`docs/superpowers/specs/2026-05-18-fuegoscript-dao-w1-design.md`](2026-05-18-fuegoscript-dao-w1-design.md) *(branch)* — W1 spec (VM, opcodes, XFG-Lock, DAO tags)
- [`docs/superpowers/specs/2026-05-18-parent-block-v12-plus-roadmap-design.md`](2026-05-18-parent-block-v12-plus-roadmap-design.md) *(branch)* — V12+ roadmap (PVT, CCP, MCR)
- [`docs/PARENT_BLOCK_V11_COMMITMENT_SCHEME.md`](../../PARENT_BLOCK_V11_COMMITMENT_SCHEME.md) *(branch)* — V11 inner tag catalog
- [`docs/DIGM_FUEGO_ARCHITECTURE_GUIDE.md`](../../DIGM_FUEGO_ARCHITECTURE_GUIDE.md) — ecosystem architecture (Paradio §4a, seeding §7, CURA §2)
- `src/SwapDaemon/SwapTypes.h` — swap state machine (COMIT + MuSig2 adaptor sigs)
- `src/CryptoNoteConfig.h` — swap fee constants (`SWAP_FEE_RATE_BPS`, `SWAP_FEE_CD_SHARE_PCT`)
