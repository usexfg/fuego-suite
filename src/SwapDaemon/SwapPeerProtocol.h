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

#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "crypto/musig2.h"
#include "crypto/dleq.h"

#include <string>
#include <cstdint>

namespace XfgSwap {

// ── Message type IDs ─────────────────────────────────────────────────

enum class PeerMessageType : uint8_t {
  // Phase 1: Key exchange
  KEY_EXCHANGE        = 1,    // Swap pubkey

  // Phase 2: Adaptor exchange (Bob → Alice)
  ADAPTOR_EXCHANGE    = 2,    // T, Q, DLEQ proof

  // Phase 3: Musig2 nonce exchange
  NONCE_EXCHANGE      = 3,    // Musig2 pub nonce

  // Phase 4: Partial sig exchange (for escrow pre-sig)
  PARTIAL_SIG         = 4,    // Musig2 partial sig

  // Phase 5: Ring signature collaboration (spend or refund)
  RING_ROUND1         = 10,   // Partial key image + ring nonce pub + ring nonce Hp
  RING_ROUND2         = 11,   // Partial response scalar

  // Control
  ABORT               = 99,   // Abort the swap
};

// ── Message payloads ─────────────────────────────────────────────────

// Phase 1: Each party sends their swap public key.
struct MsgKeyExchange {
  Crypto::PublicKey swapPubKey;
};

// Phase 2: Bob sends adaptor point T, DLEQ Q, and proof to Alice.
struct MsgAdaptorExchange {
  Crypto::PublicKey adaptorPoint;   // T = t*G
  Crypto::PublicKey adaptorDleqQ;   // Q = t*escrowPubKey
  Crypto::DLEQProof dleqProof;
};

// Phase 3: Both parties send their Musig2 public nonces.
struct MsgNonceExchange {
  Crypto::Musig2PubNonce pubNonce;
};

// Phase 4: Both parties send their Musig2 partial signatures.
struct MsgPartialSig {
  Crypto::Musig2PartialSig partialSig;
};

// Phase 5a (Round 1): Partial key image + ring nonce for collaborative ring sig.
struct MsgRingRound1 {
  Crypto::KeyImage partialKeyImage;
  Crypto::PublicKey ringNoncePub;             // k*G
  Crypto::EllipticCurvePoint ringNonceHp;     // k*Hp(escrowPub)
};

// Phase 5b (Round 2): Partial response for the real ring position.
struct MsgRingRound2 {
  Crypto::EllipticCurveScalar partialResponse;
};

// ── Wire envelope ────────────────────────────────────────────────────

// All messages are wrapped in a JSON envelope:
// {
//   "type": <uint8_t>,
//   "swapId": "<string>",
//   "payload": { ... hex-encoded fields ... }
// }

struct PeerMessage {
  PeerMessageType type;
  std::string swapId;

  // Exactly one of these is populated (based on type)
  MsgKeyExchange keyExchange;
  MsgAdaptorExchange adaptorExchange;
  MsgNonceExchange nonceExchange;
  MsgPartialSig partialSig;
  MsgRingRound1 ringRound1;
  MsgRingRound2 ringRound2;
};

// ── Serialization ────────────────────────────────────────────────────

// Serialize a PeerMessage to JSON string.
std::string serializePeerMessage(const PeerMessage& msg);

// Deserialize a JSON string to PeerMessage.
// Returns false on parse error.
bool deserializePeerMessage(const std::string& json, PeerMessage& msg);

} // namespace XfgSwap
