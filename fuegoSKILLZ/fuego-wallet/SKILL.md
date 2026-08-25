---
name: fuego-wallet
description: "Fuego domain expert for wallets: addresses, keys, balance, transactions, sending/receiving, and wallet operations."
risk: low
source: user-provided
---

# Fuego Wallet Expert

Domain expert for Fuego wallets: addresses, keys, balances, and operations.

## Scope

- **Addresses**: Address format and prefixes
- **Keys**: Spend key, view key generation
- **Balance**: Spendable and locked balances
- **Operations**: Send, receive, stake
- **Wallet Types**: CLI, GUI, integrated

## Trigger Set

**Should trigger on:**
- "wallet", "address", "addresses"
- "balance", "spendable", "locked"
- "send", "transfer", "receive"
- "keys", "view key", "spend key"
- "wallet restore", "seed", "mnemonic"

**Should NOT trigger on:**
- Non-Fuego wallet questions
- Generic crypto wallets unrelated to Fuego

## Address Format

**Source:** `src/CryptoNoteCore/CryptoNoteFormat.h`

### Format
```
fire<base58-encoded-data>
```

### Components
- Prefix: "fire" (1753191 in base58)
- Public spend key
- Public view key
- Checksum

### Example
```
fire1A2B3C4D5E6F7G8H9J0K1L2M3N4O5P6Q7R8S9T0U1V2W3X4Y5Z6
```

## Key System

### Dual-Key System
- **Spend Key**: Signing authority (never shared)
- **View Key**: Reading authority (can share for audit)

### Key Generation
```cpp
// Generate from seed
void generate_from_seed(const Seed& seed, PublicKey& spend, PublicKey& view);

// Generate random
void generate_keys(PublicKey& spend, SecretKey& spend_priv,
                  PublicKey& view, SecretKey& view_priv);
```

## Wallet Operations

### Send Transaction
1. Select inputs (UTXOs)
2. Select ring members (decoys)
3. Create outputs
4. Sign with spend key
5. Broadcast to network

### Receive
1. Monitor blockchain for outputs
2. Decrypt with view key
3. Detected as incoming

### Check Balance
```
Total = Sum(outgoing_commitments) - Sum(incoming_commitments)
Locked = Unlocked at height - Current height
Spendable = Total - Locked
```

### HEAT & HEARTH Commands (v11+)

| Command | Action |
|---------|--------|
| `mint_heat <xfg_amount>` | Burn XFG to mint HEAT |
| `hearth_add <xfg> <heat>` | Add liquidity to Hearth AMM |
| `hearth_xfg <xfg> <expected> <min_heat>` | Buy HEAT with XFG on Hearth |
| `hearth_heat <heat> <expected> <min_xfg>` | Sell HEAT for XFG on Hearth |
| `hearth_exit <lp_shares> <min_xfg> <min_heat>` | Remove liquidity from Hearth |
| `hearth_info` | Show Hearth AMM pool state |

### HEAT Mint Details
- Minimum mint: 0.1 HEAT (HEAT_MINT_MIN_HEAT = 1,000,000 atomic)
- 3.33% mint premium (HEAT_MINT_PREMIUM_BPS = 333)
- 50% of burned XFG → Eternal Flame (permanent deflation)
- 50% → Treasury (60% LP reserve, 40% peg defense)
- Overcollateralization gate: 1.5x minimum ratio
- Use `hearth_xfg` for amounts below minimum mint threshold
- No `burn_heat` command — HEAT→XFG redemption is not implemented yet

### Deposit Secret (v10+)
- Tag 0xD5 encrypts deposit secret for COLD withdrawal recovery
- Uses chacha8 encryption with ECDH key exchange
- Enables wallet recovery from seed phrase

### HEARTH Liquidity
- LP shares represent proportional pool ownership
- 0.3% fee on every Hearth swap → LP providers
- Pool terms: DEPOSIT_TERM_LP (0xFFFFFFFD), DEPOSIT_TERM_POOL_XFG (0x504F4C58), DEPOSIT_TERM_POOL_HEAT (0x504F4C48)

## Usage

```python
from references import WalletExpert

expert = WalletExpert(source_dir="/Users/aejt/fuego")

# Generate address
address = expert.generate_address(private_key)

# Get balance
balance = expert.get_balance(wallet_dir)
# Returns: {total, locked, spendable}

# Create transaction
tx_hash = expert.send(wallet_dir, destination, amount, fee)

# Parse address
keys = expert.parse_address(address)
# Returns: {spend_key, view_key}
```

## Key Files

| File | Purpose |
|------|---------|
| `src/WalletLegacy/SimpleWallet.cpp` | CLI wallet |
| `src/CryptoNoteCore/CryptoNoteFormat.h` | Address format |
| `src/crypto/crypto.h` | Key generation |
| `src/CryptoNoteCore/TransactionPool.h` | Tx pool |

## Wallet Binaries

| Binary | Purpose |
|--------|---------|
| `SimpleWallet` | CLI wallet |
| `fuego-wallet` | GUI wallet |
| `IntegratedWallet` | Embedded wallet |

## Utilities

- Use `fuego-rag` for semantic code search
- Use `fuego-codebase-mapper` for file/function search
