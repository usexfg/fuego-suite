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

// CoinGecko id -> settlement asset. The ids are the same `coingecko:<id>` strings
// DeFiLlama uses, minus the prefix, so one table serves both.
typedef std::vector<std::pair<std::string, const AssetDescriptor*>> CoinGeckoWanted;

CoinGeckoWanted coinGeckoWanted();

// Parse a /simple/price body. Pure: no clock, no I/O.
//
// Two things differ from the DeFiLlama parser and both matter:
//  * CoinGecko returns no confidence, so confidenceSupplied stays false and the
//    policy's confidence floor is skipped for this feed.
//  * CoinGecko returns no per-asset symbol, so identity is checked by key: a
//    response key that was not requested is refused rather than priced.
FeedResult parseCoinGeckoBody(const std::string& body,
                              const CoinGeckoWanted& wanted,
                              const PricePolicy& policy,
                              int64_t now);

class CoinGeckoFeed : public PriceFeed {
public:
  const char* name() const override { return "coingecko"; }
  std::vector<std::string> assets() const override;
  FeedResult fetch(const PricePolicy& policy) override;

  void setBaseUrl(const std::string& url) { m_baseUrl = url; }
  // Optional. A keyless account is capped at 3 requests per minute, measured on
  // 2026-10-04 (200,200,200,429,429,...). One batched poll a minute fits, but
  // there is no headroom for a second caller on the same host.
  void setApiKey(const std::string& key) { m_apiKey = key; }

private:
  std::string m_baseUrl = "https://api.coingecko.com";
  std::string m_apiKey;
};

} // namespace XfgSwap