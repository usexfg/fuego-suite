# 🔥 XFG → HEAT STARK Proof User Guide

This guide explains how to use the STARK proof data package system to generate proofs for XFG burns and HEAT minting.

## 📋 **Overview**

The STARK proof system allows you to:
1. **Package your burn data** into a structured JSON file
2. **Validate the data** before proof generation
3. **Generate STARK proofs** using the CLI tool
4. **Submit proofs** to the HEAT mint contract

## 🚀 **Quick Start**

### **Step 1: Create a Data Package**

```bash
# Create a standard burn template
xfg-stark-cli create-template standard -o standard_template.json

# Create your data package
xfg-stark-cli create-package \
  --template standard_template.json \
  --burn-amount 0.8 \
  --txn-hash 0x7D0725F8E03021B99560ADD456C596FEA7D8DF23529E23765E56923B73236E4D \
  --recipient 0x742d35Cc6634C0532925a3b8D4C9db96C4b4d8b6 \
  --secret "my-secret-key-123" \
  --network fuego-mainnet \
  --output my_burn_package.json
```

### **Step 2: Edit Your Package**

Open `my_burn_package.json` and add:
- **Block height** where your burn occurred
- **Timestamp** of the burn transaction
- **Optional metadata** (ENS names, labels, etc.)

### **Step 3: Validate Your Package**

```bash
xfg-stark-cli validate -i my_burn_package.json
```

### **Step 4: Generate STARK Proof**

```bash
xfg-stark-cli generate -i my_burn_package.json -o proof.json
```

## 📦 **Data Package Structure**

### **Required Fields**

| Field | Description | Example |
|-------|-------------|---------|
| `burn_amount_xfg` | Burn amount in XFG | `"0.8"` or `"800.0"` |
| `transaction_hash` | Fuego burn transaction hash | `"0x7D0725F8E03021B99560ADD456C596FEA7D8DF23529E23765E56923B73236E4D"` |
| `ethereum_address` | HEAT recipient address | `"0x742d35Cc6634C0532925a3b8D4C9db96C4b4d8b6"` |
| `secret_key` | Your private secret | `"my-secret-key-123"` |

### **Optional Fields**

| Field | Description | Example |
|-------|-------------|---------|
| `block_height` | Block where burn occurred | `1234567` |
| `timestamp` | Burn transaction timestamp | `1705312200` |
| `ens_name` | ENS name for recipient | `"alice.eth"` |
| `label` | Human-readable label | `"Alice's HEAT wallet"` |
| `salt` | Additional security | `"random-salt-67890"` |


## 🔐 **Security Best Practices**

### **Secret Management**
- ✅ **Use strong, unique secrets** for each burn
- ✅ **Store secrets securely** (password manager, hardware wallet)
- ✅ **Never share secrets** with anyone
- ✅ **Use salt** for additional security

### **Data Validation**
- ✅ **Always validate** your package before proof generation
- ✅ **Verify transaction details** (hash, amount, block height)
- ✅ **Double-check recipient address** before submission
- ✅ **Test on testnet** before mainnet

## 📊 **Supported Burn Amounts**

| Burn Amount | Atomic Units | Use Case |
|-------------|--------------|----------|
| **0.8 XFG** | 8,000,000 | Standard burns, regular HEAT accumulation |
| **800 XFG** | 8,000,000,000 | Large burns, bulk HEAT minting |

## 🌐 **Network Support**

| Network | Description | Use Case |
|---------|-------------|----------|
| `fuego-mainnet` | Production network | Real XFG burns and HEAT minting |
| `fuego-testnet` | Test network | Testing and development |

## 🛠️ **CLI Commands Reference**

### **Generate Proof**
```bash
xfg-stark-cli generate -i <package.json> -o <proof.json> [-f <format>]
```

**Options:**
- `-i, --input`: Input data package file
- `-o, --output`: Output proof file
- `-f, --format`: Output format (`json`, `binary`, `hex`)

### **Validate Package**
```bash
xfg-stark-cli validate -i <package.json>
```

**Options:**
- `-i, --input`: Input data package file

### **Create Template**
```bash
xfg-stark-cli create-template <type> -o <template.json>
```

**Types:**
- `standard`: 0.8 XFG burn template
- `large`: 800 XFG burn template
- `custom`: Custom template

### **Create Package**
```bash
xfg-stark-cli create-package \
  --template <template.json> \
  --burn-amount <amount> \
  --txn-hash <hash> \
  --recipient <address> \
  --secret <secret> \
  --network <network> \
  --output <package.json>
```

## 📁 **File Formats**

### **Data Package (.json)**
```json
{
  "metadata": { ... },
  "burn_transaction": { ... },
  "recipient": { ... },
  "secret": { ... },
  "additional_data": { ... }
}
```

### **STARK Proof (.json)**
```json
{
  "proof": "<base64-encoded-proof>",
  "public_inputs": { ... },
  "metadata": { ... }
}
```

### **STARK Proof (.binary)**
Raw binary proof data for direct contract submission.

### **STARK Proof (.hex)**
Hex-encoded proof data for debugging and verification.

## 🔍 **Troubleshooting**

### **Common Errors**

| Error | Cause | Solution |
|-------|-------|----------|
| `Burn amount must be exactly 0.8 XFG or 800.0 XFG` | Invalid burn amount | Use only 0.8 or 800.0 |
| `Transaction hash must start with 0x` | Missing 0x prefix | Add `0x` to transaction hash |
| `Ethereum address must be 0x-prefixed 40-character hex` | Invalid address format | Use valid 0x-prefixed Ethereum address |
| `Secret key must be at least 8 characters` | Secret too short | Use longer secret key |

### **Validation Warnings**

| Warning | Meaning | Action |
|---------|---------|--------|
| `Block height is 0` | Block height not set | Add actual block height |
| `Timestamp is 0` | Timestamp not set | Add actual transaction timestamp |

## 📞 **Support**

If you encounter issues:

1. **Check the validation output** for specific errors
2. **Verify your data** matches the expected format
3. **Test with example data** to isolate the problem
4. **Check network connectivity** for template downloads
5. **Review the logs** for detailed error information

## 🔗 **Next Steps**

After generating your STARK proof:

1. **Bundle proof with merkle data**: `bundle <package.json> <proof.json> <commitment_hash> <bundle.json>`
   - The CLI fetches merkle proof + Elderfier consensus data from Fuego daemon
   - Requires >=69% Elderfier consensus on the merkle root
2. **Submit bundle to L2 contract**: Submit `bundle.json` to the `FuegoCommitmentMerkleVerifier` contract on Arbitrum
   - Contract verifies merkle proof + Elderfier signatures against on-chain registry
3. **Wait for L2 confirmation** on Arbitrum
4. **L2->L1 message relay**: Token minting initiated via Arbitrum Outbox
5. **Receive HEAT/COLD tokens** in your wallet on Ethereum L1

## 📚 **Additional Resources**

- [Architecture & Implementation Summary](../IMPLEMENTATION_SUMMARY_V3.md)
- [STARK Proof Technical Documentation](../README.md)
- [HEAT Contract Integration](../contracts/README.md)
