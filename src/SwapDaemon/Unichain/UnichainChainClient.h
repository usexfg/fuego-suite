#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class UnichainChainClient : public EthChainClient {
public:
  UnichainChainClient(std::unique_ptr<EthRpcClient> rpc,
                   const std::string& address)
    : EthChainClient(std::move(rpc), address, "UNICHAIN") {}
};
}
