#pragma once

#include "../IChainClient.h"
#include "../Spv/ISpvClient.h"
#include "BtcRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class BtcChainClient : public IChainClient {
public:
  // Full-node mode
  BtcChainClient(std::unique_ptr<BtcRpcClient> rpc, const std::string& wif);

  // SPV mode (no RPC client)
  BtcChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif);

  std::string chainName() const override { return "BTC"; }
  bool supportsPtlc() const override { return true; }
  std::string getReceiveAddress() const override;
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult lockPtlc(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult verifyPtlcLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  ChainClientResult getTransactionDetails(const std::string& txId,
                                          ChainClientResult& result) override;
  bool getCurrentHeight(uint64_t& height) override;

  std::string tryExtractClaimedSecret(const SwapParams& params) override;

  // Extract the HTLC claim preimage from a spending transaction.
  // In SPV mode, fetches the raw tx via the SPV client.
  // In full-node mode, uses the RPC client.
  // Returns empty string on failure.
  std::string extractSecret(const std::string& spendingTxid,
                            const std::string& htlcRedeemScriptHex);

private:
  // SPV-mode verifyLock: fetch raw tx, parse outputs for P2WSH, verify amount and inclusion
  ChainClientResult verifyLockSpv(const SwapParams& params);

  // SPV-mode extractSecret: fetch raw spending tx, parse witness data
  std::string extractSecretSpv(const std::string& spendingTxid,
                               const std::vector<uint8_t>& htlcP2wshScriptPubKey);

  std::unique_ptr<BtcRpcClient> m_rpc;    // null in SPV mode
  std::string m_wif;                       // empty in SPV mode
  std::shared_ptr<ISpvClient> m_spvClient; // null in full-node mode
};

} // namespace XfgSwap
