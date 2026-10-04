#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class PulseChainClient : public EthChainClient {
public:
  PulseChainClient(std::unique_ptr<EthRpcClient> rpc,
                   const std::string& address)
    : EthChainClient(std::move(rpc), address, "PULSECHAIN") {}
};
}
