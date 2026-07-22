# Dynamic Ring Size (Dynamixn)

> **Mintlify page:** [Dynamixn](/privacy/dynamixn)  
> This file is the engineer-facing mirror. Keep it aligned with `DynamicRingSize.cpp`.

## Approved ring sizes (BMV 10+)

In **preference order** (largest first):

1. **32** — maximum privacy  
2. **16** — strong privacy  
3. **8** — standard / mainnet minimum  

```cpp
// DynamicRingSize.cpp
return {32, 16, 8};
```

- **Mainnet BMV 10+:** if even ring size 8 is impossible → fail construction.  
- **Testnet:** may bootstrap below the ladder (including mixIn 0) until the decoy pool grows.

## Algorithm

1. Apply only for `blockMajorVersion >= 10`.  
2. Same-amount outputs only.  
3. Pick largest of \{32, 16, 8\} the pool can support.  
4. Config bounds: min ladder step 8 on mainnet; max 32.

## Do not document (stale)

Older drafts listed 18 / 15 / 12 / 10 / 8. **That ladder is not what the code ships.**
