// Copyright (c) 2017-2026, Fuego Developers
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

#include "PriceFeed.h"

#include <string>
#include <utility>
#include <vector>

namespace XfgSwap {

// getPriceUnsafe(bytes32) -- keccak256("getPriceUnsafe(bytes32)")[0:4].
// Verified against the two canonical Keccak-256 vectors; a hand-rolled digest
// produced a different selector, which would have silently mis-called the contract.
extern const char* const kPythGetPriceUnsafeSelector;

// Pythnet Price struct: (int64 price, uint64 conf, int32 expo, uint32 publishTime),
// ABI-encoded as four 32-byte words for the getPriceUnsafe view call.
constexpr size_t kPythPriceWords = 4;

typedef std::vector<std::pair<std::string, const AssetDescriptor*>> PythWanted;

PythWanted pythWanted();

// Decode one JSON-RPC batch response. Pure: no clock, no I/O.
// A feed is only reported ok when its on-chain publishTime is inside the policy window,
// so a contract that has stopped being written to yields NO_DATA rather than a
// months-old price. An error entry (for example "execution reverted", which is what an
// id that was never published on that contract returns) is surfaced per asset.
FeedResult parsePythBatch(const std::string& body,
                          const PythWanted& wanted,
                          const PricePolicy& policy,
                          int64_t now);

class PythFeed : public PriceFeed {
public:
  const char* name() const override { return "pyth"; }
  std::vector<std::string> assets() const override;
  FeedResult fetch(const PricePolicy& policy) override;

  // Both are required. There is no default: reading a price feed needs a specific
  // chain endpoint and a specific deployed contract, and guessing either one would
  // silently return the wrong asset's price.
  void setRpcUrl(const std::string& url) { m_rpcUrl = url; }
  void setContract(const std::string& addr) { m_contract = addr; }
  void setChainId(uint64_t id) { m_chainId = id; }

  bool configured() const { return !m_rpcUrl.empty() && !m_contract.empty(); }

private:
  std::string m_rpcUrl;
  std::string m_contract;
  uint64_t    m_chainId = 0;
};

} // namespace XfgSwap