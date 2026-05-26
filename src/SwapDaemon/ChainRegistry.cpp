#include "ChainRegistry.h"

namespace XfgSwap {

void ChainRegistry::registerChain(SwapPair pair, std::unique_ptr<IChainClient> client) {
  m_clients[pair] = std::move(client);
}

IChainClient* ChainRegistry::getClient(SwapPair pair) const {
  auto it = m_clients.find(pair);
  return (it != m_clients.end()) ? it->second.get() : nullptr;
}

bool ChainRegistry::hasChain(SwapPair pair) const {
  return m_clients.count(pair) > 0;
}

std::vector<SwapPair> ChainRegistry::registeredPairs() const {
  std::vector<SwapPair> pairs;
  pairs.reserve(m_clients.size());
  for (const auto& kv : m_clients) {
    pairs.push_back(kv.first);
  }
  return pairs;
}

} // namespace XfgSwap
