#pragma once

#include "../Ethereum/EthChainClient.h"
#include <memory>

namespace XfgSwap {

class PolygonChainClient : public EthChainClient {
public:
  PolygonChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address)
    : EthChainClient(std::move(rpc), address, "POLYGON") {}
};

} // namespace XfgSwap
