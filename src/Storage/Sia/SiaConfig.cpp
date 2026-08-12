// Copyright (c) 2017-2026 Fuego Developers

#include "SiaConfig.h"
#include "Common/JsonValue.h"

#include <fstream>
#include <sstream>

namespace Fuego {
namespace Storage {

namespace {
std::string getStr(const Common::JsonValue& root, const char* key,
                   const std::string& def = {}) {
  if (!root.isObject() || !root.contains(key)) return def;
  if (root(key).isString()) return root(key).getString();
  return def;
}
uint64_t getU64(const Common::JsonValue& root, const char* key, uint64_t def) {
  if (!root.isObject() || !root.contains(key)) return def;
  try {
    if (root(key).isInteger()) return static_cast<uint64_t>(root(key).getInteger());
    if (root(key).isString()) return std::stoull(root(key).getString());
  } catch (...) {}
  return def;
}
} // namespace

bool loadSiaRenterConfig(const std::string& pathOrJson, SiaRenterConfig& out,
                         std::string& error) {
  std::string json = pathOrJson;
  // If looks like a path, read file
  if (!pathOrJson.empty() && pathOrJson[0] != '{' &&
      pathOrJson.find('\n') == std::string::npos) {
    std::ifstream ifs(pathOrJson);
    if (!ifs) {
      // treat as inline JSON only if starts with {
      if (pathOrJson.find('{') == std::string::npos) {
        error = "cannot open sia renter config: " + pathOrJson;
        return false;
      }
    } else {
      std::ostringstream ss;
      ss << ifs.rdbuf();
      json = ss.str();
    }
  }
  try {
    auto root = Common::JsonValue::fromString(json);
    out.renterdUrl = getStr(root, "sia_renterd_url",
                            getStr(root, "renterd_url", out.renterdUrl));
    out.apiPassword = getStr(root, "sia_renterd_password",
                             getStr(root, "sia_api_key", out.apiPassword));
    out.bucket = getStr(root, "sia_bucket", getStr(root, "bucket", out.bucket));
    out.clientAesKeyHex = getStr(root, "sia_client_aes_key_hex", out.clientAesKeyHex);
    out.minBalanceHastings = getU64(root, "sia_min_balance_hastings", out.minBalanceHastings);
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

} // namespace Storage
} // namespace Fuego
