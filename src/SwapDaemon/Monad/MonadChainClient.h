#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class MonadChainClient : public EthChainClient {
public:
  MonadChainClient(std::unique_ptr<EthRpcClient> rpc,
                   const std::string& address)
    : EthChainClient(std::move(rpc), address, "MONAD") {}
};
}