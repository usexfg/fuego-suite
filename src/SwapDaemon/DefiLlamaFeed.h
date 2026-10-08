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

// Maps a DeFiLlama id to the settlement asset it is expected to price. Built
// from the feed metadata table; pairs that share a settlement asset appear once.
typedef std::vector<std::pair<std::string, const AssetDescriptor*>> DefiLlamaWanted;

DefiLlamaWanted defiLlamaWanted();

// Parse a `/prices/current` body. Pure: no clock, no I/O, so it is unit-testable
// against canned responses. Entries whose reported symbol differs from the asset's
// expectedFeedSymbol are dropped rather than priced, which is what stops a resolving
// id that serves a different asset from being quoted.
FeedResult parseDefiLlamaBody(const std::string& body,
                              const DefiLlamaWanted& wanted,
                              const PricePolicy& policy,
                              int64_t now);

class DefiLlamaFeed : public PriceFeed {
public:
  const char* name() const override { return "defillama"; }
  std::vector<std::string> assets() const override;
  FeedResult fetch(const PricePolicy& policy) override;

  // Overridable so tests can point the transport at a stub server.
  void setBaseUrl(const std::string& url) { m_baseUrl = url; }

private:
  std::string m_baseUrl = "https://coins.llama.fi";
};

} // namespace XfgSwap