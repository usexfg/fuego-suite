# Base L2 Integration Plan for Fuego Atomic Swaps (corrected)

Base is an EVM-compatible L2, so the daemon reuses `EthChainClient` exactly as the existing `ARB` (Arbitrum) pair does. The premise is sound — but a correct integration is **more than an enum + config**. This revision fixes the issues in the first draft and also completes the **ARB wiring**, which is currently half-finished and carries two latent bugs that Base would otherwise inherit.

## What the original draft got wrong

1. **Registration code wouldn't compile.** `EthChainClient`'s ctor takes a `std::unique_ptr<EthRpcClient>`, not a URL string ([EthChainClient.h:12](src/SwapDaemon/Ethereum/EthChainClient.h:12)). You must build an `EthRpcClient(host, port, privKey, address, chainId, txType)` first, as ETH/ARB do ([SwapDaemon.cpp:122-151](src/SwapDaemon/SwapDaemon.cpp:122)).
2. **chainId 8453 was never wired to the signer.** chainId flows through `EthRpcClient`, not `EthChainClient`. Without it, Base txs are signed with the wrong EIP-155 id → invalid on Base, replayable elsewhere.
3. **No signing key.** The draft omitted `basePrivKeyHex`; the registration passed none → read-only client, can't sign HTLC txs.
4. **Missing `EthTxType::Eip1559`.** ARB passes it ([SwapDaemon.cpp:143](src/SwapDaemon/SwapDaemon.cpp:143)); Base is also 1559. Legacy tx type will be rejected.
5. **HTTPS deployment example won't work.** `EthRpcClient` speaks **plaintext HTTP over a raw socket** (no TLS — [EthRpcClient.cpp connectSocket](src/SwapDaemon/Ethereum/EthRpcClient.cpp)). `mainnet.base.org:443` cannot be reached directly.

## Shared bugs the draft missed (affect ARB **today**, and BASE)

| Site | Current | Problem |
|------|---------|---------|
| `PriceOracle::getSeedRate` [PriceOracle.cpp:69](src/SwapDaemon/PriceOracle.cpp:69) | ARB → `default: return 0.0` | ARB has **no seed price**; pricing/validation dead until 5 real swaps (impossible w/o a rate). |
| `PriceOracle::ctrDivisor` [PriceOracle.cpp:83](src/SwapDaemon/PriceOracle.cpp:83) | ARB → `default: 1e8` | EVM uses **1e18 wei**; ARB amounts off by 10^10. |
| `handleSwapRequest` decimals [SwapDaemon.cpp:1484](src/SwapDaemon/SwapDaemon.cpp:1484) | ARB → `default: *= 1e8` | Same 10^10 error (duplicated switch). |
| Pair loops [SwapOfferRelay.cpp:120](src/CryptoNoteCore/SwapOfferRelay.cpp:120), [:522](src/CryptoNoteCore/SwapOfferRelay.cpp:522), [SwapDaemon.cpp:1475](src/SwapDaemon/SwapDaemon.cpp:1475) | cap at pair 4 | BASE=5 offers never found/iterated. |
| Go `pairs.go` | no `PairARB` at all | ARB invisible in the TUI; never wired. |

---

## Phase 0: Shared enum / type layer

### 0.1 Add `BASE` to the enum
- **File:** `src/SwapDaemon/SwapTypes.h:78`
- Add `BASE = 5` after `ARB = 4`.

### 0.2 String conversions (incl. the 4-char `"BASE"` gotcha)
- **File:** `src/SwapDaemon/SwapTypes.cpp`
- `swapPairToString` ([:46](src/SwapDaemon/SwapTypes.cpp:46)): add `case SwapPair::BASE: return "BASE";`
- `swapPairFromString` ([:28](src/SwapDaemon/SwapTypes.cpp:28)) currently hard-rejects anything not 3 chars (`if (s.size() != 3) return false;`). `"BASE"` is 4 chars and would be rejected. Replace the fixed-length logic with a case-insensitive symbol compare:
  ```cpp
  bool swapPairFromString(const std::string& s, SwapPair& out) {
    std::string u; u.reserve(s.size());
    for (char c : s) u.push_back(static_cast<char>(::toupper(c)));
    if (u == "SOL")  { out = SwapPair::SOL;  return true; }
    if (u == "ETH")  { out = SwapPair::ETH;  return true; }
    if (u == "XMR")  { out = SwapPair::XMR;  return true; }
    if (u == "BCH")  { out = SwapPair::BCH;  return true; }
    if (u == "ARB")  { out = SwapPair::ARB;  return true; }
    if (u == "BASE") { out = SwapPair::BASE; return true; }
    return false;
  }
  ```

---

## Phase 1: Pricing & units (fixes ARB + adds BASE)

ARB and BASE are both **ETH on an L2** — the counterparty asset is ETH, priced like ETH, denominated in wei.

### 1.1 Seed rate
- **File:** `src/SwapDaemon/PriceOracle.cpp:64-70`
- Add both pairs to `getSeedRate`:
  ```cpp
  case SwapPair::ARB:  return SEED_ETH_USD / SEED_XFG_USD;  // ETH-priced L2
  case SwapPair::BASE: return SEED_ETH_USD / SEED_XFG_USD;  // ETH-priced L2
  ```

### 1.2 CTR divisor (wei)
- **File:** `src/SwapDaemon/PriceOracle.cpp:78-84`
- Add:
  ```cpp
  case SwapPair::ARB:  return 1e18;  // wei
  case SwapPair::BASE: return 1e18;  // wei
  ```

### 1.3 De-duplicate the decimals switch in handleSwapRequest
- **File:** `src/SwapDaemon/SwapDaemon.cpp:~1484`
- Replace the inline `switch(pair){…default:1e8}` with a call to the now-correct oracle helper:
  ```cpp
  ctrWhole *= m_oracle.ctrDivisor(pair);
  ```
  (Removes the duplicate and fixes ARB/BASE in one place.)

---

## Phase 2: Pair-iteration bounds

BASE=5 must be inside every pair loop:
- `src/CryptoNoteCore/SwapOfferRelay.cpp:120` — `pair < 5` → `pair < 6`
- `src/CryptoNoteCore/SwapOfferRelay.cpp:522` — `p <= 4` → `p <= 5`
- `src/SwapDaemon/SwapDaemon.cpp:1475` — `pair <= 4` → `pair <= 5`

(Consider a `SWAP_PAIR_COUNT` constant in `SwapTypes.h` to avoid future magic numbers.)

---

## Phase 3: Config + registration (C++)

### 3.1 Config fields
- **File:** `src/SwapDaemon/SwapDaemon.h` (after the ARB block, ~line 90)
  ```cpp
  // BASE (Base L2 — EVM, EIP-1559)
  std::string baseHost;
  uint16_t    basePort     = 8545;
  std::string basePrivKeyHex;     // signer key (falls back to ETH key on parse)
  std::string baseAddress;
  uint64_t    baseChainId  = 8453;  // Base mainnet
  std::string baseHtlcBinPath;
  ```

### 3.2 JSON parsing (with ETH-key reuse fallback)
- **File:** `src/SwapDaemon/ChainClientConfig.cpp` (after the ARB parse block; `out.ethPrivKeyHex`/`out.ethAddress` already parsed at [:116](src/SwapDaemon/ChainClientConfig.cpp:116))
  ```cpp
  out.baseHost       = jsonGetStr (json, "base_rpc_host", "");
  out.basePort       = (uint16_t)jsonGetUint(json, "base_rpc_port", 8545);
  out.baseAddress    = jsonGetStr (json, "base_address",  out.ethAddress);
  out.basePrivKeyHex = jsonGetStr (json, "base_priv_key", out.ethPrivKeyHex);
  out.baseChainId    = jsonGetUint(json, "base_chain_id", 8453);
  out.baseHtlcBinPath= jsonGetStr (json, "base_htlc_bin", out.ethHtlcBinPath);
  ```
  This honors "reuse the Ethereum operator key": if `base_priv_key`/`base_address` are absent, the ETH ones are used — **but still with `baseChainId=8453`** so signatures are valid on Base.

### 3.3 Register the client (correct ARB-style pattern)
- **File:** `src/SwapDaemon/SwapDaemon.cpp` (after the ARB block, ~line 152)
  ```cpp
  if (!chainCfg.baseHost.empty()) {
    std::unique_ptr<EthRpcClient> rpc;
    if (!chainCfg.basePrivKeyHex.empty() && !chainCfg.baseAddress.empty()) {
      rpc = std::make_unique<EthRpcClient>(
          chainCfg.baseHost, chainCfg.basePort,
          chainCfg.basePrivKeyHex, chainCfg.baseAddress, chainCfg.baseChainId,
          EthTxType::Eip1559);            // Base is EIP-1559
    } else {
      rpc = std::make_unique<EthRpcClient>(chainCfg.baseHost, chainCfg.basePort);
    }
    m_chainRegistry.registerChain(SwapPair::BASE,
        std::make_unique<EthChainClient>(std::move(rpc), chainCfg.baseAddress, "BASE"));
    m_logger(Logging::INFO) << "BASE chain client registered: "
      << chainCfg.baseHost << ":" << chainCfg.basePort
      << " (chainId=" << chainCfg.baseChainId << ")";
  }
  ```

---

## Phase 4: swapxfg TUI (Go) — wire **ARB and BASE**

ARB is absent from the Go side entirely, so add both for parity.

### 4.1 `swapxfg/app/pairs.go`
- Const block ([:5-11](swapxfg/app/pairs.go:5)): add
  ```go
  PairARB  uint8 = 4
  PairBASE uint8 = 5
  ```
- `ActivePairs` ([:14](swapxfg/app/pairs.go:14)): `{PairSOL, PairETH, PairXMR, PairBCH, PairARB, PairBASE}`
- `PairName`: `PairARB → "ARB/XFG"`, `PairBASE → "BASE/XFG"`
- `PairLabelLong`: `PairARB → "Arbitrum L2"`, `PairBASE → "Base L2"`
- `PairShort`: `PairARB → "ARB"`, `PairBASE → "BASE"`
- `PairFromString`: `"arb","ARB" → PairARB`; `"base","BASE" → PairBASE`
- `HotkeyPair`: map `'4' → PairARB`, `'5' → PairBASE` (both free today)

### 4.2 Address validation `swapxfg/app/validate.go:24`
- Both are EVM `0x…` addresses; extend the dispatch:
  ```go
  case "eth", "evm", "arb", "base":
      return validateETHAddress(addr)
  ```
  Ensure the pair→chain string passed to `validateAddress` for ARB/BASE is `"arb"`/`"base"` (or route through the existing `"evm"` case).

---

## Phase 5: Setup & deployment

### 5.1 RPC transport reality (important)
`EthRpcClient` uses a **plaintext HTTP socket — no TLS**. You cannot point it at a public `https://…:443` endpoint. Operators must either:
- run a local Base node (e.g. `op-geth`) exposing plaintext RPC on `127.0.0.1:8545`, **or**
- front a public HTTPS RPC with a local TLS-terminating proxy (stunnel/nginx) and point the daemon at the local plaintext port.

(If first-class HTTPS is desired, that's a separate change to `EthRpcClient` — out of scope here, and it affects ETH/ARB equally.)

### 5.2 `fuego_swapd.json`
```json
"base_rpc_host": "127.0.0.1",
"base_rpc_port": 8545,
"base_chain_id": 8453,
"base_address":  "0xYourOperatorAddress",
"base_priv_key": "<64-hex>"
```
Omit `base_address`/`base_priv_key` to reuse the ETH operator key (chainId still 8453).

---

## Verification

1. **Build:** daemon compiles with the new registration (type-correct `EthRpcClient`→`EthChainClient`).
2. **Pricing/units:** `getSeedRate(BASE)` and `getSeedRate(ARB)` return ≈214,000 (not 0); `ctrDivisor` returns 1e18 for both; a 1-ETH-equivalent BASE offer computes wei correctly (not 10^10 off).
3. **Offer flow:** post a BASE soft order; confirm it is found by `handleSwapRequest` and listed by `SwapOfferRelay` (loop bounds include pair 5).
4. **Signing:** a Base HTLC tx is signed with chainId 8453, EIP-1559, and broadcasts to a local Base node.
5. **TUI:** ARB and BASE appear in `ActivePairs`, hotkeys `4`/`5` switch to them, and `0x` address validation applies.
6. **Regression:** SOL/ETH/XMR/BCH unaffected; ARB now prices and denominates correctly.

## Scope note
This plan touches only swap wiring and pricing. It does **not** depend on, or conflict with, the in-flight security fixes (`docs/superpowers/plans/2026-05-31-atomic-swap-security-fixes.md`) or the AFK plan — but the reserve-proof verification gap there applies to BASE/ARB too, since both use `EthChainClient` (ETH ecrecover path).
