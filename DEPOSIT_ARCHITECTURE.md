# Fuego Deposit Architecture — Current vs Deprecated Systems

> **Status**: This document describes the CURRENT deposit model (HEAT burns + HEAT CDs) and
> contrasts it with DEPRECATED systems (COLD, Ember_Heat) that are no longer in the codebase.
> **Always refer to this document when modifying deposit-related code to avoid reintroducing
> deprecated patterns.**

---

## TL;DR — Current State

| Concept | Current? | Term Value | Mechanism |
|---------|----------|------------|-----------|
| **HEAT Mint (burn)** | ✅ YES | `HEAT_TERM` (0xFFFFFFFF) | Burn XFG → Mint HEAT via Hearth pool TWAP |
| **HEAT CD (term-locked)** | ✅ YES | Actual block count (e.g. 16440) | Lock HEAT, earn APY from protocol revenue |
| **COLD deposit** | ❌ REMOVED | Was 0xCD tag | Was off-chain inflationary token rewards |
| **Ember_Heat deposit** | ❌ REMOVED | Was "forever-deposit" | Was burn XFG → get Embers_Heat token |
| **XFG deposit** | ❌ REMOVED | N/A | No such thing anymore |

---

## ⚠️ CRITICAL: "HEAT" Has Two Meanings in Codebase

The word "HEAT" is used for BOTH the current flatcoin AND the legacy Embers_Heat token.
This is a major source of confusion. When you see "heat" in code, you MUST determine which
one it refers to:

| Term | Meaning | Status | Ratio |
|------|---------|--------|-------|
| **HEAT (flatcoin)** | Current flatcoin, pegged to purchasing power | ✅ Active | Dynamic (Hearth pool TWAP) |
| **Embers_Heat** | Legacy ERC20 token, inflationary | ❌ Deprecated | Fixed 10M:1 with XFG |

**Legacy 10M conversion still in code** (DO NOT use for actual minting):
- `Currency::convertXfgToHeat()` → `xfgAmount * 10000000` (legacy)
- `Currency::convertHeatToXfg()` → `heatAmount / 10000000` (legacy)
- `Currency::m_heatConversionRate = 10000000` (legacy)
- `DepositCommitmentGenerator::convertXfgToHeat()` → same 10M ratio (legacy)

**Actual minting uses Hearth pool TWAP**:
- `mintHeatV10()` → `heatMinted = xfgBurned * 10^18 / twap`
- Falls back to spot pool rate if TWAP unavailable
- The 10M ratio is ONLY for display/estimation, NOT actual mint pricing

---

## CURRENT SYSTEM: HEAT Burns + HEAT CDs

### ⚠️ Important: "Deposit" Is a Two-Step Process

You CANNOT "deposit a deposit." The current flow is:

```
Step 1: MINT HEAT (burn XFG)
    User has XFG
        │
        ▼
    mintHeatV10() or createDeposit(term=HEAT_TERM)
        │
        ▼
    Burn XFG → Mint HEAT via Hearth pool TWAP
        │
        ▼
    User now has HEAT in wallet

Step 2: DEPOSIT HEAT AS CD (lock for APY)
    User has HEAT
        │
        ▼
    createDeposit(amount, term, ...)  where term != HEAT_TERM
        │
        ▼
    Lock HEAT for N blocks → Earn APY from CD_APY_POOL
```

**Key insight**: The "deposit" in current system refers to STEP 2 — depositing already-minted
HEAT as a CD. There is NO direct XFG deposit anymore.

### How It Works Today

The current system has exactly TWO paths, distinguished by the `term` field in
`TransactionOutputCommitment`:

```
┌─────────────────────────────────────────────────────────────────┐
│                     createDeposit()                             │
│                     mintHeatV10()                               │
└──────────────┬──────────────────────────────┬───────────────────┘
               │                              │
    term == HEAT_TERM              term > 0 && term != HEAT_TERM
    (0xFFFFFFFF)                   (actual block count)
               │                              │
               ▼                              ▼
┌──────────────────────────┐    ┌──────────────────────────────┐
│     HEAT BURN / MINT     │    │       HEAT CD (TERM)         │
│                          │    │                              │
│ • Burns XFG permanently  │    │ • Locks HEAT for N blocks    │
│ • Mints HEAT via Hearth  │    │ • Earns APY from CD_APY_POOL │
│   pool TWAP pricing      │    │ • Principal returned at      │
│ • No APY, no term        │    │   maturity                   │
│ • Uses StarkCommitment   │    │ • Uses calculateCdInterest() │
│   for zkSTARK proofs     │    │ • Supports rollover          │
│ • Output: HEAT UTXOs     │    │ • Output: HEAT UTXOs locked  │
└──────────────────────────┘    └──────────────────────────────┘
```

### Key Source Files

| File | Role |
|------|------|
| `src/Wallet/WalletGreen.cpp` | `createDeposit()` and `mintHeatV10()` implementations |
| `src/CryptoNoteCore/Currency.cpp` | `calculateCdInterest()` — APY calculation for CDs |
| `src/CryptoNoteCore/Currency.h` | `m_depositTermForever = HEAT_TERM` |
| `src/CryptoNoteCore/Blockchain.cpp` | `m_heatCdFeePool`, CD_APY_POOL vault management |
| `src/CryptoNoteCore/CommitmentIndex.cpp` | `processAutoRolls()` — auto-roll matured CDs |
| `include/IWallet.h` | `Deposit` struct with `Deposit::Type::HEAT` |
| `src/CryptoNoteCore/DepositCommitment.h` | `StarkCommitmentGenerator` for HEAT burns |
| `src/CryptoNoteCore/TransactionExtra.h` | `TransactionOutputCommitment` struct |
| `include/CryptoNote.h` | `TransactionOutputCommitment`, `TransactionInputCommitmentSpend` |
| `src/CryptoNoteCore/TransactionExtra.cpp` | `computeHeatCommitment()`, `buildHeatExtra()` |

### Deposit Flow (Current)

#### 1. HEAT Mint (Burn) — `mintHeatV10()` ONLY

**⚠️ SINGLE PATH**: There is only ONE way to mint HEAT — `mintHeatV10()`.
The `createDeposit(term=HEAT_TERM)` path is DISABLED (throws error).
This ensures all minting uses Hearth pool TWAP pricing, not the legacy 10M ratio.

**Function: `mintHeatV10()` — Uses Hearth pool TWAP (ACTUAL mint pricing)**

```
User wants to burn XFG → get HEAT
    │
    ▼
mintHeatV10(xfgBurned, heatMinted, fee, mixin, txHash)
    │
    ├── Compute heatMinted via Hearth pool TWAP:
    │       heatMinted = xfgBurned * 10^18 / twap
    │       (falls back to spot pool rate)
    │       ⚠️ NOT the legacy 10M ratio!
    │
    ├── Select XFG inputs for xfgBurned + fee
    │
    ├── Create TransactionOutputCommitment outputs:
    │       commitKey = deriveCommitmentKeys(depositSecret).commitKey
    │       term = HEAT_TERM (0xFFFFFFFF)  ← MARKS AS PERMANENT
    │
    ├── Add auth tag: TX_EXTRA_HEAT_MINT_AUTH (0xF5)
    │       Contains: xfgBurned + heatMinted
    │
    └── Send transaction
            │
            ▼
    Blockchain validates via HeatMintEngine:
    • Checks xfgBurned matches input amounts
    • Checks heatMinted matches output amounts
    • Checks TWAP rate is within bounds
    • Tracks permanent burns → 50% EF, 50% Treasury
    • 80% of swap fees → HEAT → CD_APY_POOL at epoch
```

**EF/Treasury 50/50 Split** (from `Blockchain.cpp`):
```cpp
uint64_t efShare = (convertAmount * MINT_BURN_EF_PCT) / 100;      // 50% → Eternal Flame
uint64_t swfShare = (convertAmount * MINT_BURN_TREASURY_PCT) / 100; // 50% → Treasury
m_bankingIndex.addForeverDeposit(efShare, newHeight);
const_cast<Currency&>(m_currency).syncEternalFlame(m_bankingIndex.getBurnedXfgAmount());
```

**EF Tracker** (from `Currency.h`):
```cpp
void syncEternalFlame(uint64_t authoritativeTotal) {
    m_ethereal_xfg = std::min(authoritativeTotal, m_moneySupply);
}
uint64_t getEternalFlame() const { return m_ethereal_xfg; }
```

#### 2. HEAT CD (Term-Locked) — `createDeposit()`

```
User wants to lock HEAT for APY
    │
    ▼
createDeposit(amount, term, sourceAddr, destAddr, txHash, commitment)
    │
    ├── term == HEAT_TERM?
    │       YES → HEAT burn path (see above)
    │       NO  → HEAT CD path (below)
    │
    ├── Create TransactionOutputCommitment output:
    │       commitKey = deriveCommitmentKeys(depositSecret).commitKey
    │       term = actual block count (e.g. 16440 for 3 months)
    │
    ├── No commitment extra needed for term-locked CDs
    │       (the TransactionOutputCommitment output itself is sufficient)
    │
    ├── Store deposit secret locally for later withdrawal
    │
    └── Send transaction
            │
            ▼
    Blockchain tracks in CommitmentIndex:
    • entry.type = CommitmentEntry::Type::HEAT
    • entry.term = actual block count
    • entry.blockHeight = creation height
    • m_heat_count++
```

#### 3. CD Interest & Withdrawal

```
CD matures (blockHeight + term <= currentHeight)
    │
    ▼
User calls withdrawDeposit(depositId) or rolloverDeposit(depositId, newTerm)
    │
    ├── Calculate interest:
    │       interest = calculateCdInterest(amount, creationHeight, currentHeight, commitmentIndex)
    │       (pulls from CD_APY_POOL vault)
    │
    ├── Spend old commitment output:
    │       TransactionInputCommitmentSpend with ring signature
    │       claimedInterest = calculated interest
    │
    ├── Create new commitment output (if rollover):
    │       newAmount = principal + interest
    │       newTerm = user-chosen term
    │
    └── Send transaction
```

### APY Model (Current)

APY is funded by protocol revenue, not inflation:

```
Protocol Revenue Sources:
├── Swap fees (Hearth AMM)
├── Mint premiums (HEAT mint)
└── Banking fees (CD creation)

        │
        ▼
    m_heatCdFeePool (accumulates over epoch)
        │
        ▼
    CD_APY_POOL (vault partition)
        │
        ▼
    calculateCdInterest() distributes to CD holders
```

**Current APY tiers** (from `Currency.cpp`):
- Based on term length: longer lock = higher APY
- Paid from `CD_APY_POOL` vault (not minted/inflationary)
- If pool is empty, APY = 0 (no infinite inflation)

### Key Constants

```cpp
// src/CryptoNoteConfig.h
const uint32_t HEAT_TERM = 0xFFFFFFFF;           // Permanent burn/mint
const uint32_t DEPOSIT_TERM_LP = 0xFFFFFFFD;     // LP shares (not user deposits)
const uint32_t DEPOSIT_TERM_POOL_XFG = 0x504F4C58;   // 'POLX' — AMM pool XFG
const uint32_t DEPOSIT_TERM_POOL_HEAT = 0x504F4C48;  // 'POLH' — AMM pool HEAT
const uint32_t DEPOSIT_TERM_SWAP_RECEIVE_XFG = 0x53575258; // 'SWRX' — swap receive

// Currency.h
uint32_t m_depositTermForever = HEAT_TERM;  // Alias for getDepositTermBurn()
```

---

## DEPRECATED SYSTEMS — DO NOT REINTRODUCE

### Old Dual-Token System (COLD + Embers_Heat)

The old system had TWO separate tokens, both using zkSTARK-based deposits:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    OLD DUAL-TOKEN SYSTEM (DEPRECATED)                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────┐    ┌─────────────────────────────────┐ │
│  │      COLD Token             │    │      Embers_Heat Token           │ │
│  │                             │    │                                  │ │
│  │ • Inflationary rewards      │    │ • Fixed 10M:1 ratio with XFG    │ │
│  │ • Off-chain distribution    │    │ • ERC20 token                   │ │
│  │ • zkSTARK proof required    │    │ • "Forever-deposit" concept     │ │
│  │ • Tag: 0xCD                 │    │ • Burn XFG → Get Embers_Heat    │ │
│  │ • No HEAT involved          │    │ • zkSTARK proof required        │ │
│  └─────────────────────────────┘    └─────────────────────────────────┘ │
│                                                                         │
│  Both systems:                                                          │
│  • Used same deposit infrastructure                                     │
│  • Required zkSTARK proof generation                                    │
│  • Had "forever" or long-term lockup                                   │
│  • Are now completely removed                                           │
└─────────────────────────────────────────────────────────────────────────┘
```

### ❌ COLD Deposits (Removed)

**What they were:**
- XFG deposits with off-chain **inflationary** token rewards
- Used `TX_EXTRA_COLD_COMMITMENT` tag (0xCD) in transaction extra
- Used `TransactionExtraSimpleCD` struct for serialization
- Had `CommitmentType::COLD` and `Deposit::Type::COLD`
- Required zkSTARK proofs for reward claims
- **Rewards were NOT HEAT** — were a separate inflationary token (the COLD token)

**Why removed:**
- Inflationary rewards created unsustainable tokenomics
- Complexity of zkSTARK proof generation for every deposit
- Confusion with HEAT CDs (similar UX, different mechanics)

**Code locations (all commented out/removed):**
- `TransactionExtra.h`: `TX_EXTRA_COLD_COMMITMENT`, `TransactionExtraSimpleCD`, `TransactionExtraColdCommitment`
- `TransactionExtra.cpp`: `addColdCommitmentToExtra()`, `getColdCommitmentFromExtra()`, `computeColdCommitment()`, `buildColdExtra()`
- `DepositCommitment.h`: `CommitmentType::COLD`
- `CommitmentIndex.h`: `CommitmentEntry::Type::COLD`
- `IWallet.h`: `Deposit::Type::COLD`
- `SimpleWallet.cpp`: `create_cold_secret()`, `cold()` commands

**⚠️ GUARDRAIL**: If you see any code using 0xCD tag, `TransactionExtraSimpleCD`, or
`CommitmentType::COLD`, it is DEPRECATED and should NOT be used.

### ❌ Embers_Heat Deposits (Removed — Predates Current Codebase)

**What they were:**
- Original "HEAT deposits" from early Fuego development
- Burned XFG using a "forever-deposit" concept
- Rewarded **Embers_Heat token** (inflationary, **1 XFG = 10M Embers_Heat**)
- Used zkSTARKs for proof of burn
- Term was effectively infinite (no withdrawal)
- This is where the **10M:1 ratio** comes from — it's a LEGACY constant still in code

**Why removed:**
- Embers_Heat was a separate token from HEAT flatcoin
- Inflationary rewards were unsustainable
- Superseded by current HEAT flatcoin model

**Legacy 10M ratio still in code** (DO NOT use for actual minting):
- `Currency::convertXfgToHeat()` → `xfgAmount * 10000000` (legacy Embers_Heat ratio)
- `Currency::convertHeatToXfg()` → `heatAmount / 10000000` (legacy)
- `Currency::m_heatConversionRate = 10000000` (legacy)
- `DepositCommitmentGenerator::convertXfgToHeat()` → same 10M ratio (legacy)
- `PaymentServiceJsonRpcServer` uses it for display/estimation (legacy)
- `WalletGreen::createDeposit()` uses it for HEAT burn commitment extra (legacy display value)

**Actual minting uses Hearth pool TWAP** (NOT 10M ratio):
- `mintHeatV10()` → `heatMinted = xfgBurned * 10^18 / twap`
- Falls back to spot pool rate if TWAP unavailable

### ❌ XFG Deposits (Never Existed in Current Form)

**What they would have been:**
- Direct XFG deposits (locking XFG, not burning it)
- Would have used `Deposit::Type::XFG` or similar

**Why they don't exist:**
- All deposits in current system are HEAT-based
- XFG is either burned (for HEAT mint) or used as input currency
- There is no "XFG deposit" type in the codebase

**⚠️ GUARDRAIL**: If you see any code referencing `Deposit::Type::XFG` or "XFG deposit",
it does not exist and should not be created without explicit design review.

---

## CRITICAL DISTINCTIONS — GUARDRAILS

### 1. HEAT Burn vs HEAT CD

| Aspect | HEAT Burn/Mint | HEAT CD |
|--------|----------------|---------|
| **term value** | `HEAT_TERM` (0xFFFFFFFF) | Actual block count |
| **Reversible?** | No — XFG is permanently burned | Yes — principal returned at maturity |
| **Reward** | HEAT minted via pool TWAP | APY from protocol revenue (CD_APY_POOL) |
| **zkSTARK?** | Yes (StarkCommitmentGenerator) | No (ring signature only) |
| **APY source** | None (instant mint) | Protocol fees (swap, mint, banking) |
| **Code path** | `mintHeatV10()` | `createDeposit()` with `term != HEAT_TERM` |

### 2. TransactionOutputCommitment — Term Field Is Everything

The `term` field in `TransactionOutputCommitment` determines the deposit type:

```cpp
// From include/CryptoNote.h
struct TransactionOutputCommitment {
    Crypto::PublicKey commitKey;       // ring-sig spend key
    uint32_t term;                     // ← THIS DETERMINES DEPOSIT TYPE
    Crypto::EllipticCurvePoint amountCommitment;
    Crypto::MembershipProof amountProof;
};
```

| term value | Meaning |
|------------|---------|
| `0xFFFFFFFF` (HEAT_TERM) | Permanent burn / HEAT mint |
| `0xFFFFFFFD` (DEPOSIT_TERM_LP) | LP share (not user deposit) |
| `0x504F4C58` ('POLX') | AMM pool XFG (not user deposit) |
| `0x504F4C48` ('POLH') | AMM pool HEAT (not user deposit) |
| Any other value > 0 | Term-locked HEAT CD |

### 3. No Separate Deposit Token

Unlike the old COLD system, current deposits do NOT:
- Create a separate token (no "Embers", no "COLD tokens")
- Use zkSTARKs for reward claims (only HEAT burns use STARKs for proof)
- Have inflationary rewards (APY comes from protocol revenue)

### 4. Deposit Secret Storage

All deposits (both HEAT burns and HEAT CDs) store a 32-byte secret locally:
- Used to derive `commitKey` and `keyImage` for ring signature spending
- Stored in wallet's local secret store (never on blockchain)
- Required for withdrawal or rollover

```cpp
// From WalletGreen.cpp createDeposit()
std::array<uint8_t, 32> depositSecret;
generate_random_bytes(sizeof(depositSecret), depositSecret.data());
CryptoNote::DepositCommitmentKeys commitKeys = CryptoNote::deriveCommitmentKeys(depositSecret);
```

---

## ROLLOVER SYSTEM

### Current Implementation

Rollover allows a mature CD to be reinvested (principal + interest) into a new CD:

```cpp
// Two overloads in WalletGreen.h
bool rolloverDeposit(DepositId depositId, uint32_t newTerm,
                     const CommitmentIndex& commitmentIndex,
                     std::string &txHashOut);

bool rolloverDeposit(DepositId depositId, uint32_t newTerm,
                     uint64_t precomputedInterest,
                     std::string &txHashOut);
```

**Flow:**
1. Verify deposit is mature (`unlockHeight <= currentHeight`)
2. Calculate interest via `calculateCdInterest()`
3. Spend old commitment output (ring signature)
4. Create new commitment output with `principal + interest`
5. Send transaction

### Auto-Roll Feature

`CommitmentIndex::processAutoRolls()` marks matured CDs for one-time auto-roll:
- Called at epoch boundaries
- Sets `autoRollApplied` flag on matured entries
- Does NOT automatically execute rollover — just marks them

---

## WHAT TO AVOID (ANTI-PATTERNS)

### ❌ Do NOT reintroduce COLD
- No `TX_EXTRA_COLD_COMMITMENT` (0xCD tag)
- No `TransactionExtraSimpleCD` struct
- No `CommitmentType::COLD` enum value
- No `Deposit::Type::COLD` enum value

### ❌ Do NOT create XFG deposits
- There is no `Deposit::Type::XFG`
- All deposits are HEAT-based
- You cannot "deposit XFG" — XFG is burned to mint HEAT, then HEAT is deposited as CD

### ❌ Do NOT confuse the two-step deposit process
- Step 1: Burn XFG → Mint HEAT (via `mintHeatV10()` or `createDeposit(HEAT_TERM)`)
- Step 2: Deposit HEAT as CD (via `createDeposit(term != HEAT_TERM)`)
- You CANNOT "deposit a deposit" — you must mint HEAT first, then deposit it

### ❌ Do NOT use legacy 10M ratio for actual minting
- `convertXfgToHeat()` returns `xfgAmount * 10000000` — this is LEGACY Embers_Heat ratio
- Actual minting uses Hearth pool TWAP: `heatMinted = xfgBurned * 10^18 / twap`
- The 10M ratio is ONLY for display/estimation in commitment extra tags
- If you need actual mint pricing, use `mintHeatV10()` or query Hearth pool TWAP

### ❌ Do NOT use inflationary APY
- APY must come from protocol revenue (CD_APY_POOL)
- Never mint new HEAT for APY rewards
- If CD_APY_POOL is empty, APY = 0

### ❌ Do NOT confuse HEAT burn with HEAT CD
- HEAT burn: `term == HEAT_TERM`, irreversible, mints HEAT
- HEAT CD: `term != HEAT_TERM`, reversible at maturity, earns APY

### ❌ Do NOT remove deposit secret storage
- All deposits need local secret storage for withdrawal/rollover
- Without secret, commitment output is unspendable

### ❌ Do NOT confuse "heat" references in code
- "HEAT" can mean EITHER the current flatcoin OR the legacy Embers_Heat token
- `convertXfgToHeat()` uses legacy 10M ratio (Embers_Heat)
- `mintHeatV10()` uses Hearth pool TWAP (current HEAT flatcoin)
- Always check which "heat" a piece of code refers to

---

## PRIVACY CONSIDERATIONS

### Term Length and Privacy

**Question**: Do different term lengths hurt transaction privacy?

**Answer**: No. The term is embedded in the commitment preimage but does NOT narrow the
anonymity set:

- Ring signatures cover the **commitment output** (`commitKey`), not the term
- Decoy outputs are selected by **amount**, not by term
- Two deposits of 100 HEAT with different terms look identical at the ring-sig level
- The term is only visible at claim time (for APY calculation)

**Conclusion**: Adding 18mo and 36mo terms is safe from a privacy standpoint.
The real privacy consideration is **amount tier overlap** — unique amounts are a bigger signal.

### Amount Tiers

Amounts should overlap with existing denominations to avoid unique-amount fingerprinting.
Refer to `Currency.h` for valid amount tiers.

---

## FUTURE CONSIDERATIONS

### Rolling Epoch Term (Proposed)

Instead of fixed lockup terms, a "rolling epoch term" would allow:
- Claim/withdraw at any epoch boundary
- Lower APY than fixed lockups (base APY)
- Fixed lockups (1/3/6/12/18/36 mo) have higher APY multipliers

### Term Length Options

If privacy analysis confirms terms don't hurt anonymity:
- 1 month (base APY)
- 3 months (1.5x APY)
- 6 months (2x APY)
- 12 months (3x APY)
- 18 months (4x APY)
- 36 months (6x APY)

---

## SEE ALSO

- `include/IWallet.h` — `Deposit` struct and `Deposit::Type` enum
- `src/Wallet/WalletGreen.cpp` — `createDeposit()`, `mintHeatV10()`, `rolloverDeposit()`
- `src/CryptoNoteCore/Currency.cpp` — `calculateCdInterest()`
- `src/CryptoNoteCore/Blockchain.cpp` — CD_APY_POOL, m_heatCdFeePool
- `src/CryptoNoteCore/CommitmentIndex.h` — `CommitmentEntry`, `CommitmentEntry::Type`
- `src/CryptoNoteCore/DepositCommitment.h` — `StarkCommitmentGenerator`
- `src/CryptoNoteCore/TransactionExtra.h` — `TransactionOutputCommitment`
- `include/CryptoNote.h` — `TransactionInputCommitmentSpend`, `TransactionOutputUnified`
- `src/CryptoNoteConfig.h` — `HEAT_TERM`, `DEPOSIT_TERM_*` constants
