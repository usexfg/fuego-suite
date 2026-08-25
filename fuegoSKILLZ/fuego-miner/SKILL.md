---
name: fuego-miner
description: "Fuego domain expert for mining: proof-of-work, difficulty adjustment, block rewards, hashrate, and mining operations."
risk: low
source: user-provided
---

# Fuego Miner Expert

Domain expert for Fuego mining: PoW, difficulty, hashrate, and block rewards.

## Scope

- **Proof of Work**: Hashing algorithms
- **Difficulty**: DMWDA adjustment algorithm
- **Hashrate**: Network hashrate estimation
- **Block Rewards**: Miner compensation
- **Mining Operations**: Solo and pool mining

## Trigger Set

**Should trigger on:**
- "mine", "mining", "miner"
- "difficulty", "adjustment", "dmwda"
- "hashrate", "hash power", "hash"
- "block reward", "reward", "coinbase"
- "proof of work", "pow", "nonce"

**Should NOT trigger on:**
- Non-Fuego mining questions
- Generic PoW unrelated to Fuego

## Difficulty Adjustment

**Source:** `src/CryptoNoteConfig.h`

### DMWDA (Dynamic Multi-Window Difficulty Adjustment)

**Algorithm:** Zawy-LWMA1 (v4)

| Parameter | Value | Purpose |
|------------|-------|---------|
| DIFFICULTY_TARGET | 480 | Target seconds per block |
| DIFFICULTY_WINDOW_V4 | 45 | LWMA1 window (~180 blocks/day) |
| DIFFICULTY_WINDOW_V3 | 60 | Legacy LWMA1 window |
| DIFFICULTY_WINDOW_V2 | 18 | Zawy v1.0 window |

**Formula:**
```
new_difficulty = old_difficulty × (target_time / actual_time)
                × smoothing_factor
```

## Block Rewards

**Source:** `src/CryptoNoteCore/Currency.cpp`

### Reward Formula (CryptoNote)
```
reward = (reward_base - already_generated) / 2^(height / 2^EMISSION_SPEED_FACTOR)
```

### Fuego-Specific
- **Emission Speed Factor:** 20 (version 9)
- **Reward Block Size:** 430,080 bytes (420KB)
- **Max Block Size:** 8,000,000 bytes

### Basic Reward
```cpp
uint64_t basic_reward = fees + rewrite_amount;
if (block_size < BLOCK_GRANTED_FULL_REWARD_ZONE)
    basic_reward += BASE_REWARD;
```

## Hashing

### CN-FastHash
**Source:** `src/crypto/slow-hash.cpp`

Primary PoW hashing:
```cpp
Hash cn_fast_hash(const void* data, size_t len);
```

### Hash Properties
- Memory-hard (Argon2-style)
- ASIC-resistant
- 2 MB scratchpad

## Hashrate Estimation

**Source:** `src/Miner/Miner.cpp`

```
estimated_hashrate = network_difficulty / block_time
```

**Formula:**
```
hashrate = difficulty × 2^32 / block_time_seconds
```

## Usage

```python
from references import MinerExpert

expert = MinerExpert(source_dir="/Users/aejt/fuego")

# Calculate block reward
reward = expert.calculate_block_reward(height=1000000, fees=5000000)

# Estimate hashrate
hashrate = expert.estimate_hashrate(difficulty=50000000000, block_time=480)

# Analyze difficulty adjustment
difficulty = expert.get_current_difficulty()

# Get mining stats
stats = expert.get_mining_stats()
```

## Key Files

| File | Purpose |
|------|---------|
| `src/Miner/Miner.cpp` | Miner implementation |
| `src/Miner/Miner.h` | Miner class |
| `src/crypto/slow-hash.cpp` | PoW hashing |
| `src/CryptoNoteConfig.h` | Difficulty constants |
| `src/CryptoNoteCore/Currency.cpp` | Block rewards |

## Utilities

- Use `fuego-rag` for semantic code search
- Use `fuego-codebase-mapper` for file/function search
