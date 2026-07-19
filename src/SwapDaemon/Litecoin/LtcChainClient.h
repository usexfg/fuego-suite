#pragma once

#include "../IChainClient.h"
#include "../Spv/ISpvClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class LtcChainClient : public IChainClient {
public:
  // SPV mode (no RPC client required)
  explicit LtcChainClient(std::shared_ptr<ISpvClient> spvClient);

  std::string chainName() const override { return "LTC"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  bool getCurrentHeight(uint64_t& height) override;

  // Extract the HTLC claim preimage from a spending transaction.
  // Fetches the raw tx via the SPV client and parses witness data.
  // Returns empty string on failure.
  std::string extractSecret(const std::string& spendingTxid,
                            const std::string& htlcRedeemScriptHex);

private:
  // SPV-mode verifyLock: fetch raw tx, parse outputs, verify amount and inclusion
  ChainClientResult verifyLockSpv(const SwapParams& params);

  // SPV-mode extractSecret: fetch raw spending tx, parse witness stack
  std::string extractSecretSpv(const std::string& spendingTxid,
                               const std::vector<uint8_t>& htlcP2wshScriptPubKey);

  std::shared_ptr<ISpvClient> m_spvClient;
};

} // namespace XfgSwap
