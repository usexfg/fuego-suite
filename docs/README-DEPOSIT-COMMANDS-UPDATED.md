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
- **Confirmation**: Requires typing "yes" to confirm
- **Balance Check**: Automatically verifies sufficient funds

### 2. Create Burn Deposit
```bash
burn <amount>
```
- **amount**: Burn amount in atomic units (1 XFG = 10,000,000 atomic units)
- **Minimum amount**: 0.8 XFG (8,000,000 atomic units)
- **Term**: Automatically set to "Forever" for burn deposits

### 3. List All Deposits
```bash
list_deposits
```
- Displays a table of all deposits in the wallet
- Shows ID, Amount, Term, Status, and Unlock Height

### 4. Show Deposit Information
```bash
deposit_info <deposit_id>
```
- **deposit_id**: Numeric ID of the deposit to display
- Shows detailed information about a specific deposit

### 5. Withdraw Deposit
```bash
withdraw_deposit <deposit_id>
```
- **deposit_id**: Numeric ID of the deposit to withdraw
- Only unlocked deposits can be withdrawn

## Usage Examples

### Creating a COLD Deposit
```
[wallet abc123]: create_COLD 800
Creating COLD deposit of 800 XFG for 3-month term (16440 blocks)
Transaction fee: 0.0008 XFG
Total cost: 800.0008 XFG
Available balance after: 199.9992 XFG
Confirm (yes/no): yes
Creating 3-month COLD deposit...
COLD deposit transaction created successfully. Transaction ID: 5
Please wait for confirmation...
```

### Creating a Burn Deposit
```
[wallet abc123]: burn 8000000
Creating burn deposit...
Burn deposit transaction created successfully. Transaction ID: 6
Transaction hash will be available after confirmation.
Next steps:
  1. Wait for 10+ confirmations
  2. Request elderfier consensus using the transaction hash
  3. Generate STARK proof for HEAT minting
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

## Fuego Banking System Overview

### Deposit Types

1. **COLD Deposits**
   - Standard deposits for earning interest
   - Fixed 3-month term (16,440 blocks)
   - Minimum 800 XFG
   - Earn interest over time
   - User-friendly creation with confirmation

2. **Burn Deposits**
   - Burn XFG to mint HEAT on the Arbitrum network
   - Forever term (irreversible)
   - Minimum 0.8 XFG
   - Part of the Æternal Flame burn-to-mint system

### Burn-to-Mint Process

1. **Create Burn Deposit**: Use the `burn` command
2. **Wait for Confirmations**: Allow 10+ block confirmations
3. **Request Consensus**: Get elderfier network consensus
4. **Generate STARK Proof**: Create cryptographic proof for HEAT minting
5. **Claim HEAT**: Submit proof to Arbitrum contract

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
- **COLD Deposits**: Standard transaction fee (0.0008 XFG)
- **Burn Deposits**: Special burn fee structure
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