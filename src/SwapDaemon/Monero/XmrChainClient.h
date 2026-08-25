#pragma once

#include "../IChainClient.h"
#include "MoneroRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class XmrChainClient : public IChainClient {
public:
  XmrChainClient(std::unique_ptr<MoneroRpcClient> rpc,
                 const std::string& spendKeyHex,
                 const std::string& viewKeyHex);

  std::string chainName() const override { return "XMR"; }
  bool supportsPtlc() const override { return true; } // native adaptor-only, point-lock
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  bool getCurrentHeight(uint64_t& height) override;

  // Produce a reserve proof for the local wallet (taker side).
  bool getReserveProof(const std::string& address, const std::string& message,
                       std::string& signature) {
    return m_rpc->getReserveProof(address, message, signature);
  }

  // Compute the shared address from the two parties' per-swap XMR pubkeys.
  bool computeSharedAddress(const SwapParams& params, std::string& out);

  // The operator wallet's own primary XMR address (sweep destination).
  std::string ownAddress() const;

private:
  // combined spend = own + peer share (verified against the published pubs)
  // and combined view = own + peer view secrets.
  bool combinedKeys(const SwapParams& params,
                    std::vector<uint8_t>& combinedSpend,
                    std::vector<uint8_t>& combinedView,
                    std::string& error) const;

  std::unique_ptr<MoneroRpcClient> m_rpc;
  std::string m_spendKeyHex;
  std::string m_viewKeyHex;
};

} // namespace XfgSwap
