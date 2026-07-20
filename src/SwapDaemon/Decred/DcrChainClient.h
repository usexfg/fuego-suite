#pragma once

#include "../IChainClient.h"
#include "DcrRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

// Decred (DCR) chain client — hybrid PoW/PoS UTXO chain.
//
// DCR uses a different transaction format than Bitcoin:
// - Tx includes stake transactions (SSGen, SStx)
// - P2SH HTLCs work similarly to Bitcoin but with different opcodes
// - 3-byte P2PKH prefix (0x3a for mainnet) instead of 4-byte
//
// For atomic swaps, we use standard P2SH HTLC contracts.
class DcrChainClient : public IChainClient {
public:
  DcrChainClient(std::unique_ptr<DcrRpcClient> rpc);

  std::string chainName() const override { return "DCR"; }
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

  // Extract HTLC claim preimage from a spending transaction.
  std::string extractSecret(const std::string& spendingTxid,
                            const std::string& htlcRedeemScriptHex);

private:
  std::unique_ptr<DcrRpcClient> m_rpc;
};

} // namespace XfgSwap
