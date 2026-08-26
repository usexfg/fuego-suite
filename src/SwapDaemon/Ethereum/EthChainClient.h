#pragma once

#include "../IChainClient.h"
#include "EthRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class EthChainClient : public IChainClient {
public:
  EthChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address,
                 const std::string& chainName = "ETH");

  std::string chainName() const override { return m_chainName; }
  bool supportsPtlc() const override { return false; } // Phase 1: BRIDGE (HTLC on EVM, PTLC on XFG). Native Schnorr via EIP in Phase 4.
  // Pure PTLC (PointTimelock registry model) is available only when a
  // PointTimelock registry address has been wired via setPtlcRegistry().
  // Empty registry => BRIDGE (HTLC on EVM) — unchanged default behavior.
  bool supportsPurePtlc() const override;

  // Wire the pre-deployed PointTimelock registry ("0x..."). Forwards to the
  // underlying EthRpcClient so lock/verify/claim/refund point operations
  // target it. Empty address keeps pure PTLC disabled.
  void setPtlcRegistry(const std::string& registryAddress);

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

  std::string getReceiveAddress() const override { return m_address; }
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  bool getCurrentHeight(uint64_t& height) override;
  std::string tryExtractClaimedSecret(const SwapParams& params) override;

private:
  std::unique_ptr<EthRpcClient> m_rpc;
  std::string m_address;
  std::string m_chainName;
  // PointTimelock registry address ("0x..."). Empty => pure PTLC disabled.
  std::string m_ptlcRegistry;
};

} // namespace XfgSwap
