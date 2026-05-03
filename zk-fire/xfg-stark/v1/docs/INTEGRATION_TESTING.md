# Integration Testing Guide - Domain-Based Option B MVP

## Overview
This document provides a comprehensive testing plan for the end-to-end flow of the COLD deposit claim system using domain-based verification (Option B MVP).

## Pre-Testing Setup

### Prerequisites
- Fuego mainnet/testnet node running with updated RPC endpoint
- Arbitrum Sepolia testnet access
- API backend running at `http://localhost:3001`
- Frontend running at `http://localhost:3000`
- MetaMask or Web3 wallet configured for Arbitrum Sepolia
- Test ETH on Arbitrum Sepolia for gas fees
- Fuego XFG for creating test deposits

### Environment Configuration

**Backend (.env)**
```
FUEGO_MAINNET_RPC=http://localhost:18180
FUEGO_TESTNET_RPC=http://localhost:28081
ARB_SEPOLIA_RPC=https://sepolia-rollup.arbitrum.io/rpc
DOMAIN_PRIVATE_KEY=0x...
DOMAIN_PUBLIC_KEY=0x...
COLD_VERIFIER_ADDRESS=0x...
```

**Frontend (vite.config.ts proxy)**
```
proxy: {
  '/api': {
    target: 'http://localhost:3001',
    changeOrigin: true
  }
}
```

## Test Scenarios

### Test 1: RPC Endpoint Verification
**Goal:** Verify that the new `check_commitment_exists` RPC endpoint works correctly.

**Steps:**
1. Start Fuego node with fresh blockchain
2. Create a COLD deposit (0xCD tag) on mainnet/testnet
3. Call RPC endpoint: `curl -X POST http://localhost:18180/json_rpc -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":"0","method":"check_commitment_exists","params":{"commitment_hash":"0x..."}}'`

**Expected Results:**
- ✅ Endpoint responds with `{exists: true, block_height: N, status: "OK"}`
- ✅ Block height matches the block where deposit was made
- ✅ Endpoint is rate-limited and protected

**Pass/Fail:** ___

---

### Test 2: API Signature Verification
**Goal:** Verify that API correctly validates EIP-712 signatures.

**Steps:**
1. Generate a test EIP-712 signature using ethers.js
2. Submit to API: `POST /api/cold/claim`
3. Include valid signature, wallet address, and claim key

**Expected Results:**
- ✅ API validates signature correctly
- ✅ Signature verification fails with invalid signature
- ✅ Wallet address mismatch detected
- ✅ Signature is not logged or persisted

**Pass/Fail:** ___

---

### Test 3: Commitment Validation Flow
**Goal:** Verify API calls Fuego RPC and validates commitment exists.

**Steps:**
1. Create COLD deposit on testnet (lock 8 XFG for 12 months)
2. Wait for commitment to appear in CommitmentIndex
3. Derive claim key: `keccak256(commitment || nonce)`
4. Submit to API with valid signature

**Expected Results:**
- ✅ API queries `check_commitment_exists` on Fuego RPC
- ✅ API returns success if commitment found
- ✅ API returns error if commitment not found
- ✅ API handles mainnet/testnet fallback correctly
- ✅ No commitment hash is logged

**Pass/Fail:** ___

---

### Test 4: Domain Signature Generation
**Goal:** Verify API generates valid Ed25519 domain signatures.

**Steps:**
1. Submit valid claim to API
2. Capture domain signature response
3. Verify signature format (hex string, expected length)

**Expected Results:**
- ✅ Domain signature is returned as hex string
- ✅ Signature length is correct (128 hex chars = 64 bytes)
- ✅ Each request generates different signature (includes timestamp)
- ✅ Signature verification can be done on-chain (placeholder)

**Pass/Fail:** ___

---

### Test 5: L2 Contract Nullifier Check
**Goal:** Verify L2 contract correctly tracks used claim keys.

**Steps:**
1. Deploy COLDProofVerifier to Arbitrum Sepolia
2. Call `isClaimKeyUsed(claimKey)` before submission
3. Submit claim with domain signature
4. Call `isClaimKeyUsed(claimKey)` after submission

**Expected Results:**
- ✅ Returns `false` before claim
- ✅ Returns `true` after successful claim
- ✅ Prevents double-spending with same claim key
- ✅ Different claim keys can be submitted by same user

**Pass/Fail:** ___

---

### Test 6: L2 Contract Domain Signature Verification
**Goal:** Verify L2 contract validates domain signatures before minting.

**Steps:**
1. Call `claimCD()` with invalid domain signature
2. Call `claimCD()` with valid domain signature from API
3. Call `claimCD()` with valid signature but wrong claim key

**Expected Results:**
- ✅ Rejects invalid domain signature
- ✅ Accepts valid domain signature
- ✅ Mints tokens only on valid submission
- ✅ Gas estimation works correctly

**Pass/Fail:** ___

---

### Test 7: Frontend Wallet Connection
**Goal:** Verify frontend connects to MetaMask and switches networks.

**Steps:**
1. Open frontend on localhost:3000
2. Click "Connect Wallet" button
3. Approve MetaMask connection
4. Verify wallet address is displayed
5. Test network detection

**Expected Results:**
- ✅ MetaMask modal appears on click
- ✅ Connected wallet address is displayed
- ✅ Disconnect button appears
- ✅ Appropriate network warning if not on Arbitrum Sepolia
- ✅ No sensitive data is logged

**Pass/Fail:** ___

---

### Test 8: Frontend Claim Form Submission
**Goal:** Verify frontend form collects input and submits to API.

**Steps:**
1. Connect wallet
2. Enter claim key in form
3. Click "Sign & Validate Claim"
4. Approve signature in MetaMask
5. Monitor API call and response

**Expected Results:**
- ✅ Form validates claim key format (0x + 64 hex chars)
- ✅ MetaMask signature request appears
- ✅ API call is made with correct parameters
- ✅ Response is displayed (domain signature or error)
- ✅ No private data visible in network logs
- ✅ Error messages are helpful

**Pass/Fail:** ___

---

### Test 9: Privacy Verification - No Data Logging
**Goal:** Verify no sensitive data is logged on API server.

**Steps:**
1. Configure API with logging (stdout + file)
2. Submit multiple claims with different wallets
3. Analyze server logs for sensitive data

**Expected Results:**
- ✅ No claim keys in logs
- ✅ No wallet addresses in logs
- ✅ No commitment hashes in logs
- ✅ Only generic messages and status codes logged
- ✅ No correlation between submissions possible from logs
- ✅ Server is stateless (no persistent storage)

**Pass/Fail:** ___

---

### Test 10: Cross-Chain Message Flow
**Goal:** Verify L2→L1 message passing for token minting.

**Steps:**
1. Submit claim on L2 with domain signature
2. Wait for L2 block confirmation
3. Monitor for L2→L1 message in Arbitrum bridge
4. Wait for message to be delivered to L1
5. Verify CD tokens appear in wallet

**Expected Results:**
- ✅ L2 transaction is confirmed
- ✅ Message is enqueued for L1 delivery
- ✅ L1 contract receives message within timeout
- ✅ CD tokens are minted to recipient wallet
- ✅ Gas costs are within estimates
- ✅ No replay attacks possible

**Pass/Fail:** ___

---

### Test 11: Epoch-Based Fee Distribution
**Goal:** Verify elder node fee tracking and distribution at epoch boundaries.

**Steps:**
1. Create multiple COLD deposits across several blocks
2. Monitor CommitmentIndex fee accumulation
3. Wait for epoch boundary (1,234 blocks)
4. Trigger epoch finalization
5. Verify fee distribution to active elderfiers

**Expected Results:**
- ✅ Fees accumulate during epoch
- ✅ Epoch duration is ~7 days (1,234 blocks)
- ✅ Active elderfiers are correct (3-of-5 rotation)
- ✅ Fees split equally among active elderfiers
- ✅ Distribution transaction executed at epoch end
- ✅ Historical record is maintained

**Pass/Fail:** ___

---

### Test 12: Error Handling & Edge Cases
**Goal:** Verify graceful error handling in all components.

**Test Cases:**
1. Invalid claim key format
2. Non-existent commitment
3. Already-claimed nullifier
4. Network timeout on Fuego RPC
5. Contract gas estimation failure
6. Invalid wallet address
7. Expired signature
8. Concurrent submissions from same wallet

**Expected Results:**
- ✅ All errors return appropriate HTTP status codes
- ✅ Error messages are helpful (not exposing internals)
- ✅ No crashes or undefined behavior
- ✅ Retry logic works for transient errors
- ✅ User is informed of next steps

**Pass/Fail:** ___

---

## Load Testing

### Test 13: Concurrent Submissions
**Goal:** Verify API handles multiple concurrent claim submissions.

**Setup:**
```bash
# Use loadtest or k6 to simulate concurrent requests
npm install -g loadtest
loadtest -n 100 -c 10 -k http://localhost:3001/api/cold/claim
```

**Expected Results:**
- ✅ All requests complete without errors
- ✅ No race conditions in nullifier tracking
- ✅ Response times remain reasonable (<1s per request)
- ✅ Memory usage is stable (stateless design)

**Pass/Fail:** ___

---

### Test 14: RPC Rate Limiting
**Goal:** Verify RPC endpoints are protected from abuse.

**Steps:**
1. Send 1000 requests to check_commitment_exists endpoint
2. Monitor for rate limiting response
3. Verify response codes

**Expected Results:**
- ✅ Initial requests succeed
- ✅ Requests are rate-limited after threshold
- ✅ Rate limit headers are present
- ✅ Backoff/retry logic works

**Pass/Fail:** ___

---

## Security Testing

### Test 15: Signature Spoofing Prevention
**Goal:** Verify signatures cannot be replayed or spoofed.

**Steps:**
1. Capture valid signature from Test 8
2. Attempt to reuse signature with different wallet
3. Attempt to reuse signature with different claim key
4. Attempt to use timestamp from past

**Expected Results:**
- ✅ Signature fails with different wallet
- ✅ Signature fails with different claim key
- ✅ Expired signatures are rejected
- ✅ Timestamp validation prevents replay

**Pass/Fail:** ___

---

### Test 16: Nullifier Uniqueness
**Goal:** Verify each deposit can only be claimed once.

**Steps:**
1. Create COLD deposit
2. Claim with first wallet address
3. Attempt claim again with same nullifier
4. Attempt claim with different wallet

**Expected Results:**
- ✅ First claim succeeds
- ✅ Second claim fails (nullifier used)
- ✅ Different wallet cannot claim same deposit
- ✅ Nullifier is tracked on L1 contract (transparent)

**Pass/Fail:** ___

---

### Test 17: Domain Signature Validation
**Goal:** Verify API domain signature is required and validated.

**Steps:**
1. Attempt to call L2 contract without domain signature
2. Attempt with fake domain signature
3. Attempt with valid domain signature

**Expected Results:**
- ✅ Reverts without domain signature
- ✅ Reverts with invalid signature
- ✅ Succeeds with valid signature from API
- ✅ Signature verification is gas-efficient

**Pass/Fail:** ___

---

## Performance Benchmarks

| Metric | Target | Actual |
|--------|--------|--------|
| API claim validation | <500ms | ___ |
| Fuego RPC check | <100ms | ___ |
| Domain signature generation | <50ms | ___ |
| L2 contract gas (claimCD) | <100k gas | ___ |
| L2→L1 message delivery | <1 hour | ___ |
| Frontend page load | <2s | ___ |
| Signature generation (MetaMask) | <10s | ___ |

---

## Checklist - End-to-End Flow

- [ ] User creates COLD deposit on Fuego (0xCD tag)
- [ ] Deposit appears in CommitmentIndex within 1 block
- [ ] User derives claim key locally (not sent anywhere initially)
- [ ] User connects MetaMask to frontend
- [ ] User enters claim key in form
- [ ] Frontend requests signature (EIP-712)
- [ ] MetaMask shows domain-bound message
- [ ] User approves signature
- [ ] Frontend calls `/api/cold/claim`
- [ ] API validates signature
- [ ] API calls Fuego RPC: `check_commitment_exists`
- [ ] Commitment is found on blockchain
- [ ] API generates domain signature
- [ ] API returns domain signature (zero logging)
- [ ] Frontend displays domain signature
- [ ] User copies domain signature
- [ ] User submits to L2 contract `claimCD()`
- [ ] L2 contract validates domain signature
- [ ] L2 contract checks nullifier not used
- [ ] L2 contract marks nullifier as used
- [ ] L2 contract calls `ARB_SYS.sendTxToL1()`
- [ ] Transaction confirmed on L2
- [ ] Message relayed to L1
- [ ] L1 contract receives message
- [ ] CD tokens minted to recipient wallet
- [ ] User sees tokens in wallet
- [ ] No private data logged anywhere

---

## Sign-Off

**Tested By:** ____________________
**Date:** ____________________
**All Tests Passed:** ☐ Yes ☐ No

**Critical Issues Found:**
- [ ] None
- [ ] Minor (cosmetic)
- [ ] Major (needs fix before launch)
- [ ] Critical (blocks launch)

**Notes:**
_____________________________________________________________________________

---

## Deployment Checklist

Before launching to mainnet, verify:

- [ ] All integration tests pass
- [ ] Security audit complete
- [ ] Performance benchmarks met
- [ ] Error handling tested
- [ ] Documentation updated
- [ ] API infrastructure ready
- [ ] Domain keys configured securely
- [ ] Contract addresses verified
- [ ] RPC endpoints are reliable
- [ ] Monitoring/alerting configured
- [ ] Runbook created for operations
- [ ] User documentation published
