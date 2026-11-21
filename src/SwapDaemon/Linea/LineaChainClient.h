#pragma once

#include "../Ethereum/EthChainClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class LineaChainClient : public EthChainClient {
public:
  LineaChainClient(std::unique_ptr<EthRpcClient> rpc,
                   const std::string& address)
    : EthChainClient(std::move(rpc), address, "LINEA") {}
};
}
