#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class RobinhoodChainClient : public EthChainClient {
public:
  RobinhoodChainClient(std::unique_ptr<EthRpcClient> rpc,
                   const std::string& address)
    : EthChainClient(std::move(rpc), address, "ROBINHOOD") {}
};
}
