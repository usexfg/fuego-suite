// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once
#include "ISpvClient.h"
#include "SpvHeaderStore.h"
#include "ElectrumConnection.h"
#include <string>
#include <vector>
#include <memory>

namespace XfgSwap {

class ElectrumSpvClient : public ISpvClient {
public:
  // servers: list of "host:port" strings
  // minServers: minimum servers that must agree for cross-check
  // checkpointHeight: height of the checkpoint anchor
  // checkpointHashDisplay: checkpoint hash in display (big-endian) hex
  ElectrumSpvClient(
      const std::vector<std::string>& servers,
      size_t minServers,
      uint64_t checkpointHeight,
      const std::string& checkpointHashDisplay);

  ~ElectrumSpvClient();

  std::string protocolName() const override;
  bool syncHeaders() override;
  bool getTipHeight(uint64_t& height) override;

  // Stubbed for now (Tasks 7-8)
  bool verifyTxInclusion(const std::string& txid, SpvTxInclusion& out) override;
  bool findSpend(const std::string& txid, uint32_t vout, SpvSpend& out) override;
  bool getRawTx(const std::string& txid, std::vector<uint8_t>& rawTx) override;
  bool broadcastTx(const std::vector<uint8_t>& rawTx, std::string& txid) override;

  // Get the header store (for testing/inspection)
  const SpvHeaderStore& store() const { return m_store; }

private:
  bool connectToServers();

  // Eclipse mitigation: verify a header's merkle root against a majority
  // of connected servers. Returns true if a strict majority agree.
  bool crossCheckHeader(uint64_t blockHeight, const std::string& merkleRootDisplay);

  // Eclipse mitigation: after one server claims a tx inclusion, verify
  // independently with other servers. Returns the verified inclusion
  // or a rejected (empty) inclusion if not enough servers agree.
  SpvTxInclusion crossCheckTxVerify(const std::string& txid, const SpvTxInclusion& serverInclusion);

  std::vector<std::string> m_serverAddrs;
  size_t m_minServers;
  SpvHeaderStore m_store;
  std::vector<std::unique_ptr<ElectrumConnection>> m_conns;
};

} // namespace XfgSwap
