---
name: fuego-tx
description: "Fuego domain expert for transactions: transaction types, inputs, outputs, RingCT privacy, transaction states, and blockchain state."
risk: low
source: user-provided
---

# Fuego Transaction Expert

Domain expert for Fuego transactions: types, states, RingCT privacy, and blockchain.

## Scope

- **Transaction Types**: Coinbase, transfer, deposit, swap
- **Inputs/Outputs**: Transaction components
- **RingCT**: Privacy protocol (MLSAG + Pedersen)
- **Transaction Pool**: Unconfirmed transactions
- **State**: Blockchain transaction state

## Trigger Set

**Should trigger on:**
- "transaction", "tx", "input", "output"
- "ringct", "privacy", "ring signature"
- "state", "utxo", "unspent"
- "pool", "mempool", "unconfirmed"
- "coinbase", "base reward"

**Should NOT trigger on:**
- Non-Fuego transaction questions
- Generic blockchain transactions unrelated to Fuego

## Transaction Types

**Source:** `src/CryptoNoteCore/Transaction.h`

### Transaction Types
```
TX_TYPE_REGULAR = 0      # Standard transfer
TX_TYPE_COINBASE = 1   # Block reward
TX_TYPE_DEPOSIT = 2     # CD deposit
TX_TYPE_SWAP = 3       # Atomic swap
```

### HEAT/HEARTH Transaction Types (v11+)

Commitment outputs with special term markers handle HEAT and HEARTH:

| Term Marker | Value | Type |
|-------------|-------|------|
| HEAT_TERM | 0xFFFFFFFF | HEAT CD deposit (permanent) |
| TERM_REGULAR | 0 | Non-locked output (mint or AMM) |
| DEPOSIT_TERM_LP | 0xFFFFFFFD | Hearth LP share |
| DEPOSIT_TERM_POOL_XFG | 0x504F4C58 ('POLX') | AMM pool XFG reserves |
| DEPOSIT_TERM_POOL_HEAT | 0x504F4C48 ('POLH') | AMM pool HEAT reserves |
| DEPOSIT_TERM_SWAP_RECEIVE_XFG | 0x53575258 ('SWRX') | User receives XFG from HEAT→XFG swap |

### Commitment Outputs (tx_extra tags)
| Tag | Type | Purpose |
|-----|------|---------|
| 0x08 | HEAT commitment | HEAT mint/burn via commitment |
| 0xCD | COLD commitment | Cold deposit commitment |
| 0xD5 | Deposit secret | Encrypted deposit secret for COLD withdrawal |

### v12 Auth Tags
| Tag | Type | Purpose |
|-----|------|---------|
| 0xF5 | HEAT mint auth | v12 HEAT mint authorization |
| 0xF6 | AMM swap auth | v12 AMM swap authorization |
| 0xF7 | LP add auth | v10 LP add authorization |
| 0xF8 | LP rem auth | v10 LP remove authorization |
| 0xF9 | HEAT send auth | v12 HEAT send authorization |

### Orderbook Tags (v11+)
| Tag | Type | Purpose |
|-----|------|---------|
| 0xFA | Order place | Limit order placement |
| 0x0F | Order cancel | Cancel order by UTXO reference |
| 0xFC | Market buy auth | Market buy authorization |
| 0xFD | Market sell auth | Market sell authorization |

### Transaction Flow for HEAT Mint
1. User burns XFG → commitment output with tag 0x08, term=0
2. `HeatMintEngine::validateMint()` checks burning against redemption price
3. v12: `validateMintAuth()` validates declared burn/mint amounts
4. Output is classified as HEAT transaction in Blockchain.cpp
5. Commitment indexed in CommitmentIndex, output tracked in m_outputs

### Transaction Fields
```cpp
struct Transaction {
    uint8_t version;
    uint64_t unlockTime;
    TransactionInputs inputs;      // Key images + amounts
    TransactionOutputs outputs;    // Commitments + amounts
    vector<uint8_t> extra;         // Payment ID, etc.
    // ... RingCT data
};
```

## RingCT Privacy

**Protocol:** CryptoNote RingCT

### Components
1. **MLSAG Ring Signatures**: Sign inputs with decoys
2. **Pedersen Commitments**: Hide amounts
3. **Range Proofs**: Prove amount > 0

### Ring Selection
- **Min Mixin**: 8 (from block v10)
- **Max Mixin**: 32

## Transaction Flow

### Standard Transfer
1. **Create**: Build transaction with inputs/outputs
2. **Sign**: MLSAG ring signature
3. **Broadcast**: P2P network
4. **Pool**: Transaction pool
5. **Mine**: Included in block
6. **Confirm**: Block confirmations

### HEAT Mint Flow
1. Validate minimum mint (0.1 HEAT)
2. Calculate redemption price from oracle
3. Build commitment output with tag 0x08, term=0
4. Apply 3.33% mint premium
5. Split burn: 50% eternal flame, 50% treasury (60% LP / 40% peg)
6. Mint HEAT at computed rate

### HEARTH Swap Flow
1. User sends `hearth_xfg` or `hearth_heat` command
2. Wallet builds AMM swap transaction with tx_extra tags
3. Constant product formula calculates output
4. 0.3% fee → LP pool reserves
5. Output to user via DEPOSIT_TERM_SWAP_RECEIVE_XFG marker
6. Pool reserves updated (DEPOSIT_TERM_POOL_XFG / DEPOSIT_TERM_POOL_HEAT)

## Usage

```python
from references import TxExpert

expert = TxExpert(source_dir="/Users/aejt/fuego")

# Analyze transaction
tx = expert.analyze_transaction(tx_hash)

# Get tx type
tx_type = expert.get_tx_type(tx_data)

# Verify ring signature
valid = expert.verify_ring_signature(tx, ring_members)

# Get transaction pool
pool = expert.get_transaction_pool()
```

## Key Files

| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/Transaction.h` | Transaction structure |
| `src/CryptoNoteCore/Transaction.cpp` | Transaction implementation |
| `src/CryptoNoteCore/TransactionPool.h` | Transaction pool |
| `src/CryptoNoteCore/TransactionExtra.h/cpp` | Commitment tags (0x08 HEAT, 0xCD COLD) |
| `src/CryptoNoteCore/Blockchain.cpp` | HEAT tx classification, pool tracking |
| `src/CryptoNoteCore/HeatMintEngine.h/cpp` | HEAT mint validation |
| `src/crypto/mlsag.h` | Ring signatures |
| `src/crypto/pedersen.h` | Commitments |

## Utilities

- Use `fuego-rag` for semantic code search
- Use `fuego-codebase-mapper` for file/function search
