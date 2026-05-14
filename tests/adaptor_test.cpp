// Standalone tests for adaptor signatures, DLEQ proofs, and Musig2.
// Build: g++ -std=c++14 -I../include -I../src adaptor_test.cpp -L../build/src -lCrypto -lpthread -o adaptor_test

#include <cstdio>
#include <cstring>

#include "../src/crypto/crypto.h"
#include "../src/crypto/adaptor.h"
#include "../src/crypto/dleq.h"
#include "../src/crypto/musig2.h"

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

// ─── adaptor signature tests ─────────────────────────────────────────

// Full round-trip: generate → verify pre-sig → adapt → check standard sig → extract secret
static bool test_adaptor_roundtrip() {
  printf("  adaptor round-trip ... ");

  // Signer keypair
  PublicKey pub;
  SecretKey sec;
  generate_keys(pub, sec);

  // Adaptor secret t, adaptor point T = t*G
  SecretKey t_sec;
  PublicKey T_pub;
  generate_keys(T_pub, t_sec);

  // Prefix hash (arbitrary)
  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  // 1. Generate adaptor pre-signature
  AdaptorSignature pre_sig;
  if (!generate_adaptor_signature(prefix, pub, sec, T_pub, pre_sig)) {
    printf("FAIL (generate returned false)\n");
    return false;
  }

  // 2. Verify pre-signature
  if (!check_adaptor_signature(prefix, pub, T_pub, pre_sig)) {
    printf("FAIL (check pre-sig returned false)\n");
    return false;
  }

  // 3. Adapt into standard signature
  Signature sig;
  adapt_signature(pre_sig, reinterpret_cast<const EllipticCurveScalar&>(t_sec), sig);

  // 4. Verify adapted signature using standard check_signature
  if (!check_signature(prefix, pub, sig)) {
    printf("FAIL (adapted sig failed standard check_signature)\n");
    return false;
  }

  // 5. Extract adaptor secret
  EllipticCurveScalar extracted_t;
  if (!extract_adaptor_secret(pre_sig, sig, extracted_t)) {
    printf("FAIL (extract returned false)\n");
    return false;
  }

  // 6. Verify extracted t matches original
  if (memcmp(&extracted_t, &t_sec, 32) != 0) {
    printf("FAIL (extracted secret != original)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Pre-signature should NOT pass standard check_signature
static bool test_adaptor_presig_not_valid() {
  printf("  pre-sig is not a valid standard sig ... ");

  PublicKey pub;
  SecretKey sec;
  generate_keys(pub, sec);

  SecretKey t_sec;
  PublicKey T_pub;
  generate_keys(T_pub, t_sec);

  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  AdaptorSignature pre_sig;
  if (!generate_adaptor_signature(prefix, pub, sec, T_pub, pre_sig)) {
    printf("FAIL (generate failed)\n");
    return false;
  }

  // Interpret pre-sig bytes as Signature and try standard verify
  Signature fake_sig;
  memcpy(fake_sig.data, pre_sig.data, 64);
  if (check_signature(prefix, pub, fake_sig)) {
    printf("FAIL (pre-sig accepted as standard sig!)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Wrong adaptor point should fail pre-sig verification
static bool test_adaptor_wrong_adaptor() {
  printf("  wrong adaptor point rejects ... ");

  PublicKey pub;
  SecretKey sec;
  generate_keys(pub, sec);

  SecretKey t_sec;
  PublicKey T_pub;
  generate_keys(T_pub, t_sec);

  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  AdaptorSignature pre_sig;
  if (!generate_adaptor_signature(prefix, pub, sec, T_pub, pre_sig)) {
    printf("FAIL (generate failed)\n");
    return false;
  }

  // Different adaptor point
  SecretKey t2_sec;
  PublicKey T2_pub;
  generate_keys(T2_pub, t2_sec);

  if (check_adaptor_signature(prefix, pub, T2_pub, pre_sig)) {
    printf("FAIL (wrong adaptor accepted!)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Wrong secret key to adapt should produce invalid signature
static bool test_adaptor_wrong_secret() {
  printf("  wrong adaptor secret fails ... ");

  PublicKey pub;
  SecretKey sec;
  generate_keys(pub, sec);

  SecretKey t_sec;
  PublicKey T_pub;
  generate_keys(T_pub, t_sec);

  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  AdaptorSignature pre_sig;
  if (!generate_adaptor_signature(prefix, pub, sec, T_pub, pre_sig)) {
    printf("FAIL (generate failed)\n");
    return false;
  }

  // Adapt with wrong secret
  EllipticCurveScalar wrong_t;
  random_scalar(wrong_t);
  Signature bad_sig;
  adapt_signature(pre_sig, wrong_t, bad_sig);

  if (check_signature(prefix, pub, bad_sig)) {
    printf("FAIL (wrong-secret sig accepted!)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Tampered pre-sig should fail verification
static bool test_adaptor_tamper() {
  printf("  tampered pre-sig rejects ... ");

  PublicKey pub;
  SecretKey sec;
  generate_keys(pub, sec);

  SecretKey t_sec;
  PublicKey T_pub;
  generate_keys(T_pub, t_sec);

  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  AdaptorSignature pre_sig;
  if (!generate_adaptor_signature(prefix, pub, sec, T_pub, pre_sig)) {
    printf("FAIL (generate failed)\n");
    return false;
  }

  // Flip a bit in the response part
  pre_sig.data[40] ^= 1;

  if (check_adaptor_signature(prefix, pub, T_pub, pre_sig)) {
    printf("FAIL (tampered pre-sig accepted!)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Extract should fail when challenges don't match
static bool test_adaptor_extract_mismatch() {
  printf("  extract fails on unrelated sigs ... ");

  PublicKey pub;
  SecretKey sec;
  generate_keys(pub, sec);

  SecretKey t_sec;
  PublicKey T_pub;
  generate_keys(T_pub, t_sec);

  Hash prefix1, prefix2;
  generate_random_bytes(sizeof(prefix1), &prefix1);
  generate_random_bytes(sizeof(prefix2), &prefix2);

  AdaptorSignature pre_sig;
  generate_adaptor_signature(prefix1, pub, sec, T_pub, pre_sig);

  // Standard sig on different prefix
  Signature unrelated_sig;
  generate_signature(prefix2, pub, sec, unrelated_sig);

  EllipticCurveScalar extracted;
  if (extract_adaptor_secret(pre_sig, unrelated_sig, extracted)) {
    printf("FAIL (extract succeeded on unrelated sigs!)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// ─── DLEQ tests ──────────────────────────────────────────────────────

// Round-trip: generate proof → verify
static bool test_dleq_roundtrip() {
  printf("  DLEQ round-trip ... ");

  // Secret x
  EllipticCurveScalar x;
  random_scalar(x);

  // A = x*G (point on base generator)
  PublicKey A;
  secret_key_to_public_key(reinterpret_cast<const SecretKey&>(x), A);

  // P = second base point (just use another keypair's public key)
  SecretKey p_sec;
  PublicKey P;
  generate_keys(P, p_sec);

  // B = x*P
  ge_p3 P_p3;
  ge_frombytes_vartime(&P_p3, reinterpret_cast<const unsigned char*>(&P));
  ge_p2 B_p2;
  ge_scalarmult(&B_p2, reinterpret_cast<const unsigned char*>(&x), &P_p3);
  PublicKey B;
  ge_tobytes(reinterpret_cast<unsigned char*>(&B), &B_p2);

  DLEQProof proof;
  if (!generate_dleq_proof(P, A, B, reinterpret_cast<const SecretKey&>(x), proof)) {
    printf("FAIL (generate returned false)\n");
    return false;
  }

  if (!check_dleq_proof(P, A, B, proof)) {
    printf("FAIL (verify returned false)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Wrong secret should fail DLEQ verification
static bool test_dleq_wrong_secret() {
  printf("  DLEQ wrong secret rejects ... ");

  EllipticCurveScalar x, y;
  random_scalar(x);
  random_scalar(y);

  PublicKey A;
  secret_key_to_public_key(reinterpret_cast<const SecretKey&>(x), A);

  SecretKey p_sec;
  PublicKey P;
  generate_keys(P, p_sec);

  // B = y*P (using WRONG secret y instead of x)
  ge_p3 P_p3;
  ge_frombytes_vartime(&P_p3, reinterpret_cast<const unsigned char*>(&P));
  ge_p2 B_p2;
  ge_scalarmult(&B_p2, reinterpret_cast<const unsigned char*>(&y), &P_p3);
  PublicKey B;
  ge_tobytes(reinterpret_cast<unsigned char*>(&B), &B_p2);

  // Proof with x, but B is y*P not x*P
  DLEQProof proof;
  if (!generate_dleq_proof(P, A, B, reinterpret_cast<const SecretKey&>(x), proof)) {
    printf("FAIL (generate failed unexpectedly)\n");
    return false;
  }

  // Proof should fail because B != x*P
  if (check_dleq_proof(P, A, B, proof)) {
    printf("FAIL (wrong-secret proof accepted!)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Tampered proof should fail
static bool test_dleq_tamper() {
  printf("  DLEQ tampered proof rejects ... ");

  EllipticCurveScalar x;
  random_scalar(x);

  PublicKey A;
  secret_key_to_public_key(reinterpret_cast<const SecretKey&>(x), A);

  SecretKey p_sec;
  PublicKey P;
  generate_keys(P, p_sec);

  ge_p3 P_p3;
  ge_frombytes_vartime(&P_p3, reinterpret_cast<const unsigned char*>(&P));
  ge_p2 B_p2;
  ge_scalarmult(&B_p2, reinterpret_cast<const unsigned char*>(&x), &P_p3);
  PublicKey B;
  ge_tobytes(reinterpret_cast<unsigned char*>(&B), &B_p2);

  DLEQProof proof;
  generate_dleq_proof(P, A, B, reinterpret_cast<const SecretKey&>(x), proof);

  // Flip bit in response
  reinterpret_cast<unsigned char*>(&proof.response)[5] ^= 1;

  if (check_dleq_proof(P, A, B, proof)) {
    printf("FAIL (tampered proof accepted!)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// ─── Musig2 tests ────────────────────────────────────────────────────

// Full 2-of-2 signing flow without adaptor
static bool test_musig2_basic() {
  printf("  Musig2 basic 2-of-2 ... ");

  // Two signers
  PublicKey pub0, pub1;
  SecretKey sec0, sec1;
  generate_keys(pub0, sec0);
  generate_keys(pub1, sec1);

  // Key aggregation
  Musig2KeyAgg key_agg;
  if (!musig2_key_agg(pub0, pub1, key_agg)) {
    printf("FAIL (key_agg failed)\n");
    return false;
  }

  // Nonce generation
  Musig2SecNonce snonce0, snonce1;
  Musig2PubNonce pnonce0, pnonce1;
  musig2_nonce_gen(snonce0, pnonce0);
  musig2_nonce_gen(snonce1, pnonce1);

  // Nonce aggregation
  Musig2AggNonce agg_nonce;
  if (!musig2_nonce_agg(pnonce0, pnonce1, agg_nonce)) {
    printf("FAIL (nonce_agg failed)\n");
    return false;
  }

  // Prefix hash
  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  // Session init (no adaptor)
  Musig2Session session;
  if (!musig2_session_init(prefix, key_agg, agg_nonce, nullptr, session)) {
    printf("FAIL (session_init failed)\n");
    return false;
  }

  // Partial signing
  Musig2PartialSig psig0, psig1;
  musig2_partial_sign(session, key_agg, snonce0, sec0, 0, psig0);
  musig2_partial_sign(session, key_agg, snonce1, sec1, 1, psig1);

  // Partial verification
  if (!musig2_partial_verify(session, key_agg, pnonce0, pub0, 0, psig0)) {
    printf("FAIL (partial_verify signer 0)\n");
    return false;
  }
  if (!musig2_partial_verify(session, key_agg, pnonce1, pub1, 1, psig1)) {
    printf("FAIL (partial_verify signer 1)\n");
    return false;
  }

  // Aggregate
  Signature final_sig;
  musig2_aggregate(session, psig0, psig1, final_sig);

  // Verify with standard check_signature against aggregated key
  if (!check_signature(prefix, key_agg.agg_pubkey, final_sig)) {
    printf("FAIL (final sig failed check_signature)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Musig2 with adaptor point — the swap scenario
static bool test_musig2_adaptor() {
  printf("  Musig2 with adaptor (swap flow) ... ");

  // Alice (signer 0) and Bob (signer 1)
  PublicKey pub_a, pub_b;
  SecretKey sec_a, sec_b;
  generate_keys(pub_a, sec_a);
  generate_keys(pub_b, sec_b);

  // Bob's adaptor secret t, adaptor point T = t*G
  SecretKey t_sec;
  PublicKey T_pub;
  generate_keys(T_pub, t_sec);

  // Key aggregation
  Musig2KeyAgg key_agg;
  if (!musig2_key_agg(pub_a, pub_b, key_agg)) {
    printf("FAIL (key_agg)\n");
    return false;
  }

  // Nonce generation
  Musig2SecNonce snonce_a, snonce_b;
  Musig2PubNonce pnonce_a, pnonce_b;
  musig2_nonce_gen(snonce_a, pnonce_a);
  musig2_nonce_gen(snonce_b, pnonce_b);

  // Nonce aggregation
  Musig2AggNonce agg_nonce;
  if (!musig2_nonce_agg(pnonce_a, pnonce_b, agg_nonce)) {
    printf("FAIL (nonce_agg)\n");
    return false;
  }

  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  // Session init WITH adaptor
  Musig2Session session;
  if (!musig2_session_init(prefix, key_agg, agg_nonce, &T_pub, session)) {
    printf("FAIL (session_init)\n");
    return false;
  }

  // Both create partial sigs (need copies of sec nonces since they get zeroed)
  Musig2SecNonce snonce_a_copy = snonce_a;
  Musig2SecNonce snonce_b_copy = snonce_b;

  Musig2PartialSig psig_a, psig_b;
  musig2_partial_sign(session, key_agg, snonce_a, sec_a, 0, psig_a);
  musig2_partial_sign(session, key_agg, snonce_b, sec_b, 1, psig_b);

  // Partial verification
  if (!musig2_partial_verify(session, key_agg, pnonce_a, pub_a, 0, psig_a)) {
    printf("FAIL (partial_verify Alice)\n");
    return false;
  }
  if (!musig2_partial_verify(session, key_agg, pnonce_b, pub_b, 1, psig_b)) {
    printf("FAIL (partial_verify Bob)\n");
    return false;
  }

  // Without adaptor secret, aggregated sig should be INVALID
  Signature pre_agg_sig;
  musig2_aggregate(session, psig_a, psig_b, pre_agg_sig);
  if (check_signature(prefix, key_agg.agg_pubkey, pre_agg_sig)) {
    printf("FAIL (pre-adapted aggregated sig accepted!)\n");
    return false;
  }

  // Bob adapts his partial sig: psig_b.s += t
  Musig2PartialSig psig_b_adapted;
  sc_add(reinterpret_cast<unsigned char*>(&psig_b_adapted.s),
         reinterpret_cast<const unsigned char*>(&psig_b.s),
         reinterpret_cast<const unsigned char*>(&t_sec));

  // Aggregate with adapted partial sig
  Signature final_sig;
  musig2_aggregate(session, psig_a, psig_b_adapted, final_sig);

  // NOW it should verify
  if (!check_signature(prefix, key_agg.agg_pubkey, final_sig)) {
    printf("FAIL (adapted final sig failed check_signature)\n");
    return false;
  }

  // Alice can extract t from seeing the pre-adapted and adapted partial sigs
  EllipticCurveScalar extracted_t;
  sc_sub(reinterpret_cast<unsigned char*>(&extracted_t),
         reinterpret_cast<const unsigned char*>(&psig_b_adapted.s),
         reinterpret_cast<const unsigned char*>(&psig_b.s));

  if (memcmp(&extracted_t, &t_sec, 32) != 0) {
    printf("FAIL (extracted adaptor secret != original)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Key aggregation is deterministic for same inputs
static bool test_musig2_key_agg_deterministic() {
  printf("  Musig2 key_agg deterministic ... ");

  PublicKey pub0, pub1;
  SecretKey sec0, sec1;
  generate_keys(pub0, sec0);
  generate_keys(pub1, sec1);

  Musig2KeyAgg agg1, agg2;
  musig2_key_agg(pub0, pub1, agg1);
  musig2_key_agg(pub0, pub1, agg2);

  if (memcmp(&agg1.agg_pubkey, &agg2.agg_pubkey, 32) != 0) {
    printf("FAIL (agg keys differ)\n");
    return false;
  }
  if (memcmp(agg1.coeff, agg2.coeff, 64) != 0) {
    printf("FAIL (coefficients differ)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Key ordering matters: (A,B) != (B,A)
static bool test_musig2_key_order_matters() {
  printf("  Musig2 key order matters ... ");

  PublicKey pub0, pub1;
  SecretKey sec0, sec1;
  generate_keys(pub0, sec0);
  generate_keys(pub1, sec1);

  Musig2KeyAgg agg_01, agg_10;
  musig2_key_agg(pub0, pub1, agg_01);
  musig2_key_agg(pub1, pub0, agg_10);

  // Aggregated keys should differ when order is swapped
  // (Unless by astronomically unlikely coincidence the coefficients happen to be equal)
  if (memcmp(&agg_01.agg_pubkey, &agg_10.agg_pubkey, 32) == 0) {
    printf("FAIL (swapped order produced same key)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Aggregated key is a valid Ed25519 point
static bool test_musig2_agg_key_valid() {
  printf("  Musig2 aggregated key is valid point ... ");

  PublicKey pub0, pub1;
  SecretKey sec0, sec1;
  generate_keys(pub0, sec0);
  generate_keys(pub1, sec1);

  Musig2KeyAgg key_agg;
  musig2_key_agg(pub0, pub1, key_agg);

  if (!check_key(key_agg.agg_pubkey)) {
    printf("FAIL (aggregated key not a valid point)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Sec nonces are zeroed after partial signing
static bool test_musig2_nonce_zeroed() {
  printf("  Musig2 sec nonce zeroed after sign ... ");

  PublicKey pub0, pub1;
  SecretKey sec0, sec1;
  generate_keys(pub0, sec0);
  generate_keys(pub1, sec1);

  Musig2KeyAgg key_agg;
  musig2_key_agg(pub0, pub1, key_agg);

  Musig2SecNonce snonce;
  Musig2PubNonce pnonce;
  musig2_nonce_gen(snonce, pnonce);

  Musig2SecNonce snonce1;
  Musig2PubNonce pnonce1;
  musig2_nonce_gen(snonce1, pnonce1);

  Musig2AggNonce agg_nonce;
  musig2_nonce_agg(pnonce, pnonce1, agg_nonce);

  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  Musig2Session session;
  musig2_session_init(prefix, key_agg, agg_nonce, nullptr, session);

  Musig2PartialSig psig;
  musig2_partial_sign(session, key_agg, snonce, sec0, 0, psig);

  // Check nonce is zeroed
  unsigned char zeros[sizeof(Musig2SecNonce)];
  memset(zeros, 0, sizeof(zeros));
  if (memcmp(&snonce, zeros, sizeof(Musig2SecNonce)) != 0) {
    printf("FAIL (sec nonce not zeroed)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// Tampered partial sig should fail verification
static bool test_musig2_tamper_partial() {
  printf("  Musig2 tampered partial sig rejects ... ");

  PublicKey pub0, pub1;
  SecretKey sec0, sec1;
  generate_keys(pub0, sec0);
  generate_keys(pub1, sec1);

  Musig2KeyAgg key_agg;
  musig2_key_agg(pub0, pub1, key_agg);

  Musig2SecNonce snonce0, snonce1;
  Musig2PubNonce pnonce0, pnonce1;
  musig2_nonce_gen(snonce0, pnonce0);
  musig2_nonce_gen(snonce1, pnonce1);

  Musig2AggNonce agg_nonce;
  musig2_nonce_agg(pnonce0, pnonce1, agg_nonce);

  Hash prefix;
  generate_random_bytes(sizeof(prefix), &prefix);

  Musig2Session session;
  musig2_session_init(prefix, key_agg, agg_nonce, nullptr, session);

  Musig2PartialSig psig0;
  musig2_partial_sign(session, key_agg, snonce0, sec0, 0, psig0);

  // Tamper
  reinterpret_cast<unsigned char*>(&psig0.s)[3] ^= 1;

  if (musig2_partial_verify(session, key_agg, pnonce0, pub0, 0, psig0)) {
    printf("FAIL (tampered partial sig accepted!)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// ─── combined adaptor + DLEQ test (full swap scenario) ───────────────

// Simulate the swap protocol:
// 1. Bob picks secret t, publishes T = t*G with DLEQ proof
// 2. Musig2 joint address, Alice sends XFG there
// 3. Alice creates adaptor pre-sig (spending from joint to Bob)
// 4. Bob verifies pre-sig
// 5. Alice reveals t by claiming other-chain funds
// 6. Bob adapts pre-sig, completes Musig2 sig, claims XFG
// 7. Alice extracts t from on-chain sig
static bool test_full_swap_scenario() {
  printf("  full swap scenario (adaptor + DLEQ + Musig2) ... ");

  // Alice and Bob keypairs
  PublicKey pub_a, pub_b;
  SecretKey sec_a, sec_b;
  generate_keys(pub_a, sec_a);
  generate_keys(pub_b, sec_b);

  // Step 1: Bob picks adaptor secret t
  SecretKey t_sec;
  PublicKey T_pub;
  generate_keys(T_pub, t_sec);

  // Bob proves T is well-formed with DLEQ
  // Need a second base point P for DLEQ — use hash_to_ec of a fixed seed
  // Actually, for DLEQ we prove: T = t*G using G as implicit base.
  // We need a second point P and prove T = t*G and Q = t*P simultaneously.
  // For the swap, the DLEQ proves the adaptor point was generated honestly.
  // Let's use a deterministic second base.
  SecretKey p_sec;
  PublicKey P_base;
  generate_keys(P_base, p_sec);

  // Q = t*P
  ge_p3 P_p3;
  ge_frombytes_vartime(&P_p3, reinterpret_cast<const unsigned char*>(&P_base));
  ge_p2 Q_p2;
  ge_scalarmult(&Q_p2, reinterpret_cast<const unsigned char*>(&t_sec), &P_p3);
  PublicKey Q;
  ge_tobytes(reinterpret_cast<unsigned char*>(&Q), &Q_p2);

  DLEQProof dleq_proof;
  if (!generate_dleq_proof(P_base, T_pub, Q,
      reinterpret_cast<const SecretKey&>(t_sec), dleq_proof)) {
    printf("FAIL (DLEQ generate)\n");
    return false;
  }

  // Alice verifies DLEQ proof
  if (!check_dleq_proof(P_base, T_pub, Q, dleq_proof)) {
    printf("FAIL (Alice rejects DLEQ proof)\n");
    return false;
  }

  // Step 2: Musig2 joint address
  Musig2KeyAgg key_agg;
  if (!musig2_key_agg(pub_a, pub_b, key_agg)) {
    printf("FAIL (key_agg)\n");
    return false;
  }

  // Step 3: Nonce exchange + session with adaptor
  Musig2SecNonce snonce_a, snonce_b;
  Musig2PubNonce pnonce_a, pnonce_b;
  musig2_nonce_gen(snonce_a, pnonce_a);
  musig2_nonce_gen(snonce_b, pnonce_b);

  Musig2AggNonce agg_nonce;
  musig2_nonce_agg(pnonce_a, pnonce_b, agg_nonce);

  Hash tx_hash;
  generate_random_bytes(sizeof(tx_hash), &tx_hash);

  Musig2Session session;
  if (!musig2_session_init(tx_hash, key_agg, agg_nonce, &T_pub, session)) {
    printf("FAIL (session_init)\n");
    return false;
  }

  // Alice and Bob create partial sigs
  Musig2PartialSig psig_a, psig_b;
  musig2_partial_sign(session, key_agg, snonce_a, sec_a, 0, psig_a);
  musig2_partial_sign(session, key_agg, snonce_b, sec_b, 1, psig_b);

  // Verify partials
  if (!musig2_partial_verify(session, key_agg, pnonce_a, pub_a, 0, psig_a)) {
    printf("FAIL (partial_verify Alice)\n");
    return false;
  }
  if (!musig2_partial_verify(session, key_agg, pnonce_b, pub_b, 1, psig_b)) {
    printf("FAIL (partial_verify Bob)\n");
    return false;
  }

  // Step 4: Alice sends her partial sig to Bob (she keeps psig_b for later extraction)
  // Bob has both partials. Without t, aggregate sig is invalid.
  Signature incomplete_sig;
  musig2_aggregate(session, psig_a, psig_b, incomplete_sig);
  if (check_signature(tx_hash, key_agg.agg_pubkey, incomplete_sig)) {
    printf("FAIL (incomplete sig accepted!)\n");
    return false;
  }

  // Step 5-6: Bob learns t (from Alice's on-chain claim), adapts his partial sig
  Musig2PartialSig psig_b_adapted;
  sc_add(reinterpret_cast<unsigned char*>(&psig_b_adapted.s),
         reinterpret_cast<const unsigned char*>(&psig_b.s),
         reinterpret_cast<const unsigned char*>(&t_sec));

  // Bob aggregates and broadcasts
  Signature final_sig;
  musig2_aggregate(session, psig_a, psig_b_adapted, final_sig);

  if (!check_signature(tx_hash, key_agg.agg_pubkey, final_sig)) {
    printf("FAIL (final sig invalid)\n");
    return false;
  }

  // Step 7: Alice sees final_sig on-chain, extracts t
  // She knows psig_b (Bob's unadapted partial) and can compute psig_b_adapted
  // from the aggregate: psig_b_adapted.s = final_sig.r - psig_a.s
  // Then t = psig_b_adapted.s - psig_b.s
  EllipticCurveScalar adapted_b_s;
  sc_sub(reinterpret_cast<unsigned char*>(&adapted_b_s),
         final_sig.data + 32,  // aggregate response
         reinterpret_cast<const unsigned char*>(&psig_a.s));

  EllipticCurveScalar recovered_t;
  sc_sub(reinterpret_cast<unsigned char*>(&recovered_t),
         reinterpret_cast<const unsigned char*>(&adapted_b_s),
         reinterpret_cast<const unsigned char*>(&psig_b.s));

  if (memcmp(&recovered_t, &t_sec, 32) != 0) {
    printf("FAIL (Alice recovered wrong t)\n");
    return false;
  }

  printf("OK\n");
  return true;
}

// ─── main ────────────────────────────────────────────────────────────

int main() {
  bool all_pass = true;

  printf("Adaptor signature tests:\n");
  all_pass &= test_adaptor_roundtrip();
  all_pass &= test_adaptor_presig_not_valid();
  all_pass &= test_adaptor_wrong_adaptor();
  all_pass &= test_adaptor_wrong_secret();
  all_pass &= test_adaptor_tamper();
  all_pass &= test_adaptor_extract_mismatch();

  printf("\nDLEQ proof tests:\n");
  all_pass &= test_dleq_roundtrip();
  all_pass &= test_dleq_wrong_secret();
  all_pass &= test_dleq_tamper();

  printf("\nMusig2 tests:\n");
  all_pass &= test_musig2_basic();
  all_pass &= test_musig2_adaptor();
  all_pass &= test_musig2_key_agg_deterministic();
  all_pass &= test_musig2_key_order_matters();
  all_pass &= test_musig2_agg_key_valid();
  all_pass &= test_musig2_nonce_zeroed();
  all_pass &= test_musig2_tamper_partial();

  printf("\nIntegration tests:\n");
  all_pass &= test_full_swap_scenario();

  printf("\n%s\n", all_pass ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
  return all_pass ? 0 : 1;
}
