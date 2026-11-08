#pragma once

#include "../IChainClient.h"
#include "../Spv/ISpvClient.h"
#include "LtcRpcClient.h"
#include "../Utxo/UtxoHtlcFullNode.h"
#include "../Utxo/UtxoHtlcSpv.h"
#include <string>
#include <memory>

namespace XfgSwap {

class LtcChainClient : public IChainClient {
public:
  // Full-node mode
  LtcChainClient(std::unique_ptr<LtcRpcClient> rpc, const std::string& wif,
                 UtxoNetwork network = UtxoNetwork::MAINNET);

  // SPV mode (no RPC client): Bob's verification and claim; no lock funding.
  LtcChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif,
                 UtxoNetwork network = UtxoNetwork::MAINNET);

  std::string chainName() const override { return "LTC"; }
  bool usesSpvVerification() const override { return m_spvClient != nullptr; }
  bool isReadyForNewSwap() override;
  std::string readinessError() override;
  std::string readinessErrorFor(SwapRole role) override;
  std::string claimDeadlineError(const SwapParams& params) override;
  std::string getClaimPubKey() const override;
  bool hasConfirmedClaim(const SwapParams& params) override;
  ChainClientResult bumpClaim(const SwapParams& params) override;
  bool supportsPtlc() const override { return true; }
  // Taproot point locks (UtxoPointLock) over the full node or, for Bob, SPV.
  bool supportsPurePtlc() const override;

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

private:


  std::unique_ptr<LtcRpcClient> m_rpc;    // null in SPV mode
  std::unique_ptr<UtxoHtlcFullNode> m_fullNode;  // HTLC swap path over m_rpc
  std::string m_wif;                       // empty in SPV mode
  std::shared_ptr<ISpvClient> m_spvClient; // null in full-node mode
  std::unique_ptr<UtxoHtlcSpv> m_spvHtlc;  // HTLC swap path over m_spvClient
};

} // namespace XfgSwap
