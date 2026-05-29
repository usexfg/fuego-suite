#include <gtest/gtest.h>
#include "SwapDaemon/ChainRegistry.h"
#include "TestChainClient.h"

using namespace XfgSwap;

TEST(ChainRegistryTest, RegisterAndRetrieve) {
  ChainRegistry registry;
  auto testClient = std::make_unique<TestChainClient>("TEST");
  auto* rawPtr = testClient.get();
  registry.registerChain(SwapPair::BCH, std::move(testClient));

  EXPECT_TRUE(registry.hasChain(SwapPair::BCH));
  EXPECT_EQ(registry.getClient(SwapPair::BCH), rawPtr);
  EXPECT_EQ(registry.getClient(SwapPair::BCH)->chainName(), "TEST");
}

TEST(ChainRegistryTest, UnregisteredPairReturnsNull) {
  ChainRegistry registry;
  EXPECT_FALSE(registry.hasChain(SwapPair::ETH));
  EXPECT_EQ(registry.getClient(SwapPair::ETH), nullptr);
}

TEST(ChainRegistryTest, ListRegisteredPairs) {
  ChainRegistry registry;
  registry.registerChain(SwapPair::BCH, std::make_unique<TestChainClient>("BCH"));
  registry.registerChain(SwapPair::ETH, std::make_unique<TestChainClient>("ETH"));

  auto pairs = registry.registeredPairs();
  EXPECT_EQ(pairs.size(), 2u);
}

TEST(ChainRegistryTest, DispatchLockViaRegistry) {
  ChainRegistry registry;
  auto testClient = std::make_unique<TestChainClient>("BCH");
  testClient->lockResult = ChainClientResult::ok("abc123");
  auto* rawPtr = testClient.get();
  registry.registerChain(SwapPair::BCH, std::move(testClient));

  SwapParams params;
  params.pair = SwapPair::BCH;
  auto result = registry.getClient(SwapPair::BCH)->lock(params);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.txId, "abc123");
  EXPECT_EQ(rawPtr->lockCalls, 1);
}

TEST(ChainRegistryTest, RegisterArbAndDispatch) {
  ChainRegistry registry;
  auto ethClient = std::make_unique<TestChainClient>("ETH");
  auto arbClient = std::make_unique<TestChainClient>("ARB");
  auto* ethRaw = ethClient.get();
  auto* arbRaw = arbClient.get();

  registry.registerChain(SwapPair::ETH, std::move(ethClient));
  registry.registerChain(SwapPair::ARB, std::move(arbClient));

  EXPECT_TRUE(registry.hasChain(SwapPair::ETH));
  EXPECT_TRUE(registry.hasChain(SwapPair::ARB));
  EXPECT_EQ(registry.getClient(SwapPair::ETH)->chainName(), "ETH");
  EXPECT_EQ(registry.getClient(SwapPair::ARB)->chainName(), "ARB");

  // Dispatch lock to each
  SwapParams ethParams;
  ethParams.pair = SwapPair::ETH;
  registry.getClient(SwapPair::ETH)->lock(ethParams);
  EXPECT_EQ(ethRaw->lockCalls, 1);

  SwapParams arbParams;
  arbParams.pair = SwapPair::ARB;
  registry.getClient(SwapPair::ARB)->lock(arbParams);
  EXPECT_EQ(arbRaw->lockCalls, 1);
}
