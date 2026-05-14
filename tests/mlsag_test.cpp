// Quick standalone test for MLSAG sign/verify round-trip.
// Build: g++ -std=c++14 -I../include -I../src mlsag_test.cpp -L../build/src -lCrypto -lpthread -o mlsag_test

#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/crypto/crypto.h"
#include "../src/crypto/mlsag.h"
#include "../src/crypto/pedersen.h"

extern "C" {
#include "../src/crypto/random.h"
}

using namespace Crypto;

static void random_scalar(EllipticCurveScalar& s) {
  unsigned char tmp[64];
  generate_random_bytes(64, tmp);
  sc_reduce(tmp);
  memcpy(&s, tmp, 32);
}

// Test: generate + verify MLSAG for various ring sizes
static bool test_mlsag_roundtrip(size_t ring_size, size_t real_idx) {
  printf("  ring_size=%zu, real_idx=%zu ... ", ring_size, real_idx);

  pedersen_init();

  // Generate ring member keys
  std::vector<PublicKey> pubs(ring_size);
  std::vector<SecretKey> secs(ring_size);
  for (size_t i = 0; i < ring_size; i++) {
    generate_keys(pubs[i], secs[i]);
  }

  // Generate commitments: C[i] = amount_i * H + mask_i * G
  uint64_t real_amount = 50000000ULL; // 5 XFG
  std::vector<EllipticCurvePoint> commitments(ring_size);
  std::vector<EllipticCurveScalar> masks(ring_size);
  std::vector<uint64_t> amounts(ring_size);

  for (size_t i = 0; i < ring_size; i++) {
    amounts[i] = (i == real_idx) ? real_amount : (10000000ULL * (i + 1)); // fake amounts for decoys
    random_scalar(masks[i]);
    pedersen_commit(commitments[i], amounts[i], masks[i]);
  }

  // Pseudo-commitment: C_pseudo = real_amount * H + z * G
  // where z is chosen so that sec_commit = mask[real_idx] - z
  EllipticCurveScalar z;
  random_scalar(z);
  EllipticCurvePoint pseudo_commitment;
  pedersen_commit(pseudo_commitment, real_amount, z);

  // sec_commit = mask[real_idx] - z  (discrete log of D[real_idx] = C[real_idx] - C_pseudo)
  EllipticCurveScalar sec_commit;
  sc_sub(reinterpret_cast<unsigned char*>(&sec_commit),
         reinterpret_cast<const unsigned char*>(&masks[real_idx]),
         reinterpret_cast<const unsigned char*>(&z));

  // Key image
  KeyImage key_image;
  generate_key_image(pubs[real_idx], secs[real_idx], key_image);

  // Prefix hash (arbitrary for test)
  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  // Sign
  EllipticCurveScalar sig_c0;
  std::vector<EllipticCurveScalar> sig_s(ring_size * 2);
  bool ok = generate_mlsag(
      prefix, pubs.data(), commitments.data(), pseudo_commitment,
      key_image, secs[real_idx], sec_commit, real_idx, ring_size,
      sig_c0, sig_s.data());
  if (!ok) {
    printf("FAIL (generate returned false)\n");
    return false;
  }

  // Verify
  ok = check_mlsag(
      prefix, pubs.data(), commitments.data(), pseudo_commitment,
      key_image, ring_size, sig_c0, sig_s.data());
  if (!ok) {
    printf("FAIL (verify returned false)\n");
    return false;
  }

  // Tamper test: flip a bit in sig_s[0] and verify it fails
  reinterpret_cast<unsigned char*>(&sig_s[0])[0] ^= 1;
  ok = check_mlsag(
      prefix, pubs.data(), commitments.data(), pseudo_commitment,
      key_image, ring_size, sig_c0, sig_s.data());
  if (ok) {
    printf("FAIL (tampered sig accepted!)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Test: wrong amount in pseudo_commitment should fail
static bool test_mlsag_wrong_amount() {
  printf("  wrong pseudo_commitment amount ... ");

  pedersen_init();
  const size_t ring_size = 4;
  const size_t real_idx = 1;

  std::vector<PublicKey> pubs(ring_size);
  std::vector<SecretKey> secs(ring_size);
  for (size_t i = 0; i < ring_size; i++)
    generate_keys(pubs[i], secs[i]);

  uint64_t real_amount = 50000000ULL;
  std::vector<EllipticCurvePoint> commitments(ring_size);
  std::vector<EllipticCurveScalar> masks(ring_size);
  for (size_t i = 0; i < ring_size; i++) {
    random_scalar(masks[i]);
    pedersen_commit(commitments[i], (i == real_idx) ? real_amount : 10000000ULL * (i + 1), masks[i]);
  }

  // Pseudo with WRONG amount (trying to inflate)
  EllipticCurveScalar z;
  random_scalar(z);
  EllipticCurvePoint pseudo_commitment;
  pedersen_commit(pseudo_commitment, real_amount + 1, z); // +1 atomic unit inflation attempt

  // sec_commit = mask - z (but D[real_idx] won't be z*G anymore because amounts differ)
  EllipticCurveScalar sec_commit;
  sc_sub(reinterpret_cast<unsigned char*>(&sec_commit),
         reinterpret_cast<const unsigned char*>(&masks[real_idx]),
         reinterpret_cast<const unsigned char*>(&z));

  KeyImage key_image;
  generate_key_image(pubs[real_idx], secs[real_idx], key_image);

  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  EllipticCurveScalar sig_c0;
  std::vector<EllipticCurveScalar> sig_s(ring_size * 2);

  // generate_mlsag will produce a signature, but it won't verify
  // because the commitment difference D[real_idx] = C - C_pseudo
  // has discrete log (mask - z) for G component but also has
  // (real_amount - (real_amount+1))*H = -1*H in the H component.
  // So sec_commit is NOT the actual discrete log of D[real_idx].
  // The ring won't close.
  bool ok = generate_mlsag(
      prefix, pubs.data(), commitments.data(), pseudo_commitment,
      key_image, secs[real_idx], sec_commit, real_idx, ring_size,
      sig_c0, sig_s.data());
  if (!ok) {
    // generate might succeed even with wrong amounts (it doesn't validate)
    // but verify must fail
    printf("OK (generate rejected)\n");
    return true;
  }

  ok = check_mlsag(
      prefix, pubs.data(), commitments.data(), pseudo_commitment,
      key_image, ring_size, sig_c0, sig_s.data());
  if (ok) {
    printf("FAIL (wrong-amount sig accepted — inflation possible!)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

int main() {
  printf("MLSAG tests:\n");

  bool all_pass = true;

  // Round-trip tests with various ring sizes and real indices
  all_pass &= test_mlsag_roundtrip(1, 0);   // degenerate: ring size 1
  all_pass &= test_mlsag_roundtrip(2, 0);
  all_pass &= test_mlsag_roundtrip(2, 1);
  all_pass &= test_mlsag_roundtrip(4, 0);
  all_pass &= test_mlsag_roundtrip(4, 2);
  all_pass &= test_mlsag_roundtrip(4, 3);
  all_pass &= test_mlsag_roundtrip(9, 0);   // typical Fuego ring size
  all_pass &= test_mlsag_roundtrip(9, 4);
  all_pass &= test_mlsag_roundtrip(9, 8);
  all_pass &= test_mlsag_roundtrip(16, 7);

  // Inflation attack test
  all_pass &= test_mlsag_wrong_amount();

  printf("\n%s\n", all_pass ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
  return all_pass ? 0 : 1;
}
