#pragma once

#include "IChainClient.h"
#include "SwapTypes.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace XfgSwap {

class ChainRegistry {
public:
  void registerChain(SwapPair pair, std::unique_ptr<IChainClient> client);
  IChainClient* getClient(SwapPair pair) const;
  bool hasChain(SwapPair pair) const;
  std::vector<SwapPair> registeredPairs() const;

private:
  std::map<SwapPair, std::unique_ptr<IChainClient>> m_clients;
};

} // namespace XfgSwap
