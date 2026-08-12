#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class PulseXChainClient : public EthChainClient {
public:
  PulseXChainClient(std::unique_ptr<EthRpcClient> rpc,
                   const std::string& address)
    : EthChainClient(std::move(rpc), address, "PULSEX") {}
};
}
