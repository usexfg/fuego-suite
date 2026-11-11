#pragma once
#include "../Ethereum/EthChainClient.h"

namespace XfgSwap {

// CLV Parachain (a.k.a. "Robinhood Chain") — Substrate parachain with EVM module
// Chain ID: 1024 (EVM), SS58 prefix: 73 (Substrate)
//
// The EVM module allows standard Ethereum-style atomic swaps using
// the same Solidity HTLC contracts as ETH/BSC/Polygon/OPTIMISM.
class CLVChainClient : public EthChainClient {
public:
  CLVChainClient(std::unique_ptr<EthRpcClient> rpc,
                 const std::string& address)
    : EthChainClient(std::move(rpc), address, "CLV") {}
};
}