#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class GleecChainClient : public EthChainClient {
public:
  GleecChainClient(std::unique_ptr<EthRpcClient> rpc,
                   const std::string& address)
    : EthChainClient(std::move(rpc), address, "GLEEC") {}
};
}