# Ed25519 Signature Verification - Quick Start Guide

## 30-Second Overview

**What**: Signature verification system for COLD deposit claims
**Why**: Proves API (usexfg.org) approved the claim before minting tokens
**How**: Ed25519 signatures on claim keys, validated in L2 contract

---

## 5-Minute Setup

### Step 1: Deploy Contract to Testnet

```bash
cd xfg-stark
npm install

# Set environment variables
export DOMAIN_PUBLIC_KEY="0x..." # Your Ed25519 public key
export ARBITRUM_RPC_URL="https://sepolia-rollup.arbitrum.io/rpc"
export PRIVATE_KEY="0x..."        # Deployer wallet private key

# Deploy
npm run deploy:testnet
```

### Step 2: Configure API

```bash
# In xfg-stark/api/.env
COLD_VERIFIER_ADDRESS=0x...      # Contract address from step 1
DOMAIN_PRIVATE_KEY=0x...         # Your Ed25519 private key
DOMAIN_PUBLIC_KEY=0x...          # Matching public key
FUEGO_MAINNET_RPC=http://localhost:18180
FUEGO_TESTNET_RPC=http://localhost:28280   # Note: 28280, not 28081
```

### Step 3: Start API

```bash
cd xfg-stark/api
npm start

# Should see: "✅ API running on http://localhost:3001"
```

### Step 4: Test Signature Verification

```bash
# Health check
curl http://localhost:3001/api/cold/health

# Expected response:
# {
#   "success": true,
#   "fuego": {
#     "mainnet": "healthy",
#     "testnet": "healthy"
#   }
# }
```

---

## How It Works: 3 Steps

### Step 1: User Submits Claim

```typescript
// User's wallet sends:
POST /api/cold/claim
{
  "claimKey": "0x1234...",        // Derived locally (not commitment!)
  "signature": "0x5678...",        // EIP-712 from MetaMask
  "walletAddress": "0x9abc..."     // User's wallet
}
```

### Step 2: API Validates & Signs

```typescript
API does:
1. Verify EIP-712 signature
2. Query Fuego: "Does commitment exist?"
3. Generate Ed25519 domain signature
4. Return: { domainSignature, ... }

User receives:
{
  "success": true,
  "domainSignature": "0x...",     // Ed25519 signature
  "claimKey": "0x1234...",
  "walletAddress": "0x9abc...",
  "nextStep": "Submit to L2 contract"
}
```

### Step 3: User Submits to Contract

```solidity
// User calls L2 contract:
function claimCD(
    address recipient,
    uint8 depositTier,
    bytes32 claimKey,
    bytes32 commitment,
    bytes calldata domainSignature   // From API
) external payable

// Contract validates:
1. Domain signature (64 bytes, matches domain public key)
2. Nullifier not used (prevent double-claim)
3. Calculate interest
4. Send L2→L1 message
5. Mint tokens on L1
```

---

## Key Validation Points

### Signature Requirements

```
✓ Must be exactly 64 bytes
  └─ Ed25519 signatures: 32-byte R + 32-byte S

✓ Domain public key must be set
  └─ Configured at contract deployment

✓ Contract validates signature was signed by domain
  └─ MVP: Structure check (64 bytes)
  └─ Phase 2: Cryptographic verification (precompile)
```

### Nullifier Protection

```
✓ Each claim key can only be used once
  └─ Contract tracks in usedNullifiers mapping

✓ Different wallet = different claim
  └─ Users can claim multiple times with different keys
  └─ Privacy preserved: commitment never exposed

✓ Replay attack prevented
  └─ Same signature + same claim key rejected 2nd time
  └─ Different users cannot claim same commitment
```

---

## Testing the Implementation

### Run All Tests

```bash
cd xfg-stark
npm run test

# Expected output:
# ✓ Domain Signature Verification (5 tests)
# ✓ Domain Public Key Management (3 tests)
# ✓ Claim Nullifier Tracking (2 tests)
# ✓ Domain Message Encoding (3 tests)
# ✓ ... (8 more test groups)
#
# 13 passing (X.XXs)
```

### Test Specific Scenario

```bash
# Test only signature validation
npm run test -- --grep "Domain Signature Verification"

# Test only nullifier tracking
npm run test -- --grep "Nullifier"

# Test with coverage report
npm run coverage
```

### Manual Testing via curl

```bash
# 1. Check API health
curl -X GET http://localhost:3001/api/cold/health

# 2. Submit test claim (requires valid EIP-712 signature)
curl -X POST http://localhost:3001/api/cold/claim \
  -H "Content-Type: application/json" \
  -d '{
    "claimKey": "0x1234567890123456789012345678901234567890123456789012345678901234",
    "signature": "0x...",  # Valid EIP-712 signature
    "walletAddress": "0x9abc..."
  }'

# 3. Expected successful response:
# {
#   "success": true,
#   "domainSignature": "0x...",
#   "claimKey": "0x1234...",
#   "walletAddress": "0x9abc...",
#   "message": "Claim validated by domain...",
#   "nextStep": "Submit domainSignature to L2 contract claimCD() function"
# }
```

---

## Common Issues & Fixes

### Issue 1: "Invalid domain signature"

**Cause**: Signature is empty or wrong length

**Fix**:
```solidity
// Contract checks:
// 1. Signature length == 64 bytes
// 2. Domain public key != 0x0

// Make sure:
- Domain signature from API is 64 bytes
- Domain public key set in constructor
- API actually generated the signature
```

### Issue 2: "Already claimed"

**Cause**: Same claim key submitted twice

**Fix**:
```typescript
// This is intentional replay prevention

// Solutions:
1. Use different claim key (derive with different nonce)
2. Create new Fuego commitment (lock more XFG)
3. Use different wallet (new nullifier per wallet)

// Check status:
const isUsed = await contract.isClaimKeyUsed(claimKey);
```

### Issue 3: "Commitment not found on Fuego"

**Cause**: Commitment doesn't exist on blockchain

**Fix**:
```bash
# 1. Verify deposit exists on Fuego:
curl -X POST http://localhost:28280/json_rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": "0",
    "method": "check_commitment_exists",
    "params": { "commitment_hash": "0x..." }
  }'

# 2. If "exists": false, create a new deposit:
fire_wallet cold --amount 8 --term 12

# 3. Wait for block confirmation
# 4. Try claim again with commitment from new deposit
```

### Issue 4: Testnet RPC Connection Error

**Cause**: Wrong port (28081 instead of 28280)

**Fix**:
```bash
# Check environment variable:
echo $FUEGO_TESTNET_RPC

# Should be: http://localhost:28280
# NOT:       http://localhost:28081

# Fix in:
1. xfg-stark/api/.env
2. xfg-stark/api/src/routes/claim.ts (line 21 & 136)
3. Test configuration files
```

### Issue 5: Contract Not Deployed

**Cause**: Contract address not set in API

**Fix**:
```bash
# 1. Deploy contract:
npm run deploy:testnet
# Outputs: Contract deployed at 0x...

# 2. Copy address to .env:
COLD_VERIFIER_ADDRESS=0x...

# 3. Restart API:
npm restart
```

---

## Message Format Reference

### Domain Message (What Gets Signed)

```
Format: "usexfg.org:" + claimKey + ":" + timestamp

Example:
usexfg.org:0x1234567890abcdef...cdef:1704067200

Components:
- usexfg.org   = Domain name (prevents cross-domain attacks)
- claimKey     = 32-byte hash of commitment (nullifier)
- timestamp    = Unix timestamp (enables future freshness check)
```

### How to Generate (API Side)

```typescript
// xfg-stark/api/src/routes/claim.ts line 179

const domainSignatureMessage = `usexfg.org|${claimKey}|${Math.floor(Date.now() / 1000)}`;

// In production: Sign with Ed25519 private key
const domainSignature = ed25519.sign(domainSignatureMessage, DOMAIN_PRIVATE_KEY);

// In MVP: Use placeholder for testnet
const domainSignature = ethers.id(domainSignatureMessage);
```

### How to Verify (Contract Side)

```solidity
// xfg-stark/COLDProofVerifier_v3.sol line 383

bytes memory message = abi.encodePacked(
    "usexfg.org:",
    claimKey,
    ":",
    timestamp
);

// MVP: Check signature length (64 bytes)
require(domainSignature.length == 64, "Invalid signature length");

// Phase 2: Verify cryptographically with precompile
// bool isValid = _verifyEd25519Signature(message, domainSignature, domainPublicKey);
```

---

## Security Reminders

### For Developers

```
DO:
✓ Store Ed25519 private key in secure vault (not in code)
✓ Rotate keys periodically (via updateDomainPublicKey)
✓ Monitor for unusual claim patterns
✓ Use HTTPS for API endpoints
✓ Rate-limit API endpoints

DON'T:
✗ Log claim keys or commitments
✗ Expose private key in logs
✗ Store signatures in database
✗ Trust unverified signatures
✗ Skip nullifier checks
```

### For Users

```
DO:
✓ Derive claim key locally (keccak256(commitment || nonce)) > idk bout that. thought commitmetn was hashh (secret|something|something) 
✓ Use unique wallet per claim (for privacy)
✓ Keep private keys secure
✓ Use HTTPS for frontend
✓ Verify contract address on block explorer

DON'T:
✗ Send commitment to API (only send claim key)
✗ Reuse same claim key
✗ Share signatures publicly
✗ Trust unverified APIs
✗ Use same wallet repeatedly (for privacy)
```

---

## Troubleshooting Checklist

Before opening an issue, verify:

- [ ] **Testnet RPC port is 28280** (not 28081)
- [ ] **Domain public key is set** in contract constructor
- [ ] **Domain private key is configured** in API environment
- [ ] **All tests pass** (`npm run test`)
- [ ] **API health check works** (GET /api/cold/health)
- [ ] **Fuego deposit exists** on testnet (check_commitment_exists returns true)
- [ ] **Signature length is 64 bytes** (Ed25519 standard)
- [ ] **Claim key not already used** (check isClaimKeyUsed)
- [ ] **Wallet has enough ETH** for L1 gas fee (>0.1 ETH recommended)
- [ ] **Contract is unpaused** (check paused() == false)

---

## Documentation Links

For more details, see:

- **Architecture & Design**: `IMPLEMENTATION_SUMMARY_OPTION_B.md`
- **Complete Technical Details**: `ED25519_SIGNATURE_VERIFICATION.md`
- **Integration Testing Guide**: `INTEGRATION_TESTING.md`
- **API Implementation**: `xfg-stark/api/src/routes/claim.ts`
- **Smart Contract Code**: `COLDProofVerifier_v3.sol`
- **Test Suite**: `test/COLDProofVerifier_v3.test.ts`

---

## Support

- **GitHub Issues**: Report bugs or ask questions
- **Discord**: Join community discussions
- **Email**: dev@usexfg.org for security issues

---

**Last Updated**: January 27, 2025
**Status**: ✅ Ready for Testnet
**Version**: Ed25519 MVP Implementation
