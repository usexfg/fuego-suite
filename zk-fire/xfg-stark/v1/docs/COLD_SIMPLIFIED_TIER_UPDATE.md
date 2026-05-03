# COLD Deposits - Simplified 4-Tier Structure

**Date:** 2026-01-18
**Status:** ✅ Ready for deployment
**Branch:** `cold-starks`

---

## 🎯 **Final Tier Structure**

### **Initial 4 Tiers (Hardcoded):**

| Tier | Amount | Term | APY | CD Interest (atomic) | CD Interest (readable) |
|------|--------|------|-----|---------------------|------------------------|
| **0** | 0.8 XFG | 3mo | 8% | 640,000 | 0.00000064 CD |
| **1** | 0.8 XFG | 12mo | 21% | 1,680,000 | 0.00000168 CD |
| **2** | 800 XFG | 3mo | 33% | 2,640,000,000 | 0.00264 CD |
| **3** | 800 XFG | 12mo | 69% | 5,520,000,000 | 0.00552 CD |

### **Legacy Tiers (Pre-2026, 800 XFG only):**

| Tier | Amount | Term | APY | CD Interest (atomic) | CD Interest (readable) |
|------|--------|------|-----|---------------------|------------------------|
| **2** | 800 XFG | 3mo | **80%** | 6,400,000,000 | 0.0064 CD |
| **3** | 800 XFG | 12mo | **80%** | 6,400,000,000 | 0.0064 CD |

**Legacy Cutoff:** 2026-01-01 00:00:00 UTC (timestamp: `1735689600`)

---

## 💡 **Why This Structure?**

### **Simplified Start:**
- Only 2 XFG amounts: **0.8 XFG** (small) and **800 XFG** (large)
- Only 2 lock periods: **3 months** (short) and **12 months** (long)
- Total of **4 initial tiers** instead of 8

### **DAO Upgradeable:**
- DAO can add new tiers (4, 5, 6...) via governance
- New tiers stored in `mapping(uint256 => uint256) dynamicTiers`
- Example: Add 8 XFG, 80 XFG, or custom amounts/terms later

### **Large Deposit Advantage:**
- Prevents gaming with many small deposits
- **800 separate 0.8 XFG deposits @ 21%:** 1,344,000,000 atomic = 0.001344 CD
- **1 × 800 XFG deposit @ 69%:** 5,520,000,000 atomic = 0.00552 CD
- **Advantage: 4.1x more CD for single large deposit!**

---

## 🔧 **Smart Contract Updates**

### **COLDDepositProofVerifier.sol**

#### **New Constants:**
```solidity
// Initial 4 tiers (hardcoded)
TIER0_CD_INTEREST = 640_000;          // 0.8 XFG × 3mo @ 8%
TIER1_CD_INTEREST = 1_680_000;        // 0.8 XFG × 12mo @ 21%
TIER2_CD_INTEREST = 2_640_000_000;    // 800 XFG × 3mo @ 33%
TIER3_CD_INTEREST = 5_520_000_000;    // 800 XFG × 12mo @ 69%

// Legacy tiers (only 800 XFG before 2026)
LEGACY_TIER2_CD = 6_400_000_000;      // 800 XFG × 3mo @ 80%
LEGACY_TIER3_CD = 6_400_000_000;      // 800 XFG × 12mo @ 80%

// Dynamic tier storage
mapping(uint256 => uint256) public dynamicTiers;
uint256 public maxTierIndex;  // Initialized to 3
```

#### **New Admin Functions:**
```solidity
// Add a new tier (DAO governance only)
function addTier(uint256 tierIndex, uint256 cdAmount) external onlyOwner

// Update an existing dynamic tier (DAO governance only, tiers 4+ only)
function updateTier(uint256 tierIndex, uint256 cdAmount) external onlyOwner
```

#### **New View Functions:**
```solidity
// Get initial tier amounts (0-3)
function getAllInitialTierAmounts() external pure returns (
    uint256 tier0, uint256 tier1, uint256 tier2, uint256 tier3
)

// Get dynamic tier amount (4+)
function getDynamicTierAmount(uint256 tierIndex) external view returns (uint256)

// Get max tier index
function getMaxTierIndex() external view returns (uint256)
```

---

## 📊 **CD Calculation Examples**

### **Tier 0: 0.8 XFG × 3mo @ 8%**
```
(0.8 / 100,000) × 0.08 × 10^12
= 0.00000064 × 10^12
= 640,000 atomic units
```

### **Tier 1: 0.8 XFG × 12mo @ 21%**
```
(0.8 / 100,000) × 0.21 × 10^12
= 0.00000168 × 10^12
= 1,680,000 atomic units
```

### **Tier 2: 800 XFG × 3mo @ 33%**
```
(800 / 100,000) × 0.33 × 10^12
= 0.00264 × 10^12
= 2,640,000,000 atomic units
```

### **Tier 3: 800 XFG × 12mo @ 69%**
```
(800 / 100,000) × 0.69 × 10^12
= 0.00552 × 10^12
= 5,520,000,000 atomic units
```

### **Legacy Tier 2-3: 800 XFG @ 80%**
```
(800 / 100,000) × 0.80 × 10^12
= 0.0064 × 10^12
= 6,400,000,000 atomic units
```

---

## 🔮 **Future DAO Additions**

### **Example: Adding 8 XFG Tiers**

The DAO could later add:
- **Tier 4:** 8 XFG × 3mo @ 18% → 14,400,000 atomic units
- **Tier 5:** 8 XFG × 12mo @ 33% → 26,400,000 atomic units

**Via Governance:**
```solidity
// DAO proposal to add tier 4
COLDVerifier.addTier(4, 14_400_000);

// DAO proposal to add tier 5
COLDVerifier.addTier(5, 26_400_000);
```

### **Example: Adding 80 XFG Tiers**

- **Tier 6:** 80 XFG × 3mo @ 27% → 216,000,000 atomic units
- **Tier 7:** 80 XFG × 12mo @ 42% → 336,000,000 atomic units

### **Example: Custom Tiers**

DAO could add any custom amount/term combination:
- **Tier 8:** 100 XFG × 6mo @ 25% → 200,000,000 atomic units
- **Tier 9:** 5000 XFG × 24mo @ 100% → 80,000,000,000 atomic units

---

## ⚖️ **Preventing Gaming**

### **Why Not Many Small Deposits?**

**Scenario:** User has 800 XFG total

**Option A: 800 separate 0.8 XFG deposits @ 21% (1yr)**
```
800 × 1,680,000 = 1,344,000,000 atomic units = 0.001344 CD
```

**Option B: 1 × 800 XFG deposit @ 69% (1yr)**
```
5,520,000,000 atomic units = 0.00552 CD
```

**Result:** Option B earns **4.1x more CD!**

This incentivizes:
- ✅ Larger capital commitments
- ✅ Deeper liquidity for COLDAO
- ✅ Fewer on-chain transactions
- ❌ Prevents spamming with small deposits

---

## 🧪 **Testing Checklist**

### **Initial Tiers (0-3):**
- [ ] Test tier 0 (0.8 XFG × 3mo @ 8%)
- [ ] Test tier 1 (0.8 XFG × 12mo @ 21%)
- [ ] Test tier 2 (800 XFG × 3mo @ 33%)
- [ ] Test tier 3 (800 XFG × 12mo @ 69%)

### **Legacy Tiers:**
- [ ] Test legacy tier 2 (800 XFG × 3mo @ 80% pre-2026)
- [ ] Test legacy tier 3 (800 XFG × 12mo @ 80% pre-2026)
- [ ] Test that tier 0-1 DON'T get legacy bonus

### **DAO Functions:**
- [ ] Test adding tier 4 via `addTier()`
- [ ] Test updating tier 4 via `updateTier()`
- [ ] Test that hardcoded tiers (0-3) cannot be updated
- [ ] Test sequential tier addition requirement
- [ ] Test maxTierIndex updates correctly

### **View Functions:**
- [ ] Test `getAllInitialTierAmounts()`
- [ ] Test `getDynamicTierAmount()` for tier 4+
- [ ] Test `getMaxTierIndex()`
- [ ] Test `getTierInfo()` with isLegacy flag
- [ ] Test `getLegacyTierAmounts()`
- [ ] Test `isLegacyDeposit()` validation

---

## 📝 **Deployment Checklist**

### **Testnet:**
1. [ ] Deploy FuegoCOLDAOToken (Sepolia)
2. [ ] Deploy COLDAOGovernor (Sepolia)
3. [ ] Deploy COLDDepositProofVerifier (Arbitrum Sepolia)
4. [ ] Configure minter authorization
5. [ ] Set API verifier address
6. [ ] Test all 4 initial tiers
7. [ ] Test legacy deposits
8. [ ] Test DAO adding tier 4
9. [ ] Verify gas costs

### **Mainnet:**
1. [ ] Security audit
2. [ ] Deploy to mainnet (same order)
3. [ ] Configure production API
4. [ ] Transfer ownership to DAO
5. [ ] Document contract addresses

---

## 🎯 **Key Benefits**

✅ **Simplicity:** Only 4 initial tiers, easy to understand
✅ **Flexibility:** DAO can add unlimited new tiers
✅ **Anti-Gaming:** Large deposit advantage prevents spam
✅ **Legacy Support:** Early 800 XFG adopters get 80% APY
✅ **Upgradeable:** Future-proof without contract upgrades
✅ **Gas Efficient:** Hardcoded tiers cheaper than dynamic lookups

---

**Winter is coming. ❄️**

**Status:** Ready for testnet deployment!
