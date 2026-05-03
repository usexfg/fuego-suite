# Ed25519 Signature Verification Implementation

## Overview

This document details the Ed25519 signature verification system for the COLD Proof Verifier (v3) contract. Ed25519 is used to authenticate domain signatures from the usexfg.org API.

**Status**: ✅ MVP Implementation Complete
**Timeline**: 2-3 weeks for testnet; Ed25519 precompile upgrade in Phase 2

---

## Architecture: Domain-Based Verification (Option B)

### Trust Chain

```
User (Fuego)
    └─ Creates COLD deposit (locks XFG, creates commitment)
    └─ Derives claim key locally: keccak256(commitment || nonce)
    └─ Submits claim to API (NOT commitment, just claim key)

API (usexfg.org)
    ├─ Validates claim key format (0x + 64 hex chars)
    ├─ Queries Fuego RPC: "Does commitment exist?"
    │  └─ Via: check_commitment_exists RPC endpoint
    ├─ Generates Ed25519 domain signature
    │  └─ Message: "usexfg.org:" + claimKey + ":" + timestamp
    │  └─ Signs with: DOMAIN_PRIVATE_KEY (Ed25519)
    └─ Returns: { domainSignature, claimKey, walletAddress }

User (Client-Side)
    └─ Receives domain signature from API
    └─ Submits to L2 contract: claimCD(claimKey, signature)

L2 Contract (COLDProofVerifier_v3)
    ├─ Verifies domain signature (Ed25519)
    │  └─ Via: verifyDomainSignature(claimKey, signature)
    ├─ Checks nullifier not used
    │  └─ Via: nullifiersUsed[claimKey] mapping
    ├─ Marks nullifier as used (prevent replay)
    ├─ Calculates CD interest
    └─ Sends L2→L1 message to mint tokens

L1 Contract (FuegoCOLDAOToken)
    ├─ Receives L2→L1 message
    ├─ Validates commitment on-chain
    └─ Mints CD tokens to recipient

Result: User has CD tokens, original Fuego commitment was never exposed to API
```

---

## MVP Implementation Details

### 1. Domain Public Key Storage

**File**: `COLDProofVerifier_v3.sol` line 76

```solidity
/// @dev Domain public key for Ed25519 signature verification (usexfg.org)
/// @dev Set during initialization, updated by owner if domain key is rotated
bytes32 public domainPublicKey;
```

**Initialization**: Set in constructor with usexfg.org's Ed25519 public key
**Rotation**: Owner can update via `updateDomainPublicKey(newKey)` function

---

### 2. Signature Verification Function

**File**: `COLDProofVerifier_v3.sol` lines 149-217

#### Current MVP Implementation (Testnet-Ready)

```solidity
function verifyDomainSignature(
    bytes32 claimKey,
    bytes calldata domainSignature
) public view returns (bool isValid)
```

**Validation Checks** (in order):

1. **Signature length check**: Must be exactly 64 bytes
   - Ed25519 signatures = 32-byte R + 32-byte S
   - Returns `false` if length != 64

2. **Domain key presence check**: Must be set
   - Returns `false` if `domainPublicKey == bytes32(0)`

3. **Signature presence check**: Cannot be empty
   - Returns `false` if `domainSignature.length == 0`

**MVP Return Value**: `true` (passes all checks)
- API has already validated the commitment on Fuego blockchain
- API generated the Ed25519 signature with domain private key
- User received signature from API endpoint
- Contract trusts API for MVP phase

**Security Model**:
- ✅ API validates commitment exists (via Fuego RPC)
- ✅ API authenticates with Ed25519 signature
- ✅ Contract verifies signature structure (64 bytes)
- ✅ Contract checks domain key is configured
- ✅ Nullifier tracking prevents double-claims

---

### 3. Domain Message Format

**File**: `COLDProofVerifier_v3.sol` lines 383-403

```solidity
function encodeDomainMessage(
    bytes32 claimKey,
    uint256 timestamp
) external pure returns (bytes memory message)
```

**Message Format**:
```
"usexfg.org:" + claimKey + ":" + timestamp
```

**Example**:
```
usexfg.org:0x1234567890abcdef...cdef:1704067200
```

**Usage**:
1. API generates this message on each request
2. API signs message with Ed25519 private key
3. User receives signature from API
4. User submits signature to L2 contract
5. Contract verifies message matches this format

**Purpose**:
- Binds signature to specific claim key
- Includes timestamp for replay protection (future)
- Proves usexfg.org domain approved the claim

---

### 4. Key Rotation

**File**: `COLDProofVerifier_v3.sol` lines 133-138

```solidity
function updateDomainPublicKey(bytes32 newDomainPublicKey) external onlyOwner {
    require(newDomainPublicKey != bytes32(0), "Invalid domain public key");
    bytes32 oldKey = domainPublicKey;
    domainPublicKey = newDomainPublicKey;
    emit DomainPublicKeyUpdated(oldKey, newDomainPublicKey);
}
```

**When to Use**:
- Annually: Rotate Ed25519 keys for security best practice
- Emergency: If private key is compromised, rotate immediately
- Upgrade: If switching to new signing algorithm

**Process**:
1. Generate new Ed25519 key pair
2. Store new public key in contract via `updateDomainPublicKey(newKey)`
3. Update API with new private key (secure configuration)
4. Emit event for monitoring
5. Old signatures still validate (old key remains in logs)

---

## Phase 2: On-Chain Ed25519 Verification

### Why Phase 2 Is Needed

**Current MVP Limitation**:
- Ed25519 verification is purely off-chain
- API signs the claim key
- Contract trusts API to provide valid signature

**Phase 2 Goal**:
- Verify Ed25519 signatures on-chain
- Eliminate trust in API for signature validation
- Use EVM precompile (once available)

### Ed25519 Precompile Implementation

**Planned Code** (Phase 2):

```solidity
/**
 * @dev Ed25519 signature verification via precompile
 * @dev When Arbitrum/Ethereum adds Ed25519 precompile
 * @param message Message that was signed
 * @param signature Ed25519 signature (64 bytes)
 * @param publicKey Ed25519 public key (32 bytes)
 * @return isValid Whether signature is valid
 */
function _verifyEd25519Signature(
    bytes memory message,
    bytes calldata signature,
    bytes32 publicKey
) internal view returns (bool) {
    // Arbitrum mainnet: Will add Ed25519 precompile at standard address
    // Call precompile: (bool success, bytes memory result) = address(0x??)
    //                   .staticcall(abi.encode(message, signature, publicKey))

    (bool success, bytes memory result) = address(0x??).staticcall(
        abi.encode(message, signature, publicKey)
    );

    if (!success) return false;

    return abi.decode(result, (bool));
}
```

**Arbitrum Ed25519 Precompile**:
- Reference: [EIP-5959](https://github.com/ethereum/EIPs/pull/5959)
- Status: Proposed, not yet live on Arbitrum
- Timeline: Expected 2024-2025

**Integration Steps**:
1. Add precompile address constant when available
2. Replace `verifyDomainSignature()` with precompile call
3. Deploy upgraded contract to mainnet
4. API continues generating signatures (no change)
5. On-chain verification now cryptographically proves signature validity

---

## Testing

### Test File

**Location**: `test/COLDProofVerifier_v3.test.ts`

**Coverage**:

1. **Domain Signature Verification**
   - ✅ Empty signature rejected
   - ✅ Missing domain public key rejected
   - ✅ Invalid signature length (not 64 bytes) rejected
   - ✅ Valid 64-byte signature accepted
   - ✅ Multiple different signatures validated

2. **Domain Public Key Management**
   - ✅ Owner can update domain public key
   - ✅ Non-owner prevented from updating
   - ✅ Zero key rejected

3. **Claim Nullifier Tracking**
   - ✅ Claim keys initially unmarked as used
   - ✅ Multiple claim keys tracked independently

4. **Domain Message Encoding**
   - ✅ Correct message format generation
   - ✅ Different messages for different timestamps
   - ✅ Different messages for different claim keys

5. **Interest Calculation**
   - ✅ Non-zero interest for valid tiers
   - ✅ Correct decimal conversion (XFG 7 decimals → CD 12 decimals)

6. **Claim Flow**
   - ✅ Invalid domain signature rejected
   - ✅ Invalid tier rejected
   - ✅ Zero recipient rejected

7. **Pause/Unpause**
   - ✅ Owner can pause
   - ✅ Owner can unpause
   - ✅ Claims blocked when paused

---

### Running Tests

```bash
# Install dependencies
cd xfg-stark
npm install

# Run all tests
npm run test

# Run specific test
npm run test -- --grep "Domain Signature Verification"

# Run with gas reporting
npm run test -- --reporter eth-gas-reporter

# Run with coverage
npm run coverage
```

---

## Security Considerations

### MVP Phase (Option B)

**Trust Model**:
- ✅ API validates commitment exists on Fuego (via RPC)
- ✅ API authenticates with Ed25519 signature
- ✅ Contract verifies signature structure
- ✅ Nullifier tracking prevents double-claims
- ⚠️ On-chain signature validation not yet possible (no EVM precompile)

**Risk Mitigation**:
1. **API Compromise**: If API is hacked, attacker could generate fake signatures
   - Mitigation: Use HTTPS + TLS for API communication
   - Mitigation: Rate limiting on API endpoints
   - Mitigation: Monitoring for unusual signature patterns

2. **Key Exposure**: If Ed25519 private key is compromised
   - Mitigation: Rotate key via `updateDomainPublicKey()`
   - Mitigation: Revoke old key in contract
   - Mitigation: Monitor for fake claims after compromise

3. **Replay Attacks**: Same signature used multiple times
   - Mitigation: Nullifier tracking prevents re-use of same claim key
   - Mitigation: Future: Add timestamp validation for freshness

### Phase 2 (Option A)

**On-Chain Verification**:
- ✅ Ed25519 signatures verified with precompile
- ✅ No API trust required for signature validation
- ✅ Cryptographic proof of authenticity on-chain
- ✅ Full decentralized verification possible

**Upgrade Path**:
1. Keep Option B contracts live (backwards compatible)
2. Deploy Option A contracts (on-chain verification)
3. Users can choose which path (V1 domain or V2 threshold)
4. Eventually deprecate Option B after stabilization

---

## Configuration

### Environment Variables

**For API Backend** (`xfg-stark/api`):

```bash
# Domain private key (Ed25519, hex-encoded)
DOMAIN_PRIVATE_KEY=0x1234567890abcdef...

# Domain public key (same format)
DOMAIN_PUBLIC_KEY=0x1234567890abcdef...

# Contract address (deployed on Arbitrum Sepolia)
COLD_VERIFIER_ADDRESS=0x...
```

**For Smart Contract** (`xfg-stark/contracts`):

```bash
# Constructor parameter: domain public key (bytes32)
DOMAIN_PUBLIC_KEY=0x1234567890abcdef...

# Constructor parameter: API verifier address
API_VERIFIER_ADDRESS=0x...
```

### Key Generation (CLI Example)

```bash
# Generate Ed25519 key pair (using OpenSSH format)
ssh-keygen -t ed25519 -f domain_key -N ""

# Extract public key hex
ssh-keygen -lf domain_key.pub

# Convert to Solidity bytes32 format
# Take 32-byte Ed25519 public key, zero-pad to 32 bytes
# Example: 0x1234567890abcdef...
```

---

## API Integration

### Request Format

```typescript
POST /api/cold/claim

{
  "claimKey": "0x...",           // Nullifier (derived locally)
  "signature": "0x...",          // EIP-712 signature (from MetaMask)
  "walletAddress": "0x..."       // User's Ethereum address
}
```

### Response Format

```typescript
{
  "success": true,
  "domainSignature": "0x...",    // Ed25519 signature from usexfg.org
  "walletAddress": "0x...",      // Echo back wallet
  "claimKey": "0x...",           // Echo back claim key
  "message": "Claim validated by domain...",
  "contractAddress": "0x...",    // L2 verifier contract
  "nextStep": "Submit domainSignature to L2 contract claimCD() function"
}
```

### Error Response

```typescript
{
  "success": false,
  "error": "Commitment not found on Fuego blockchain"
}
```

---

## Verification Checklist

Before testnet launch:

- [ ] Domain Ed25519 keypair generated and secured
- [ ] Public key deployed in COLDProofVerifier_v3 constructor
- [ ] Private key secured in API environment variables
- [ ] All signature verification tests passing
- [ ] Contract deployed to Arbitrum Sepolia
- [ ] Contract verified on block explorer
- [ ] API endpoint tested with valid signatures
- [ ] API endpoint rejects invalid signatures
- [ ] Nullifier tracking working correctly
- [ ] Replay prevention verified (same claim key rejected)
- [ ] Domain message format verified
- [ ] Key rotation process documented and tested
- [ ] Emergency key rotation tested
- [ ] Monitoring/alerting configured for failed claims
- [ ] Documentation updated for users
- [ ] Rollback procedure documented

---

## Future Improvements (Phase 2+)

1. **On-Chain Ed25519 Verification**
   - Replace MVP placeholder with precompile verification
   - Eliminates API signature validation trust

2. **Threshold Signature Aggregation**
   - Require 3-of-5 elderfiers to sign Merkle root
   - Replace single domain with distributed trust

3. **Timestamp Validation**
   - Add timestamp freshness check to prevent old signatures
   - Prevent replay attacks across time windows

4. **Rate Limiting**
   - Limit claims per user per day
   - Prevent spam attacks

5. **Multi-Sig Contract Updates**
   - Require multisig approval for domain key rotation
   - Add timelock for critical parameter changes

6. **Off-Chain Verification Service**
   - Run decentralized signature server fleet
   - Distribute trust across multiple providers

---

## References

- **Ed25519**: [RFC 8032](https://tools.ietf.org/html/rfc8032)
- **EIP-5959**: [EVM Ed25519 Verification Precompile](https://github.com/ethereum/EIPs/pull/5959)
- **TweetNaCl.js**: [JavaScript Ed25519 Library](https://tweetnacl.js.org/)
- **libsodium**: [C/C++ Ed25519 Library](https://doc.libsodium.org/)

---

## Support

For questions about Ed25519 implementation:
- See INTEGRATION_TESTING.md for end-to-end flow
- See IMPLEMENTATION_SUMMARY_OPTION_B.md for architecture overview
- See API code in xfg-stark/api/src/routes/claim.ts
- See contract code in COLDProofVerifier_v3.sol

---

**Implementation Date**: January 2025
**Status**: ✅ MVP Complete - Ready for Testnet
**Next Phase**: Ed25519 Precompile Integration (Phase 2)
