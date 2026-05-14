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

#include "SwapPeerProtocol.h"
#include "Common/JsonValue.h"
#include "Common/StringTools.h"

#include <cstring>

namespace XfgSwap {

// ── helpers ──────────────────────────────────────────────────────────

template <typename T>
static std::string podHex(const T& pod) {
  return Common::toHex(&pod, sizeof(pod));
}

template <typename T>
static bool hexPod(const std::string& hex, T& pod) {
  return Common::podFromHex(hex, pod);
}

// Serialize the Musig2PubNonce (2 × EllipticCurvePoint = 64 bytes).
static std::string nonceToHex(const Crypto::Musig2PubNonce& n) {
  return Common::toHex(&n, sizeof(n));
}

static bool hexToNonce(const std::string& hex, Crypto::Musig2PubNonce& n) {
  return Common::podFromHex(hex, n);
}

// Serialize the Musig2PartialSig (EllipticCurveScalar = 32 bytes).
static std::string partialSigToHex(const Crypto::Musig2PartialSig& s) {
  return Common::toHex(&s, sizeof(s));
}

static bool hexToPartialSig(const std::string& hex, Crypto::Musig2PartialSig& s) {
  return Common::podFromHex(hex, s);
}

// Serialize the DLEQProof (2 × EllipticCurveScalar = 64 bytes).
static std::string dleqToHex(const Crypto::DLEQProof& p) {
  return Common::toHex(&p, sizeof(p));
}

static bool hexToDleq(const std::string& hex, Crypto::DLEQProof& p) {
  return Common::podFromHex(hex, p);
}

// ── serialize ────────────────────────────────────────────────────────

std::string serializePeerMessage(const PeerMessage& msg) {
  Common::JsonValue root(Common::JsonValue::OBJECT);
  root.insert("type", static_cast<int64_t>(static_cast<uint8_t>(msg.type)));
  root.insert("swapId", msg.swapId);

  Common::JsonValue payload(Common::JsonValue::OBJECT);

  switch (msg.type) {
    case PeerMessageType::KEY_EXCHANGE:
      payload.insert("swapPubKey", podHex(msg.keyExchange.swapPubKey));
      break;

    case PeerMessageType::ADAPTOR_EXCHANGE:
      payload.insert("adaptorPoint", podHex(msg.adaptorExchange.adaptorPoint));
      payload.insert("adaptorDleqQ", podHex(msg.adaptorExchange.adaptorDleqQ));
      payload.insert("dleqProof", dleqToHex(msg.adaptorExchange.dleqProof));
      break;

    case PeerMessageType::NONCE_EXCHANGE:
      payload.insert("pubNonce", nonceToHex(msg.nonceExchange.pubNonce));
      break;

    case PeerMessageType::PARTIAL_SIG:
      payload.insert("partialSig", partialSigToHex(msg.partialSig.partialSig));
      break;

    case PeerMessageType::RING_ROUND1:
      payload.insert("partialKeyImage", podHex(msg.ringRound1.partialKeyImage));
      payload.insert("ringNoncePub", podHex(msg.ringRound1.ringNoncePub));
      payload.insert("ringNonceHp", podHex(msg.ringRound1.ringNonceHp));
      break;

    case PeerMessageType::RING_ROUND2:
      payload.insert("partialResponse", podHex(msg.ringRound2.partialResponse));
      break;

    case PeerMessageType::ABORT:
      break;
  }

  root.insert("payload", payload);
  return root.toString();
}

// ── deserialize ──────────────────────────────────────────────────────

bool deserializePeerMessage(const std::string& json, PeerMessage& msg) {
  try {
    Common::JsonValue root = Common::JsonValue::fromString(json);
    if (!root.isObject()) return false;

    msg.type = static_cast<PeerMessageType>(
        static_cast<uint8_t>(root("type").getInteger()));
    msg.swapId = root("swapId").getString();

    if (!root.contains("payload")) return false;
    const auto& p = root("payload");

    switch (msg.type) {
      case PeerMessageType::KEY_EXCHANGE:
        if (!hexPod(p("swapPubKey").getString(), msg.keyExchange.swapPubKey))
          return false;
        break;

      case PeerMessageType::ADAPTOR_EXCHANGE:
        if (!hexPod(p("adaptorPoint").getString(), msg.adaptorExchange.adaptorPoint))
          return false;
        if (!hexPod(p("adaptorDleqQ").getString(), msg.adaptorExchange.adaptorDleqQ))
          return false;
        if (!hexToDleq(p("dleqProof").getString(), msg.adaptorExchange.dleqProof))
          return false;
        break;

      case PeerMessageType::NONCE_EXCHANGE:
        if (!hexToNonce(p("pubNonce").getString(), msg.nonceExchange.pubNonce))
          return false;
        break;

      case PeerMessageType::PARTIAL_SIG:
        if (!hexToPartialSig(p("partialSig").getString(), msg.partialSig.partialSig))
          return false;
        break;

      case PeerMessageType::RING_ROUND1:
        if (!hexPod(p("partialKeyImage").getString(), msg.ringRound1.partialKeyImage))
          return false;
        if (!hexPod(p("ringNoncePub").getString(), msg.ringRound1.ringNoncePub))
          return false;
        if (!hexPod(p("ringNonceHp").getString(), msg.ringRound1.ringNonceHp))
          return false;
        break;

      case PeerMessageType::RING_ROUND2:
        if (!hexPod(p("partialResponse").getString(), msg.ringRound2.partialResponse))
          return false;
        break;

      case PeerMessageType::ABORT:
        break;

      default:
        return false;
    }

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace XfgSwap
