# Legacy Bond Staged Unlock — Implementation Plan

> v1.11.00 | supersedes the "wait 72 epochs, claim everything" model

---

## Architecture

Two independent claim paths, both funded from `m_legacyBondYieldPool`:

| Path | Command | Fee | Releases |
|---|---|---|---|
| **Interest-only** | `claim_bond_interest <id>` | None | Accrued interest (quarterly or on-demand) |
| **Staged principal** | `claim_bond_stage <id>` | 1% on interest | 25% principal + accrued interest |

### Lifecycle

```
Epoch 0:  migrate_deposit → bond registered
            └─ Prompt: "Enable staged principal unlocks? (1% fee on interest per stage)"
               Yes: bond.useStagedUnlock = true → 4-stage schedule created
               No:  principal locked until epoch 72

Epoch 18: Stage 1 matures (if staged). Interest accrues (all bonds).
            claim_bond_interest → free, any bond at any time
            claim_bond_stage   → 25% principal + interest − 1% fee on interest

Epoch 36: Stage 2
Epoch 54: Stage 3  
Epoch 72: Stage 4 + final maturity — remaining principal + interest released
```

### Fee flow per staged claim

```
  Principal due:   63,562 XFG (25% of 254,250)
  Interest accrued: 15,891 XFG (quarterly at 25% APY, or whatever pool has)
  − 1% service fee:    159 XFG ──→ m_yemState.yemReserve
  − network tx fee:     ~1 XFG ──→ miner
  Net to holder:   79,293 XFG
```

---

## Files Changed

### 1. `CryptoNoteConfig.h` — constants

```cpp
// Bond staged unlocking (v1.11.00)
constexpr uint32_t BOND_STAGED_TOTAL_STAGES    = 4;
constexpr uint32_t BOND_STAGED_INTERVAL_EPOCHS = 18;    // quarterly (72/4)
constexpr uint64_t BOND_STAGED_SERVICE_FEE_BPS = 100;   // 1% on interest → YEM Reserve
constexpr uint64_t BOND_STAGED_PRINCIPAL_PCT   = 25;    // 25% per stage
```

### 2. `TransactionExtra.h` — flag on bond struct

```cpp
struct TransactionExtraLegacyBond {
  Crypto::Hash originalTxHash;
  uint64_t amount;
  uint32_t originalCreationHeight;
  bool     useStagedUnlock;            // NEW — v1.11.00

  bool serialize(ISerializer& serializer);
};
```

Wire format bump (backward-compatible):
- **Write**: append 1 byte `useStagedUnlock` after the 44 existing bytes → 45 bytes total
- **Read**: if ≥ 45 bytes in stream, read the flag; otherwise default `false`

### 3. `TransactionExtra.cpp` — serialize changes

**`addLegacyBondToExtra()`**: append `uint8_t(useStagedUnlock ? 1 : 0)` after height bytes.

**`parseTransactionExtra()` (0xCB case)**: check remaining bytes after height parse. If 1 byte remains, read as bool. If 0, default false.

### 4. `Blockchain.h` — staged bond state

```cpp
// Bond staged unlock tracking (v1.11.00)
struct BondStageSlot {
    Crypto::Hash depositTxHash;      // original deposit tx
    uint32_t     stageNumber;        // 1-4
    uint32_t     maturityEpoch;      // epoch when this stage unlocks
    uint64_t     principalAmount;    // 25% of bond amount
    bool         claimed;            // already withdrawn?
};

std::unordered_map<Crypto::Hash, std::vector<BondStageSlot>> m_stagedBondSchedules;
// key = deposit tx hash → 4 stage slots (created at bond registration)
```

### 5. `Blockchain.cpp` — pushTransaction (0xCB handler)

After `m_totalLegacyBondLocked += bond.amount`:

```cpp
if (bond.useStagedUnlock) {
    uint32_t epochDuration = ...;  // from CryptoNoteConfig
    uint32_t currentEpoch   = block.height / epochDuration;
    uint32_t baseEpoch      = currentEpoch;  // bond is registered now
    
    std::vector<BondStageSlot> slots;
    for (uint32_t i = 0; i < BOND_STAGED_TOTAL_STAGES; ++i) {
        BondStageSlot slot;
        slot.depositTxHash  = bond.originalTxHash;
        slot.stageNumber    = i + 1;
        slot.maturityEpoch  = baseEpoch + BOND_STAGED_INTERVAL_EPOCHS * (i + 1);
        slot.principalAmount = (bond.amount * BOND_STAGED_PRINCIPAL_PCT) / 100;
        slot.claimed        = false;
        slots.push_back(slot);
    }
    m_stagedBondSchedules[bond.originalTxHash] = slots;
    
    logger(DEBUGGING) << "Bond staged: " << Common::podToHex(bond.originalTxHash)
                      << " 4 stages, first matures epoch " << slots[0].maturityEpoch;
}
```

### 6. `Blockchain.cpp` — validateBondStageClaim (new)

Called from `pushTransaction` when a tx spends a bond input with `0xCC` AND `0xCD` tags (indicating staged withdrawal):

```cpp
bool Blockchain::validateBondStageClaim(
    const Crypto::Hash& depositTxHash,
    uint32_t stageNumber,
    uint64_t claimedPrincipal,
    uint64_t claimedInterest
) {
    auto it = m_stagedBondSchedules.find(depositTxHash);
    if (it == m_stagedBondSchedules.end())
        return false;  // not a staged bond

    uint32_t currentEpoch = getCurrentBlockchainHeight() / 
        (isTestnet() ? parameters::TESTNET_EPOCH_DURATION_BLOCKS 
                      : parameters::EPOCH_DURATION_BLOCKS);

    // Find matching stage
    BondStageSlot* slot = nullptr;
    for (auto& s : it->second) {
        if (s.stageNumber == stageNumber) { slot = &s; break; }
    }
    if (!slot) return false;
    if (slot->claimed) return false;
    if (currentEpoch < slot->maturityEpoch) return false;
    if (claimedPrincipal != slot->principalAmount) return false;

    // Calculate max interest for this bond from commitment index
    uint64_t maxInterest = m_currency.calculateCdInterest(
        slot->principalAmount, 0, getCurrentBlockchainHeight(),
        m_commitmentIndex, true  // isLegacyBond
    );
    if (claimedInterest > maxInterest || claimedInterest > m_legacyBondYieldPool)
        return false;

    // Deduct 1% service fee on interest → YEM Reserve
    uint64_t svcFee = (claimedInterest * BOND_STAGED_SERVICE_FEE_BPS) / 10000;
    uint64_t netInterest = claimedInterest - svcFee;

    m_legacyBondYieldPool -= netInterest;
    m_yemState.yemReserve += svcFee;
    m_totalLegacyBondLocked -= claimedPrincipal;
    slot->claimed = true;

    logger(DEBUGGING) << "Bond stage " << stageNumber << " claimed: principal="
                      << claimedPrincipal << " interest=" << claimedInterest
                      << " fee=" << svcFee << " → YEM Reserve";

    return true;
}
```

### 7. `Blockchain.cpp` — interest-only claim (modify existing 0xCC)

The existing 0xCC handler already validates interest claims. No changes needed — `claim_bond_interest` uses the same mechanism. The wallet just queries `getCdInterest()` and issues a 0xCC claim.

However, add a check: if the bond has staged unlocks, ensure interest claims don't conflict with staged claims (i.e., don't let interest-only claim the interest that's already been paid via staging).

Simplest approach: the commitment index already tracks paid interest per CD. If the user claimed interest via a stage, the subsequent interest-only call will return the remaining (if any). No conflict.

### 8. `Blockchain.cpp` — popBlock reversal

```cpp
// In popBlock(), after restoring epoch snapshot:
// If popped block contained a bond stage claim (0xCB with 0xCC + staged logic):
//   - Restore slot.claimed = false
//   - m_legacyBondYieldPool += netInterest (reverse debit)
//   - m_yemState.yemReserve -= svcFee (reverse service fee)
//   - m_totalLegacyBondLocked += claimedPrincipal
```

Track per-block bond claims in `m_blockBondStageClaims` deque (same pattern as `m_blockBurnScalpContributions`).

### 9. `SimpleWallet.cpp` — migrate_deposit (modified)

After the bond terms display, before confirmation:

```cpp
// v1.11.00: staged unlock prompt
success_msg_writer() << "";
success_msg_writer() << "=== Staged Principal Unlock (optional) ===";
success_msg_writer() << "  4 quarterly releases of 25% principal + interest";
success_msg_writer() << "  Service fee: 1% on the interest portion only";
success_msg_writer() << "  Network fee: standard tx fee per withdrawal";
success_msg_writer() << "  Without staging: principal locked until epoch 72";
success_msg_writer() << "";
success_msg_writer() << "Enable staged principal unlocks? (1) Yes  (2) No ";

std::string stagedChoice;
m_consoleHandler.readLine(stagedChoice);

bool useStaged = false;
if (stagedChoice == "1" || stagedChoice == "yes" || stagedChoice == "Yes") {
    useStaged = true;
    // Show the schedule
    uint64_t stagePrincipal = (deposit.amount * CryptoNote::parameters::BOND_STAGED_PRINCIPAL_PCT) / 100;
    success_msg_writer() << "Staged schedule:";
    success_msg_writer() << "  Stage 1 @ epoch " << (currentEpoch + 18) << ": "
                         << m_currency.formatAmount(stagePrincipal) << " XFG principal + interest";
    success_msg_writer() << "  Stage 2 @ epoch " << (currentEpoch + 36);
    success_msg_writer() << "  Stage 3 @ epoch " << (currentEpoch + 54);
    success_msg_writer() << "  Stage 4 @ epoch " << (currentEpoch + 72) << " (final maturity)";
}

bond.useStagedUnlock = useStaged;
```

### 10. `SimpleWallet.cpp` — claim_bond_interest (new command)

```
Usage: claim_bond_interest <deposit_id>

1. Look up deposit, verify 0xCB bond tag
2. Query accrued interest via RPC getCdInterest()
3. Display: "Accrued interest: 1,234.56 XFG (no fee)"
4. Confirm
5. Send tx with 0xCC tag for accrued interest amount
6. wallet->withdrawLegacyBond(depositId, accruedInterest, 0)
   with principal=0 variant (interest-only claim)
```

### 11. `SimpleWallet.cpp` — claim_bond_stage (new command)

```
Usage: claim_bond_stage <deposit_id>

1. Look up deposit, verify staged bond
2. Query RPC get_bond_stages for next available stage
3. Display:
   Stage 2/4 — matures epoch 36 (current: 35 → available at epoch 36)
   or
   Stage 2/4 — matures epoch 36 (ready now!)
     Principal: 63,562.00 XFG
     Interest:  15,891.00 XFG
     − 1% fee:    158.91 XFG
     Net:       79,294.09 XFG
   Confirm? (1) OK  (2) No
4. Send tx spending bond input with 0xCC claim for stage amounts
5. wallet->withdrawLegacyBond(depositId, accruedInterest, stagePrincipal)
```

### 12. `SimpleWallet.cpp` — legacy_bond_info (new command)

```
Usage: legacy_bond_info [deposit_id]

Without args: list all legacy bonds with summary
With <id>: detailed view per bond

Detailed view:
  Deposit ID:      42
  Amount:          1,234.56 XFG
  Bond TX:         a1b2c3...
  Created:         epoch 12
  Matures:         epoch 84
  Staged:          Yes (4 stages)
  
  Stage 1: epoch 30  [MATURE]  63,562 + 12,345 interest  — claimable
  Stage 2: epoch 48  [LOCKED]  63,562 + interest  — 18 epochs remaining
  Stage 3: epoch 66  [LOCKED]  ...
  Stage 4: epoch 84  [LOCKED]  ... (final)
  
  Accrued interest: 8,234 XFG (claimable now via claim_bond_interest)
  Next staged claim: Stage 1 — 63,562 + 12,345 (1% fee = 123 XFG)
```

### 13. RPC endpoint: `get_bond_stages`

```cpp
// In JsonRpcServer.cpp
{
  "deposit_tx_hash": "a1b2c3...",
  "staged": true,
  "total_principal": 254250,
  "total_locked": 190688,  // remaining if 1 stage claimed
  "stages": [
    {"number": 1, "epoch": 30, "principal": 63562, "mature": true,  "claimed": false},
    {"number": 2, "epoch": 48, "principal": 63562, "mature": false, "claimed": false},
    {"number": 3, "epoch": 66, "principal": 63562, "mature": false, "claimed": false},
    {"number": 4, "epoch": 84, "principal": 63562, "mature": false, "claimed": false}
  ],
  "accrued_interest": 8234,
  "service_fee_bps": 100
}
```

### 14. RPC: modify `getCdInterest` to accept partial principal

The existing `getCdInterest` calculates interest on the full deposit amount. For staged bonds, each stage is 25% of the bond. Either:
- Pass a `principalFraction` parameter (e.g., 0.25 for one stage), or
- Add a new endpoint `get_bond_stage_interest` that uses `BOND_STAGED_PRINCIPAL_PCT`

Simpler: the wallet knows the stage principal (25% × bond.amount) and passes that as the `amount` parameter to the existing `getCdInterest` call. The function already works with any amount.

---

## Serialization & popBlock

### BlockCacheSerializer additions

```cpp
void BlockCacheSerializer::serialize(ISerializer& s) {
    // ... existing ...
    s(m_bs.m_stagedBondSchedules, "bond_stages");
}
```

Bump `CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER`.

### popBlock

Track per-block bond stage claims in `m_blockBondStageClaims`:

```cpp
struct BlockBondStageClaim {
    Crypto::Hash depositTxHash;
    uint32_t stageNumber;
    uint64_t principal;
    uint64_t netInterest;  // after fee
    uint64_t svcFee;
};
std::vector<BlockBondStageClaim> m_blockBondStageClaims;
```

On popBlock:
```cpp
for (auto& claim : m_blockBondStageClaims) {
    auto it = m_stagedBondSchedules.find(claim.depositTxHash);
    if (it != m_stagedBondSchedules.end()) {
        for (auto& slot : it->second) {
            if (slot.stageNumber == claim.stageNumber) {
                slot.claimed = false;
                m_totalLegacyBondLocked += claim.principal;
                m_legacyBondYieldPool += claim.netInterest;
                m_yemState.yemReserve -= claim.svcFee;
            }
        }
    }
}
m_blockBondStageClaims.clear();
```

---

## Future: Tanda Pool

After all legacy bonds mature (year 2-3), `m_legacyBondYieldPool` has no more obligation. The pool balance can be repurposed as a tanda fund:

```
tanda_cycle(n):
  pool = m_legacyBondYieldPool  // = accumulated protocol revenue
  for each tanda member in order:
    member receives pool / n
  reset pool
```

No code changes in v1.11.00 — just the pool repurposing switch at a future activation height.

---

## Implementation Sequence

| # | File | Change |
|---|---|---|
| 1 | `CryptoNoteConfig.h` | 4 new constants |
| 2 | `TransactionExtra.h` | `useStagedUnlock` flag |
| 3 | `TransactionExtra.cpp` | serialize/parse new byte |
| 4 | `Blockchain.h` | `BondStageSlot` + `m_stagedBondSchedules` |
| 5 | `Blockchain.cpp` | 0xCB handler creates slots, `validateBondStageClaim`, popBlock reversal |
| 6 | `SimpleWallet.cpp` | `migrate_deposit` prompt, `claim_bond_interest`, `claim_bond_stage`, `legacy_bond_info` |
| 7 | `JsonRpcServer.cpp` | `get_bond_stages` endpoint |
| 8 | BlockCache storage bump | Serialize new state |
| 9 | YEM bond manager | See Appendix A — full back-end infra |

---

## Appendix A — YEM Bond Manager (Back-end Infrastructure)

> Prerequisite for staged unlocks. Zero code exists today — this entire section is new implementation.

### A.1 State Variables — `Blockchain.h`

```cpp
#include "CryptoNote.h"
#include "Serialization/ISerializer.h"
#include <deque>
#include <vector>

namespace CryptoNote {

// ── Bond record ─────────────────────────────────────────────
struct YemBond {
    uint64_t             principal;         // bond face value in atomic XFG
    uint32_t             issuedAtEpoch;     // epoch index when bond was created
    uint32_t             termEpochs;        // bond duration (72 for legacy)
    uint64_t             rateBps;           // annual coupon rate in basis points
    Crypto::Hash         depositTxHash;     // original deposit tx (for 0xCB/0xCF matching)
    AccountPublicAddress creditor;          // bondholder payout address
    bool                 repaid;            // fully paid (matured + withdrawn)

    void serialize(ISerializer& s) {
        s(principal,      "principal");
        s(issuedAtEpoch,  "issued_epoch");
        s(termEpochs,     "term");
        s(rateBps,        "rate");
        s(depositTxHash,  "deposit_tx");
        s(creditor,       "creditor");
        s(repaid,         "repaid");
    }
};

// ── Bond registry ───────────────────────────────────────────
class YemBondIndex {
public:
    void     issue(const YemBond& bond);
    void     markRepaid(const Crypto::Hash& depositTxHash);
    bool     hasBond(const Crypto::Hash& depositTxHash) const;
    uint64_t getTotalOutstanding() const;
    std::vector<YemBond> getActiveBonds(uint32_t currentEpoch) const;
    bool     isEmpty() const;

    // Returns: vector of {bond, couponAmount} for bonds with coupons due this epoch
    std::vector<std::pair<YemBond, uint64_t>> processCoupons(
        uint32_t currentEpoch, uint64_t reserveAvailable);

    // Returns: vector of {creditor, amountOwed} for matured bonds needing payout
    std::vector<std::pair<AccountPublicAddress, uint64_t>> processMaturities(
        uint32_t currentEpoch, uint64_t swfAvailable);

    void serialize(ISerializer& s);

private:
    std::vector<YemBond> m_bonds;
    uint64_t             m_totalOutstanding = 0;
};

// ── YEM system state ────────────────────────────────────────
struct YemState {
    uint64_t swfBalance       = 0;   // sovereign wealth fund
    uint64_t yemReserve       = 0;   // paper credit from burn scalp
    uint64_t rollingRates[3]  = {};  // 3-epoch rolling avg for CD rate
    uint32_t rollingRateCount = 0;
    uint32_t lagEpochCounter  = 0;   // 3-epoch activation delay
    bool     lagComplete      = false;

    void serialize(ISerializer& s) {
        s(swfBalance,       "swf");
        s(yemReserve,       "yem_reserve");
        s.binary(rollingRates, sizeof(rollingRates), "rolling_rates");
        s(rollingRateCount, "rolling_count");
        s(lagEpochCounter,  "lag_counter");
        s(lagComplete,      "lag_complete");
    }
};

// ── Coinbase payout queue entry ─────────────────────────────
struct YemPayout {
    AccountPublicAddress recipient;
    uint64_t             amount;
    Crypto::Hash         sourceTxHash;     // for deterministic ordering

    void serialize(ISerializer& s) {
        s(recipient,     "recipient");
        s(amount,        "amount");
        s(sourceTxHash,  "source_tx");
    }
};

// ── Per-block tracking (popBlock reversal) ──────────────────
struct BlockBondStageClaim {
    Crypto::Hash depositTxHash;
    uint32_t     stageNumber;
    uint64_t     principal;
    uint64_t     netInterest;
    uint64_t     svcFee;
};

// ── Blockchain class members (add to private section) ───────
class Blockchain {
private:
    // ... existing members ...

    YemState            m_yemState;
    YemBondIndex        m_yemBonds;
    uint64_t            m_rebalancerVault = 0;
    std::deque<YemPayout> m_pendingYemPayouts;

    // Per-block tracking for popBlock reversal
    std::vector<std::pair<uint32_t, uint64_t>> m_blockBurnScalpContributions;
    std::vector<BlockBondStageClaim>            m_blockBondStageClaims;
    std::vector<YemPayout>                      m_blockYemPayouts;  // restored on pop
};
```

### A.2 YemBondIndex Implementation — `Blockchain.cpp`

```cpp
void YemBondIndex::issue(const YemBond& bond) {
    m_bonds.push_back(bond);
    m_totalOutstanding += bond.principal;
}

void YemBondIndex::markRepaid(const Crypto::Hash& depositTxHash) {
    for (auto& b : m_bonds) {
        if (b.depositTxHash == depositTxHash) {
            if (!b.repaid) {
                m_totalOutstanding -= b.principal;
                b.repaid = true;
            }
            return;
        }
    }
}

bool YemBondIndex::hasBond(const Crypto::Hash& depositTxHash) const {
    for (auto& b : m_bonds) {
        if (b.depositTxHash == depositTxHash) return true;
    }
    return false;
}

uint64_t YemBondIndex::getTotalOutstanding() const {
    return m_totalOutstanding;
}

std::vector<YemBond> YemBondIndex::getActiveBonds(uint32_t currentEpoch) const {
    std::vector<YemBond> active;
    for (const auto& b : m_bonds) {
        if (!b.repaid && (b.issuedAtEpoch + b.termEpochs) > currentEpoch) {
            active.push_back(b);
        }
    }
    return active;
}

bool YemBondIndex::isEmpty() const {
    return m_bonds.empty();
}

// Coupon = principal * rate / EPY  (simple, per-epoch)
// Paid from YEM Reserve via coinbase
std::vector<std::pair<YemBond, uint64_t>> YemBondIndex::processCoupons(
    uint32_t currentEpoch, uint64_t reserveAvailable
) {
    std::vector<std::pair<YemBond, uint64_t>> result;
    for (auto& b : m_bonds) {
        if (b.repaid) continue;
        if ((b.issuedAtEpoch + b.termEpochs) <= currentEpoch) continue;
        // Only pay coupon on quarterly boundaries (every 18 epochs)
        if ((currentEpoch - b.issuedAtEpoch) % 18 != 0) continue;

        uint64_t couponPerYear = (b.principal * b.rateBps) / 10000;
        uint64_t couponPerEpoch = couponPerYear / 73;  // 73 epochs/yr
        if (couponPerEpoch <= reserveAvailable) {
            result.push_back({b, couponPerEpoch});
            reserveAvailable -= couponPerEpoch;
        }
    }
    return result;
}

// Matured bonds: principal repaid from SWF, remainder from coinbase
std::vector<std::pair<AccountPublicAddress, uint64_t>>
YemBondIndex::processMaturities(uint32_t currentEpoch, uint64_t swfAvailable) {
    std::vector<std::pair<AccountPublicAddress, uint64_t>> result;
    for (auto& b : m_bonds) {
        if (b.repaid) continue;
        if ((b.issuedAtEpoch + b.termEpochs) > currentEpoch) continue;

        uint64_t owed = b.principal;
        uint64_t fromSWF = std::min(owed, swfAvailable);
        owed -= fromSWF;
        swfAvailable -= fromSWF;

        if (owed > 0) {
            result.push_back({b.creditor, owed});  // coinbase gap
        }
        b.repaid = true;
        m_totalOutstanding -= b.principal;
    }
    return result;
}
```

### A.3 Burn Scalp — `Blockchain.cpp`, `pushToBankingIndex()`

Intercept after per-transaction burn accumulation, before `addForeverDeposit()`:

```cpp
// After accumulating permanentBurns from all HEAT commitments...
uint64_t scalped = (permanentBurns * YEM_BURN_SCALP_BPS) / 10000;
if (scalped > 0 && m_yemState.lagComplete) {
    m_yemState.yemReserve += scalped;
    permanentBurns -= scalped;
    m_blockBurnScalpContributions.push_back({block.height, scalped});
}
m_bankingIndex.addForeverDeposit(permanentBurns, block.height);
```

Only activates post-lag (3 epochs after YEM activation height). No scalp during lag — all burn goes to EternalFlame while the SWF builds.

### A.4 Transaction Extra — 0xCF YEM Bond Tag

**`TransactionExtra.h`**:
```cpp
constexpr uint8_t TX_EXTRA_YEM_BOND = 0xCF;

struct TransactionExtraYemBond {
    Crypto::Hash         originalDepositTx;
    uint64_t             amount;
    uint32_t             termEpochs;
    uint64_t             rateBps;
    bool                 useStagedUnlock;    // v1.11.00

    bool serialize(ISerializer& s) {
        s(originalDepositTx, "orig_tx");
        s(amount,            "amount");
        s(termEpochs,        "term");
        s(rateBps,           "rate");
        s(useStagedUnlock,   "staged");
        return true;
    }
};
```

**`TransactionExtra.cpp`**: add `addYemBondToExtra()` / parse `TX_EXTRA_YEM_BOND` case following existing 0xCB/0xCC pattern. Add visitor for `TransactionExtraYemBond` to the variant dispatch.

### A.5 Epoch Boundary — Bond Processing

**`Blockchain.cpp`, `pushBlock()`, epoch boundary section (~line 3948):**

After the existing legacy bond fee split, add:

```cpp
// ── YEM bond coupons (post-activation, post-lag) ──
if (yemActive && m_yemState.lagComplete) {
    auto coupons = m_yemBonds.processCoupons(
        currentEpoch, m_yemState.yemReserve);
    for (auto& [bond, coupon] : coupons) {
        m_yemState.yemReserve -= coupon;
        m_pendingYemPayouts.push_back({
            bond.creditor, coupon, bond.depositTxHash
        });
    }
}
```

After SWF smoothing step, add:

```cpp
// ── YEM bond maturities ──
if (yemActive) {
    auto maturities = m_yemBonds.processMaturities(
        currentEpoch, m_yemState.swfBalance);
    for (auto& [creditor, gapAmount] : maturities) {
        // Gap: SWF couldn't cover → coinbase from YEM Reserve
        if (m_yemState.yemReserve >= gapAmount) {
            m_yemState.yemReserve -= gapAmount;
            m_pendingYemPayouts.push_back({
                creditor, gapAmount, Crypto::Hash{}  // empty hash = maturity gap
            });
        }
    }
}
```

### A.6 Coinbase Payout Engine — `Currency.h` + `Currency.cpp`

**`Currency.h`**: add new overload:

```cpp
bool constructMinerTx(
    uint32_t height,
    size_t effectiveMedianSize,
    uint64_t alreadyGeneratedCoins,
    uint64_t fee,
    const AccountPublicAddress& minerAddress,
    Transaction& tx,
    const BinaryArray& extraNonce = BinaryArray(),
    const std::vector<std::pair<AccountPublicAddress, uint64_t>>&
        protocolPayouts = {}  // NEW
) const;
```

**`Currency.cpp`**: replace the old unused reward output loop (~lines 663-688) with:

```cpp
uint64_t protocolPayoutTotal = 0;
for (const auto& [recipient, amount] : protocolPayouts) {
    Crypto::KeyDerivation derivation;
    if (!generate_key_derivation(recipient.viewPublicKey, txkey.secretKey, derivation))
        return false;

    size_t outputIndex = tx.outputs.size();
    Crypto::PublicKey outEphemeralPubKey;
    derive_public_key(derivation, outputIndex,
                      recipient.spendPublicKey, outEphemeralPubKey);

    Crypto::KeyOutput ko;
    ko.key = outEphemeralPubKey;
    tx.outputs.push_back(TransactionOutput(amount, ko));
    protocolPayoutTotal += amount;
}
// Validate: minerReward + protocolPayoutTotal matches expected block emission
```

### A.7 Queue Draining — `Core.cpp`

**In the block preparation path (~line 620), before `constructMinerTx()`:**

```cpp
std::vector<std::pair<AccountPublicAddress, uint64_t>> protocolPayouts;
if (height >= YEM_ACTIVATION_HEIGHT) {
    uint64_t reserveAvailable = m_blockchain.getYemReserve();
    auto& queue = m_blockchain.getYemPayoutQueue();
    while (!queue.empty() && protocolPayouts.size() < YEM_MAX_PAYOUTS_PER_BLOCK) {
        auto& payout = queue.front();
        if (payout.amount > reserveAvailable) break;
        protocolPayouts.push_back({payout.recipient, payout.amount});
        reserveAvailable -= payout.amount;
        m_blockchain.debitYemReserve(payout.amount);
        m_blockYemPayouts.push_back(payout);  // track for popBlock
        queue.pop_front();
    }
}
m_currency.constructMinerTx(..., protocolPayouts);
```

### A.8 Consensus Validation — `Blockchain.cpp`, `validate_miner_transaction()`

```cpp
uint64_t expectedYemPayouts = computeExpectedYemPayouts(height, *this);
if (blockMajorVersion >= BLOCK_MAJOR_VERSION_YEM) {
    if (coinbaseTotal != baseReward + expectedYemPayouts) {
        logger(ERROR) << "Coinbase mismatch: got " << coinbaseTotal
                      << " expected " << (baseReward + expectedYemPayouts);
        return false;
    }
}
```

`computeExpectedYemPayouts()` is deterministic — it reads the payout queue state known at the given height and computes the first N payouts that would be drained.

### A.9 EpochStateSnapshot Additions

Add 9 fields to the existing 16-field struct:

```cpp
struct EpochStateSnapshot {
    // ... existing 16 fields ...
    uint64_t yemSwfBalance;
    uint64_t yemReserve;
    uint64_t rebalancerVault;
    uint32_t yemLagEpochCounter;
    bool     yemLagComplete;
    uint64_t yemBondOutstanding;
    // Rolling rates (3 values)
    uint64_t rollingRate0;
    uint64_t rollingRate1;
    uint64_t rollingRate2;
    uint32_t rollingRateCount;
};
```

### A.10 popBlock Reversal

After restoring EpochStateSnapshot at popped height:

```cpp
// Reverse burn scalp
if (!m_blockBurnScalpContributions.empty()) {
    auto& back = m_blockBurnScalpContributions.back();
    if (back.first == poppedHeight) {
        m_yemState.yemReserve -= back.second;
        m_blockBurnScalpContributions.pop_back();
    }
}

// Reverse bond stage claims
for (auto& claim : m_blockBondStageClaims) {
    // restore m_stagedBondSchedules, m_totalLegacyBondLocked,
    // m_legacyBondYieldPool, m_yemState.yemReserve
}
m_blockBondStageClaims.clear();

// Reverse coinbase payouts: push back into queue
for (auto it = m_blockYemPayouts.rbegin(); it != m_blockYemPayouts.rend(); ++it) {
    m_pendingYemPayouts.push_front(*it);
}
m_blockYemPayouts.clear();
```

### A.11 Serialization — BlockCacheStorage

```cpp
// Additional fields in BlockCacheSerializer::serialize():
s(m_bs.m_yemState,               "yem_state");
s(m_bs.m_yemBonds,               "yem_bonds");
s(m_bs.m_rebalancerVault,        "rebalancer_vault");
s(m_bs.m_pendingYemPayouts,      "yem_payouts");
s(m_bs.m_stagedBondSchedules,    "bond_stages");
```

Bump `CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER`. Old caches rebuild from raw blocks.

### A.12 Activation + Config Constants

```cpp
// CryptoNoteConfig.h
constexpr uint32_t YEM_ACTIVATION_HEIGHT     = 450000;  // TBD
constexpr uint32_t YEM_LAG_EPOCHS            = 3;
constexpr uint32_t YEM_ROLLING_WINDOW        = 3;
constexpr uint64_t YEM_SWF_SAVE_PCT          = 60;
constexpr uint64_t YEM_SWF_DRIP_BPS          = 100;     // 1%/epoch
constexpr uint64_t YEM_TREASURY_BACKSTOP_BPS = 10;      // 0.1%/epoch
constexpr uint64_t YEM_MAX_PAYOUTS_PER_BLOCK = 10;
constexpr uint64_t YEM_BURN_SCALP_BPS        = 800;     // 8%
constexpr uint64_t YEM_BOND_MAX_RATE_BPS     = 2500;    // 25%/yr cap
constexpr uint64_t YEM_BOND_MIN_RATE_BPS     = 300;     // 3%/yr floor
constexpr uint64_t YEM_BOND_RATE_MULTIPLIER  = 150;     // 1.5× organic rate
constexpr uint64_t YEM_LP_FEED_PCT           = 75;      // 75% LP yield → SWF

// Legacy bond staging (v1.11.00) — already in plan
constexpr uint32_t BOND_STAGED_TOTAL_STAGES    = 4;
constexpr uint32_t BOND_STAGED_INTERVAL_EPOCHS = 18;
constexpr uint64_t BOND_STAGED_SERVICE_FEE_BPS = 100;
constexpr uint64_t BOND_STAGED_PRINCIPAL_PCT   = 25;
```

### A.13 Legacy Bond → YEM Bond Migration

When a pre-v10 deposit migrates via `migrate_deposit` (0xCB tag):

1. The 0xCB tag registers it in the existing system (`m_totalLegacyBondLocked`, CD share)
2. Simultaneously, the bond is issued in YemBondIndex via `m_yemBonds.issue()`
3. The principal → SWF if YEM is active; otherwise stays in legacy pool
4. Coupons are paid from the legacy CD yield pool (50% share) during the transition
5. After YEM activation: coupons switch to YEM Reserve + coinbase
6. Staged unlock schedule applies to both paths (principal exits LP, interest from pool)

Gated: `if (block.height >= YEM_ACTIVATION_HEIGHT) { m_yemBonds.issue(bond); }`

---

## Combined Implementation Sequence (Staged Unlock + YEM Bond Manager)

| # | File | Change |
|---|---|---|
| 1 | `CryptoNoteConfig.h` | All YEM + bond staging constants |
| 2 | `Blockchain.h` | YemState, YemBond, YemBondIndex, YemPayout, BondStageSlot, all members |
| 3 | `Blockchain.cpp` | YemBondIndex methods, burn scalp intercept, epoch boundary coupons/maturities |
| 4 | `TransactionExtra.h` | 0xCF `TransactionExtraYemBond`, `useStagedUnlock` flag on 0xCB |
| 5 | `TransactionExtra.cpp` | serialize/parse both 0xCB + 0xCF |
| 6 | `Currency.h` / `.cpp` | `constructMinerTx` protocolPayouts parameter |
| 7 | `Core.cpp` | Queue draining before `constructMinerTx` |
| 8 | `Blockchain.cpp` | `validateBondStageClaim`, consensus validation |
| 9 | `Blockchain.cpp` | popBlock reversal (scalp + payouts + stages) |
| 10 | `Blockchain.cpp` | EpochStateSnapshot serialization bump |
| 11 | `SimpleWallet.cpp` | `migrate_deposit` prompt, `claim_bond_interest`, `claim_bond_stage`, `legacy_bond_info` |
| 12 | `JsonRpcServer.cpp` | `get_bond_stages`, `get_yem_status` |
