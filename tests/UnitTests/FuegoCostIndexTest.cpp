#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/FuegoCostIndex.h"
#include "CryptoNoteCore/TransactionExtra.h"

using namespace CryptoNote;

namespace {

using Basket = std::array<uint32_t, FuegoCostIndex::COMMODITY_COUNT>;

// Index order matches the vote struct: power, milk, bread, eggs, gas, rice.
constexpr size_t POWER = 0;
constexpr size_t MILK = 1;
constexpr size_t BREAD = 2;
constexpr size_t EGGS = 3;
constexpr size_t GAS = 4;
constexpr size_t RICE = 5;

// Every category bit set.
constexpr uint8_t ALL = 0x3f;
// The five categories that existed before rice was added.
constexpr uint8_t PRE_RICE = 0x1f;

std::vector<uint8_t> makeVote(uint8_t mask, const Basket& prices) {
  TransactionExtraMinerBasketVote vote{};
  vote.present_mask = mask;
  vote.power = prices[POWER];
  vote.milk = prices[MILK];
  vote.bread = prices[BREAD];
  vote.eggs = prices[EGGS];
  vote.gas = prices[GAS];
  vote.rice = prices[RICE];
  std::vector<uint8_t> extra;
  EXPECT_TRUE(addMinerBasketVoteToExtra(extra, vote));
  return extra;
}

Basket baselinePrices() {
  return {parameters::FCI_BASELINE_POWER, parameters::FCI_BASELINE_MILK,
          parameters::FCI_BASELINE_BREAD, parameters::FCI_BASELINE_EGGS,
          parameters::FCI_BASELINE_GAS, parameters::FCI_BASELINE_RICE};
}

Basket doubledPrices() {
  Basket prices = baselinePrices();
  for (auto& price : prices) price *= 2;
  return prices;
}

// A basket where only the listed categories carry a price. The caller pairs
// this with a mask containing exactly those bits.
Basket onlyAt(std::initializer_list<std::pair<size_t, uint32_t>> entries) {
  Basket prices{};
  for (const auto& entry : entries) prices[entry.first] = entry.second;
  return prices;
}

void addRepeatedVotes(FuegoCostIndex& index, uint32_t count, uint8_t mask,
                      const Basket& prices) {
  const auto extra = makeVote(mask, prices);
  for (uint32_t vote = 0; vote < count; ++vote)
    EXPECT_TRUE(index.addCoinbaseVote(extra));
}

void addRepeatedVotes(FuegoCostIndex& index, uint32_t count,
                      const Basket& prices) {
  addRepeatedVotes(index, count, ALL, prices);
}

}  // namespace

TEST(FuegoCostIndexTest, StartsAtTheConfiguredBasketAndReference) {
  FuegoCostIndex index;

  EXPECT_EQ(parameters::FCI_INDEX_SCALE, index.indexCents());
  EXPECT_EQ(parameters::FCI_HEARTHSTONE_INITIAL_CENTS,
            index.hearthstoneCents());

  const auto tally = index.closeEpoch(1, BLOCK_MAJOR_VERSION_11,
                                      parameters::FCI_EPOCH_BLOCKS);
  EXPECT_EQ(parameters::FCI_INDEX_SCALE, tally.indexCents);
  EXPECT_EQ(parameters::FCI_HEARTHSTONE_INITIAL_CENTS,
            tally.hearthstoneCents);
}

TEST(FuegoCostIndexTest, PricesAndReferenceAreIntegerCents) {
  FuegoCostIndex index;

  // 399 is $3.99. The basket and the reference must both live on that grid,
  // so a vote can never carry a fractional cent into consensus.
  EXPECT_EQ(parameters::FCI_BASELINE_MILK, 390U);
  EXPECT_EQ(parameters::FCI_BASELINE_GAS, 350U);
  EXPECT_EQ(parameters::FCI_BASELINE_EGGS, 300U);
  EXPECT_EQ(parameters::FCI_BASELINE_BREAD, 250U);
  EXPECT_EQ(parameters::FCI_BASELINE_RICE, 250U);
  EXPECT_EQ(parameters::FCI_HEARTHSTONE_INITIAL_CENTS, 158U);
}

TEST(FuegoCostIndexTest, WeightsCoverSixCategoriesAndTotalOneHundred) {
  EXPECT_EQ(6u, FuegoCostIndex::COMMODITY_COUNT);
  EXPECT_EQ(100u, parameters::FCI_WEIGHT_POWER +
                      parameters::FCI_WEIGHT_GAS +
                      parameters::FCI_WEIGHT_RICE +
                      parameters::FCI_WEIGHT_MILK +
                      parameters::FCI_WEIGHT_BREAD +
                      parameters::FCI_WEIGHT_EGGS);
  EXPECT_EQ(3u, parameters::FCI_MIN_FRESH_QUORUM);
}

TEST(FuegoCostIndexTest, ScalesQuorumToTheNetworkEpochLength) {
  EXPECT_EQ(351, FuegoCostIndex::minimumQuorumForEpoch(900));
  EXPECT_EQ(4, FuegoCostIndex::minimumQuorumForEpoch(10));
  EXPECT_EQ(0, FuegoCostIndex::minimumQuorumForEpoch(0));
}

TEST(FuegoCostIndexTest, TrimsOutliersAndCarriesCategoriesWithoutQuorum) {
  FuegoCostIndex index;
  for (uint32_t price : {10u, 20u, 30u, 40u, 100u}) {
    const auto extra = makeVote(1u, onlyAt({{POWER, price}}));
    ASSERT_TRUE(index.addCoinbaseVote(extra));
  }

  const auto tally = index.closeEpoch(1, BLOCK_MAJOR_VERSION_11, 10);
  EXPECT_EQ(5, tally.voteCounts[POWER]);
  EXPECT_EQ(30, tally.medianPrices[POWER]);
  EXPECT_EQ(parameters::FCI_BASELINE_MILK, tally.medianPrices[MILK]);
  EXPECT_EQ(parameters::FCI_BASELINE_GAS, tally.medianPrices[GAS]);
  EXPECT_EQ(parameters::FCI_BASELINE_RICE, tally.medianPrices[RICE]);
}

TEST(FuegoCostIndexTest, RemovesTheLastVoteDuringAnIntraEpochRollback) {
  FuegoCostIndex index;
  const auto earlierVote = makeVote(1u, onlyAt({{POWER, 100}}));
  const auto lastVote = makeVote(1u, onlyAt({{POWER, 200}}));
  ASSERT_TRUE(index.addCoinbaseVote(earlierVote));
  ASSERT_TRUE(index.addCoinbaseVote(lastVote));

  EXPECT_TRUE(index.removeCoinbaseVote(lastVote));
  const auto tally = index.closeEpoch(1, BLOCK_MAJOR_VERSION_11, 10);
  EXPECT_EQ(1, tally.voteCounts[POWER]);
}

TEST(FuegoCostIndexTest, IgnoresDuplicateUnknownAndZeroValuedVotes) {
  FuegoCostIndex index;
  const Basket prices = onlyAt({{POWER, 120}});
  auto duplicate = makeVote(1u, prices);
  ASSERT_TRUE(addMinerBasketVoteToExtra(
      duplicate,
      TransactionExtraMinerBasketVote{1u, 120, 0, 0, 0, 0, 0}));
  EXPECT_FALSE(index.addCoinbaseVote(duplicate));

  // Bit 6 is outside the six-category basket.
  EXPECT_FALSE(index.addCoinbaseVote(makeVote(0x40u, prices)));
  EXPECT_FALSE(index.addCoinbaseVote(makeVote(1u, Basket{})));

  const auto tally = index.closeEpoch(1, BLOCK_MAJOR_VERSION_11, 10);
  EXPECT_EQ(0, tally.voteCounts[POWER]);
  EXPECT_EQ(parameters::FCI_BASELINE_POWER, tally.medianPrices[POWER]);
}

// Rice was appended as mask bit 5 so that every five-category vote authored
// before the upgrade still serializes to the identical byte string. If this
// ever fails, old coinbases stop parsing and the chain splits.
TEST(FuegoCostIndexTest, FiveCategoryVotesDoNotCarryRiceOnTheWire) {
  const Basket prices = baselinePrices();

  TransactionExtraMinerBasketVote withoutRice{};
  withoutRice.present_mask = PRE_RICE;
  withoutRice.power = prices[POWER];
  withoutRice.milk = prices[MILK];
  withoutRice.bread = prices[BREAD];
  withoutRice.eggs = prices[EGGS];
  withoutRice.gas = prices[GAS];

  TransactionExtraMinerBasketVote withRice = withoutRice;
  withRice.rice = 99999;

  std::vector<uint8_t> encodedWithout;
  std::vector<uint8_t> encodedWith;
  ASSERT_TRUE(addMinerBasketVoteToExtra(encodedWithout, withoutRice));
  ASSERT_TRUE(addMinerBasketVoteToExtra(encodedWith, withRice));
  EXPECT_EQ(encodedWithout, encodedWith);
}

TEST(FuegoCostIndexTest, TalliesBeforeV12WithoutChangingTheReference) {
  FuegoCostIndex index;
  addRepeatedVotes(index, 4, doubledPrices());

  const auto tally = index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER,
                                      BLOCK_MAJOR_VERSION_11, 10);
  EXPECT_EQ(parameters::FCI_INDEX_SCALE * 2, tally.indexCents);
  EXPECT_FALSE(tally.referenceUpdated);
  EXPECT_EQ(parameters::FCI_HEARTHSTONE_INITIAL_CENTS,
            tally.hearthstoneCents);
}

TEST(FuegoCostIndexTest, ChangesTheReferenceOnlyAtV12QuarterBoundaries) {
  FuegoCostIndex index;
  addRepeatedVotes(index, 4, doubledPrices());
  auto tally = index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER - 1,
                                BLOCK_MAJOR_VERSION_12, 10);
  EXPECT_FALSE(tally.referenceUpdated);
  EXPECT_EQ(parameters::FCI_HEARTHSTONE_INITIAL_CENTS,
            tally.hearthstoneCents);

  addRepeatedVotes(index, 4, doubledPrices());
  tally = index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER,
                           BLOCK_MAJOR_VERSION_12, 10);
  EXPECT_EQ(504u, parameters::FCI_MAX_QUARTERLY_DRIFT_BPS);
  const uint64_t allowedChange =
      parameters::FCI_HEARTHSTONE_INITIAL_CENTS *
      parameters::FCI_MAX_QUARTERLY_DRIFT_BPS / 10000;
  EXPECT_TRUE(tally.referenceUpdated);
  EXPECT_EQ(parameters::FCI_HEARTHSTONE_INITIAL_CENTS + allowedChange,
            tally.hearthstoneCents);

  addRepeatedVotes(index, 4, doubledPrices());
  tally = index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER + 1,
                           BLOCK_MAJOR_VERSION_12, 10);
  EXPECT_FALSE(tally.referenceUpdated);
  EXPECT_EQ(parameters::FCI_HEARTHSTONE_INITIAL_CENTS + allowedChange,
            tally.hearthstoneCents);
}

// The quarterly gate needs three categories that actually cleared quorum in
// the trailing quarter. Two is not enough.
TEST(FuegoCostIndexTest, TwoFreshCategoriesStayBelowTheQuarterlyGate) {
  FuegoCostIndex index;

  addRepeatedVotes(index, 4, 0x11u,
                   onlyAt({{POWER, parameters::FCI_BASELINE_POWER * 2},
                           {GAS, parameters::FCI_BASELINE_GAS * 2}}));
  index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER - 1,
                   BLOCK_MAJOR_VERSION_12, 10);

  const auto tally = index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER,
                                      BLOCK_MAJOR_VERSION_12, 10);
  EXPECT_EQ(2u, parameters::FCI_MIN_FRESH_QUORUM - 1);
  EXPECT_FALSE(tally.referenceUpdated);
  EXPECT_EQ(parameters::FCI_HEARTHSTONE_INITIAL_CENTS,
            tally.hearthstoneCents);
}

TEST(FuegoCostIndexTest, ThreeFreshCategoriesOpenTheQuarterlyGate) {
  FuegoCostIndex index;

  addRepeatedVotes(index, 4, 0x13u,
                   onlyAt({{POWER, parameters::FCI_BASELINE_POWER * 2},
                           {MILK, parameters::FCI_BASELINE_MILK * 2},
                           {GAS, parameters::FCI_BASELINE_GAS * 2}}));
  index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER - 1,
                   BLOCK_MAJOR_VERSION_12, 10);

  const auto tally = index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER,
                                      BLOCK_MAJOR_VERSION_12, 10);
  EXPECT_TRUE(tally.referenceUpdated);
  EXPECT_GT(tally.hearthstoneCents,
            parameters::FCI_HEARTHSTONE_INITIAL_CENTS);
}

// A category that stopped reporting must age out of the window rather than
// count forever.
TEST(FuegoCostIndexTest, CategoriesThatStaleOutBothDropOutOfTheGate) {
  FuegoCostIndex index;

  addRepeatedVotes(index, 4, ALL, doubledPrices());
  index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER - 1,
                   BLOCK_MAJOR_VERSION_12, 10);

  // Two quarters later every category last cleared quorum at epoch 17, which
  // is outside the trailing window, so the gate is closed.
  const auto tally = index.closeEpoch(
      2 * parameters::FCI_EPOCHS_PER_QUARTER, BLOCK_MAJOR_VERSION_12, 10);
  EXPECT_FALSE(tally.referenceUpdated);
}

TEST(FuegoCostIndexTest, FreezesTheReferenceWhenTheQuarterHasNoFreshBasket) {
  FuegoCostIndex index;
  addRepeatedVotes(index, 4, doubledPrices());
  index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER,
                   BLOCK_MAJOR_VERSION_12, 10);
  const uint64_t firstQuarterReference = index.hearthstoneCents();

  const auto staleTally = index.closeEpoch(
      2 * parameters::FCI_EPOCHS_PER_QUARTER,
      BLOCK_MAJOR_VERSION_12, 10);
  EXPECT_FALSE(staleTally.referenceUpdated);
  EXPECT_EQ(firstQuarterReference, staleTally.hearthstoneCents);
}

TEST(FuegoCostIndexTest, CapsDownwardReferenceMovementAt504BasisPoints) {
  FuegoCostIndex index;
  Basket lowerPrices = baselinePrices();
  for (auto& price : lowerPrices) price /= 2;
  addRepeatedVotes(index, 4, lowerPrices);

  const auto tally = index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER,
                                      BLOCK_MAJOR_VERSION_12, 10);
  EXPECT_EQ(parameters::FCI_INDEX_SCALE / 2, tally.indexCents);
  EXPECT_TRUE(tally.referenceUpdated);

  const uint64_t allowedChange =
      parameters::FCI_HEARTHSTONE_INITIAL_CENTS *
      parameters::FCI_MAX_QUARTERLY_DRIFT_BPS / 10000;
  EXPECT_EQ(parameters::FCI_HEARTHSTONE_INITIAL_CENTS - allowedChange,
            tally.hearthstoneCents);
}

TEST(FuegoCostIndexTest, SnapshotRestoreReturnsThePriorEpochState) {
  FuegoCostIndex index;
  const auto beforeEpoch = index.snapshot();
  addRepeatedVotes(index, 4, doubledPrices());
  index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER,
                   BLOCK_MAJOR_VERSION_12, 10);

  index.restore(beforeEpoch);
  EXPECT_EQ(parameters::FCI_INDEX_SCALE, index.indexCents());
  EXPECT_EQ(parameters::FCI_HEARTHSTONE_INITIAL_CENTS,
            index.hearthstoneCents());
}

// The quarterly gate reads per-category last-quorum epochs, so a reorg that
// rewinds an epoch boundary has to rewind that history too. Otherwise a
// replayed boundary would count categories from the abandoned branch.
TEST(FuegoCostIndexTest, SnapshotRestoreAlsoRewindsTheQuorumWindow) {
  FuegoCostIndex index;

  addRepeatedVotes(index, 4, 0x11u,
                   onlyAt({{POWER, parameters::FCI_BASELINE_POWER * 2},
                           {GAS, parameters::FCI_BASELINE_GAS * 2}}));
  index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER - 1,
                   BLOCK_MAJOR_VERSION_12, 10);

  const auto beforeBoundary = index.snapshot();
  auto tallied = index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER,
                                  BLOCK_MAJOR_VERSION_12, 10);
  EXPECT_FALSE(tallied.referenceUpdated);

  // Restore, then let a third category report inside the same window. The
  // gate must see three fresh categories, which can only happen if the two
  // earlier ones survive the restore.
  index.restore(beforeBoundary);
  addRepeatedVotes(index, 4, 0x04u,
                   onlyAt({{BREAD, parameters::FCI_BASELINE_BREAD * 2}}));
  index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER - 1,
                   BLOCK_MAJOR_VERSION_12, 10);

  tallied = index.closeEpoch(parameters::FCI_EPOCHS_PER_QUARTER,
                             BLOCK_MAJOR_VERSION_12, 10);
  EXPECT_TRUE(tallied.referenceUpdated);
}
