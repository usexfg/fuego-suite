# Plan: HEAT Fee Payment Mechanism

## Problem

All transactions require XFG to pay the fee (`MINIMUM_FEE_8KH = 0.0008 XFG`). The per-asset balance check at `Blockchain.cpp:3134-3141` enforces:

```
inAssets.xfg >= outAssets.xfg + xfgFee
inAssets.heat == outAssets.heat
inAssets.lp == outAssets.lp
```

A user who converted all XFG → HEAT (via `swap` or `mint_heat`) is **completely stuck**: they can't send HEAT, they can't convert back, and they can't do anything. Even a `heat_send` requires XFG for the fee.

Additionally, `getTransactionAllInputsAmount` (line 462) sums ALL asset types into a single scalar for `in_amount`, so `fee = in_amount - out_amount` mixes XFG and HEAT amounts. If a user has only HEAT, the fee is computed as `HEAT_in - HEAT_out`, which is correct in magnitude but gets checked against the XFG constraint.

## Design: HEAT Fee Conversion at Pool Rate

**Core idea**: When `inAssets.xfg == 0 && inAssets.heat > 0`, allow the user to pay the XFG fee in HEAT at the Hearth pool exchange rate. The HEAT fee output goes to treasury instead of an XFG fee output.

---

## Changes

### 1. `Blockchain.cpp:3134-3141` — Per-asset balance check (default case)

**Current code**:
```cpp
} else {
  if (inAssets.xfg < outAssets.xfg + xfgFee ||
      inAssets.heat != outAssets.heat ||
      inAssets.lp != outAssets.lp) {
    isTransactionValid = false;
    logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " per-asset balance violation";
  }
}
```

**New code**:
```cpp
} else {
  if (inAssets.xfg >= outAssets.xfg + xfgFee) {
    // Standard: user pays XFG fee
    if (inAssets.heat != outAssets.heat || inAssets.lp != outAssets.lp) {
      isTransactionValid = false;
      logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " per-asset balance violation";
    }
  } else if (inAssets.xfg == 0 && !m_ammPool.isEmpty()
             && m_ammPool.reserveHeat > 0 && m_ammPool.reserveXfg > 0) {
    // HEAT fee: user has no XFG, pay fee in HEAT at pool rate
    FixedPoint64 poolRate = FixedPoint64::fromRatio(
        m_ammPool.reserveXfg, m_ammPool.reserveHeat);
    FixedPoint64 feeFp = FixedPoint64::fromUint64(xfgFee);
    uint64_t heatFee = feeFp.div(poolRate).toUint64();
    if (inAssets.heat >= outAssets.heat + heatFee
        && inAssets.xfg == outAssets.xfg
        && inAssets.lp == outAssets.lp) {
      // Valid: treasury receives HEAT instead of XFG fee
    } else {
      isTransactionValid = false;
      logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id
        << " HEAT fee balance mismatch (heatFee="
        << m_currency.formatAmount(heatFee) << ")";
    }
  } else {
    // Insufficient XFG and can't convert HEAT (empty pool)
    isTransactionValid = false;
    logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id
      << " insufficient XFG for fee and no HEAT pool rate available";
  }
}
```

**Key property**: The `fee` variable is still `in_amount - out_amount` (single scalar). When the user pays with HEAT, `fee` = HEAT_fee at pool rate. The per-asset check now verifies HEAT covers the fee instead of XFG.

---

### 2. `Blockchain.cpp:2944` — Fee computation

**No change needed**. The existing logic:
```cpp
uint64_t fee = in_amount < out_amount ? m_currency.minimumFee(blockData.majorVersion) : in_amount - out_amount;
```

This still works: `in_amount` = total HEAT inputs, `out_amount` = total HEAT outputs + fee. The fee is the correct HEAT amount.

---

### 3. `TransactionPool.cpp:208-263` — Mempool validation

The mempool checks `fee >= minimumFee()`. Since `fee = HEAT_in - HEAT_out` and `minimumFee()` returns `MINIMUM_FEE_8KH` (0.0008 XFG), the fee in HEAT terms might not match. The mempool doesn't have access to the blockchain's AMM pool state.

**Approach**: Skip the fee minimum check for HEAT-only transactions in the mempool, and rely on block validation to enforce the correct fee.

**In `TransactionPool.cpp`**, after line 228 (`const uint64_t fee = inputs_amount - outputs_amount;`), add detection:

```cpp
bool isHeatOnlyTx = false;
// v10+ HEAT-only: fee is paid in HEAT at pool rate, skip XFG minimum check
if (tx.version >= TRANSACTION_VERSION_2) {
  AssetBalance inA = m_currency.getTransactionInputAssetAmounts(tx, height);
  AssetBalance outA = m_currency.getTransactionOutputAssetAmounts(tx);
  if (inA.xfg == 0 && inA.heat > 0) {
    isHeatOnlyTx = true;
  }
}
```

Then modify line 257:
```cpp
if (!isFusionTransaction && !isHeatOnlyTx && fee < m_currency.minimumFee()) {
  // existing rejection
}
```

---

### 4. Transaction building (wallet side)

The wallet (`WalletTransactionSender.cpp`) needs to detect when the user has zero XFG and build the transaction differently:

- **Standard case** (has XFG): fee output goes to treasury as XFG (existing behavior)
- **HEAT fee case** (no XFG): no XFG fee output. Instead, a HEAT commitment output to the treasury's view key is included for the HEAT fee amount. The HEAT fee output uses the same `TransactionOutputCommitment` format with `HEAT_TERM`.

**In `WalletTransactionSender.cpp`**, the fee output logic (`makeTransaction`) needs:

```cpp
if (userHasXfg) {
  // Standard: add XFG fee output to treasury
  output.amount = fee;
  output.target = KeyOutput{treasuryPublicKey};
} else {
  // HEAT fee: add HEAT commitment output to treasury
  // The fee in HEAT is computed at pool rate
  uint64_t heatFee = fee * poolRate.denom / poolRate.num;
  output.amount = heatFee;
  output.target = TransactionOutputCommitment{...}; // HEAT_TERM
}
```

The wallet also needs access to the pool rate (via RPC `get_pool_info` or `heat_metrics` endpoint).

---

### 5. HEAT-specific operations (swap, add_liq, etc.)

For `swap`, `add_liq`, `remove_liq`, `heat_deposit` — these already require the user to have XFG for the fee. With this change, users with only HEAT can also perform these operations by paying the fee in HEAT.

**Important**: The `minimumFee()` return value (`MINIMUM_FEE_8KH = 8000`) is in XFG units. When paying in HEAT, the wallet needs to convert this to HEAT at the pool rate.

---

## Security Considerations

1. **Pool rate manipulation**: If an attacker can manipulate the AMM pool rate, they could pay less HEAT fee. However, the pool rate is set by `reserveXfg / reserveHeat` and is expensive to manipulate (requires large trades).

2. **Mempool validation gap**: The mempool can't easily validate HEAT fees without pool state. This is acceptable — block validation is authoritative, and the mempool is just a cache.

3. **Treasury accounting**: The treasury receives HEAT instead of XFG. This HEAT can be converted back to XFG at pool rate during epoch boundaries, or held as HEAT reserve.

4. **Dust attack**: The HEAT fee output must be above dust threshold. Since `MINIMUM_FEE_8KH = 8000` (0.0008 XFG) and pool rate is ~1:1, the HEAT fee is ~8000 Hnano — well above dust.

---

## Files to Modify

| File | Line(s) | Change |
|------|---------|--------|
| `src/CryptoNoteCore/Blockchain.cpp` | 3134-3141 | Add HEAT fee fallback in per-asset balance check |
| `src/CryptoNoteCore/TransactionPool.cpp` | 228-263 | Skip XFG minimum fee check for HEAT-only txs |
| `src/WalletLegacy/WalletTransactionSender.cpp` | ~makeTransaction | Build HEAT fee output when user has no XFG |
| `src/WalletLegacy/WalletSendTransactionContext.h` | — | Add `heatFee` field to context |
| `src/SimpleWallet/SimpleWallet.cpp` | — | Pass pool rate info to wallet for fee conversion |

---

## Testing

1. User with 0 XFG, 100 HEAT sends 50 HEAT → should succeed with ~8000 HEAT fee
2. User with 0 XFG, 100 HEAT does `swap` → should succeed
3. User with 0 XFG, 100 HEAT does `mint_heat` → should succeed (HEAT in, HEAT out + fee)
4. Pool rate edge case: empty pool → should reject (no rate available)
5. Pool rate extreme: 1:1000 → fee should be 8M HEAT (expensive but valid)
6. Standard user with XFG → existing behavior unchanged
7. Mempool: HEAT-only tx should pass mempool check
8. Block validation: HEAT fee at pool rate is correctly validated

---

## Implementation Order

1. **Blockchain.cpp** — per-asset balance check (core consensus change)
2. **TransactionPool.cpp** — mempool relaxation
3. **WalletTransactionSender.cpp** — tx building with HEAT fee output
4. **SimpleWallet.cpp** — pool rate pass-through for fee conversion
5. **Test all paths on testnet**
