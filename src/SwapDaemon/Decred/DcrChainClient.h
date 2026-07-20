#pragma once

#include "../IChainClient.h"
#include "DcrRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

// Decred (DCR) chain client — hybrid PoW/PoS UTXO chain.
//
// DCR uses Bitcoin-like scripts with P2SH HTLCs.
// Transaction format includes version, locktime, expiry.
// Address prefixes: P2PKH 0x073F, P2SH 0x071A (mainnet).
class DcrChainClient : public IChainClient {
public:
  DcrChainClient(std::unique_ptr<DcrRpcClient> rpc,
                 const std::string& wif = "");

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

  std::string extractSecret(const std::string& spendingTxid,
                            const std::string& htlcRedeemScriptHex);

private:
  std::unique_ptr<DcrRpcClient> m_rpc;
  std::string m_wif;  // WIF private key for signing
};

} // namespace XfgSwap
