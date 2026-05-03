# COLD Deposits Testnet Deployment Guide

**Branch:** `cold-starks`
**Networks:** Fuego Testnet → Arbitrum Sepolia → Ethereum Sepolia

---

## 📋 **Overview**

**COLD Deposits** are time-locked XFG deposits:
- User deposits XFG on Fuego (locked, not burned)
- Chooses deposit amount (0.8, 8, 80, or 800 XFG)
- Chooses lock period (3 or 12 months)
- STARK proof generated locally
- API (usexfg.org) validates proof
- CD tokens minted on Ethereum L1 via Arbitrum L2

**8 Amount×Time Tiers (4 amounts × 2 terms):**
- Tier 0: 0.8 XFG × 3mo @ 8% → 640,000 atomic units
- Tier 1: 0.8 XFG × 12mo @ 27% → 2,160,000 atomic units
- Tier 2: 8 XFG × 3mo @ 18% → 14,400,000 atomic units
- Tier 3: 8 XFG × 12mo @ 33% → 26,400,000 atomic units
- Tier 4: 80 XFG × 3mo @ 27% → 216,000,000 atomic units
- Tier 5: 80 XFG × 12mo @ 42% → 336,000,000 atomic units
- Tier 6: 800 XFG × 3mo @ 33% → 2,640,000,000 atomic units
- Tier 7: 800 XFG × 12mo @ 69% → 5,520,000,000 atomic units

**Legacy (Pre-2026) Bonus:**
- Tier 6-7: 800 XFG before 2026-01-01 @ 80% → 6,400,000,000 atomic units

*Note: Longer lock + larger amount = more CD interest. Unlock handled on Fuego side.*

---

## 🚀 **Deployment Steps**

### **1. Deploy FuegoCOLDAOToken on Ethereum Sepolia**

```bash
# Deploy CD token (ERC-1155)
forge create FuegoCOLDAOToken \
  --constructor-args \
    <initial_minter_placeholder> \
    <coldao_governor_placeholder> \
    <your_address> \
  --private-key $PRIVATE_KEY \
  --rpc-url $SEPOLIA_RPC \
  --verify
```

**Record address:** `CD_TOKEN = __________________`

---

### **2. Deploy COLDAOGovernor on Ethereum Sepolia**

```bash
# Deploy DAO governor
forge create COLDAOGovernor \
  --constructor-args \
    $CD_TOKEN \
    800 \
    <your_address> \
  --private-key $PRIVATE_KEY \
  --rpc-url $SEPOLIA_RPC \
  --verify
```

**Record address:** `GOVERNOR = __________________`

---

### **3. Deploy COLDDepositProofVerifier on Arbitrum Sepolia**

```bash
# Deploy L2 verifier
forge create COLDDepositProofVerifier \
  --constructor-args \
    $CD_TOKEN \
    <api_verifier_address> \
    <your_address> \
  --private-key $PRIVATE_KEY \
  --rpc-url $ARB_SEPOLIA_RPC \
  --verify
```

**Record address:** `COLD_VERIFIER = __________________`

---

### **4. Configure Contracts**

```bash
# On Ethereum Sepolia: Authorize COLD verifier to mint
cast send $CD_TOKEN \
  "addAuthorizedMinter(address)" \
  $COLD_VERIFIER \
  --private-key $PRIVATE_KEY \
  --rpc-url $SEPOLIA_RPC

# On Arbitrum Sepolia: Set API verifier
cast send $COLD_VERIFIER \
  "updateAPIVerifier(address)" \
  <usexfg_backend_address> \
  --private-key $PRIVATE_KEY \
  --rpc-url $ARB_SEPOLIA_RPC
```

---

## ✅ **Verification Checklist**

- [ ] FuegoCOLDAOToken deployed on Sepolia
- [ ] COLDAOGovernor deployed on Sepolia
- [ ] COLDDepositProofVerifier deployed on Arbitrum Sepolia
- [ ] CD token has authorized minter (COLD verifier)
- [ ] COLD verifier has API verifier set
- [ ] All contracts verified on explorers

---

## 🧪 **Testing Flow**

### **Test 1: Simulate Deposit on Fuego Testnet**

```bash
# 1. User deposits XFG on Fuego testnet
# (Handle this via Fuego CLI - outside scope)

# Record transaction details:
TXN_HASH="<fuego_deposit_txn_hash>"
RECIPIENT="<your_eth_address>"
TIER=0  # 0.8 XFG deposit
```

---

### **Test 2: Generate STARK Proof**

```bash
# Generate proof using Rust CLI (separate cold-stark CLI later)
cargo run --bin xfg-stark-cli -- \
  create-package \
  $TXN_HASH \
  $RECIPIENT \
  cold_proof.json

# Validate package
cargo run --bin xfg-stark-cli -- validate cold_proof.json

# Generate STARK proof
cargo run --bin xfg-stark-cli -- \
  generate \
  cold_proof.json \
  cold_proof_final.json
```

---

### **Test 3: Submit to API (Mock for now)**

```bash
# In production, API would:
# 1. Validate STARK proof
# 2. Extract nullifier, commitment, recipient, tier
# 3. Call COLD verifier on Arbitrum

# For testing, call verifier directly as API:
# Note: depositTimestamp for legacy testing should be < 1735689600 (2026-01-01)
cast send $COLD_VERIFIER \
  "claimCD(address,uint8,bytes32,bytes32,uint256,uint256)" \
  $RECIPIENT \
  $TIER \
  <nullifier_from_proof> \
  <commitment_from_proof> \
  <network_id> \
  <deposit_timestamp> \
  --value 0.001ether \
  --private-key $API_VERIFIER_KEY \
  --rpc-url $ARB_SEPOLIA_RPC
```

---

### **Test 4: Verify CD Minted on L1**

```bash
# Wait ~10 minutes for L2→L1 message

# Check CD balance (edition 0)
cast call $CD_TOKEN \
  "balanceOf(address,uint256)" \
  $RECIPIENT \
  0 \
  --rpc-url $SEPOLIA_RPC

# Should return 640,000 for tier 0 (0.8 XFG deposit)
```

---

## 📊 **Expected Results**

### **Standard Deposits (Post-2026):**

| Tier | XFG Amount | Lock Period | APY | CD Minted (atomic) | CD Minted (readable) |
|------|-----------|-------------|-----|-------------------|---------------------|
| 0 | 0.8 XFG | 3 months | 8% | 640,000 | 0.00000064 CD |
| 1 | 0.8 XFG | 12 months | 27% | 2,160,000 | 0.00000216 CD |
| 2 | 8 XFG | 3 months | 18% | 14,400,000 | 0.0000144 CD |
| 3 | 8 XFG | 12 months | 33% | 26,400,000 | 0.0000264 CD |
| 4 | 80 XFG | 3 months | 27% | 216,000,000 | 0.000216 CD |
| 5 | 80 XFG | 12 months | 42% | 336,000,000 | 0.000336 CD |
| 6 | 800 XFG | 3 months | 33% | 2,640,000,000 | 0.00264 CD |
| 7 | 800 XFG | 12 months | 69% | 5,520,000,000 | 0.00552 CD |

### **Legacy Deposits (Pre-2026, 800 XFG only):**

| Tier | XFG Amount | Lock Period | APY | CD Minted (atomic) | CD Minted (readable) |
|------|-----------|-------------|-----|-------------------|---------------------|
| 6 | 800 XFG | 3 months | **80%** | 6,400,000,000 | 0.0064 CD |
| 7 | 800 XFG | 12 months | **80%** | 6,400,000,000 | 0.0064 CD |

---

## 🔍 **View Functions for Testing**

```bash
# Get tier info (standard)
cast call $COLD_VERIFIER "getTierInfo(uint8,bool)" 0 false

# Get tier info (legacy)
cast call $COLD_VERIFIER "getTierInfo(uint8,bool)" 6 true

# Get all standard tier amounts
cast call $COLD_VERIFIER "getAllTierAmounts()"

# Get legacy tier amounts (tier 6 and tier 7 only)
cast call $COLD_VERIFIER "getLegacyTierAmounts()"

# Check if deposit qualifies for legacy rate
cast call $COLD_VERIFIER "isLegacyDeposit(uint256,uint8)" 1735000000 6  # true (before 2026)
cast call $COLD_VERIFIER "isLegacyDeposit(uint256,uint8)" 1736000000 6  # false (after 2026)

# Check nullifier used
cast call $COLD_VERIFIER "isNullifierUsed(bytes32)" <nullifier>

# Get statistics
cast call $COLD_VERIFIER "getStatistics()"

# Estimate gas (standard)
cast call $COLD_VERIFIER "estimateL1GasFee(address,uint8,bool)" $RECIPIENT 0 false

# Estimate gas (legacy)
cast call $COLD_VERIFIER "estimateL1GasFee(address,uint8,bool)" $RECIPIENT 6 true
```

---

## 🐛 **Troubleshooting**

### **Error: "Only API verifier can submit proofs"**
- Make sure you're calling from the API verifier address
- Or update API verifier to your test address

### **Error: "Nullifier already used"**
- This nullifier was already claimed
- Generate a new proof with different XFG deposit

### **Error: "Only Arbitrum Outbox"**
- mintFromL2 can only be called by Arbitrum's Outbox
- Wait for L2→L1 message to relay

### **Error: "Commitment already used"**
- This commitment was already used on L1
- Prevents replay attacks

---

## 📝 **Contract Addresses (Record Here)**

### **Ethereum Sepolia:**
```
FuegoCOLDAOToken:     ____________________
COLDAOGovernor:       ____________________
```

### **Arbitrum Sepolia:**
```
COLDDepositProofVerifier: ____________________
API Verifier Address:     ____________________
```

### **Fuego Testnet:**
```
Deposit Address:      ____________________
```

---

## 🔗 **Next Steps**

1. ✅ Deploy contracts to testnets
2. ✅ Test end-to-end flow
3. ⏳ Build API backend for proof verification
4. ⏳ Integrate with frontend
5. ⏳ Security audit
6. ⏳ Mainnet deployment

---

## 📞 **Support**

- **Docs**: `/docs/`
- **Issues**: GitHub Issues
- **API**: usexfg.org (coming soon)

---

**Winter is coming. ❄️**
