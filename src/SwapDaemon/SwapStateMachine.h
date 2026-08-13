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
#include <string>
#include <ctime>

namespace XfgSwap {

class SwapStateMachine {
public:
  SwapStateMachine();
  explicit SwapStateMachine(SwapParams params);

  // State transitions
  bool transition(SwapState newState, uint32_t currentHeight = 0);
  SwapState currentState() const;

  // Access params
  SwapParams& params();
  const SwapParams& params() const;

  // Timestamps
  time_t createdAt() const;
  time_t updatedAt() const;

  // Serialization (JSON string)
  std::string serialize() const;
  static SwapStateMachine deserialize(const std::string& json);

  // Encryption key for adaptorSecret at rest (set before serialize for encryption)
  void setEncryptionKey(const std::string& key);
  bool hasEncryptionKey() const;

  // Decrypt adaptorSecret and ourSwapSecKey after loading and setting
  // encryption key. Must be called after setEncryptionKey on a deserialized
  // record that has adaptorSecretEnc / ourSwapSecKeyEnc data.
  // No-op if no encrypted data is present.
  void decryptStoredSecret();

  // Check if swap is in a terminal state
  bool isTerminal() const;

  // ── Optimistic concurrency ──
  // Every persisted record carries a monotonically increasing version.
  // SwapDatabase::saveSwapLocked rejects a save whose in-memory version does
  // not match the on-disk record — a concurrent writer (P2P thread) won the
  // race. Callers reload and re-apply. Prevents lost-update clobbering
  // between the tick thread and the peer-message thread.
  uint64_t recordVersion() const { return m_recordVersion; }
  void bumpRecordVersion() { ++m_recordVersion; }

private:
  // Validate that a transition from current state to newState is legal
  bool isValidTransition(SwapState newState) const;

  SwapParams m_params;
  SwapState m_state;
  time_t m_createdAt;
  time_t m_updatedAt;
  uint64_t m_recordVersion = 0;
  std::string m_encryptionKey;
};

} // namespace XfgSwap
