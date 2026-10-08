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

#include "DefiLlamaFeed.h"

#include "Common/JsonValue.h"
#include "HTTP/httplib.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <ctime>

namespace XfgSwap {

static std::string upperAscii(const std::string& in) {
  std::string out = in;
  for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

DefiLlamaWanted defiLlamaWanted() {
  DefiLlamaWanted out;
  const auto& catalog = assetCatalog();
  for (const auto& a : catalog) {
    if (!a.priced || a.defiLlamaId.empty()) continue;
    out.emplace_back(a.defiLlamaId, &a);
  }
  std::sort(out.begin(), out.end(),
            [](const std::pair<std::string, const AssetDescriptor*>& l,
               const std::pair<std::string, const AssetDescriptor*>& r) {
              return l.first < r.first;
            });
  return out;
}

std::vector<std::string> DefiLlamaFeed::assets() const {
  return distinctSymbolsForFeed("defillama");
}

FeedResult parseDefiLlamaBody(const std::string& body,
                              const DefiLlamaWanted& wanted,
                              const PricePolicy& policy,
                              int64_t now) {
  (void)policy;
  FeedResult result;
  result.feedName = "defillama";
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

  if (!root.isObject() || !root.contains("coins")) {
    result.transportReason = "response has no coins object";
    return result;
  }
  const auto& coins = root("coins");
  if (!coins.isObject()) {
    result.transportReason = "coins is not an object";
    return result;
  }

  result.transportOk = true;

  for (const auto& entry : coins.getObject()) {
    const std::string& id = entry.first;
    const auto& asset = std::find_if(wanted.begin(), wanted.end(),
        [&id](const std::pair<std::string, const AssetDescriptor*>& w) { return w.first == id; });
    if (asset == wanted.end()) continue;

    AssetPrice ap;
    const Common::JsonValue& v = entry.second;
    if (!v.isObject()) {
      ap.reason = "entry is not an object";
      result.assets[asset->second->symbol] = ap;
      continue;
    }

    // Fail closed on identity: if we know which symbol this id must report and the
    // feed does not report it, the entry is not proven to be the asset we asked for.
    const std::string wantSym = upperAscii(asset->second->expectedFeedSymbol);
    if (!wantSym.empty()) {
      if (!v.contains("symbol") || !v("symbol").isString()) {
        ap.reason = "entry carries no symbol, cannot verify it is " + wantSym;
        result.assets[asset->second->symbol] = ap;
        continue;
      }
      const std::string gotSym = upperAscii(v("symbol").getString());
      if (gotSym != wantSym) {
        ap.reason = "symbol mismatch: feed reports " + gotSym + ", expected " + wantSym;
        result.assets[asset->second->symbol] = ap;
        continue;
      }
    }

    if (!v.contains("price") || !(v("price").isReal() || v("price").isInteger())) {
      ap.reason = "missing or non-numeric price";
      result.assets[asset->second->symbol] = ap;
      continue;
    }
    const double price = v("price").isReal() ? v("price").getReal()
                                              : static_cast<double>(v("price").getInteger());
    if (!std::isfinite(price) || price <= 0.0) {
      ap.reason = "price not finite or not positive";
      result.assets[asset->second->symbol] = ap;
      continue;
    }

    if (!v.contains("timestamp") || !v("timestamp").isInteger()) {
      ap.reason = "missing provider timestamp";
      result.assets[asset->second->symbol] = ap;
      continue;
    }

    ap.ok = true;
    ap.observation.price = price;
    ap.observation.providerTimestamp = v("timestamp").getInteger();
    ap.observation.fetchTimestamp = now;
    if (v.contains("confidence") && v("confidence").isReal()) {
      ap.observation.confidenceSupplied = true;
      ap.observation.confidence = v("confidence").getReal();
    }
    result.assets[asset->second->symbol] = ap;
  }

  return result;
}

FeedResult DefiLlamaFeed::fetch(const PricePolicy& policy) {
  FeedResult result;
  result.feedName = "defillama";

  const DefiLlamaWanted wanted = defiLlamaWanted();
  if (wanted.empty()) {
    result.transportReason = "no priced assets in the catalog";
    return result;
  }

  std::string ids;
  for (const auto& w : wanted) {
    if (!ids.empty()) ids += ",";
    ids += w.first;
  }

  std::string host = m_baseUrl;
  bool https = true;
  if (host.rfind("https://", 0) == 0) host = host.substr(8);
  else if (host.rfind("http://", 0) == 0) { https = false; host = host.substr(7); }

  const std::string path = "/prices/current/" + ids;
  httplib::Headers headers = {{"User-Agent", "xfg-swapd/1"}, {"Accept", "application/json"}};

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
    else if (res->status != 200) failure = "HTTP " + std::to_string(res->status);
    else if (res->body.size() > policy.maxResponseBytes) failure = "response exceeds maxResponseBytes";
    else { body = res->body; got = true; }
  } else {
    httplib::Client cli(host);
    cli.set_connection_timeout(policy.connectTimeoutSec, 0);
    cli.set_read_timeout(policy.readTimeoutSec, 0);
    auto res = cli.Get(path.c_str(), headers);
    if (!res) failure = std::string("transport: ") + httplib::to_string(res.error());
    else if (res->status != 200) failure = "HTTP " + std::to_string(res->status);
    else if (res->body.size() > policy.maxResponseBytes) failure = "response exceeds maxResponseBytes";
    else { body = res->body; got = true; }
  }

  if (!got) {
    result.transportReason = failure;
    return result;
  }

  return parseDefiLlamaBody(body, wanted, policy, static_cast<int64_t>(::time(nullptr)));
}

} // namespace XfgSwap