// Copyright (c) 2017-2026 Fuego Developers
//
// Config for renterd storage backend (Phase 7A).

#pragma once

#include <cstdint>
#include <string>

namespace Fuego {
namespace Storage {

struct SiaRenterConfig {
  // renterd bus / API base (e.g. http://127.0.0.1:9980)
  std::string renterdUrl = "http://127.0.0.1:9980";
  // API password (Basic auth, empty user — same convention as siad)
  std::string apiPassword;
  // Logical bucket / namespace prefix for DIGM objects
  std::string bucket = "digm";
  // Optional client-side AES key (32-byte hex). Empty = no client encrypt.
  std::string clientAesKeyHex;
  // Minimum SC balance in hastings before put (0 = skip check)
  uint64_t minBalanceHastings = 0;
  // Request timeouts
  uint32_t connectTimeoutSec = 10;
  uint32_t readTimeoutSec = 120;
};

// Load from JSON file or object string. Returns false on parse/open error.
bool loadSiaRenterConfig(const std::string& pathOrJson, SiaRenterConfig& out,
                         std::string& error);

} // namespace Storage
} // namespace Fuego
