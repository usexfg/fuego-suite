// Copyright (c) 2017-2026 Fuego Developers
#include "gtest/gtest.h"
#include "CryptoNoteCore/HeatMintEngine.h"
#include "Common/FixedPoint.h"

using namespace CryptoNote;

// Mock Transaction builder for testing
static Transaction makeHeatMintTx(uint64_t xfgIn, uint64_t heatOut, uint64_t fee) {
  Transaction tx;
  // XFG input
  KeyInput ki;
  ki.amount = xfgIn;
  tx.inputs.push_back(ki);
  // HEAT output (commitment with FOREVER term)
  TransactionOutput out;
  out.amount = heatOut;
  TransactionOutputCommitment co;
  co.term = CryptoNote::parameters::HEAT_TERM;
  out.target = co;
  tx.outputs.push_back(out);
  return tx;
}

TEST(HeatMintEngine, ValidMint) {
  HeatMintEngine engine;
  Transaction tx = makeHeatMintTx(100, 500, 0); // 100 XFG → 500 HEAT at 0.2 rate
  uint64_t burned = 0, minted = 0;
  FixedPoint64 price = FixedPoint64::fromRatio(1, 5); // 0.2
  EXPECT_TRUE(engine.validateMint(tx, 0, price, burned, minted));
  EXPECT_EQ(burned, 100u);
  EXPECT_EQ(minted, 500u);
}

TEST(HeatMintEngine, InvalidRatio) {
  HeatMintEngine engine;
  Transaction tx = makeHeatMintTx(100, 600, 0); // expects 500, got 600
  uint64_t burned = 0, minted = 0;
  FixedPoint64 price = FixedPoint64::fromRatio(1, 5);
  EXPECT_FALSE(engine.validateMint(tx, 0, price, burned, minted));
}

TEST(HeatMintEngine, IsHeatMint) {
  HeatMintEngine engine;
  Transaction tx = makeHeatMintTx(100, 500, 0);
  EXPECT_TRUE(engine.isHeatMint(tx));
}

TEST(HeatMintEngine, NotHeatMint) {
  HeatMintEngine engine;
  Transaction tx;
  KeyInput ki;
  ki.amount = 100;
  tx.inputs.push_back(ki);
  KeyOutput ko;
  tx.outputs.push_back({100, ko});
  EXPECT_FALSE(engine.isHeatMint(tx));
}

TEST(HeatMintEngine, ZeroRedemptionPrice) {
  HeatMintEngine engine;
  Transaction tx = makeHeatMintTx(100, 500, 0);
  uint64_t burned, minted;
  FixedPoint64 zero;
  EXPECT_FALSE(engine.validateMint(tx, 0, zero, burned, minted));
}

TEST(HeatMintEngine, InsufficientBalance) {
  HeatMintEngine engine;
  Transaction tx;
  KeyInput ki;
  ki.amount = 50;
  tx.inputs.push_back(ki);
  TransactionOutput out;
  out.amount = 500;
  TransactionOutputCommitment co;
  co.term = CryptoNote::parameters::HEAT_TERM;
  out.target = co;
  tx.outputs.push_back(out);
  uint64_t burned, minted;
  FixedPoint64 price = FixedPoint64::fromRatio(1, 5);
  EXPECT_FALSE(engine.validateMint(tx, 0, price, burned, minted));
}
