// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.
//
//   J. Nick, T. Ruffing, and Y. Seurin, 2021
//   BIP-327: MuSig2 for BIP-340-compatible multi-signatures
//   Ed25519 SUPERCOP ref10 D.J. Bernstein et al
//


#include "musig2.h"
#include "crypto.h"

extern "C" {
#include "crypto-ops.h"
#include "random.h"
}

#include <mutex>
#include <cstring>

namespace Crypto {

// Domain separators
static const char MUSIG2_KEYSET_TAG[]  = "FuegoMusig2KeySet";
static const char MUSIG2_KEYAGG_TAG[]  = "FuegoMusig2KeyAgg";
static const char MUSIG2_NONBIND_TAG[] = "FuegoMusig2NonBind";

// ─── helpers ──────────────────────────────────────────────────────────

static inline void musig2_random_scalar(EllipticCurveScalar &res) {
  unsigned char tmp[64];
  generate_random_bytes(64, tmp);
  sc_reduce(tmp);
  memcpy(&res, tmp, 32);
}

// sc_mul: r = a * b  (mod l)
// Implemented via sc_mulsub: mulsub(r, a, b, 0) = 0 - a*b = -(a*b),
// then negate.
static void sc_mul(unsigned char *r,
                   const unsigned char *a,
                   const unsigned char *b) {
  unsigned char zero[32];
  sc_0(zero);
  unsigned char neg[32];
  sc_mulsub(neg, a, b, zero);  // neg = -(a*b)
  sc_sub(r, zero, neg);        // r = 0 - (-(a*b)) = a*b
}

// sc_muladd: r = c + a*b  (mod l)
static void sc_muladd(unsigned char *r,
                      const unsigned char *a,
                      const unsigned char *b,
                      const unsigned char *c) {
  unsigned char ab[32];
  sc_mul(ab, a, b);
  sc_add(r, c, ab);
}

// Add two Ed25519 points from compressed form, write result compressed.
// Returns false if either input fails to decode.
static bool point_add(const unsigned char *a_bytes,
                      const unsigned char *b_bytes,
                      unsigned char *out_bytes) {
  ge_p3 a_p3, b_p3;
  if (ge_frombytes_vartime(&a_p3, a_bytes) != 0) return false;
  if (ge_frombytes_vartime(&b_p3, b_bytes) != 0) return false;
  ge_cached b_cached;
  ge_p3_to_cached(&b_cached, &b_p3);
  ge_p1p1 sum_p1p1;
  ge_add(&sum_p1p1, &a_p3, &b_cached);
  ge_p3 sum_p3;
  ge_p1p1_to_p3(&sum_p3, &sum_p1p1);
  ge_p3_tobytes(out_bytes, &sum_p3);
  return true;
}

// Mirror of s_comm from crypto.cpp for Schnorr challenge computation.
struct musig2_s_comm {
  Hash h;
  EllipticCurvePoint key;
  EllipticCurvePoint comm;
};

// ─── key aggregation ──────────────────────────────────────────────────

bool musig2_key_agg(
    const PublicKey &pub0,
    const PublicKey &pub1,
    Musig2KeyAgg &agg)
{
  // L = Hs(KEYSET_TAG || pub0 || pub1) — identifies this key set
  struct {
    char tag[sizeof(MUSIG2_KEYSET_TAG) - 1];
    unsigned char pk0[32];
    unsigned char pk1[32];
  } L_buf;
  memcpy(L_buf.tag, MUSIG2_KEYSET_TAG, sizeof(MUSIG2_KEYSET_TAG) - 1);
  memcpy(L_buf.pk0, &pub0, 32);
  memcpy(L_buf.pk1, &pub1, 32);

  EllipticCurveScalar L;
  hash_to_scalar(&L_buf, sizeof(L_buf), L);

  // a0 = Hs(KEYAGG_TAG || L || pub0)
  // a1 = Hs(KEYAGG_TAG || L || pub1)
  struct {
    char tag[sizeof(MUSIG2_KEYAGG_TAG) - 1];
    EllipticCurveScalar L_hash;
    unsigned char pk[32];
  } coeff_buf;
  memcpy(coeff_buf.tag, MUSIG2_KEYAGG_TAG, sizeof(MUSIG2_KEYAGG_TAG) - 1);
  coeff_buf.L_hash = L;

  memcpy(coeff_buf.pk, &pub0, 32);
  hash_to_scalar(&coeff_buf, sizeof(coeff_buf), agg.coeff[0]);

  memcpy(coeff_buf.pk, &pub1, 32);
  hash_to_scalar(&coeff_buf, sizeof(coeff_buf), agg.coeff[1]);

  // P = a0*P0 + a1*P1
  ge_p3 P0_p3, P1_p3;
  if (ge_frombytes_vartime(&P0_p3,
      reinterpret_cast<const unsigned char*>(&pub0)) != 0) {
    return false;
  }
  if (ge_frombytes_vartime(&P1_p3,
      reinterpret_cast<const unsigned char*>(&pub1)) != 0) {
    return false;
  }

  // a0*P0
  ge_p2 a0P0_p2;
  ge_scalarmult(&a0P0_p2,
                reinterpret_cast<const unsigned char*>(&agg.coeff[0]),
                &P0_p3);
  unsigned char a0P0_bytes[32];
  ge_tobytes(a0P0_bytes, &a0P0_p2);

  // a1*P1
  ge_p2 a1P1_p2;
  ge_scalarmult(&a1P1_p2,
                reinterpret_cast<const unsigned char*>(&agg.coeff[1]),
                &P1_p3);
  unsigned char a1P1_bytes[32];
  ge_tobytes(a1P1_bytes, &a1P1_p2);

  // P = a0*P0 + a1*P1
  if (!point_add(a0P0_bytes, a1P1_bytes,
      reinterpret_cast<unsigned char*>(&agg.agg_pubkey))) {
    return false;
  }

  return true;
}

// ─── nonce generation ─────────────────────────────────────────────────

void musig2_nonce_gen(
    Musig2SecNonce &sec_nonce,
    Musig2PubNonce &pub_nonce)
{
  std::lock_guard<std::mutex> lock(random_lock);
  for (size_t j = 0; j < MUSIG2_V; ++j) {
    musig2_random_scalar(sec_nonce.k[j]);
    ge_p3 R_p3;
    ge_scalarmult_base(&R_p3,
        reinterpret_cast<const unsigned char*>(&sec_nonce.k[j]));
    ge_p3_tobytes(reinterpret_cast<unsigned char*>(&pub_nonce.R[j]), &R_p3);
  }
}

// ─── nonce aggregation ────────────────────────────────────────────────

bool musig2_nonce_agg(
    const Musig2PubNonce &nonce0,
    const Musig2PubNonce &nonce1,
    Musig2AggNonce &agg_nonce)
{
  for (size_t j = 0; j < MUSIG2_V; ++j) {
    if (!point_add(
        reinterpret_cast<const unsigned char*>(&nonce0.R[j]),
        reinterpret_cast<const unsigned char*>(&nonce1.R[j]),
        reinterpret_cast<unsigned char*>(&agg_nonce.R_agg[j]))) {
      return false;
    }
  }
  return true;
}

// ─── session initialization ───────────────────────────────────────────

bool musig2_session_init(
    const Hash &prefix_hash,
    const Musig2KeyAgg &key_agg,
    const Musig2AggNonce &agg_nonce,
    const PublicKey *adaptor_point,
    Musig2Session &session)
{
  // b = Hs(NONBIND_TAG || R_agg[0] || R_agg[1] || agg_pubkey || prefix_hash)
  struct {
    char tag[sizeof(MUSIG2_NONBIND_TAG) - 1];
    unsigned char R0[32];
    unsigned char R1[32];
    unsigned char P[32];
    Hash msg;
  } b_buf;
  memcpy(b_buf.tag, MUSIG2_NONBIND_TAG, sizeof(MUSIG2_NONBIND_TAG) - 1);
  memcpy(b_buf.R0, &agg_nonce.R_agg[0], 32);
  memcpy(b_buf.R1, &agg_nonce.R_agg[1], 32);
  memcpy(b_buf.P, &key_agg.agg_pubkey, 32);
  b_buf.msg = prefix_hash;

  hash_to_scalar(&b_buf, sizeof(b_buf), session.b);

  // R = R_agg[0] + b * R_agg[1]
  ge_p3 R0_p3;
  if (ge_frombytes_vartime(&R0_p3,
      reinterpret_cast<const unsigned char*>(&agg_nonce.R_agg[0])) != 0) {
    return false;
  }
  ge_p3 R1_p3;
  if (ge_frombytes_vartime(&R1_p3,
      reinterpret_cast<const unsigned char*>(&agg_nonce.R_agg[1])) != 0) {
    return false;
  }

  // b*R1
  ge_p2 bR1_p2;
  ge_scalarmult(&bR1_p2,
                reinterpret_cast<const unsigned char*>(&session.b),
                &R1_p3);
  unsigned char bR1_bytes[32];
  ge_tobytes(bR1_bytes, &bR1_p2);

  // R = R0 + b*R1
  ge_p3 bR1_p3;
  if (ge_frombytes_vartime(&bR1_p3, bR1_bytes) != 0) {
    return false;
  }
  ge_cached bR1_cached;
  ge_p3_to_cached(&bR1_cached, &bR1_p3);
  ge_p1p1 R_p1p1;
  ge_add(&R_p1p1, &R0_p3, &bR1_cached);
  ge_p3 R_p3;
  ge_p1p1_to_p3(&R_p3, &R_p1p1);

  // If adapted: R = R + T
  if (adaptor_point != nullptr) {
    ge_p3 T_p3;
    if (ge_frombytes_vartime(&T_p3,
        reinterpret_cast<const unsigned char*>(adaptor_point)) != 0) {
      return false;
    }
    ge_cached T_cached;
    ge_p3_to_cached(&T_cached, &T_p3);
    ge_p1p1 RT_p1p1;
    ge_add(&RT_p1p1, &R_p3, &T_cached);
    ge_p1p1_to_p3(&R_p3, &RT_p1p1);
  }

  ge_p3_tobytes(reinterpret_cast<unsigned char*>(&session.R_combined), &R_p3);

  // c = Hs(prefix_hash || agg_pubkey || R)
  // Same hash domain as standard Fuego Schnorr signatures.
  musig2_s_comm comm_buf;
  comm_buf.h = prefix_hash;
  comm_buf.key = reinterpret_cast<const EllipticCurvePoint&>(key_agg.agg_pubkey);
  comm_buf.comm = reinterpret_cast<const EllipticCurvePoint&>(session.R_combined);

  hash_to_scalar(&comm_buf, sizeof(musig2_s_comm), session.challenge);

  return true;
}

// ─── partial signing ──────────────────────────────────────────────────

bool musig2_partial_sign(
    Musig2Session &session,
    const Musig2KeyAgg &key_agg,
    Musig2SecNonce &sec_nonce,
    const SecretKey &sec_key,
    unsigned int signer_index,
    Musig2PartialSig &partial_sig)
{
  // Guard: reject if this session has already produced a partial signature.
  // Reusing the same nonce in two signatures leaks the private key.
  if (session.nonceSigned) {
    return false; // nonce already used — refuse to sign
  }

  // k_eff = k[0] + b * k[1]
  unsigned char k_eff[32];
  sc_muladd(k_eff,
            reinterpret_cast<const unsigned char*>(&session.b),
            reinterpret_cast<const unsigned char*>(&sec_nonce.k[1]),
            reinterpret_cast<const unsigned char*>(&sec_nonce.k[0]));

  // effective_secret = a[i] * x
  unsigned char ax[32];
  sc_mul(ax,
         reinterpret_cast<const unsigned char*>(&key_agg.coeff[signer_index]),
         reinterpret_cast<const unsigned char*>(&sec_key));

  // s_i = k_eff - c * a[i] * x
  sc_mulsub(reinterpret_cast<unsigned char*>(&partial_sig.s),
            reinterpret_cast<const unsigned char*>(&session.challenge),
            ax,
            k_eff);

  // Securely erase secret nonces — MUST NOT be reused
  memset(&sec_nonce, 0, sizeof(Musig2SecNonce));

  // Mark session as signed to prevent nonce reuse on any subsequent call.
  session.nonceSigned = true;
  return true;
}

// ─── partial verification ─────────────────────────────────────────────

bool musig2_partial_verify(
    const Musig2Session &session,
    const Musig2KeyAgg &key_agg,
    const Musig2PubNonce &signer_nonce,
    const PublicKey &signer_pub,
    unsigned int signer_index,
    const Musig2PartialSig &partial_sig)
{
  if (sc_check(reinterpret_cast<const unsigned char*>(&partial_sig.s)) != 0) {
    return false;
  }

  // R_eff_i = R_i[0] + b * R_i[1]
  ge_p3 Ri0_p3;
  if (ge_frombytes_vartime(&Ri0_p3,
      reinterpret_cast<const unsigned char*>(&signer_nonce.R[0])) != 0) {
    return false;
  }
  ge_p3 Ri1_p3;
  if (ge_frombytes_vartime(&Ri1_p3,
      reinterpret_cast<const unsigned char*>(&signer_nonce.R[1])) != 0) {
    return false;
  }

  ge_p2 bRi1_p2;
  ge_scalarmult(&bRi1_p2,
                reinterpret_cast<const unsigned char*>(&session.b),
                &Ri1_p3);
  unsigned char bRi1_bytes[32];
  ge_tobytes(bRi1_bytes, &bRi1_p2);
  ge_p3 bRi1_p3;
  if (ge_frombytes_vartime(&bRi1_p3, bRi1_bytes) != 0) {
    return false;
  }
  ge_cached bRi1_cached;
  ge_p3_to_cached(&bRi1_cached, &bRi1_p3);
  ge_p1p1 Reff_p1p1;
  ge_add(&Reff_p1p1, &Ri0_p3, &bRi1_cached);
  ge_p3 Reff_p3;
  ge_p1p1_to_p3(&Reff_p3, &Reff_p1p1);

  // a_i * P_i
  ge_p3 Pi_p3;
  if (ge_frombytes_vartime(&Pi_p3,
      reinterpret_cast<const unsigned char*>(&signer_pub)) != 0) {
    return false;
  }
  ge_p2 aiPi_p2;
  ge_scalarmult(&aiPi_p2,
                reinterpret_cast<const unsigned char*>(&key_agg.coeff[signer_index]),
                &Pi_p3);
  unsigned char aiPi_bytes[32];
  ge_tobytes(aiPi_bytes, &aiPi_p2);
  ge_p3 aiPi_p3;
  if (ge_frombytes_vartime(&aiPi_p3, aiPi_bytes) != 0) {
    return false;
  }

  // Verify: s_i*G + c*(a_i*P_i) == R_eff_i
  // => s_i*G + c*(a_i*P_i) should give R_eff_i
  // Using: ge_double_scalarmult_base_vartime(r, a, A, b) = a*A + b*G
  ge_p2 check_p2;
  ge_double_scalarmult_base_vartime(&check_p2,
      reinterpret_cast<const unsigned char*>(&session.challenge),
      &aiPi_p3,
      reinterpret_cast<const unsigned char*>(&partial_sig.s));

  unsigned char check_bytes[32];
  ge_tobytes(check_bytes, &check_p2);
  unsigned char Reff_bytes[32];
  ge_p3_tobytes(Reff_bytes, &Reff_p3);

  return memcmp(check_bytes, Reff_bytes, 32) == 0;
}

// ─── signature aggregation ────────────────────────────────────────────

void musig2_aggregate(
    const Musig2Session &session,
    const Musig2PartialSig &sig0,
    const Musig2PartialSig &sig1,
    Signature &final_sig)
{
  // Challenge is the session challenge
  memcpy(final_sig.data,
         reinterpret_cast<const unsigned char*>(&session.challenge),
         32);

  // s = s0 + s1
  sc_add(final_sig.data + 32,
         reinterpret_cast<const unsigned char*>(&sig0.s),
         reinterpret_cast<const unsigned char*>(&sig1.s));
}

} // namespace Crypto
