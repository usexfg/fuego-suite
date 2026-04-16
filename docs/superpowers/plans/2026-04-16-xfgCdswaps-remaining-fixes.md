# xfgCdswaps Remaining Fixes Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the open intentional stubs and minor follow-ups that the 66-fix `fix/xfgCdswaps-criticals` review left behind, so the atomic-swap subsystem can complete real on-chain transactions on every supported chain.

**Architecture:** The active swap stack is daemon-side C++ in `src/SwapDaemon/`, with chain adapters under `Ethereum/`, `BitcoinCash/`, `Monero/`, and `Solana/`. The TUI lives in Go under `swapxfg/` and talks to the daemon over JSON-RPC. The current branch (`fix/xfgCdswaps-criticals`) replaced 56 critical/important defects but deliberately left:
1. ETH transaction signing (no `secp256k1` linkage),
2. BCH HTLC signing (no BIP143 implementation),
3. XMR `claimAdaptor` (Alice+Bob spend-key combine),
4. Six TUI goroutines awaiting `tea.Cmd` conversion,
5. Several minor hardening items.

This plan finishes them in seven phases ordered by build-time dependency: crypto libraries first, then per-chain transaction paths, then config wiring, then hardening, then end-to-end testnet validation.

**Tech Stack:** C++17, CMake, libsecp256k1 (to be added), OpenSSL (already linked), JSON-RPC over HTTP, Go 1.24+ (Bubble Tea TUI), Monero RPC, Ethereum JSON-RPC, BCH JSON-RPC, Solana JSON-RPC.

---

## File Structure

### New files
- `external/secp256k1/` — submodule or vendored copy of bitcoin-core/secp256k1 (recommended over OpenSSL EC for deterministic ECDSA + Schnorr).
- `cmake/Findsecp256k1.cmake` — locate vendored or system library.
- `src/SwapDaemon/Crypto/Secp256k1Signer.{h,cpp}` — thin C++ wrapper over the C API (sign, verify, pubkey-from-priv, recid).
- `src/SwapDaemon/Crypto/RlpEncoder.{h,cpp}` — minimal RLP encode for EIP-155 transaction bodies.
- `src/SwapDaemon/Crypto/Bip143Sighash.{h,cpp}` — BCH BIP143 sighash for P2SH HTLC inputs.
- `src/SwapDaemon/ChainClientConfig.{h,cpp}` — per-chain config struct with private keys and addresses; loaded from `swap_config.json`.
- `tests/SwapDaemon/Secp256k1SignerTests.cpp`
- `tests/SwapDaemon/RlpEncoderTests.cpp`
- `tests/SwapDaemon/Bip143SighashTests.cpp`
- `tests/SwapDaemon/EthSignTxTests.cpp`
- `tests/SwapDaemon/BchHtlcSignTests.cpp`
- `tests/SwapDaemon/ChainClientConfigTests.cpp`
- `swapxfg/app/tui_cmds_test.go` — covers converted goroutines.
- `docs/runbooks/testnet-swap-walkthrough.md` — Phase G handoff.

### Modified files
- `src/SwapDaemon/CMakeLists.txt` — link `secp256k1`, add new files.
- `src/SwapDaemon/Ethereum/EthRpcClient.{h,cpp}` — replace 5 `throw` stubs at `EthRpcClient.cpp:335,345,374,403,412` with real EIP-155 sign + send.
- `src/SwapDaemon/BitcoinCash/BchRpcClient.{h,cpp}` — replace stubs at `BchRpcClient.cpp:373,383,396,408` and persist `redeemScriptHex`.
- `src/SwapDaemon/Monero/MoneroRpcClient.cpp:444` — implement `claimAdaptor` spend-key combine.
- `src/SwapDaemon/SwapDaemon.cpp:581,927` — pass real `redeemScriptHex` from `SwapParams`.
- `src/SwapDaemon/SwapStateMachine.cpp` — add `FAILED` transition from ETH refund failure path.
- `src/SwapDaemon/main.cpp` — switch construction to `ChainClientConfig`-aware constructor.
- `src/CryptoNoteCore/TransactionExtra.cpp:1321` — replace dummy-secret heuristic.
- `swapxfg/app/tui.go:499,523,565,590,608,636` — convert remaining 6 goroutines to `tea.Cmd`.
- `swapxfg/app/bridge_eth.go:24` and `swapxfg/app/bridge_sol.go:20` — verify or regenerate SRI hashes against pinned versions.
- `swapxfg/app/bridge.go` — add base58 checksum check for XMR addresses.

### Files this plan does NOT touch
- `src/SwapDaemon/pool_v11/*` — deferred to v11 by user direction.
- `src/CryptoNoteCore/AliasIndex.*` — already closed in `aafb4eb3`.
- Any `Blockchain.cpp` consensus rules — out of scope; the open items are all daemon/wallet adapters and crypto plumbing.

---

## Chunk 1: Phase A — Cryptographic library integration

### Task A1: Vendor `secp256k1` and wire it into the SwapDaemon build

**Files:**
- Create: `external/secp256k1/` (git submodule of `https://github.com/bitcoin-core/secp256k1.git` pinned to a tagged release)
- Create: `cmake/Findsecp256k1.cmake`
- Modify: `src/SwapDaemon/CMakeLists.txt`
- Modify: `CMakeLists.txt` (top-level — add `add_subdirectory(external/secp256k1)` guarded by an option)

- [ ] **Step 1: Add the submodule**

```bash
cd /Users/aejt/fuego
git submodule add https://github.com/bitcoin-core/secp256k1.git external/secp256k1
cd external/secp256k1 && git checkout v0.5.0 && cd -
```

- [ ] **Step 2: Write `cmake/Findsecp256k1.cmake`**

```cmake
# Locates secp256k1 — prefers vendored build, falls back to system pkg-config.
if(TARGET secp256k1)
  set(secp256k1_FOUND TRUE)
  return()
endif()
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SECP256K1 IMPORTED_TARGET libsecp256k1)
  if(SECP256K1_FOUND)
    add_library(secp256k1 ALIAS PkgConfig::SECP256K1)
    set(secp256k1_FOUND TRUE)
  endif()
endif()
```

- [ ] **Step 3: Add `option(USE_VENDORED_SECP256K1 "Build secp256k1 from external/" ON)` to top-level CMake and `add_subdirectory(external/secp256k1)` when ON. Configure secp256k1 with `SECP256K1_BUILD_BENCHMARK=OFF`, `SECP256K1_BUILD_TESTS=OFF`, `SECP256K1_ENABLE_MODULE_RECOVERY=ON`, `SECP256K1_ENABLE_MODULE_SCHNORRSIG=ON`.**

- [ ] **Step 4: In `src/SwapDaemon/CMakeLists.txt`, add `target_link_libraries(SwapDaemon PRIVATE secp256k1)`.**

- [ ] **Step 5: Run a clean configure + build and confirm `secp256k1` shows up in link line for SwapDaemon.**

```bash
cd /Users/aejt/fuego && rm -rf build && cmake -S . -B build && cmake --build build --target SwapDaemon -j 4 2>&1 | tail -40
```

Expected: build succeeds; no "secp256k1 not found" errors.

- [ ] **Step 6: Commit**

```bash
git add external/secp256k1 .gitmodules cmake/Findsecp256k1.cmake CMakeLists.txt src/SwapDaemon/CMakeLists.txt
git commit -m "build(swap): vendor secp256k1 and link into SwapDaemon"
```

---

### Task A2: `Secp256k1Signer` C++ wrapper

**Files:**
- Create: `src/SwapDaemon/Crypto/Secp256k1Signer.h`
- Create: `src/SwapDaemon/Crypto/Secp256k1Signer.cpp`
- Test: `tests/SwapDaemon/Secp256k1SignerTests.cpp`
- Modify: `src/SwapDaemon/CMakeLists.txt` (add new sources + test executable)

- [ ] **Step 1: Write failing test for `signRecoverable` against a known BIP-32 test vector**

`tests/SwapDaemon/Secp256k1SignerTests.cpp`:
```cpp
#include "SwapDaemon/Crypto/Secp256k1Signer.h"
#include <gtest/gtest.h>

TEST(Secp256k1Signer, SignRecoverableMatchesKnownVector) {
  // Test vector from go-ethereum/crypto/secp256k1: privkey, msgHash, expected r||s||v.
  std::array<uint8_t, 32> priv = {/* 32-byte test key */};
  std::array<uint8_t, 32> msgHash = {/* 32-byte digest */};
  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto sig = signer.signRecoverable(msgHash, priv);
  ASSERT_EQ(sig.r.size(), 32u);
  ASSERT_EQ(sig.s.size(), 32u);
  EXPECT_TRUE(sig.recid == 0 || sig.recid == 1);
  // Verify s is in low-S form (BIP-62).
  EXPECT_LT(sig.s[0], 0x80);
}

TEST(Secp256k1Signer, PubkeyFromPrivkeyDeterministic) {
  std::array<uint8_t, 32> priv = {/* test key */};
  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto p1 = signer.derivePublicKey(priv);
  auto p2 = signer.derivePublicKey(priv);
  EXPECT_EQ(p1, p2);
  EXPECT_EQ(p1.size(), 65u);  // uncompressed
  EXPECT_EQ(p1[0], 0x04);
}
```

- [ ] **Step 2: Run the test to verify it fails to compile**

```bash
cd build && cmake --build . --target Secp256k1SignerTests 2>&1 | tail -10
```

Expected: compile error — header missing.

- [ ] **Step 3: Implement `Secp256k1Signer`**

`src/SwapDaemon/Crypto/Secp256k1Signer.h`:
```cpp
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace CryptoNote::SwapDaemon::Crypto {

struct RecoverableSignature {
  std::array<uint8_t, 32> r;
  std::array<uint8_t, 32> s;
  uint8_t recid;  // 0 or 1
};

class Secp256k1Signer {
 public:
  Secp256k1Signer();
  ~Secp256k1Signer();
  RecoverableSignature signRecoverable(const std::array<uint8_t, 32>& msgHash,
                                       const std::array<uint8_t, 32>& privKey);
  // Returns 65-byte uncompressed pubkey (0x04 || X || Y).
  std::vector<uint8_t> derivePublicKey(const std::array<uint8_t, 32>& privKey);
  // Returns 33-byte compressed pubkey (0x02/0x03 || X). Required for BCH P2PKH.
  std::vector<uint8_t> derivePublicKeyCompressed(const std::array<uint8_t, 32>& privKey);

 private:
  void* m_ctx;  // secp256k1_context*
};

}  // namespace
```

`src/SwapDaemon/Crypto/Secp256k1Signer.cpp`: wrap `secp256k1_ecdsa_sign_recoverable`, `secp256k1_ec_pubkey_create`, normalize to low-S via `secp256k1_ecdsa_signature_normalize`. Use `SECP256K1_CONTEXT_SIGN` only.

- [ ] **Step 4: Run the test until it passes**

```bash
cd build && cmake --build . --target Secp256k1SignerTests && ./tests/SwapDaemon/Secp256k1SignerTests
```

Expected: 2 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/SwapDaemon/Crypto/Secp256k1Signer.{h,cpp} tests/SwapDaemon/Secp256k1SignerTests.cpp src/SwapDaemon/CMakeLists.txt
git commit -m "feat(swap): add Secp256k1Signer wrapper for ETH/BCH signing"
```

---

## Chunk 2: Phase B — ETH transaction path completion

### Task B1: `RlpEncoder` for EIP-155 bodies

**Files:**
- Create: `src/SwapDaemon/Crypto/RlpEncoder.{h,cpp}`
- Test: `tests/SwapDaemon/RlpEncoderTests.cpp`

- [ ] **Step 1: Write failing test from EIP-155 reference vector**

```cpp
TEST(RlpEncoder, EncodeEip155Vector) {
  // Vector: nonce=9, gasPrice=20gwei, gasLimit=21000, to=0x3535...3535,
  // value=10**18, data=, chainId=1
  // Expected pre-sign bytes (hex): "ec098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a764000080018080"
  using namespace CryptoNote::SwapDaemon::Crypto;
  RlpEncoder enc;
  enc.beginList();
  enc.writeUint(9);
  enc.writeUint(20000000000ull);
  enc.writeUint(21000);
  enc.writeBytes(fromHex("3535353535353535353535353535353535353535"));
  enc.writeUint(1000000000000000000ull);
  enc.writeBytes({});  // empty data
  enc.writeUint(1);    // chainId
  enc.writeBytes({});  // r=0 placeholder
  enc.writeBytes({});  // s=0 placeholder
  enc.endList();
  EXPECT_EQ(toHex(enc.finalize()),
            "ec098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a764000080018080");
}
```

- [ ] **Step 2: Run to verify failure**

- [ ] **Step 3: Implement minimal RLP encoder** — only `writeUint` (big-endian, no leading zeros, special-case 0 → empty), `writeBytes` (single-byte < 0x80 special-case), and list framing. Reject negative or oversized lengths.

- [ ] **Step 4: Run test to verify pass**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(swap): minimal RLP encoder for EIP-155 transactions"
```

---

### Task B2: `EthRpcClient::signAndSendRawTransaction`

**Files:**
- Modify: `src/SwapDaemon/Ethereum/EthRpcClient.h:60,83,90` (remove "throws" comments, declare new signing method)
- Modify: `src/SwapDaemon/Ethereum/EthRpcClient.cpp:335,345,374,403,412` (replace stubs)
- Test: `tests/SwapDaemon/EthSignTxTests.cpp`

- [ ] **Step 1: Failing test — round-trip sign-then-recover**

```cpp
TEST(EthSignTx, SignedTxRecoversToSenderAddress) {
  // Use vendored secp256k1 + RlpEncoder. Build a tx, sign, recover sender, compare to derived address.
  auto priv = fromHex("4646464646464646464646464646464646464646464646464646464646464646");
  EthRawTx tx{.nonce=9, .gasPrice=20000000000, .gasLimit=21000,
              .to=fromHex("3535353535353535353535353535353535353535"),
              .value=1000000000000000000ull, .data={}, .chainId=1};
  auto raw = EthRpcClient::signTransaction(tx, priv);
  // Expected raw hex from EIP-155 spec.
  EXPECT_EQ(toHex(raw),
    "f86c098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a76400008025"
    "a028ef61340bd939bc2195fe537567866003e1a15d3c71ff63e1590620aa636276"
    "a067cbe9d8997f761aecb703304b3800ccf555c9f3dc64214b297fb1966a3b6d83");
}
```

- [ ] **Step 2: Run to verify failure**

- [ ] **Step 3: Implement `signTransaction`** — RLP-encode tx with `(chainId, 0, 0)` for the EIP-155 v field, keccak256 the result, sign with `Secp256k1Signer`, then RLP-re-encode with `v = recid + 35 + 2*chainId` and signature `(r, s)`.

- [ ] **Step 4: Replace each throw at lines 335, 345, 374, 403, 412 with calls to `signTransaction` + `eth_sendRawTransaction`. Update header comments to remove "throws std::runtime_error".**

- [ ] **Step 5: Run all SwapDaemon tests**

```bash
cd build && cmake --build . --target SwapDaemonTests && ctest -R Eth
```

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(swap-eth): EIP-155 sign+send for lock/claim/refund/approve"
```

---

### Task B3: ETH refund failure transitions to `FAILED`

**Files:**
- Modify: `src/SwapDaemon/SwapStateMachine.cpp` (add `FAILED` transition from ETH refund-error path)
- Test: extend `tests/SwapDaemon/SwapStateMachineTests.cpp`

- [ ] **Step 1: Failing test — when `refundEthLock` throws, machine transitions `XFG_REFUNDED → FAILED` (not stuck).**

- [ ] **Step 2: Run to fail**

- [ ] **Step 3: Catch in `SwapDaemon.cpp` ETH refund branch and call `m_state.transition(SwapState::FAILED, "eth_refund_error: " + e.what())`.**

- [ ] **Step 4: Run to pass**

- [ ] **Step 5: Commit**

```bash
git commit -m "fix(swap): ETH refund failure transitions to FAILED instead of looping"
```

---

## Chunk 3: Phase C — BCH HTLC signing

### Task C1: `Bip143Sighash` for P2SH HTLC inputs

**Files:**
- Create: `src/SwapDaemon/Crypto/Bip143Sighash.{h,cpp}`
- Test: `tests/SwapDaemon/Bip143SighashTests.cpp`

- [ ] **Step 1: Failing test against the BCH BIP143 reference vector** (use the test vector from CHIP-2017-08-OPCODES; one P2SH input, one output, sighash type 0x41 / `SIGHASH_ALL | SIGHASH_FORKID`).

- [ ] **Step 2: Run to fail**

- [ ] **Step 3: Implement `computeSighash(prevOutsHash, sequenceHash, outpoint, scriptCode, value, sequence, outputsHash, locktime, sighashType)` — double-SHA256 of the concatenated BIP143 preimage. Always use `SIGHASH_ALL | SIGHASH_FORKID = 0x41` for BCH.**

- [ ] **Step 4: Run to pass**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(swap-bch): BIP143 sighash for HTLC P2SH inputs"
```

---

### Task C2: BCH `lock` / `claim` / `refund` actually sign and broadcast

**Files:**
- Modify: `src/SwapDaemon/BitcoinCash/BchRpcClient.cpp:365–414`
- Modify: `src/SwapDaemon/BitcoinCash/HtlcScript.{h,cpp}` (expose `buildClaimScriptSig(sig, pubkey, secret, redeemScript)` and refund variant if missing)
- Test: `tests/SwapDaemon/BchHtlcSignTests.cpp`

- [ ] **Step 1: Failing test — given a fixture redeem script + funding tx, build a claim tx, sign it, and verify the resulting hex parses through `bitcoin-cli decoderawtransaction` (or an in-process equivalent if no bitcoind in CI — at minimum verify that re-hashing the sighash and recovering the pubkey matches the signer's pubkey).**

- [ ] **Step 2: Run to fail**

- [ ] **Step 3: Replace each `// TODO` block in `BchRpcClient.cpp` with the real call sequence:**
   1. `listUnspent` for the HTLC address (verify amount + confirms in `verifyHtlcFunded`).
   2. Build raw tx skeleton (1 input from HTLC outpoint, 1 output to claim/refund address).
   3. Compute BIP143 sighash with `redeemScript` as `scriptCode`.
   4. Sign with `Secp256k1Signer`.
   5. Build `scriptSig = <sig||0x41> <pubkey> <secret-or-empty> <0|1> <redeemScript>`.
   6. `sendRawTransaction`.

- [ ] **Step 4: Run to pass**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(swap-bch): real BIP143 signing for HTLC lock/claim/refund"
```

---

### Task C3: Persist `redeemScriptHex` in `SwapParams`

**Files:**
- Modify: `src/SwapDaemon/SwapParams.h` — add `std::string bchRedeemScriptHex;`
- Modify: `src/SwapDaemon/SwapDatabase.cpp` — extend serialization
- Modify: `src/SwapDaemon/SwapDaemon.cpp:581,927` — pass `params.bchRedeemScriptHex` instead of `""`
- Test: `tests/SwapDaemon/SwapDatabaseTests.cpp` — round-trip new field

- [ ] **Step 1: Failing test — serialize/deserialize a `SwapParams` with a non-empty `bchRedeemScriptHex` and confirm it round-trips.**

- [ ] **Step 2: Run to fail**

- [ ] **Step 3: Add field to struct, serialization, and the two `SwapDaemon.cpp` call sites. Set the field whenever a BCH HTLC is created.**

- [ ] **Step 4: Run to pass**

- [ ] **Step 5: Commit**

```bash
git commit -m "fix(swap-bch): persist redeemScriptHex in SwapParams"
```

---

## Chunk 4: Phase D — Configuration wiring (`ChainClientConfig`)

### Task D1: `ChainClientConfig` struct and JSON loader

**Files:**
- Create: `src/SwapDaemon/ChainClientConfig.{h,cpp}`
- Test: `tests/SwapDaemon/ChainClientConfigTests.cpp`
- Create: `docs/runbooks/swap-config-example.json` (example, not committed-secret)

- [ ] **Step 1: Failing test — load `swap_config.json`, expect populated `ethPrivKey`, `ethAddress`, `bchPrivKey`, `bchAddress`, `solKeypairPath`, `xmrSpendKey`, `xmrViewKey`. Reject hex of wrong length or empty required fields.**

- [ ] **Step 2: Run to fail**

- [ ] **Step 3: Implement struct + `loadFromFile(const std::string& path, ChainClientConfig& out, std::string& err)`. Validate hex lengths (32 bytes ETH/BCH/XMR priv). Fail closed — empty key = error, not silent zero.**

- [ ] **Step 4: Run to pass**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(swap): ChainClientConfig with per-chain key/address loading"
```

---

### Task D2: Wire `ChainClientConfig` into `SwapDaemon` construction

**Files:**
- Modify: `src/SwapDaemon/SwapDaemon.{h,cpp}` — add ctor overload taking `const ChainClientConfig&`; thread fields into each `*RpcClient`.
- Modify: `src/SwapDaemon/Ethereum/EthRpcClient` — accept `signerPrivKey` + `signerAddress`.
- Modify: `src/SwapDaemon/BitcoinCash/BchRpcClient` — accept `signerPrivKey` + `signerAddress`.
- Modify: `src/SwapDaemon/Monero/MoneroRpcClient` — accept `spendKey` + `viewKey`.
- Modify: `src/SwapDaemon/main.cpp` — load `--swap-config <path>` and use the new ctor.

- [ ] **Step 1: Failing test — construct `SwapDaemon` with a fixture config, then call `client->getSignerAddress()` on each adapter and confirm it matches.**

- [ ] **Step 2: Run to fail**

- [ ] **Step 3: Add field + accessor to each RpcClient. Update SwapDaemon ctor + main.cpp.**

- [ ] **Step 4: Run to pass**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(swap): wire ChainClientConfig through SwapDaemon construction"
```

---

## Chunk 5: Phase E — XMR `claimAdaptor` completion

### Task E1: Combine Alice+Bob spend keys with adaptor secret

**Files:**
- Modify: `src/SwapDaemon/Monero/MoneroRpcClient.cpp:421–460`
- Modify: `src/SwapDaemon/Monero/AdaptorSignature.{h,cpp}` — expose `combineSpendKeys(aliceShare, bobShare, adaptorSecret) -> spendKey`
- Test: `tests/SwapDaemon/XmrClaimAdaptorTests.cpp`

- [ ] **Step 1: Failing test — given Alice+Bob spend-key shares and adaptor secret, the combined key matches `(aliceShare + bobShare + adaptorSecret) mod l` and signs a CLSAG that verifies under the joint public key.**

- [ ] **Step 2: Run to fail**

- [ ] **Step 3: Implement `combineSpendKeys` in `AdaptorSignature.cpp` (Ed25519 scalar addition mod ℓ via `sc_add`). Replace the TODO at `MoneroRpcClient.cpp:444` with: load alice+bob+adaptor scalars → combine → import as wallet spend key → call `sweep_all`.**

- [ ] **Step 4: Run to pass**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(swap-xmr): claimAdaptor combines spend keys with adaptor secret"
```

---

## Chunk 6: Phase F — Hardening & follow-ups

### Task F1: Verify SRI hashes against pinned CDN files

**Files:**
- Modify: `swapxfg/app/bridge_eth.go:24` (ethers 5.7.2 SRI)
- Modify: `swapxfg/app/bridge_sol.go:20` (`@solana/web3.js` 1.95.3 SRI)

- [ ] **Step 1: Pull the actual files and recompute SHA-384 SRI**

```bash
curl -sSL https://cdn.jsdelivr.net/npm/ethers@5.7.2/dist/ethers.umd.min.js | openssl dgst -sha384 -binary | openssl base64 -A
curl -sSL https://cdn.jsdelivr.net/npm/@solana/web3.js@1.95.3/lib/index.iife.min.js | openssl dgst -sha384 -binary | openssl base64 -A
```

- [ ] **Step 2: If hashes differ from the placeholders at `bridge_eth.go:24` / `bridge_sol.go:20`, replace them. Bump pinned versions only if SRI verification fails on a known-good library version.**

- [ ] **Step 3: Commit**

```bash
git commit -m "fix(swapxfg): verify SRI hashes for ethers/web3.js against pinned CDN files"
```

---

### Task F2: Convert remaining 6 TUI goroutines to `tea.Cmd`

**Files:**
- Modify: `swapxfg/app/tui.go:499,523,565,590,608,636`
- Test: `swapxfg/app/tui_cmds_test.go`

- [ ] **Step 1: For each of the 6 TODO sites, identify the goroutine body and the message it eventually sends to the model. Wrap as `func() tea.Msg { ... return resultMsg{...} }` returned via `tea.Cmd`.**

- [ ] **Step 2: Failing test per converted command — assert the `tea.Cmd` returns the expected message type.**

- [ ] **Step 3: Run go test**

```bash
cd swapxfg && go test ./app/... -run TuiCmd -v
```

- [ ] **Step 4: Commit per logical group (e.g. send/receive cmds, balance cmd) so the change history stays bisectable.**

```bash
git commit -m "refactor(swapxfg): convert remaining TUI goroutines to tea.Cmd"
```

---

### Task F3: Replace `isDummyGiftSecret` heuristic

**Files:**
- Modify: `src/CryptoNoteCore/TransactionExtra.cpp:1321`
- Test: `tests/UnitTests/TransactionExtraTests.cpp`

- [ ] **Step 1: Failing test — a real (random) 32-byte gift secret is NOT classified as dummy with probability > 0.999; a known dummy pattern IS classified as dummy.**

- [ ] **Step 2: Decide replacement: either (a) remove `isDummyGiftSecret` entirely if no production caller depends on it, or (b) replace the statistical heuristic with a versioned magic-byte prefix (e.g. `0xDD 0xDD` first two bytes) and validate explicitly.**

- [ ] **Step 3: Verify there are no consensus-affecting callers** (`grep -n isDummyGiftSecret src/`). If any exist, prefer option (b) with a clearly versioned prefix.

- [ ] **Step 4: Run tests**

- [ ] **Step 5: Commit**

```bash
git commit -m "fix(extra): replace isDummyGiftSecret statistical heuristic"
```

---

### Task F4: XMR address validator checks base58 checksum

**Files:**
- Modify: `swapxfg/app/bridge.go` (XMR address validator)
- Test: `swapxfg/app/bridge_test.go`

- [ ] **Step 1: Failing test — a malformed XMR address with corrupted checksum is rejected; a known-good mainnet/testnet address is accepted.**

- [ ] **Step 2: Implement Monero base58 (Crockford-style 8-byte blocks → 11 chars, last block variable). Validate the trailing 4-byte keccak checksum.**

- [ ] **Step 3: Run go test**

- [ ] **Step 4: Commit**

```bash
git commit -m "fix(swapxfg): XMR address validator checks base58 checksum"
```

---

## Chunk 7: Phase G — End-to-end testnet validation

### Task G1: Testnet swap walkthrough runbook

**Files:**
- Create: `docs/runbooks/testnet-swap-walkthrough.md`

- [ ] **Step 1: Document the per-pair runbook for XFG↔ETH, XFG↔BCH, XFG↔XMR, XFG↔SOL.** Include:
  - Funding faucet URLs.
  - `swap_config.json` template (with placeholder keys, never real ones).
  - Expected daemon log lines per state transition.
  - Rollback steps if a swap stalls.

- [ ] **Step 2: Commit**

```bash
git commit -m "docs(swap): testnet swap walkthrough runbook"
```

---

### Task G2: Run the runbook end-to-end per chain (manual)

This task is a manual checklist — no code changes, just signoff.

- [ ] **Step 1: XFG ↔ ETH (Sepolia)** — initiate, lock, claim, verify on-chain.
- [ ] **Step 2: XFG ↔ BCH (chipnet)** — initiate, fund HTLC, claim with secret, verify.
- [ ] **Step 3: XFG ↔ XMR (stagenet)** — initiate, claimAdaptor, verify XMR sweep lands.
- [ ] **Step 4: XFG ↔ SOL (devnet)** — initiate, lock, claim, verify.
- [ ] **Step 5: For each pair, also run the refund path: let lock expire, run refund, confirm funds returned.**
- [ ] **Step 6: Update `docs/review/xfgCdswaps-review.md` with a new "Phase G results" section listing pass/fail per pair and any new issues uncovered.**

- [ ] **Step 7: Commit the updated review doc**

```bash
git commit -m "docs(review): record Phase G testnet swap results"
```

---

## Plan Review Loop

After each chunk above is implemented, dispatch the plan-document-reviewer subagent against that chunk to confirm:

1. Every task names exact file paths.
2. Tests are written before implementation.
3. Commits happen at task boundaries, not at the end.
4. No skipped abstractions ("add validation" without showing what).
5. Each task is bite-sized (2–5 min steps), not "implement EIP-155" as a single line.

If the reviewer flags issues, fix in-place before moving to the next chunk.

---

## Out of scope

- `src/SwapDaemon/pool_v11/*` — zkLPswap pool trading, deferred to v11 by user direction.
- Any consensus-rule change in `Blockchain.cpp` — this plan is daemon/wallet adapters only.
- Wallet GUI (`fuego-wallet` repo) integration — separate repository.
- Mainnet activation/coordinated-fork checklist — comes after Phase G testnet validation passes.
- Gating these changes behind a feature flag — atomic-swap code is already off the consensus path; the daemon adapter simply throws today and will succeed tomorrow.

---

## Verification (whole-plan)

After all 7 chunks complete:
1. `make` builds clean with `secp256k1` linked.
2. All new unit tests pass under `ctest`.
3. `swapxfg` Go tests pass under `go test ./...`.
4. Each open item in `docs/review/xfgCdswaps-review.md` "intentional stubs" section is either closed (with a commit SHA) or explicitly re-deferred with rationale.
5. Phase G runbook records at least one successful swap and one successful refund per chain.
