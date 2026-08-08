#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {
class CronosChainClient : public EthChainClient {
public:
  CronosChainClient(std::unique_ptr<EthRpcClient> rpc,
                    const std::string& address)
    : EthChainClient(std::move(rpc), address, "CRONOS") {}
};
}