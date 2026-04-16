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
}

bool SwapStateMachine::isValidTransition(SwapState newState) const {
  // Any state can transition to FAILED
  if (newState == SwapState::FAILED) {
    return true;
  }

  switch (m_state) {
    // ── ADAPTOR SWAP STATES (active — v1) ────────────────────────────────────
    case SwapState::INITIATED:
      return newState == SwapState::ADAPTOR_KEYS_EXCHANGED;

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
      return newState == SwapState::ADAPTOR_XFG_SPENT;

    // ── zkLPSWAP POOL STATES (v11 — deferred) ────────────────────────────────
    // These transitions are not reachable from any active v1 swap path.
    // Pool implementation lives in src/SwapDaemon/pool_v11/ — see README there.
    case SwapState::POOL_DEPOSIT_INITIATED:
      return newState == SwapState::POOL_DEPOSIT_LOCKED_A;

    case SwapState::POOL_DEPOSIT_LOCKED_A:
      return newState == SwapState::POOL_DEPOSIT_LOCKED_B;

    case SwapState::POOL_DEPOSIT_LOCKED_B:
      return newState == SwapState::POOL_DEPOSIT_CONFIRMED;

    case SwapState::POOL_DEPOSIT_CONFIRMED:
      return newState == SwapState::POOL_DEPOSIT_COMPLETE ||
             newState == SwapState::POOL_DEPOSIT_REFUNDED;

    case SwapState::POOL_DEPOSIT_COMPLETE:
      return newState == SwapState::POOL_CHECKPOINT_GENERATED;

    case SwapState::POOL_DEPOSIT_REFUNDED:
      return newState == SwapState::FAILED;

    case SwapState::POOL_WITHDRAW_INITIATED:
      return newState == SwapState::POOL_WITHDRAW_LOCKED;

    case SwapState::POOL_WITHDRAW_LOCKED:
      return newState == SwapState::POOL_WITHDRAW_COMPLETE ||
             newState == SwapState::POOL_WITHDRAW_REFUNDED;

    case SwapState::POOL_WITHDRAW_COMPLETE:
      return newState == SwapState::POOL_CHECKPOINT_GENERATED;

    case SwapState::POOL_WITHDRAW_REFUNDED:
      return newState == SwapState::FAILED;

    case SwapState::POOL_SWAP_INITIATED:
      return newState == SwapState::POOL_SWAP_EXECUTED;

    case SwapState::POOL_SWAP_EXECUTED:
      return newState == SwapState::POOL_SWAP_COMPLETE ||
             newState == SwapState::POOL_SWAP_REFUNDED;

    case SwapState::POOL_SWAP_COMPLETE:
      return newState == SwapState::POOL_CHECKPOINT_GENERATED;

    case SwapState::POOL_SWAP_REFUNDED:
      return newState == SwapState::FAILED;

    case SwapState::POOL_FEE_CLAIM_INITIATED:
      return newState == SwapState::POOL_FEE_CLAIMED;

    case SwapState::POOL_FEE_CLAIMED:
      return newState == SwapState::POOL_CHECKPOINT_GENERATED;

    case SwapState::POOL_FEE_CLAIM_REFUNDED:
      return newState == SwapState::FAILED;

    case SwapState::POOL_CHECKPOINT_GENERATED:
      // Can transition to any new pool operation or back to initiated for new cycle
      return newState == SwapState::POOL_DEPOSIT_INITIATED ||
             newState == SwapState::POOL_WITHDRAW_INITIATED ||
             newState == SwapState::POOL_SWAP_INITIATED ||
             newState == SwapState::POOL_FEE_CLAIM_INITIATED;

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

bool SwapStateMachine::transition(SwapState newState) {
  if (!isValidTransition(newState)) {
    return false;
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
         // Pool refund/failure states are terminal (funds returned or lost)
         m_state == SwapState::POOL_DEPOSIT_REFUNDED ||
         m_state == SwapState::POOL_WITHDRAW_REFUNDED ||
         m_state == SwapState::POOL_SWAP_REFUNDED ||
         m_state == SwapState::POOL_FEE_CLAIM_REFUNDED ||
         // Pool checkpoint is the final success state after all pool ops complete
         m_state == SwapState::POOL_CHECKPOINT_GENERATED ||
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

  // Serialization version — bump when adding new fields so deserialize()
  // can skip missing fields on older records gracefully.
  root.insert("serVersion", static_cast<int64_t>(2));

  root.insert("swapId", m_params.swapId);
  root.insert("pair", static_cast<int64_t>(static_cast<uint8_t>(m_params.pair)));
  root.insert("role", static_cast<int64_t>(static_cast<uint8_t>(m_params.role)));
  root.insert("xfgAmount", static_cast<int64_t>(m_params.xfgAmount));
  root.insert("ctrAmount", static_cast<int64_t>(m_params.ctrAmount));
  root.insert("state", static_cast<int64_t>(static_cast<uint8_t>(m_state)));

  // Adaptor sig fields
  root.insert("ourSwapPubKey", Common::podToHex(m_params.ourSwapPubKey));
  root.insert("peerSwapPubKey", Common::podToHex(m_params.peerSwapPubKey));
  root.insert("escrowPubKey", Common::podToHex(m_params.escrowPubKey));
  root.insert("adaptorPoint", Common::podToHex(m_params.adaptorPoint));
  root.insert("adaptorDleqQ", Common::podToHex(m_params.adaptorDleqQ));
  root.insert("escrowTxHash", Common::podToHex(m_params.escrowTxHash));
  root.insert("escrowOutputIndex", static_cast<int64_t>(m_params.escrowOutputIndex));

  // Persist adaptorSecret so a daemon restart mid-swap can recover the
  // 32-byte scalar needed to adapt the pre-signature on the XFG chain.
  // TODO: encrypt at rest — currently plaintext. Key at minimum should be
  //       ChaCha20-Poly1305 keyed from the wallet spend key (available in
  //       wallet context but not in scope of SwapStateMachine today).
  root.insert("adaptorSecret", Common::podToHex(m_params.adaptorSecret));

  // Legacy fields (kept for backward compat in DB)
  root.insert("aliceXfgPubKey", Common::podToHex(m_params.aliceXfgPubKey));
  root.insert("bobXfgPubKey", Common::podToHex(m_params.bobXfgPubKey));

  root.insert("xfgTimeoutHeight", static_cast<int64_t>(m_params.xfgTimeoutHeight));
  root.insert("ctrTimeoutBlock", static_cast<int64_t>(m_params.ctrTimeoutBlock));

  root.insert("ctrLockTxId", m_params.ctrLockTxId);
  root.insert("ctrAddress", m_params.ctrAddress);
  root.insert("peerEndpoint", m_params.peerEndpoint);
  root.insert("bchRedeemScriptHex", m_params.bchRedeemScriptHex);

  root.insert("createdAt", static_cast<int64_t>(m_createdAt));
  root.insert("updatedAt", static_cast<int64_t>(m_updatedAt));

  return root.toString();
}

SwapStateMachine SwapStateMachine::deserialize(const std::string& json) {
  Common::JsonValue root = Common::JsonValue::fromString(json);

  // serVersion added in v2; v1 records simply omit it and fall back gracefully.
  // int64_t serVersion = root.contains("serVersion") ? root("serVersion").getInteger() : 1;

  SwapParams params;
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

  // Restore adaptorSecret persisted since serVersion 2.
  // Absent in v1 records — zero-initialised by SwapStateMachine() default ctor
  // and will be populated from the counterparty chain claim when the swap resumes.
  if (root.contains("adaptorSecret"))
    Common::podFromHex(root("adaptorSecret").getString(), params.adaptorSecret);

  // Legacy fields
  if (root.contains("aliceXfgPubKey"))
    Common::podFromHex(root("aliceXfgPubKey").getString(), params.aliceXfgPubKey);
  if (root.contains("bobXfgPubKey"))
    Common::podFromHex(root("bobXfgPubKey").getString(), params.bobXfgPubKey);

  params.xfgTimeoutHeight = static_cast<uint32_t>(root("xfgTimeoutHeight").getInteger());
  params.ctrTimeoutBlock = static_cast<uint64_t>(root("ctrTimeoutBlock").getInteger());

  params.ctrLockTxId = root("ctrLockTxId").getString();
  params.ctrAddress = root("ctrAddress").getString();
  params.peerEndpoint = root("peerEndpoint").getString();
  // bchRedeemScriptHex was added in serialization v2; gracefully default to ""
  // for older records that pre-date the field.
  try { params.bchRedeemScriptHex = root("bchRedeemScriptHex").getString(); }
  catch (...) { params.bchRedeemScriptHex = ""; }

  SwapStateMachine sm(params);
  sm.m_state = static_cast<SwapState>(static_cast<uint8_t>(root("state").getInteger()));
  sm.m_createdAt = static_cast<time_t>(root("createdAt").getInteger());
  sm.m_updatedAt = static_cast<time_t>(root("updatedAt").getInteger());

  return sm;
}

} // namespace XfgSwap
