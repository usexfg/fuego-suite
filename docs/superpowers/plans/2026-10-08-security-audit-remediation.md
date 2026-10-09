# Security Audit Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the 9 remaining OPEN/LOW/INFO findings from the two-round Fuego security audit, leaving only R2-1 (which needs an owner protocol decision) outstanding.

**Architecture:** Six small, independent changes grouped by subsystem — dead-code deletion (crypto), a secure-zero primitive (crypto), a Rust hygiene fix (swap adapter), a cipher upgrade (swap secrets), a wallet-file cipher bump (wallet), and two consensus hardening items (dead-type arm, epoch-work failure path). Each is self-contained and independently testable; none depends on another. R2-1 is explicitly excluded because it is a protocol change under an active branch (`codex/treasury-vault-auth`).

**Tech Stack:** C++17, OpenSSL 3.x (EVP, HMAC), Rust (`secp256k1` crate), CMake 3.16+, GoogleTest, `make build-release` ground truth.

---

## Scope check

The open findings span three subsystems. Per the scope-check rule, splitting is considered —
but each unit here is small (5-60 lines), independently testable, and shares no interface. They
are grouped into one plan with one task per finding rather than three plans, because splitting
would create three documents for nine one-hour changes. **If you prefer three separate plans,
say so and split at Task 4 (crypto) / Tasks 1-2 (swap) / Tasks 3-6 (consensus+wallet).**

**Excluded:** R2-1 (treasury vault key). CRITICAL, but it is a protocol change requiring an
activation/migration decision, and the concurrent session has an in-flight branch
`codex/treasury-vault-auth`. Do not start it here.

**Target branch:** create `security/audit-fixes-round2` from `master`. Do not work on `master`
directly; a concurrent session is active in this working tree.

---

## File Structure

| File | Responsibility | Task |
|------|----------------|------|
| `src/crypto/oaes_lib.c`, `oaes_lib.h` | Dead third-party AES wrapper; delete unreferenced exports | 1 |
| `src/crypto/crypto-util.c` (new, glob-collected) | Secure-zero primitive | 2 |
| `src/crypto/musig2.cpp` | Consume the new primitive for nonce wipe | 2 |
| `fuego-swapd-adaptor/src/lib.rs` | Secret type must not print itself | 3 |
| `src/SwapDaemon/SwapSecretEncryption.{h,cpp}` | Secret-at-rest cipher upgrade | 4 |
| `src/Wallet/WalletGreen.{h,cpp}` | Wallet-file cipher upgrade + format bump | 5 |
| `src/CryptoNoteCore/Blockchain.cpp` | Dead input-type arm; epoch-work failure path | 6, 7 |

New test files are named in each task. Nothing here restructures an existing large file.

---

### Task 1: Delete dead OAES exports

The OpenAES wrapper ships functions nobody calls. Confirmed by grep: `oaes_sprintf` has only its
definition and header declaration; `oaes_key_gen_128/192/256` have no callers. The sole live
consumer of OAES is CryptoNight (`slow-hash.c`), which calls `oaes_key_import_data` and
`oaes_free` only. `oaes_get_seed` feeds `srand()` used only by the dead key-gen path.

**Files:**
- Modify: `src/crypto/oaes_lib.c` (remove `oaes_sprintf`, `oaes_key_gen*`, `oaes_get_seed`)
- Modify: `src/crypto/oaes_lib.h` (remove their declarations)
- Test: build only (these are compile-time removals; there is no runtime behaviour to assert)

- [ ] **Step 1: Confirm zero callers before deleting**

Run:
```bash
grep -rn "oaes_sprintf\|oaes_key_gen_128\|oaes_key_gen_192\|oaes_key_gen_256" src/ include/ tests/ | grep -v "oaes_lib\.\(c\|h\):"
```
Expected: no output (only `oaes_lib.c`/`.h` lines are filtered out). If any line appears, STOP —
something calls it and this task is wrong.

- [ ] **Step 2: Record the baseline build**

Run:
```bash
cd build/release && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
```
Expected: `Built target ...` with no `error:`.

- [ ] **Step 3: Delete the declarations from the header**

In `src/crypto/oaes_lib.h`, remove the three `oaes_key_gen_*` declarations (they sit together
after `oaes_free`) and the `oaes_sprintf` declaration. Leave every other declaration intact —
`oaes_key_import_data`, `oaes_free`, `oaes_encrypt`, `oaes_decrypt` are all live.

- [ ] **Step 4: Delete the definitions from the source**

In `src/crypto/oaes_lib.c`, remove:
- the whole `oaes_sprintf` function (starts at the `OAES_RET oaes_sprintf(` line)
- `static OAES_RET oaes_key_gen(OAES_CTX * ctx, size_t key_size)` and the three thin
  `oaes_key_gen_128` / `_192` / `_256` wrappers
- BOTH `oaes_get_seed` overloads (the `char buf[RANDSIZ + 1]` ISAAC variant and the
  `uint32_t` variant) — the ISAAC one is inside `#ifdef OAES_HAVE_ISAAC`, the other in `#else`

Leave `srand(oaes_get_seed())` removal alone in `oaes_alloc`: it is inside the `#else` branch of
`OAES_HAVE_ISAAC`. After deleting `oaes_get_seed`, replace that line's branch so it no longer
references the removed function — see Step 5.

- [ ] **Step 5: Deal with the now-dangling `srand` call**

`oaes_alloc` initialises the RNG with `srand(oaes_get_seed())` in the non-ISAAC path. Since the
only consumer of that RNG (`oaes_key_gen`) is gone, replace the `#else` body's
`srand(oaes_get_seed());` with nothing and leave the ISAAC branch intact. Concretely, the block
becomes:

```c
#else
		/* RNG initialisation removed with oaes_key_get_seed: the only consumer
		   (oaes_key_gen) is gone, and CryptoNight imports its key directly. */
#endif
```

- [ ] **Step 6: Build**

Run:
```bash
cd build/release && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:|Error [0-9]"
```
Expected: no output.

- [ ] **Step 7: Run the crypto-dependent tests**

Run:
```bash
cd build/release && ctest --output-on-failure 2>&1 | tail -4
```
Expected: `100% tests passed out of 6`.

- [ ] **Step 8: Commit**

```bash
git add src/crypto/oaes_lib.c src/crypto/oaes_lib.h
git commit -m "refactor(crypto): delete dead OAES exports

oaes_sprintf, oaes_key_gen_128/192/256 and both oaes_get_seed overloads had no
callers. The only live OAES consumer is CryptoNight, which imports its key via
oaes_key_import_data and never generates one. Removing them also removes the
srand() seeding that fed the deleted key-gen path, which was the sole reason a
weak time+pid+heap seed existed in the crypto tree."
```

---

### Task 2: Add a real secure-zero primitive

`src/crypto/crypto-util.h:31` **declares** `sodium_memzero` but nothing defines or calls it.
Result: every secret wipe in the tree is a plain `memset`, which the optimiser is free to
delete once the buffer is dead. The swap daemon already has the correct pattern —
`SwapSecretEncryption::secureZero` uses a `volatile` pointer, which cannot be elided. Promote
that into the crypto library and route the worst offender through it.

**Files:**
- Create: `src/crypto/crypto-util.c` (auto-collected — see Step 5)
- Modify: `src/crypto/crypto-util.h` (declarations already exist)
- Modify: `src/crypto/musig2.cpp` (use it for the nonce wipe)
- Test: `tests/UnitTests/SecureZeroTest.cpp` (new)


- [ ] **Step 1: Write the failing test**

Create `tests/UnitTests/SecureZeroTest.cpp`:

```cpp
// Secure-zero primitive. Plain memset is dead-store-eliminated once the buffer is
// gone, so secret material needs a wipe the compiler must keep. The swap daemon
// already had the right pattern (SwapSecretEncryption::secureZero, volatile
// pointer); this promotes it into the crypto library.

#include <cstring>

#include "gtest/gtest.h"

#include "crypto/crypto-util.h"

TEST(SecureZero, WipesEveryByte) {
  unsigned char buf[64];
  std::memset(buf, 0xAB, sizeof(buf));

  sodium_memzero(buf, sizeof(buf));

  for (size_t i = 0; i < sizeof(buf); ++i) {
    EXPECT_EQ(0, buf[i]) << "byte " << i << " survived the wipe";
  }
}

TEST(SecureZero, WipesPartialRange) {
  unsigned char buf[32];
  std::memset(buf, 0xFF, sizeof(buf));

  sodium_memzero(buf, 8);

  for (size_t i = 0; i < 8; ++i) {
    EXPECT_EQ(0, buf[i]) << "byte " << i << " should have been wiped";
  }
  for (size_t i = 8; i < sizeof(buf); ++i) {
    EXPECT_EQ(0xFF, buf[i]) << "byte " << i << " is outside the range and must be untouched";
  }
}

TEST(SecureZero, ZeroLengthIsANoOp) {
  unsigned char buf[8];
  std::memset(buf, 0x11, sizeof(buf));

  sodium_memzero(buf, 0);

  EXPECT_EQ(0x11, buf[0]);
}

TEST(SecureZero, SurvivesOptimisation) {
  // Compile-time check that the wipe is not dead code: with -O2 the compiler
  // must still emit the stores. Asserting on the assembly is out of scope for a
  // unit test, so this asserts the observable contract that motivates it: a
  // caller cannot get the old bytes back.
  unsigned char buf[128];
  std::memset(buf, 0x5A, sizeof(buf));
  unsigned char copy[128];
  std::memcpy(copy, buf, sizeof(buf));

  sodium_memzero(buf, sizeof(buf));

  EXPECT_NE(0, std::memcmp(buf, copy, sizeof(buf)));
}
```

- [ ] **Step 2: Register the test target**

Append to `tests/CMakeLists.txt`:

```cmake
# Secure-zero primitive for secret material. Plain memset is dead-store-eliminated
# once the buffer is dead, so key wipes need a store the compiler must keep.
add_executable(secure_zero_tests
    UnitTests/SecureZeroTest.cpp
)
target_include_directories(secure_zero_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(secure_zero_tests PRIVATE
    Crypto
    gtest
    gtest_main
)
add_test(NAME secure_zero_tests COMMAND secure_zero_tests)
```

- [ ] **Step 3: Run it to verify it fails**

Run:
```bash
cd build/release && cmake ../.. > /dev/null && cmake --build . --target secure_zero_tests -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:" | head -5
```
Expected: a link error mentioning `sodium_memzero` — the symbol is declared but never defined.

- [ ] **Step 4: Implement the primitive**

Create `src/crypto/crypto-util.c`:

```c
// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#include <stddef.h>

#include "crypto-util.h"

// Wipe that survives dead-store elimination. The volatile-qualified store is the
// whole point: the compiler may not prove these writes are observable and drop
// them, which is exactly what happens to a plain memset on a soon-to-die local.
void sodium_memzero(void *pnt, size_t length) {
  volatile unsigned char *volatile p = (volatile unsigned char *)pnt;
  while (length--) {
    *p++ = 0;
  }
}

// Constant-time equality. Used where a secret or tag is compared, so the compare
// duration does not leak the position of the first difference.
int sodium_compare(const void *a1, const void *a2, size_t length) {
  const unsigned char *b1 = (const unsigned char *)a1;
  const unsigned char *b2 = (const unsigned char *)a2;
  unsigned char d = 0;
  for (size_t i = 0; i < length; ++i) {
    d |= (unsigned char)(b1[i] ^ b2[i]);
  }
  // 0 when equal, -1 otherwise; never 0-vs-nonzero timing signal.
  return (1 & ((d - 1) >> 8)) - 1;
}
```

Note the header declares both `sodium_memzero` and `sodium_compare`; implement both now so the
declaration stops being a lie.

- [ ] **Step 5: Re-run cmake so the glob picks up the new source**

`src/CMakeLists.txt:13` collects crypto with `file(GLOB_RECURSE Crypto crypto/*)`, so
`crypto-util.c` is added automatically — **there is no CMakeLists edit for the crypto library.**
Because it is a glob rather than an explicit list, a newly created file is invisible until cmake
re-configures, so this step is required rather than optional:

```bash
cd build/release && cmake ../.. 2>&1 | tail -2
```
Expected: `Build files have been written to: ...`. Confirm the source landed:
```bash
grep -c "crypto-util.c" build/release/src/CMakeFiles/Crypto.dir/build.make
```
Expected: a non-zero count. If it is zero, the glob has not refreshed — re-run cmake.

- [ ] **Step 6: Run the test to verify it passes**

Run:
```bash
cd build/release && cmake --build . --target secure_zero_tests -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:|Built target secure_zero_tests"
```
Expected: `Built target secure_zero_tests`, no errors.

Run:
```bash
./build/release/tests/secure_zero_tests 2>&1 | tail -4
```
Expected: `[  PASSED  ] 4 tests.`

- [ ] **Step 7: Route the MuSig2 nonce wipe through it**

In `src/crypto/musig2.cpp`, the nonce wipe at line ~371 is a plain `memset`. Change it:

```c
  // Securely erase the secret nonce — MUST NOT be reused. All-zero now marks
  // it spent. sodium_memzero rather than memset: the compiler may drop a memset
  // on a dead buffer, which would leave the nonce recoverable from core dumps.
  sodium_memzero(&sec_nonce, sizeof(Musig2SecNonce));
```

Add the include next to the other crypto includes at the top of `musig2.cpp`:

```cpp
#include "crypto-util.h"
```

- [ ] **Step 8: Full build and full test suite**

Run:
```bash
cd build/release && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:|Error [0-9]"
```
Expected: no output.

Run:
```bash
cd build/release && ctest --output-on-failure 2>&1 | tail -4
```
Expected: `100% tests passed out of 7`.

- [ ] **Step 9: Confirm the wipe is not optimised away**

Disassemble the compiled object and confirm the stores are present:

```bash
objdump -d build/release/src/CMakeFiles/Crypto.dir/crypto/crypto-util.c.o | grep -A 12 "<_sodium_memzero>" | head -16
```
Expected: a loop body containing a `movb $0x0` store. If the symbol is absent or has no store,
the wipe was elided and this task is not actually done.

- [ ] **Step 10: Commit**

```bash
git add src/crypto/crypto-util.c src/crypto/musig2.cpp tests/UnitTests/SecureZeroTest.cpp tests/CMakeLists.txt
git commit -m "fix(crypto): define sodium_memzero and use it for the MuSig2 nonce wipe

sodium_memzero and sodium_compare were declared in crypto-util.h but never
defined or called, so every secret wipe in the tree was a plain memset that the
optimiser is free to delete once the buffer is dead. Defines both, promoting the
volatile-pointer pattern the swap daemon already used in
SwapSecretEncryption::secureZero, and routes the MuSig2 secret-nonce wipe through
it -- a reused or leaked Musig2SecNonce is a full key-extraction, and it sat
64 bytes from a signed partial. Verified the stores survive -O2 by disassembling
the object."
```

---

### Task 3: Stop the Rust adaptor secret from printing itself

`AdaptorSecret` derives `Debug`, so any `{:?}` anywhere prints the raw preimage. No such call
exists today, but this is the swap secret that unlocks the counterparty's funds — a logging
statement added in a hurry would leak it.

**Files:**
- Modify: `fuego-swapd-adaptor/src/lib.rs:48`
- Test: the crate's existing test module (`cargo test`)

- [ ] **Step 1: Confirm the derived set and that nothing depends on it**

Run:
```bash
grep -n "AdaptorSecret" fuego-swapd-adaptor/src/lib.rs
```
Expected: the type is declared with `#[derive(Clone, Debug, PartialEq, Eq)]` and used in the
roundtrip tests.

- [ ] **Step 2: Replace the derive with a redacting impl**

Change the declaration from:

```rust
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AdaptorSecret(pub [u8; 32]);
```

to:

```rust
#[derive(Clone, PartialEq, Eq)]
pub struct AdaptorSecret(pub [u8; 32]);

// Redacted on purpose: this is the swap preimage that unlocks the counterparty's
// funds. Deriving Debug would put the raw scalar in any log line or panic message
// that happens to format it.
impl std::fmt::Debug for AdaptorSecret {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "AdaptorSecret([redacted])")
    }
}
```

- [ ] **Step 3: Verify the crate builds and its tests pass**

Run:
```bash
cd fuego-swapd-adaptor && cargo test 2>&1 | tail -12
```
Expected: all tests pass — `test_adaptor_sig_roundtrip`, `test_wrong_tweak_fails`,
`test_musig2_key_agg`, `test_scalar_negate`, `test_scalar_add`, `test_nonce_combination`.

If the crate does not build in this environment (no toolchain, or it is built only via CMake),
note that in the changelog sign-off instead of silently skipping.

- [ ] **Step 4: Confirm no `{:?}` now breaks and none existed before**

Run:
```bash
grep -rn "{:?}\|{:#?}" fuego-swapd-adaptor/src/
```
Expected: no occurrence targeting `AdaptorSecret`. A redacting `Debug` keeps such code
compiling, so this grep is about intent, not compilation.

- [ ] **Step 5: Commit**

```bash
git add fuego-swapd-adaptor/src/lib.rs
git commit -m "fix(swap): redact AdaptorSecret's Debug output

AdaptorSecret derived Debug, so any {:?} prints the raw 32-byte preimage -- the
value that unlocks the counterparty's side of an atomic swap. Nothing formats it
today, but the footgun is one log statement wide. Replace the derive with a
hand-written impl that prints [redacted]."
```

---

### Task 4: Upgrade swap secret-at-rest encryption off ChaCha8

`SwapSecretEncryption` encrypts `adaptorSecret`, MuSig2 sec nonces, and Monero spend/view keys
at rest. It is Encrypt-then-MAC with HMAC-SHA256 and a constant-time tag compare — the
construction is correct. The primitive is the issue: ChaCha8 is an 8-round variant, roughly a
quarter of ChaCha20's security margin. Fix it in place with ChaCha20-Poly1305, which is already
reachable through the linked OpenSSL EVP.

**Files:**
- Modify: `src/SwapDaemon/SwapSecretEncryption.h` (nonce 8→12, add envelope version)
- Modify: `src/SwapDaemon/SwapSecretEncryption.cpp`
- Test: `src/SwapDaemon/tests/test_production_gates.cpp` (existing coverage of encrypt/decrypt)

- [ ] **Step 1: Run the existing gate test to get a baseline**

Run:
```bash
cd build/release && cmake --build . --target test_production_gates -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:|Built target"
find build/release -name "test_production_gates" -type f -perm +111 | head -1
```
Expected: builds; note the binary path, then run it — expected PASS (this is the pre-change
baseline).

- [ ] **Step 2: Add the envelope version and widen the nonce**

In `src/SwapDaemon/SwapSecretEncryption.h`:

```cpp
constexpr size_t CHACHA8_KEY_SIZE   = 32;
constexpr size_t CHACHA8_NONCE_SIZE = 8;   // -> replaced below
```

becomes:

```cpp
// Envelope version. 1 = legacy ChaCha8 + HMAC-SHA256 EtM; 2 = ChaCha20-Poly1305.
// The version byte is prepended to the stored blob so old records stay readable
// after the upgrade instead of failing to authenticate.
constexpr uint8_t SECRET_ENVELOPE_V1 = 1;
constexpr uint8_t SECRET_ENVELOPE_V2 = 2;

constexpr size_t CHACHA8_KEY_SIZE   = 32;
constexpr size_t CHACHA8_NONCE_SIZE = 8;   // legacy v1 nonce, still needed to read old records
constexpr size_t CHACHA20_NONCE_SIZE = 12;
constexpr size_t SALT_SIZE          = 16;
constexpr size_t TAG_SIZE           = 32;   // legacy v1 HMAC tag
constexpr size_t POLY1305_TAG_SIZE  = 16;
constexpr size_t SECRET_PLAINTEXT   = 32;
```

- [ ] **Step 3: Keep the legacy path for reads only**

In `SwapSecretEncryption.cpp`, rename the existing `encrypt`/`decrypt` bodies to
`encryptV1`/`decryptV1` (private statics) and leave them byte-for-byte unchanged apart from the
names. They exist so records written before the upgrade still load.

- [ ] **Step 4: Implement the v2 encrypt**

Add to `SwapSecretEncryption.cpp`:

```cpp
bool SwapSecretEncryption::encrypt(
    const uint8_t* plaintext,
    size_t plaintextLen,
    const std::string& encryptionKey,
    EncryptedSecret& out) {
  if (!plaintext || plaintextLen == 0 || encryptionKey.empty()) {
    return false;
  }

  // Memory-hard KDF, same as v1: the encryption key is a maker-wallet secret, so
  // a stolen database should still cost real work to attack offline.
  Randomize::randomBytes(SALT_SIZE, out.salt.data());

  std::string saltedKey = encryptionKey;
  saltedKey.append(reinterpret_cast<const char*>(out.salt.data()), SALT_SIZE);

  Crypto::cn_context ctx;
  Crypto::chacha8_key cipherKey;
  Crypto::generate_chacha8_key(ctx, saltedKey, cipherKey);
  std::fill(saltedKey.begin(), saltedKey.end(), '\0');

  // 96-bit nonce, fresh per record. Random is acceptable here because these
  // records are never encrypted twice under the same key.
  unsigned char nonce[CHACHA20_NONCE_SIZE];
  if (RAND_bytes(nonce, sizeof(nonce)) != 1) {
    secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }
  std::memcpy(out.nonce.data(), nonce, CHACHA20_NONCE_SIZE);

  // ChaCha20-Poly1305 AEAD: one primitive, no separate MAC key, no separate tag
  // comparison step to get wrong. AAD binds the version and salt so neither can
  // be swapped between records.
  unsigned char aad[2 + SALT_SIZE];
  aad[0] = SECRET_ENVELOPE_V2;
  aad[1] = SALT_SIZE;
  std::memcpy(aad + 2, out.salt.data(), SALT_SIZE);

  out.ciphertext.resize(plaintextLen + POLY1305_TAG_SIZE);

  EVP_CIPHER_CTX* cctx = EVP_CIPHER_CTX_new();
  if (!cctx) {
    secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  int len = 0, total = 0;
  bool ok = EVP_EncryptInit_ex(cctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1
         && EVP_CIPHER_CTX_ctrl(cctx, EVP_CTRL_AEAD_SET_IVLEN, CHACHA20_NONCE_SIZE, nullptr) == 1
         && EVP_EncryptInit_ex(cctx, nullptr, nullptr, cipherKey.data, out.nonce.data()) == 1
         && EVP_EncryptUpdate(cctx, nullptr, &len, aad, sizeof(aad)) == 1
         && EVP_EncryptUpdate(cctx, out.ciphertext.data(), &len, plaintext, (int)plaintextLen) == 1
         && (total = len,
             EVP_EncryptFinal_ex(cctx, out.ciphertext.data() + total, &len) == 1);

  EVP_CIPHER_CTX_free(cctx);
  secureZero(cipherKey.data, sizeof(cipherKey));

  if (!ok) {
    return false;
  }
  return true;
}
```

- [ ] **Step 5: Implement the v2 decrypt with a v1 fallback**

Add:

```cpp
bool SwapSecretEncryption::decrypt(
    const EncryptedSecret& encrypted,
    const std::string& encryptionKey,
    uint8_t* out,
    size_t outLen) {
  if (!out || outLen == 0) {
    return false;
  }

  // v1 records have no version byte and carry no explicit envelope marker; they
  // are recognised by the 8-byte nonce they were written with. Anything that is
  // not exactly v1-shaped is treated as v2, so a malformed blob cannot silently
  // downgrade into the legacy path with attacker-chosen parameters.
  const bool looksV1 = encrypted.salt.size() == SALT_SIZE
                    && (encrypted.nonce[7] == 0 || encrypted.ciphertext.size() == outLen + TAG_SIZE);
  if (looksV1 && encrypted.ciphertext.size() == outLen + TAG_SIZE) {
    return decryptV1(encrypted, encryptionKey, out, outLen);
  }

  if (encrypted.ciphertext.size() != outLen + POLY1305_TAG_SIZE) {
    return false;
  }

  Randomize::randomBytes(0, nullptr); // no-op; keeps the Randomize include honest
  std::string saltedKey = encryptionKey;
  saltedKey.append(reinterpret_cast<const char*>(encrypted.salt.data()), SALT_SIZE);

  Crypto::cn_context ctx;
  Crypto::chacha8_key cipherKey;
  Crypto::generate_chacha8_key(ctx, saltedKey, cipherKey);
  std::fill(saltedKey.begin(), saltedKey.end(), '\0');

  unsigned char aad[2 + SALT_SIZE];
  aad[0] = SECRET_ENVELOPE_V2;
  aad[1] = SALT_SIZE;
  std::memcpy(aad + 2, encrypted.salt.data(), SALT_SIZE);

  EVP_CIPHER_CTX* dctx = EVP_CIPHER_CTX_new();
  if (!dctx) {
    secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  int len = 0;
  bool ok = EVP_DecryptInit_ex(dctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1
         && EVP_CIPHER_CTX_ctrl(dctx, EVP_CTRL_AEAD_SET_IVLEN, CHACHA20_NONCE_SIZE, nullptr) == 1
         && EVP_DecryptInit_ex(dctx, nullptr, nullptr, cipherKey.data, encrypted.nonce.data()) == 1
         && EVP_DecryptUpdate(dctx, nullptr, &len, aad, sizeof(aad)) == 1
         && EVP_DecryptUpdate(dctx, out, &len, encrypted.ciphertext.data(), (int)outLen) == 1;

  if (ok) {
    int finalLen = 0;
    // The tag check is the authority: a wrong key or a flipped byte fails here.
    ok = EVP_DecryptFinal_ex(dctx, out + len, &finalLen) == 1;
  }

  EVP_CIPHER_CTX_free(dctx);
  secureZero(cipherKey.data, sizeof(cipherKey));

  if (!ok) {
    // Do not hand back unauthenticated plaintext.
    std::fill(out, out + outLen, 0);
    return false;
  }
  return true;
}
```

Add `#include <openssl/rand.h>` to the includes.

- [ ] **Step 6: Update the callers to pass the version byte**

`SwapStateMachine.cpp` packs and unpacks the envelope by concatenating
`nonce || salt || ciphertext || tag`. Change both `packEncrypted` and `packEncryptedBytes` to
prepend the version byte:

```cpp
    encData.push_back(static_cast<char>(SECRET_ENVELOPE_V2));
```

and both unpack paths to read and branch on it, routing `SECRET_ENVELOPE_V1` to the legacy
decoder.

- [ ] **Step 7: Build and run the gate test**

Run:
```bash
cd build/release && cmake --build . --target test_production_gates -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:|Built target"
```
Expected: builds clean.

Run the binary. Expected: PASS, including the existing "wrong encryption key is rejected" case —
that is the check that proves the new AEAD path actually authenticates.

- [ ] **Step 8: Full build and suite**

Run:
```bash
cd build/release && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:|Error [0-9]"
cd build/release && ctest --output-on-failure 2>&1 | tail -4
```
Expected: no build errors; `100% tests passed`.

- [ ] **Step 9: Commit**

```bash
git add src/SwapDaemon/SwapSecretEncryption.h src/SwapDaemon/SwapSecretEncryption.cpp src/SwapDaemon/SwapStateMachine.cpp
git commit -m "fix(swap): move swap secret-at-rest to ChaCha20-Poly1305

The swap daemon encrypts the adaptor preimage, the MuSig2 secret nonce and the
Monero spend/view keys at rest. The Encrypt-then-MAC construction was already
correct -- HMAC-SHA256 over nonce||salt||ciphertext, constant-time tag compare --
but the cipher was ChaCha8, an 8-round variant with roughly a quarter of
ChaCha20's margin.

Switch to ChaCha20-Poly1305 through the already-linked OpenSSL EVP: one AEAD
instead of a cipher plus a hand-rolled MAC, and the tag check is the authority so
a wrong key cannot yield plaintext. The envelope gains a version byte and records
written by v1 still decrypt through the retained legacy path."
```

---

### Task 5: Upgrade wallet-file encryption and bump the format version

Same primitive issue as Task 4, on the file that holds spend keys for the whole wallet. The
existing construction is sound — separate HMAC-derived MAC key with a domain label, constant-time
`CRYPTO_memcmp` verify, fail-closed — so this is a cipher swap plus a format bump, not a redesign.

**Files:**
- Modify: `src/Wallet/WalletGreen.cpp:2277-2308` (`encryptKeyPair`), and `decryptKeyPair`
- Modify: the wallet-file version constant consumed by `loadWallet`/`saveWalletCache`
- Test: `tests/UnitTests/TestWallet.cpp` (existing wallet round-trip)

- [ ] **Step 1: Locate the wallet-file version handling**

Run:
```bash
grep -rn "CURRENT_WALLET_VERSION\|WALLET_VERSION" src/Wallet/WalletGreen.cpp | head -8
```
Expected: a version constant and comparisons in the load path. Note the current value — this is
what you bump.

- [ ] **Step 2: Record a baseline: existing wallet round-trip**

Run:
```bash
cd build/release && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:" | head -3
ctest --output-on-failure 2>&1 | tail -4
```
Expected: current suite green. This is the "does my change break wallets" baseline.

- [ ] **Step 3: Add the v2 encrypt alongside the existing one**

In `WalletGreen.cpp`, next to `encryptKeyPair`, add:

```cpp
  EncryptedWalletRecord WalletGreen::encryptKeyPairV2(const PublicKey &publicKey,
                                                      const SecretKey &secretKey,
                                                      uint64_t creationTimestamp,
                                                      const Crypto::chacha8_key &key,
                                                      const Crypto::chacha8_iv &iv) {

    std::string serializedKeys;
    StringOutputStream outputStream(serializedKeys);
    BinaryOutputStreamSerializer serializer(outputStream);

    serializer(const_cast<PublicKey &>(publicKey), "publicKey");
    serializer(const_cast<SecretKey &>(secretKey), "secretKey");
    serializer.binary(&creationTimestamp, sizeof(uint64_t), "creationTimestamp");

    // ChaCha20 needs a 12-byte nonce; the wallet's per-record IV counter is 8
    // bytes. Zero-extend: the counter stays unique, which is what the nonce needs,
    // and the high 4 bytes are constant so no state is spent on them.
    unsigned char nonce[CHACHA20_NONCE_SIZE] = {0};
    std::memcpy(nonce, iv.data, sizeof(iv.data));

    unsigned char aad[8];
    std::memcpy(aad, iv.data, sizeof(iv.data));
    uint32_t recordVersion = WALLET_KEY_RECORD_VERSION_V2;
    std::memcpy(aad + 4, &recordVersion, sizeof(recordVersion));

    out.size() = serializedKeys.size() + POLY1305_TAG_SIZE;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    bool ok = ctx != nullptr
           && EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1
           && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, CHACHA20_NONCE_SIZE, nullptr) == 1
           && EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data, nonce) == 1
           && EVP_EncryptUpdate(ctx, nullptr, &len, aad, sizeof(aad)) == 1
           && EVP_EncryptUpdate(ctx, out.data(), &len,
                                (const unsigned char *)serializedKeys.data(),
                                (int)serializedKeys.size()) == 1
           && EVP_EncryptFinal_ex(ctx, out.data() + len, &len) == 1;
    if (ctx) { EVP_CIPHER_CTX_free(ctx); }

    // Wipe the plaintext buffer now that it is ciphertext.
    std::fill(serializedKeys.begin(), serializedKeys.end(), '\0');

    if (!ok) {
      throw std::runtime_error("Wallet key record encryption failed");
    }
    return out;
  }
```

Use the file's actual member name for the output record (`out` in the snippet is a placeholder —
read the surrounding `encryptKeyPair` and match it exactly).

- [ ] **Step 4: Add the v2 decrypt with a v1 fallback, fail-closed**

Mirror `encryptKeyPairV2` with `EVP_Decrypt*`. On `EVP_DecryptFinal_ex` failure, zero the output
buffer and throw — mirroring the existing v1 behaviour at `WalletGreen.cpp:2255`, which rejects on
a tag mismatch. Select v1 vs v2 by the record version byte written in Step 3.

- [ ] **Step 5: Bump the wallet-file version and keep the v1 read path**

Bump the constant found in Step 1 by one. `loadWallet` must accept both the old and new version
and dispatch accordingly, so existing wallet files keep opening. `saveWalletCache` writes the
new version only.

- [ ] **Step 6: Build**

Run:
```bash
cd build/release && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:|Error [0-9]"
```
Expected: no output.

- [ ] **Step 7: Round-trip test, including backwards compatibility**

Run the wallet tests. Expected: PASS.

Then verify the compatibility claim explicitly, because "old wallets still open" is the whole
risk of this task: create a wallet file with the pre-change code (check out the previous commit's
binary or keep a v1 fixture), then open it with the new code and confirm the balance and keys
match. If no v1 fixture exists, write one: hardcode a small v1-format wallet file under
`tests/data/` and assert it loads. Do not skip this — a format bump that cannot read the format
it replaced is a wallet-loss bug.

- [ ] **Step 8: Commit**

```bash
git add src/Wallet/WalletGreen.cpp src/Wallet/WalletGreen.h
git commit -m "fix(wallet): move wallet key records to ChaCha20-Poly1305

Wallet key records were encrypted with ChaCha8. The surrounding AEAD was already
correct -- HMAC-SHA256 with a separately derived, domain-separated MAC key and a
constant-time CRYPTO_memcmp tag check that fails closed -- so this is a cipher
swap, not a redesign.

The 8-byte per-record IV counter is zero-extended to ChaCha20's 12-byte nonce,
which preserves the uniqueness the nonce depends on. The wallet-file version is
bumped and the v1 read path is retained, so existing wallet files still open."
```

---

### Task 6: Resolve the dead `TransactionInputUnified` arm

`checkTransactionInputs` rejects `Unified` as an unsupported type, but `pushTransaction`'s
vault-gate loop still handles it (`Blockchain.cpp:5993-5997`), and `CryptoNoteSerialization`
tags it `0x5`. Today that is fail-closed and harmless. It is a hazard the day someone activates
the type: validation and connect would disagree, and the connect-side branch would run with no
validation behind it.

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp:5993-5997`
- Test: none needed (removal of a dead branch; covered by the existing suite)

- [ ] **Step 1: Confirm the type really is unreachable**

Run:
```bash
grep -rn "TransactionInputUnified" src/CryptoNoteCore/ | grep -v "^src/CryptoNoteCore/CryptoNoteSerialization"
```
Expected: hits only in the vault-gate loop and the asset-classification helper. If validation
ever grows a real `Unified` branch, this task is wrong — stop and re-read.

- [ ] **Step 2: Delete the dead connect-side branch**

In `Blockchain.cpp`, remove this arm from the vault-spend detection loop:

```cpp
      } else if (inv.type() == typeid(TransactionInputUnified)) {
        if (m_vault.isVaultKeyImage(boost::get<TransactionInputUnified>(inv).keyImage)) {
          isVaultSpend = true; break;
        }
      }
```

leaving the `KeyInput` and `CommitmentSpend` arms. Then make the unsupported-type rejection in
`checkTransactionInputs` name the type explicitly, so the log tells an operator what arrived:

```cpp
      else
      {
        logger(INFO, BRIGHT_WHITE) << "Transaction << " << transactionHash
            << " contains input of unsupported type: " << typeid(txin).name();
        return false;
      }
```

- [ ] **Step 3: Build and test**

Run:
```bash
cd build/release && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:|Error [0-9]"
cd build/release && ctest --output-on-failure 2>&1 | tail -4
```
Expected: no build errors; suite green.

- [ ] **Step 4: Commit**

```bash
git add src/CryptoNoteCore/Blockchain.cpp
git commit -m "fix(consensus): drop the dead TransactionInputUnified connect branch

checkTransactionInputs rejects Unified as an unsupported input type, so the
vault-gate branch in pushTransaction that handled it could never run. Harmless
today, and a trap the day the type is activated: connect would then execute a
vault-spend classification with no validation behind it. Removing the branch and
naming the type in the rejection log."
```

---

### Task 7: Do not return false from mid-mutation epoch work

`processBlockEpochWork` bails out with `return false` at `Blockchain.cpp:5104` (bonus-vault
overflow) after epoch accounting has begun mutating state. On the cache-rebuild path
(`:1233-1236`) that `false` is only logged, and the rebuild loop `return`s — leaving the node
running on a half-rebuilt cache and serving wrong balances.

This is the same shape as M-1 (fixed in round 1): an early return out of a mutating function.
It is unreachable in practice (the guard needs `bonusHeat` near `UINT64_MAX`), so this task is
hardening, not a live-bug fix. Keep it small.

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp:1233-1236` and `:5102-5105`
- Test: none practical (the trigger is unreachable); assert via the existing suite staying green

- [ ] **Step 1: Confirm the trigger is effectively unreachable, so this stays hardening**

Read the guard at `:5102`. It fires only when `m_bonusVaultBalance > UINT64_MAX - bonusHeat`,
where `bonusHeat` is derived from an epoch fee share times a pool rate. Record the arithmetic in
a comment so the next reader does not have to re-derive it.

- [ ] **Step 2: Make the cache-rebuild path fail loudly instead of silently**

Replace the silent `return` at `:1233-1236`:

```cpp
      if (!processBlockEpochWork(block.bl, block.height, blockHash)) {
        logger(ERROR, BRIGHT_RED)
          << "Cache rebuild replay epoch work failed at height " << b
          << " -- epoch accounting is incomplete; refusing to serve this cache";
        // A partially-rebuilt cache yields wrong balances and wrong APY for every
        // query that follows, which is worse than not answering. Clear the cache
        // so the next request rebuilds from genesis.
        m_blocks.clear();
        m_blockIndex = BlockIndex{};
        m_difficulty = 0;
        return;
      }
```

Match the exact member names and types from the surrounding code before editing — the reset in
this snippet is indicative and must be adjusted to what the class actually holds.

- [ ] **Step 3: Build and test**

Run:
```bash
cd build/release && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep -E "error:|Error [0-9]"
cd build/release && ctest --output-on-failure 2>&1 | tail -4
cd build/release && ./src/test_daemons/core_tests 2>&1 | grep -E "Passed:|Failed:"
```
Expected: no build errors; suite green; `Passed: 202 / 202`.

- [ ] **Step 4: Commit**

```bash
git add src/CryptoNoteCore/Blockchain.cpp
git commit -m "fix(consensus): fail loudly on epoch-work failure during cache rebuild

processBlockEpochWork returns false part-way through epoch accounting on the
bonus-vault overflow guard. On the cache-rebuild path that was logged and then
returned from the rebuild loop, leaving the node serving a half-rebuilt cache --
wrong balances and wrong APY for every query after it. Discard the cache instead
so the next request rebuilds cleanly. Same shape as the round-1 M-1 finding:
an early return out of a mutating function."
```

---

### Task 8: Correct the stale `fuego-currency` skill documentation

Not a code change, but a live hazard: the skill documents an 80/20 swap-fee split where the code
is 69/11/20 (`CryptoNoteConfig.h:260,265,285`), and presents the treasury 60/40
sub-allocation as if it were the fee split. An agent that trusts it will "correct" working
revenue routing. This audit nearly made that mistake.

**Files:**
- Modify: `~/.config/opencode/skills/fuego-currency/SKILL.md`
- Modify: `~/.opencode/skills/fuego-currency/SKILL.md` (keep the two copies identical)

- [ ] **Step 1: Confirm the code values are the ones to document**

Run:
```bash
grep -nE "SWAP_FEE_CD_SHARE_PCT|SWAP_FEE_BONUS_VAULT_PCT|SWAP_FEE_TREASURY_SHARE_PCT|TREASURY_LP_PCT|TREASURY_RESERVE_PCT|MINT_BURN_EF_PCT" src/CryptoNoteConfig.h -A1
```
Expected: 69, 11, 20, 60, 40, 50. Those are the numbers the docs must state.

- [ ] **Step 2: Fix the fee-pool distribution section**

Replace the skill's 80/20 split description with the actual routing, and be explicit that 60/40
is a split *of the treasury slice*, not of the fee:

```
**Swap fee split (src/CryptoNoteConfig.h, 69/11/20 = 100):**
  m_currentEpochSwapFees (cross-chain atomic swap protocol fees)
    ├─ 69% → m_cdYieldPool            (SWAP_FEE_CD_SHARE_PCT)
    ├─ 11% → BONUS_VAULT              (SWAP_FEE_BONUS_VAULT_PCT)
    └─ 20% → treasury                 (SWAP_FEE_TREASURY_SHARE_PCT)

**Treasury sub-allocation — this is a split OF THE 20%, not of the fee:**
  treasury
    ├─ 60% → LP Reserve               (TREASURY_LP_PCT)
    └─ 40% → Reserve Balance          (TREASURY_RESERVE_PCT)
```

- [ ] **Step 3: Fix the constants table and the formulas**

In the skill's constants table, replace `SWAP_FEE_CD_SHARE_PCT = 80` with `69`, drop the
nonexistent 80/20 `cdShare` formula in favour of the three-way split above, and correct the
"HEAT Treasury: Mint burn 50/50 split, LP reserve (60%), peg defense (40%)" line to say LP
reserve 60% / reserve balance 40% — there is no "peg defense" bucket at this constant set.

- [ ] **Step 4: Fix the `estimate_cd_apy` example**

The skill's example uses `(0.8 × epoch_swap_fees × 73)`. Replace `0.8` with the CD pool's share
of the CD yield, and state that on-chain AMM fees flow 100% to the CD pool while only the
cross-chain swap fees are split. The `swapFees` field in epoch reports tracks only the
cross-chain component — that part of the skill is already correct, keep it.

- [ ] **Step 5: Add a staleness warning at the top**

Prepend to the skill file:

```markdown
> **Verify constants against source before trusting this document.** The fee
> splits and percentages below were stale (documenting 80/20 where the code is
> 69/11/20) and were corrected on 2026-10-08 during a security audit. Treat
> `src/CryptoNoteConfig.h` as the single source of truth, not this file.
```

- [ ] **Step 6: Keep both copies identical**

Run:
```bash
diff ~/.config/opencode/skills/fuego-currency/SKILL.md ~/.opencode/skills/fuego-currency/SKILL.md && echo "IDENTICAL"
```
Expected: `IDENTICAL`. If not, copy the corrected file over the other.

- [ ] **Step 7: Commit (docs only, no code)**

This lives in the dotfile repo, not this repository. If the skills directory is tracked
separately, commit there; otherwise record the change in `CHANGELOG.agent.md` instead:

```bash
git add CHANGELOG.agent.md
git commit -m "docs: correct stale fee-split constants in the fuego-currency skill

The skill documented an 80/20 swap-fee split and a 60/40 treasury split; the
code is 69/11/20, with 60/40 applying only to the treasury sub-allocation. An
agent trusting the stale doc would have reverted working revenue routing."
```

---

## Final verification checklist

Run before declaring this plan done:

- [ ] `cd build/release && cmake --build . -j$(sysctl -n hw.ncpu)` — no errors
- [ ] `cd build/release && ctest --output-on-failure` — all pass
- [ ] `./build/release/src/test_daemons/core_tests` — `Passed: 202 / 202`, exit 0
- [ ] `objdump` on `crypto-util.c.o` shows the wipe stores survive `-O2` (Task 2)
- [ ] A pre-change wallet file still opens under the new code (Task 5) — **do not skip**
- [ ] `cargo test` in `fuego-swapd-adaptor` passes (Task 3)
- [ ] Both `fuego-currency` skill copies identical (Task 8)
- [ ] `CHANGELOG.agent.md` has a task list and sign-off table per finding, per `AGENTS.md`
- [ ] `git status` shows only intended files; no secrets committed; no stashes touched

## Explicitly out of scope

- **R2-1** (treasury vault spend key derivable from public data) — CRITICAL, protocol change,
  active on `codex/treasury-vault-auth`. Needs an owner decision first.
- R2-3 (fee-split integer dust) — leaving up to 2 atomic units per epoch unallocated is
  intentional-looking and not worth a consensus-visible arithmetic change.
- I-2 (popen in the Tor/I2P probes) — all commands are string literals; PATH-hijack needs a local
  attacker. Optional hardening.
- I-3 (`PointTimelock.sol` permissionless refund, id-squat) — design choices to document, not bugs.
- Round 3 scope (OSPEAD bias, MLSAG/RingCT, epoch-mint dual paths, difficulty arithmetic, Go/TUI
  fee splits, fuzzing) — listed in `docs/SECURITY_AUDIT_FINDINGS.md` under "Not covered".
