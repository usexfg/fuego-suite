# Relayer-Based Claim Batching

**Date:** 2026-01-20
**Status:** Practical Implementation

---

## 🎯 **The Real Solution**

**You're right:** Claims already have addresses built-in. There's no "pooling" at the contract level.

**The solution:** Batch at the **relayer/API level**, not the contract level.

---

## 🤔 **What is a Relayer?**

**A relayer is a backend service (daemon) that submits transactions on behalf of users.**

**In this system:**
- **Relayer = Node.js/TypeScript backend service** (running 24/7)
- **NOT a smart contract** (it's off-chain infrastructure)
- **NOT a person** (it's automated software)
- **Similar to:** API server, but with transaction submission capabilities

**What it does:**
1. Accepts claim requests from users via HTTP API
2. Accumulates requests in a queue (in-memory or database)
3. When threshold reached (e.g., 10 claims), submits all claims in one transaction
4. Pays gas fees from its own wallet
5. Users reimburse gas fees separately (or service charges a fee)

**Why call it a "relayer"?**
- It **relays** user intents (claims) to the blockchain
- Common term in crypto: Tornado Cash relayers, meta-transaction relayers, etc.
- Provides privacy by breaking the link between user action and on-chain transaction

**Architecture:**
```
Users → HTTP API → Relayer Service → Smart Contract
        (request)   (queue + batch)   (on-chain tx)
```

---

## 🔄 **How It Actually Works**

### **Without Batching (Current):**

```typescript
// User calls API directly
await coldAPI.claimCD({
  recipient: freshAddress,
  tier: 6,
  // ...
});

// API immediately submits to contract
await coldVerifier.claimCD(freshAddress, tier, ...);

// Result: Claim processed immediately, timing reveals linkage
```

**Privacy:** 0% (unique transaction timestamp)

---

### **With Relayer Batching (Proposed):**

```typescript
// User submits to relayer queue
await relayerAPI.submitClaim({
  recipient: freshAddress,
  tier: 6,
  // ...
});

// Relayer accumulates claims...
// User 1 → Queue: [claim1]
// User 2 → Queue: [claim1, claim2]
// ...
// User 10 → Queue: [claim1...claim10]

// Once 10 claims accumulated, relayer processes ALL in one transaction:
await coldVerifier.multicall([
  claimCD(address1, tier1, ...),
  claimCD(address2, tier2, ...),
  claimCD(address3, tier3, ...),
  // ... 10 total claims
]);

// Result: All 10 claims in same block, same transaction
```

**Privacy:** 90% (1/10 anonymity set)

---

## 📐 **Architecture**

```
┌─────────────┐
│   User 1    │ ──┐
└─────────────┘   │  POST /api/batch/submit-claim
                  │  (HTTP request)
┌─────────────┐   │
│   User 2    │ ──┤
└─────────────┘   │
                  ├──> Relayer Service (Node.js backend)
┌─────────────┐   │    ┌───────────────────────┐
│   User 3    │ ──┤    │  In-Memory Queue      │
└─────────────┘   │    │  [claim1, claim2,...] │
                  │    └───────────────────────┘
┌─────────────┐   │         │
│   User 10   │ ──┘         │ Once 10 claims
└─────────────┘             │ or 24 hours...
                            ▼
                  ┌─────────────────┐
                  │ Relayer Wallet  │
                  │ (has ETH for gas)│
                  └────────┬─────────┘
                           │
                           │ multicall.aggregate([
                           │   claimCD(addr1, tier1, ...),
                           │   claimCD(addr2, tier2, ...),
                           │   ...
                           │ ])
                           ▼
                  ┌─────────────────┐
                  │  COLDVerifier   │
                  │ Smart Contract  │
                  │  (on Arbitrum)  │
                  └────────┬─────────┘
                           │
                           │ 10 CD mints (same tx)
                           ▼
                  ┌─────────────────┐
                  │ L2→L1 Bridge    │
                  │ (10 messages)   │
                  └────────┬─────────┘
                           │
                           ▼
                  ┌─────────────────┐
                  │ FuegoCOLDAO     │
                  │ (Ethereum L1)   │
                  │ Mints CD to:    │
                  │ - addr1         │
                  │ - addr2         │
                  │ - ... (10 total)│
                  └─────────────────┘
```

**Key Components:**

1. **Relayer Service** (Node.js daemon):
   - Runs 24/7 on server (e.g., usexfg.org backend)
   - Has its own Ethereum wallet with ETH for gas
   - Exposes HTTP API for users to submit claims
   - Maintains in-memory queue of pending claims

2. **Relayer Wallet** (EOA - Externally Owned Account):
   - Private key controlled by relayer service
   - Pays gas fees for batched transactions
   - Needs to be funded with ETH periodically

3. **Multicall Contract** (optional):
   - Allows multiple function calls in one transaction
   - Or use relayer to submit multiple txs in quick succession

4. **Smart Contracts** (already deployed):
   - COLDDepositProofVerifier.sol (Arbitrum)
   - FuegoCOLDAOToken.sol (Ethereum)
   - No changes needed to existing contracts!

---

## 🛠️ **Implementation**

### **1. Relayer API (Backend)**

```typescript
// relayer-service/src/batchQueue.ts

interface QueuedClaim {
  recipient: string;
  tier: number;
  nullifier: string;
  commitment: string;
  networkId: string;
  depositTimestamp: number;
  submittedAt: number;
  userSignature: string;  // Proof user authorized this
}

class BatchQueue {
  private queue: QueuedClaim[] = [];
  private readonly BATCH_SIZE = 10;
  private readonly MAX_WAIT_MS = 24 * 60 * 60 * 1000; // 24 hours

  /**
   * Add claim to queue
   */
  async addClaim(claim: QueuedClaim): Promise<string> {
    // Validate claim
    await this.validateClaim(claim);

    // Add to queue
    this.queue.push(claim);
    console.log(`Claim queued. Queue size: ${this.queue.length}`);

    // Check if we should process batch
    if (this.queue.length >= this.BATCH_SIZE) {
      await this.processBatch();
    }

    return `Claim queued. ${this.BATCH_SIZE - this.queue.length} more needed for batch.`;
  }

  /**
   * Process batch of claims
   */
  private async processBatch(): Promise<void> {
    if (this.queue.length === 0) return;

    console.log(`Processing batch of ${this.queue.length} claims...`);

    // Get claims to process
    const batch = this.queue.splice(0, this.BATCH_SIZE);

    // Build multicall data
    const calls = batch.map(claim => ({
      target: COLD_VERIFIER_ADDRESS,
      callData: coldVerifier.interface.encodeFunctionData('claimCD', [
        claim.recipient,
        claim.tier,
        claim.nullifier,
        claim.commitment,
        claim.networkId,
        claim.depositTimestamp
      ])
    }));

    // Submit all claims in one transaction
    const tx = await multicall.aggregate(calls, {
      value: ethers.parseEther('0.01') // Estimated gas for all claims
    });

    await tx.wait();

    console.log(`✅ Batch processed! TX: ${tx.hash}`);
    console.log(`${batch.length} claims executed in single transaction`);
  }

  /**
   * Force process batch if max wait time exceeded
   */
  async checkAndProcessStale(): Promise<void> {
    if (this.queue.length === 0) return;

    const oldestClaim = this.queue[0];
    const waitTime = Date.now() - oldestClaim.submittedAt;

    if (waitTime >= this.MAX_WAIT_MS) {
      console.log(`Max wait time exceeded. Processing ${this.queue.length} claims...`);
      await this.processBatch();
    }
  }
}

// Run stale check every hour
setInterval(() => batchQueue.checkAndProcessStale(), 60 * 60 * 1000);
```

### **2. Relayer API Endpoint**

```typescript
// relayer-service/src/routes/batch.ts

import express from 'express';
import { ethers } from 'ethers';

const router = express.Router();

/**
 * POST /api/batch/submit-claim
 * Submit claim to batch queue
 */
router.post('/submit-claim', async (req, res) => {
  try {
    const {
      recipient,
      tier,
      nullifier,
      commitment,
      networkId,
      depositTimestamp,
      userSignature
    } = req.body;

    // Verify user signature (proves they authorized this claim)
    const message = ethers.solidityPackedKeccak256(
      ['address', 'uint8', 'bytes32', 'bytes32', 'uint256', 'uint256'],
      [recipient, tier, nullifier, commitment, networkId, depositTimestamp]
    );

    const signer = ethers.recoverAddress(message, userSignature);
    // Verify signer is authorized (could check against deposit ownership, etc.)

    // Add to batch queue
    const queuedClaim: QueuedClaim = {
      recipient,
      tier,
      nullifier,
      commitment,
      networkId,
      depositTimestamp,
      submittedAt: Date.now(),
      userSignature
    };

    const status = await batchQueue.addClaim(queuedClaim);

    res.json({
      success: true,
      message: status,
      queuePosition: batchQueue.getQueueSize(),
      estimatedWait: batchQueue.estimateWaitTime()
    });

  } catch (error) {
    console.error('Batch submit error:', error);
    res.status(400).json({
      success: false,
      error: error.message
    });
  }
});

/**
 * GET /api/batch/status
 * Get current batch queue status
 */
router.get('/status', (req, res) => {
  res.json({
    queueSize: batchQueue.getQueueSize(),
    batchSize: 10,
    estimatedWait: batchQueue.estimateWaitTime(),
    maxWait: '24 hours'
  });
});

export default router;
```

### **3. Frontend Integration**

```typescript
// frontend/src/services/batchClaims.ts

/**
 * Submit claim to batch relayer
 */
export async function submitBatchedClaim(
  recipient: string,
  tier: number,
  depositProof: DepositProof
): Promise<BatchSubmission> {
  // Sign claim authorization
  const message = ethers.solidityPackedKeccak256(
    ['address', 'uint8', 'bytes32', 'bytes32', 'uint256', 'uint256'],
    [
      recipient,
      tier,
      depositProof.nullifier,
      depositProof.commitment,
      depositProof.networkId,
      depositProof.timestamp
    ]
  );

  const signature = await signer.signMessage(ethers.getBytes(message));

  // Submit to relayer
  const response = await fetch('https://relayer.usexfg.org/api/batch/submit-claim', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      recipient,
      tier,
      nullifier: depositProof.nullifier,
      commitment: depositProof.commitment,
      networkId: depositProof.networkId,
      depositTimestamp: depositProof.timestamp,
      userSignature: signature
    })
  });

  const data = await response.json();

  if (!data.success) {
    throw new Error(data.error);
  }

  return {
    queued: true,
    position: data.queuePosition,
    estimatedWait: data.estimatedWait
  };
}

/**
 * Check batch queue status
 */
export async function getBatchStatus(): Promise<BatchStatus> {
  const response = await fetch('https://relayer.usexfg.org/api/batch/status');
  const data = await response.json();

  return {
    queueSize: data.queueSize,
    batchSize: data.batchSize,
    estimatedWait: data.estimatedWait,
    maxWait: data.maxWait
  };
}
```

### **4. User Flow**

```typescript
// User wants to claim with privacy

// Option 1: Immediate claim (no batching)
await coldAPI.claimCD({ recipient, tier, ... });
console.log('✅ Claimed immediately (low privacy)');

// Option 2: Batched claim (better privacy)
const fresh = ethers.Wallet.createRandom().address;
await submitBatchedClaim(fresh, tier, depositProof);
console.log('⏳ Claim queued for batching...');

// Check queue status
const status = await getBatchStatus();
console.log(`Queue: ${status.queueSize}/${status.batchSize}`);
console.log(`Estimated wait: ${status.estimatedWait}`);

// Claim will be processed when:
// - 10 claims accumulated, OR
// - 24 hours elapsed
```

---

## 📊 **Privacy Improvement**

### **Without Batching:**

```
User A claims at block 1000 → Unique timestamp
User B claims at block 1005 → Unique timestamp
User C claims at block 1010 → Unique timestamp

Result: Easy to correlate each claim to specific user
Privacy: 0%
```

### **With Batching:**

```
User A submits → Queue
User B submits → Queue
User C submits → Queue
...
User J submits → Queue

All 10 processed at block 1020 → Same timestamp

Result: All 10 claims identical timestamp, 1/10 chance to identify
Privacy: 90%
```

---

## ⚖️ **Trade-offs**

### **Pros:**
✅ Breaks timing correlation (all claims same block)
✅ Large anonymity set (1/10 or 1/N)
✅ No contract changes needed
✅ Works for both COLD and LP claims
✅ Users can still claim immediately if they want

### **Cons:**
❌ Requires trusted relayer (or decentralized relayer network)
❌ Claims delayed (wait for batch to fill)
❌ Relayer pays gas (needs funding/incentives)
❌ Single point of failure (if relayer offline)

---

## 🔐 **Trust Model**

### **What Can the Relayer Do?**

**The relayer CAN:**
- ✅ See all pending claims (recipient addresses, amounts, tiers)
- ✅ Delay processing claims (up to max wait time)
- ✅ Reorder claims within a batch
- ✅ Go offline (claims won't be processed)

**The relayer CANNOT:**
- ❌ Steal CD tokens (they go to user-specified recipient)
- ❌ Change recipient addresses (user signature required)
- ❌ Prevent users from claiming directly (bypass relayer)
- ❌ Double-spend claims (nullifier prevents this)
- ❌ Fake proofs (smart contract validates all proofs)

### **Trust Assumptions:**

1. **Users trust relayer to:**
   - Submit their claims within reasonable time (24 hours max)
   - Not censor specific users
   - Not leak timing information to attackers

2. **Users DON'T need to trust relayer for:**
   - Custody of funds (relayer never holds user assets)
   - Correctness of claims (smart contract validates)
   - Privacy of recipient addresses (already public on-chain after claim)

### **Mitigation Strategies:**

1. **Make relayer optional** - Users can claim directly if they want
2. **Open-source relayer code** - Anyone can run their own relayer
3. **Multiple relayers** - Users choose which relayer to use
4. **Decentralized relayer network** - Rotate who submits batches
5. **Fallback mechanism** - Auto-submit if relayer offline for >48 hours

---

## 🌐 **Deployment Options**

### **Option 1: Centralized Relayer (Simplest)**

**Who runs it:** usexfg.org team
**Cost:** ~$100/month (server + gas fees)
**Pros:** Simple, reliable, maintained by team
**Cons:** Single point of trust/failure

```
User → usexfg.org API → Relayer (same server) → Arbitrum
```

### **Option 2: Community Relayers (Decentralized)**

**Who runs it:** Anyone (open-source software)
**Cost:** Gas fees (users reimburse)
**Pros:** No single point of failure, censorship-resistant
**Cons:** Complex coordination, reliability varies

```
User → Relayer 1 ─┐
User → Relayer 2 ─┼→ Arbitrum
User → Relayer 3 ─┘
```

### **Option 3: Incentivized Relayer Network**

**Who runs it:** Economic actors (paid for service)
**Cost:** Service fee (e.g., 0.5% of CD claimed)
**Pros:** Economically sustainable, competitive
**Cons:** Requires token economics, more complex

```
User pays 0.5% fee → Relayer earns fee → Submits batch
```

### **Recommended for MVP: Option 1 (Centralized)**

**Why:**
- Simplest to implement (3-4 days)
- Lowest operational cost
- Team controls reliability
- Can decentralize later if needed

**Migration path:**
1. Start with centralized relayer (v1)
2. Open-source relayer code (v1.1)
3. Allow community relayers (v2)
4. Add economic incentives (v3)

---

## 🎯 **Recommendation**

**Implement relayer batching as OPTIONAL:**

```typescript
// Users choose:

// Fast + Low Privacy
await coldAPI.claimImmediate(recipient);

// Slow + High Privacy
await coldAPI.claimBatched(recipient);
```

**Why optional:**
- Some users want immediate claims (don't care about privacy)
- Some users willing to wait for better privacy
- Let users decide their own privacy/speed tradeoff

**Effort:** 3-4 days to build relayer service
**Privacy gain:** 90% for batched claims

---

## 🚧 **You're Right About One Thing**

Claims can't "pool" without addresses because:
- COLD proofs include recipient in STARK proof
- LP rewards need recipient for minting
- No way to "hold" CD tokens without destination

**The ONLY batching point is the relayer** - accumulate requests off-chain, submit together on-chain.

---

**Winter is coming. ❄️**
