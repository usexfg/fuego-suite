# COLD Deposit Tier Reference

**Date:** 2026-01-18
**Branch:** `cold-starks`
**Contract:** `COLDDepositProofVerifier.sol`

---

## 📊 **Tier Structure Overview**

COLD deposits use an **8-tier system** combining:
- **4 XFG amount tiers**: 0.8, 8, 80, 800 XFG
- **2 time tiers**: 3 months (short), 12 months (long)

**Tier Encoding:** `tier = (amountIndex * 2) + termIndex`
- Even tiers (0, 2, 4, 6) = 3-month lock
- Odd tiers (1, 3, 5, 7) = 12-month lock

---

## 📈 **Full Tier Matrix**

| Tier | XFG Amount | Lock Period | APY | CD Interest (atomic units) | CD Interest (readable) |
|------|------------|-------------|-----|---------------------------|------------------------|
| **0** | 0.8 XFG | 3 months | 8% | 640,000 | 0.00000064 CD |
| **1** | 0.8 XFG | 12 months | 27% | 2,160,000 | 0.00000216 CD |
| **2** | 8 XFG | 3 months | 18% | 14,400,000 | 0.0000144 CD |
| **3** | 8 XFG | 12 months | 33% | 26,400,000 | 0.0000264 CD |
| **4** | 80 XFG | 3 months | 27% | 216,000,000 | 0.000216 CD |
| **5** | 80 XFG | 12 months | 42% | 336,000,000 | 0.000336 CD |
| **6** | 800 XFG | 3 months | 33% | 2,640,000,000 | 0.00264 CD |
| **7** | 800 XFG | 12 months | 69% | 5,520,000,000 | 0.00552 CD |

---

## 🏛️ **Legacy Deposits (Pre-2026)**

**Eligibility:** Only **800 XFG deposits** (tier 6 or tier 7) made **before 2026-01-01 00:00:00 UTC**

| Tier | XFG Amount | Lock Period | Legacy APY | Legacy CD Interest (atomic) | Legacy CD Interest (readable) |
|------|------------|-------------|-----------|----------------------------|------------------------------|
| **6** | 800 XFG | 3 months | **80%** | 6,400,000,000 | 0.0064 CD |
| **7** | 800 XFG | 12 months | **80%** | 6,400,000,000 | 0.0064 CD |

**Cutoff Timestamp:** `1735689600` (2026-01-01 00:00:00 UTC)

**Note:** All other tiers (0-5) use standard APY rates even if deposited before 2026.

---

## 🧮 **CD Calculation Formula**

### **Standard Formula:**
```
CD_interest = (XFG_amount / 100,000) × APY_rate
```

### **Examples:**

**Tier 0 (0.8 XFG × 3mo @ 8%):**
```
CD = (0.8 / 100,000) × 0.08
   = 0.00000064 CD
   = 0.00000064 × 10^12 = 640,000 atomic units (12 decimals)
```

**Tier 7 (800 XFG × 12mo @ 69%):**
```
CD = (800 / 100,000) × 0.69
   = 0.00552 CD
   = 0.00552 × 10^12 = 5,520,000,000 atomic units
```

**Legacy Tier 6 (800 XFG × 3mo @ 80%):**
```
CD = (800 / 100,000) × 0.80
   = 0.0064 CD
   = 0.0064 × 10^12 = 6,400,000,000 atomic units
```

---

## 📐 **Supply Ratio**

**1 COLD : 100,000 XFG**
- 1 XFG = 0.00001 COLD (base ratio, before APY)
- CD interest is then calculated from this base ratio

---

## 🔢 **Decimals**

- **XFG:** 7 decimals (1 XFG = 10,000,000 atomic units)
- **CD:** 12 decimals (1 CD = 1,000,000,000,000 atomic units)
- **HEAT:** 18 decimals (1 HEAT = 10^18 atomic units)

---

## 📋 **APY Progression Analysis**

### **By Amount (12-month lock):**
- 0.8 XFG: 27% APY (tier 1)
- 8 XFG: 33% APY (tier 3) → +6%
- 80 XFG: 42% APY (tier 5) → +9%
- 800 XFG: 69% APY (tier 7) → +27%

**Pattern:** Larger deposits earn proportionally higher APY

### **By Term (for each amount):**
| Amount | 3mo APY | 12mo APY | Long-term Premium |
|--------|---------|----------|-------------------|
| 0.8 XFG | 8% | 27% | +19% |
| 8 XFG | 18% | 33% | +15% |
| 80 XFG | 27% | 42% | +15% |
| 800 XFG | 33% | 69% | +36% |

**Pattern:** 12-month locks earn significantly more interest

---

## 🎯 **Tier Selection Guide**

### **Choose 3-month lock if:**
- Need shorter commitment period
- Want faster unlock of XFG principal
- Testing the system

### **Choose 12-month lock if:**
- Want maximum CD interest earnings
- Comfortable with longer lock period
- Long-term DAO voting power strategy

### **Choose larger amounts if:**
- Maximizing APY rate (800 XFG = 69% vs 0.8 XFG = 27%)
- Significant CD token accumulation
- Long-term COLDAO governance participation

---

## 🔐 **Contract Constants**

```solidity
// Standard tier CD amounts (12 decimals: 1 CD = 10^12 atomic units)
TIER0_CD_INTEREST = 640_000;          // 0.00000064 CD
TIER1_CD_INTEREST = 2_160_000;        // 0.00000216 CD
TIER2_CD_INTEREST = 14_400_000;       // 0.0000144 CD
TIER3_CD_INTEREST = 26_400_000;       // 0.0000264 CD
TIER4_CD_INTEREST = 216_000_000;      // 0.000216 CD
TIER5_CD_INTEREST = 336_000_000;      // 0.000336 CD
TIER6_CD_INTEREST = 2_640_000_000;    // 0.00264 CD
TIER7_CD_INTEREST = 5_520_000_000;    // 0.00552 CD

// Legacy tier CD amounts (only tier 6-7)
LEGACY_TIER6_CD = 6_400_000_000;      // 0.0064 CD
LEGACY_TIER7_CD = 6_400_000_000;      // 0.0064 CD

// Legacy cutoff timestamp
LEGACY_CUTOFF_TIMESTAMP = 1735689600; // 2026-01-01 00:00:00 UTC
```

---

## 🔍 **View Functions**

### **Get Tier Info:**
```solidity
function getTierInfo(uint8 tier, bool isLegacy)
    external pure returns (
        uint256 cdAmount,
        string memory xfgAmount,
        string memory lockPeriod,
        uint256 apyBps
    )
```

### **Check Legacy Eligibility:**
```solidity
function isLegacyDeposit(uint256 depositTimestamp, uint8 tier)
    external pure returns (bool isLegacy)
```

### **Get All Tier Amounts:**
```solidity
function getAllTierAmounts()
    external pure returns (
        uint256 tier0, tier1, tier2, tier3,
        uint256 tier4, tier5, tier6, tier7
    )
```

### **Get Legacy Tier Amounts:**
```solidity
function getLegacyTierAmounts()
    external pure returns (uint256 tier6, uint256 tier7)
```

---

## 💡 **Examples**

### **Example 1: Small Deposit, Short Term**
- **Deposit:** 0.8 XFG
- **Lock Period:** 3 months
- **Tier:** 0
- **APY:** 8%
- **CD Earned:** 640,000 atomic units (0.00000064 CD)

### **Example 2: Medium Deposit, Long Term**
- **Deposit:** 80 XFG
- **Lock Period:** 12 months
- **Tier:** 5
- **APY:** 42%
- **CD Earned:** 336,000,000 atomic units (0.000336 CD)

### **Example 3: Large Deposit, Legacy**
- **Deposit:** 800 XFG
- **Lock Period:** 3 months
- **Deposit Date:** 2025-12-15 (before 2026)
- **Tier:** 6
- **Legacy:** Yes
- **APY:** 80% (instead of 33%)
- **CD Earned:** 6,400,000,000 atomic units (0.0064 CD)

### **Example 4: Large Deposit, Standard**
- **Deposit:** 800 XFG
- **Lock Period:** 12 months
- **Deposit Date:** 2026-01-15 (after 2026)
- **Tier:** 7
- **Legacy:** No
- **APY:** 69%
- **CD Earned:** 5,520,000,000 atomic units (0.00552 CD)

---

## ⚠️ **Important Notes**

1. **XFG Principal:** Locked on Fuego, unlocks handled off-chain
2. **CD Token:** Only **interest** is minted, not principal
3. **Legacy Bonus:** Only applies to 800 XFG deposits before 2026
4. **Tier Encoding:** Single uint8 (0-7) encodes both amount and term
5. **Nullifier Protection:** Each deposit can only be claimed once
6. **Network Support:** Both mainnet and testnet Fuego

---

## 📞 **Smart Contract Integration**

### **API Verifier Call:**
```solidity
COLDDepositProofVerifier.claimCD(
    recipient,        // Address to receive CD
    tier,            // 0-7
    nullifier,       // From STARK proof
    commitment,      // From STARK proof
    networkId,       // Fuego mainnet or testnet
    depositTimestamp // For legacy detection
)
```

### **Legacy Detection:**
```solidity
bool isLegacy = depositTimestamp < LEGACY_CUTOFF_TIMESTAMP
                && (tier == 6 || tier == 7);
```

---

**Winter is coming. ❄️**
