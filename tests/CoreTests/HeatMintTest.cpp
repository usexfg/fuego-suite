// Copyright (c) 2017-2026 Fuego Developers
#include "gtest/gtest.h"
#include "CryptoNoteCore/HeatMintEngine.h"
#include "CryptoNoteConfig.h"

using namespace CryptoNote;

// Canonical price scale: HEAT atomics per XFG atomic × COIN.
// 5 HEAT per XFG → price = 5 × COIN.
static const uint64_t PRICE_5 = 5 * parameters::COIN;

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
  Transaction tx = makeHeatMintTx(100, 500, 0); // 100 XFG → 500 HEAT at 5 HEAT/XFG
  uint64_t burned = 0, minted = 0;
  EXPECT_TRUE(engine.validateMint(tx, 0, PRICE_5, burned, minted));
  EXPECT_EQ(burned, 100u);
  EXPECT_EQ(minted, 500u);
}

TEST(HeatMintEngine, InvalidRatio) {
  HeatMintEngine engine;
  Transaction tx = makeHeatMintTx(100, 600, 0); // expects 500, got 600
  uint64_t burned = 0, minted = 0;
  EXPECT_FALSE(engine.validateMint(tx, 0, PRICE_5, burned, minted));
}

TEST(HeatMintEngine, LaunchRatio) {
  // Seed pool: 10 XFG per 1 HEAT → HEAT/XFG = 0.1 → price = COIN / 10.
  HeatMintEngine engine;
  const uint64_t price = parameters::COIN / 10;
  Transaction tx = makeHeatMintTx(parameters::COIN, parameters::COIN / 10, 0); // 1 XFG → 0.1 HEAT
  uint64_t burned = 0, minted = 0;
  EXPECT_TRUE(engine.validateMint(tx, 0, price, burned, minted));
  EXPECT_EQ(burned, parameters::COIN);
  EXPECT_EQ(minted, parameters::COIN / 10);
}

TEST(HeatMintEngine, OverMintRejected) {
  // 1 XFG at launch ratio: minting 0.2 HEAT (2× over) must be rejected.
  HeatMintEngine engine;
  const uint64_t price = parameters::COIN / 10;
  Transaction tx = makeHeatMintTx(parameters::COIN, parameters::COIN / 5, 0);
  uint64_t burned = 0, minted = 0;
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
  EXPECT_FALSE(engine.validateMint(tx, 0, 0, burned, minted));
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
  EXPECT_FALSE(engine.validateMint(tx, 0, PRICE_5, burned, minted));
}

TEST(HeatMintEngine, FeeReducesBurn) {
  HeatMintEngine engine;
  // 100 in, 90 out, 10 fee → burned = 0 → no HEAT allowed
  Transaction tx;
  KeyInput ki;
  ki.amount = 100;
  tx.inputs.push_back(ki);
  TransactionOutput out;
  out.amount = 90;
  out.target = KeyOutput();
  tx.outputs.push_back(out);
  uint64_t burned = 0, minted = 0;
  EXPECT_FALSE(engine.validateMint(tx, 10, PRICE_5, burned, minted));
}
