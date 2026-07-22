#pragma once

#include "../IChainClient.h"
#include "../Spv/ISpvClient.h"
#include "BchRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class BchChainClient : public IChainClient {
public:
  // Full-node mode (existing)
  BchChainClient(std::unique_ptr<BchRpcClient> rpc, const std::string& wif);

  // SPV mode (no RPC client required)
  BchChainClient(std::shared_ptr<ISpvClient> spvClient);

  std::string chainName() const override { return "BCH"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  ChainClientResult getTransactionDetails(const std::string& txId,
                                          ChainClientResult& result) override;
  bool getCurrentHeight(uint64_t& height) override;

  // Extract the HTLC claim preimage from a spending transaction.
  // In SPV mode, fetches the raw tx via the SPV client.
  // In full-node mode, uses the RPC client.
  // Returns empty string on failure.
  std::string extractSecret(const std::string& spendingTxid,
                            const std::string& htlcRedeemScriptHex);

  std::string tryExtractClaimedSecret(const SwapParams& params) override;

private:
  // SPV-mode verifyLock: fetch raw tx, parse outputs, verify amount and inclusion
  ChainClientResult verifyLockSpv(const SwapParams& params);

  // SPV-mode extractSecret: fetch raw spending tx, parse scriptSig
  std::string extractSecretSpv(const std::string& spendingTxid,
                               const std::vector<uint8_t>& htlcP2shScriptPubKey);

  std::unique_ptr<BchRpcClient> m_rpc;   // null in SPV mode
  std::string m_wif;                      // empty in SPV mode
  std::shared_ptr<ISpvClient> m_spvClient;  // null in full-node mode
};

} // namespace XfgSwap
