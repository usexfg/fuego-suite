#pragma once

#include "../IChainClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

// Forward declaration for the Substrate RPC client (full implementation in .cpp).
class SubstrateRpcClient;

// Polkadot Relay Chain client — Substrate-based (no EVM module on relay chain).
// Requires full SCALE encoding for RPC calls and XCM for cross-chain messaging.
//
// Chain parameters:
//   - SS58 prefix: 0 (Polkadot)
//   - Chain ID: N/A (Polkadot uses genesis hash + session keys)
//   - Block time: ~6 seconds
//   - Consensus: Nominated Proof of Stake (NPoS)
//
// For atomic swaps, Polkadot uses `pallet-hashTimeLock` (if enabled in runtime)
// or XCM-based cross-chain HTLC transfers.
class PolkadotChainClient : public IChainClient {
public:
  explicit PolkadotChainClient(std::unique_ptr<SubstrateRpcClient> rpc,
                               const std::string& address);

  std::string chainName() const override { return "DOT"; }

  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  bool getCurrentHeight(uint64_t& height) override;
  std::string tryExtractClaimedSecret(const SwapParams& params) override;

private:
  std::unique_ptr<SubstrateRpcClient> m_rpc;
  std::string m_address;  // SS58 format address
};

} // namespace XfgSwap
