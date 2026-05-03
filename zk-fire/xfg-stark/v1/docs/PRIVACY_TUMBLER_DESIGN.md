# Privacy Tumbler for CD Rewards

**Date:** 2026-01-20
**Status:** Design Proposal

---

## 🎯 **Goal**

Add privacy to CD token claims from:
- **COLD Deposits** (CD interest paid immediately on claim)
- **LP Rewards** (CD interest for staking HEAT/ETH LP)
- **HEAT Burns** (CD minted when burning HEAT)

**Key Insight:** Since COLD interest is paid immediately (XFG can't be unlocked on-chain), we can add privacy layers to the claiming process.

---

## 🔐 **Privacy Tumbler Design**

### **Concept: Pool-Based Anonymity**

Instead of claiming directly to your address, claim through a privacy pool that:
1. Accepts claim requests with ZK proofs
2. Batches multiple claims together
3. Distributes CD tokens to fresh addresses
4. Breaks the link between claimer → recipient

**Similar to:** Tornado Cash, but specifically for CD rewards

---

## 📐 **Architecture**

```
┌─────────────────┐
│  User A Wants   │
│  to Claim CD    │
└────────┬────────┘
         │
         │ 1. Generate ZK proof of eligibility
         │    (proves: "I have valid COLD/LP/HEAT claim")
         │    (hides: which specific claim)
         │
         ▼
┌─────────────────┐
│  Privacy Pool   │
│   Contract      │
└────────┬────────┘
         │
         │ 2. Verify ZK proof
         │ 3. Mark claim as "pending" (encrypted)
         │ 4. Wait for batch threshold (e.g., 10 claims)
         │
         ▼
┌─────────────────┐
│  Batch Claims   │
│  (10 users)     │
└────────┬────────┘
         │
         │ 5. Process all 10 claims simultaneously
         │ 6. Mint CD tokens to pool
         │ 7. Users withdraw to fresh addresses
         │    (using separate ZK proof)
         │
         ▼
┌─────────────────┐
│  User A Gets CD │
│  at Fresh Addr  │
└─────────────────┘
```

**Anonymity Set:** If 10 users batch together, each user has 1/10 chance of being identified (10% probability vs 100% without tumbler).

---

## 🛠️ **Implementation Options**

### **Option 1: Simple Commitment Pool (No ZK) - EASIEST**

**Difficulty:** Low (3-4 days)
**Privacy Level:** Medium
**Gas Cost:** ~150k gas per claim

#### **How It Works:**

1. **Deposit Phase:**
   ```solidity
   function depositClaim(bytes32 commitment, uint256 cdAmount) external {
       // User commits to future withdrawal address (hashed)
       // Transfers CD tokens to pool
       commitments[commitment] = Commitment({
           amount: cdAmount,
           timestamp: block.timestamp,
           claimed: false
       });

       // User waits for others to join pool
   }
   ```

2. **Withdrawal Phase (after delay):**
   ```solidity
   function withdraw(
       bytes32 secret,      // Reveals commitment
       address recipient    // Fresh address
   ) external {
       bytes32 commitment = keccak256(abi.encodePacked(secret, recipient));
       require(commitments[commitment].exists, "Invalid commitment");
       require(!commitments[commitment].claimed, "Already claimed");
       require(block.timestamp > commitments[commitment].timestamp + MIN_DELAY, "Too soon");

       // Mark as claimed
       commitments[commitment].claimed = true;

       // Send CD to fresh address
       cdToken.transfer(recipient, commitments[commitment].amount);
   }
   ```

**Privacy Mechanism:**
- Commitment hides recipient address
- Time delay separates deposit → withdraw
- If 10 people deposit and withdraw in same period, 1/10 anonymity

**Limitations:**
- Amount visible (unless using fixed denominations)
- Timing correlation possible
- No cryptographic anonymity

**Cost:** ~$50-100 in gas (2 transactions: deposit + withdraw)

---

### **Option 2: Fixed-Denomination Pool - BETTER**

**Difficulty:** Medium (5-7 days)
**Privacy Level:** High
**Gas Cost:** ~200k gas per claim

#### **How It Works:**

Force all deposits into fixed denominations (e.g., 0.001 CD, 0.01 CD, 0.1 CD, 1 CD):

```solidity
// Pool for 0.01 CD denomination
contract CDTumbler_001 {
    uint256 public constant DENOMINATION = 10_000_000_000; // 0.01 CD (12 decimals)

    mapping(bytes32 => bool) public commitments;
    mapping(bytes32 => bool) public nullifiers;

    function deposit(bytes32 commitment) external {
        // User must deposit EXACTLY 0.01 CD
        require(cdToken.transferFrom(msg.sender, address(this), DENOMINATION));
        require(!commitments[commitment], "Commitment exists");

        commitments[commitment] = true;
        emit Deposit(commitment, block.timestamp);
    }

    function withdraw(
        bytes32 nullifier,
        address recipient,
        bytes32 secret
    ) external {
        // Verify commitment
        bytes32 commitment = keccak256(abi.encodePacked(nullifier, secret));
        require(commitments[commitment], "Invalid commitment");
        require(!nullifiers[nullifier], "Already withdrawn");

        // Mark as used
        nullifiers[nullifier] = true;

        // Send to fresh address
        cdToken.transfer(recipient, DENOMINATION);
        emit Withdrawal(recipient, nullifier);
    }
}
```

**Privacy Benefits:**
- All deposits/withdrawals same size (no amount correlation)
- Users split large amounts across multiple deposits
- Larger anonymity set (everyone using 0.01 CD pool)

**Example:**
- User has 0.05 CD to claim
- Deposits into 0.01 CD pool 5 times (separate commitments)
- Withdraws to 5 different fresh addresses over time

**Anonymity Set:** If pool has 100 deposits, each withdrawal has 1/100 chance (1% probability).

---

### **Option 3: ZK-SNARK Tumbler (Tornado Cash Style) - BEST**

**Difficulty:** High (3-4 weeks)
**Privacy Level:** Maximum
**Gas Cost:** ~500k gas per claim (ZK proof verification expensive)

#### **How It Works:**

Use ZK-SNARKs to prove claim eligibility without revealing which claim:

```solidity
contract CDTumblerZK {
    uint256 public constant DENOMINATION = 10_000_000_000; // 0.01 CD

    // Merkle tree of all commitments
    uint256 public merkleRoot;
    mapping(bytes32 => bool) public nullifiers;

    // ZK verifier contract
    IVerifier public verifier;

    function deposit(bytes32 commitment) external {
        require(cdToken.transferFrom(msg.sender, address(this), DENOMINATION));

        // Add to merkle tree
        _insertCommitment(commitment);
        emit Deposit(commitment);
    }

    function withdraw(
        bytes calldata zkProof,
        bytes32 nullifier,
        address recipient
    ) external {
        require(!nullifiers[nullifier], "Already withdrawn");

        // Verify ZK proof
        // Proves: "I know a secret for a commitment in the tree"
        // Hides: which specific commitment
        require(
            verifier.verifyProof(
                zkProof,
                [merkleRoot, uint256(nullifier), uint256(uint160(recipient))]
            ),
            "Invalid proof"
        );

        // Mark nullifier used
        nullifiers[nullifier] = true;

        // Send to recipient
        cdToken.transfer(recipient, DENOMINATION);
        emit Withdrawal(recipient, nullifier);
    }
}
```

**Privacy Benefits:**
- ✅ **Perfect anonymity:** No link between deposit → withdrawal
- ✅ **Large anonymity set:** All historical deposits in same denomination
- ✅ **Cryptographically secure:** Cannot break even with full blockchain analysis

**ZK Circuit Proves:**
```
Public Inputs:
- merkleRoot (all deposits)
- nullifier (prevents double-spend)
- recipient (where to send)

Private Inputs:
- secret (your unique secret)
- pathElements (merkle proof)
- pathIndices (merkle proof)

Circuit Logic:
1. Compute commitment = hash(nullifier, secret)
2. Verify commitment exists in merkle tree
3. Output is valid only if proof is correct
```

**Anonymity Set:** If pool has 10,000 deposits, each withdrawal has 1/10,000 chance (0.01% probability).

---

## 💡 **Practical Privacy Strategies (No Contract Changes)**

### **Strategy 1: Claim Relayer Service**

**Difficulty:** Low (2-3 days)
**Privacy Level:** Medium
**Cost:** ~$5-10 relayer fee

**How It Works:**
```typescript
// User signs claim request off-chain
const claimRequest = {
  recipient: freshAddress,
  tier: userTier,
  nullifier: userNullifier,
  // ... other claim data
};

const signature = await wallet.signMessage(JSON.stringify(claimRequest));

// Submit to relayer service (off-chain)
await fetch('https://relayer.usexfg.org/submit-claim', {
  method: 'POST',
  body: JSON.stringify({ claimRequest, signature })
});

// Relayer batches multiple claims and submits
// User's address never touches the claim transaction
```

**Relayer Contract:**
```solidity
function batchClaim(
    ClaimRequest[] calldata requests,
    bytes[] calldata signatures
) external onlyRelayer {
    for (uint i = 0; i < requests.length; i++) {
        // Verify signature
        address signer = recoverSigner(requests[i], signatures[i]);

        // Claim on behalf of user
        _processClaim(requests[i], signer);
    }
}
```

**Privacy Benefits:**
- User address never appears in claim transaction
- Relayer pays gas (user reimburses via separate tx)
- Can batch multiple users' claims together

**Privacy Risks:**
- Relayer knows user → recipient mapping (trust required)
- Still traceable if relayer is compromised

---

### **Strategy 2: Time-Delayed Fresh Address**

**Difficulty:** Very Low (frontend only)
**Privacy Level:** Low-Medium
**Cost:** Free

**How It Works:**
1. User generates fresh address
2. **Waits 1-7 days** before claiming to it
3. During wait, other users also claim
4. Timing correlation harder to establish

**Frontend Feature:**
```typescript
function schedulePrivateClaim(tier: number, delayDays: number) {
  const freshAddress = ethers.Wallet.createRandom().address;
  const claimDate = Date.now() + (delayDays * 24 * 60 * 60 * 1000);

  // Store encrypted claim info locally
  const encrypted = encryptClaimData({
    tier,
    freshAddress,
    claimDate
  }, userPassword);

  localStorage.setItem('pendingClaim', encrypted);

  // Remind user when it's time
  scheduleNotification(claimDate, 'Time to claim your CD rewards!');
}
```

**Privacy Benefits:**
- Breaks temporal correlation
- Blends with crowd if many users adopt
- No additional cost

**Privacy Risks:**
- Still linkable if only user claiming that tier that day
- Requires user discipline (remember to claim later)

---

### **Strategy 3: Stealth Address Protocol**

**Difficulty:** Medium (1-2 weeks, requires wallet integration)
**Privacy Level:** High
**Cost:** ~$20-30 in extra gas

**How It Works:**
```typescript
// User publishes "stealth meta-address" once
const stealthMetaAddress = {
  spendPubKey: generateSpendKey(),
  viewPubKey: generateViewKey()
};

// Claimer generates one-time stealth address
function generateStealthAddress(recipientMeta: StealthMetaAddress) {
  const ephemeralKey = ethers.Wallet.createRandom();

  // Compute stealth address (ECDH)
  const sharedSecret = ecdh(ephemeralKey.privateKey, recipientMeta.spendPubKey);
  const stealthAddress = deriveAddress(sharedSecret);

  return {
    stealthAddress,
    ephemeralPubKey: ephemeralKey.publicKey  // Published on-chain
  };
}

// Claim CD to stealth address
const { stealthAddress, ephemeralPubKey } = generateStealthAddress(userStealthMeta);

await coldVerifier.claimCD(
  stealthAddress,  // CD sent here (unlinkable)
  tier,
  nullifier,
  // ...
);

// User scans blockchain for their stealth payments
function scanForStealthPayments(userViewKey: string) {
  // Check all CD mint events
  const events = await cdToken.queryFilter(cdToken.filters.Transfer(null, null));

  for (const event of events) {
    const ephemeralPubKey = extractEphemeralKey(event);
    const sharedSecret = ecdh(userViewKey, ephemeralPubKey);
    const expectedAddress = deriveAddress(sharedSecret);

    if (event.args.to === expectedAddress) {
      // This payment is for me!
      const privateKey = derivePrivateKey(sharedSecret);
      // Can now spend from stealth address
    }
  }
}
```

**Privacy Benefits:**
- Each claim goes to unique, unlinkable address
- No address reuse ever
- Recipient controls all stealth addresses (trustless)

**Privacy Risks:**
- Stealth meta-address publicly linked to identity (if not careful)
- Requires wallet support (MetaMask plugin, custom interface)

---

## 📊 **Difficulty vs Privacy Matrix**

| Solution | Implementation Time | Privacy Level | Gas Cost | Trust Required |
|----------|-------------------|---------------|----------|----------------|
| **Fresh Address (current)** | 0 days | Low | $10 | None |
| **Time-Delayed Claim** | 1 day (frontend) | Low-Medium | $10 | None |
| **Claim Relayer** | 2-3 days | Medium | $15 | Relayer |
| **Commitment Pool** | 3-4 days | Medium | $20 | None |
| **Fixed-Denomination** | 5-7 days | High | $30 | None |
| **Stealth Addresses** | 1-2 weeks | High | $30 | None |
| **ZK Tumbler** | 3-4 weeks | Maximum | $100 | None |

---

## 🚀 **Recommended Implementation Path**

### **Phase 1: Quick Wins (Week 1-2)**

1. **Claim Relayer Service**
   - Deploy relayer contract (2 days)
   - Build relayer backend (2 days)
   - Batch multiple claims together
   - **Privacy gain:** 50% (breaks direct address linkage)

2. **Frontend Time-Delay Feature**
   - Add "Schedule Private Claim" button (1 day)
   - Let users delay claims by 1-7 days
   - **Privacy gain:** 20% (temporal decorrelation)

**Total Time:** 1 week
**Total Privacy Improvement:** ~70%

### **Phase 2: Medium Gains (Week 3-5)**

3. **Fixed-Denomination Tumbler**
   - Deploy 4-5 denomination pools (0.001, 0.01, 0.1, 1 CD)
   - Users split claims across pools
   - Wait for pool to accumulate deposits before withdrawing
   - **Privacy gain:** 80% (large anonymity set)

**Total Time:** 3 weeks cumulative
**Total Privacy Improvement:** ~90%

### **Phase 3: Maximum Privacy (Week 6-10)**

4. **ZK-SNARK Tumbler**
   - Design ZK circuit (1 week)
   - Implement & test circuit (2 weeks)
   - Deploy verifier contract (1 week)
   - Build frontend (1 week)
   - **Privacy gain:** 99% (cryptographic anonymity)

**Total Time:** 10 weeks cumulative
**Total Privacy Improvement:** ~99%

---

## 💰 **Cost-Benefit Analysis**

### **For Users:**

**Current (no privacy):**
- Gas: $10
- Privacy: 0%

**With Relayer:**
- Gas: $15
- Privacy: 50%
- **Worth it?** Yes (small cost for big gain)

**With Fixed-Denomination Pool:**
- Gas: $30
- Privacy: 80%
- **Worth it?** Yes for large claims (>$1000)

**With ZK Tumbler:**
- Gas: $100
- Privacy: 99%
- **Worth it?** Only for very large claims (>$10,000)

### **For Protocol:**

**Development Costs:**
- Relayer: 1 week dev time (~$5k)
- Fixed-Denom: 2 weeks (~$10k)
- ZK Tumbler: 5 weeks (~$25k)

**Ongoing Costs:**
- Relayer hosting: ~$50/month
- ZK circuit maintenance: ~$200/month (if updated)

**Benefits:**
- Attracts privacy-conscious users
- Differentiates from competitors
- Reduces on-chain surveillance risk

---

## 🔧 **Minimal Viable Tumbler (1 Week Build)**

Here's what I'd build first:

```solidity
// CDPrivacyPool.sol - Simple commitment-based tumbler
contract CDPrivacyPool {
    IERC20 public cdToken;

    struct Commitment {
        uint256 amount;
        uint256 timestamp;
        bool claimed;
    }

    mapping(bytes32 => Commitment) public commitments;
    uint256 public constant MIN_DELAY = 1 days;

    event Deposit(bytes32 indexed commitment, uint256 amount, uint256 timestamp);
    event Withdrawal(address indexed recipient, uint256 amount);

    function deposit(bytes32 commitment, uint256 amount) external {
        require(amount > 0, "Invalid amount");
        require(!commitments[commitment].exists, "Commitment exists");

        // Transfer CD to pool
        cdToken.transferFrom(msg.sender, address(this), amount);

        // Store commitment
        commitments[commitment] = Commitment({
            amount: amount,
            timestamp: block.timestamp,
            claimed: false
        });

        emit Deposit(commitment, amount, block.timestamp);
    }

    function withdraw(bytes32 secret, address recipient) external {
        bytes32 commitment = keccak256(abi.encodePacked(secret, recipient));

        Commitment storage c = commitments[commitment];
        require(c.amount > 0, "Invalid commitment");
        require(!c.claimed, "Already claimed");
        require(block.timestamp >= c.timestamp + MIN_DELAY, "Too soon");

        // Mark as claimed
        c.claimed = true;

        // Send to recipient
        cdToken.transfer(recipient, c.amount);

        emit Withdrawal(recipient, c.amount);
    }
}
```

**Usage:**
```typescript
// 1. User generates commitment
const secret = ethers.hexlify(ethers.randomBytes(32));
const freshAddress = ethers.Wallet.createRandom().address;
const commitment = ethers.keccak256(
  ethers.solidityPacked(['bytes32', 'address'], [secret, freshAddress])
);

// 2. Deposit CD
await cdToken.approve(privacyPool.address, cdAmount);
await privacyPool.deposit(commitment, cdAmount);

// 3. Wait 1+ days...

// 4. Withdraw to fresh address
await privacyPool.withdraw(secret, freshAddress);
```

**Gas Cost:** ~200k gas total (~$20 at 50 gwei)
**Privacy:** Medium (anonymity set = concurrent users)
**Time to Build:** 3-4 days

---

## ✅ **Recommendation**

**Start with Phase 1 (1 week):**
1. Build simple commitment pool (above)
2. Add claim relayer service
3. Measure adoption

**If users adopt (>10% usage):**
- Move to Phase 2 (fixed denominations)
- Larger anonymity sets = better privacy

**If users demand maximum privacy:**
- Invest in Phase 3 (ZK tumbler)
- Only worth it if whales need privacy for large claims

**Realistic Outcome:**
- 80% of users: Use relayer ($15 gas, 50% privacy)
- 15% of users: Use fixed-denom pool ($30 gas, 80% privacy)
- 5% of users: Use ZK tumbler ($100 gas, 99% privacy)

---

**Winter is coming. ❄️**
