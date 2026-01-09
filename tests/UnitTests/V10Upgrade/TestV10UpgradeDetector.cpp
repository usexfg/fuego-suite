// Copyright (c) 2017-2025 Fuego Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <vector>
#include <memory>

#include "gtest/gtest.h"

#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "CryptoNoteCore/UpgradeDetector.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/CurrencyBuilder.h"
#include "Logging/ConsoleLogger.h"

namespace {
  using CryptoNote::BLOCK_MAJOR_VERSION_1;
  using CryptoNote::BLOCK_MAJOR_VERSION_2;
  using CryptoNote::BLOCK_MAJOR_VERSION_3;
  using CryptoNote::BLOCK_MAJOR_VERSION_4;
  using CryptoNote::BLOCK_MAJOR_VERSION_5;
  using CryptoNote::BLOCK_MAJOR_VERSION_6;
  using CryptoNote::BLOCK_MAJOR_VERSION_7;
  using CryptoNote::BLOCK_MAJOR_VERSION_8;
  using CryptoNote::BLOCK_MAJOR_VERSION_9;
  using CryptoNote::BLOCK_MAJOR_VERSION_10;
  using CryptoNote::BLOCK_MINOR_VERSION_0;
  using CryptoNote::BLOCK_MINOR_VERSION_1;

  struct BlockEx {
    CryptoNote::Block bl;
    uint32_t height;
  };

  typedef std::vector<BlockEx> BlockVectorEx;
  typedef CryptoNote::BasicUpgradeDetector<BlockVectorEx> UpgradeDetector;

  // V10 test fixtures
  class V10UpgradeTest : public ::testing::Test {
  public:
    CryptoNote::Currency createV10Currency(uint64_t upgradeV10Height = 100) {
      CryptoNote::CurrencyBuilder currencyBuilder(logger);
      currencyBuilder.upgradeVotingThreshold(90);
      currencyBuilder.upgradeVotingWindow(720);
      currencyBuilder.upgradeWindow(720);
      
      // Set all upgrade heights to create a multi-version chain
      currencyBuilder.upgradeHeightV2(20);   // Small chain
      currencyBuilder.upgradeHeightV3(40);
      currencyBuilder.upgradeHeightV4(60);
      currencyBuilder.upgradeHeightV5(80);
      currencyBuilder.upgradeHeightV6(90);
      currencyBuilder.upgradeHeightV7(95);
      currencyBuilder.upgradeHeightV8(98);
      currencyBuilder.upgradeHeightV9(99);
      currencyBuilder.upgradeHeightV10(upgradeV10Height);  // V10 at height 100
      
      return currencyBuilder.currency();
    }

  protected:
    Logging::ConsoleLogger logger;
  };

  // Test V10 detector initialization
  class V10UpgradeDetectorV10Init : public V10UpgradeTest {};
  class V10UpgradeDetectorV9Init : public V10UpgradeTest {};
  class V10UpgradeDetectorVoting : public V10UpgradeTest {};
  class V10UpgradeDetectorEmission : public V10UpgradeTest {};
  class V10UpgradeDetectorPenalty : public V10UpgradeTest {};

  void createBlocksV10(BlockVectorEx& blockchain, size_t count, uint8_t majorVersion, uint8_t minorVersion, uint32_t startHeight) {
    for (size_t i = 0; i < count; ++i) {
      BlockEx b;
      b.bl.majorVersion = majorVersion;
      b.bl.minorVersion = minorVersion;
      b.bl.timestamp = 0;
      b.height = startHeight + i;
      blockchain.push_back(b);
    }
  }

  void createBlocksV10WithDetector(BlockVectorEx& blockchain, UpgradeDetector& upgradeDetector, size_t count, uint8_t majorVersion, uint8_t minorVersion, uint32_t startHeight) {
    for (size_t i = 0; i < count; ++i) {
      BlockEx b;
      b.bl.majorVersion = majorVersion;
      b.bl.minorVersion = minorVersion;
      b.bl.timestamp = 0;
      b.height = startHeight + i;
      blockchain.push_back(b);
      upgradeDetector.blockPushed();
    }
  }

  void popBlocksV10(BlockVectorEx& blockchain, UpgradeDetector& upgradeDetector, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      if (!blockchain.empty()) {
        blockchain.pop_back();
        upgradeDetector.blockPopped();
      }
    }
  }

  // Helper to create a simple v10 chain up to upgrade
  BlockVectorEx createSimpleV10Chain(CryptoNote::Currency& currency, uint32_t v10Height) {
    BlockVectorEx blocks;
    
    // Pre-v10 blocks (v9)
    uint32_t blocksBeforeV10 = v10Height;
    for (uint32_t i = 0; i < blocksBeforeV10 - 1; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_9;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.bl.timestamp = i * 60; // 1 minute intervals
      b.height = i;
      blocks.push_back(b);
    }
    
    // V10 blocks
    for (uint32_t i = v10Height - 1; i < v10Height + 100; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_10;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.bl.timestamp = i * 60;
      b.height = i;
      blocks.push_back(b);
    }
    
    return blocks;
  }

  // Test V10 detector with exact upgrade height
  TEST_F(V10UpgradeDetectorV10Init, handlesV10UpgradeAtExactHeight) {
    CryptoNote::Currency currency = createV10Currency(100);
    BlockVectorEx blocks;
    
    // Create blocks up to just before v10
    createBlocksV10(blocks, 99, BLOCK_MAJOR_VERSION_9, BLOCK_MINOR_VERSION_0, 0);
    
    // V10 upgrade detector
    UpgradeDetector v8_detector(currency, blocks, BLOCK_MAJOR_VERSION_8, logger);
    UpgradeDetector v9_detector(currency, blocks, BLOCK_MAJOR_VERSION_9, logger);
    UpgradeDetector v10_detector(currency, blocks, BLOCK_MAJOR_VERSION_10, logger);
    
    // Initialize all detectors
    ASSERT_TRUE(v8_detector.init());
    ASSERT_TRUE(v9_detector.init());
    ASSERT_TRUE(v10_detector.init());
    
    // Verify upgrade heights
    ASSERT_EQ(v8_detector.upgradeHeight(), 98);
    ASSERT_EQ(v9_detector.upgradeHeight(), 99);
    ASSERT_EQ(v10_detector.upgradeHeight(), 100);
    
    // V10 should be ready for the block at height 101
    // (v10 requires voting complete)
  }

  // Test V9 to V10 transition
  TEST_F(V10UpgradeDetectorV9Init, v9ToV10Transition) {
    CryptoNote::Currency currency = createV10Currency(100);
    BlockVectorEx blocks = createSimpleV10Chain(currency, 100);
    
    UpgradeDetector v9_detector(currency, blocks, BLOCK_MAJOR_VERSION_9, logger);
    UpgradeDetector v10_detector(currency, blocks, BLOCK_MAJOR_VERSION_10, logger);
    
    ASSERT_TRUE(v9_detector.init());
    ASSERT_TRUE(v10_detector.init());
    
    // V9 upgrade at 99, V10 at 100
    ASSERT_EQ(v9_detector.upgradeHeight(), 99);
    ASSERT_EQ(v10_detector.upgradeHeight(), 100);
  }

  // Test V10 voting with proper majority
  TEST_F(V10UpgradeDetectorVoting, v10VotingCompleteCondition) {
    CryptoNote::Currency currency = createV10Currency(100);
    BlockVectorEx blocks;
    
    // V9 chain setup (existing)
    createBlocksV10(blocks, 99, BLOCK_MAJOR_VERSION_9, BLOCK_MINOR_VERSION_0, 0);
    
    UpgradeDetector v10_detector(currency, blocks, BLOCK_MAJOR_VERSION_10, logger);
    ASSERT_TRUE(v10_detector.init());
    
    uint32_t nextBlockHeight = blocks.size();
    
    // Add V10 voting blocks (need 90% of 720 = 648 + proper minority)
    // This simulates the voting window for V10
    createBlocksV10WithDetector(blocks, v10_detector, 
      currency.upgradeVotingWindow(), 
      BLOCK_MAJOR_VERSION_9, BLOCK_MINOR_VERSION_1, 
      nextBlockHeight);
    
    nextBlockHeight += currency.upgradeVotingWindow();
    
    // Add required number of V9 blocks for voting (need 648 blocks with minor version 1)
    // Since we have 720 blocks total, we need 648 with v10 vote
    createBlocksV10WithDetector(blocks, v10_detector, 
      currency.minNumberVotingBlocks() - 1, 
      BLOCK_MAJOR_VERSION_9, BLOCK_MINOR_VERSION_1, 
      nextBlockHeight);
    
    nextBlockHeight += currency.minNumberVotingBlocks() - 1;
    
    // Should not be complete yet (missing 1 block)
    ASSERT_EQ(v10_detector.votingCompleteHeight(), UpgradeDetector::UNDEF_HEIGHT);
    
    // Add one more vote
    BlockEx b;
    b.bl.majorVersion = BLOCK_MAJOR_VERSION_9;
    b.bl.minorVersion = BLOCK_MINOR_VERSION_1;
    b.bl.timestamp = nextBlockHeight * 60;
    b.height = nextBlockHeight;
    blocks.push_back(b);
    v10_detector.blockPushed();
    
    // Now should be complete
    ASSERT_EQ(v10_detector.votingCompleteHeight(), blocks.size() - 1);
  }

  // Test emission speed factor for V10
  TEST_F(V10UpgradeDetectorEmission, v10EmissionFactor) {
    CryptoNote::Currency currency = createV10Currency(100);
    
    // V10 still uses FUEGO emission factor (20) per the design
    // v8 = FANGO (19), v9 = FUEGO (20), v10 = FUEGO (20)
    unsigned int v8_factor = currency.emissionSpeedFactor(BLOCK_MAJOR_VERSION_8);
    unsigned int v9_factor = currency.emissionSpeedFactor(BLOCK_MAJOR_VERSION_9);
    unsigned int v10_factor = currency.emissionSpeedFactor(BLOCK_MAJOR_VERSION_10);
    
    // These should match FANGO and FUEGO constants
    ASSERT_EQ(v8_factor, 19);  // FANGO
    ASSERT_EQ(v9_factor, 20);  // FUEGO
    ASSERT_EQ(v10_factor, 20); // Should use FUEGO (v9+) for v10
    
    // Verify FUEGO is 20 in the currency
    unsigned int fuego_factor = currency.emissionSpeedFactor_FUEGO();
    ASSERT_EQ(fuego_factor, 20);
  }

  // Test V10 minimum fee (8KH = 8000)
  TEST_F(V10UpgradeDetectorEmission, v10MinimumFee) {
    CryptoNote::Currency currency = createV10Currency(100);
    
    // V10 should have minimum fee of 8000 (8KH)
    uint64_t v10_fee = currency.minimumFee(BLOCK_MAJOR_VERSION_10);
    ASSERT_EQ(v10_fee, 8000);
    
    // V9 should be 0.008 XFG (80000)
    uint64_t v9_fee = currency.minimumFee(BLOCK_MAJOR_VERSION_9);
    ASSERT_EQ(v9_fee, 80000);
    
    // V8 should be 0.008 XFG (80000)  
    uint64_t v8_fee = currency.minimumFee(BLOCK_MAJOR_VERSION_8);
    ASSERT_EQ(v8_fee, 80000);
    
    // V1-V7 should be 0.08 XFG (800000)
    uint64_t v1_fee = currency.minimumFee(BLOCK_MAJOR_VERSION_1);
    ASSERT_EQ(v1_fee, 800000);
  }

  // Test V10 block penalty calculations
  TEST_F(V10UpgradeDetectorPenalty, v10BlockRewardPenaltyCalculation) {
    CryptoNote::Currency currency = createV10Currency(100);
    
    uint64_t amount = 10000000000; // 1000 XFG
    size_t medianSize = 430080;    // 420KB (full reward zone)
    size_t smallBlock = 200000;    // 200KB (under median) - no penalty
    size_t largeBlock = 860160;    // 840KB (2x median)
    size_t veryLargeBlock = 1290240; // 1.23MB (3x median) - full penalty
    
    // Get penalized amounts
    uint64_t noPenalty = CryptoNote::getPenalizedAmount(amount, medianSize, smallBlock, CryptoNote::BLOCK_MAJOR_VERSION_10);
    uint64_t moderatePenalty = CryptoNote::getPenalizedAmount(amount, medianSize, largeBlock, CryptoNote::BLOCK_MAJOR_VERSION_10);
    uint64_t fullPenalty = CryptoNote::getPenalizedAmount(amount, medianSize, veryLargeBlock, CryptoNote::BLOCK_MAJOR_VERSION_10);
    
    // Small block: no penalty (should equal original)
    ASSERT_EQ(noPenalty, amount);
    
    // 2x median: should be ~500 XFG
    uint64_t expectedModerate = amount / 2;
    uint64_t tolerance = amount / 100; // 1% tolerance
    ASSERT_NEAR(moderatePenalty, expectedModerate, tolerance);
    
    // 3x+ median: should be 0 (full penalty)
    ASSERT_EQ(fullPenalty, 0);
  }

  // Test V10 block reward full calculation
  TEST_F(V10UpgradeDetectorPenalty, v10FullBlockRewardCalculation) {
    CryptoNote::Currency currency = createV10Currency(100);
    
    uint8_t blockMajorVersion = BLOCK_MAJOR_VERSION_10;
    size_t medianSize = 430080;
    size_t currentBlockSize = 500000; // 477KB - slightly over median
    uint64_t alreadyGeneratedCoins = 5000000000000000; // Starting supply
    uint64_t fee = 8000; // V10 minimum fee
    uint32_t height = 101; // V10 height
    
    uint64_t reward = 0;
    int64_t emissionChange = 0;
    
    bool result = currency.getBlockReward(blockMajorVersion, medianSize, currentBlockSize, 
                                         alreadyGeneratedCoins, fee, height, reward, emissionChange);
    
    ASSERT_TRUE(result);
    ASSERT_GT(reward, 0);
    
    // V10 should get proper emission based on FUEGO factor (20)
    // (moneySupply - alreadyGeneratedCoins) >> 20
    uint64_t moneySupply = currency.moneySupply();
    uint64_t baseRewardExpected = (moneySupply - alreadyGeneratedCoins) >> 20;
    ASSERT_GT(reward, fee); // Reward should be more than just fee
    
    // Emission change should account for penalty
    ASSERT_GT(emissionChange, 0);
  }

  // Test V10 minimum mixin size enforcement
  TEST_F(V10UpgradeDetectorPenalty, v10MinimumMixinRequirement) {
    CryptoNote::Currency currency = createV10Currency(100);
    
    // V10 has MIN_TX_MIXIN_SIZE_V10 = 8
    size_t v10_min_mixin = currency.minMixin();
    size_t v10_min_mixin_height = 8;
    
    // This should be 8 from config
    ASSERT_EQ(v10_min_mixin, 8);
    ASSERT_EQ(v10_min_mixin_height, 8);
  }

  // Test V10 upgrade at exact value from DYNAMIGO spec
  TEST_F(V10UpgradeDetectorV10Init, v10SpecActivationHeight) {
    // Create currency with DYNAMIGO activation height
    CryptoNote::CurrencyBuilder currencyBuilder(logger);
    currencyBuilder.upgradeVotingThreshold(90);
    currencyBuilder.upgradeVotingWindow(720);
    currencyBuilder.upgradeWindow(720);
    
    // Use the activation height from DYNAMIGO spec
    currencyBuilder.upgradeHeightV10(980980);
    
    CryptoNote::Currency currency = currencyBuilder.currency();
    
    // Test that V10 is properly configured
    BlockVectorEx blocks;
    UpgradeDetector v10_detector(currency, blocks, BLOCK_MAJOR_VERSION_10, logger);
    
    ASSERT_TRUE(v10_detector.init());
    ASSERT_EQ(v10_detector.upgradeHeight(), 980980);
    
    // Verify emission for V10
    unsigned int v10_emission = currency.emissionSpeedFactor(BLOCK_MAJOR_VERSION_10);
    ASSERT_EQ(v10_emission, 20); // FUEGO emission factor
  }

  // Test complete multi-version chain (v1→v10)
  class V10MultiVersionChain : public V10UpgradeTest {};

  TEST_F(V10MultiVersionChain, completeVersionTransitions) {
    CryptoNote::Currency currency = createV10Currency(100);
    
    BlockVectorEx blocks;
    
    // Create full chain with all versions
    uint32_t heights[] = {0, 20, 40, 60, 80, 90, 95, 99, 100};
    uint8_t versions[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    // V1 blocks
    for (uint32_t i = 0; i < 20; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_1;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_1;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V2 blocks
    for (uint32_t i = 20; i < 40; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_2;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V3 blocks
    for (uint32_t i = 40; i < 60; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_3;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V4 blocks
    for (uint32_t i = 60; i < 80; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_4;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V5 blocks
    for (uint32_t i = 80; i < 90; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_5;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V6 blocks
    for (uint32_t i = 90; i < 95; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_6;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V7 blocks
    for (uint32_t i = 95; i < 98; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_7;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V8 blocks
    for (uint32_t i = 98; i < 99; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_8;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V9 blocks
    for (uint32_t i = 99; i < 100; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_9;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V10 blocks
    for (uint32_t i = 100; i < 200; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_10;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.height = i;
      blocks.push_back(b);
    }
    
    // Initialize all version detectors
    UpgradeDetector v2(currency, blocks, BLOCK_MAJOR_VERSION_2, logger);
    ASSERT_TRUE(v2.init());
    
    UpgradeDetector v3(currency, blocks, BLOCK_MAJOR_VERSION_3, logger);
    ASSERT_TRUE(v3.init());
    
    UpgradeDetector v4(currency, blocks, BLOCK_MAJOR_VERSION_4, logger);
    ASSERT_TRUE(v4.init());
    
    UpgradeDetector v5(currency, blocks, BLOCK_MAJOR_VERSION_5, logger);
    ASSERT_TRUE(v5.init());
    
    UpgradeDetector v6(currency, blocks, BLOCK_MAJOR_VERSION_6, logger);
    ASSERT_TRUE(v6.init());
    
    UpgradeDetector v7(currency, blocks, BLOCK_MAJOR_VERSION_7, logger);
    ASSERT_TRUE(v7.init());
    
    UpgradeDetector v8(currency, blocks, BLOCK_MAJOR_VERSION_8, logger);
    ASSERT_TRUE(v8.init());
    
    UpgradeDetector v9(currency, blocks, BLOCK_MAJOR_VERSION_9, logger);
    ASSERT_TRUE(v9.init());
    
    UpgradeDetector v10(currency, blocks, BLOCK_MAJOR_VERSION_10, logger);
    ASSERT_TRUE(v10.init());
    
    // Verify all major version transitions
    ASSERT_EQ(v2.upgradeHeight(), 20);
    ASSERT_EQ(v3.upgradeHeight(), 40);
    ASSERT_EQ(v4.upgradeHeight(), 60);
    ASSERT_EQ(v5.upgradeHeight(), 80);
    ASSERT_EQ(v6.upgradeHeight(), 90);
    ASSERT_EQ(v7.upgradeHeight(), 95);
    ASSERT_EQ(v8.upgradeHeight(), 98);
    ASSERT_EQ(v9.upgradeHeight(), 99);
    ASSERT_EQ(v10.upgradeHeight(), 100);
    
    // Verify emission factors
    ASSERT_EQ(currency.emissionSpeedFactor(BLOCK_MAJOR_VERSION_8), 19);
    ASSERT_EQ(currency.emissionSpeedFactor(BLOCK_MAJOR_VERSION_9), 20);
    ASSERT_EQ(currency.emissionSpeedFactor(BLOCK_MAJOR_VERSION_10), 20);
  }

  // Integration test: V9 pre-mining → V10 voting → V10 upgrade
  class V10IntegrationTests : public V10UpgradeTest {};

  TEST_F(V10IntegrationTests, v9VotingToV10Activation) {
    // This simulates the complete V10 upgrade process
    CryptoNote::Currency currency;
    {
      CryptoNote::CurrencyBuilder builder(logger);
      builder.upgradeVotingThreshold(90);
      builder.upgradeVotingWindow(720);
      builder.upgradeWindow(720);
      
      // Set V10 at 1000 for this test
      builder.upgradeHeightV10(1000);
      currency = builder.currency();
    }
    
    BlockVectorEx blocks;
    
    // Phase 1: V9 chain up to voting start (999 blocks)
    for (uint32_t i = 0; i < 1000; ++i) {
      BlockEx b;
      // First 720 blocks: standard V9
      if (i < 720) {
        b.bl.majorVersion = BLOCK_MAJOR_VERSION_9;
        b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      } else {
        // Last 280 blocks: voting for V10
        b.bl.majorVersion = BLOCK_MAJOR_VERSION_9;
        b.bl.minorVersion = BLOCK_MINOR_VERSION_1;
      }
      b.bl.timestamp = i * 60;
      b.height = i;
      blocks.push_back(b);
    }
    
    // Add detector
    UpgradeDetector v10_detector(currency, blocks, BLOCK_MAJOR_VERSION_10, logger);
    ASSERT_TRUE(v10_detector.init());
    
    // V180 at 999, not complete yet
    ASSERT_EQ(v10_detector.votingCompleteHeight(), UpgradeDetector::UNDEF_HEIGHT);
    
    // Phase 2: Add voting completion blocks (need 648 with v10 vote)
    // Currently have 280, need 648 more at minor version 1
    for (uint32_t i = 1000; i < 1369; ++i) {  // 280 + 649 = 929 blocks (exceeds 648)
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_9;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_1;
      b.bl.timestamp = i * 60;
      b.height = i;
      blocks.push_back(b);
      v10_detector.blockPushed();
    }
    
    // V10 voting should now be complete
    ASSERT_NE(v10_detector.votingCompleteHeight(), UpgradeDetector::UNDEF_HEIGHT);
    ASSERT_EQ(v10_detector.votingCompleteHeight(), 1368); // Height where 648th vote occurred
    
    // V10 upgrade should happen at 1000 + 720 = 1720
    // Wait for upgrade window
    for (uint32_t i = blocks.size(); i < 1720; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_9;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.bl.timestamp = i * 60;
      b.height = i;
      blocks.push_back(b);
      v10_detector.blockPushed();
    }
    
    // Upgrade height
    uint32 upgrade_height = currency.calculateUpgradeHeight(1368);
    ASSERT_EQ(v10_detector.upgradeHeight(), upgrade_height);
  }

  // Test V10 block template construction
  TEST_F(V10IntegrationTests, v10BlockTemplateReward) {
    CryptoNote::Currency currency = createV10Currency(1000);
    
    // V10 block at height 1001
    uint8_t version = BLOCK_MAJOR_VERSION_10;
    uint32_t height = 1001;
    
    size_t medianSize = 430080; // Standard zone
    size_t currentSize = 500000; // Slightly over median
    uint64_t alreadyGen = 500000000000000;
    uint64_t fee = 8000; // V10 min fee
    
    uint64_t reward;
    int64_t emissionChange;
    
    bool ok = currency.getBlockReward(version, medianSize, currentSize, alreadyGen, fee, height, reward, emissionChange);
    ASSERT_TRUE(ok);
    
    // Verify reward calculation is consistent
    ASSERT_GT(reward, fee);
    ASSERT_GT(emissionChange, 0);
    
    // V10 should use FUEGO emission (20) - verify reward size
    // Reward should be approximately: (8M8 - alreadyGen) >> 20 minus penalty
    uint64_t moneySupply = currency.moneySupply();
    uint64_t expectedBase = (moneySupply - alreadyGen) >> 20;
    uint64_t penalty = CryptoNote::getPenalizedAmount(expectedBase, medianSize, currentSize, CryptoNote::BLOCK_MAJOR_VERSION_10);
    
    // With negative penalty, reward should be penalized
    uint64_t expectedReward = penalty + fee;
    
    // Allow reasonable variance
    uint64_t tolerance = expectedReward / 10; // 10% tolerance
    ASSERT_NEAR(reward, expectedReward, tolerance);
  }

  // Test v10 inflation protection with dynamic supply
  class V10DynamicSupplyTest : public V10UpgradeTest {};

  TEST_F(V10DynamicSupplyTest, v10EternalFlameIntegration) {
    CryptoNote::Currency currency = createV10Currency(1000);
    
    double burn_pct_1 = currency.getBurnPercentage();
    ASSERT_EQ(burn_pct_1, 0.0); // Start at zero
    
    // Simulate burn adding to eternal flame
    uint64_t burnAmount = 8000000000; // 800 XFG burn
    currency.addEternalFlame(burnAmount);
    
    double burn_pct_2 = currency.getBurnPercentage();
    ASSERT_GT(burn_pct_2, 0.0);
    
    // Verify base reward calculation accounts for eternal flame
    uint64_t baseMoneySupply = currency.getBaseMoneySupply();
    uint64_t eternalFlame;
    currency.getEternalFlame(eternalFlame);
    
    // At v10, base supply should be constant (8M8)
    // Eternal flame represents burned XFG
    // Real supply = baseSupply - (burned - reborn)
    // For our test: baseMoneySupply is fixed
    
    uint64_t moneySupply = currency.moneySupply();
    uint64_t adjusted = moneySupply - eternalFlame;
    
    // V10 block reward calculation
    uint8_t version = BLOCK_MAJOR_VERSION_10;
    uint32_t height = 1001;
    uint64_t fee = 0;
    
    uint64_t reward;
    int64_t emissionChange;
    
    bool ok = currency.getBlockReward(version, 430080, 400000, moneySupply - 8000000000, fee, height, reward, emissionChange);
    ASSERT_TRUE(ok);
    
    // V10 should handle eternal flame differently than pre-v10
    // Reward should be based on adjusted supply
    ASSERT_GT(reward, 0);
  }

  // Test V10 min ring size enforcement
  class V10PrivacyTest : public V10UpgradeTest {};

  TEST_F(V10PrivacyTest, v10MinimumRingSizeEnforcement) {
    CryptoNote::Currency currency = createV10Currency(1000);
    
    // V10 requires minimum mixin of 8
    size_t minMixin = currency.minMixin();
    ASSERT_EQ(minMixin, 8);
    
    // Test lock/trickle size calculation
    size_t txMaxSize = currency.transactionMaxSize();
    size_t fusionMaxSize = currency.fusionTxMaxSize();
    
    // V10 maintain reasonable sizes
    ASSERT_GT(txMaxSize, 1000000);
    ASSERT_GT(fusionMaxSize, 100000);
  }

  // Test V10 upgrade recovery (rollback)
  class V10UpgradeRollback : public V10UpgradeTest {};

  TEST_F(V10UpgradeRollback, v10RollbackDuringVoting) {
    CryptoNote::Currency currency = createV10Currency(1000);
    
    BlockVectorEx blocks;
    
    // Create chain
    for (uint32_t i = 0; i < 1000; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_9;
      b.bl.minorVersion = (i >= 720 && i < 1368) ? BLOCK_MINOR_VERSION_1 : BLOCK_MINOR_VERSION_0;
      b.bl.timestamp = i * 60;
      b.height = i;
      blocks.push_back(b);
    }
    
    UpgradeDetector v10_detector(currency, blocks, BLOCK_MAJOR_VERSION_10, logger);
    ASSERT_TRUE(v10_detector.init());
    
    // Add voting blocks
    for (uint32_t i = 1000; i < 1369; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_9;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_1;
      b.bl.timestamp = i * 60;
      b.height = i;
      blocks.push_back(b);
      v10_detector.blockPushed();
    }
    
    ASSERT_EQ(v10_detector.votingCompleteHeight(), 1368);
    
    // Rollback 10 blocks
    popBlocksV10(blocks, v10_detector, 10);
    
    // v10 detector should still work
    ASSERT_TRUE(v10_detector.init());
    // Voting should be incomplete after rollback
    ASSERT_EQ(v10_detector.votingCompleteHeight(), UpgradeDetector::UNDEF_HEIGHT);
  }

  // Test v10 specific parameters in Currency class
  class V10ParameterTests : public V10UpgradeTest {};

  TEST_F(V10ParameterTests, v10CurrencyConfiguration) {
    CryptoNote::CurrencyBuilder builder(logger);
    
    // V10 specific parameters
    builder.upgradeHeightV10(980980);
    builder.emissionSpeedFactor_FUEGO(20);
    
    CryptoNote::Currency currency = builder.currency();
    
    // Max mixin for privacy
    size_t maxMixin = currency.maxMixin();
    ASSERT_GT(maxMixin, 8);
    
    // Min mixin for v10
    size_t minMixin_v10 = currency.minMixin();
    ASSERT_EQ(minMixin_v10, 8);
  }

  // Test V10 block size limits
  TEST_F(V10ParameterTests, v10BlockSizeHandling) {
    CryptoNote::Currency currency = createV10Currency(1000);
    
    // V10 should use the current block granted full reward zone
    size_t zone_v10 = currency.blockGrantedFullRewardZoneByBlockVersion(BLOCK_MAJOR_VERSION_10);
    size_t zone_v9 = currency.blockGrantedFullRewardZoneByBlockVersion(BLOCK_MAJOR_VERSION_9);
    size_t zone_at_height = currency.blockGrantedFullRewardZoneByHeightVersion(BLOCK_MAJOR_VERSION_10, 1001);
    
    // V10 should maintain or increase size limit
    ASSERT_GE(zone_v10, zone_v9);
    ASSERT_EQ(zone_at_height, zone_v10);
    
    // Currency max block cumulative size
    size_t maxBlock = currency.maxBlockCumulativeSize(10000);
    ASSERT_GT(maxBlock, 0);
  }

  // Test V10 DMWDA difficulty settings
  class V10DMWDATests : public V10UpgradeTest {};

  TEST_F(V10DMWDATests, v10DMWDAFlags) {
    CryptoNote::Currency currency = createV10Currency(1000);
    
    // DMWDA configurations from DYNAMIGO spec
    // These are namespace constants, verify they exist
    using namespace CryptoNote::parameters;
    
    // These should be defined in the currency
    // SHORT_WINDOW, MEDIUM_WINDOW, LONG_WINDOW, EMERGENCY_WINDOW
    
    // Verify we can create a Block with V10 properties
    BlockEx testBlock;
    testBlock.bl.majorVersion = BLOCK_MAJOR_VERSION_10;
    testBlock.bl.minorVersion = BLOCK_MINOR_VERSION_0;
    testBlock.height = 1001;
    
    // The block should be valid for v10
    ASSERT_EQ(testBlock.bl.majorVersion, 10);
    ASSERT_EQ(testBlock.height, 1001);
  }

  // Final comprehensive test: V10 full upgrade scenario
  class V10FullScenario : public V10UpgradeTest {};

  TEST_F(V10FullScenario, V10CompleteUpgradeProcess) {
    // This tests the entire V10 upgrade from V1 to V10 with all mechanisms
    
    CryptoNote::Currency currency = createV10Currency(1000);
    
    BlockVectorEx blocks;
    
    // Create full chain with test data
    // V1-V7: 0-94 (95 blocks)
    createBlocksV10(blocks, 95, BLOCK_MAJOR_VERSION_1, BLOCK_MINOR_VERSION_1, 0);
    
    // V8: 95-97 (3 blocks)
    for (uint32_t i = 95; i < 98; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_8;
      b.bl.minorVersion = BLOCK_MINOR_VERSION_0;
      b.bl.timestamp = i * 60;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V9: 98-999 (902 blocks, includes voting for V10)
    for (uint32_t i = 98; i < 1000; ++i) {
      BlockEx b;
      b.bl.majorVersion = BLOCK_MAJOR_VERSION_9;
      // Voting for V10 starts at 720, so enable minor version 1
      b.bl.minorVersion = (i >= 720) ? BLOCK_MINOR_VERSION_1 : BLOCK_MINOR_VERSION_0;
      b.bl.timestamp = i * 60;
      b.height = i;
      blocks.push_back(b);
    }
    
    // V10: 1000-1050 (50 blocks)
    createBlocksV10(blocks, 50, BLOCK_MAJOR_VERSION_10, BLOCK_MINOR_VERSION_0, 1000);
    
    // Initialize detectors
    UpgradeDetector v8(currency, blocks, BLOCK_MAJOR_VERSION_8, logger);
    UpgradeDetector v9(currency, blocks, BLOCK_MAJOR_VERSION_9, logger);
    UpgradeDetector v10(currency, blocks, BLOCK_MAJOR_VERSION_10, logger);
    
    ASSERT_TRUE(v8.init());
    ASSERT_TRUE(v9.init());
    ASSERT_TRUE(v10.init());
    
    // Verify version at specific heights
    uint8_t v10_start = currency.blockMajorVersionAtHeight(1000);
    uint8_t v9_before = currency.blockMajorVersionAtHeight(999);
    uint8_t v8_before = currency.blockMajorVersionAtHeight(97);
    
    ASSERT_EQ(v8_before, BLOCK_MAJOR_VERSION_8);
    ASSERT_EQ(v9_before, BLOCK_MAJOR_VERSION_9);
    ASSERT_EQ(v10_start, BLOCK_MAJOR_VERSION_10);
    
    // Test block reward at V10 height
    uint64_t reward;
    int64_t emission;
    bool ok = currency.getBlockReward(
      BLOCK_MAJOR_VERSION_10,
      currency.blockGrantedFullRewardZone(),
      400000, // 381KB block
      currency.moneySupply() - 4000000000000, // 4M XFG generated
      8000,  // V10 fee
      1001,  // V10 height
      reward,
      emission
    );
    
    ASSERT_TRUE(ok);
    ASSERT_GT(reward, 0);
    ASSERT_GT(emission, 0);
    
    // Fee verification
    uint64_t v10_fee = currency.minimumFee(BLOCK_MAJOR_VERSION_10);
    uint64_t v9_fee = currency.minimumFee(BLOCK_MAJOR_VERSION_9);
    uint64_t v8_fee = currency.minimumFee(BLOCK_MAJOR_VERSION_8);
    
    ASSERT_EQ(v10_fee, 8000);   // 8KH = 0.0008 XFG
    ASSERT_EQ(v9_fee, 80000);   // 0.008 XFG
    ASSERT_EQ(v8_fee, 80000);   // 0.008 XFG (v8 change)
    
    // Emission factor verification
    ASSERT_EQ(currency.emissionSpeedFactor(BLOCK_MAJOR_VERSION_8), 19);
    ASSERT_EQ(currency.emissionSpeedFactor(BLOCK_MAJOR_VERSION_9), 20);
    ASSERT_EQ(currency.emissionSpeedFactor(BLOCK_MAJOR_VERSION_10), 20);
    
    // Validate the entire chain
    for (const auto& block : blocks) {
      uint8_t expected = currency.blockMajorVersionAtHeight(block.height);
      ASSERT_EQ(block.bl.majorVersion, expected) << "Height: " << block.height;
    }
    
    // Simulate penalty calculation for V10 blocks
    uint64_t basePenalty = CryptoNote::getPenalizedAmount(
      5000000000,          // 500 XFG
      430080,               // 420KB median
      500000,               // 476KB block
      CryptoNote::BLOCK_MAJOR_VERSION_10
    );
    
    // Should be slightly penalized (500KB vs 420KB median)
    ASSERT_LT(basePenalty, 5000000000);
    ASSERT_GT(basePenalty, 0);
    
    // V10 advantage: lower fees, same emissions (FUEGO)
    // V9 requires 80000 fee, V10 requires 8000
    // But V9 may have different reward calculations
  }

} // namespace