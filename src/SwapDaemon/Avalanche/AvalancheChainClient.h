#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class AvalancheChainClient : public EthChainClient {
public:
  AvalancheChainClient(std::unique_ptr<EthRpcClient> rpc,
                       const std::string& address)
    : EthChainClient(std::move(rpc), address, "AVAX") {}
};
}