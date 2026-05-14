// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "SwapTxBuilder.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "Common/StringTools.h"
#include "Serialization/BinarySerializationTools.h"
#include "CryptoNoteConfig.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include "crypto/crypto-ops.h"
}

namespace XfgSwap {

// ─── helpers ─────────────────────────────────────────────────────────

// sc_mul is static in musig2.cpp; replicate here.
// Computes r = a * b (mod L) via: sc_mulsub(neg, a, b, 0) => -a*b, then negate.
static void sc_mul_local(unsigned char* r,
                         const unsigned char* a,
                         const unsigned char* b) {
  unsigned char zero[32] = {};
  unsigned char neg[32];
  sc_mulsub(neg, a, b, zero);  // neg = 0 - a*b = -a*b
  sc_sub(r, zero, neg);        // r = 0 - (-a*b) = a*b
}

// Deterministic random scalar from seed + index. Both parties compute
// identical values so the decoy ring positions match exactly.
static void deterministicScalar(const Crypto::Hash& seed, uint32_t index,
                                Crypto::EllipticCurveScalar& out) {
  // H(seed || index) → reduce to scalar
  uint8_t buf[sizeof(seed) + sizeof(index)];
  std::memcpy(buf, &seed, sizeof(seed));
  std::memcpy(buf + sizeof(seed), &index, sizeof(index));
  Crypto::Hash h;
  Crypto::cn_fast_hash(buf, sizeof(buf), h);
  sc_reduce32(reinterpret_cast<unsigned char*>(&h));
  std::memcpy(&out, &h, 32);
}

// Point addition: result = a + b  (ge_p3 encoding via bytes)
static bool pointAdd(const uint8_t* a, const uint8_t* b, uint8_t* result) {
  ge_p3 A_p3, B_p3;
  if (ge_frombytes_vartime(&A_p3, a) != 0) return false;
  if (ge_frombytes_vartime(&B_p3, b) != 0) return false;

  ge_cached B_cached;
  ge_p3_to_cached(&B_cached, &B_p3);

  ge_p1p1 sum_p1p1;
  ge_add(&sum_p1p1, &A_p3, &B_cached);

  ge_p3 sum_p3;
  ge_p1p1_to_p3(&sum_p3, &sum_p1p1);
  ge_p3_tobytes(result, &sum_p3);
  return true;
}

// hash_to_ec: maps a public key to an ed25519 point (for key images).
// Replicates the static function in crypto.cpp.
static void hashToEc(const Crypto::PublicKey& key, ge_p3& result) {
  Crypto::Hash h;
  ge_p2 point;
  ge_p1p1 point2;
  Crypto::cn_fast_hash(&key, sizeof(Crypto::PublicKey), h);
  ge_fromfe_frombytes_vartime(&point, reinterpret_cast<const unsigned char*>(&h));
  ge_mul8(&point2, &point);
  ge_p1p1_to_p3(&result, &point2);
}

// Signer index in key aggregation: Alice = 0, Bob = 1.
static unsigned int signerIndex(SwapRole role) {
  return role == SwapRole::ALICE ? 0 : 1;
}

// ─── Step 1: Build unsigned tx ───────────────────────────────────────

bool SwapTxBuilder::buildUnsignedEscrowSpend(
    FuegoRpcClient& rpc,
    const SwapParams& params,
    const Crypto::PublicKey& destinationKey,
    uint64_t fee,
    CryptoNote::Transaction& tx,
    Crypto::Hash& prefixHash,
    CollaborativeRingState& ringState) {

  if (fee < MIN_FEE) fee = MIN_FEE;
  if (params.xfgAmount <= fee) return false;

  // Fetch decoy outputs (MIN_RING_SIZE - 1 decoys)
  std::vector<RandomOutputEntry> decoys;
  if (!rpc.getRandomOutputs(params.xfgAmount, MIN_RING_SIZE, decoys)) {
    return false;
  }

  // Remove any decoy that matches the real escrow output index
  decoys.erase(
      std::remove_if(decoys.begin(), decoys.end(),
                     [&](const RandomOutputEntry& e) {
                       return e.globalIndex == params.escrowOutputIndex;
                     }),
      decoys.end());

  // Ensure we have enough decoys
  if (decoys.size() < MIN_RING_SIZE - 1) {
    return false;
  }
  decoys.resize(MIN_RING_SIZE - 1);

  // Build sorted ring: insert real escrow output among decoys
  struct RingMember {
    uint32_t globalIndex;
    Crypto::PublicKey pubKey;
  };
  std::vector<RingMember> ring;
  ring.reserve(MIN_RING_SIZE);
  for (const auto& d : decoys) {
    ring.push_back({static_cast<uint32_t>(d.globalIndex), d.outKey});
  }
  ring.push_back({params.escrowOutputIndex, params.escrowPubKey});

  // Sort by global index (consensus expects relative offsets from sorted order)
  std::sort(ring.begin(), ring.end(),
            [](const RingMember& a, const RingMember& b) {
              return a.globalIndex < b.globalIndex;
            });

  // Find the real output's position in the sorted ring
  ringState.ringGlobalIndices.clear();
  ringState.ringPubKeys.clear();
  ringState.realIndex = 0;
  for (size_t i = 0; i < ring.size(); ++i) {
    ringState.ringGlobalIndices.push_back(ring[i].globalIndex);
    ringState.ringPubKeys.push_back(ring[i].pubKey);
    if (ring[i].globalIndex == params.escrowOutputIndex) {
      ringState.realIndex = i;
    }
  }

  // Convert absolute indices to relative offsets
  std::vector<uint32_t> relativeOffsets =
      CryptoNote::absolute_output_offsets_to_relative(ringState.ringGlobalIndices);

  // ── Build Transaction ──

  tx = CryptoNote::Transaction{};
  tx.version = CryptoNote::TRANSACTION_VERSION_1;
  tx.unlockTime = 0;

  // Generate a one-time tx keypair for the output derivation.
  // For the escrow spend, the destination key is already a one-time key
  // provided by the caller, so we use a fresh tx pubkey for extra.
  CryptoNote::KeyPair txKey;
  Crypto::generate_keys(txKey.publicKey, txKey.secretKey);
  CryptoNote::addTransactionPublicKeyToExtra(tx.extra, txKey.publicKey);

  // Input: KeyInput referencing the escrow with decoy ring.
  // Key image will be filled in after collaborative computation.
  CryptoNote::KeyInput input;
  input.amount = params.xfgAmount;
  input.outputIndexes = relativeOffsets;
  // Key image placeholder — filled after ringRound1Finalize
  std::memset(&input.keyImage, 0, sizeof(input.keyImage));
  tx.inputs.push_back(input);

  // Output: send (amount - fee) to destination
  CryptoNote::KeyOutput keyOut;
  keyOut.key = destinationKey;
  CryptoNote::TransactionOutput output;
  output.amount = params.xfgAmount - fee;
  output.target = keyOut;
  tx.outputs.push_back(output);

  // Empty signature slot (will be filled by ringRound2Finalize)
  tx.signatures.push_back(std::vector<Crypto::Signature>(ring.size()));

  // Compute prefix hash from the TransactionPrefix portion.
  // NOTE: key image is zero at this point. It must be set before broadcast,
  // but the prefix hash is computed from the serialized prefix which includes
  // the key image. We recompute the prefix hash after key image is set.
  // For now, return a placeholder.
  if (!CryptoNote::getObjectHash(static_cast<CryptoNote::TransactionPrefix&>(tx), prefixHash)) {
    return false;
  }

  return true;
}

// ─── Step 2: Key image ───────────────────────────────────────────────

void SwapTxBuilder::computePartialKeyImage(
    const SwapParams& params,
    Crypto::KeyImage& partialKI) {
  // effective_secret = coeff[my_index] * my_secret_key
  unsigned int idx = signerIndex(params.role);
  Crypto::EllipticCurveScalar effectiveSec;
  sc_mul_local(reinterpret_cast<unsigned char*>(&effectiveSec),
         reinterpret_cast<const unsigned char*>(&params.musig2.keyAgg.coeff[idx]),
         reinterpret_cast<const unsigned char*>(&params.ourSwapSecKey));

  // partialKI = effectiveSec * Hp(escrowPubKey)
  ge_p3 hp;
  hashToEc(params.escrowPubKey, hp);
  ge_p2 result;
  ge_scalarmult(&result, reinterpret_cast<const unsigned char*>(&effectiveSec), &hp);
  ge_tobytes(reinterpret_cast<unsigned char*>(&partialKI), &result);
}

bool SwapTxBuilder::aggregateKeyImages(
    const Crypto::KeyImage& ki0,
    const Crypto::KeyImage& ki1,
    Crypto::KeyImage& result) {
  return pointAdd(
      reinterpret_cast<const uint8_t*>(&ki0),
      reinterpret_cast<const uint8_t*>(&ki1),
      reinterpret_cast<uint8_t*>(&result));
}

// ─── Step 3: Ring Round 1 ────────────────────────────────────────────

void SwapTxBuilder::ringRound1Generate(
    const SwapParams& params,
    CollaborativeRingState& ringState) {
  // Compute partial key image
  computePartialKeyImage(params, ringState.ourPartialKeyImage);

  // Generate ring nonce k_ours
  Crypto::generate_keys(ringState.ourRingNoncePub,
      *reinterpret_cast<Crypto::SecretKey*>(&ringState.ourRingNonceSec));

  // Compute k_ours * Hp(escrowPub) for the ring's "b" component
  ge_p3 hp;
  hashToEc(params.escrowPubKey, hp);
  ge_p2 kHp;
  ge_scalarmult(&kHp, reinterpret_cast<const unsigned char*>(&ringState.ourRingNonceSec), &hp);
  ge_tobytes(reinterpret_cast<unsigned char*>(&ringState.ourRingNonceHp), &kHp);
}

bool SwapTxBuilder::ringRound1Finalize(
    const Crypto::Hash& prefixHash,
    CollaborativeRingState& ringState) {

  // Aggregate key image
  if (!aggregateKeyImages(ringState.ourPartialKeyImage,
                          ringState.peerPartialKeyImage,
                          ringState.aggregateKeyImage)) {
    return false;
  }

  // Aggregate nonce: K = K_ours + K_peer
  Crypto::PublicKey aggNoncePub;
  if (!pointAdd(reinterpret_cast<const uint8_t*>(&ringState.ourRingNoncePub),
                reinterpret_cast<const uint8_t*>(&ringState.peerRingNoncePub),
                reinterpret_cast<uint8_t*>(&aggNoncePub))) {
    return false;
  }
  Crypto::EllipticCurvePoint aggNonceHp;
  if (!pointAdd(reinterpret_cast<const uint8_t*>(&ringState.ourRingNonceHp),
                reinterpret_cast<const uint8_t*>(&ringState.peerRingNonceHp),
                reinterpret_cast<uint8_t*>(&aggNonceHp))) {
    return false;
  }

  // Build the ring signature challenge hash buffer.
  // Format matches crypto.cpp's generate_ring_signature:
  //   rs_comm { Hash h; struct { EllipticCurvePoint a, b; } ab[ring_size]; }
  //
  // For the real position: ab[j].a = aggNoncePub, ab[j].b = aggNonceHp
  // For decoy positions: ab[i].a and ab[i].b computed from deterministic (c_i, r_i)

  size_t ringSize = ringState.ringPubKeys.size();

  // Compute the shared seed for deterministic decoy values
  // We derive it from prefixHash (both parties have the same prefix)
  Crypto::Hash seed;
  Crypto::cn_fast_hash(prefixHash.data, sizeof(prefixHash.data), seed);

  // Prepare image for ge_dsm_precomp
  ge_p3 imageP3;
  if (ge_frombytes_vartime(&imageP3,
      reinterpret_cast<const unsigned char*>(&ringState.aggregateKeyImage)) != 0) {
    return false;
  }
  ge_dsmp imagePre;
  ge_dsm_precomp(imagePre, &imageP3);

  // Build the rs_comm buffer
  size_t bufSize = sizeof(Crypto::Hash) + ringSize * 2 * sizeof(Crypto::EllipticCurvePoint);
  std::vector<uint8_t> buf(bufSize);
  std::memcpy(buf.data(), &prefixHash, sizeof(Crypto::Hash));

  Crypto::EllipticCurveScalar sum;
  sc_0(reinterpret_cast<unsigned char*>(&sum));

  // Prepare decoy sigs
  ringState.decoySigs.resize(ringSize);

  for (size_t i = 0; i < ringSize; ++i) {
    uint8_t* ab_a = buf.data() + sizeof(Crypto::Hash) + i * 64;
    uint8_t* ab_b = ab_a + 32;

    if (i == ringState.realIndex) {
      // Real position: use aggregate nonce
      std::memcpy(ab_a, &aggNoncePub, 32);
      std::memcpy(ab_b, &aggNonceHp, 32);
    } else {
      // Decoy: deterministic c_i, r_i from seed
      Crypto::EllipticCurveScalar c_i, r_i;
      deterministicScalar(seed, static_cast<uint32_t>(i * 2), c_i);
      deterministicScalar(seed, static_cast<uint32_t>(i * 2 + 1), r_i);

      // Store in decoySigs
      std::memcpy(&ringState.decoySigs[i], &c_i, 32);
      std::memcpy(reinterpret_cast<uint8_t*>(&ringState.decoySigs[i]) + 32, &r_i, 32);

      // Compute ab[i].a = r_i*G + c_i*P_i
      ge_p3 P_i;
      if (ge_frombytes_vartime(&P_i,
          reinterpret_cast<const unsigned char*>(&ringState.ringPubKeys[i])) != 0) {
        return false;
      }
      ge_p2 tmp2;
      ge_double_scalarmult_base_vartime(&tmp2,
          reinterpret_cast<const unsigned char*>(&c_i),
          &P_i,
          reinterpret_cast<const unsigned char*>(&r_i));
      ge_tobytes(ab_a, &tmp2);

      // Compute ab[i].b = r_i*Hp(P_i) + c_i*I
      ge_p3 hpI;
      hashToEc(ringState.ringPubKeys[i], hpI);
      ge_double_scalarmult_precomp_vartime(&tmp2,
          reinterpret_cast<const unsigned char*>(&r_i),
          &hpI,
          reinterpret_cast<const unsigned char*>(&c_i),
          imagePre);
      ge_tobytes(ab_b, &tmp2);

      // Accumulate sum of decoy challenges
      sc_add(reinterpret_cast<unsigned char*>(&sum),
             reinterpret_cast<unsigned char*>(&sum),
             reinterpret_cast<const unsigned char*>(&c_i));
    }
  }

  // h = H(buf)
  Crypto::EllipticCurveScalar h;
  Crypto::cn_fast_hash(buf.data(), buf.size(),
      *reinterpret_cast<Crypto::Hash*>(&h));
  sc_reduce32(reinterpret_cast<unsigned char*>(&h));

  // c_real = h - sum
  sc_sub(reinterpret_cast<unsigned char*>(&ringState.realChallenge),
         reinterpret_cast<unsigned char*>(&h),
         reinterpret_cast<unsigned char*>(&sum));

  return true;
}

// ─── Step 4: Ring Round 2 ────────────────────────────────────────────

void SwapTxBuilder::ringRound2Sign(
    const SwapParams& params,
    CollaborativeRingState& ringState) {
  // r_ours = k_ours - c_j * coeff_ours * sec_ours
  unsigned int idx = signerIndex(params.role);

  // effective_secret = coeff * sec
  uint8_t effectiveSec[32];
  sc_mul_local(effectiveSec,
         reinterpret_cast<const unsigned char*>(&params.musig2.keyAgg.coeff[idx]),
         reinterpret_cast<const unsigned char*>(&params.ourSwapSecKey));

  // r_ours = k_ours - c_j * effective_secret
  sc_mulsub(reinterpret_cast<unsigned char*>(&ringState.ourPartialResponse),
            reinterpret_cast<const unsigned char*>(&ringState.realChallenge),
            effectiveSec,
            reinterpret_cast<const unsigned char*>(&ringState.ourRingNonceSec));

  // Zero the nonce secret
  std::memset(&ringState.ourRingNonceSec, 0, sizeof(ringState.ourRingNonceSec));
}

bool SwapTxBuilder::ringRound2Finalize(
    CollaborativeRingState& ringState,
    CryptoNote::Transaction& tx) {

  if (tx.signatures.empty() || tx.inputs.empty()) return false;

  size_t ringSize = ringState.ringPubKeys.size();
  if (tx.signatures[0].size() != ringSize) return false;

  // r_real = r_ours + r_peer
  Crypto::EllipticCurveScalar rReal;
  sc_add(reinterpret_cast<unsigned char*>(&rReal),
         reinterpret_cast<const unsigned char*>(&ringState.ourPartialResponse),
         reinterpret_cast<const unsigned char*>(&ringState.peerPartialResponse));

  // Assemble the complete ring signature
  for (size_t i = 0; i < ringSize; ++i) {
    if (i == ringState.realIndex) {
      // Real position: (c_j, r_j)
      std::memcpy(&tx.signatures[0][i], &ringState.realChallenge, 32);
      std::memcpy(reinterpret_cast<uint8_t*>(&tx.signatures[0][i]) + 32, &rReal, 32);
    } else {
      // Decoy: copy from decoySigs
      tx.signatures[0][i] = ringState.decoySigs[i];
    }
  }

  // Set key image in the input
  auto& input = boost::get<CryptoNote::KeyInput>(tx.inputs[0]);
  input.keyImage = ringState.aggregateKeyImage;

  return true;
}

// ─── Utility ─────────────────────────────────────────────────────────

std::string SwapTxBuilder::serializeToHex(const CryptoNote::Transaction& tx) {
  CryptoNote::BinaryArray ba = CryptoNote::storeToBinary(tx);
  return Common::toHex(ba.data(), ba.size());
}

Crypto::Hash SwapTxBuilder::sharedSeed(const SwapParams& params) {
  // Both parties share escrowTxHash — deterministic seed
  Crypto::Hash seed;
  Crypto::cn_fast_hash(params.escrowTxHash.data,
                       sizeof(params.escrowTxHash.data), seed);
  return seed;
}

} // namespace XfgSwap
