#pragma once

#include "SwapTypes.h"
#include "ChainClientResult.h"
#include <string>
#include <memory>

namespace XfgSwap {

class IChainClient {
public:
  virtual ~IChainClient() = default;
  virtual std::string chainName() const = 0;
  virtual ChainClientResult lock(const SwapParams& params) = 0;
  virtual ChainClientResult verifyLock(const SwapParams& params) = 0;
  virtual ChainClientResult claim(const SwapParams& params) = 0;
  virtual ChainClientResult refund(const SwapParams& params) = 0;

  // Verify a counterparty reserve proof.
  // expectedMessage: the proof's signed message must equal this (binds the proof
  //   to a specific swap/offer — pass the offerId). Empty disables the binding check.
  // minAmount: minimum counterparty-chain balance the proof must demonstrate.
  virtual ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                               uint64_t minAmount,
                                               const std::string& proof) = 0;

  virtual bool getCurrentHeight(uint64_t& height) { (void)height; return false; }
};

} // namespace XfgSwap
