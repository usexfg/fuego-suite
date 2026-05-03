# COLD Proof Submission Flow with Domain Linking

**Date:** 2026-01-18
**Status:** Design Document

---

## 🎯 **Overview**

The COLD proof submission process uses **domain linking** to bind the user's browser session to their Fuego deposit, ensuring that:
1. The API fetches valid data from a trusted Fuego daemon
2. The browser timestamp validates the proof freshness
3. The user controls which domain receives their proof data
4. Replay attacks are prevented via nullifier + commitment

---

## 🔐 **Security Model**

### **Trust Boundaries:**
- **User trusts:** Their own browser, usexfg.org API
- **API trusts:** Fuego daemon node (reads blockchain data)
- **Contract trusts:** API verifier address (validates proofs off-chain)

### **Threat Model:**
- ❌ Malicious user tries to fake deposit data
- ❌ Malicious domain tries to steal proof
- ❌ Man-in-the-middle tries to replay proof
- ❌ API tries to submit proof to wrong recipient
- ❌ Stale proof submitted after deposit is unlocked

---

## 📊 **Flow Diagram**

```
┌─────────────┐
│   Browser   │
│  (usexfg.org)│
└──────┬──────┘
       │ 1. User initiates COLD claim
       │    - Connects wallet
       │    - Signs domain binding message
       │
       ▼
┌─────────────┐
│   Browser   │
│  Generates  │
│  Timestamp  │
└──────┬──────┘
       │ 2. Browser creates claim request
       │    - Current timestamp (browser time)
       │    - Domain: usexfg.org
       │    - Recipient address (from wallet)
       │    - Deposit tx hash (user provides)
       │
       ▼
┌─────────────┐
│  usexfg.org │
│     API     │
└──────┬──────┘
       │ 3. API validates request
       │    - Verify domain binding signature
       │    - Check timestamp freshness (±5 min)
       │    - Validate recipient address
       │
       ▼
┌─────────────┐
│    Fuego    │
│   Daemon    │
└──────┬──────┘
       │ 4. API queries Fuego daemon
       │    - Fetch deposit transaction
       │    - Verify deposit status (locked, not unlocked)
       │    - Extract: amount, lock period, timestamp
       │    - Verify nullifier not used
       │
       ▼
┌─────────────┐
│  usexfg.org │
│  Generates  │
│    Proof    │
└──────┬──────┘
       │ 5. API generates STARK proof
       │    - Input: deposit tx data + recipient
       │    - Output: proof + nullifier + commitment
       │    - Validate proof locally
       │
       ▼
┌─────────────┐
│  Arbitrum   │
│   Sepolia   │
└──────┬──────┘
       │ 6. API submits to COLDDepositProofVerifier
       │    - Call claimCD() with proof data
       │    - Include L1 gas fee
       │    - Emit events
       │
       ▼
┌─────────────┐
│  Ethereum   │
│   Sepolia   │
└──────┬──────┘
       │ 7. L2→L1 message relay (~10 min)
       │    - Arbitrum bridge relays message
       │    - FuegoCOLDAOToken.mintFromL2()
       │    - CD tokens minted to recipient
       │
       ▼
┌─────────────┐
│   Browser   │
│  Shows CD   │
│   Balance   │
└─────────────┘
```

---

## 🔗 **Domain Linking Mechanism**

### **Purpose:**
Bind the proof request to a specific domain (usexfg.org) to prevent:
- Phishing sites from stealing proofs
- Malicious frontends from redirecting claims
- Cross-site replay attacks

### **Implementation:**

#### **1. Browser Signs Domain Binding Message**

```typescript
// User's wallet signs this message
const domainBindingMessage = {
  domain: "usexfg.org",
  recipient: userAddress,
  depositTxHash: depositTxHash,
  timestamp: Date.now(), // Browser timestamp in milliseconds
  nonce: randomNonce()   // Prevent replay
};

// EIP-712 typed data signature
const signature = await wallet.signTypedData({
  domain: {
    name: "COLD Deposits",
    version: "1",
    chainId: 421614, // Arbitrum Sepolia
  },
  types: {
    DomainBinding: [
      { name: "domain", type: "string" },
      { name: "recipient", type: "address" },
      { name: "depositTxHash", type: "bytes32" },
      { name: "timestamp", type: "uint256" },
      { name: "nonce", type: "bytes32" }
    ]
  },
  message: domainBindingMessage
});
```

#### **2. API Validates Domain Binding**

```typescript
// API receives request
interface ClaimRequest {
  domainBinding: DomainBindingMessage;
  signature: string;
}

// Validate signature
const recoveredAddress = ethers.verifyTypedData(
  domain,
  types,
  domainBindingMessage,
  signature
);

// Check domain matches
if (domainBinding.domain !== "usexfg.org") {
  throw new Error("Invalid domain");
}

// Check signer matches recipient
if (recoveredAddress !== domainBinding.recipient) {
  throw new Error("Signature mismatch");
}

// Check timestamp freshness (±5 minutes)
const now = Date.now();
const diff = Math.abs(now - domainBinding.timestamp);
if (diff > 5 * 60 * 1000) {
  throw new Error("Timestamp too old or too far in future");
}

// Check nonce not used (prevent replay)
if (await isNonceUsed(domainBinding.nonce)) {
  throw new Error("Nonce already used");
}

// Mark nonce as used
await markNonceUsed(domainBinding.nonce);
```

---

## ⏰ **Timestamp Validation**

### **Why Timestamp Matters:**
1. **Proof Freshness:** Ensure proof is recent (not replayed from old session)
2. **Deposit Status:** Verify deposit is still locked (not unlocked)
3. **Legacy Detection:** Determine if deposit qualifies for 80% APY (pre-2026)

### **Three Timestamps:**

| Timestamp | Source | Purpose |
|-----------|--------|---------|
| **Browser Timestamp** | User's browser (`Date.now()`) | Proof freshness validation |
| **Deposit Timestamp** | Fuego blockchain (deposit tx time) | Legacy rate determination |
| **Block Timestamp** | Arbitrum/Ethereum (when proof submitted) | On-chain verification |

### **Validation Rules:**

```typescript
// 1. Browser timestamp freshness (API validates)
const browserTimestamp = domainBinding.timestamp;
const serverTimestamp = Date.now();
const diff = Math.abs(serverTimestamp - browserTimestamp);

if (diff > 5 * 60 * 1000) { // 5 minutes
  throw new Error("Browser timestamp too old");
}

// 2. Deposit timestamp (from Fuego daemon)
const depositTx = await fuegoRPC.getTransaction(depositTxHash);
const depositTimestamp = depositTx.timestamp;

// Check deposit is still locked (not past unlock time)
const lockPeriodSeconds = tier <= 1 ? 90 * 24 * 60 * 60 : 365 * 24 * 60 * 60;
const unlockTimestamp = depositTimestamp + lockPeriodSeconds;

if (Date.now() / 1000 > unlockTimestamp) {
  throw new Error("Deposit unlock period has passed");
}

// 3. Check if legacy deposit (before 2026-01-01)
const LEGACY_CUTOFF = 1735689600; // 2026-01-01 00:00:00 UTC
const isLegacy = depositTimestamp < LEGACY_CUTOFF;
```

---

## 🔍 **Fuego Daemon Query**

### **API Queries Trusted Fuego Daemon:**

```typescript
interface FuegoDepositQuery {
  txHash: string;
}

interface FuegoDepositData {
  txHash: string;
  sender: string;
  amount: number;           // XFG amount (atomic units: 7 decimals)
  lockPeriodMonths: number; // 3 or 12
  timestamp: number;        // Unix timestamp
  nullifier: string;        // Derived from deposit
  commitment: string;       // Hash of deposit data
  status: "locked" | "unlocked";
  networkId: string;        // Fuego mainnet or testnet
}

// API queries daemon
const depositData = await fuegoRPC.queryDeposit({
  txHash: depositTxHash
});

// Validate deposit exists and is locked
if (!depositData) {
  throw new Error("Deposit not found");
}

if (depositData.status !== "locked") {
  throw new Error("Deposit already unlocked");
}

// Validate amount and period match a valid tier
const tier = calculateTier(depositData.amount, depositData.lockPeriodMonths);
if (tier === null) {
  throw new Error("Invalid deposit amount/period combination");
}

// Check nullifier not already used
const isNullifierUsed = await coldVerifier.isNullifierUsed(depositData.nullifier);
if (isNullifierUsed) {
  throw new Error("Deposit already claimed");
}
```

### **Tier Calculation:**

```typescript
function calculateTier(xfgAmount: number, lockMonths: number): number | null {
  // XFG amount in atomic units (7 decimals)
  // 0.8 XFG = 8_000_000
  // 800 XFG = 8_000_000_000

  const amount = xfgAmount / 10_000_000; // Convert to readable XFG

  // Determine amount index
  let amountIndex: number;
  if (Math.abs(amount - 0.8) < 0.0001) {
    amountIndex = 0; // 0.8 XFG
  } else if (Math.abs(amount - 800) < 0.0001) {
    amountIndex = 1; // 800 XFG (maps to index 1 in simplified structure)
  } else {
    return null; // Invalid amount
  }

  // Determine term index
  let termIndex: number;
  if (lockMonths === 3) {
    termIndex = 0; // 3 months
  } else if (lockMonths === 12) {
    termIndex = 1; // 12 months
  } else {
    return null; // Invalid lock period
  }

  // Calculate tier: for simplified 4-tier structure
  // Tier 0: 0.8 XFG × 3mo
  // Tier 1: 0.8 XFG × 12mo
  // Tier 2: 800 XFG × 3mo
  // Tier 3: 800 XFG × 12mo
  const tier = (amountIndex * 2) + termIndex;

  return tier;
}
```

---

## 🛡️ **Security Checks**

### **API-Side Validation:**

```typescript
async function validateClaimRequest(request: ClaimRequest): Promise<void> {
  // 1. Domain binding signature
  const recoveredAddress = verifyDomainBindingSignature(
    request.domainBinding,
    request.signature
  );

  if (recoveredAddress !== request.domainBinding.recipient) {
    throw new Error("Signature verification failed");
  }

  // 2. Domain matches usexfg.org
  if (request.domainBinding.domain !== "usexfg.org") {
    throw new Error("Invalid domain");
  }

  // 3. Timestamp freshness (±5 minutes)
  const now = Date.now();
  const diff = Math.abs(now - request.domainBinding.timestamp);
  if (diff > 5 * 60 * 1000) {
    throw new Error("Request timestamp expired");
  }

  // 4. Nonce not reused
  if (await isNonceUsed(request.domainBinding.nonce)) {
    throw new Error("Nonce already used");
  }

  // 5. Recipient is valid Ethereum address
  if (!ethers.isAddress(request.domainBinding.recipient)) {
    throw new Error("Invalid recipient address");
  }

  // 6. Deposit tx hash is valid
  if (!isValidFuegoTxHash(request.domainBinding.depositTxHash)) {
    throw new Error("Invalid deposit transaction hash");
  }
}
```

### **Fuego Daemon Validation:**

```typescript
async function validateDepositData(
  depositData: FuegoDepositData,
  recipient: string
): Promise<void> {
  // 1. Deposit exists on Fuego blockchain
  if (!depositData) {
    throw new Error("Deposit not found on Fuego");
  }

  // 2. Deposit is still locked
  if (depositData.status !== "locked") {
    throw new Error("Deposit is not locked");
  }

  // 3. Lock period not expired
  const lockSeconds = depositData.lockPeriodMonths === 3
    ? 90 * 24 * 60 * 60
    : 365 * 24 * 60 * 60;

  const unlockTime = depositData.timestamp + lockSeconds;
  if (Date.now() / 1000 > unlockTime) {
    throw new Error("Deposit lock period expired");
  }

  // 4. Amount matches valid tier
  const tier = calculateTier(depositData.amount, depositData.lockPeriodMonths);
  if (tier === null) {
    throw new Error("Invalid deposit amount/period");
  }

  // 5. Nullifier not already claimed
  const isUsed = await coldVerifier.isNullifierUsed(depositData.nullifier);
  if (isUsed) {
    throw new Error("Deposit already claimed");
  }

  // 6. Network ID is valid (mainnet or testnet)
  const validNetworkIds = [
    "93385046440755750514194170694064996624",  // Mainnet
    "112015110234323138517908755257434054688"  // Testnet
  ];

  if (!validNetworkIds.includes(depositData.networkId)) {
    throw new Error("Invalid Fuego network ID");
  }
}
```

---

## 📦 **API Implementation**

### **Endpoint: POST /api/cold/claim**

```typescript
import express from 'express';
import { ethers } from 'ethers';

const router = express.Router();

router.post('/claim', async (req, res) => {
  try {
    const { domainBinding, signature } = req.body;

    // 1. Validate domain binding
    await validateClaimRequest({ domainBinding, signature });

    // 2. Query Fuego daemon
    const depositData = await fuegoRPC.queryDeposit({
      txHash: domainBinding.depositTxHash
    });

    // 3. Validate deposit data
    await validateDepositData(depositData, domainBinding.recipient);

    // 4. Calculate tier
    const tier = calculateTier(
      depositData.amount,
      depositData.lockPeriodMonths
    );

    // 5. Determine if legacy
    const LEGACY_CUTOFF = 1735689600;
    const isLegacy = depositData.timestamp < LEGACY_CUTOFF;

    // 6. Generate STARK proof (placeholder - actual implementation separate)
    const proof = await generateSTARKProof({
      depositTxHash: domainBinding.depositTxHash,
      recipient: domainBinding.recipient,
      tier: tier,
      depositTimestamp: depositData.timestamp,
      nullifier: depositData.nullifier,
      commitment: depositData.commitment,
      networkId: depositData.networkId
    });

    // 7. Validate proof locally
    const isValid = await verifySTARKProof(proof);
    if (!isValid) {
      throw new Error("STARK proof validation failed");
    }

    // 8. Estimate L1 gas fee
    const gasEstimate = await coldVerifier.getRecommendedGasFee(
      domainBinding.recipient,
      tier,
      isLegacy
    );

    // 9. Submit to COLDDepositProofVerifier on Arbitrum
    const tx = await coldVerifier.claimCD(
      domainBinding.recipient,
      tier,
      depositData.nullifier,
      depositData.commitment,
      depositData.networkId,
      depositData.timestamp,
      { value: gasEstimate }
    );

    // 10. Mark nonce as used
    await markNonceUsed(domainBinding.nonce);

    // 11. Return success response
    res.json({
      success: true,
      txHash: tx.hash,
      tier: tier,
      isLegacy: isLegacy,
      estimatedCDAmount: isLegacy
        ? getLegacyCDAmount(tier)
        : getStandardCDAmount(tier),
      arbiscanUrl: `https://sepolia.arbiscan.io/tx/${tx.hash}`,
      message: "Proof submitted successfully. CD tokens will be minted on L1 in ~10 minutes."
    });

  } catch (error) {
    console.error('Claim error:', error);
    res.status(400).json({
      success: false,
      error: error.message
    });
  }
});

export default router;
```

---

## 🌐 **Frontend Implementation**

### **Browser Client (React/TypeScript):**

```typescript
import { ethers } from 'ethers';
import { useState } from 'react';

interface ClaimCOLDProps {
  depositTxHash: string;
}

export function ClaimCOLD({ depositTxHash }: ClaimCOLDProps) {
  const [loading, setLoading] = useState(false);
  const [result, setResult] = useState<any>(null);

  async function handleClaim() {
    setLoading(true);

    try {
      // 1. Get wallet provider
      const provider = new ethers.BrowserProvider(window.ethereum);
      const signer = await provider.getSigner();
      const userAddress = await signer.getAddress();

      // 2. Generate nonce
      const nonce = ethers.hexlify(ethers.randomBytes(32));

      // 3. Create domain binding message
      const domainBinding = {
        domain: "usexfg.org",
        recipient: userAddress,
        depositTxHash: depositTxHash,
        timestamp: Date.now(),
        nonce: nonce
      };

      // 4. Sign domain binding (EIP-712)
      const signature = await signer.signTypedData(
        {
          name: "COLD Deposits",
          version: "1",
          chainId: 421614 // Arbitrum Sepolia
        },
        {
          DomainBinding: [
            { name: "domain", type: "string" },
            { name: "recipient", type: "address" },
            { name: "depositTxHash", type: "bytes32" },
            { name: "timestamp", type: "uint256" },
            { name: "nonce", type: "bytes32" }
          ]
        },
        domainBinding
      );

      // 5. Submit to API
      const response = await fetch('/api/cold/claim', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          domainBinding,
          signature
        })
      });

      const data = await response.json();

      if (!data.success) {
        throw new Error(data.error);
      }

      // 6. Show success
      setResult(data);
      console.log('Claim successful:', data);

    } catch (error) {
      console.error('Claim failed:', error);
      alert(`Claim failed: ${error.message}`);
    } finally {
      setLoading(false);
    }
  }

  return (
    <div>
      <button onClick={handleClaim} disabled={loading}>
        {loading ? 'Submitting...' : 'Claim CD Tokens'}
      </button>

      {result && (
        <div>
          <h3>✅ Claim Submitted!</h3>
          <p>Transaction: {result.txHash}</p>
          <p>Tier: {result.tier}</p>
          <p>Legacy Deposit: {result.isLegacy ? 'Yes' : 'No'}</p>
          <p>Estimated CD: {result.estimatedCDAmount} atomic units</p>
          <a href={result.arbiscanUrl} target="_blank">View on Arbiscan</a>
          <p>⏳ CD tokens will be minted on L1 in ~10 minutes</p>
        </div>
      )}
    </div>
  );
}
```

---

## 🔐 **Security Guarantees**

### ✅ **What This Prevents:**

1. **Phishing Attacks:** Domain binding ensures proof only valid for usexfg.org
2. **Replay Attacks:** Nonce prevents reusing same domain binding
3. **Timestamp Spoofing:** API validates browser time within ±5 minutes
4. **Fake Deposits:** API queries trusted Fuego daemon for on-chain data
5. **Double Claiming:** Nullifier prevents claiming same deposit twice
6. **Wrong Recipient:** Signature must match recipient address
7. **Stale Proofs:** Deposit lock status checked before submission

### ⚠️ **Trust Assumptions:**

1. **User trusts:** usexfg.org domain and API
2. **API trusts:** Fuego daemon node (blockchain data source)
3. **Contract trusts:** API verifier address (off-chain proof validation)
4. **Browser timestamp:** Assumed within ±5 min of server time

---

## 📝 **Next Steps**

1. ✅ Design complete - documented above
2. ⏳ Implement API server (Node.js/Express)
3. ⏳ Implement Fuego daemon RPC client
4. ⏳ Implement STARK proof generation (integrate Winterfell)
5. ⏳ Implement frontend UI (React)
6. ⏳ Deploy API to usexfg.org
7. ⏳ Test end-to-end flow on testnet

---

**Winter is coming. ❄️**
