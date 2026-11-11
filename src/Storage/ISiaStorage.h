// Copyright (c) 2017-2026 Fuego Developers
//
// Abstract Sia/renterd storage interface — DIGM upload infrastructure.
// Implementations talk to renterd (S3 gateway or worker objects API).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Fuego {
namespace Storage {

struct SiaPutResult {
  bool success = false;
  std::string error;
  std::string key;          // object key in bucket
  std::string contentHash;  // SHA-256 hex of plaintext (CID)
  uint64_t sizeBytes = 0;
};

struct SiaGetResult {
  bool success = false;
  std::string error;
  std::vector<uint8_t> data;
  std::string contentHash;  // recomputed SHA-256 hex
  bool hashMatches = false; // true if expectedCid empty or matches
};

class ISiaStorage {
public:
  virtual ~ISiaStorage() = default;

  // Connectivity / readiness (renterd bus reachable, optional min SC).
  virtual bool ping(std::string& statusOut) = 0;

  // Put bytes under key (or auto-key from content hash). Returns CID.
  virtual SiaPutResult putObject(const std::string& key,
                                 const uint8_t* data, size_t len,
                                 bool encryptClientSide = true) = 0;

  // Get object by key; if expectedCid non-empty, verify SHA-256.
  virtual SiaGetResult getObject(const std::string& key,
                                 const std::string& expectedCid = {}) = 0;

  // Delete object (best-effort).
  virtual bool deleteObject(const std::string& key, std::string& error) = 0;

  // SHA-256 hex of buffer (CID scheme).
  static std::string contentHashHex(const uint8_t* data, size_t len);
};

} // namespace Storage
} // namespace Fuego
