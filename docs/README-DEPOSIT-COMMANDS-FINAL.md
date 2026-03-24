# Fuego Deposit Commands in SimpleWallet

The Fuego SimpleWallet (xfg_wallet) now includes direct deposit functionality that was previously only available through walletd RPC calls. This integration allows users to create and manage deposits directly from the command-line interface without needing to connect to a separate walletd service.

## Available Deposit Commands

### 1. Create COLD Deposit
```bash
create_COLD <amount>
```
- **amount**: Deposit amount in XFG (e.g., 800 for 800 XFG)
- **term**: Automatically set to 16,440 blocks (3 months) - network standard
- **Minimum amount**: 800 XFG
- **Fee**: 0.8 XFG (same as large burn deposits)
- **Confirmation**: Requires typing "yes" to confirm
- **Balance Check**: Automatically verifies sufficient funds

### 2. Create Standard Burn Deposit
```bash
create_burn
```
- **Amount**: Fixed at 0.8 XFG (8,000,000 atomic units)
- **Fee**: 0.008 XFG (standard burn fee)
- **Term**: Automatically set to "Forever" for burn deposits
- **Automatic Commitment**: Embeds commitment data for STARK proof generation

### 3. Create Large Burn Deposit
```bash
create_large_burn <amount>
```
- **amount**: Burn amount in XFG (e.g., 800 for 800 XFG)
- **Minimum amount**: 800 XFG
- **Fee**: 0.8 XFG (large burn fee)
- **Term**: Automatically set to "Forever" for burn deposits
- **Confirmation**: Requires typing "yes" to confirm
- **Balance Check**: Automatically verifies sufficient funds

### 4. List All Deposits
```bash
list_deposits
```
- Displays a table of all deposits in the wallet
- Shows ID, Amount, Term, Status, and Unlock Height

### 5. Show Deposit Information
```bash
deposit_info <deposit_id>
```
- **deposit_id**: Numeric ID of the deposit to display
- Shows detailed information about a specific deposit

### 6. Withdraw Deposit
```bash
withdraw_deposit <deposit_id>
```
- **deposit_id**: Numeric ID of the deposit to withdraw
- Only unlocked deposits can be withdrawn

### 7. Generate Proof
```bash
generate_proof <tx_hash>
```
- **tx_hash**: Transaction hash of burn or COLD deposit
- Generates proof data for use with xfg-stark-cli

## Usage Examples

### Creating a COLD Deposit
```
[wallet abc123]: create_COLD 800
Creating COLD deposit of 800 XFG for 3-month term (16440 blocks)
Transaction fee: 0.8 XFG
Total cost: 800.8 XFG
Available balance after: 199.2 XFG
Confirm (yes/no): yes
Creating 3-month COLD deposit...
COLD deposit transaction created successfully. Transaction ID: 5
Please wait for confirmation...
```

### Creating a Standard Burn Deposit
```
[wallet abc123]: create_burn
Creating standard burn deposit (0.8 XFG)...
Standard burn deposit transaction created successfully. Transaction ID: 6
Transaction hash will be available after confirmation.
Next steps:
  1. Wait for 10+ confirmations
  2. Use transaction hash with xfg-stark-cli to generate STARK proof
  3. Submit proof to HEAT contract for minting
```

### Creating a Large Burn Deposit
```
[wallet abc123]: create_large_burn 800
Creating large burn deposit of 800 XFG
Transaction fee: 0.8 XFG
Total cost: 800.8 XFG
Available balance after: 199.2 XFG
Confirm (yes/no): yes
Creating large burn deposit...
Large burn deposit transaction created successfully. Transaction ID: 7
Transaction hash will be available after confirmation.
Next steps:
  1. Wait for 10+ confirmations
  2. Use transaction hash with xfg-stark-cli to generate STARK proof
  3. Submit proof to HEAT contract for minting
```

### Listing Deposits
```
[wallet abc123]: list_deposits
Deposits (2):
ID    Amount          Term      Status      Unlock Height
--------------------------------------------------------
  0          800.0000     16440      Locked         45000
  1            0.8000   Forever    Unlocked         12345
```

### Viewing Deposit Details
```
[wallet abc123]: deposit_info 1
Deposit Information:
  ID: 1
  Amount: 0.8000
  Term: Forever (Burn)
  Status: Unlocked
  Creating Transaction ID: 3
  Spending Transaction ID: Not spent
  Height: 12000
  Unlock Height: 12345
  Transaction Hash: a1b2c3d4e5f6...
```

### Withdrawing an Unlocked Deposit
```
[wallet abc123]: withdraw_deposit 1
Withdrawing deposit...
Deposit withdrawal transaction created successfully. Transaction ID: 7
```

### Generating Proof for Deposit
```
[wallet abc123]: generate_proof a1b2c3d4e5f6...
Found burn secret for transaction: a1b2c3d4e5f6...
Secret: abc123...
Amount: 8000000 atomic XFG

=== STARK PROOF DATA FOR CONTRACT ===
Transaction Hash: a1b2c3d4e5f6...
Secret Key: abc123...
Amount: 8000000 atomic XFG
=====================================
Proof data generated for burn transaction a1b2c3d4e5f6...
```

## Fuego Banking System Overview

### Deposit Types

1. **COLD Deposits**
   - Standard deposits for earning interest
   - Fixed 3-month term (16,440 blocks)
   - Minimum 800 XFG
   - 0.8 XFG fee (same as large burn deposits)
   - Earn interest over time
   - User-friendly creation with confirmation
   - Automatic commitment generation for proof generation

2. **Burn Deposits**
   - Burn XFG to mint HEAT on the Arbitrum network
   - Forever term (irreversible)
   - Minimum 0.8 XFG
   - Part of the Æternal Flame burn-to-mint system
   - Automatic commitment data embedding

### Burn-to-Mint Process

1. **Create Burn Deposit**: Use the `create_burn` or `create_large_burn` command
2. **Wait for Confirmations**: Allow 10+ block confirmations
3. **Generate STARK Proof**: Use `generate_proof` command and xfg-stark-cli
4. **Claim HEAT**: Submit proof to Arbitrum contract

## Security Best Practices

1. **Amount Verification**: Always double-check deposit amounts before confirming
2. **Term Understanding**: Understand the commitment period for deposits
3. **Burn Irreversibility**: Remember that burn deposits cannot be undone
4. **Backup Wallet**: Regularly backup your wallet file and remember your password
5. **Transaction Monitoring**: Monitor deposit transactions until confirmed
6. **Confirmation Requirement**: Always read and confirm deposit details

## Technical Details

### Deposit Parameters
- **Minimum COLD Deposit**: 800 XFG (8,000,000,000 atomic units)
- **Minimum Burn Deposit**: 0.8 XFG (8,000,000 atomic units)
- **Standard Term**: 16,440 blocks (3 months) - automatically used for COLD deposits
- **Forever Term**: 4,294,967,295 blocks (used for burn deposits)

### Fees
- **COLD Deposits**: 0.8 XFG fee (same as large burn deposits)
- **Standard Burn Deposits**: 0.008 XFG fee
- **Large Burn Deposits**: 0.8 XFG fee
- **Withdrawals**: Standard transaction fee

## Troubleshooting

### Common Issues

1. **"Deposit amount is too small"**
   - Ensure amount meets minimum requirements (800 XFG for COLD deposits)
   - Remember amounts are now in XFG, not atomic units

2. **"Deposit is still locked"**
   - Wait until the unlock height is reached
   - Check current blockchain height with `height` command

3. **"Deposit not found"**
   - Verify the deposit ID exists with `list_deposits`
   - Deposit IDs are zero-indexed

4. **"Insufficient balance"**
   - Check available balance with `balance` command
   - Ensure sufficient funds for amount plus transaction fee

### Getting Help

- Use `help` command to see all available commands
- Check wallet logs for detailed error information
- Visit Fuego community forums for additional support

This integration makes Fuego's powerful banking and deposit functionality directly accessible through the simplewallet interface, eliminating the need for separate walletd processes while maintaining full compatibility with the existing deposit ecosystem.