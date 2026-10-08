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

#include "CoinGeckoFeed.h"

#include "Common/JsonValue.h"
#include "HTTP/httplib.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace XfgSwap {

CoinGeckoWanted coinGeckoWanted() {
  CoinGeckoWanted out;
  const auto& catalog = assetCatalog();
  for (const auto& a : catalog) {
    if (!a.priced || a.defiLlamaId.empty()) continue;
    if (a.defiLlamaId.rfind("coingecko:", 0) != 0) continue;
    out.emplace_back(a.defiLlamaId.substr(std::string("coingecko:").size()), &a);
  }
  std::sort(out.begin(), out.end(),
            [](const std::pair<std::string, const AssetDescriptor*>& l,
               const std::pair<std::string, const AssetDescriptor*>& r) {
              return l.first < r.first;
            });
  return out;
}

std::vector<std::string> CoinGeckoFeed::assets() const {
  return distinctSymbolsForFeed("coingecko");
}

FeedResult parseCoinGeckoBody(const std::string& body,
                              const CoinGeckoWanted& wanted,
                              const PricePolicy& policy,
                              int64_t now) {
  (void)policy;
  FeedResult result;
  result.feedName = "coingecko";
  result.transportOk = false;

  if (body.empty()) {
    result.transportReason = "empty body";
    return result;
  }

  Common::JsonValue root;
  try {
    root = Common::JsonValue::fromString(body);
  } catch (const std::exception& e) {
    result.transportReason = std::string("malformed JSON: ") + e.what();
    return result;
  }
  if (!root.isObject()) {
    result.transportReason = "response is not an object";
    return result;
  }
  result.transportOk = true;

  for (const auto& entry : root.getObject()) {
    const std::string& id = entry.first;
    const auto it = std::find_if(wanted.begin(), wanted.end(),
        [&id](const std::pair<std::string, const AssetDescriptor*>& w) { return w.first == id; });
    // CoinGecko keys the response by the id we asked for and returns no symbol, so a
    // key we did not request cannot be attributed to any of our assets.
    if (it == wanted.end()) continue;

    AssetPrice ap;
    const Common::JsonValue& v = entry.second;
    if (!v.isObject()) {
      ap.reason = "entry is not an object";
      result.assets[it->second->symbol] = ap;
      continue;
    }

    if (!v.contains("usd") || !(v("usd").isReal() || v("usd").isInteger())) {
      ap.reason = "missing or non-numeric usd price";
      result.assets[it->second->symbol] = ap;
      continue;
    }
    const double price = v("usd").isReal() ? v("usd").getReal()
                                            : static_cast<double>(v("usd").getInteger());
    if (!std::isfinite(price) || price <= 0.0) {
      ap.reason = "price not finite or not positive";
      result.assets[it->second->symbol] = ap;
      continue;
    }

    // include_last_updated_at=true gives a unix seconds stamp. Without it there is no
    // provider clock, and the policy requires one.
    if (!v.contains("last_updated_at") || !v("last_updated_at").isInteger()) {
      ap.reason = "missing last_updated_at";
      result.assets[it->second->symbol] = ap;
      continue;
    }

    ap.ok = true;
    ap.observation.price = price;
    ap.observation.providerTimestamp = v("last_updated_at").getInteger();
    ap.observation.fetchTimestamp = now;
    // CoinGecko publishes no confidence band, so nothing is claimed here and the
    // policy's confidence floor is skipped for this feed.
    ap.observation.confidenceSupplied = false;
    result.assets[it->second->symbol] = ap;
  }

  return result;
}

FeedResult CoinGeckoFeed::fetch(const PricePolicy& policy) {
  FeedResult result;
  result.feedName = "coingecko";

  const CoinGeckoWanted wanted = coinGeckoWanted();
  if (wanted.empty()) {
    result.transportReason = "no priced assets with a coingecko id";
    return result;
  }

  std::string ids;
  for (const auto& w : wanted) {
    if (!ids.empty()) ids += ",";
    ids += w.first;
  }

  // One request for every asset. The keyless tier allows 3 requests per minute, so a
  // per-asset loop would guarantee a 429 on the third asset.
  const std::string path = "/api/v3/simple/price?ids=" + ids +
                           "&vs_currencies=usd&include_last_updated_at=true";

  std::string host = m_baseUrl;
  bool https = true;
  if (host.rfind("https://", 0) == 0) host = host.substr(8);
  else if (host.rfind("http://", 0) == 0) { https = false; host = host.substr(7); }

  httplib::Headers headers = {{"Accept", "application/json"}, {"User-Agent", "xfg-swapd/1"}};
  std::string key = m_apiKey;
  if (key.empty()) {
    const char* env = std::getenv("CG_API_KEY");
    if (env != nullptr) key = env;
  }
  if (!key.empty()) headers.emplace("x-cg-pro-api-key", key);

  std::string body;
  bool got = false;
  std::string failure;
  if (https) {
    httplib::SSLClient cli(host);
    cli.set_connection_timeout(policy.connectTimeoutSec, 0);
    cli.set_read_timeout(policy.readTimeoutSec, 0);
    cli.enable_server_certificate_verification(true);
    cli.set_follow_location(false);
    auto res = cli.Get(path.c_str(), headers);
    if (!res) failure = std::string("transport: ") + httplib::to_string(res.error());
    else if (res->status == 429) {
      // Deliberately no retry: the keyless tier is 3/min and retrying inside the
      // window extends the block.
      failure = "HTTP 429 rate limited (keyless tier allows 3 requests/minute)";
    }
    else if (res->status != 200) failure = "HTTP " + std::to_string(res->status);
    else if (res->body.size() > policy.maxResponseBytes) failure = "response exceeds maxResponseBytes";
    else { body = res->body; got = true; }
  } else {
    httplib::Client cli(host);
    cli.set_connection_timeout(policy.connectTimeoutSec, 0);
    cli.set_read_timeout(policy.readTimeoutSec, 0);
    auto res = cli.Get(path.c_str(), headers);
    if (!res) failure = std::string("transport: ") + httplib::to_string(res.error());
    else if (res->status == 429) failure = "HTTP 429 rate limited (keyless tier allows 3 requests/minute)";
    else if (res->status != 200) failure = "HTTP " + std::to_string(res->status);
    else if (res->body.size() > policy.maxResponseBytes) failure = "response exceeds maxResponseBytes";
    else { body = res->body; got = true; }
  }

  if (!got) {
    result.transportReason = failure;
    return result;
  }

  return parseCoinGeckoBody(body, wanted, policy, static_cast<int64_t>(::time(nullptr)));
}

} // namespace XfgSwap