# SPV Light Mode — Verified Implementation Plan

> **Validated against:** `spv_architecture.md`, `swap_expansion_guide.md`, `swap_spv_master_roadmap.md`
> **Status:** Pre-execution validation complete — 4 critical gaps found, plan corrected below.

---

## Validation Findings (Gaps vs Assumptions)

| # | Gap | Fix |
|---|-----|-----|
| 1 | **SPV constructors don't accept WIF** — `m_wif` is empty in SPV mode, so claim/refund can't sign | Add `const std::string& wif` to SPV constructors (new overload, keep old for backward compat) |
| 2 | **KMD tx-building helpers are static in .cpp** — `buildRawTransaction`, `createClaimScriptSig`, `createRefundScriptSig`, `writeLE64`, `signKmdInput` are all `static` in `KmdRpcClient.cpp`, not in the header | Hoist them into `KmdHtlcScript.h` as public static methods |
| 3 | **Signing helpers (`signBtcInput`/`signBchInput`/`signKmdInput`) are static in RpcClient .cpp** — not accessible from ChainClient | Hoist into the respective `*HtlcScript` class (or into a shared `P2shSighashSigner` utility) |
| 4 | **lockHtlc uses RPC `sendtoaddress`** — not a locally-signed tx. SPV `lock()` needs UTXO discovery + raw tx building + signing + broadcast. Much more complex than claim/refund | Keep `lock()` RPC-only for now. Defer SPV `lock()` to a follow-up (Phase 2b). Users can externally fund the P2WSH/P2SH address from any wallet — then `verifyLock()` and `claim()`/`refund()` work purely via SPV |

### Additional verification
- ✅ State machine: `ADAPTOR_WAITING_SPV` (17) and `ADAPTOR_SECRET_CONFIRMED_SPV` (18) exist in `SwapTypes.h`
- ✅ `handleWaitingSpv()` in `SwapDaemon.cpp` calls `client->claim()` then `client->tryExtractClaimedSecret()` — once claim works in SPV, the state machine handles it
- ✅ All four ChainClient classes have SPV constructors, `verifyLockSpv()`, `extractSecretSpv()`
- ✅ `Secp256k1Signer` + `Bip143Sighash` exist and work with `sighashType=0x01` (BTC/LTC/KMD) and `0x41` (BCH)
- ✅ `ElectrumConnection` handles plaintext TCP (port 50001), no TLS yet
- ✅ BCH's `HtlcScript.h` (reference impl) has all helpers: `buildRawTransaction`, `createClaimScriptSig`, `createRefundScriptSig`, `redeemScriptToP2shScriptPubKey`

---

## Architecture Summary

```
User's `xfg-swapd`
    │
    ├── ElectrumSpvClient ───── 1+ Electrum servers (TCP 50001)
    │   ├── syncHeaders()       ✓ blockchain.headers.subscribe + batch fetch
    │   ├── getTipHeight()      ✓
    │   ├── verifyTxInclusion() ✓ blockchain.transaction.get_merkle
    │   ├── getRawTx()          ✓ blockchain.transaction.get
    │   ├── findSpend()         ✓ blockchain.scripthash.get_history
    │   ├── broadcastTx()       NEW: blockchain.transaction.broadcast
    │
    ├── Local Signing (hoisted from RpcClient.cpp to HtlcScript classes)
    │   ├── Secp256k1Signer     ✓ ECDSA sign, key derivation
    │   ├── Bip143Sighash       ✓ BIP143 sighash (generic; takes sighashType)
    │   ├── BtcHtlcScript        ✓ P2WSH SegWit tx building, witness stacks
    │   ├── KmdHtlcScript        NEW+: buildRawTransaction, createClaimScriptSig, etc.
    │   └── signBtcInput/signKmdInput  NEW: hoisted signing helpers
    │
    └── ISpvClient             ✓ abstraction layer
        └── broadcastTx()       NEW: pure virtual + Electrum impl
```

---

## Phased Implementation Plan

### Phase 1: Prep — Hoist KMD helpers + add signing helpers to HtlcScript

**Rationale:** Before we can implement SPV claim/refund, the transaction-building and signing primitives must be accessible from the ChainClient layer, not buried as `static` functions in `*RpcClient.cpp`.

#### Phase 1a: Hoist KMD TX-building helpers into `KmdHtlcScript.h/.cpp`

| File | Change |
|------|--------|
| `src/SwapDaemon/Komodo/KmdHtlcScript.h` | Add public static methods: `buildRawTransaction()`, `createClaimScriptSig()`, `createRefundScriptSig()`, `signInput()` |
| `src/SwapDaemon/Komodo/KmdHtlcScript.cpp` | Move implementations from `KmdRpcClient.cpp` (lines 470-753). Add `writeLE64()` to private section. |
| `src/SwapDaemon/Komodo/KmdRpcClient.cpp` | Replace static function calls with calls to `KmdHtlcScript::*` equivalents. Remove duplicated function bodies. |

**Methods to add to `KmdHtlcScript.h`:**
```cpp
// Build a raw Bitcoin-style transaction (version 1, one input, one output).
// For P2SH HTLC spending. Handles CLTV nLocktime and nSequence.
static std::vector<uint8_t> buildRawTransaction(
    const std::string& inputTxid, uint32_t inputVout,
    uint64_t inputAmount,      // not used (fee is fixed), kept for API consistency
    const std::vector<uint8_t>& scriptSig,
    const std::string& outputAddress,
    uint64_t outputAmount, uint32_t nLockTime);

// ScriptSig for claiming: <sig> <preimage> OP_TRUE <redeemScript>
static std::vector<uint8_t> createClaimScriptSig(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& preimage,
    const std::vector<uint8_t>& redeemScript);

// ScriptSig for refund: <sig> OP_FALSE <redeemScript>
static std::vector<uint8_t> createRefundScriptSig(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& redeemScript);

// Sign a KMD P2SH input using BIP143 sighash (sighashType=0x01).
// Returns DER-encoded signature with sighash byte appended.
static std::vector<uint8_t> signInput(
    const std::array<uint8_t, 32>& privKey,
    uint32_t txVersion, uint32_t nLocktime, uint32_t nSequence,
    const std::string& htlcTxid, uint32_t htlcVout,
    const std::vector<uint8_t>& redeemScript,
    uint64_t htlcAmount,
    const std::vector<uint8_t>& outputScript, uint64_t outputAmount);
```

#### Phase 1b: Add BTC signing helper to `BtcHtlcScript.h/.cpp`

| File | Change |
|------|--------|
| `src/SwapDaemon/Bitcoin/BtcHtlcScript.h` | Add public static method: `signInput()` |
| `src/SwapDaemon/Bitcoin/BtcHtlcScript.cpp` | Move `signBtcInput()` from `BtcRpcClient.cpp` (lines 568-628). Add `base58CheckDecode` + `wifToPrivKey` helpers. |
| `src/SwapDaemon/Bitcoin/BtcRpcClient.cpp` | Replace `signBtcInput()` calls with `BtcHtlcScript::signInput()` |

```cpp
// BtcHtlcScript.h — new method:
// Sign a P2WSH SegWit input using BIP143 sighash (sighashType=0x01).
// Returns DER-encoded signature with 0x01 sighash byte appended.
static std::vector<uint8_t> signInput(
    const std::array<uint8_t, 32>& privKey,
    uint32_t txVersion, uint32_t nLocktime, uint32_t nSequence,
    const std::string& htlcTxid, uint32_t htlcVout,
    const std::vector<uint8_t>& witnessScript,
    uint64_t htlcAmount,
    const std::vector<uint8_t>& outputScript, uint64_t outputAmount);
```

#### Phase 1c: Add BCH signing helper to `HtlcScript.h` (same pattern)

| File | Change |
|------|--------|
| `src/SwapDaemon/BitcoinCash/HtlcScript.h` | Add public static method: `signInput()` |
| `src/SwapDaemon/BitcoinCash/HtlcScript.cpp` | Move `signBchInput()` from `BchRpcClient.cpp`. Uses `sighashType=0x41`. |
| `src/SwapDaemon/BitcoinCash/BchRpcClient.cpp` | Replace `signBchInput()` calls with `BchHtlcScript::signInput()` |

#### Phase 1d: Add WIF decode helper to each HtlcScript class

Each HtlcScript class needs `wifToPrivKey()` as a public static method so the ChainClient can decode WIF without depending on the RpcClient:
```cpp
// Decode a WIF-encoded private key to 32 raw bytes.
// Verifies the chain-specific version byte.
// BTC: 0x80, LTC: 0xB0, KMD: 0xBC, BCH: 0x80
static bool wifToPrivKey(const std::string& wif, std::array<uint8_t, 32>& privKey);
static bool base58CheckDecode(const std::string& encoded, uint8_t& version,
                               std::vector<uint8_t>& payload);
```

---

### Phase 2: Config + Wiring (read-only SPV for BTC/LTC/KMD)

**What this phase delivers:** BTC/LTC/KMD can `verifyLock()`, `getTransactionDetails()`, `tryExtractClaimedSecret()` via SPV. No full node needed to *verify* a counterparty's lock.

| File | Change |
|------|--------|
| `src/SwapDaemon/SwapDaemon.h` | Add `btcMode`, `btcSpvServers`, `btcSpvMinServers`, `btcSpvCheckpointHeight`, `btcSpvCheckpointHash` with same defaults as BCH. Same for LTC, KMD. |
| `src/SwapDaemon/ChainClientConfig.cpp` | Parse `btc_mode`, `btc_spv_server_N` loop (up to 16, gap-terminated), `btc_spv_min_servers`, `btc_spv_checkpoint_height`, `btc_spv_checkpoint_hash`. Same for LTC, KMD. |
| `src/SwapDaemon/SwapDaemon.cpp` | Wire BTC/LTC/KMD SPV mode in constructor — identical pattern to BCH (lines 171-193). Each creates `ElectrumSpvClient` with its own server list + checkpoint, then `registerChain(swapPair, new ChainClient(spvClient))`. |

**Config example:**
```json
{
  "btc_mode": "spv",
  "btc_spv_server_0": "electrum.blockstream.info:50001",
  "btc_spv_min_servers": 1,
  "btc_wif": "L...",
  "ltc_mode": "spv",
  "ltc_spv_server_0": "electrum-ltc.bysh.me:50001",
  "ltc_spv_min_servers": 1,
  "ltc_wif": "T...",
  "kmd_mode": "spv",
  "kmd_spv_server_0": "electrum1.cipig.net:10001",
  "kmd_spv_min_servers": 1,
  "kmd_wif": "U..."
}
```

---

### Phase 3: Add `broadcastTx()` to ISpvClient + ElectrumSpvClient

| File | Change |
|------|--------|
| `src/SwapDaemon/Spv/ISpvClient.h` | Add `virtual bool broadcastTx(const std::vector<uint8_t>& rawTx, std::string& txid) = 0;` |
| `src/SwapDaemon/Spv/ElectrumSpvClient.h` | Declare `broadcastTx` override. |
| `src/SwapDaemon/Spv/ElectrumSpvClient.cpp` | Implement via `blockchain.transaction.broadcast` (Electrum JSON-RPC). |

```cpp
bool ElectrumSpvClient::broadcastTx(const std::vector<uint8_t>& rawTx, std::string& txid) {
  if (m_conns.empty()) return false;
  std::string hexTx = BtcHtlcScript::bytesToHex(rawTx);
  std::string params = "[\"" + hexTx + "\"]";
  std::string result = m_conns[0]->call("blockchain.transaction.broadcast", params);
  if (result.empty()) return false;
  Common::JsonValue json = Common::JsonValue::fromString(result);
  if (!json.isString()) return false;
  txid = json.getString();
  return txid.size() == 64;
}
```

---

### Phase 4: SPV claim + refund for BTC (BtcChainClient)

| File | Change |
|------|--------|
| `src/SwapDaemon/Bitcoin/BtcChainClient.h` | Add SPV constructor overload: `BtcChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif)`. Stores WIF in `m_wif`. Keep old constructor. |
| `src/SwapDaemon/Bitcoin/BtcChainClient.cpp` | Modify `claim()` and `refund()`: if `m_spvClient && !m_wif.empty()`, sign locally + broadcast via SPV instead of failing. Update `lock()` to check for WIF too (though lock stays RPC-only for now). |

**claim() new SPV path:**
```cpp
// If m_wif is empty, fail early (no signing key available)
if (!m_spvClient || m_wif.empty()) {
  return ChainClientResult::fail("BTC claim: no RPC client and no WIF (cannot sign)");
}
// 1. Decode WIF to privKey
std::array<uint8_t, 32> privKey;
if (!BtcHtlcScript::wifToPrivKey(m_wif, privKey)) return fail(...);
// 2. Build witness script + preimage
auto witnessScript = BtcHtlcScript::hexToBytes(params.chainState);
auto preimage = BtcHtlcScript::hexToBytes(Common::podToHex(params.adaptorSecret));
// 3. Decode destAddress → output script
uint8_t addrVersion; std::vector<uint8_t> pubKeyHash;
if (!BtcHtlcScript::base58CheckDecode(params.ctrAddress, addrVersion, pubKeyHash))
  return fail("BTC claim: invalid dest address");
auto outputScript = BtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);
// 4. Fee: use hardcoded 1000 sats (or query Electrum estimate)
uint64_t fee = 1000;
uint64_t outputAmount = params.ctrAmount - fee;
// 5. Sign with BIP143 sighash (0x01)
auto der = BtcHtlcScript::signInput(privKey, 2, 0, 0xFFFFFFFD,
    params.ctrLockTxId, 0, witnessScript, params.ctrAmount,
    outputScript, outputAmount);
// 6. Build witness stack + raw SegWit tx
auto witnessStack = BtcHtlcScript::createClaimWitness(der, preimage, witnessScript);
std::vector<uint8_t> emptyScriptSig;
auto rawTx = BtcHtlcScript::buildRawSegWitTx(
    params.ctrLockTxId, 0, params.ctrAmount,
    emptyScriptSig, witnessStack, params.ctrAddress, outputAmount, 0);
// 7. Broadcast via SPV
std::string claimTxId;
if (!m_spvClient->broadcastTx(rawTx, claimTxId))
  return fail("BTC claim SPV broadcast failed");
return ChainClientResult::ok(claimTxId);
```

**refund() new SPV path:** Same pattern, uses `createRefundWitness`, nLocktime, nSequence=0xFFFFFFFE.

**lock() stays RPC-only for now** — SPV lock requires UTXO management which is Phase 2b.

---

### Phase 5: SPV claim + refund for LTC (LtcChainClient)

Identical to Phase 4 with chain-specific constants:
- Uses `LtcHtlcScript::wifToPrivKey()` (WIF version 0xB0)
- Uses `BtcHtlcScript::witnessScriptToAddress(redeem, "ltc")` for bech32 HRP
- Uses `2` for tx version
- Uses `BtcHtlcScript::signInput()` with `sighashType=0x01` (reuses BTC's signing helper)
- Uses `BtcHtlcScript::createClaimWitness()` / `BtcHtlcScript::buildRawSegWitTx()` (reuses BTC's tx building)

---

### Phase 6: SPV claim + refund for KMD (KmdChainClient)

| File | Change |
|------|--------|
| `src/SwapDaemon/Komodo/KmdChainClient.h` | Add SPV constructor with WIF overload |
| `src/SwapDaemon/Komodo/KmdChainClient.cpp` | SPV claim/refund using KmdHtlcScript::signInput + buildRawTransaction + broadcastTx |

Different from BTC because KMD uses P2SH + legacy tx format (version 1):
- Uses `KmdHtlcScript::signInput()` with `sighashType=0x01` + `txVersion=1`
- Uses `KmdHtlcScript::createClaimScriptSig()` / `KmdHtlcScript::createRefundScriptSig()`
- Uses `KmdHtlcScript::buildRawTransaction()` (version 1, not SegWit)
- Addresses decoded via `KmdHtlcScript::base58CheckDecode()` (version 0x3C/0x55)

---

### Phase 7: SPV claim + refund for BCH (BchChainClient)

| File | Change |
|------|--------|
| `src/SwapDaemon/BitcoinCash/BchChainClient.h` | Add SPV constructor with WIF overload |
| `src/SwapDaemon/BitcoinCash/BchChainClient.cpp` | SPV claim/refund using `BchHtlcScript::signInput(sighashType=0x41, txVersion=1)` + `BchHtlcScript::createClaimScriptSig()` + `BchHtlcScript::buildRawTransaction()` + `broadcastTx()` |

---

### Phase 8: Wiring — SwapDaemon.cpp SPV constructors with WIF

| File | Change |
|------|--------|
| `src/SwapDaemon/SwapDaemon.cpp` | In the SPV-mode registration blocks (added in Phase 2), pass `chainCfg.btcWif` to the new SPV+WIF constructor: `std::make_unique<BtcChainClient>(spvClient, chainCfg.btcWif)` instead of just `std::make_unique<BtcChainClient>(spvClient)`. Same for LTC, KMD. Also update BCH SPV registration to pass WIF. |

---

## Execution Order

```
Phase 1 (Prep: hoist KMD helpers + signing helpers into HtlcScript classes)
  │   Can parallelize: 4 sub-agents
  │   1a: KMD (KmdHtlcScript.h/.cpp + KmdRpcClient.cpp refactor)
  │   1b: BTC (BtcHtlcScript.h/.cpp + BtcRpcClient.cpp refactor)
  │   1c: BCH (HtlcScript.h/.cpp + BchRpcClient.cpp refactor)
  │   1d: WIF helpers (each HtlcScript class)
  │
  ▼
Phase 2 (Config + Wiring for BTC/LTC/KMD SPV)
  │   3 chain-config agents (BTC, LTC, KMD) + 1 SwapDaemon wiring agent
  │
  ▼
Phase 3 (broadcastTx in ISpvClient + ElectrumSpvClient)
  │   Single agent
  │
  ▼
Phases 4-7 (SPV claim/refund per chain)
  │   Fully parallel: 4 agents (BTC, LTC, KMD, BCH)
  │
  ▼
Phase 8 (Wiring: pass WIF to SPV constructors)
  │   Single agent (SwapDaemon.cpp)
  │
  ▼
Build verification: make SwapDaemonLib && make SwapDaemon
```

---

## Design Observations (potential cleanup, not blocking)

### 1. LtcRpcClient.cpp has duplicate helper code

`LtcRpcClient.cpp` still contains the original file-static `createClaimWitness()`, `createRefundWitness()`, `buildRawSegWitTx()` — these were *copied* into `LtcHtlcScript` as public static methods, *not removed* from the RPC client. Both are used:
- LtcRpcClient.cpp file-static → used by RPC claim/refund path
- LtcHtlcScript public static → used by SPV claim/refund path

**Fix:** Refactor LtcRpcClient.cpp to call `LtcHtlcScript::createClaimWitness()` etc., then delete the file-static duplicates. Same may apply to other chains if helpers were copied rather than hoisted with a redirect.

### 2. No null-check on m_spvClient in SPV claim/refund

The SPV signing path dereferences `m_spvClient->broadcastTx()` unconditionally (after the `m_wif.empty()` gate). If both `m_rpc` and `m_spvClient` are null but a WIF is set, this would crash.

In practice this invariant is enforced by the two mutually exclusive constructors (RPC vs. SPV), but it's fragile — and the same unchecked-dereference pattern exists across all existing SPV methods in the codebase. A defensive `if (!m_spvClient) return fail(...)` before the broadcast call would be cheap insurance.

### 3. Hardcoded 1000-satoshi fee

All SPV claim/refund paths hardcode `fee = 1000`. RPC `refundHtlc` also hardcodes 1000 (no `estimatefee` call there either). RPC `claimHtlc` calls `estimateFeeSatoshis` which starts at 1000 and adapts. There's no fee estimation in SPV mode at all — 1000 is a reasonable floor that works for SegWit P2WSH inputs (around 140 vB → 7 sat/vB at 1000 sats), but a fixed fee will break during high-fee periods.

**Long-term fix:** Query `blockchain.estimatefee` from Electrum during broadcast, or add a `fee_multiplier` config parameter.

### 4. Test MockSpvClient dup in 3 test files

Each chain's test file defines its own `MockSpvClient` class with identical structure but separate state. The `broadcastTx` stub was added to all three manually. These should share a single `MockSpvClient` in a test utility header.

---

## What's NOT in scope (deferred follow-ups)

| Item | Reason |
|------|--------|
| SPV `lock()` (funding HTLC from SPV) | Requires UTXO discovery + coin selection + change outputs. Users can externally fund P2WSH/P2SH address from any wallet. `verifyLock()` then works via SPV. |
| SPV `verifyReserveProof` | Requires Electrum `verifymessage` + `get_balance` — some Electrum servers support this, some don't. Keep RPC-only for now. |
| TLS (port 50002) | Plaintext TCP works on 50001. TLS is hardening, not blocking. |
| Electrum server auto-discovery | Config-supplied server list is sufficient for now. |

---

## Verification

After all phases:
1. Build: `cmake ../.. && make -j$(nproc) SwapDaemonLib && make -j$(nproc) SwapDaemon`
2. Create config with only SPV servers (no RPC hosts):
```json
{
  "btc_mode": "spv",
  "btc_spv_server_0": "electrum.blockstream.info:50001",
  "btc_wif": "L...",
  "bch_mode": "spv", 
  "bch_spv_server_0": "electroncash.org:50002",
  "bch_wif": "K...",
  "xfg_secret_key": "<hex>"
}
```
3. Bob (counterparty) funds the HTLC address externally
4. `xfg-swapd` verifies lock via SPV (`verifyLockSpv`)
5. When secret is revealed, `claim()` builds + signs + broadcasts claim tx via Electrum
6. `tryExtractClaimedSecret()` discovers spending tx via `findSpend()` + parses preimage
7. All without running a single full node.
