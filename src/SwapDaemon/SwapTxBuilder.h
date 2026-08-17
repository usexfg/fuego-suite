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

#pragma once

#include "SwapTypes.h"
#include "FuegoRpcClient.h"
#include "CryptoNote.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"

#include <vector>
#include <string>

namespace XfgSwap {

// State for collaborative ring signature between Alice and Bob.
// The 2-of-2 Musig2 escrow requires a shared ring of MIN_RING_SIZE outputs
// (real + decoys). Neither party holds the aggregate secret, so the ring
// signature is constructed collaboratively in 2 rounds. The decoy set is
// agreed through RING_ROUND1 (descriptor exchange) so both parties sign the
// exact same ring.
struct CollaborativeRingState {
  // Agreed decoy set (sorted absolute global indices, including real)
  std::vector<uint32_t> ringGlobalIndices;
  std::vector<Crypto::PublicKey> ringPubKeys;
  size_t realIndex = 0;                     // position of escrow output in ring

  // Partial key images: KI = KI_ours + KI_peer
  Crypto::KeyImage ourPartialKeyImage;
  Crypto::KeyImage peerPartialKeyImage;
  Crypto::KeyImage aggregateKeyImage;

  // Ring nonces: K = K_ours + K_peer (for k*G and k*Hp positions)
  Crypto::PublicKey ourRingNoncePub;         // k_ours * G
  Crypto::EllipticCurvePoint ourRingNonceHp; // k_ours * Hp(escrowPub)
  Crypto::EllipticCurveScalar ourRingNonceSec; // k_ours (secret)
  Crypto::PublicKey peerRingNoncePub;
  Crypto::EllipticCurvePoint peerRingNonceHp;

  // After challenge computation
  Crypto::EllipticCurveScalar realChallenge;  // c_j for the real position

  // Partial responses: r_real = r_ours + r_peer
  Crypto::EllipticCurveScalar ourPartialResponse;
  Crypto::EllipticCurveScalar peerPartialResponse;

  // Deterministic decoy signatures (from shared seed)
  // sig[i] for i != realIndex are random c_i, r_i from the shared seed
  std::vector<Crypto::Signature> decoySigs;
};

// Minimal transaction builder for Musig2 escrow spend/refund.
//
// Builds a v1 CryptoNote::Transaction with:
//   - One KeyInput referencing the escrow output (ring of decoys)
//   - One or two KeyOutput(s) (destination + optional change)
//   - Ring signature via collaborative 2-party protocol
class SwapTxBuilder {
public:
  // Minimum ring size: 8 decoys + 1 real = 9 entries, sampled from regular
  // transaction outputs of the escrow amount. The full ring is exchanged in
  // RING_ROUND1 so both parties sign identical prefixes.
  static constexpr size_t MIN_RING_SIZE = 9;
  // Minimum fee in atomic units (0.001 XFG)
  static constexpr uint64_t MIN_FEE = 10000;

  // ── Step 1: Build unsigned transaction skeleton ──────────────────────

  // Construct the unsigned transaction spending the escrow output.
  // Either fetches decoys from the daemon (descriptor initiator) or adopts
  // the peer-agreed ring descriptor (all subsequent builds), then builds
  // inputs/outputs and computes the tx prefix hash. The signature slot is
  // left empty.
  //
  // ringGlobalIndices/ringPubKeys: optional peer-agreed ring. When provided,
  // the real escrow entry must be present exactly once; decoy fetch is
  // skipped so both parties build identical prefixes.
  //
  // Returns false if decoy fetch fails or params are invalid.
  // On success: tx, prefixHash, and ringState are populated.
  static bool buildUnsignedEscrowSpend(
      FuegoRpcClient& rpc,
      const SwapParams& params,
      const Crypto::PublicKey& destinationKey,  // one-time output key for recipient
      uint64_t protocolFee,                      // protocol fee sent to treasury (0 = no treasury output)
      const Crypto::PublicKey& treasuryKey,      // treasury output key (ignored if protocolFee == 0)
      uint64_t fee,                              // network fee
      CryptoNote::Transaction& tx,
      Crypto::Hash& prefixHash,
      CollaborativeRingState& ringState,
      const std::vector<uint32_t>* ringGlobalIndices = nullptr,
      const std::vector<Crypto::PublicKey>* ringPubKeys = nullptr);

  // ── Step 2: Collaborative key image ──────────────────────────────────

  // Compute this party's partial key image contribution.
  // KI_ours = (coeff_ours * sec_ours) * Hp(escrowPubKey)
  static void computePartialKeyImage(
      const SwapParams& params,
      Crypto::KeyImage& partialKI);

  // Aggregate two partial key images via point addition.
  // KI = KI_alice + KI_bob
  static bool aggregateKeyImages(
      const Crypto::KeyImage& ki0,
      const Crypto::KeyImage& ki1,
      Crypto::KeyImage& result);

  // ── Step 3: Collaborative ring signature (Round 1) ───────────────────

  // Generate ring nonce and partial key image for exchange with peer.
  // Populates ringState.ourRingNonce* and ringState.ourPartialKeyImage.
  static void ringRound1Generate(
      const SwapParams& params,
      CollaborativeRingState& ringState);

  // After receiving peer's Round 1 data, compute the aggregate nonce
  // point and key image, fill in the deterministic decoy sigs, and
  // derive the real-position challenge c_j.
  // Both parties call this with identical inputs and get identical c_j.
  //
  // CRITICAL: prefixHash MUST be computed AFTER writing aggregateKeyImage into
  // the KeyInput. Call order:
  //   1. ringRound1Generate (or restore persisted our Round 1 material)
  //   2. set peer Round 1 fields
  //   3. writeAggregateKeyImageToTx(tx, ringState)  // aggregates KI + sets input
  //   4. recompute prefixHash from tx
  //   5. ringRound1Finalize(prefixHash, ringState)
  //   6. ringRound2Sign / ringRound2Finalize
  static bool ringRound1Finalize(
      const Crypto::Hash& prefixHash,
      CollaborativeRingState& ringState);

  // Aggregate our+peer partial key images and write into tx.inputs[0].keyImage.
  // Must be called before computing the prefix hash used by ringRound1Finalize.
  static bool writeAggregateKeyImageToTx(
      CryptoNote::Transaction& tx,
      CollaborativeRingState& ringState);

  // ── Step 4: Collaborative ring signature (Round 2) ───────────────────

  // Compute our partial response for the real ring position.
  // r_ours = k_ours - c_j * coeff_ours * sec_ours
  static void ringRound2Sign(
      const SwapParams& params,
      CollaborativeRingState& ringState);

  // After receiving peer's partial response, assemble the complete ring
  // signature and attach it to the transaction.
  static bool ringRound2Finalize(
      CollaborativeRingState& ringState,
      CryptoNote::Transaction& tx);

  // ── Utility ──────────────────────────────────────────────────────────

  // Serialize a transaction to hex for sendRawTransaction.
  static std::string serializeToHex(const CryptoNote::Transaction& tx);

  // Generate a deterministic shared seed from the escrow tx hash.
  // Both parties derive identical decoy ring sig values from this.
  static Crypto::Hash sharedSeed(const SwapParams& params);
};

} // namespace XfgSwap
