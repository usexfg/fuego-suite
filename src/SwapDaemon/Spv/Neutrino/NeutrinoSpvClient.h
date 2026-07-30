// Copyright (c) 2017-2026 Fuego Developers
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
#include "../ISpvClient.h"
#include "../SpvHeaderStore.h"
#include <string>
#include <memory>
#include <vector>
#include <map>

namespace XfgSwap {

// BIP-158 Golomb-coded set filter parameters
struct GcsFilterParams {
  uint32_t M = 784931;   // SipHash modulator (BIP-158 default)
  uint32_t P = 19;       // Filter pruning threshold (2^P false positive rate)
  uint64_t k0 = 0;       // SipHash key part 0 (0 = derive from block hash)
  uint64_t k1 = 0;       // SipHash key part 1 (0 = derive from block hash)
};

// Abstract connection to a Neutrino (BIP-157/158) peer.
class NeutrinoConnection {
public:
  virtual ~NeutrinoConnection() = default;
  virtual bool sendRequest(const std::string& method, const std::string& params,
                           std::string& response) = 0;
};

// Compute SipHash-2-4 of data with given 128-bit key (k0, k1).
// This is the PRF used by BIP-158 GCS filters.
uint64_t SipHash(const uint8_t* data, size_t len, uint64_t k0, uint64_t k1);

// Derive BIP-158 SipHash key from block hash: SHA256(blockHash)[0:8] = k0, SHA256(blockHash)[8:16] = k1.
// If blockHash is empty, falls back to legacy key (k0 = M, k1 = P) for backward compatibility.
GcsFilterParams deriveFilterKey(const std::vector<uint8_t>& blockHash,
                                const GcsFilterParams& base = GcsFilterParams());

// BIP-157/158 compact block filter SPV client.
//
// Instead of Electrum's per-query script disclosure, downloads GCS filters
// for each block and matches locally to determine which blocks contain
// transactions relevant to our watched scripts.
class NeutrinoSpvClient : public ISpvClient {
public:
  NeutrinoSpvClient(SpvHeaderStore& store,
                    const std::vector<SpvHeaderStore::Checkpoint>& checkpoints,
                    const GcsFilterParams& params = GcsFilterParams());

  // ISpvClient
  std::string protocolName() const override { return "neutrino"; }
  bool syncHeaders() override;
  bool getTipHeight(uint64_t& height) override;
  bool verifyTxInclusion(const std::string& txid, SpvTxInclusion& out) override;
  bool findSpend(const std::string& txid, uint32_t vout, SpvSpend& out) override;
  bool getRawTx(const std::string& txid, std::vector<uint8_t>& rawTx) override;
  bool broadcastTx(const std::vector<uint8_t>& rawTx, std::string& txid) override;

  // Neutrino-specific: add a peer connection
  void addConnection(std::unique_ptr<NeutrinoConnection> conn);

  // Set the scriptPubKey we're watching (for filter matching)
  void setWatchScript(const std::vector<uint8_t>& scriptPubKey);

  // Download and cache a filter for a given block height
  bool getFilter(uint64_t height, std::vector<uint8_t>& filter);

  // Match a script against a filter (BIP-158 GCS match)
  static bool matchFilter(const std::vector<uint8_t>& filter,
                          const std::vector<uint8_t>& data,
                          const GcsFilterParams& params);

  // Build a GCS filter from a set of byte vectors
  static std::vector<uint8_t> buildFilter(
      const std::vector<std::vector<uint8_t>>& items,
      const GcsFilterParams& params);

  const SpvHeaderStore& store() const { return m_store; }

private:
  bool downloadFilterHeaders(uint64_t startHeight, uint64_t count);
  bool downloadFilter(uint64_t height, std::vector<uint8_t>& filter);
  bool downloadBlockTxs(uint64_t height, std::vector<std::string>& txids);

  SpvHeaderStore& m_store;
  GcsFilterParams m_params;
  std::vector<std::unique_ptr<NeutrinoConnection>> m_connections;
  std::vector<uint8_t> m_watchScript;

  // Filter cache: height -> filter bytes
  std::map<uint64_t, std::vector<uint8_t>> m_filterCache;
};

} // namespace XfgSwap
