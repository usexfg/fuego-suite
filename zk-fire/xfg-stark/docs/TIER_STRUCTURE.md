# XFG Ecosystem Tier Structure

**Date:** 2026-01-20
**Status:** Complete Specification

---

## 🎯 **Overview**

The XFG ecosystem uses a unified 4-amount tier structure across all services:
- **HEAT Burns** (instant CD rewards)
- **COLD Deposits** (time-locked CD interest)
- **LP Rewards** (time-weighted CD for HEAT/ETH LPs)

All tiers use the same XFG/HEAT amount thresholds with service-specific term multipliers.

---

## 📊 **Core Amount Tiers**

### **4 Amount Tiers (Shared Across All Services)**

| Tier Index | XFG Amount | HEAT Equivalent | Atomic Units (XFG) | Atomic Units (HEAT) |
|-----------|------------|-----------------|-------------------|---------------------|
| **0** | 0.8 XFG | 8M HEAT | 8,000,000 | 8,000,000 × 10^18 |
| **1** | 8 XFG | 80M HEAT | 80,000,000 | 80,000,000 × 10^18 |
| **2** | 80 XFG | 800M HEAT | 800,000,000 | 800,000,000 × 10^18 |
| **3** | 800 XFG | 8B HEAT | 8,000,000,000 | 8,000,000,000 × 10^18 |

**Note:** XFG has 7 decimals, HEAT has 18 decimals (standard ERC-20)

---

## 🔥 **HEAT Burns (Instant)**

### **Term Tiers: 1 tier (Forever/Instant)**

HEAT tokens are **burned forever** and CD tokens are minted instantly.

### **4 Tiers Total (4 amounts × 1 term)**

| Tier | HEAT Burned | CD Minted | Formula |
|------|------------|-----------|---------|
| **0** | 8M HEAT | 800 CD | (8M / 10M) × 1.0 CD |
| **1** | 80M HEAT | 8,000 CD | (80M / 10M) × 1.0 CD |
| **2** | 800M HEAT | 80,000 CD | (800M / 10M) × 1.0 CD |
| **3** | 8B HEAT | 800,000 CD | (8B / 10M) × 1.0 CD |

**Conversion Rate:** 10M HEAT = 1 CD (instant, permanent burn)

---

## ❄️ **COLD Deposits (Time-Locked)**

### **Term Tiers: 2 tiers (3 months, 12 months)**

XFG tokens are **locked** (not burned) on Fuego blockchain. CD **interest** is minted after lock period.

### **8 Tiers Total (4 amounts × 2 terms)**

**Tier Encoding:** `(amountIndex * 2) + termIndex`
- Amount Index: 0 = 0.8 XFG, 1 = 8 XFG, 2 = 80 XFG, 3 = 800 XFG
- Term Index: 0 = 3 months, 1 = 12 months

| Tier | XFG Amount | Lock Period | APY | CD Interest | Formula |
|------|-----------|-------------|-----|-------------|---------|
| **0** | 0.8 XFG | 3 months | 8% | 640,000 | (0.8/100k) × 0.08 × 10^12 |
| **1** | 0.8 XFG | 12 months | 27% | 2,160,000 | (0.8/100k) × 0.27 × 10^12 |
| **2** | 8 XFG | 3 months | 18% | 14,400,000 | (8/100k) × 0.18 × 10^12 |
| **3** | 8 XFG | 12 months | 33% | 26,400,000 | (8/100k) × 0.33 × 10^12 |
| **4** | 80 XFG | 3 months | 27% | 216,000,000 | (80/100k) × 0.27 × 10^12 |
| **5** | 80 XFG | 12 months | 42% | 336,000,000 | (80/100k) × 0.42 × 10^12 |
| **6** | 800 XFG | 3 months | 33% | 2,640,000,000 | (800/100k) × 0.33 × 10^12 |
| **7** | 800 XFG | 12 months | 69% | 5,520,000,000 | (800/100k) × 0.69 × 10^12 |

**CD Interest Formula:** `(XFG_amount / 100,000) × APY × 10^12` atomic units

### **Legacy Deposits (Before 2026-01-01)**

**Only 800 XFG deposits (Tier 6 & 7) deposited before 2026-01-01 00:00:00 UTC receive 80% APY:**

| Tier | XFG Amount | Lock Period | APY | CD Interest (Legacy) |
|------|-----------|-------------|-----|---------------------|
| **6** | 800 XFG | 3 months | **80%** | 6,400,000,000 |
| **7** | 800 XFG | 12 months | **80%** | 6,400,000,000 |

Legacy cutoff timestamp: `1735689600` (2026-01-01 00:00:00 UTC)

---

## 🌊 **LP Rewards (Time-Weighted)**

### **Term Tiers: Continuous (Default → Multiplier over 1 year)**

HEAT/ETH LP providers earn time-weighted CD rewards. APY increases linearly from base to target over 1 year of staking.

### **4 Tiers Total (4 amounts × continuous time-weighting)**

| Tier | HEAT in LP | Base APY | Target APY (1yr) | Time-Weighting Formula |
|------|-----------|----------|------------------|----------------------|
| **0** | 8M HEAT (0.8 XFG) | 8% | 18% | 8% → 18% over 1 year |
| **1** | 80M HEAT (8 XFG) | 18% | 33% | 18% → 33% over 1 year |
| **2** | 800M HEAT (80 XFG) | 33% | 55% | 33% → 55% over 1 year |
| **3** | 8B HEAT (800 XFG) | 55% | 69% | 55% → 69% over 1 year |

### **Time-Weighted APY Calculation**

```solidity
effectiveAPY = baseAPY + (targetAPY - baseAPY) × min(duration / 365 days, 1)
```

**Examples:**
- **Day 1:** Tier 3 earns 55% APY (base rate)
- **Day 182** (6 months): Tier 3 earns ~62% APY (halfway to target)
- **Day 365+** (1 year): Tier 3 earns 69% APY (target rate)

### **CD Reward Calculation**

```solidity
cdRewards = (heatAmount × effectiveAPY × duration) / (365 days × 10000)
```

Where:
- `heatAmount`: HEAT tokens in LP position (atomic units, 18 decimals)
- `effectiveAPY`: Time-weighted APY (basis points, e.g., 6900 = 69%)
- `duration`: Time since last claim (seconds)

### **Minimum Requirements**

- Minimum 8M HEAT in LP position required to receive rewards (0.8 XFG equivalent)
- No maximum lock period (rewards continue indefinitely)
- Users can claim and unstake at any time (no lock)

---

## 🔢 **Tier Encoding**

### **COLD Deposits (8 tiers)**

```
Tier = (AmountIndex × 2) + TermIndex

AmountIndex:
  0 = 0.8 XFG
  1 = 8 XFG
  2 = 80 XFG
  3 = 800 XFG

TermIndex:
  0 = 3 months
  1 = 12 months

Examples:
  Tier 0 = (0 × 2) + 0 = 0.8 XFG × 3mo
  Tier 3 = (1 × 2) + 1 = 8 XFG × 12mo
  Tier 6 = (3 × 2) + 0 = 800 XFG × 3mo
```

### **HEAT Burns (4 tiers)**

```
Tier = AmountIndex (no term multiplier)

Examples:
  Tier 0 = 8M HEAT (0.8 XFG equivalent)
  Tier 1 = 80M HEAT (8 XFG equivalent)
  Tier 2 = 800M HEAT (80 XFG equivalent)
  Tier 3 = 8B HEAT (800 XFG equivalent)
```

### **LP Rewards (4 tiers)**

```
Tier = AmountIndex (time-weighted, no discrete term)

Examples:
  Tier 0 = 8M HEAT in LP (0.8 XFG equivalent)
  Tier 1 = 80M HEAT in LP (8 XFG equivalent)
  Tier 2 = 800M HEAT in LP (80 XFG equivalent)
  Tier 3 = 8B HEAT in LP (800 XFG equivalent)
```

---

## 📐 **CD Token Decimals**

**CD tokens have 12 decimals:**
- 1 CD = 1,000,000,000,000 (10^12) atomic units
- This allows precise fractional rewards for small deposits

**Examples:**
- Tier 0 COLD: 640,000 atomic units = 0.00000064 CD
- Tier 7 COLD: 5,520,000,000 atomic units = 0.00552 CD
- Tier 3 HEAT Burn: 800,000 CD × 10^12 atomic units

---

## 🔐 **Privacy Considerations**

### **COLD Deposits Privacy**

**On-chain Visibility:**
- ✅ **Fuego deposit tx** (public on Fuego blockchain)
- ✅ **L2 claim tx** (public on Arbitrum - recipient address exposed)
- ✅ **L1 mint tx** (public on Ethereum - recipient address exposed)

**Privacy Recommendations:**
- Use fresh recipient address per deposit
- Never reuse COLD claim addresses for other activities
- CD tokens are standard ERC-20 (no built-in privacy)

### **LP Rewards Privacy**

**On-chain Visibility:**
- ✅ **LP staking tx** (public - links staker address to LP position)
- ✅ **Reward claim tx** (public - links staker to CD balance)
- ✅ **CD token transfers** (public - standard ERC-20 transfers)

**Privacy Recommendations:**
- Use dedicated address for LP staking
- Consider privacy-focused swaps (Tornado Cash alternatives)
- Batch small rewards to minimize tx count

### **HEAT Burns Privacy**

**On-chain Visibility:**
- ✅ **HEAT burn tx** (public - links burner address to CD mint)
- ✅ **CD token balance** (public - visible to anyone)

**Privacy Recommendations:**
- Use fresh address for each burn
- Transfer HEAT through privacy layers before burning
- Delay claiming to break timing correlation

### **Future Privacy Enhancements**

**Potential improvements (not yet implemented):**
1. **Zero-knowledge CD claims**: Prove COLD deposit without revealing recipient
2. **Stealth addresses**: Generate one-time addresses for CD minting
3. **Privacy pools**: Group claims to break linkability
4. **Private LP rewards**: Claim rewards to shielded addresses

---

## 📊 **Comparison Table**

| Feature | HEAT Burns | COLD Deposits | LP Rewards |
|---------|-----------|--------------|-----------|
| **Asset Used** | HEAT (ERC-20) | XFG (Fuego native) | HEAT/ETH LP tokens |
| **Asset Status** | Burned (forever) | Locked (returns later) | Staked (withdrawable) |
| **Tiers** | 4 (amounts only) | 8 (4 amounts × 2 terms) | 4 (amounts only) |
| **Term Options** | 1 (instant) | 2 (3mo, 12mo) | Continuous (time-weighted) |
| **CD Type** | Principal | Interest | Interest |
| **Privacy** | Low (on-chain burn) | Medium (L2→L1 bridge) | Low (on-chain staking) |
| **Withdrawability** | Never (burned) | After lock period | Anytime |
| **APY Range** | N/A (1:1 ratio) | 8% - 80% | 8% - 69% (time-weighted) |

---

## 🚀 **Usage Examples**

### **Example 1: COLD Deposit**

User deposits **8 XFG** for **12 months**:
- Tier calculation: `(1 × 2) + 1 = 3`
- Lock period: 365 days (12 months)
- APY: 33% (standard, post-2026)
- CD interest earned: 26,400,000 atomic units = 0.0000264 CD
- Principal returns: 8 XFG unlocked after 12 months on Fuego

### **Example 2: LP Rewards**

User stakes LP tokens containing **800M HEAT** (80 XFG equivalent):
- Tier: 2 (800M HEAT threshold)
- Base APY: 33%, Target APY: 55%
- Day 1 effective APY: 33%
- Day 183 (6 months) effective APY: 44%
- Day 365 (1 year+) effective APY: 55%
- CD rewards compound as long as LP remains staked

### **Example 3: HEAT Burn**

User burns **80M HEAT**:
- Tier: 1 (80M HEAT)
- CD minted instantly: 8,000 CD = 8,000 × 10^12 atomic units
- HEAT tokens burned forever (supply reduced)
- CD tokens received immediately (no lock)

---

## 🔗 **Contract References**

- **TierConversions.sol**: `/TierConversions.sol:9-82`
- **COLDDepositProofVerifier.sol**: `/COLDDepositProofVerifier.sol:10-545`
- **LPRewardsManager.sol**: `/LPRewardsManager.sol:11-472`
- **HEATBurnProofVerifier.sol**: (uses same 4-amount tiers)

---

## 📝 **Notes**

1. **Tier consistency**: All services use same XFG/HEAT amount thresholds
2. **Term multipliers**: Service-specific (burns=1, cold=2, lp=continuous)
3. **Legacy rates**: Only 800 XFG COLD deposits before 2026 get 80% APY
4. **DAO governance**: New tiers can be added via FuegoCOLDAO proposals
5. **Privacy tradeoffs**: All transactions are on-chain and linkable

---

**Winter is coming. ❄️**
