# Fuego Security & Bug Fix — Phased Implementation Plan

> Generated from 72 findings across 5 domains (Core, P2P, Wallet/RPC, Daemons, Go).
> 17 Critical, 24 High, 24 Medium, 7 Low.

---

## Phase 0 — Blocker Fixes (4 fixes, ~1 hour)

These are 10-minute fixes that unblock subsequent work. Each is a single-line or single-function change with zero architectural risk.

### Fix 0.1: Bond interest always returns zero

- **File:** `src/CryptoNoteCore/Currency.cpp:316`
- **Problem:** `calculateInterest` computes interest into local variables but returns `offchaininterest` which is initialized to `0` and never assigned. All legacy bond interest = 0.
- **Fix:** Change `return offchaininterest;` to `return interestLo;`
- **Verify:** Write a unit test calling `calculateInterest` with known inputs, compare to expected output.

### Fix 0.2: Secret view key logged in plaintext

- **File:** `src/Daemon/Daemon.cpp:391-394`
- **Problem:** `logger(INFO, BRIGHT_YELLOW) << "Secret view key set: " << vk_str;` — view key written to all logs permanently.
- **Fix:** Replace with redacted log message: `logger(INFO) << "Secret view key configured (redacted)";`
- **Verify:** Start daemon with `--set-view-key`, check logs — key must not appear.

### Fix 0.3: Testnet cache filename typo

- **File:** `src/CryptoNoteCore/Currency.cpp:97`
- **Problem:** `"tesnet_"` instead of `"testnet_"` — forces full blockchain re-scan on every testnet restart.
- **Fix:** Change `"tesnet_"` to `"testnet_"`.
- **Verify:** Run node on testnet twice — second start should load cache instantly.

### Fix 0.4: `cn_slow_hash` ignores `prehashed` parameter

- **File:** `src/crypto/hash.h:70`
- **Problem:** C++ wrapper accepts `prehashed` parameter but hardcodes `0` to the C function. Callers expecting pre-hashed mode get wrong hash outputs.
- **Fix:** Change last argument from `0` to `prehashed`.
- **Verify:** Search codebase for callers passing `prehashed=1`. Confirm they compute correctly.

---

## Phase 1 — Critical Fixes (13 fixes, ~2 weeks)

### Priority 1-A: Consensus & Fund Safety (must not break consensus)

#### Fix 1.1: Burn proof validation is completely broken

- **File:** `src/CryptoNoteCore/Currency.cpp:1755-1776`
- **Problem:** `validateBurnProofData` validates only string lengths (64 hex chars). Any 64-char hex strings pass as a valid burn proof. `calculateBurnCommitment` and `calculateBurnNullifier` exist but are never called.
- **Fix:**
  ```cpp
  bool Currency::validateBurnProofData(const std::string& secret, uint64_t amount,
                                        const std::string& commitment, const std::string& nullifier) {
    if (!Common::isValidHex(secret, 64))         return false;
    if (!isValidBurnDepositAmount(amount))       return false;
    if (!Common::isValidHex(commitment, 64))     return false;
    if (!Common::isValidHex(nullifier, 64))      return false;

    Crypto::SecretKey sec;
    if (!Common::podFromHex(secret, sec))        return false;

    Crypto::Hash expectedNullifier = calculateBurnNullifier(sec);
    Crypto::Hash expectedCommitment = calculateBurnCommitment(sec, amount);

    if (Common::podToHex(expectedNullifier) != nullifier)   return false;
    if (Common::podToHex(expectedCommitment) != commitment) return false;

    return true;
  }
  ```
- **Verify:** Write test: generate secret+amount, compute expected/nullifier, call validate → must pass. Modify one char → must fail. Test with empty, short, invalid-hex inputs.

#### Fix 1.2: `getTransactionOutputs` security-critical stub

- **File:** `src/SwapDaemon/FuegoRpcClient.cpp:294-308`
- **Problem:** Function clears outputs and returns `true`. Await swap output verification is blind. Any swap relying on this to verify escrow sees a lie.
- **Fix:** Implement actual output retrieval, or if not yet feasible:
  ```cpp
  bool FuegoRpcClient::getTransactionOutputs(...) {
    // TODO(Phase 2): Implement blockchain output retrieval
    outputs.clear();
    lastError = "getTransactionOutputs not yet implemented";
    return false; // FAIL OPEN — callers must know verification didn't happen
  }
  ```
- **Verify:** After fix, swap initiation that reaches this path must fail with clear error rather than silently succeeding.

#### Fix 1.3: Adaptor signature point validation (2 issues)

- **File 1:** `src/crypto/adaptor.cpp:84` — after `ge_frombytes_vartime(&adaptor_point, ...)`:
  ```cpp
  if (!point_is_valid(reinterpret_cast<const unsigned char*>(&adaptor_point)))
    return false;
  ```
- **File 2:** `src/crypto/adaptor.cpp:146` — after `ge_frombytes_vartime(&pub, ...)`:
  ```cpp
  if (!point_is_valid(reinterpret_cast<const unsigned char*>(&pub)))
    return false;
  ```
- **Verify:** Write unit tests with identity element, order-8 point, valid point. Check signature rejection on invalid input.

#### Fix 1.4: Overflow in `convertXfgToHeat`

- **File:** `src/CryptoNoteCore/Currency.cpp:1706`
- **Fix:**
  ```cpp
  uint64_t Currency::convertXfgToHeat(uint64_t xfgAmount) const {
    if (xfgAmount > UINT64_MAX / 10000000) {
      return UINT64_MAX;
    }
    return xfgAmount * 10000000;
  }
  ```
- **Verify:** Unit test with `UINT64_MAX / 10000000 + 1` → must return `UINT64_MAX`, not wrap.

### Priority 1-B: Authentication & Authorization

#### Fix 1.5: No authentication on PaymentGate RPC

- **File:** `src/PaymentGate/PaymentServiceJsonRpcServer.cpp`
- **Problem:** Full wallet control (send, export, rekey keys) on unauthenticated JSON-RPC endpoint.
- **Fix:** Implement HMAC-signed request authentication or API-key-based auth:
  1. Generate a random 32-byte API key at daemon startup, write to a local file (0600).
  2. Require `Authorization: Bearer <key>` header on all PaymentGate RPC handlers.
  3. Reject requests without valid key with HTTP 401.
- **Verify:** `curl http://127.0.0.1:PORT/json_rpc` with wallet operation → 401. With key → succeeds.

#### Fix 1.6: `getSpendkeys` returns secret spend key over unauthenticated RPC

- **File:** `src/PaymentGate/WalletService.cpp:204`
- **Fix:** Remove `secretSpendKey` from the RPC response. If needed for backup, create a separate authenticated `exportWallet` command that requires explicit confirmation.
- **Verify:** Call `getSpendkeys` via RPC — `secretSpendKey` must be absent or returned only with elevated auth.

### Priority 1-C: RNG & Cryptographic Hardening

#### Fix 1.7: RNG fork-safety

- **File:** `src/crypto/random.c:99-108`
- **Problem:** No `pthread_atfork` handler. After fork, parent and child share identical RNG state → identical keys/nonces.
- **Fix:**
  ```c
  static void reinit_random(void) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
      read(fd, &state, STATE_LENGTH);
      close(fd);
    }
  }
  // In init_random, after initial seed:
  pthread_atfork(NULL, NULL, reinit_random);
  ```
- **Verify:** Fork a process, generate random bytes in parent and child, confirm they differ.

#### Fix 1.8: Modulo bias in `randomValue`

- **File:** `src/crypto/randomize.h:57`
- **Problem:** `result % range` produces biased distribution where `range` doesn't evenly divide `2^(8*sizeof(T))`. Affects ring member selection — deanonymization risk.
- **Fix:** Use rejection sampling:
  ```cpp
  template<typename T>
  T randomValue(T min, T max) {
    T range = max - min + 1;
    T limit = (std::numeric_limits<T>::max() / range) * range;
    T result;
    do {
      generate_random_bytes(sizeof(T), &result);
    } while (result >= limit);
    return min + (result % range);
  }
  ```
- **Verify:** Statistical test: generate 1M values in range [0, 200), verify chi-squared vs uniform.

### Priority 1-D: Memory Safety for Secrets

#### Fix 1.9: PasswordContainer — secure memory

- **File:** `src/SimpleWallet/PasswordContainer.h`, `PasswordContainer.cpp`
- **Fix:**
  1. Replace `std::string m_password` with a `SecureString` class that uses `mlock()` / `munlock()`.
  2. Add destructor that zeros memory before free.
  3. Use `volatile` writes or `explicit_bzero` to prevent compiler optimization.
- **Verify:** After destroying `PasswordContainer`, memory dump search for password string must find zeroes.

#### Fix 1.10: ETH private key zeroization

- **File:** `src/SwapDaemon/Ethereum/EthRpcClient.h`, `EthRpcClient.cpp`
- **Fix:** Add destructor:
  ```cpp
  ~EthRpcClient() {
    closeSocket();
    std::fill(m_privKey.begin(), m_privKey.end(), 0);
    volatile uint8_t* p = m_privKey.data();
    for (size_t i = 0; i < m_privKey.size(); i++) p[i] = 0;
  }
  ```
- **Verify:** After process exit, check core dump for private key bytes.

### Priority 1-E: Missing Validation

#### Fix 1.11: No rate limiting on RPC endpoints

- **File:** `src/Rpc/HttpServer.cpp`
- **Fix:** Add `std::map<std::string, std::deque<time_t>> m_requestLog` keyed by IP. Reject with `HTTP 429` when > N requests in window (e.g., 100 req/sec).
- **Verify:** `ab -n 1000 -c 50 http://localhost:PORT/` — after threshold, get 429 responses.

#### Fix 1.12: Unbounded input vectors

- **File:** `src/Rpc/CoreRpcServerCommandsDefinitions.h` — `block_ids`, `txs_hashes`
- **Fix:** In `RpcServer::processRequest`, add check:
  ```cpp
  if (req.txs_hashes.size() > MAX_RPC_VECTOR_SIZE) {
    return makeJsonErrorResponse(INVALID_INPUT, "Too many items, max: " + MAX_RPC_VECTOR_SIZE);
  }
  ```
  Define `MAX_RPC_VECTOR_SIZE = 10000` in `CryptoNoteConfig.h`.
- **Verify:** Send RPC request with 100,001 hashes → rejected.

#### Fix 1.13: MLSAG secret nonce cleanup on error paths

- **File:** `src/crypto/mlsag.cpp:101-102, 118, 174`
- **Fix:** Add `memset(&alpha0, 0, sizeof(alpha0)); memset(&alpha1, 0, sizeof(alpha1));` before each `return false;`.
- **Verify:** Code review — confirm every return path zeroes the scalars.

---

## Phase 2 — High Severity (23 fixes, ~3 weeks)

### Priority 2-A: Transport Security

#### Fix 2.1: No TLS on RPC (daemon + wallet)

- **Files:** `src/Rpc/HttpServer.cpp`, `src/Rpc/HttpClient.cpp`, `src/Rpc/RpcServerConfig.h`
- **Fix:** Integrate OpenSSL or mbedTLS for TLS support:
  1. Add `m_tlsEnable`, `m_certFile`, `m_keyFile` to `RpcServerConfig`.
  2. Wrap `boost::asio::ip::tcp::socket` with `boost::asio::ssl::stream<boost::asio::ip::tcp::socket>` when TLS enabled.
  3. Generate self-signed cert at daemon startup if none provided.
  4. `HttpClient` verifies cert or uses `--insecure` flag for localhost.
- **Verify:** `curl --insecure https://127.0.0.1:PORT/` — works. `curl http://` — rejected.

#### Fix 2.2: Basic Auth over plaintext → TLS required

- **Files:** `src/Rpc/HttpServer.cpp`, `src/Wallet/WalletRpcServer.cpp`
- **Fix:** After TLS is implemented (Fix 2.1), reject Basic Auth requests over non-TLS connections. Alternatively add API-key auth as well.
- **Verify:** Send Basic Auth over HTTP → rejected. Over HTTPS → accepted.

#### Fix 2.3: P2P clearnet encryption

- **Files:** `src/P2p/NetNode.cpp`, `src/P2p/LevinProtocol.cpp`
- **Fix:** Implement Noise Protocol Framework at the Levin layer:
  1. During handshake, perform `XX` pattern key exchange.
  2. After handshake, wrap all `sendMessage`/`readCommand` in `NoiseTransport` encrypt/decrypt.
  3. Maintain backward compat: if peer doesn't support Noise, fall back (with loud warning) or require `--no-clearnet` flag.
- **Verify:** Wireshark capture of P2P traffic must show encrypted payloads after handshake.

#### Fix 2.4: P2P message-level authentication (HMAC)

- **Files:** `src/P2p/LevinProtocol.cpp`, `src/P2p/LevinProtocol.h`
- **Fix:** After Noise handshake (Fix 2.3), append 16-byte HMAC to each message. Reject any message with invalid HMAC.
- **Verify:** Craft a modified packet, inject into connection → rejected.

### Priority 2-B: DoS Protection

#### Fix 2.5: Inbound connection rate limiting

- **File:** `src/P2p/NetNode.cpp:1877-1901`
- **Fix:** In `acceptLoop()`:
  1. Track per-IP connection count in `std::map<std::string, std::atomic<size_t>>`.
  2. Reject connections (close immediately) when over threshold (e.g., 8 per IP, 128 total).
  3. Global connection cap: refuse new accepts when active connections >= max.
- **Verify:** Connect from same IP 9 times → 9th rejected with no handler spawned.

#### Fix 2.6: Levin max packet size reduction

- **File:** `src/P2p/LevinProtocol.cpp:28`
- **Fix:** Reduce `LEVIN_DEFAULT_MAX_PACKET_SIZE` from `100000000` (100MB) to `20971520` (20MB) to match `P2P_DEFAULT_PACKET_MAX_SIZE`.
- **Verify:** Send packet > 20MB → rejected.

#### Fix 2.7: JSON depth limit

- **File:** `src/JsonRpcServer/JsonRpcServer.cpp`
- **Fix:** Add max nesting depth check before or during parsing. Reject payloads > 32 levels deep.
- **Verify:** Send JSON with 50 nested levels → `INVALID_JSON` error.

### Priority 2-C: Swap & Payment Gate Security

#### Fix 2.8: Swap secret encryption hardening

- **File:** `src/SwapDaemon/SwapDaemon.cpp:211-222`
- **Fix:**
  1. Generate per-swap random 32-byte salt, store in swap state alongside ciphertext.
  2. Remove `XFG_SWAP_ENC_KEY` env var fallback.
  3. Use HMAC-SHA256 instead of `cn_fast_hash` for MAC.
- **Verify:** Encrypt two swaps with same maker key → different ciphertexts (different salts).

#### Fix 2.9: Hardcoded 20 gwei gas price → dynamic

- **File:** `src/SwapDaemon/Ethereum/EthRpcClient.cpp:637,724`
- **Fix:** Query `eth_gasPrice` at transaction construction time. Use result as gas price for legacy transactions. Add 20% buffer for EIP-1559 maxFeePerGas. Fallback to a configurable default (`--eth-gas-price`).
- **Verify:** On testnet/Goerli, verify gas price in constructed transaction matches current network state.

#### Fix 2.10: Integer overflow in fee surcharge

- **File:** `src/SwapDaemon/SwapDaemon.cpp:634`
- **Fix:**
  ```cpp
  if (params.xfgAmount > UINT64_MAX - senderSurcharge) {
    m_logger(Logging::ERROR) << "Fee surcharge overflow on swap " << swapId;
    return false;
  }
  params.xfgAmount += senderSurcharge;
  ```
- **Verify:** Unit test with `params.xfgAmount = UINT64_MAX`, `senderSurcharge = 1` → must reject.

#### Fix 2.11: Price oracle bootstrap validation

- **File:** `src/SwapDaemon/PriceOracle.cpp:169-170`
- **Fix:** In bootstrap mode, enforce that rates are within ±50% of seed rate, not unbounded. Add `--max-bootstrap-drift` config parameter.
- **Verify:** First swap on a fresh pair with rate 10x seed → rejected. Rate within 50% → accepted.

### Priority 2-D: Wallet Security

#### Fix 2.12: Wallet file AEAD encryption

- **File:** `src/Wallet/WalletGreen.cpp:650-720`
- **Fix:** Replace current encryption with AES-256-GCM or ChaCha20-Poly1305. Always verify MAC before deserializing any data.
- **Verify:** Create wallet, corrupt one byte of ciphertext, attempt load → must fail with integrity error.

#### Fix 2.13: Wallet file permissions → 0600

- **File:** `src/Wallet/WalletGreen.cpp`
- **Fix:** After wallet file creation or save:
  ```cpp
  chmod(walletPath.c_str(), S_IRUSR | S_IWUSR);
  ```
- **Verify:** `ls -la wallet_file` → `-rw-------`.

#### Fix 2.14: RPC error message sanitization

- **Files:** `src/Rpc/RpcServer.cpp`, `src/Wallet/WalletRpcServer.cpp`
- **Fix:** Return generic error codes to RPC clients. Log detailed errors server-side only. Never include file paths, stack traces, or internal state in RPC error responses.
- **Verify:** Trigger various error conditions via RPC, confirm responses contain no internal detail.

### Priority 2-E: P2P Security

#### Fix 2.15: Peerlist poisoning mitigation

- **File:** `src/P2p/NetNode.cpp:1454-1477`
- **Fix:**
  1. Rate-limit peerlist acceptance: accept from a given peer at most once per handshake, N times per hour for timed sync.
  2. Limit peers accepted from single source to 50.
  3. Only peer-exchange with peers that have proof-of-trust.
- **Verify:** Simulate eclipse — connect attacker node, inject 1000 fake peers. Verify only first 50 enter peerlist.

#### Fix 2.16: Swap offer validation before processing

- **File:** `src/CryptoNoteProtocol/CryptoNoteProtocolHandler.cpp:1191-1207`
- **Fix:** Before passing offer to core, validate:
  1. `offerId` length ≤ 64 (SHA-256 hex).
  2. `xfgAmount`, `rateNum` within reasonable ranges.
  3. `pair` is a valid enum value (0-4).
  4. Signature verified against `makerPubKey`.
- **Verify:** Inject swap offer with 10KB offerId string → rejected at protocol layer.

#### Fix 2.17: DNS leak with privacy zones

- **File:** `src/P2p/NetNode.cpp:811-822`
- **Fix:** When any privacy zone (I2P/Tor) is active, skip clearnet DNS seed resolution entirely unless `--allow-cleartnet-seeds` is explicitly set.
- **Verify:** Start node with `--tor`, check network traffic — no DNS queries for seed nodes.

#### Fix 2.18: UPnP disable by default

- **File:** `src/P2p/NetNode.cpp:75-107, 882-883`
- **Fix:** Change UPnP from opt-out to opt-in. Add `--enable-upnp` flag. If not set, skip `addPortMapping`.
- **Verify:** Start node without `--enable-upnp` — no UPnP discovery packets on network.

### Priority 2-F: Go Auxiliaries

#### Fix 2.19: swapxfg headless API authentication

- **File:** `swapxfg/app/daemon.go:175`
- **Fix:** Generate random API token at startup, print to stdout. Require `Authorization: Bearer <token>` on all `/offer`, `/cancel`, etc. endpoints. Reject with 401.
- **Verify:** `curl http://127.0.0.1:18190/offer` without token → 401. With token → 200.

#### Fix 2.20: Division by zero in TUI rate calculation

- **File:** `swapxfg/app/tui.go:372`
- **Fix:**
  ```go
  ctrFloat, err := strconv.ParseFloat(amtCtr, 64)
  if err != nil || ctrFloat <= 0 {
    return error
  }
  rateFloat := (float64(xfgAtomic) / 1e7) / ctrFloat
  ```
- **Verify:** Press Enter on `amtCtr` field with empty value → error message, not panic.

#### Fix 2.21: XMR address validation

- **File:** `swapxfg/app/validate.go:146`
- **Fix:** Implement Monero block-based Base58 decoding. Each block encodes 8 bytes → 11 chars. Validate checksum after decoding.
- **Verify:** Paste valid XMR mainnet address → accepted. Modified address → rejected.

---

## Phase 3 — Medium Severity (24 fixes, ~3 weeks)

### Priority 3-A: Consensus Edge Cases

#### Fix 3.1: Duplicate ring signature verification

- **File:** `src/CryptoNoteCore/Blockchain.cpp:2121-2134`
- **Fix:** Remove the unconditional `check_tx_input` at line 2121. Keep only the checkpoint-guarded call at line 2129. Restructure so the check runs once.
- **Verify:** Old block verifies correctly after removal. Measure verification time reduction.

#### Fix 3.2: Wrong block height for withdrawal txs

- **File:** `src/CryptoNoteCore/Core.cpp:284`
- **Fix:** When `getBlockContainingTx` fails for a withdrawal transaction, determine the correct block height from the withdrawal's commitment index reference rather than using current tip.
- **Verify:** Withdrawal transaction tested at various block heights — rules applied correctly based on commitment height.

#### Fix 3.3: DLEQ proof generation missing point validation

- **File:** `src/crypto/dleq.cpp:87-90`
- **Fix:** Add `point_is_valid` checks for `base_point`, `point_G`, `point_P` after each `ge_frombytes_vartime` in `generate_dleq_proof`, matching the checks in `check_dleq_proof`.
- **Verify:** Generate DLEQ proof with invalid point → rejected at generation, not at verification.

### Priority 3-B: Race Conditions & Memory

#### Fix 3.4: Race condition in `tickLoop` swap iteration

- **File:** `src/SwapDaemon/SwapDaemon.cpp:305-311`
- **Fix:** Copy swap IDs under mutex, then process each outside the lock. If `loadSwap` fails for a deleted entry, skip gracefully.
- **Verify:** Stress test with concurrent swap creation, deletion, and tick loop.

#### Fix 3.5: Uninitialized `time_t` in ban command

- **File:** `src/Daemon/DaemonCommandsHandler.cpp:468`
- **Fix:** Initialize `time_t seconds = 0;` (or a sensible default). Reject if args missing.
- **Verify:** Run `ban <address>` without seconds argument → reject with usage message.

#### Fix 3.6: Underflow in supply calculations

- **File:** `src/PaymentGate/WalletService.cpp:2264-2271`
- **Fix:**
  ```cpp
  if (ethereal_xfg > baseTotalSupply) {
    return make_error_code(INTERNAL_WALLET_ERROR);
  }
  uint64_t realTotalSupply = baseTotalSupply - ethereal_xfg;
  ```
- **Verify:** Set `ethereal_xfg > baseTotalSupply` via test mock → error, not wrapped value.

#### Fix 3.7: Swapxfg bridge mutex during blocking I/O

- **File:** `swapxfg/app/bridge.go:203`
- **Fix:** Marshal JSON and release `b.mu` before calling `conn.WriteMessage`:
  ```go
  func (b *BridgeServer) Send(msg interface{}) error {
    data, err := json.Marshal(msg)
    if err != nil { return err }
    b.mu.Lock()
    conn := b.conn
    b.mu.Unlock()
    if conn == nil { return ErrNotConnected }
    return conn.WriteMessage(websocket.TextMessage, data)
  }
  ```
- **Verify:** Slow network on bridge connection → pressing Stop button still works.

### Priority 3-C: Financial Precision

#### Fix 3.8: Double-precision float for rate calculations

- **File:** `src/SwapDaemon/PriceOracle.cpp:91-101`
- **Fix:** Replace `double` arithmetic with scaled integer (uint128) arithmetic for all rate calculations. Convert to double only for display.
- **Verify:** Compare output of old vs new calculation for boundary values — no sub-satoshi drift.

#### Fix 3.9: `parseAmount` wei limit breaks ETH

- **File:** `swapxfg/app/validate.go:195`
- **Fix:** Remove `1e15` hard limit. Use `math.MaxUint64` as upper bound instead, or create separate function for raw atomic units.
- **Verify:** Enter `1000000000000000000` (1 ETH in wei) → accepted.

#### Fix 3.10: BCH CashAddr checksum validation

- **File:** `swapxfg/app/validate.go:105`
- **Fix:** Implement full polymod checksum verification per CashAddr spec (BIP-173 and cashaddr updates).
- **Verify:** Valid BCH address → accepted. BCH address with typo'd char → rejected.

### Priority 3-D: Logging, Error Handling & Cleanup

#### Fix 3.11-3.24 (remaining Medium findings)

- **Time-dependent security (P2P wall clock):** Replace `time(nullptr)` with `std::chrono::steady_clock` for ban durations. (`NetNode.cpp`)
- **Dandelion `hop_count` bound:** Enforce max 10-20 hops before forced fluff. (`CryptoNoteProtocolHandler.cpp`)
- **Debug command hardening:** Remove `os_version` from stat info. Ensure `ALLOW_DEBUG_COMMANDS` disabled in release. (`NetNode.cpp`)
- **Silent exception swallow (`init_config`):** Log warning when deserialization fails. (`NetNode.cpp:367`)
- **`try_ping` port restriction:** Only back-ping to standard P2P port. (`NetNode.cpp:1634`)
- **Undifferentiated error codes:** Return distinct error codes for parse vs hash mismatch. (`NodeRpcProxy.cpp:658-690`)
- **HttpClient connect timeout:** Add `deadline_timer` with configurable timeout. (`HttpClient.cpp:45-70`)
- **ChainClientConfig JSON parser:** Log warnings on parse failures. (`ChainClientConfig.cpp`)
- **Swap fee double-reporting:** Track reported fees in swap state. (`SwapDaemon.cpp:1314`)
- **Swap Secret Encryption MAC:** Replace `cn_fast_hash` with HMAC-SHA256. (`SwapSecretEncryption.cpp`)
- **Ban list periodic pruning:** Add cleanup in `idle_worker` or `timeoutLoop`. (`NetNode.cpp:319`)
- **Duplicate connections (incoming+outgoing):** Track peerId across both directions. (`NetNode.cpp:492`)
- **`time_t` → `uint64_t` for swap timestamps:** Prevent Y2038 issue. (`SwapTypes.h`, `PriceOracle.h`)
- **Dandelion stem randomization:** Document intentional cubic bias. (`NetNode.cpp:65-73`)

---

## Phase 4 — Low Severity (7 fixes, ~1 week)

- **`Blockchain.cpp:2531`** — Implement median-over-last-N-blocks timestamp (TODO noted).
- **`Currency.cpp:1725`** — Stronger domain separation tags for burn hashes.
- **`NetNode.cpp:433`** — PeerId rotate on session restart (tracking prevention).
- **Chain private key zeroization** — `SecureString` for all `std::string` key storage in `ChainClientConfig`, `EthRpcClient`, `XmrChainClient`, `BchChainClient`, `SolChainClient`.
- **`NetNode.cpp:367`** — Don't silently replace corrupted peerlist with default.
- **Pointer alignment abstraction** — Clean up the `--enable-wextra` flag in build system.

---

## Dev Guide

### Build & Test

```bash
# Build (from repo root)
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# Core tests
make -j$(nproc) && ctest --output-on-failure

# Specific test targets
make tests_core tests_crypto tests_p2p
```

### Testing Strategy Per Domain

| Domain | Test Type | Command | Coverage Target |
|--------|-----------|---------|----------------|
| Crypto | Unit tests | `./tests/crypto_tests` | > 80% new code |
| Core | Integration + unit | `./tests/core_tests` | > 70% critical paths |
| P2P | Network simulation | `./tests/p2p_tests` | Rate limits, handshake, encryption |
| Wallet/RPC | Integration | `./tests/wallet_tests` + `curl` scripts | Auth, encryption, rate limits |
| SwapDaemon | Integration | Manual swap flow | Full swap lifecycle |
| Go (swapxfg) | Unit tests | `cd swapxfg && go test ./...` | > 80% |

### Verification Checklist (per fix)

1. Unit test passes with various inputs (valid, invalid, edge, boundary).
2. Integration test confirms no regression in related functionality.
3. Manual smoke test on testnet before mainnet deployment.
4. Code review by at least one other developer.
5. No new compiler warnings (build with `-Wall -Wextra`).

### Git Workflow

```bash
# Each fix in its own branch off hearth
git checkout -b fix/interest-zero hearth
# ... implement fix ...
git add src/CryptoNoteCore/Currency.cpp
git commit -m "fix: bond interest always returns computed interestLo

Previously calculateInterest computed interest into local variables
but returned the unmodified offchaininterest (always 0).
Change return to interestLo.

Refs: Phase 0.1"

# Push and create PR
git push origin fix/interest-zero
gh pr create --base hearth --title "fix: bond interest always returns zero"
```

### Deployment Sequence

1. **Testnet first** — Deploy all changes to testnet. Run for ≥ 1 week.
2. **Monitor** — Watch logs for unexpected errors, check swap success rate.
3. **Graceful upgrade** — New daemon must accept old peer connections during rollout window.
4. **Mainnet rollout** — Coordinate with node operators. Phase deployment:
   - Day 1: Phase 0 + Phase 1 to 25% of nodes
   - Day 3: Phase 0 + Phase 1 to 100% of nodes
   - Day 7: Phase 2 to 100% of nodes
   - Day 14: Phase 3 to 100% of nodes
   - Day 21: Phase 4 (optional, can bundle with next feature release)

### Dependencies Between Fixes

```
Fix 2.2 (Basic Auth over TLS)   depends on → Fix 2.1 (TLS for RPC)
Fix 2.4 (P2P message HMAC)      depends on → Fix 2.3 (P2P Noise encryption)
Fix 2.3 (P2P Noise)             depends on → Fix 2.5 (connection rate limiter — for DoS during rollout)
Fix 1.9 (SecureString)          used by   → Fix 1.10, Fix 3.24, Phase 4 key zeroization
Fix 1.11 (RPC rate limiting)    depends on → Fix 2.1 (TLS) — rate limit per-TLS-session
Fix 2.12 (AEAD wallet)          used by   → Fix 2.13 (wallet file perms)
```

### Rollback Plan

Any Phase 1 fix can be individually reverted without affecting other fixes (each is self-contained). For consensus-critical fixes (1.1, 1.3, 1.7, 1.8), coordinate rollback across all nodes simultaneously.

---

## Timeline Summary

| Phase | Fixes | Est. Effort | Cumulative |
|-------|-------|-------------|------------|
| Phase 0 | 4 | 1 hour | 1 hour |
| Phase 1 | 13 | 2 weeks | 2 weeks |
| Phase 2 | 23 | 3 weeks | 5 weeks |
| Phase 3 | 24 | 3 weeks | 8 weeks |
| Phase 4 | 7 | 1 week | 9 weeks |

**Total: 71 fixes over ~9 weeks** (single developer, full-time). With 2-3 developers, Phase 1+2 can complete in ~3 weeks.
