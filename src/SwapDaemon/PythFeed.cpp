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

#include "PythFeed.h"

#include "Common/JsonValue.h"
#include "HTTP/httplib.h"
#include "crypto/hash.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace XfgSwap {

const char* const kPythGetPriceUnsafeSelector = "96834ad3";

PythWanted pythWanted() {
  PythWanted out;
  const auto& catalog = assetCatalog();
  for (const auto& a : catalog) {
    if (a.pythId.empty()) continue;
    out.emplace_back(a.pythId, &a);
  }
  return out;
}

std::vector<std::string> PythFeed::assets() const {
  return distinctSymbolsForFeed("pyth");
}

// --- hex helpers -----------------------------------------------------------
static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Read the low 64 bits of a 32-byte word, two's complement signed.
static int64_t wordSigned64(const std::string& hex, size_t wordIndex) {
  size_t off = wordIndex * 64;
  if (hex.size() < off + 64) return 0;
  uint64_t v = 0;
  for (size_t i = 0; i < 64; ++i) {
    int d = hexVal(hex[off + i]);
    if (d < 0) return 0;
    v = (v << 4) | static_cast<uint64_t>(d);
  }
  return static_cast<int64_t>(v);
}

static uint64_t wordUnsigned64(const std::string& hex, size_t wordIndex) {
  return static_cast<uint64_t>(wordSigned64(hex, wordIndex));
}

// expo is an int32 sign-extended across the whole 256-bit word.
static int32_t wordSigned32(const std::string& hex, size_t wordIndex) {
  size_t off = wordIndex * 64;
  if (hex.size() < off + 64) return 0;
  bool negative = hex[off] >= '8';
  uint32_t v = 0;
  for (size_t i = 0; i < 64; ++i) {
    int d = hexVal(hex[off + i]);
    if (d < 0) return 0;
    v = (v << 4) | static_cast<uint32_t>(d);
  }
  (void)negative;
  return static_cast<int32_t>(static_cast<uint32_t>(v & 0xffffffffu));
}

static std::string strip0x(const std::string& s) {
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return s.substr(2);
  return s;
}

FeedResult parsePythBatch(const std::string& body,
                          const PythWanted& wanted,
                          const PricePolicy& policy,
                          int64_t now) {
  FeedResult result;
  result.feedName = "pyth";
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

  if (!root.isArray()) {
    result.transportReason = "response is not a JSON-RPC batch array";
    return result;
  }
  result.transportOk = true;

  // Batch ids are the positional index of the request we built, so they map straight
  // back to the wanted list without echoing any user-controlled string.
  const auto& arr = root.getArray();
  for (const auto& entry : arr) {
    if (!entry.isObject() || !entry.contains("id") || !entry("id").isInteger()) continue;
    const int64_t idx = entry("id").getInteger();
    if (idx < 0 || static_cast<size_t>(idx) >= wanted.size()) continue;
    const AssetDescriptor* asset = wanted[static_cast<size_t>(idx)].second;

    AssetPrice ap;

    if (entry.contains("error")) {
      ap.reason = "contract call failed (id never published on this chain?)";
      result.assets[asset->symbol] = ap;
      continue;
    }
    if (!entry.contains("result") || !entry("result").isString()) {
      ap.reason = "no result field";
      result.assets[asset->symbol] = ap;
      continue;
    }

    const std::string hex = strip0x(entry("result").getString());
    if (hex.size() < kPythPriceWords * 64) {
      ap.reason = "short return, expected 4 words";
      result.assets[asset->symbol] = ap;
      continue;
    }

    const int64_t rawPrice = wordSigned64(hex, 0);
    const uint64_t conf = wordUnsigned64(hex, 1);
    const int32_t expo = wordSigned32(hex, 2);
    const int64_t publishTime = static_cast<int64_t>(wordUnsigned64(hex, 3));

    const double price = static_cast<double>(rawPrice) * std::pow(10.0, static_cast<double>(expo));
    if (!std::isfinite(price) || price <= 0.0) {
      ap.reason = "decoded price not finite or not positive";
      result.assets[asset->symbol] = ap;
      continue;
    }

    ap.observation.price = price;
    ap.observation.providerTimestamp = publishTime;
    ap.observation.fetchTimestamp = now;
    // Pyth's conf is an absolute price band, so a SMALLER band means a BETTER price,
    // while the policy's confidence is a [0,1] quality score where HIGHER is better
    // (DeFiLlama supplies 0.99 directly). Converting needs the sign flipped:
    // uncertainty = band/price, quality = 1 - uncertainty.
    const double band = static_cast<double>(conf) * std::pow(10.0, static_cast<double>(expo));
    const double uncertainty = price > 0.0 ? band / price : 1.0;
    double quality = 1.0 - uncertainty;
    if (quality < 0.0) quality = 0.0;
    if (quality > 1.0) quality = 1.0;
    ap.observation.confidenceSupplied = true;
    ap.observation.confidence = quality;

    // A Pyth contract that has stopped being written to still answers getPriceUnsafe
    // with whatever it last stored, so age has to be judged here rather than trusted.
    QuoteStatus st = validateObservation(ap.observation, policy, now, ap.reason);
    if (st != QuoteStatus::OK) {
      ap.ok = false;
      if (ap.reason.empty()) ap.reason = "on-chain update rejected by policy";
      result.assets[asset->symbol] = ap;
      continue;
    }

    ap.ok = true;
    result.assets[asset->symbol] = ap;
  }

  return result;
}

FeedResult PythFeed::fetch(const PricePolicy& policy) {
  FeedResult result;
  result.feedName = "pyth";

  if (!configured()) {
    result.transportReason = "pyth feed not configured: set an RPC url and contract address";
    return result;
  }

  const PythWanted wanted = pythWanted();
  if (wanted.empty()) {
    result.transportReason = "no assets carry a Pyth price id";
    return result;
  }

  // One JSON-RPC batch, one eth_call per id.
  std::string req = "[";
  for (size_t i = 0; i < wanted.size(); ++i) {
    if (i) req += ",";
    req += "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(i) +
           ",\"method\":\"eth_call\",\"params\":[{\"to\":\"" + m_contract +
           "\",\"data\":\"0x" + kPythGetPriceUnsafeSelector + wanted[i].first +
           "\"},\"latest\"]}";
  }
  req += "]";

  std::string host = m_rpcUrl;
  bool https = true;
  if (host.rfind("https://", 0) == 0) host = host.substr(8);
  else if (host.rfind("http://", 0) == 0) { https = false; host = host.substr(7); }
  if (m_chainId != 0) host += "/chainId/" + std::to_string(m_chainId);

  const httplib::Headers headers = {{"Content-Type", "application/json"}};

  std::string body;
  bool got = false;
  std::string failure;
  if (https) {
    httplib::SSLClient cli(host);
    cli.set_connection_timeout(policy.connectTimeoutSec, 0);
    cli.set_read_timeout(policy.readTimeoutSec, 0);
    cli.enable_server_certificate_verification(true);
    cli.set_follow_location(false);
    auto res = cli.Post("/", headers, req, "application/json");
    if (!res) failure = std::string("transport: ") + httplib::to_string(res.error());
    else if (res->status != 200) failure = "HTTP " + std::to_string(res->status);
    else if (res->body.size() > policy.maxResponseBytes) failure = "response exceeds maxResponseBytes";
    else { body = res->body; got = true; }
  } else {
    httplib::Client cli(host);
    cli.set_connection_timeout(policy.connectTimeoutSec, 0);
    cli.set_read_timeout(policy.readTimeoutSec, 0);
    auto res = cli.Post("/", headers, req, "application/json");
    if (!res) failure = std::string("transport: ") + httplib::to_string(res.error());
    else if (res->status != 200) failure = "HTTP " + std::to_string(res->status);
    else if (res->body.size() > policy.maxResponseBytes) failure = "response exceeds maxResponseBytes";
    else { body = res->body; got = true; }
  }

  if (!got) {
    result.transportReason = failure;
    return result;
  }

  return parsePythBatch(body, wanted, policy, static_cast<int64_t>(::time(nullptr)));
}

} // namespace XfgSwap