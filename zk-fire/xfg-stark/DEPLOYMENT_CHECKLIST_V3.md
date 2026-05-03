# XFG-STARK v3 Deployment Checklist

**Target Networks:**
- Arbitrum Sepolia (Testnet)
- Ethereum Sepolia (Testnet)
- Arbitrum One (Mainnet)
- Ethereum Mainnet

---

## ✅ **Pre-Deployment**

### **1. Code Review**
- [ ] Solidity contracts reviewed and audited
- [ ] Rust CLI tested with all 4 HEAT tiers (0-3) and all 8 COLD tiers (0-7)
- [ ] Test coverage >90%
- [ ] No critical TODOs in code
- [ ] All old Eldernode references removed

### **2. Testing Complete**
- [ ] Local testing (Hardhat/Foundry)
- [ ] Testnet deployment successful
- [ ] End-to-end flow verified
- [ ] Gas optimization completed
- [ ] API backend tested

---

## 📦 **Testnet Deployment (Sepolia)**

### **Step 1: Deploy Ethereum L1 Contracts**
```bash
# Deploy HEAT Token
forge create HEATToken \
  --constructor-args <owner> <initial_minter> \
  --private-key $PRIVATE_KEY \
  --rpc-url $SEPOLIA_RPC

# Deploy COLDAO Token
forge create FuegoCOLDAOToken \
  --constructor-args <initial_minter> <governor> <owner> \
  --private-key $PRIVATE_KEY \
  --rpc-url $SEPOLIA_RPC

# Deploy COLDAO Governor
forge create COLDAOGovernor \
  --constructor-args <cd_token> <initial_apy> <owner> \
  --private-key $PRIVATE_KEY \
  --rpc-url $SEPOLIA_RPC

# Deploy LP Rewards Manager
forge create LPRewardsManager \
  --constructor-args <cd_token> <heat_token> <lp_token> <edition_id> <owner> \
  --private-key $PRIVATE_KEY \
  --rpc-url $SEPOLIA_RPC
```

**Record addresses:**
- [ ] HEATToken: `____________________`
- [ ] FuegoCOLDAOToken: `____________________`
- [ ] COLDAOGovernor: `____________________`
- [ ] LPRewardsManager: `____________________`

---

### **Step 2: Deploy Arbitrum L2 Contracts**
```bash
# Deploy HEAT Burn Proof Verifier v3
forge create HEATBurnProofVerifier_v3 \
  --constructor-args <heat_token> <api_verifier> <owner> \
  --private-key $PRIVATE_KEY \
  --rpc-url $ARB_SEPOLIA_RPC

# Deploy COLD Proof Verifier v3
forge create COLDProofVerifier_v3 \
  --constructor-args <cd_token> <governor> <api_verifier> <owner> \
  --private-key $PRIVATE_KEY \
  --rpc-url $ARB_SEPOLIA_RPC
```

**Record addresses:**
- [ ] HEATBurnProofVerifier_v3: `____________________`
- [ ] COLDProofVerifier_v3: `____________________`

---

### **Step 3: Configure Contracts**

**On Ethereum L1:**
```solidity
// Authorize minters for CD token
cdToken.addAuthorizedMinter(coldProofVerifier_v3);
cdToken.addAuthorizedMinter(lpRewardsManager);

// Update HEAT token minter
heatToken.updateMinter(heatVerifier_v3);
```

**On Arbitrum L2:**
```solidity
// Set API verifier addresses
heatVerifier_v3.updateAPIVerifier(usexfgBackend);
coldVerifier_v3.updateAPIVerifier(usexfgBackend);
```

**Verification checklist:**
- [ ] CD token has 2 authorized minters
- [ ] HEAT token minter updated
- [ ] API verifier set on both L2 contracts
- [ ] Governor set on COLD verifier
- [ ] LP manager has correct edition ID

---

### **Step 4: Test End-to-End Flow**

**HEAT Burn Flow:**
```bash
# 1. Generate test burn proof
xfg-stark-cli create-package <txn_hash> <recipient> heat_package.json

# 2. Submit to API
curl -X POST https://testnet.usexfg.org/api/v1/verify-proof \
  -d @heat_package.json

# 3. Verify tokens minted on L1
cast call $HEAT_TOKEN "balanceOf(address)" $RECIPIENT --rpc-url $SEPOLIA_RPC
```

**COLD Deposit Flow:**
```bash
# 1. Generate test deposit proof
xfg-stark-cli create-package <txn_hash> <recipient> cold_package.json

# 2. Submit to API
curl -X POST https://testnet.usexfg.org/api/v1/verify-proof \
  -d @cold_package.json

# 3. Verify CD minted on L1
cast call $CD_TOKEN "balanceOf(address,uint256)" $RECIPIENT 0 --rpc-url $SEPOLIA_RPC
```

**LP Rewards Flow:**
```bash
# 1. Stake LP tokens
cast send $LP_REWARDS_MANAGER "stakeLPTokens(uint256)" 1000000 \
  --private-key $PRIVATE_KEY --rpc-url $SEPOLIA_RPC

# 2. Wait 1 day

# 3. Claim rewards
cast send $LP_REWARDS_MANAGER "claimRewards()" \
  --private-key $PRIVATE_KEY --rpc-url $SEPOLIA_RPC

# 4. Verify CD balance
cast call $CD_TOKEN "balanceOf(address,uint256)" $USER 0 --rpc-url $SEPOLIA_RPC
```

**Test results:**
- [ ] HEAT tokens minted correctly
- [ ] CD tokens minted correctly
- [ ] LP rewards claimed successfully
- [ ] No nullifier replay possible
- [ ] Gas costs acceptable

---

## 🚀 **Mainnet Deployment**

### **Pre-Mainnet Checklist:**
- [ ] All testnet tests passed
- [ ] Security audit completed
- [ ] Multisig setup for ownership
- [ ] Gas price acceptable
- [ ] Sufficient ETH for deployment
- [ ] API backend production-ready
- [ ] Monitoring/alerting configured
- [ ] Documentation updated
- [ ] Community announcement ready

### **Mainnet Deployment Steps:**

**Same as testnet but with:**
- Use production RPC URLs
- Use multisig as owner
- Higher gas price for faster confirmation
- Verify contracts on Etherscan/Arbiscan
- Announce contract addresses publicly

**Post-Deployment:**
- [ ] Contracts verified on explorers
- [ ] Transfer ownership to multisig
- [ ] Add contracts to frontend
- [ ] Update documentation
- [ ] Monitor for 24 hours
- [ ] Announce to community

---

## 🔐 **Security**

### **Access Control:**
- [ ] All contracts owned by multisig
- [ ] API verifier address is trusted backend
- [ ] No test keys in production
- [ ] Rate limiting enabled on API
- [ ] Nullifier database backed up

### **Monitoring:**
- [ ] Set up alerts for:
  - Large mints
  - Unusual nullifier patterns
  - Failed verifications
  - Contract pauses
  - Ownership changes

---

## 📊 **Metrics to Track**

### **On-Chain:**
- Total HEAT minted
- Total CD minted
- Total XFG locked
- Number of claims
- Gas costs per claim

### **API:**
- Proof submissions/day
- Verification success rate
- Average verification time
- Error rate by type
- API uptime

---

## 🆘 **Emergency Procedures**

### **If Security Issue Found:**
1. Pause all contracts
2. Notify multisig signers
3. Investigate issue
4. Deploy fixes if needed
5. Unpause after review

### **Pause Commands:**
```solidity
heatVerifier_v3.pause();
coldVerifier_v3.pause();
cdToken.pause();
lpRewardsManager.pause();
```

---

## 📞 **Contact Info**

- **Lead Dev**: `____________________`
- **Security**: `____________________`
- **Backend**: `____________________`
- **Multisig**: `____________________`

---

**Status:** ⏳ Ready for testnet deployment

**Last Updated:** 2026-01-17
