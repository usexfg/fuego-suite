#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class OptimismChainClient : public EthChainClient {
public:
  OptimismChainClient(std::unique_ptr<EthRpcClient> rpc,
                      const std::string& address)
    : EthChainClient(std::move(rpc), address, "OPTIMISM") {}
};
}