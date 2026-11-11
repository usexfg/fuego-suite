// Copyright (c) 2017-2026 Fuego Developers
//
// Sia daemon (siad / walletd) HTTP API client for SC atomic swaps.
// Auth: User-Agent: Sia-Agent + API password (HTTP basic empty user).

#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace XfgSwap {

struct SiaTxInfo {
  std::string txid;
  uint64_t confirmations = 0;
  bool confirmed = false;
};

class SiaRpcClient {
public:
  SiaRpcClient(const std::string& host, uint16_t port,
               const std::string& apiPassword);

  bool getBlockHeight(uint64_t& height);
  bool getBalance(uint64_t& hastings); // 1 SC = 10^24 hastings
  bool getAddress(std::string& address);

  // Send SC to address. amountHastings: base units.
  // Optional memo/description embeds hashlock or preimage for claim discoverability.
  bool sendSiacoins(const std::string& destAddress,
                    uint64_t amountHastings,
                    const std::string& memo,
                    std::string& txid);

  // List recent wallet transactions (JSON blob for parsing).
  bool getTransactions(std::string& jsonOut);

  // Find transaction by id / confirmations.
  bool getTransaction(const std::string& txid, SiaTxInfo& info);

  // Scan wallet txs for a memo containing "preimage:" + 64 hex after a lock.
  bool findClaimPreimage(const std::string& lockTxidHint,
                         std::string& preimageHex);

private:
  std::string http(const std::string& method, const std::string& path,
                   const std::string& body = "");
  std::string m_host;
  uint16_t m_port;
  std::string m_apiPassword;
};

} // namespace XfgSwap
