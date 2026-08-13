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
#include "crypto/hash.h"

#include <cstring>
#include <vector>

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

// ── digest / sign / verify ───────────────────────────────────────────
//
// Canonical layout (length-prefix-free; types are fixed-size or have a
// type-determined order, so concatenation is unambiguous):
//   1 byte    msg.type
//   4 bytes   little-endian length of swapId
//   N bytes   swapId
//   ...       payload fields in struct-declaration order, raw bytes
//
// Anything outside the union for the message's type is excluded.

static void appendU32LE(std::vector<uint8_t>& buf, uint32_t v) {
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

template <typename T>
static void appendPod(std::vector<uint8_t>& buf, const T& v) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
  buf.insert(buf.end(), p, p + sizeof(T));
}

Crypto::Hash peerMessageDigest(const PeerMessage& msg) {
  std::vector<uint8_t> buf;
  buf.reserve(1 + 4 + msg.swapId.size() + 128);
  buf.push_back(static_cast<uint8_t>(msg.type));
  appendU32LE(buf, static_cast<uint32_t>(msg.swapId.size()));
  buf.insert(buf.end(), msg.swapId.begin(), msg.swapId.end());

  switch (msg.type) {
    case PeerMessageType::KEY_EXCHANGE:
      appendPod(buf, msg.keyExchange.swapPubKey);
      break;
    case PeerMessageType::ADAPTOR_EXCHANGE:
      appendPod(buf, msg.adaptorExchange.adaptorPoint);
      appendPod(buf, msg.adaptorExchange.adaptorDleqQ);
      appendPod(buf, msg.adaptorExchange.dleqProof);
      appendPod(buf, msg.adaptorExchange.htlcHashLock);
      break;
    case PeerMessageType::NONCE_EXCHANGE:
      appendPod(buf, msg.nonceExchange.pubNonce);
      break;
    case PeerMessageType::PARTIAL_SIG:
      appendPod(buf, msg.partialSig.partialSig);
      break;
    case PeerMessageType::ESCROW_FUNDED:
      appendPod(buf, msg.escrowFunded.escrowTxHash);
      break;
    case PeerMessageType::RING_ROUND1:
      appendPod(buf, msg.ringRound1.partialKeyImage);
      appendPod(buf, msg.ringRound1.ringNoncePub);
      appendPod(buf, msg.ringRound1.ringNonceHp);
      break;
    case PeerMessageType::RING_ROUND2:
      appendPod(buf, msg.ringRound2.partialResponse);
      break;
    case PeerMessageType::SECRET_REVEAL:
      appendPod(buf, msg.secretReveal.adaptorSecret);
      if (!msg.secretReveal.claimTxId.empty()) {
        buf.insert(buf.end(), msg.secretReveal.claimTxId.begin(),
                   msg.secretReveal.claimTxId.end());
      }
      break;
    case PeerMessageType::AFK_CLAIM:
      // Length-prefixed strings: prevents cross-field concatenation ambiguity.
      appendU32LE(buf, static_cast<uint32_t>(msg.afkClaim.ctrLockTxId.size()));
      buf.insert(buf.end(), msg.afkClaim.ctrLockTxId.begin(), msg.afkClaim.ctrLockTxId.end());
      appendU32LE(buf, static_cast<uint32_t>(msg.afkClaim.payoutAddress.size()));
      buf.insert(buf.end(), msg.afkClaim.payoutAddress.begin(), msg.afkClaim.payoutAddress.end());
      appendU32LE(buf, static_cast<uint32_t>(msg.afkClaim.finalSigHex.size()));
      buf.insert(buf.end(), msg.afkClaim.finalSigHex.begin(), msg.afkClaim.finalSigHex.end());
      break;
    case PeerMessageType::AFK_CLAIM_ACK:
      break;
    case PeerMessageType::ABORT:
      break;
  }

  Crypto::Hash h;
  Crypto::cn_fast_hash(buf.data(), buf.size(), h);
  return h;
}

bool signPeerMessage(PeerMessage& msg,
                     const Crypto::PublicKey& pub,
                     const Crypto::SecretKey& sec) {
  Crypto::Hash digest = peerMessageDigest(msg);
  Crypto::generate_signature(digest, pub, sec, msg.signature);
  return true;
}

bool verifyPeerMessage(const PeerMessage& msg, const Crypto::PublicKey& pub) {
  Crypto::Hash digest = peerMessageDigest(msg);
  return Crypto::check_signature(digest, pub, msg.signature);
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
      payload.insert("htlcHashLock", podHex(msg.adaptorExchange.htlcHashLock));
      break;

    case PeerMessageType::NONCE_EXCHANGE:
      payload.insert("pubNonce", nonceToHex(msg.nonceExchange.pubNonce));
      break;

    case PeerMessageType::PARTIAL_SIG:
      payload.insert("partialSig", partialSigToHex(msg.partialSig.partialSig));
      break;

    case PeerMessageType::ESCROW_FUNDED:
      payload.insert("escrowTxHash", podHex(msg.escrowFunded.escrowTxHash));
      break;

    case PeerMessageType::RING_ROUND1:
      payload.insert("partialKeyImage", podHex(msg.ringRound1.partialKeyImage));
      payload.insert("ringNoncePub", podHex(msg.ringRound1.ringNoncePub));
      payload.insert("ringNonceHp", podHex(msg.ringRound1.ringNonceHp));
      break;

    case PeerMessageType::RING_ROUND2:
      payload.insert("partialResponse", podHex(msg.ringRound2.partialResponse));
      break;

    case PeerMessageType::SECRET_REVEAL:
      payload.insert("adaptorSecret", podHex(msg.secretReveal.adaptorSecret));
      if (!msg.secretReveal.claimTxId.empty())
        payload.insert("claimTxId", msg.secretReveal.claimTxId);
      break;

    case PeerMessageType::AFK_CLAIM:
      payload.insert("ctrLockTxId", msg.afkClaim.ctrLockTxId);
      payload.insert("payoutAddress", msg.afkClaim.payoutAddress);
      payload.insert("finalSigHex", msg.afkClaim.finalSigHex);
      break;

    case PeerMessageType::AFK_CLAIM_ACK:
      break;

    case PeerMessageType::ABORT:
      break;
  }

  root.insert("payload", payload);
  root.insert("signature", podHex(msg.signature));
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
        if (p.contains("htlcHashLock") && !p("htlcHashLock").getString().empty()) {
          if (!hexPod(p("htlcHashLock").getString(), msg.adaptorExchange.htlcHashLock))
            return false;
        }
        break;

      case PeerMessageType::NONCE_EXCHANGE:
        if (!hexToNonce(p("pubNonce").getString(), msg.nonceExchange.pubNonce))
          return false;
        break;

      case PeerMessageType::PARTIAL_SIG:
        if (!hexToPartialSig(p("partialSig").getString(), msg.partialSig.partialSig))
          return false;
        break;

      case PeerMessageType::ESCROW_FUNDED:
        if (!hexPod(p("escrowTxHash").getString(), msg.escrowFunded.escrowTxHash))
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

      case PeerMessageType::SECRET_REVEAL:
        if (!hexPod(p("adaptorSecret").getString(), msg.secretReveal.adaptorSecret))
          return false;
        if (p.contains("claimTxId") && p("claimTxId").isString())
          msg.secretReveal.claimTxId = p("claimTxId").getString();
        break;

      case PeerMessageType::AFK_CLAIM:
        if (p.contains("ctrLockTxId") && p("ctrLockTxId").isString())
          msg.afkClaim.ctrLockTxId = p("ctrLockTxId").getString();
        if (p.contains("payoutAddress") && p("payoutAddress").isString())
          msg.afkClaim.payoutAddress = p("payoutAddress").getString();
        if (p.contains("finalSigHex") && p("finalSigHex").isString())
          msg.afkClaim.finalSigHex = p("finalSigHex").getString();
        break;

      case PeerMessageType::AFK_CLAIM_ACK:
        break;

      case PeerMessageType::ABORT:
        break;

      default:
        return false;
    }

    if (!root.contains("signature")) {
      // Pre-auth wire format from before M2 — reject (no swaps were live on
      // mainnet so there are no legitimate unsigned messages in existence).
      return false;
    }
    if (!hexPod(root("signature").getString(), msg.signature)) return false;

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace XfgSwap
