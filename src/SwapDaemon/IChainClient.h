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
};

} // namespace XfgSwap
