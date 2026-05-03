# LP Rewards Distribution & Privacy Guide

**Date:** 2026-01-20
**Status:** Complete Specification

---

## 🎯 **Overview**

The LPRewardsManager contract manages time-weighted CD rewards for HEAT/ETH liquidity providers on Uniswap V2/V3. This document explains how rewards are distributed, when users interact with the contract, and privacy considerations.

---

## 📊 **Reward Distribution Flow**

### **Step-by-Step Process**

```
┌──────────────┐
│   LP User    │
│  (Has LP     │
│   tokens)    │
└──────┬───────┘
       │
       │ 1. User stakes LP tokens
       │    (calls stakeLPTokens)
       ▼
┌──────────────┐
│LPRewardsManager│
│  Contract    │
└──────┬───────┘
       │ 2. Contract calculates HEAT in LP
       │    (queries Uniswap pool reserves)
       │ 3. Determines tier (0-3)
       │ 4. Records position with timestamp
       ▼
┌──────────────┐
│   Position   │
│   Active     │
│  (earning    │
│   rewards)   │
└──────┬───────┘
       │
       │ Time passes...
       │ Effective APY increases linearly
       │ (baseAPY → targetAPY over 1 year)
       ▼
┌──────────────┐
│   LP User    │
│ Decides to   │
│ Claim Rewards│
└──────┬───────┘
       │
       │ 5. User claims rewards
       │    (calls claimRewards)
       ▼
┌──────────────┐
│LPRewardsManager│
│  Calculates  │
│  Pending CD  │
└──────┬───────┘
       │ 6. Calculate effective APY (time-weighted)
       │ 7. Calculate CD rewards:
       │    (HEAT × effectiveAPY × duration) / (365 days × 10000)
       │ 8. Mint CD tokens to user
       │ 9. Update lastClaimTime
       ▼
┌──────────────┐
│  CD Tokens   │
│  Minted to   │
│    User      │
└──────┬───────┘
       │
       │ User can:
       │ - Claim again later (more rewards accrue)
       │ - Unstake LP (claims final rewards + returns LP tokens)
       │ - Keep staking (rewards continue indefinitely)
       ▼
┌──────────────┐
│   LP User    │
│ Has CD Tokens│
└──────────────┘
```

---

## 🔄 **User Interactions**

### **When Users MUST Interact**

| Action | When | Contract Function | Gas Cost (Est.) |
|--------|------|------------------|-----------------|
| **Stake LP** | Initial LP staking | `stakeLPTokens(uint256 lpAmount)` | ~150k gas |
| **Claim Rewards** | Anytime (optional) | `claimRewards()` | ~200k gas |
| **Unstake LP** | When withdrawing LP | `unstakeLPTokens()` | ~250k gas |

### **When Users DON'T Need to Interact**

✅ **Rewards accrue automatically** - no need to claim frequently
✅ **No lock period** - can unstake anytime
✅ **No penalty for late claims** - rewards continue accruing
✅ **No deposit/withdraw fees** - only gas costs

---

## 💰 **Reward Calculation**

### **Time-Weighted APY Formula**

```solidity
effectiveAPY = baseAPY + (targetAPY - baseAPY) × min(duration / 365 days, 1)
```

**Where:**
- `duration`: Time since position opened (seconds)
- `baseAPY`: Starting APY for tier (basis points)
- `targetAPY`: Target APY after 1 year (basis points)

**Example (Tier 2: 800M HEAT):**
```
Base APY: 33% (3300 basis points)
Target APY: 55% (5500 basis points)

Day 1:   effectiveAPY = 33%
Day 183: effectiveAPY = 33% + (55% - 33%) × (183/365) = 44%
Day 365: effectiveAPY = 55%
```

### **CD Rewards Formula**

```solidity
cdRewards = (heatAmount × effectiveAPY × duration) / (365 days × 10000)
```

**Where:**
- `heatAmount`: HEAT tokens in LP position (atomic units, 18 decimals)
- `effectiveAPY`: Time-weighted APY (basis points)
- `duration`: Time since last claim (seconds)

**Example Calculation:**
```
LP Position: 800M HEAT (tier 2)
Time staked: 6 months (183 days)
Effective APY: 44% (see above)

cdRewards = (800,000,000 × 10^18 × 4400 × 183 days) / (365 days × 10000)
          = (800M × 10^18 × 4400 × 15,811,200 seconds) / (31,536,000 seconds × 10000)
          = 176,640,000,000 CD atomic units
          = 0.17664 CD tokens
```

---

## ⏰ **Optimal Claiming Strategy**

### **Gas Efficiency**

Claiming rewards costs ~200k gas. Compare gas cost to reward value:

| Frequency | Pros | Cons |
|-----------|------|------|
| **Daily** | Compound rewards sooner | High gas costs |
| **Weekly** | Balanced approach | Moderate gas costs |
| **Monthly** | Low gas costs | Delayed compounding |
| **On unstake** | Minimal transactions | No early access to rewards |

**Recommendation:** Claim when `(reward value) > (10× gas cost)` to maintain profitability.

### **APY Multiplier Strategy**

Since APY increases linearly over 1 year:

**Option 1: Early Claim (Day 1)**
- Earn at base APY (e.g., 33%)
- Lower rewards but instant access

**Option 2: Patient Claim (Day 365+)**
- Earn at target APY (e.g., 55%)
- Higher rewards but delayed access

**Optimal:** Claim quarterly (90 days) to balance APY growth with gas costs.

---

## 🔐 **Privacy Analysis**

### **On-Chain Visibility**

#### **What's PUBLIC:**

1. **Staking Transaction**
   - Staker address (your wallet)
   - LP token amount
   - Tier assignment
   - Timestamp
   - **Risk:** Links your address to LP position size

2. **Claim Transaction**
   - Staker address
   - CD amount claimed
   - Timestamp
   - **Risk:** Reveals reward accumulation rate (implies tier)

3. **Unstake Transaction**
   - Staker address
   - LP tokens returned
   - Final CD rewards
   - **Risk:** Reveals total LP position duration

4. **LP Token Transfers**
   - All Uniswap LP token transfers are public
   - **Risk:** LP acquisition/disposal is visible

#### **What's PRIVATE:**

❌ **Nothing** - All data is on-chain and publicly queryable
- Anyone can call `getLPPosition(address)` to see your position
- Anyone can calculate your pending rewards
- Anyone can track your claim history

---

## 🛡️ **Privacy Strategies**

### **Strategy 1: Fresh Address Per Position**

**How it works:**
1. Create new wallet address
2. Transfer HEAT + ETH to new address
3. Provide liquidity from new address
4. Stake LP from new address
5. Never link new address to main identity

**Privacy level:** Medium
- ✅ Breaks address linkability
- ✅ Harder to attribute LP position to identity
- ❌ On-chain size/timing still visible
- ❌ Transfer trail may be traceable

**Cost:** ~$50 in gas (ETH Sepolia testnet: minimal)

### **Strategy 2: Time-Based Privacy**

**How it works:**
1. Delay claiming rewards (wait weeks/months)
2. Claim in random intervals (not regular patterns)
3. Unstake during high-volume periods (blend with crowd)

**Privacy level:** Low
- ✅ Harder to predict claim timing
- ✅ Blends with other LP providers
- ❌ Position size still visible
- ❌ Address still traceable

**Cost:** Free (just behavioral changes)

### **Strategy 3: Batch Operations**

**How it works:**
1. Accumulate multiple LP positions in different addresses
2. Claim all rewards in same block
3. Transfer all CD tokens to mixer/privacy pool
4. Withdraw from mixer to fresh address

**Privacy level:** High
- ✅ Breaks temporal correlation
- ✅ Obscures individual position sizes
- ✅ Output address unlinkable (if using mixer)
- ❌ Input addresses still visible
- ❌ Requires privacy infrastructure

**Cost:** ~$200 in gas + mixer fees

### **Strategy 4: Privacy-Preserving Swaps**

**How it works:**
1. Acquire HEAT through privacy-focused DEX
2. Provide liquidity from privacy-enhanced address
3. Stake LP tokens
4. Claim rewards to shielded address (future feature)
5. Withdraw CD through privacy pool

**Privacy level:** Very High
- ✅ Input source obscured
- ✅ LP position unlinkable to identity
- ✅ Output destination private
- ❌ Requires advanced privacy tools
- ❌ Higher complexity and cost

**Cost:** ~$500+ (depends on privacy tools used)

---

## 🚀 **Future Privacy Enhancements**

### **Planned Features (Not Yet Implemented)**

#### **1. Zero-Knowledge Reward Claims**

**Concept:** Prove you have eligible LP position without revealing address

```solidity
function claimRewardsZK(
    bytes32 commitment,        // LP position commitment
    bytes calldata zkProof,    // ZK proof of stake duration
    address recipient          // Fresh address for CD tokens
) external;
```

**Benefits:**
- ✅ Staker address hidden
- ✅ Position size hidden
- ✅ Claim timing obscured
- ✅ Recipient address unlinkable

**Challenges:**
- Requires ZK circuit design
- Higher gas costs (~500k gas)
- Complex user experience

#### **2. Stealth Address Generation**

**Concept:** Generate one-time addresses for CD minting

```solidity
function claimToStealth(
    bytes32 stealthMeta,       // Stealth address metadata
    bytes calldata signature   // Owner signature
) external;
```

**Benefits:**
- ✅ Each claim goes to unique address
- ✅ No address reuse
- ✅ Harder to link claims together

**Challenges:**
- Requires wallet support (MetaMask plugin)
- User must track stealth keys
- Still linkable via position data

#### **3. Privacy Pools for LP Rewards**

**Concept:** Group-based reward claiming (like Tornado Cash)

```solidity
function depositToPrivacyPool(uint256 lpAmount) external;
function withdrawFromPrivacyPool(bytes calldata zkProof, address recipient) external;
```

**Benefits:**
- ✅ Complete address unlinkability
- ✅ Position size obscured in pool
- ✅ Anonymity set grows with users

**Challenges:**
- Regulatory concerns (AML/KYC)
- Requires critical mass of users
- Complex ZK circuit maintenance

#### **4. Homomorphic Reward Accumulation**

**Concept:** Encrypt reward amounts, reveal only on claim

```solidity
function getEncryptedRewards(address lpHolder) external view returns (bytes calldata);
function claimEncryptedRewards(bytes calldata decryptionKey) external;
```

**Benefits:**
- ✅ Reward amounts private until claim
- ✅ Position tier hidden
- ✅ Timing correlation harder

**Challenges:**
- Very high gas costs (homomorphic ops)
- Limited Solidity support
- Key management complexity

---

## 📋 **Comparison: Privacy vs Convenience**

| Privacy Level | Method | Convenience | Cost | Recommended For |
|--------------|--------|-------------|------|-----------------|
| **None** | Default staking | ⭐⭐⭐⭐⭐ Very Easy | ~$10 gas | Transparent users |
| **Low** | Time randomization | ⭐⭐⭐⭐ Easy | Free | Casual privacy |
| **Medium** | Fresh addresses | ⭐⭐⭐ Moderate | ~$50 gas | Privacy-conscious |
| **High** | Batch + mixer | ⭐⭐ Complex | ~$200 | Serious privacy |
| **Very High** | Privacy swaps | ⭐ Expert only | ~$500+ | Maximum privacy |

---

## 🧑‍💻 **Code Examples**

### **Example 1: Simple Staking**

```typescript
import { ethers } from 'ethers';

// Approve LP tokens
const lpToken = new ethers.Contract(LP_TOKEN_ADDRESS, ERC20_ABI, signer);
await lpToken.approve(LP_REWARDS_MANAGER_ADDRESS, lpAmount);

// Stake LP tokens
const lpRewardsManager = new ethers.Contract(
  LP_REWARDS_MANAGER_ADDRESS,
  LP_REWARDS_ABI,
  signer
);

const tx = await lpRewardsManager.stakeLPTokens(lpAmount);
await tx.wait();

console.log('LP staked successfully!');
```

### **Example 2: Claim Rewards with Privacy**

```typescript
// Generate fresh address for rewards (privacy-enhanced)
const freshWallet = ethers.Wallet.createRandom();

// Claim rewards (standard method - sends to staker address)
const tx = await lpRewardsManager.claimRewards();
await tx.wait();

// Immediately transfer CD to fresh address
const cdToken = new ethers.Contract(CD_TOKEN_ADDRESS, ERC20_ABI, signer);
const balance = await cdToken.balanceOf(await signer.getAddress());
await cdToken.transfer(freshWallet.address, balance);

console.log('Rewards claimed and moved to fresh address:', freshWallet.address);
```

### **Example 3: Calculate Pending Rewards**

```typescript
// Query pending rewards (view function - no gas cost)
const pendingRewards = await lpRewardsManager.getPendingRewards(userAddress);

console.log(`Pending CD rewards: ${ethers.formatUnits(pendingRewards, 12)} CD`);

// Query position details
const position = await lpRewardsManager.getLPPosition(userAddress);
console.log('Position:', {
  lpAmount: ethers.formatEther(position.lpTokenAmount),
  tier: position.tier,
  startTime: new Date(position.startTime * 1000).toISOString(),
  lastClaim: new Date(position.lastClaimTime * 1000).toISOString()
});

// Calculate effective APY
const effectiveAPY = await lpRewardsManager.calculateTimeWeightedAPY(userAddress);
console.log(`Current APY: ${effectiveAPY / 100}%`);
```

### **Example 4: Unstake with Final Rewards**

```typescript
// Unstake LP tokens (automatically claims final rewards)
const tx = await lpRewardsManager.unstakeLPTokens();
const receipt = await tx.wait();

// Parse events to get final reward amount
const event = receipt.logs.find(log =>
  log.topics[0] === ethers.id('LPUnstaked(address,uint256,uint256,uint256)')
);

console.log('LP unstaked! Final rewards:', event.args.finalRewards);
```

---

## ⚠️ **Security Considerations**

### **Smart Contract Risks**

1. **Reentrancy:** Contract uses `ReentrancyGuard` to prevent attacks
2. **Oracle Manipulation:** Uses Uniswap pool reserves (harder to manipulate)
3. **Rounding Errors:** All calculations use integer math (no precision loss)
4. **Pausing:** Owner can pause in emergency (but cannot steal funds)

### **Economic Risks**

1. **Impermanent Loss:** LP positions subject to IL (unrelated to CD rewards)
2. **APY Changes:** Future DAO proposals may modify APY structure
3. **Gas Costs:** High ETH gas prices reduce reward profitability
4. **HEAT Price Volatility:** Rewards paid in CD, but LP contains HEAT

### **Privacy Risks**

1. **Address Linkability:** All transactions publicly visible
2. **Timing Attacks:** Claim timing may reveal behavior patterns
3. **Metadata Leakage:** RPC providers may log IP addresses
4. **Social Engineering:** Wallet addresses may be socially linked

---

## 📚 **Resources**

- **LPRewardsManager Contract:** `/LPRewardsManager.sol`
- **Tier Structure:** `/docs/TIER_STRUCTURE.md`
- **COLD Deposit Flow:** `/docs/COLD_PROOF_SUBMISSION_FLOW.md`
- **Uniswap V2 Docs:** https://docs.uniswap.org/contracts/v2/overview

---

## ❓ **FAQ**

### **Q: Do I need to claim rewards regularly?**
**A:** No, rewards accrue automatically. Claim whenever gas costs are reasonable relative to rewards.

### **Q: What happens if I never claim?**
**A:** Rewards continue accruing indefinitely. You can claim all accumulated rewards at once when unstaking.

### **Q: Can I increase my tier after staking?**
**A:** No, tier is fixed at stake time. Unstake and re-stake with more HEAT to upgrade tier.

### **Q: Are LP rewards taxable?**
**A:** Consult tax professional. In most jurisdictions, rewards are taxable as income when received.

### **Q: How private are LP rewards?**
**A:** Not very private by default. All transactions are publicly visible on-chain. Use privacy strategies above to enhance privacy.

### **Q: Can I stake LP from multiple addresses?**
**A:** Yes, each address can have one active LP position. Use multiple addresses for privacy or diversification.

### **Q: What if HEAT price crashes while I'm staked?**
**A:** LP positions are subject to impermanent loss. CD rewards continue regardless of HEAT price.

### **Q: Can the contract owner steal my LP tokens?**
**A:** No, LP tokens are held by the contract and only withdrawable by the staker. Owner cannot access user funds.

---

**Winter is coming. ❄️**
