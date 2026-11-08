#pragma once

#include "../IChainClient.h"
#include "EthRpcClient.h"
#include <string>
#include <memory>
#include <mutex>

namespace XfgSwap {

class EthChainClient : public IChainClient {
public:
  EthChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address,
                 const std::string& chainName = "ETH");

  std::string chainName() const override { return m_chainName; }
  std::string readinessError() override;
  std::string readinessErrorFor(SwapRole) override { return readinessError(); }
  std::string claimDeadlineError(const SwapParams& params) override;
  bool isReadyForNewSwap() override { return readinessError().empty(); }
  uint64_t configuredChainId() const override {
    return m_rpc ? m_rpc->expectedChainId() : 0;
  }
  bool supportsPtlc() const override { return false; } // Phase 1: BRIDGE (HTLC on EVM, PTLC on XFG). Native Schnorr via EIP in Phase 4.
  // Point locks need the pinned PointTimelock at the configured registry.
  bool supportsPurePtlc() const override { return pointLockSupport() == PointLockSupport::AVAILABLE; }
  PointLockSupport pointLockSupport() const override;


protected:
  bool hasDeployedHtlcRegistry();

public:

  // Canonical endian transforms (ContractAbi endian rule): Solidity uint256 /
  // libsecp256k1 read scalars BIG-endian; CryptoNote stores LITTLE-endian;
  // cross-curve reuse is sound because t < l_ed25519 < n_secp.
  // secretBeHex: CryptoNote LE scalar -> canonical BE hex (PointTimelock claim).
  static std::string secretBeHex(const Crypto::SecretKey& t);
  // secretLeHexFromBe: on-chain revealed BE scalar hex -> CryptoNote LE hex
  // (XFG adaptor consumption). Empty input / wrong length => empty.
  static std::string secretLeHexFromBe(const std::string& beHex64);

  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult lockPtlc(const SwapParams& params) override { return lock(params); }
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult getTransactionDetails(const std::string& txId,
                                          ChainClientResult& result) override;

  std::string getReceiveAddress() const override { return m_address; }
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  ChainClientResult verifyReserveProofWide(const std::string& expectedMessage,
                                           const AtomicAmount& minAmount,
                                           const std::string& proof) override;
  bool getCurrentHeight(uint64_t& height) override;
  std::string tryExtractClaimedSecret(const SwapParams& params) override;
  bool hasConfirmedClaim(const SwapParams& params) override;

private:
  bool lockOpenUntil(const SwapParams& params, uint64_t minTimeoutBlock, std::string& error);
  bool timeoutFloor(uint64_t blocks, uint64_t& floor, std::string& error);

  std::unique_ptr<EthRpcClient> m_rpc;
  std::string m_address;
  std::string m_chainName;
  mutable std::mutex m_pointLockMutex;
  mutable bool m_pointLockConfirmed = false;
};

} // namespace XfgSwap
