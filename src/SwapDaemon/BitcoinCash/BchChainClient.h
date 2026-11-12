#pragma once

#include "../IChainClient.h"
#include "../Spv/ISpvClient.h"
#include "BchRpcClient.h"
#include "../Utxo/UtxoHtlcFullNode.h"
#include "../Utxo/UtxoHtlcSpv.h"
#include <string>
#include <memory>

namespace XfgSwap {

class BchChainClient : public IChainClient {
public:
  BchChainClient(std::unique_ptr<BchRpcClient> rpc, const std::string& wif,
                 UtxoNetwork network = UtxoNetwork::MAINNET);
  // SPV mode (no RPC client): Bob's verification and claim; no lock funding.
  BchChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif,
                 UtxoNetwork network = UtxoNetwork::MAINNET);

  std::string chainName() const override { return "BCH"; }
  bool usesSpvVerification() const override { return m_spvHtlc != nullptr; }
  bool isReadyForNewSwap() override;
  std::string readinessError() override;
  std::string readinessErrorFor(SwapRole role) override;
  std::string claimDeadlineError(const SwapParams& params) override;
  std::string getClaimPubKey() const override;
  bool hasConfirmedClaim(const SwapParams& params) override;

  std::string getReceiveAddress() const override;
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
  std::string tryExtractClaimedSecret(const SwapParams& params) override;
  bool supportsPurePtlc() const override;
  ChainClientResult bumpClaim(const SwapParams& params) override;

private:
  std::unique_ptr<BchRpcClient> m_rpc;            // null in SPV mode
  std::unique_ptr<UtxoHtlcFullNode> m_fullNode;   // HTLC swap path over m_rpc
  std::unique_ptr<UtxoHtlcSpv> m_spvHtlc;         // HTLC swap path over SPV
};

}  // namespace XfgSwap
