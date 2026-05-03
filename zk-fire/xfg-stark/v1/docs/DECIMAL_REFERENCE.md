# Token Decimal Reference

**Date:** 2026-01-18

---

## 📐 **Decimal Structure**

### **CD Token (FuegoCOLDAOToken)**
- **Decimals:** 12
- **1 CD = 1,000,000,000,000 atomic units** (1 trillion)
- **Example:** 0.0064 CD = 6,400,000,000 atomic units (6.4 billion)

### **XFG Token**
- **Decimals:** 7
- **1 XFG = 10,000,000 atomic units** (10 million)
- **Example:** 800 XFG = 8,000,000,000 atomic units (8 billion)

### **HEAT Token**
- **Decimals:** 18 (standard ERC-20)
- **1 HEAT = 1,000,000,000,000,000,000 atomic units** (1 quintillion)
- **Example:** 8M HEAT = 8,000,000 × 10^18 atomic units

---

## 🧮 **CD Interest Calculation**

### **Formula:**
```
CD_interest = (XFG_amount / 100,000) × APY × 10^12
```

### **Why the 10^12 multiplier?**
Because CD has 12 decimals, we need to convert the decimal CD amount to atomic units.

### **Example: Legacy Tier 6**

**Step 1: Calculate decimal CD amount**
```
CD = (800 / 100,000) × 0.80
   = 0.008 × 0.80
   = 0.0064 CD
```

**Step 2: Convert to atomic units**
```
0.0064 CD × 10^12 = 6,400,000,000 atomic units
```

**NOT:**
```
❌ 0.0064 × 1,000 = 6,400 (WRONG - missing 6 zeros!)
```

---

## 📊 **Quick Reference Table**

| Readable CD | Atomic Units (12 decimals) | Scientific Notation |
|------------|---------------------------|-------------------|
| 1 CD | 1,000,000,000,000 | 10^12 |
| 0.1 CD | 100,000,000,000 | 10^11 |
| 0.01 CD | 10,000,000,000 | 10^10 |
| 0.001 CD | 1,000,000,000 | 10^9 |
| 0.0001 CD | 100,000,000 | 10^8 |
| 0.00001 CD | 10,000,000 | 10^7 |
| 0.000001 CD | 1,000,000 | 10^6 |
| 0.0000001 CD | 100,000 | 10^5 |
| 0.00000001 CD | 10,000 | 10^4 |
| 0.000000001 CD | 1,000 | 10^3 |
| **0.0064 CD** | **6,400,000,000** | **6.4 × 10^9** |

---

## ✅ **Correct Tier Values**

### **Standard Tiers:**
```solidity
TIER0_CD_INTEREST = 640_000;          // 0.00000064 CD
TIER1_CD_INTEREST = 2_160_000;        // 0.00000216 CD
TIER2_CD_INTEREST = 14_400_000;       // 0.0000144 CD
TIER3_CD_INTEREST = 26_400_000;       // 0.0000264 CD
TIER4_CD_INTEREST = 216_000_000;      // 0.000216 CD
TIER5_CD_INTEREST = 336_000_000;      // 0.000336 CD
TIER6_CD_INTEREST = 2_640_000_000;    // 0.00264 CD
TIER7_CD_INTEREST = 5_520_000_000;    // 0.00552 CD
```

### **Legacy Tiers:**
```solidity
LEGACY_TIER6_CD = 6_400_000_000;      // 0.0064 CD @ 80% APY
LEGACY_TIER7_CD = 6_400_000_000;      // 0.0064 CD @ 80% APY
```

---

## 🔍 **Verification**

### **Check: Does 800 XFG @ 80% = 0.0064 CD?**

**Step 1: Base conversion**
```
800 XFG / 100,000 = 0.008 base CD
```

**Step 2: Apply APY**
```
0.008 × 0.80 = 0.0064 CD ✅
```

**Step 3: Convert to atomic units**
```
0.0064 × 10^12 = 6,400,000,000 atomic units ✅
```

**Verification in Solidity:**
```solidity
// This is what gets stored in the contract
uint256 public constant LEGACY_TIER6_CD = 6_400_000_000;

// To display to user, divide by 10^12
// 6,400,000,000 / 10^12 = 0.0064 CD ✅
```

---

## 💡 **Key Takeaways**

1. **CD has 12 decimals** → 1 CD = 10^12 atomic units
2. **All contract values are in atomic units** (no decimals)
3. **To convert readable → atomic**: multiply by 10^12
4. **To convert atomic → readable**: divide by 10^12
5. **Legacy 800 XFG @ 80% = 6.4 billion atomic units**, NOT 6.4 million

---

**Winter is coming. ❄️**
