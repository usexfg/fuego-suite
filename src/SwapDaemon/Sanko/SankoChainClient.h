#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class SankoChainClient : public EthChainClient {
public:
  SankoChainClient(std::unique_ptr<EthRpcClient> rpc,
                   const std::string& address)
    : EthChainClient(std::move(rpc), address, "SANKO") {}
};
}