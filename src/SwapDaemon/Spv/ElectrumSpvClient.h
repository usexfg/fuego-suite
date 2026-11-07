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
#include <memory>
#include <set>
#include <string>
#include <vector>

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

  // As above, validating every header against `rules` from the checkpoint on.
  ElectrumSpvClient(
      const std::vector<std::string>& servers,
      size_t minServers,
      uint64_t checkpointHeight,
      const std::string& checkpointHashDisplay,
      const SpvChainRules& rules);

  // Three servers on distinct hosts, a strict-majority quorum of at least
  // two, verified transports, a checkpoint, the chain's header rules, and a
  // completed header sync.
  std::string newSwapTrustError() override;
  bool feeRatesPerKilobyte(uint64_t& relayFloor, uint64_t& nextBlocks) override;

  static constexpr size_t MIN_TRUSTED_SERVERS = 3;

  ~ElectrumSpvClient();

  std::string protocolName() const override;
  bool syncHeaders() override;
  bool getTipHeight(uint64_t& height) override;

  bool verifyTxInclusion(const std::string& txid, SpvTxInclusion& out) override;
  bool findSpend(const std::string& txid, uint32_t vout, SpvSpend& out) override;
  bool getRawTx(const std::string& txid, std::vector<uint8_t>& rawTx) override;
  bool broadcastTx(const std::vector<uint8_t>& rawTx, std::string& txid) override;

  // Compute the canonical txid from locally serialized bytes (strip witness
  // for SegWit). Never trust an Electrum server's returned txid as identity.
  static std::string computeTransactionId(const std::vector<uint8_t>& rawTx);

  // Get the header store (for testing/inspection)
  const SpvHeaderStore& store() const { return m_store; }

private:
  enum class ProofCheck { UNAVAILABLE, MISMATCH, PROVEN };

  // Reuses live connections; reconnects every server when one stops answering.
  bool ensureConnected();
  bool connectToServers();
  bool startFromCheckpoint();
  std::string quorumPolicyError() const;
  // Strict majority of the connected servers.
  size_t quorumSize() const;
  bool quorumTipHeight(uint64_t& height);

  bool syncFrom(ElectrumConnection& source, uint64_t target);
  bool resumeHeight(ElectrumConnection& source, uint64_t target, uint64_t& next, bool& switchedBranch);
  bool appendHeaders(ElectrumConnection& source, uint64_t from, uint64_t target);
  // True when a quorum of servers serves the header with this hash at this height.
  bool crossCheckHeader(uint64_t blockHeight, const std::string& hashDisplay);

  ProofCheck provenHeight(ElectrumConnection& server, const std::string& txid, uint64_t& height);
  std::set<std::string> scriptHistory(const std::vector<uint8_t>& scriptPubKey);

  std::vector<std::string> m_serverAddrs;
  size_t m_minServers;
  uint64_t m_checkpointHeight = 0;
  SpvHeaderStore m_store;
  std::vector<std::unique_ptr<ElectrumConnection>> m_conns;
};

} // namespace XfgSwap
