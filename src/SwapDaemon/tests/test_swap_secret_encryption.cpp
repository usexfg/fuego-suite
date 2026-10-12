// At-rest encryption for swap secrets (audit L-2).
//
// The defect under test: SwapSecretEncryption encrypted with ChaCha8, an
// 8-round variant, so roughly a quarter of ChaCha20's security margin. The
// surrounding construction was correct -- Encrypt-then-MAC with HMAC-SHA256 and
// a constant-time tag compare -- which is why this is a cipher upgrade and not
// a redesign.
//
// The upgrade's real risk is not the new cipher, it is the format change. A swap
// interrupted across an upgrade must still recover its adaptor secret or the
// funds sit locked until the timelock, so v1 records must keep decrypting. The
// v1 writer below is reimplemented from the pre-change code, so the fixture is
// genuinely pre-upgrade data rather than something the new code produced.

#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include <iostream>
#include <string>
#include <vector>

#include "SwapDaemon/SwapSecretEncryption.h"
#include "crypto/chacha8.h"
#include "crypto/crypto.h"
#include "crypto/randomize.h"

using namespace XfgSwap;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
  if (cond) { ++g_pass; std::cout << "  PASS: " << msg << "\n"; } \
  else { ++g_fail; std::cerr << "  FAIL: " << msg << "\n"; } \
} while (0)

static const char* kKey   = "test-encryption-key-material";
static const char* kOther = "a-different-key";

static Crypto::SecretKey secretWithTag(uint8_t tag) {
  Crypto::SecretKey s = {};
  for (size_t i = 0; i < sizeof(s.data); ++i) {
    s.data[i] = static_cast<uint8_t>(tag + i);
  }
  return s;
}

static bool sameSecret(const Crypto::SecretKey& a, const Crypto::SecretKey& b) {
  return std::memcmp(a.data, b.data, sizeof(a.data)) == 0;
}

// ── v2 round trips ──────────────────────────────────────────────────────────

static void roundTripsASecret() {
  Crypto::SecretKey in = secretWithTag(0x11);
  SwapSecretEncryption::EncryptedSecret enc;
  CHECK(SwapSecretEncryption::encrypt(in, kKey, enc), "encrypt succeeds");
  CHECK(enc.version == SECRET_ENVELOPE_V2, "writes the v2 envelope");

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(SwapSecretEncryption::decrypt(enc, kKey, out), "decrypt succeeds");
  CHECK(sameSecret(in, out), "round trip preserves the secret");
}

static void roundTripsAVariableLengthSecret() {
  // The MuSig2 sec nonce is 64 bytes. The old unpack lambda hardcoded 32, which
  // is exactly what forced the duplicated parser.
  std::vector<uint8_t> in(64);
  for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<uint8_t>(i * 3);

  SwapSecretEncryption::EncryptedSecret enc;
  CHECK(SwapSecretEncryption::encrypt(in.data(), in.size(), kKey, enc),
        "encrypt succeeds for a 64-byte secret");

  std::vector<uint8_t> out(64, 0xFF);
  CHECK(SwapSecretEncryption::decrypt(enc, kKey, out.data(), out.size()),
        "decrypt succeeds for a 64-byte secret");
  CHECK(std::memcmp(in.data(), out.data(), in.size()) == 0,
        "round trip preserves a 64-byte secret");
}

// ── authentication ──────────────────────────────────────────────────────────

static void rejectsTheWrongKey() {
  Crypto::SecretKey in = secretWithTag(0x22);
  SwapSecretEncryption::EncryptedSecret enc;
  SwapSecretEncryption::encrypt(in, kKey, enc);

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(!SwapSecretEncryption::decrypt(enc, kOther, out),
        "the wrong key must fail to decrypt");
}

static void rejectsATamperedCiphertext() {
  Crypto::SecretKey in = secretWithTag(0x33);
  SwapSecretEncryption::EncryptedSecret enc;
  SwapSecretEncryption::encrypt(in, kKey, enc);
  enc.ciphertext[0] ^= 0x01;

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(!SwapSecretEncryption::decrypt(enc, kKey, out),
        "a single flipped ciphertext bit must fail authentication");
}

// The tail, not the head, is what distinguishes a real AEAD from a MAC over a
// prefix. A tag computed over only part of the buffer would leave the head
// authenticated and the tail silently unchecked -- so tampering at index 0
// (the single case that used to exist) proves nothing about the far end.
//
// This is also why the sweep is TAIL-SCOPED rather than every byte: the
// CryptoNight KDF costs ~134 ms per encrypt+decrypt, so an exhaustive sweep of
// all 76 field bytes costs ~10 s and a wrong-key sweep costs ~80 s. Both
// measured, neither affordable in a suite that runs in 39 s total. Tail
// positions cover the bug class at ~1/25th of the cost.
static void rejectsATamperedTail() {
  Crypto::SecretKey in = secretWithTag(0x1A);

  struct Case { std::string name; std::function<void(SwapSecretEncryption::EncryptedSecret&)> f; };
  std::vector<Case> cases;

  cases.push_back(Case{"last ciphertext byte (== last tag byte)",
                       [](SwapSecretEncryption::EncryptedSecret& e) { e.ciphertext.back() ^= 0x01; }});
  // Every bit of that last byte, so a single-bit tag error cannot slip through.
  for (int bit = 0; bit < 8; ++bit) {
    const int b = bit;
    cases.push_back(Case{std::string("last ciphertext byte, bit ") + std::to_string(b),
                         [b](SwapSecretEncryption::EncryptedSecret& e) {
                           e.ciphertext.back() ^= (uint8_t)(1 << b);
                         }});
  }
  cases.push_back(Case{"last nonce byte",
                       [](SwapSecretEncryption::EncryptedSecret& e) { e.nonce.back() ^= 0x01; }});
  cases.push_back(Case{"last salt byte",
                       [](SwapSecretEncryption::EncryptedSecret& e) { e.salt.back() ^= 0x01; }});

  for (const auto& c : cases) {
    SwapSecretEncryption::EncryptedSecret enc;
    SwapSecretEncryption::encrypt(in, kKey, enc);
    c.f(enc);
    Crypto::SecretKey out = secretWithTag(0xFF);
    CHECK(!SwapSecretEncryption::decrypt(enc, kKey, out),
          std::string("tampering with the ").append(c.name).append(" must be rejected").c_str());
  }
}

static void rejectsATamperedNonce() {
  Crypto::SecretKey in = secretWithTag(0x44);
  SwapSecretEncryption::EncryptedSecret enc;
  SwapSecretEncryption::encrypt(in, kKey, enc);
  enc.nonce[0] ^= 0x01;

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(!SwapSecretEncryption::decrypt(enc, kKey, out),
        "a tampered nonce must fail authentication");
}

static void rejectsATamperedSalt() {
  Crypto::SecretKey in = secretWithTag(0x55);
  SwapSecretEncryption::EncryptedSecret enc;
  SwapSecretEncryption::encrypt(in, kKey, enc);
  enc.salt[0] ^= 0x01;

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(!SwapSecretEncryption::decrypt(enc, kKey, out),
        "a tampered salt must fail authentication");
}

// The version byte is bound as AAD, so rewriting v2 -> v1 must not silently
// reach the legacy decoder.
static void rejectsAVersionDowngrade() {
  Crypto::SecretKey in = secretWithTag(0x66);
  SwapSecretEncryption::EncryptedSecret enc;
  SwapSecretEncryption::encrypt(in, kKey, enc);
  enc.version = SECRET_ENVELOPE_V1;

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(!SwapSecretEncryption::decrypt(enc, kKey, out),
        "a v2 record relabelled as v1 must be rejected, not downgraded");
}

static void rejectsAnUnknownVersion() {
  Crypto::SecretKey in = secretWithTag(0x77);
  SwapSecretEncryption::EncryptedSecret enc;
  SwapSecretEncryption::encrypt(in, kKey, enc);
  enc.version = 0xEE;

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(!SwapSecretEncryption::decrypt(enc, kKey, out),
        "an unknown version must fail closed rather than be guessed");
}

// Nonce reuse under ChaCha20 breaks confidentiality and authenticity.
static void usesAFreshNoncePerRecord() {
  Crypto::SecretKey in = secretWithTag(0x88);
  SwapSecretEncryption::EncryptedSecret a, b;
  SwapSecretEncryption::encrypt(in, kKey, a);
  SwapSecretEncryption::encrypt(in, kKey, b);

  CHECK(std::memcmp(a.nonce.data(), b.nonce.data(), CHACHA20_NONCE_SIZE) != 0,
        "nonce differs between two records of the same secret");
  CHECK(std::memcmp(a.salt.data(), b.salt.data(), SALT_SIZE) != 0,
        "salt differs between two records");
  CHECK(a.ciphertext != b.ciphertext,
        "identical plaintext does not produce identical ciphertext");
}

// ── envelope encode / decode ────────────────────────────────────────────────

static void encodeDecodeRoundTripsTheEnvelope() {
  Crypto::SecretKey in = secretWithTag(0x99);
  SwapSecretEncryption::EncryptedSecret enc;
  SwapSecretEncryption::encrypt(in, kKey, enc);

  const std::vector<uint8_t> blob = SwapSecretEncryption::encode(enc);
  CHECK(!blob.empty() && blob[0] == SECRET_ENVELOPE_V2,
        "the version byte leads the envelope");

  SwapSecretEncryption::EncryptedSecret dec;
  CHECK(SwapSecretEncryption::decode(blob, SECRET_PLAINTEXT, dec),
        "a well-formed envelope decodes");
  CHECK(dec.version == enc.version, "version survives the round trip");
  CHECK(dec.ciphertext == enc.ciphertext, "ciphertext survives the round trip");

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(SwapSecretEncryption::decrypt(dec, kKey, out),
        "the decoded envelope still decrypts");
  CHECK(sameSecret(in, out), "the full envelope round trip preserves the secret");
}

static void encodeDecodeRoundTripsAVariableLengthEnvelope() {
  std::vector<uint8_t> in(64);
  for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<uint8_t>(i ^ 0x5A);

  SwapSecretEncryption::EncryptedSecret enc;
  SwapSecretEncryption::encrypt(in.data(), in.size(), kKey, enc);

  const std::vector<uint8_t> blob = SwapSecretEncryption::encode(enc);
  SwapSecretEncryption::EncryptedSecret dec;
  CHECK(SwapSecretEncryption::decode(blob, in.size(), dec),
        "a 64-byte envelope decodes");

  std::vector<uint8_t> out(64, 0xFF);
  CHECK(SwapSecretEncryption::decrypt(dec, kKey, out.data(), out.size()),
        "a 64-byte envelope still decrypts");
  CHECK(std::memcmp(in.data(), out.data(), in.size()) == 0,
        "the 64-byte envelope round trip preserves the secret");
}

static void decodeRejectsATruncatedBlob() {
  Crypto::SecretKey in = secretWithTag(0xAA);
  SwapSecretEncryption::EncryptedSecret enc;
  SwapSecretEncryption::encrypt(in, kKey, enc);

  std::vector<uint8_t> blob = SwapSecretEncryption::encode(enc);
  blob.pop_back();

  SwapSecretEncryption::EncryptedSecret dec;
  CHECK(!SwapSecretEncryption::decode(blob, SECRET_PLAINTEXT, dec),
        "a truncated envelope must be rejected");
}

// A record written for a 32-byte secret must not be accepted as a 64-byte one.
// This is the exact class of bug the old hardcoded-length unpacker had.
static void decodeRejectsAWrongPlaintextLength() {
  Crypto::SecretKey in = secretWithTag(0xBB);
  SwapSecretEncryption::EncryptedSecret enc;
  SwapSecretEncryption::encrypt(in, kKey, enc);
  const std::vector<uint8_t> blob = SwapSecretEncryption::encode(enc);

  SwapSecretEncryption::EncryptedSecret dec;
  CHECK(!SwapSecretEncryption::decode(blob, 64, dec),
        "a 32-byte record must not decode as 64 bytes");
}

// ── v1 backward compatibility ───────────────────────────────────────────────

// The ORIGINAL v1 writer: ChaCha8 + HMAC-SHA256 EtM, no version byte.
static SwapSecretEncryption::EncryptedSecret makeLegacyV1(
    const Crypto::SecretKey& plaintext, const std::string& key) {
  SwapSecretEncryption::EncryptedSecret enc;
  enc.version = SECRET_ENVELOPE_V1;
  Randomize::randomBytes(SALT_SIZE, enc.salt.data());

  std::string saltedKey = key;
  saltedKey.append(reinterpret_cast<const char*>(enc.salt.data()), SALT_SIZE);

  Crypto::cn_context ctx;
  Crypto::chacha8_key cipherKey;
  Crypto::generate_chacha8_key(ctx, saltedKey, cipherKey);

  Crypto::chacha8_iv iv;
  Randomize::randomBytes(CHACHA8_NONCE_SIZE, iv.data);
  std::memcpy(enc.nonce.data(), iv.data, CHACHA8_NONCE_SIZE);

  enc.ciphertext.resize(sizeof(plaintext.data));
  Crypto::chacha8(plaintext.data, sizeof(plaintext.data), cipherKey, iv,
                  reinterpret_cast<char*>(enc.ciphertext.data()));

  std::string tagInput(reinterpret_cast<const char*>(enc.nonce.data()), CHACHA8_NONCE_SIZE);
  tagInput.append(reinterpret_cast<const char*>(enc.salt.data()), SALT_SIZE);
  tagInput.append(reinterpret_cast<const char*>(enc.ciphertext.data()), enc.ciphertext.size());

  unsigned int tagLen = TAG_SIZE;
  HMAC(EVP_sha256(), cipherKey.data, CHACHA8_KEY_SIZE,
       reinterpret_cast<const unsigned char*>(tagInput.data()), tagInput.size(),
       enc.tag.data(), &tagLen);
  return enc;
}

static void stillDecryptsAV1Record() {
  // The upgrade must not strand a swap that was mid-flight across it.
  Crypto::SecretKey in = secretWithTag(0xCC);
  SwapSecretEncryption::EncryptedSecret v1 = makeLegacyV1(in, kKey);

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(SwapSecretEncryption::decrypt(v1, kKey, out), "a v1 record still decrypts");
  CHECK(sameSecret(in, out), "a v1 record yields the original secret");
}

static void aV1RecordWithTheWrongKeyStillFails() {
  // Backward compatibility must not become an authentication bypass.
  Crypto::SecretKey in = secretWithTag(0xDD);
  SwapSecretEncryption::EncryptedSecret v1 = makeLegacyV1(in, kKey);

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(!SwapSecretEncryption::decrypt(v1, kOther, out),
        "a v1 record must still reject the wrong key");
}

static void aV1TamperedRecordStillFails() {
  Crypto::SecretKey in = secretWithTag(0xEE);
  SwapSecretEncryption::EncryptedSecret v1 = makeLegacyV1(in, kKey);
  v1.ciphertext[3] ^= 0x01;

  Crypto::SecretKey out = secretWithTag(0xFF);
  CHECK(!SwapSecretEncryption::decrypt(v1, kKey, out),
        "a tampered v1 record must still fail authentication");
}

int main() {
  std::cout << "=== Swap secret-at-rest encryption (audit L-2) ===\n";
  roundTripsASecret();
  roundTripsAVariableLengthSecret();
  rejectsTheWrongKey();
  rejectsATamperedCiphertext();
  rejectsATamperedTail();
  rejectsATamperedNonce();
  rejectsATamperedSalt();
  rejectsAVersionDowngrade();
  rejectsAnUnknownVersion();
  usesAFreshNoncePerRecord();
  encodeDecodeRoundTripsTheEnvelope();
  encodeDecodeRoundTripsAVariableLengthEnvelope();
  decodeRejectsATruncatedBlob();
  decodeRejectsAWrongPlaintextLength();
  stillDecryptsAV1Record();
  aV1RecordWithTheWrongKeyStillFails();
  aV1TamperedRecordStillFails();

  std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}