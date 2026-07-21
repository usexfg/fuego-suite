#pragma once

#include "../IChainClient.h"
#include "../Spv/ISpvClient.h"
#include "DcrRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

// Decred (DCR) chain client — hybrid PoW/PoS UTXO chain.
//
// DCR uses Bitcoin-like scripts with P2SH HTLCs.
// Transaction format includes version, locktime, expiry.
// Address prefixes: P2PKH 0x073F, P2SH 0x071A (mainnet).
//
// Supports two modes:
//   - Full-node RPC (default): uses DcrRpcClient for all operations
//   - SPV (Neutrino/BIP-157/158): uses NeutrinoSpvClient for verification,
//     RPC still required for lock/claim/refund
class DcrChainClient : public IChainClient {
public:
  // Full-node mode (existing)
  DcrChainClient(std::unique_ptr<DcrRpcClient> rpc,
                 const std::string& wif = "");

  // SPV mode: Neutrino client for verification, RPC for signing operations
  DcrChainClient(std::shared_ptr<ISpvClient> spvClient,
                 std::unique_ptr<DcrRpcClient> rpc,
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
  // SPV-mode verifyLock: fetch raw tx, parse outputs, verify P2SH + inclusion
  ChainClientResult verifyLockSpv(const SwapParams& params);

  // SPV-mode extractSecret: fetch raw spending tx, parse scriptSig
  std::string extractSecretSpv(const std::string& spendingTxid,
                               const std::vector<uint8_t>& htlcP2shScriptPubKey);

  std::unique_ptr<DcrRpcClient> m_rpc;       // null in pure-SPV mode
  std::string m_wif;
  std::shared_ptr<ISpvClient> m_spvClient;   // null in full-node mode
};

} // namespace XfgSwap
