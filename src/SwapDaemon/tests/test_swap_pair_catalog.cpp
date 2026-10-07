// Copyright (c) 2017-2026 Fuego Developers

#include "SwapDaemon/SwapPairCatalog.h"
#include "SwapDaemon/SwapTypes.h"
#include "SwapDaemon/SwapTimelock.h"
#include "SwapDaemon/PriceOracle.h"
#include "SwapDaemon/Ethereum/EthRpcClient.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <string>

using namespace XfgSwap;

static void testLegacyIdsAreStable() {
  assert(static_cast<uint8_t>(SwapPair::SOL) == 0);
  assert(static_cast<uint8_t>(SwapPair::ETH) == 1);
  assert(static_cast<uint8_t>(SwapPair::POLYGON) == 11);
  assert(static_cast<uint8_t>(SwapPair::PULSECHAIN) == 23);
  assert(static_cast<uint8_t>(SwapPair::DOT) == 28);
}

static void testCatalogIsContiguousAndUnique() {
  assert(SWAP_PAIR_COUNT == 46);
  assert(MAX_SWAP_PAIR_INDEX == 45);
  std::set<std::string> keys;
  std::set<std::string> symbols;
  std::set<uint64_t> evmChainIds;
  size_t evmCount = 0;
  size_t adapterCount = 0;
  size_t stagedCount = 0;
  for (size_t i = 0; i < SWAP_PAIR_COUNT; ++i) {
    const auto& descriptor = SWAP_PAIR_CATALOG[i];
    assert(descriptor.id == i);
    assert(static_cast<uint8_t>(descriptor.pair) == i);
    assert(descriptor.minBlockTimeMs > 0);
    assert(descriptor.maxBlockTimeMs >= descriptor.minBlockTimeMs);
    assert(keys.insert(descriptor.key).second);
    assert(symbols.insert(descriptor.symbol).second);
    if (descriptor.family == SwapChainFamily::EVM) {
      ++evmCount;
      assert(descriptor.chainId != 0);
      assert(descriptor.decimals == 18);
      assert(evmChainIds.insert(descriptor.chainId).second);
    }
    if (descriptor.support == SwapPairSupport::ADAPTER) ++adapterCount;
    if (descriptor.support == SwapPairSupport::STAGED) ++stagedCount;
  }
  assert(evmCount == 32);
  assert(adapterCount == 17);
  assert(stagedCount == 4);
}

static void testActivationAndNames() {
  assert(!isProtocolSwapPair(static_cast<uint8_t>(SwapPair::SIA)));
  assert(!isProtocolSwapPair(static_cast<uint8_t>(SwapPair::ZANO)));
  assert(!isProtocolSwapPair(static_cast<uint8_t>(SwapPair::TON)));
  assert(!isProtocolSwapPair(static_cast<uint8_t>(SwapPair::DOT)));
  assert(isProtocolSwapPair(static_cast<uint8_t>(SwapPair::BEAM)));
  assert(static_cast<uint8_t>(SwapPair::BEAM) == 42);
  assert(swapPairDescriptor(SwapPair::BEAM)->family == SwapChainFamily::EVM);
  assert(swapPairDescriptor(SwapPair::ZANO)->family == SwapChainFamily::CRYPTONOTE);
  assert(swapPairDescriptor(SwapPair::BTC)->family == SwapChainFamily::UTXO);

  SwapPair pair{};
  assert(swapPairFromString("peaq", pair) && pair == SwapPair::PEAQ);
  assert(swapPairFromString("beam", pair) && pair == SwapPair::BEAM);
  assert(swapPairFromString("BSC", pair) && pair == SwapPair::BNB);
  assert(swapPairFromString("SC", pair) && pair == SwapPair::SIA);
  assert(!swapPairFromString("TEMPO", pair));
  assert(std::strcmp(swapPairToString(SwapPair::DOMA), "DOMA") == 0);
}

static void testDerivedMetadata() {
  assert(minMsPerBlock(SwapPair::SEI) == 200);
  assert(maxMsPerBlock(SwapPair::SEI) == 1000);
  assert(minMsPerBlock(SwapPair::RSK) == 15000);
  assert(maxMsPerBlock(SwapPair::RSK) == 60000);
  assert(msPerBlock(SwapPair::PEAQ) == maxMsPerBlock(SwapPair::PEAQ));
  assert(claimRunwayBlocks(SwapPair::SEI) == 18000);
  uint64_t minimumWindow = 0;
  assert(minimumCounterpartyWindowBlocks(SwapPair::SEI, 6, minimumWindow));
  assert(minimumWindow == 18006);
  assert(timelockOrderingOk(SwapPair::SEI, 100, 280, 1000, 44200));
  assert(!timelockOrderingOk(SwapPair::SEI, 100, 280, 0,
                             std::numeric_limits<uint64_t>::max()));
  assert(std::fabs(PriceOracle::ctrDivisor(SwapPair::BEAM) - 1e18) < 1.0);
  const auto* monad = swapPairDescriptor(SwapPair::MONAD);
  assert(monad && monad->chainId == 143);
}

static void testRpcEndpointParsing() {
  EthRpcClient avalanche("https://api.avax.network/ext/bc/C/rpc", 0);
  assert(avalanche.usesTls());
  assert(avalanche.endpointUrl() == "https://api.avax.network:443");
  assert(avalanche.rpcPath() == "/ext/bc/C/rpc");

  EthRpcClient local("127.0.0.1", 8545);
  assert(!local.usesTls());
  assert(local.endpointUrl() == "http://127.0.0.1:8545");
  assert(local.rpcPath() == "/");

  EthRpcClient tlsByPort("rpc.example", 443);
  assert(tlsByPort.usesTls());

  for (const std::string& invalid : {
      "https://rpc.example:0", "https://rpc.example:65536",
      "https://rpc.example:not-a-port"}) {
    bool rejected = false;
    try {
      EthRpcClient client(invalid, 0);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    assert(rejected);
  }
}

int main() {
  testLegacyIdsAreStable();
  testCatalogIsContiguousAndUnique();
  testActivationAndNames();
  testDerivedMetadata();
  testRpcEndpointParsing();
  std::cout << "All swap pair catalog tests passed.\n";
  return 0;
}
