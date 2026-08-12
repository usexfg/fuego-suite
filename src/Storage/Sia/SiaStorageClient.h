// Copyright (c) 2017-2026 Fuego Developers
//
// renterd-backed storage client (Phase 7A + put/get infrastructure for DIGM).
// Uses renterd worker object API:
//   PUT/GET/DELETE /api/worker/objects/{bucket}/{key}
// with Basic auth (":" + password).

#pragma once

#include "../ISiaStorage.h"
#include "SiaConfig.h"
#include <memory>
#include <string>

namespace Fuego {
namespace Storage {

class SiaStorageClient : public ISiaStorage {
public:
  explicit SiaStorageClient(SiaRenterConfig cfg);

  bool ping(std::string& statusOut) override;
  SiaPutResult putObject(const std::string& key,
                         const uint8_t* data, size_t len,
                         bool encryptClientSide = true) override;
  SiaGetResult getObject(const std::string& key,
                         const std::string& expectedCid = {}) override;
  bool deleteObject(const std::string& key, std::string& error) override;

  // Optional: bus wallet balance in hastings (0 if unavailable).
  bool getWalletBalanceHastings(uint64_t& hastings);

  const SiaRenterConfig& config() const { return m_cfg; }

private:
  std::string objectPath(const std::string& key) const;
  std::string http(const std::string& method, const std::string& path,
                   const std::string& body, const std::string& contentType,
                   int& statusOut);

  // Client-side AES-256-CTR encrypt/decrypt when clientAesKeyHex is 64 hex chars.
  bool encrypt(const uint8_t* in, size_t len, std::vector<uint8_t>& out, std::string& err);
  bool decrypt(const uint8_t* in, size_t len, std::vector<uint8_t>& out, std::string& err);

  SiaRenterConfig m_cfg;
};

} // namespace Storage
} // namespace Fuego
