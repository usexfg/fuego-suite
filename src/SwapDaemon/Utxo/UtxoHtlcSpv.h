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

#include "UtxoHtlcSwap.h"
#include "UtxoPointLock.h"
#include "SwapDaemon/Spv/ISpvClient.h"

#include <array>
#include <memory>
#include <string>

namespace XfgSwap {

// The SPV half of a hash-time-locked swap on a Bitcoin-family chain
// (remediation plan 2.5). Bob verifies Alice's lock and claims it from
// quorum-verified headers and Merkle proofs; nothing a single server says is
// trusted. Funding a lock needs coin selection over Electrum and is not done
// here, so Alice cannot start a swap over SPV. Refund and claim discovery stay
// available to recover swaps that already exist.
class UtxoHtlcSpv {
public:
  UtxoHtlcSpv(UtxoHtlcChain chain, std::shared_ptr<ISpvClient> spv, const std::string& wif);

  std::string claimPubKeyHex() const;
  bool supportsPointLocks() const { return m_hasKey && !m_chain.network.bech32Hrp.empty() &&
                                           m_chain.encoding == UtxoHtlcEncoding::P2WSH; }
  std::string receiveAddress() const;
  std::string readinessErrorFor(SwapRole role);
  bool currentHeight(uint64_t& height);

  ChainClientResult lock(const SwapParams& params);
  ChainClientResult verifyLock(const SwapParams& params);
  ChainClientResult claim(const SwapParams& params);
  ChainClientResult refund(const SwapParams& params);
  std::string claimDeadlineError(const SwapParams& params);
  ChainClientResult transactionDetails(const std::string& txid);
  std::string tryExtractClaimedSecret(const SwapParams& params);
  bool hasConfirmedClaim(const SwapParams& params);
  // Raises an unconfirmed point-lock claim's fee the way the chain's nodes
  // allow (UtxoFeeBump): the next pre-signed rung, or a child paying for it.
  ChainClientResult bumpClaim(const SwapParams& params);

private:
  bool syncedTip(uint64_t& tip, std::string& error);
  bool lockOutput(const SwapParams& params, const std::vector<uint8_t>& expectedScriptPubKey, uint64_t& value,
                  std::string& error);
  ChainClientResult verifyPointLock(const SwapParams& params);
  ChainClientResult claimPoint(const SwapParams& params);
  std::string pointClaimDeadline(const SwapParams& params);
  bool hasConfirmedPointClaim(const SwapParams& params);
  ChainClientResult broadcastPointClaim(const SwapParams& params, const PointLockState& state,
                                        const PointLockClaimOffer& offer, uint64_t lockValue);
  ChainClientResult payForClaimByChild(const std::string& claimTxid, const std::vector<uint8_t>& rawClaim,
                                       uint64_t lockValue);
  bool spendFee(uint64_t virtualSize, uint64_t lockValue, uint64_t& fee, std::string& error);
  ChainClientResult broadcastSpend(bool claimPath, const SwapParams& params);
  bool claimPreimage(const SwapParams& params, const SpvSpend& spend, std::vector<uint8_t>& preimage) const;
  std::string failurePrefix() const { return m_chain.ticker + " SPV: "; }

  const UtxoHtlcChain m_chain;
  const std::shared_ptr<ISpvClient> m_spv;
  std::array<uint8_t, 32> m_key{};
  std::vector<uint8_t> m_pubKey;
  bool m_hasKey = false;
};

}  // namespace XfgSwap
