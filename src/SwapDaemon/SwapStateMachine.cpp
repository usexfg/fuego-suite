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

#include "SwapStateMachine.h"
#include "SwapSecretEncryption.h"
#include "Common/JsonValue.h"
#include "Common/StringTools.h"
#include "../Common/pod-class.h"
#include "../crypto/hash.h"
#include <sstream>
#include <cstring>

namespace XfgSwap {

SwapStateMachine::SwapStateMachine()
  : m_params()
  , m_state(SwapState::INITIATED)
  , m_createdAt(std::time(nullptr))
  , m_updatedAt(m_createdAt) {
  std::memset(&m_params.aliceXfgPubKey, 0, sizeof(m_params.aliceXfgPubKey));
  std::memset(&m_params.bobXfgPubKey, 0, sizeof(m_params.bobXfgPubKey));
  std::memset(&m_params.ourSwapSecKey, 0, sizeof(m_params.ourSwapSecKey));
  std::memset(&m_params.ourSwapPubKey, 0, sizeof(m_params.ourSwapPubKey));
  std::memset(&m_params.peerSwapPubKey, 0, sizeof(m_params.peerSwapPubKey));
  std::memset(&m_params.escrowPubKey, 0, sizeof(m_params.escrowPubKey));
  std::memset(&m_params.adaptorPoint, 0, sizeof(m_params.adaptorPoint));
  std::memset(&m_params.adaptorSecret, 0, sizeof(m_params.adaptorSecret));
  std::memset(&m_params.adaptorDleqQ, 0, sizeof(m_params.adaptorDleqQ));
  std::memset(&m_params.escrowTxHash, 0, sizeof(m_params.escrowTxHash));
  std::memset(&m_params.hashLock, 0, sizeof(m_params.hashLock));
  std::memset(&m_params.preimage, 0, sizeof(m_params.preimage));
  std::memset(&m_params.ringPeerPartialKeyImage, 0, sizeof(m_params.ringPeerPartialKeyImage));
  std::memset(&m_params.ringPeerRingNoncePub, 0, sizeof(m_params.ringPeerRingNoncePub));
  std::memset(&m_params.ringPeerRingNonceHp, 0, sizeof(m_params.ringPeerRingNonceHp));
  std::memset(&m_params.ringPeerPartialResponse, 0, sizeof(m_params.ringPeerPartialResponse));
  m_params.ringPeerRound1Received = false;
  m_params.ringPeerRound2Received = false;
  m_params.ringOurRound1Sent = false;
  m_params.ringOurRound2Sent = false;
  m_params.ringTxBroadcast = false;
  m_params.pair = SwapPair::SOL;
  m_params.role = SwapRole::BOB;
  m_params.xfgAmount = 0;
  m_params.ctrAmount = 0;
  m_params.xfgTimeoutHeight = 0;
  m_params.ctrTimeoutBlock = 0;
  m_params.escrowOutputIndex = 0;
  m_params.htlcOutputIndex = 0;
}

SwapStateMachine::SwapStateMachine(SwapParams params)
  : m_params(std::move(params))
  , m_state(SwapState::INITIATED)
  , m_createdAt(std::time(nullptr))
  , m_updatedAt(m_createdAt) {
  // Crypto POD fields in SwapParams may be uninitialized if the caller used
  // bare `SwapParams p;` — leave encryption fail-closed honest only when
  // secrets are intentionally non-zero.
}

bool SwapStateMachine::isValidTransition(SwapState newState) const {
  // Any state can transition to FAILED
  if (newState == SwapState::FAILED) {
    return true;
  }

  switch (m_state) {
    // ── ADAPTOR SWAP STATES (active — v1) ────────────────────────────────────
    case SwapState::INITIATED:
      return newState == SwapState::ADAPTOR_KEYS_EXCHANGED ||
             newState == SwapState::AFK_OFFER_LOCKED;

    case SwapState::ADAPTOR_KEYS_EXCHANGED:
      return newState == SwapState::ADAPTOR_ESCROW_FUNDED;

    case SwapState::ADAPTOR_ESCROW_FUNDED:
      return newState == SwapState::ADAPTOR_PRESIGS_READY ||
             newState == SwapState::ADAPTOR_REFUNDED;

    case SwapState::ADAPTOR_PRESIGS_READY:
      return newState == SwapState::ADAPTOR_CTR_LOCKED ||
             newState == SwapState::ADAPTOR_REFUNDED;

    case SwapState::ADAPTOR_CTR_LOCKED:
      return newState == SwapState::ADAPTOR_SECRET_REVEALED ||
             newState == SwapState::ADAPTOR_REFUNDED;

    case SwapState::ADAPTOR_SECRET_REVEALED:
      return newState == SwapState::ADAPTOR_XFG_SPENT ||
             newState == SwapState::ADAPTOR_WAITING_SPV;

    case SwapState::ADAPTOR_WAITING_SPV:
      return newState == SwapState::ADAPTOR_SECRET_CONFIRMED_SPV ||
             newState == SwapState::ADAPTOR_XFG_SPENT ||
             newState == SwapState::ADAPTOR_REFUNDED;

    case SwapState::ADAPTOR_SECRET_CONFIRMED_SPV:
      return newState == SwapState::ADAPTOR_XFG_SPENT ||
             newState == SwapState::ADAPTOR_REFUNDED;

    case SwapState::AFK_OFFER_LOCKED:
      return newState == SwapState::AFK_OFFER_ACCEPTED ||
             newState == SwapState::AFK_REFUNDED;

    case SwapState::AFK_OFFER_ACCEPTED:
      return newState == SwapState::AFK_CLAIMED ||
             newState == SwapState::AFK_REFUNDED;

    // Terminal states
    case SwapState::ADAPTOR_XFG_SPENT:
    case SwapState::ADAPTOR_REFUNDED:
    case SwapState::FAILED:
      return false;

    // Legacy HTLC states (kept for DB compat, no transitions)
    default:
      return false;
  }
}

bool SwapStateMachine::transition(SwapState newState, uint32_t currentHeight) {
  if (!isValidTransition(newState)) {
    return false;
  }

  if (newState == SwapState::AFK_REFUNDED ||
      newState == SwapState::ADAPTOR_REFUNDED ||
      newState == SwapState::XFG_REFUNDED ||
      newState == SwapState::CTR_REFUNDED) {
    if (currentHeight == 0) {
      return false;
    }
    if (m_params.xfgTimeoutHeight > 0 && currentHeight < m_params.xfgTimeoutHeight) {
      return false;
    }
  }

  m_state = newState;
  m_updatedAt = std::time(nullptr);
  return true;
}

SwapState SwapStateMachine::currentState() const {
  return m_state;
}

SwapParams& SwapStateMachine::params() {
  return m_params;
}

const SwapParams& SwapStateMachine::params() const {
  return m_params;
}

time_t SwapStateMachine::createdAt() const {
  return m_createdAt;
}

time_t SwapStateMachine::updatedAt() const {
  return m_updatedAt;
}

bool SwapStateMachine::isTerminal() const {
  return m_state == SwapState::ADAPTOR_XFG_SPENT ||
          m_state == SwapState::ADAPTOR_REFUNDED ||
          m_state == SwapState::FAILED ||
          m_state == SwapState::AFK_CLAIMED ||
          m_state == SwapState::AFK_REFUNDED ||
          // Legacy HTLC states (protocol v1, inactive but kept for DB compat).
         // XFG_CLAIMED and CTR_CLAIMED represent successful completion.
         // XFG_REFUNDED and CTR_REFUNDED represent timeout/refund completion.
         // XFG_LOCKED and CTR_LOCKED are intermediate — NOT included here.
         m_state == SwapState::XFG_CLAIMED ||    // Alice claimed XFG (success)
         m_state == SwapState::CTR_CLAIMED ||    // Bob claimed counterparty (success)
         m_state == SwapState::XFG_REFUNDED ||   // Bob refunded XFG after timeout
         m_state == SwapState::CTR_REFUNDED;     // Alice refunded counterparty after timeout
}

std::string SwapStateMachine::serialize() const {
  Common::JsonValue root(Common::JsonValue::OBJECT);

  // Serialization version — bump when adding new fields.
  root.insert("serVersion", static_cast<int64_t>(3));

  root.insert("swapId", m_params.swapId);
  root.insert("pair", static_cast<int64_t>(static_cast<uint8_t>(m_params.pair)));
  root.insert("role", static_cast<int64_t>(static_cast<uint8_t>(m_params.role)));
  root.insert("xfgAmount", static_cast<int64_t>(m_params.xfgAmount));
  root.insert("ctrAmount", static_cast<int64_t>(m_params.ctrAmount));
  root.insert("state", static_cast<int64_t>(static_cast<uint8_t>(m_state)));

  // Adaptor sig fields
  root.insert("ourSwapPubKey", Common::podToHex(m_params.ourSwapPubKey));
  root.insert("peerSwapPubKey", Common::podToHex(m_params.peerSwapPubKey));
  root.insert("expectedPeerSwapPubKey", Common::podToHex(m_params.expectedPeerSwapPubKey));
  root.insert("escrowPubKey", Common::podToHex(m_params.escrowPubKey));
  root.insert("adaptorPoint", Common::podToHex(m_params.adaptorPoint));
  root.insert("adaptorDleqQ", Common::podToHex(m_params.adaptorDleqQ));
  root.insert("escrowTxHash", Common::podToHex(m_params.escrowTxHash));
  root.insert("escrowOutputIndex", static_cast<int64_t>(m_params.escrowOutputIndex));

  // Persist secrets encrypted at rest. No plaintext fallback.
  // Encryption key must be set before serialize(). Format per secret:
  //   nonce(8) || salt(16) || ciphertext(32) || tag(32) = 88 bytes → 176 hex.
  auto isNonZero = [](const Crypto::SecretKey& s) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
    for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return true;
    return false;
  };
  const bool needEnc = isNonZero(m_params.adaptorSecret) || isNonZero(m_params.ourSwapSecKey)
      || m_params.ringOurRound1MaterialValid;
  if (needEnc && !hasEncryptionKey()) {
    // Refuse to write a record that would permanently drop live secrets.
    return "";
  }

  auto packEncrypted = [&](const Crypto::SecretKey& secret, std::string& hexOut) -> bool {
    SwapSecretEncryption::EncryptedSecret encrypted;
    if (!SwapSecretEncryption::encrypt(secret, m_encryptionKey, encrypted)) return false;
    std::string encData;
    encData.reserve(CHACHA8_NONCE_SIZE + SALT_SIZE + encrypted.ciphertext.size() + TAG_SIZE);
    encData.insert(encData.end(), encrypted.nonce.begin(), encrypted.nonce.end());
    encData.insert(encData.end(), encrypted.salt.begin(), encrypted.salt.end());
    encData.insert(encData.end(), encrypted.ciphertext.begin(), encrypted.ciphertext.end());
    encData.insert(encData.end(), encrypted.tag.begin(), encrypted.tag.end());
    hexOut = Common::toHex(encData.data(), encData.size());
    return true;
  };

  if (hasEncryptionKey()) {
    std::string adaptorHex, secKeyHex, ringNonceHex;
    if (!packEncrypted(m_params.adaptorSecret, adaptorHex)) return "";
    if (!packEncrypted(m_params.ourSwapSecKey, secKeyHex)) return "";
    // ring nonce secret is an EllipticCurveScalar (32 bytes) — same size as SecretKey
    Crypto::SecretKey ringNonceAsKey;
    std::memcpy(&ringNonceAsKey, &m_params.ringOurRingNonceSec, 32);
    if (!packEncrypted(ringNonceAsKey, ringNonceHex)) return "";
    root.insert("adaptorSecretEnc", adaptorHex);
    root.insert("ourSwapSecKeyEnc", secKeyHex);
    root.insert("ringOurRingNonceSecEnc", ringNonceHex);
  } else {
    root.insert("adaptorSecretEnc", "");
    root.insert("ourSwapSecKeyEnc", "");
    root.insert("ringOurRingNonceSecEnc", "");
  }

  // Legacy fields (kept for backward compat in DB)
  root.insert("aliceXfgPubKey", Common::podToHex(m_params.aliceXfgPubKey));
  root.insert("bobXfgPubKey", Common::podToHex(m_params.bobXfgPubKey));

  root.insert("xfgTimeoutHeight", static_cast<int64_t>(m_params.xfgTimeoutHeight));
  root.insert("ctrTimeoutBlock", static_cast<int64_t>(m_params.ctrTimeoutBlock));

  root.insert("ctrLockTxId", m_params.ctrLockTxId);
  root.insert("ctrClaimTxId", m_params.ctrClaimTxId);
  root.insert("ctrAddress", m_params.ctrAddress);
  root.insert("peerEndpoint", m_params.peerEndpoint);
  root.insert("chainState", m_params.chainState);

  // Collaborative ring state (peer + our public Round 1 material)
  root.insert("ringPeerPartialKeyImage", Common::podToHex(m_params.ringPeerPartialKeyImage));
  root.insert("ringPeerRingNoncePub",    Common::podToHex(m_params.ringPeerRingNoncePub));
  root.insert("ringPeerRingNonceHp",     Common::podToHex(m_params.ringPeerRingNonceHp));
  root.insert("ringPeerPartialResponse", Common::podToHex(m_params.ringPeerPartialResponse));
  root.insert("ringPeerRound1Received", static_cast<int64_t>(m_params.ringPeerRound1Received ? 1 : 0));
  root.insert("ringPeerRound2Received", static_cast<int64_t>(m_params.ringPeerRound2Received ? 1 : 0));
  root.insert("ringOurRound1Sent", static_cast<int64_t>(m_params.ringOurRound1Sent ? 1 : 0));
  root.insert("ringOurRound2Sent", static_cast<int64_t>(m_params.ringOurRound2Sent ? 1 : 0));
  root.insert("ringTxBroadcast", static_cast<int64_t>(m_params.ringTxBroadcast ? 1 : 0));

  root.insert("ringOurPartialKeyImage", Common::podToHex(m_params.ringOurPartialKeyImage));
  root.insert("ringOurRingNoncePub",    Common::podToHex(m_params.ringOurRingNoncePub));
  root.insert("ringOurRingNonceHp",     Common::podToHex(m_params.ringOurRingNonceHp));
  root.insert("ringOurPartialResponse", Common::podToHex(m_params.ringOurPartialResponse));
  root.insert("ringOurRound1MaterialValid",
              static_cast<int64_t>(m_params.ringOurRound1MaterialValid ? 1 : 0));
  root.insert("adaptorSecretRevealedToPeer",
              static_cast<int64_t>(m_params.adaptorSecretRevealedToPeer ? 1 : 0));
  root.insert("adaptorSecretReceived",
              static_cast<int64_t>(m_params.adaptorSecretReceived ? 1 : 0));

  root.insert("createdAt", static_cast<int64_t>(m_createdAt));
  root.insert("updatedAt", static_cast<int64_t>(m_updatedAt));

  return root.toString();
}

SwapStateMachine SwapStateMachine::deserialize(const std::string& json) {
  Common::JsonValue root = Common::JsonValue::fromString(json);

  SwapParams params{};
  // Secrets are never in plaintext JSON. Zero them until decryptStoredSecret().
  std::memset(&params.ourSwapSecKey, 0, sizeof(params.ourSwapSecKey));
  std::memset(&params.adaptorSecret, 0, sizeof(params.adaptorSecret));
  std::memset(&params.ringOurRingNonceSec, 0, sizeof(params.ringOurRingNonceSec));
  std::memset(&params.preimage, 0, sizeof(params.preimage));

  params.swapId = root("swapId").getString();
  params.pair = static_cast<SwapPair>(static_cast<uint8_t>(root("pair").getInteger()));
  params.role = static_cast<SwapRole>(static_cast<uint8_t>(root("role").getInteger()));
  params.xfgAmount = static_cast<uint64_t>(root("xfgAmount").getInteger());
  params.ctrAmount = static_cast<uint64_t>(root("ctrAmount").getInteger());

  // Adaptor sig fields
  if (root.contains("ourSwapPubKey"))
    Common::podFromHex(root("ourSwapPubKey").getString(), params.ourSwapPubKey);
  if (root.contains("peerSwapPubKey"))
    Common::podFromHex(root("peerSwapPubKey").getString(), params.peerSwapPubKey);
  if (root.contains("expectedPeerSwapPubKey"))
    Common::podFromHex(root("expectedPeerSwapPubKey").getString(), params.expectedPeerSwapPubKey);
  if (root.contains("escrowPubKey"))
    Common::podFromHex(root("escrowPubKey").getString(), params.escrowPubKey);
  if (root.contains("adaptorPoint"))
    Common::podFromHex(root("adaptorPoint").getString(), params.adaptorPoint);
  if (root.contains("adaptorDleqQ"))
    Common::podFromHex(root("adaptorDleqQ").getString(), params.adaptorDleqQ);
  if (root.contains("escrowTxHash"))
    Common::podFromHex(root("escrowTxHash").getString(), params.escrowTxHash);
  if (root.contains("escrowOutputIndex"))
    params.escrowOutputIndex = static_cast<uint32_t>(root("escrowOutputIndex").getInteger());

  // Restore secrets: encrypted blobs only. Decrypted by decryptStoredSecret()
  // once the in-memory encryption key is applied.
  // NOTE: use Common::fromHex into a vector — podFromHex(T) uses sizeof(T), which
  // for std::vector is the object size (~24), not the hex payload length. That
  // bug permanently broke crash-restart recovery of adaptorSecret / ourSwapSecKey.
  auto decodeEncBlob = [](const std::string& hex, std::vector<uint8_t>& out) {
    out.clear();
    if (hex.empty() || (hex.size() % 2) != 0) return;
    if (!Common::fromHex(hex, out)) out.clear();
  };

  if (root.contains("adaptorSecretEnc") && !root("adaptorSecretEnc").getString().empty()) {
    decodeEncBlob(root("adaptorSecretEnc").getString(), params.encBlob);
  } else if (root.contains("adaptorSecret")) {
    // Legacy v2 record: the adaptor secret was stored under "adaptorSecret"
    // (with an inline "encKey"). That scheme is removed in v3 and cannot be
    // migrated — the secret is unrecoverable under the new at-rest format.
    // Fail loudly so the loader skips this record rather than resurrecting a
    // swap it can never complete (which would loop forever / risk escrow
    // lockup). Operators must refund such swaps on-chain manually.
    throw std::runtime_error(
      "legacy v2 swap record (pre-Wildfire) cannot be migrated to v3 — "
      "adaptorSecret unrecoverable; manual on-chain refund required");
  }

  if (root.contains("ourSwapSecKeyEnc") && !root("ourSwapSecKeyEnc").getString().empty()) {
    decodeEncBlob(root("ourSwapSecKeyEnc").getString(), params.encSecKeyBlob);
  }
  if (root.contains("ringOurRingNonceSecEnc") && !root("ringOurRingNonceSecEnc").getString().empty()) {
    decodeEncBlob(root("ringOurRingNonceSecEnc").getString(), params.encRingNonceBlob);
  }

  // Legacy fields
  if (root.contains("aliceXfgPubKey"))
    Common::podFromHex(root("aliceXfgPubKey").getString(), params.aliceXfgPubKey);
  if (root.contains("bobXfgPubKey"))
    Common::podFromHex(root("bobXfgPubKey").getString(), params.bobXfgPubKey);

  params.xfgTimeoutHeight = static_cast<uint32_t>(root("xfgTimeoutHeight").getInteger());
  params.ctrTimeoutBlock = static_cast<uint64_t>(root("ctrTimeoutBlock").getInteger());

  params.ctrLockTxId = root("ctrLockTxId").getString();
  if (root.contains("ctrClaimTxId"))
    params.ctrClaimTxId = root("ctrClaimTxId").getString();
  params.ctrAddress = root("ctrAddress").getString();
  params.peerEndpoint = root("peerEndpoint").getString();
  params.chainState = root("chainState").getString();

  // Collaborative ring state (peer data, written by every record).
  Common::podFromHex(root("ringPeerPartialKeyImage").getString(), params.ringPeerPartialKeyImage);
  Common::podFromHex(root("ringPeerRingNoncePub").getString(),    params.ringPeerRingNoncePub);
  Common::podFromHex(root("ringPeerRingNonceHp").getString(),     params.ringPeerRingNonceHp);
  Common::podFromHex(root("ringPeerPartialResponse").getString(), params.ringPeerPartialResponse);
  params.ringPeerRound1Received = root("ringPeerRound1Received").getInteger() != 0;
  params.ringPeerRound2Received = root("ringPeerRound2Received").getInteger() != 0;
  params.ringOurRound1Sent      = root("ringOurRound1Sent").getInteger() != 0;
  params.ringOurRound2Sent      = root("ringOurRound2Sent").getInteger() != 0;
  params.ringTxBroadcast        = root("ringTxBroadcast").getInteger() != 0;

  if (root.contains("ringOurPartialKeyImage"))
    Common::podFromHex(root("ringOurPartialKeyImage").getString(), params.ringOurPartialKeyImage);
  if (root.contains("ringOurRingNoncePub"))
    Common::podFromHex(root("ringOurRingNoncePub").getString(), params.ringOurRingNoncePub);
  if (root.contains("ringOurRingNonceHp"))
    Common::podFromHex(root("ringOurRingNonceHp").getString(), params.ringOurRingNonceHp);
  if (root.contains("ringOurPartialResponse"))
    Common::podFromHex(root("ringOurPartialResponse").getString(), params.ringOurPartialResponse);
  if (root.contains("ringOurRound1MaterialValid"))
    params.ringOurRound1MaterialValid = root("ringOurRound1MaterialValid").getInteger() != 0;
  if (root.contains("adaptorSecretRevealedToPeer"))
    params.adaptorSecretRevealedToPeer = root("adaptorSecretRevealedToPeer").getInteger() != 0;
  if (root.contains("adaptorSecretReceived"))
    params.adaptorSecretReceived = root("adaptorSecretReceived").getInteger() != 0;

  SwapStateMachine sm(params);
  sm.m_state = static_cast<SwapState>(static_cast<uint8_t>(root("state").getInteger()));
  sm.m_createdAt = static_cast<time_t>(root("createdAt").getInteger());
  sm.m_updatedAt = static_cast<time_t>(root("updatedAt").getInteger());

  // NOTE: encryption key is NEVER persisted. It is injected at runtime by
  // SwapDatabase::setEncryptionKey() before decryptStoredSecret() is called.
  // Any "encKey" field in legacy records is ignored.

  return sm;
}

void SwapStateMachine::setEncryptionKey(const std::string& key) {
  m_encryptionKey = key;
}

bool SwapStateMachine::hasEncryptionKey() const {
  return !m_encryptionKey.empty();
}

void SwapStateMachine::decryptStoredSecret() {
  if (m_encryptionKey.empty()) return;

  auto unpack = [&](const std::vector<uint8_t>& blob, Crypto::SecretKey& out) -> bool {
    if (blob.size() < CHACHA8_NONCE_SIZE + SALT_SIZE + 32 + TAG_SIZE) return false;
    SwapSecretEncryption::EncryptedSecret encrypted;
    std::memcpy(encrypted.nonce.data(), blob.data(), CHACHA8_NONCE_SIZE);
    std::memcpy(encrypted.salt.data(),  blob.data() + CHACHA8_NONCE_SIZE, SALT_SIZE);
    encrypted.ciphertext.assign(blob.begin() + CHACHA8_NONCE_SIZE + SALT_SIZE,
                                blob.begin() + CHACHA8_NONCE_SIZE + SALT_SIZE + 32);
    std::memcpy(encrypted.tag.data(), blob.data() + CHACHA8_NONCE_SIZE + SALT_SIZE + 32, TAG_SIZE);
    return SwapSecretEncryption::decrypt(encrypted, m_encryptionKey, out);
  };

  if (!m_params.encBlob.empty() && unpack(m_params.encBlob, m_params.adaptorSecret)) {
    m_params.encBlob.clear();
  }
  if (!m_params.encSecKeyBlob.empty() && unpack(m_params.encSecKeyBlob, m_params.ourSwapSecKey)) {
    m_params.encSecKeyBlob.clear();
  }
  if (!m_params.encRingNonceBlob.empty()) {
    Crypto::SecretKey ringNonceAsKey;
    if (unpack(m_params.encRingNonceBlob, ringNonceAsKey)) {
      std::memcpy(&m_params.ringOurRingNonceSec, &ringNonceAsKey, 32);
      m_params.encRingNonceBlob.clear();
    }
  }
}

} // namespace XfgSwap
