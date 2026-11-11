#pragma once

#include "../Ethereum/EthChainClient.h"
#include <memory>

namespace XfgSwap {

class BscChainClient : public EthChainClient {
public:
  BscChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address)
    : EthChainClient(std::move(rpc), address, "BSC") {}
};

} // namespace XfgSwap
