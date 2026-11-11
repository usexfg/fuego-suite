// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2014-2016 SDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gtest/gtest.h"

#include <CryptoNoteCore/BankingIndex.h>

using namespace CryptoNote;

class BankingIndexTest : public ::testing::Test {
public:
  const std::size_t DEFAULT_HEIGHT = 10;
  BankingIndexTest() : index(static_cast<BankingIndex::DepositHeight>(DEFAULT_HEIGHT)) {
  }
  BankingIndex index;
};

TEST_F(BankingIndexTest, EmptyAfterCreate) {
  ASSERT_EQ(0, index.fullDepositAmount());
  ASSERT_EQ(0, index.fullInterestAmount());
}

TEST_F(BankingIndexTest, AddBlockUpdatesGlobalAmount) {
  index.pushBlock(10, 1);
  ASSERT_EQ(10, index.fullDepositAmount());
}

TEST_F(BankingIndexTest, AddBlockUpdatesFullInterest) {
  index.pushBlock(10, 1);
  ASSERT_EQ(1, index.fullInterestAmount());
}

TEST_F(BankingIndexTest, GlobalAmountIsSumOfBlockDeposits) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 1);
  ASSERT_EQ(9 + 12, index.fullDepositAmount());
}

TEST_F(BankingIndexTest, AddEmptyBlockDoesntChangeAmount) {
  index.pushBlock(9, 1);
  index.pushBlock(0, 0);
  ASSERT_EQ(9, index.fullDepositAmount());
}

TEST_F(BankingIndexTest, AddEmptyBlockDoesntChangeInterest) {
  index.pushBlock(9, 1);
  index.pushBlock(0, 0);
  ASSERT_EQ(1, index.fullInterestAmount());
}

TEST_F(BankingIndexTest, FirstBlockPushUpdatesDepositAmountAtHeight0) {
  index.pushBlock(9, 1);
  ASSERT_EQ(9, index.depositAmountAtHeight(0));
}

TEST_F(BankingIndexTest, FirstBlockPushUpdatesDepositInterestAtHeight0) {
  index.pushBlock(9, 1);
  ASSERT_EQ(1, index.depositInterestAtHeight(0));
}

TEST_F(BankingIndexTest, FullDepositAmountEqualsDepositAmountAtLastHeight) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 2);
  ASSERT_EQ(index.fullDepositAmount(), index.depositAmountAtHeight(index.size() - 1));
}

TEST_F(BankingIndexTest, FullInterestAmountEqualsDepositInterestAtLastHeight) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 2);
  ASSERT_EQ(index.fullInterestAmount(), index.depositInterestAtHeight(index.size() - 1));
}

TEST_F(BankingIndexTest, FullDepositAmountEqualsDepositAmountAtHeightGreaterThanLastHeight) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 2);
  ASSERT_EQ(index.fullDepositAmount(), index.depositAmountAtHeight(index.size()));
}

TEST_F(BankingIndexTest, FullInterestAmountEqualsInterestAmountAtHeightGreaterThanLastHeight) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 2);
  ASSERT_EQ(index.fullInterestAmount(), index.depositInterestAtHeight(index.size()));
}

TEST_F(BankingIndexTest, RemoveReducesGlobalAmount) {
  index.pushBlock(12, 1);
  index.popBlock();
  ASSERT_EQ(0, index.fullDepositAmount());
}

TEST_F(BankingIndexTest, AddEmptyBlockIncrementsSize) {
  index.pushBlock(0, 0);
  ASSERT_EQ(1, index.size());
  index.pushBlock(0, 0);
  ASSERT_EQ(2, index.size());
}

TEST_F(BankingIndexTest, PopEmptyBlockDecrementsSize) {
  index.pushBlock(0, 0);
  index.popBlock();
  ASSERT_EQ(0, index.size());
}

TEST_F(BankingIndexTest, AddNonEmptyBlockIncrementsSize) {
  index.pushBlock(9, 1);
  ASSERT_EQ(1, index.size());
  index.pushBlock(12, 1);
  ASSERT_EQ(2, index.size());
}

TEST_F(BankingIndexTest, PopNonEmptyBlockDecrementsSize) {
  index.pushBlock(9, 1);
  index.popBlock();
  ASSERT_EQ(0, index.size());
}

TEST_F(BankingIndexTest, PopLastEmptyBlockDoesNotChangeFullDepositAmount) {
  index.pushBlock(9, 1);
  index.pushBlock(0, 0);
  index.popBlock();
  ASSERT_EQ(9, index.fullDepositAmount());
}

TEST_F(BankingIndexTest, PopLastEmptyBlockDoesNotChangeFullInterestAmount) {
  index.pushBlock(9, 1);
  index.pushBlock(0, 0);
  index.popBlock();
  ASSERT_EQ(1, index.fullInterestAmount());
}

TEST_F(BankingIndexTest, MultipleRemovals) {
  index.pushBlock(10, 1);
  index.pushBlock(0, 0);
  index.pushBlock(11, 1);
  index.pushBlock(0, 0);
  index.pushBlock(12, 1);
  ASSERT_EQ(5, index.popBlocks(0));
  ASSERT_EQ(0, index.fullDepositAmount());
}

TEST_F(BankingIndexTest, MultipleRemovalsDecrementSize) {
  index.pushBlock(10, 1);
  index.pushBlock(11, 1);
  index.pushBlock(0, 0);
  index.pushBlock(12, 1);
  ASSERT_EQ(1, index.popBlocks(3));
  ASSERT_EQ(4 - 1, index.size());
}

TEST_F(BankingIndexTest, PopBlockReducesFullAmount) {
  index.pushBlock(10, 1);
  index.pushBlock(12, 1);
  index.popBlock();
  ASSERT_EQ(10, index.fullDepositAmount());
}

TEST_F(BankingIndexTest, PopBlockDecrementsSize) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 1);

  auto size = index.size();
  index.popBlock();
  ASSERT_EQ(size - 1, index.size());
}

TEST_F(BankingIndexTest, DepositAmountAtAnyHeightIsZeroAfterCreation) {
  ASSERT_EQ(0, index.depositAmountAtHeight(10));
}

TEST_F(BankingIndexTest, DepositInterestAtAnyHeightIsZeroAfterCreation) {
  ASSERT_EQ(0, index.depositInterestAtHeight(10));
}

TEST_F(BankingIndexTest, DepositAmountIsZeroAtAnyHeightBeforeFirstDeposit) {
  index.pushBlock(0, 0);
  index.pushBlock(9, 1);
  ASSERT_EQ(0, index.depositAmountAtHeight(0));
}

TEST_F(BankingIndexTest, DepositInterestIsZeroAtAnyHeightBeforeFirstDeposit) {
  index.pushBlock(0, 0);
  index.pushBlock(9, 1);
  ASSERT_EQ(0, index.depositInterestAtHeight(0));
}

TEST_F(BankingIndexTest, DepositAmountAtHeightInTheMiddle) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 1);
  index.pushBlock(14, 1);
  ASSERT_EQ(9 + 12, index.depositAmountAtHeight(1));
}

TEST_F(BankingIndexTest, MaxAmountIsReturnedForHeightLargerThanLastBlock) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 1);
  index.pushBlock(14, 1);
  ASSERT_EQ(index.depositAmountAtHeight(20), index.fullDepositAmount());
}

TEST_F(BankingIndexTest, DepositAmountAtHeightInTheMiddleLooksForLowerBound) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 1);
  index.pushBlock(14, 1);
  index.pushBlock(7, 1);
  ASSERT_EQ(9 + 12 + 14, index.depositAmountAtHeight(2));
}

TEST_F(BankingIndexTest, DepositAmountAtHeightInTheMiddleIgnoresEmptyBlocks) {
  index.pushBlock(9, 1);
  index.pushBlock(0, 0);
  index.pushBlock(12, 1);
  index.pushBlock(0, 0);
  index.pushBlock(14, 1);
  index.pushBlock(0, 0);
  index.pushBlock(7, 1);
  ASSERT_EQ(9 + 12, index.depositAmountAtHeight(3));
}

TEST_F(BankingIndexTest, MultiPopZeroChangesNothing) {
  ASSERT_EQ(0, index.popBlocks(0));
  ASSERT_EQ(0, index.depositAmountAtHeight(0));
}

TEST_F(BankingIndexTest, DepositAmountAtNonExistingHeight) {
  ASSERT_EQ(0, index.depositAmountAtHeight(4));
}

TEST_F(BankingIndexTest, MultiPopZeroClearsIndex) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 1);
  index.pushBlock(14, 1);
  ASSERT_EQ(3, index.popBlocks(0));
  ASSERT_EQ(0, index.depositAmountAtHeight(0));
}

TEST_F(BankingIndexTest, GetInterestOnHeight) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 1);
  index.pushBlock(14, 1);
  ASSERT_EQ(3, index.depositInterestAtHeight(14));
}

TEST_F(BankingIndexTest, CanSubmitNegativeDeposit) {
  index.pushBlock(20, 1);
  index.pushBlock(-14, 1);
}

TEST_F(BankingIndexTest, DepositAmountCanBeReduced) {
  index.pushBlock(9, 1);
  index.pushBlock(12, 1);
  index.pushBlock(-14, 1);
  ASSERT_EQ(9 + 12 - 14, index.fullDepositAmount());
}

TEST_F(BankingIndexTest, PopBlocksZeroReturnsZero) {
  ASSERT_EQ(0, index.popBlocks(0));
}

TEST_F(BankingIndexTest, PopBlocksRemovesEmptyBlocks) {
  index.pushBlock(1, 1);
  index.pushBlock(0, 0);
  index.pushBlock(0, 0);
  ASSERT_EQ(2, index.popBlocks(1));
  ASSERT_EQ(1, index.size());
  ASSERT_EQ(1, index.fullDepositAmount());
  ASSERT_EQ(1, index.fullInterestAmount());
}
