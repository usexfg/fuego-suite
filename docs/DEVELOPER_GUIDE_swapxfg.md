# Fuego Developer Guide: swapxfg, Aliases & CDs

> **Review Date:** April 2026  
> **Status:** Active Development  
> **Language:** C++ (Core), Go (TUI)

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Atomic Swaps (swapxfg)](#atomic-swaps-swapxfg)
3. [Certificate of Deposit (CD/FuegoCD)](#certificate-of-deposit-cdfuego-cd)
4. [On-Chain Aliases](#on-chain-aliases)
5. [Critical Issues to Fix](#critical-issues-to-fix)
6. [Key Files Reference](#key-files-reference)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                         User Interface                               │
├─────────────────────────────────────────────────────────────────────┤
│  fuego-wallet (C++)          │  swapxfg (Go TUI)                   │
│  ├── swap command            │  ├── Trading terminal               │
│  ├── cold command            │  ├── Order book                     │
│  ├── gen_proof               │  ├── CD market                       │
│  └── register_alias          │  └── Bridge integration             │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ RPC
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      SwapDaemon (C++)                                 │
├─────────────────────────────────────────────────────────────────────┤
│  SwapStateMachine  │  PoolAMM  │  AdaptorSwap  │  FuegoRpcClient │
│  SwapDatabase       │  PoolTypes │  Musig2       │  PriceOracle    │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Consensus
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    Blockchain (CryptoNote)                            │
├─────────────────────────────────────────────────────────────────────┤
│  CommitmentIndex  │  Blockchain  │  TransactionExtra  │  Core     │
│  DepositCommitment│  AliasIndex  │  DepositInfo       │           │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Atomic Swaps (swapxfg)

### Protocol Flow

The atomic swap uses **adaptor signatures** with **Musig2** multi-signature scheme:

```
┌─────────┐                    ┌─────────┐
│  Alice  │                    │   Bob   │
└────┬────┘                    └────┬────┘
     │                               │
     │  1. Exchange pubkeys         │
     │ ─────────────────────────────►│
     │                               │
     │  2. Bob generates adaptor T=t·G
     │ ◄─────────────────────────────│
     │                               │
     │  3. Alice funds escrow       │
     │     (XFG → joint key P)      │
     │ ─────────────────────────────►│
     │                               │
     │  4. Exchange pre-signatures   │
     │ ◄─────────────────────────────│
     │                               │
     │  5. Alice reveals t          │
     │ ─────────────────────────────►│
     │                               │
     │  6. Bob claims XFG           │
     │     (adapts sig with t)       │
     │                               │
```

### State Machine States

**Adaptor Signature Flow:**
```
INITIATED → ADAPTOR_KEYS_EXCHANGED → ADAPTOR_ESCROW_FUNDED → 
ADAPTOR_PRESIGS_READY → ADAPTOR_CTR_LOCKED → 
ADAPTOR_SECRET_REVEALED → ADAPTOR_XFG_SPENT
```

**Pool Operations Flow:**
```
POOL_DEPOSIT_INITIATED → ... → POOL_CHECKPOINT_GENERATED
POOL_WITHDRAW_INITIATED → ... → POOL_WITHDRAW_COMPLETE
POOL_SWAP_INITIATED → ... → POOL_SWAP_COMPLETE
```

### AMM Constant Product Formula

```cpp
// PoolAMM.cpp:23-40
(reserveIn + inputWithFee) * (reserveOut - output) >= reserveIn * reserveOut

where:
  inputWithFee = inputAmount * (10000 - feeBps) / 10000
```

### Key Components

| Component | Purpose | File |
|----------|---------|------|
| SwapStateMachine | State transitions | SwapStateMachine.cpp |
| SwapDaemon | Orchestration | SwapDaemon.cpp |
| AdaptorSwap | Adaptor sig protocol | AdaptorSwap.cpp |
| PoolAMM | LP pool math | PoolAMM.cpp |
| Musig2 | Multi-sig | src/crypto/musig2.cpp |

---

## Certificate of Deposit (CD/FuegoCD)

### Deposit Types

| Type  | Tag | Description |
|-------|-----|-------------|
| HEAT  | 0x08 | Permanent burn, key discarded |
|  xCD  | 0xCD | Term-locked deposit, earns interest |
| YIELD | 0x07 | Interest-bearing CIA deposits |

### Term Codes & APR

| Term | Code | APR | Blocks |
|------|------|-----|--------|
| 3 months | 1 | 8% | 16,440 |
| 9 months | 2 | 18% | 49,320 |
| 1 year | 3 | 21% | 65,760 |
| 3 years | 4 | 33% | 197,280 |
| 5 years | 5 | 80% | 328,800 |

### Commitment Generation (v3 Unified Format)

```cpp
// 56-byte preimage for keccak256:
secret[32] || le64(amount) || le32(networkId) || le32(chainId) || le32(version) || le32(term)
```

### Interest Calculation

Interest accrues from **swap fees** distributed to CD holders:
- 80% of epoch swap fees → CD yield pool
- 20% → Chain Treasury

---

## On-Chain Aliases

### Alias Registration

```cpp
// TX_EXTRA_ELDERFIER_ALIAS = 0xEA
struct TransactionExtraAliasRegistration {
  uint8_t version = 1;
  std::string alias;      // 8 chars [a-z0-9&]
  Crypto::Hash aliasHash; // cn_fast_hash(alias)
  Crypto::Hash addressHash; // cn_fast_hash(address) - privacy
  std::string ownerAddress; // Optional, often empty
  uint8_t aliasType = 0;  // 0=Elderfier, 1=Regular
};
```

### Validation Rules

- 8 characters
- Lowercase letters, numbers, ampersand
- Must send to dev fund address to register
- First-come-first-served

---

## Critical Issues to Fix

### 1. DLEQ Proof Verification Disabled ⚠️ CRITICAL

**File:** `src/SwapDaemon/Monero/AdaptorSignature.cpp:279`

```cpp
// TODO (protocol v2): verify DLEQ proof using distinct generators G and H.
return true;  // UNSAFE - always returns true
```

**Impact:** Breaks cryptographic binding property of adaptor signatures.

**Fix Required:** Implement DLEQ verification with distinct generators.

---

### 2. Integer Overflow in AMM ⚠️ CRITICAL

**File:** `src/SwapDaemon/PoolAMM.cpp:31-40`

```cpp
// UNSAFE - 64-bit overflow possible
uint64_t numerator = inputWithFee * reserveOut;
uint64_t denominator = reserveIn * 10000 + inputWithFee;
```

**Fix:** Use 128-bit arithmetic:
```cpp
__uint128_t numerator = (__uint128_t)inputWithFee * reserveOut;
__uint128_t denominator = (__uint128_t)reserveIn * 10000 + inputWithFee;
```

---

### 3. Interest Estimation Returns Zero ⚠️ CRITICAL

**File:** `src/WalletRpcServer/WalletRpcServer.cpp:870`

```cpp
res.estimated_interest = 0;  // Always zero
```

**Fix:** Expose CommitmentIndex via INode interface.

---

### 4. Rollover Not Implemented ⚠️ CRITICAL

**File:** `src/WalletRpcServer/WalletRpcServer.cpp:823`

```cpp
throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR,
  "rollover_cd requires Core access not yet exposed via INode");
```

**Fix:** Expose CommitmentIndex, implement rollover logic.

---

### 5. Weak Gift Secret Encryption ⚠️ HIGH

**File:** `src/CryptoNoteCore/TransactionExtra.cpp:1214`

```cpp
// UNSAFE - XOR is not encryption
for (size_t i = 0; i < secret.size(); ++i) {
  gift_secret[i] = secret[i] ^ chachaKey[i % chachaKey.size()];
}
```

**Fix:** Implement proper ChaCha20-Poly1305.

---

### 6. Epoch Reports Not Populated ⚠️ HIGH

**File:** `src/CryptoNoteCore/CommitmentIndex.cpp:288`

```cpp
EpochReport CommitmentIndex::generateEpochReport(...) const {
  report.epochNumber = epochNumber;
  // Missing: totalFeesDistributed, swapFeesCollected, feeRateFixedPoint
}
```

**Fix:** Populate all epoch report fields from fee pool data.

---

### 7. XOR "Encryption" for Secrets ⚠️ HIGH

**Same as #5** - Gift secrets use insecure XOR encryption.

---

### 8. Merkle Tree O(n²) Performance ⚠️ MEDIUM

**File:** `src/CryptoNoteCore/CommitmentIndex.cpp`

`computeMerkleRootInternal()` recomputes from scratch on every addition.

**Fix:** Use append-only Merkle tree structure.

---

## Key Files Reference

### Atomic Swap Files

| File | Lines | Status | Notes |
|------|-------|--------|-------|
| `src/SwapDaemon/SwapStateMachine.cpp` | 287 | ⚠️ Partial | Needs persistence |
| `src/SwapDaemon/SwapDaemon.cpp` | 810 | ⚠️ Partial | P2P incomplete |
| `src/SwapDaemon/PoolAMM.cpp` | 205 | ⚠️ Fix overflow | AMM math |
| `src/SwapDaemon/AdaptorSwap.cpp` | 228 | ⚠️ DLEQ disabled | Enable DLEQ verify |
| `src/SwapDaemon/Monero/AdaptorSignature.cpp` | 591 | ⚠️ DLEQ disabled | Enable DLEQ verify |
| `src/crypto/musig2.cpp` | 417 | ✅ Ready | Core complete |
| `src/crypto/adaptor.cpp` | ~100 | ✅ Ready | Core complete |
| `src/crypto/dleq.cpp` | ~100 | ✅ Ready | Core complete |
| `src/SwapDaemon/SwapTxBuilder.cpp` | 451 | ✅ Ready | Complete |

### CD/FuegoCD Files

| File | Lines | Status | Notes |
|------|-------|--------|-------|
| `src/CryptoNoteCore/DepositCommitment.cpp` | 243 | ✅ Ready | v3 commitments |
| `src/CryptoNoteCore/CommitmentIndex.cpp` | 1445 | ⚠️ Partial | Need epoch reports |
| `src/CryptoNoteCore/Blockchain.cpp` | 4180 | ✅ Ready | Validation complete |
| `src/Wallet/WalletGreen.cpp` | 4906 | ⚠️ Partial | Secrets not persisted |
| `src/WalletRpcServer/WalletRpcServer.cpp` | ~900 | ⚠️ Partial | Rollover not impl |

### Alias Files

| File | Lines | Status | Notes |
|------|-------|--------|-------|
| `src/CryptoNoteCore/AliasIndex.cpp` | ~300 | ⚠️ Partial | Hardcoded special cases |
| `src/CryptoNoteCore/TransactionExtra.cpp` | ~1400 | ✅ Ready | Serialization OK |

---

## Testing Checklist

### Atomic Swap
- [ ] DLEQ proof verification
- [ ] Nonce uniqueness enforcement
- [ ] State persistence on crash
- [ ] Timeout automatic refund
- [ ] AMM overflow protection
- [ ] Double precision in LP shares

### CDs/FuegoCDs
- [ ] Interest calculation accuracy
- [ ] Rollover functionality
- [ ] Epoch report population
- [ ] Gift secret encryption (ChaCha20)
- [ ] Deposit secret persistence
- [ ] Merkle tree performance

### Aliases
- [ ] Signature requirement for registration
- [ ] Rainbow table resistance
- [ ] Special case alias handling

---

## Security Audit Priority

1. **DLEQ Proof Verification** - Core atomic swap security
2. **AMM Integer Overflow** - Financial loss risk
3. **Gift Secret Encryption** - Privacy concern
4. **Nonce Reuse Prevention** - Key extraction risk
5. **Replay Protection** - Double-spend risk

---

## Related Documentation

- [Musig2 BIP-327](https://github.com/bitcoin/bips/blob/master/bip-0327.mediawiki)
- [Adaptor Signatures](https://github.com/Blockstream/research/raw/master/adaptor_sigs/adaptor_sigs.pdf)
- [ChaCha20-Poly1305 RFC 7539](https://tools.ietf.org/html/rfc7539)

---

*Last Updated: April 2026*
