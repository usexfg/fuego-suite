#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class PlasmaChainClient : public EthChainClient {
public:
  PlasmaChainClient(std::unique_ptr<EthRpcClient> rpc,
                   const std::string& address)
    : EthChainClient(std::move(rpc), address, "PLASMA") {}
};
}
