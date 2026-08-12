// Copyright (c) 2017-2026 Fuego Developers
//
// TON HTTP API (toncenter-compatible) + BOC HTLC messages.

#pragma once

#include <string>
#include <cstdint>
#include <array>

namespace XfgSwap {

struct TonHtlcState {
  uint64_t amountNano = 0;
  std::string hashLockHex;      // 64 hex, SHA-256(preimage)
  uint64_t timeoutUnix = 0;
  std::string recipient;        // raw "wc:hex"
  std::string sender;
  bool claimed = false;
  bool refunded = false;
  std::string preimageHex;      // 64 hex after claim
};

class TonRpcClient {
public:
  TonRpcClient(const std::string& host, uint16_t port,
               const std::string& apiKey = "",
               const std::string& htlcAddress = "",
               int workchain = 0);

  bool getMasterchainSeqno(uint64_t& seqno);
  bool getBalance(const std::string& address, uint64_t& nanoTons);
  bool getHtlcState(const std::string& address, TonHtlcState& out);
  bool sendBoc(const std::string& bocBase64, std::string& messageHashHex);

  // Build + send claim/refund external messages to pre-deployed HTLC.
  bool lockHtlc(const std::string& walletKeyHex,
                const std::string& recipient,
                const std::string& hashLockSha256Hex,
                uint64_t timeoutUnix,
                uint64_t amountNano,
                std::string& lockRef,
                std::string& error);

  bool claimHtlc(const std::string& walletKeyHex,
                 const std::string& htlcAddress,
                 const std::string& preimageHex,
                 std::string& claimRef,
                 std::string& error);

  bool refundHtlc(const std::string& walletKeyHex,
                  const std::string& htlcAddress,
                  std::string& refundRef,
                  std::string& error);

  const std::string& htlcAddress() const { return m_htlcAddress; }

private:
  std::string rpcCall(const std::string& method, const std::string& paramsJson);
  std::string httpPost(const std::string& body);
  bool sendExternalBody(const std::string& destAddr, const std::string& bodyBocB64,
                        std::string& msgHash, std::string& error);
  static bool hexTo32(const std::string& hex, uint8_t out[32]);

  std::string m_host;
  uint16_t m_port;
  std::string m_apiKey;
  std::string m_htlcAddress;
  int m_workchain;
};

} // namespace XfgSwap
