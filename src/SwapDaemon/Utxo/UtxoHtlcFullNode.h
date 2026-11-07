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

#include "UtxoFullNode.h"
#include "UtxoHtlcSwap.h"

#include <array>
#include <string>

namespace XfgSwap {

// The full-node half of a hash-time-locked swap on a Bitcoin-family chain.
//   Alice (holds the coin): lock, refund, and discovery of Bob's claim from
//   chain data alone.
//   Bob (holds XFG): verification of Alice's exact lock outpoint and claim.
// Every value is checked against the node; nothing from the peer is trusted
// beyond the outpoint and script it names.
class UtxoHtlcFullNode {
public:
  UtxoHtlcFullNode(UtxoHtlcChain chain, UtxoRpcCall rpc, const std::string& wif);

  bool hasSigningKey() const { return m_hasKey; }
  std::string claimPubKeyHex() const;
  std::string receiveAddress() const;
  std::string readinessError();
  bool currentHeight(uint64_t& height);

  ChainClientResult lock(const SwapParams& params);
  ChainClientResult verifyLock(const SwapParams& params);
  ChainClientResult claim(const SwapParams& params);
  ChainClientResult refund(const SwapParams& params);
  ChainClientResult transactionDetails(const std::string& txid);
  std::string tryExtractClaimedSecret(const SwapParams& params);
  bool hasConfirmedClaim(const SwapParams& params);

private:
  bool findPriorLockSend(const std::string& address, uint64_t amount, std::string& txid);
  bool lockedValue(const UtxoOutpoint& lock, uint64_t& value, std::string& error);
  ChainClientResult broadcastSpend(bool claimPath, const SwapParams& params);
  bool findClaimSpend(const SwapParams& params, UtxoSpend& spend);
  bool claimHintSpend(const SwapParams& params, UtxoSpend& spend);
  std::string failurePrefix() const { return m_chain.ticker + " full node: "; }

  const UtxoHtlcChain m_chain;
  const UtxoRpcCall m_rpc;
  std::array<uint8_t, 32> m_key{};
  std::vector<uint8_t> m_pubKey;
  bool m_hasKey = false;
  UtxoSpendTracker m_spends;
};

}  // namespace XfgSwap
