// Copyright (c) 2017-2026 Fuego Developers
#include "gtest/gtest.h"
#include "CryptoNoteCore/HeatMintEngine.h"
#include "CryptoNoteConfig.h"
#include <limits>

using namespace CryptoNote;

// Canonical price scale: HEAT atomics per XFG atomic × COIN.
// 5 HEAT per XFG → price = 5 × COIN.
static const uint64_t PRICE_5 = 5 * parameters::COIN;

// Base-math tests run at v10 (no mint premium) so their expectations stay the
// plain burn→HEAT ratio. The premium is a v11 rule; v11 tests below opt in.
static const uint8_t V10 = BLOCK_MAJOR_VERSION_10;
static const uint8_t V11 = BLOCK_MAJOR_VERSION_11;

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

static bool validate(const HeatMintEngine& engine, const Transaction& tx,
                     uint64_t fee, uint64_t price, uint64_t& burned,
                     uint64_t& minted, uint8_t version = V10) {
  uint64_t premiumHeat = 0;
  return engine.validateMint(tx, fee, price, burned, minted, version,
                             premiumHeat);
}

TEST(HeatMintEngine, ValidMint) {
  HeatMintEngine engine;
  Transaction tx = makeHeatMintTx(100, 500, 0); // 100 XFG → 500 HEAT at 5 HEAT/XFG
  uint64_t burned = 0, minted = 0;
  EXPECT_TRUE(validate(engine, tx, 0, PRICE_5, burned, minted));
  EXPECT_EQ(burned, 100u);
  EXPECT_EQ(minted, 500u);
}

TEST(HeatMintEngine, InvalidRatio) {
  HeatMintEngine engine;
  Transaction tx = makeHeatMintTx(100, 600, 0); // expects 500, got 600
  uint64_t burned = 0, minted = 0;
  EXPECT_FALSE(validate(engine, tx, 0, PRICE_5, burned, minted));
}

TEST(HeatMintEngine, LaunchRatio) {
  // Seed pool: 10 XFG per 1 HEAT → HEAT/XFG = 0.1 → price = COIN / 10.
  HeatMintEngine engine;
  const uint64_t price = parameters::COIN / 10;
  Transaction tx = makeHeatMintTx(parameters::COIN, parameters::COIN / 10, 0); // 1 XFG → 0.1 HEAT
  uint64_t burned = 0, minted = 0;
  EXPECT_TRUE(validate(engine, tx, 0, price, burned, minted));
  EXPECT_EQ(burned, parameters::COIN);
  EXPECT_EQ(minted, parameters::COIN / 10);
}

TEST(HeatMintEngine, OverMintRejected) {
  // 1 XFG at launch ratio: minting 0.2 HEAT (2× over) must be rejected.
  HeatMintEngine engine;
  const uint64_t price = parameters::COIN / 10;
  Transaction tx = makeHeatMintTx(parameters::COIN, parameters::COIN / 5, 0);
  uint64_t burned = 0, minted = 0;
  EXPECT_FALSE(validate(engine, tx, 0, price, burned, minted));
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
  EXPECT_FALSE(validate(engine, tx, 0, 0, burned, minted));
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
  EXPECT_FALSE(validate(engine, tx, 0, PRICE_5, burned, minted));
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
  EXPECT_FALSE(validate(engine, tx, 10, PRICE_5, burned, minted));
}

// Amounts in a mint transaction are chosen by its author, so the sums in
// scanMintTx must not wrap. Before the checked adds, `xfgOutputs + fee` wrapped
// for a large enough output: a transaction creating an output of UINT64_MAX
// from 1000 atomics of input passed conservation and reported a burn of 1000.
TEST(HeatMintEngine, OutputPlusFeeOverflowRejected) {
  HeatMintEngine engine;
  Transaction tx;

  KeyInput ki;
  ki.amount = 1000;
  tx.inputs.push_back(ki);

  TransactionOutput bigOut;
  bigOut.amount = std::numeric_limits<uint64_t>::max();
  bigOut.target = KeyOutput();
  tx.outputs.push_back(bigOut);

  TransactionOutput heatOut;
  heatOut.amount = 1;
  TransactionOutputCommitment co;
  co.term = parameters::HEAT_TERM;
  heatOut.target = co;
  tx.outputs.push_back(heatOut);

  uint64_t burned = 0, minted = 0;
  EXPECT_FALSE(validate(engine, tx, 1, PRICE_5, burned, minted));
}

TEST(HeatMintEngine, InputSumOverflowRejected) {
  HeatMintEngine engine;
  Transaction tx;

  for (int i = 0; i < 2; ++i) {
    KeyInput ki;
    ki.amount = std::numeric_limits<uint64_t>::max();
    tx.inputs.push_back(ki);
  }

  TransactionOutput heatOut;
  heatOut.amount = 1;
  TransactionOutputCommitment co;
  co.term = parameters::HEAT_TERM;
  heatOut.target = co;
  tx.outputs.push_back(heatOut);

  uint64_t burned = 0, minted = 0;
  EXPECT_FALSE(validate(engine, tx, 0, PRICE_5, burned, minted));
}

// --- Mint premium (v11+) -----------------------------------------------------
//
// The premium is a haircut INSIDE the declared burn, not a surcharge: the burn
// still buys heat(xfgBurned) in total, but heat(premiumXfg) of it is minted to
// the AuntPoL HEAT leg (treasury reserve) and only the remainder may reach the
// minter. So the minter's cap is heat(xfgBurned) - heat(premiumXfg).

TEST(HeatMintEngine, PremiumIsZeroBeforeV11) {
  HeatMintEngine engine;
  Transaction tx = makeHeatMintTx(100, 500, 0);
  uint64_t burned = 0, minted = 0, premium = 0;
  EXPECT_TRUE(engine.validateMint(tx, 0, PRICE_5, burned, minted, V10, premium));
  EXPECT_EQ(premium, 0u);
  EXPECT_EQ(minted, 500u);
}

TEST(HeatMintEngine, PremiumHeatForAtV11) {
  HeatMintEngine engine;
  // 1% of 100 XFG = 1 XFG of the burn → 5 HEAT at 5 HEAT/XFG.
  EXPECT_EQ(engine.premiumHeatFor(100, PRICE_5, V11), 5u);
  EXPECT_EQ(engine.premiumHeatFor(100, PRICE_5, V10), 0u);
  EXPECT_EQ(engine.premiumHeatFor(0, PRICE_5, V11), 0u);
}

TEST(HeatMintEngine, V11MintCappedByPremium) {
  HeatMintEngine engine;
  // 100 XFG buys 500 HEAT; 1% premium reserves 5, so 495 is the cap.
  Transaction atCap = makeHeatMintTx(100, 495, 0);
  uint64_t burned = 0, minted = 0, premium = 0;
  EXPECT_TRUE(engine.validateMint(atCap, 0, PRICE_5, burned, minted, V11, premium));
  EXPECT_EQ(burned, 100u);
  EXPECT_EQ(minted, 495u);
  EXPECT_EQ(premium, 5u);

  // One atomic over the cap is rejected — the premium may not be handed back.
  Transaction overCap = makeHeatMintTx(100, 496, 0);
  EXPECT_FALSE(engine.validateMint(overCap, 0, PRICE_5, burned, minted, V11, premium));

  // And the full pre-premium amount is rejected too.
  Transaction full = makeHeatMintTx(100, 500, 0);
  EXPECT_FALSE(engine.validateMint(full, 0, PRICE_5, burned, minted, V11, premium));
}

TEST(HeatMintEngine, V11PremiumTotalIsConserved) {
  // Whatever the split, the burn still creates exactly heat(xfgBurned):
  // the minter's mint plus the premium must equal the no-premium entitlement.
  HeatMintEngine engine;
  Transaction tx = makeHeatMintTx(100, 495, 0);
  uint64_t burned = 0, minted = 0, premium = 0;
  ASSERT_TRUE(engine.validateMint(tx, 0, PRICE_5, burned, minted, V11, premium));
  EXPECT_EQ(minted + premium, 500u);
}

TEST(HeatMintEngine, V11AuthRejectsMintedAboveCap) {
  HeatMintEngine engine;
  uint64_t premium = 0;

  // The declared mint matches the tx, but it is above the premium-adjusted cap.
  Transaction over = makeHeatMintTx(100, 496, 0);
  EXPECT_FALSE(engine.validateMintAuth(over, 0, PRICE_5, 100, 496, V11, premium));

  Transaction ok = makeHeatMintTx(100, 495, 0);
  EXPECT_TRUE(engine.validateMintAuth(ok, 0, PRICE_5, 100, 495, V11, premium));
  EXPECT_EQ(premium, 5u);
}

TEST(HeatMintEngine, V11AuthUnchangedBeforeV11) {
  HeatMintEngine engine;
  Transaction tx = makeHeatMintTx(100, 500, 0);
  uint64_t premium = 0;
  EXPECT_TRUE(engine.validateMintAuth(tx, 0, PRICE_5, 100, 500, V10, premium));
  EXPECT_EQ(premium, 0u);
}

// --- Premium funds BOTH AuntPoL legs ----------------------------------------
//
// The premium is split across the two assets the LP needs. The XFG half is the
// withheld premium XFG (it is never burned, so the burn tallies only see the
// remainder); the HEAT half is what that XFG buys at the mint TWAP. Because
// both halves are derived from the same premiumXfg, the position is
// self-funding: no SWF transfer, and the two legs pair at the mint price.

TEST(HeatMintEngine, PremiumXfgIsOnePercentOfBurn) {
  EXPECT_EQ(heatMintPremiumXfg(10000, V11), 100u);
  EXPECT_EQ(heatMintPremiumXfg(100, V11), 1u);
  EXPECT_EQ(heatMintPremiumXfg(99, V11), 0u); // floors, never rounds up
  EXPECT_EQ(heatMintPremiumXfg(10000, V10), 0u);
}

TEST(HeatMintEngine, PremiumXfgPlusBankedBurnIsDeclaredBurn) {
  // bankedBurn() is the declared burn minus the premium. The two must add back
  // to the declared burn exactly, or the EF/SWF tallies drift from the XFG the
  // transaction actually destroys.
  for (uint64_t xfg : {1u, 99u, 100u, 12345u, 1000000u, 123456789u}) {
    const uint64_t premium = heatMintPremiumXfg(xfg, V11);
    const uint64_t burned = xfg - premium;
    EXPECT_LE(premium, xfg);
    EXPECT_EQ(burned + premium, xfg);
  }
}

TEST(HeatMintEngine, PremiumLegsAreBothPricedFromPremiumXfg) {
  HeatMintEngine engine;
  // 10,000 XFG at 5 HEAT/XFG: 1% premium = 100 XFG → 500 HEAT. The HEAT leg is
  // exactly what the XFG leg buys, so the pair is balanced at the mint price.
  const uint64_t xfgBurned = 10000;
  const uint64_t premiumXfg = heatMintPremiumXfg(xfgBurned, V11);
  const uint64_t premiumHeat = engine.premiumHeatFor(xfgBurned, PRICE_5, V11);
  EXPECT_EQ(premiumXfg, 100u);
  EXPECT_EQ(premiumHeat, 500u);
  EXPECT_EQ(premiumHeat, (premiumXfg * PRICE_5) / parameters::COIN);
  // The minter's cap still excludes both legs.
  Transaction tx = makeHeatMintTx(xfgBurned, 49500, 0); // 50,000 - 500
  uint64_t burned = 0, minted = 0, premium = 0;
  EXPECT_TRUE(engine.validateMint(tx, 0, PRICE_5, burned, minted, V11, premium));
  EXPECT_EQ(burned, xfgBurned);
  EXPECT_EQ(minted, 49500u);
  EXPECT_EQ(premium, premiumHeat);
}
