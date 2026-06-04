# ARB Pricing & Units Fix

> **For agentic workers:** Steps use checkbox (`- [ ]`) syntax. Small, independent bug fix — single PR.

**Goal:** Fix two latent ARB bugs in `PriceOracle` and de-duplicate the decimals switch in `handleSwapRequest` so ARB swaps price and denominate correctly.

**Architecture:** Three small edits in two files. Pure bug fix. No new files, no dependencies on the Base/AFK/security plans.

**Tech Stack:** C++17, standalone `int main()` test (per `src/SwapDaemon/tests/test_adaptor_roundtrip.cpp`).

---

## Context

`SwapPair::ARB = 4` was added but pricing/units paths still treat it as "unknown":

| Site | Current behavior | Correct behavior |
|------|------------------|------------------|
| `PriceOracle::getSeedRate(ARB)` [PriceOracle.cpp:62-71](src/SwapDaemon/PriceOracle.cpp:62) | `default: return 0.0` — no seed price | Return the ETH seed rate (~214,000 XFG/coin). ARB is ETH on an L2. |
| `PriceOracle::ctrDivisor(ARB)` [PriceOracle.cpp:77-85](src/SwapDaemon/PriceOracle.cpp:77) | `default: 1e8` — satoshi units | Return `1e18` (wei). |
| `handleSwapRequest` inline switch [SwapDaemon.cpp:1523-1529](src/SwapDaemon/SwapDaemon.cpp:1523) | `default: *= 1e8` — duplicates the same bug | Call the oracle helper. |

Net effect today: an ARB offer either has no validatable rate (TWAP fallback returns 0) or is computed off by `1e18 / 1e8 = 10^10` when checking reserve-proof amounts. ARB swaps cannot work correctly until these are fixed.

`SwapOfferRelay::getSeedRate(uint8_t)` already returns `214000.0` for ARB ([SwapOfferRelay.cpp:42](src/CryptoNoteCore/SwapOfferRelay.cpp:42)) — the split-brain with `PriceOracle::getSeedRate` is why this didn't already crash. A follow-up should DRY these two impls; out of scope for this fix.

---

## Task 1: Fix `PriceOracle` seed rate and divisor for ARB

**Files:**
- Modify: `src/SwapDaemon/PriceOracle.cpp`
- Test: `src/SwapDaemon/tests/test_price_oracle_arb.cpp` (new)

- [ ] **Step 1: Write the failing test**

```cpp
// test_price_oracle_arb.cpp
#include "SwapDaemon/PriceOracle.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace XfgSwap;

int main() {
  // ARB is an ETH-priced L2 — seed rate should match ETH (~214k).
  double arb = PriceOracle::getSeedRate(SwapPair::ARB);
  double eth = PriceOracle::getSeedRate(SwapPair::ETH);
  assert(arb > 100000.0 && "ARB seed rate must be non-trivial");
  assert(std::fabs(arb - eth) < 1.0 && "ARB seed rate must equal ETH");

  // ARB CTR amounts are wei (1e18), not satoshi (1e8).
  assert(PriceOracle::ctrDivisor(SwapPair::ARB) == 1e18);

  // Regression: existing pairs unchanged.
  assert(PriceOracle::ctrDivisor(SwapPair::SOL) == 1e9);
  assert(PriceOracle::ctrDivisor(SwapPair::ETH) == 1e18);
  assert(PriceOracle::ctrDivisor(SwapPair::XMR) == 1e12);
  assert(PriceOracle::ctrDivisor(SwapPair::BCH) == 1e8);

  std::cout << "test_price_oracle_arb PASS\n";
  return 0;
}
```

- [ ] **Step 2: Run to verify it fails** — build the swap tests target, run the new exe. Expect FAIL (ARB seed rate is 0; ctrDivisor returns 1e8 default).

- [ ] **Step 3: Add the ARB cases**

In `PriceOracle.cpp`, edit `getSeedRate` ([:64-70](src/SwapDaemon/PriceOracle.cpp:64)) — add **before** the `default`:
```cpp
    case SwapPair::ARB: return SEED_ETH_USD / SEED_XFG_USD;  // ARB = ETH on L2
```

Edit `ctrDivisor` ([:78-84](src/SwapDaemon/PriceOracle.cpp:78)) — add **before** the `default`:
```cpp
    case SwapPair::ARB: return 1e18;  // wei (EVM L2)
```

- [ ] **Step 4: Run to verify it passes** — rebuild and run; expect PASS.

- [ ] **Step 5: Commit**

```bash
git add src/SwapDaemon/PriceOracle.cpp src/SwapDaemon/tests/test_price_oracle_arb.cpp
git commit -m "fix(swap): price ARB as an ETH-equivalent L2 (seed rate + wei divisor)"
```

---

## Task 2: De-duplicate the decimals switch in handleSwapRequest

**Files:**
- Modify: `src/SwapDaemon/SwapDaemon.cpp:1523-1529`

The inline switch silently miscomputes ARB amounts (and any future EVM L2) — and duplicates logic that `PriceOracle::ctrDivisor` already owns. After Task 1, the oracle helper is correct; route through it.

- [ ] **Step 1: Replace the inline switch**

Change:
```cpp
    switch (pair) {
      case SwapPair::SOL: ctrWhole *= 1e9;  break;
      case SwapPair::ETH: ctrWhole *= 1e18; break;
      case SwapPair::XMR: ctrWhole *= 1e12; break;
      case SwapPair::BCH: ctrWhole *= 1e8;  break;
      default:            ctrWhole *= 1e8;  break;
    }
```
to:
```cpp
    ctrWhole *= PriceOracle::ctrDivisor(pair);
```

- [ ] **Step 2: Build the daemon**

Run the project's swapd build target. Expect compiles clean (`PriceOracle.h` is already included via `SwapDaemon.h`).

- [ ] **Step 3: Manual sanity check (optional, log-level)**

Trace a synthetic ARB offer through `handleSwapRequest`: a 1 XFG fill at 214,000 XFG/ETH should produce `requiredCtrAmount ≈ 1/214000 * 1e18 ≈ 4.67e12 wei`, not `4.67e2`.

- [ ] **Step 4: Commit**

```bash
git add src/SwapDaemon/SwapDaemon.cpp
git commit -m "fix(swap): route handleSwapRequest decimals through PriceOracle::ctrDivisor"
```

---

## Verification

1. New test `test_price_oracle_arb` passes; existing tests unchanged.
2. Daemon builds clean.
3. `getSeedRate(ARB) ≈ 214000`, not 0; `ctrDivisor(ARB) == 1e18`, not 1e8.
4. ARB amounts through `handleSwapRequest` use wei.

## Follow-ups (out of scope, noted for later)

- **DRY the seed-rate split-brain:** `SwapOfferRelay::getSeedRate(uint8_t)` ([SwapOfferRelay.cpp:37](src/CryptoNoteCore/SwapOfferRelay.cpp:37)) duplicates `PriceOracle::getSeedRate(SwapPair)`. Make one the single source of truth.
- **Pair-iteration bounds** capping at 4 (`SwapOfferRelay.cpp:120,522`; `SwapDaemon.cpp:1475`) still don't matter for ARB but will for BASE; the Base plan handles them.
