// Copyright (c) 2017-2026 Fuego Developers
//
// Regression test for F-001: CD-interest fee-pool inflation via multiple
// TransactionInputCommitmentSpend inputs in a single transaction.
//
// Before the fix, checkCommitmentSpendInput() capped each input's
// claimedInterest at the live m_feePoolBalance, but every input of a
// transaction was validated against the same pre-connect snapshot. Two inputs
// each <= pool could pass while their sum exceeded the pool; the excess was
// minted into the outputs (getTransactionInputAmount adds claimedInterest to the
// input side of the conservation check) while the connect-time decrement was
// silently skipped — unbacked supply.
//
// The fix sums claimedInterest across all CommitmentSpend inputs
// (Currency::sumCommitmentClaimedInterest) and rejects the transaction when the
// aggregate exceeds the fee pool, before pushTransaction draws it down.
//
// NOTE: the tests/ tree is not currently wired into CMake (tests/CMakeLists.txt
// is an empty stub), so this file is not built by default. It mirrors the style
// of HeatMintTest.cpp and is ready to run once a CoreTests target is added.

#include "gtest/gtest.h"
#include <limits>
#include "CryptoNoteCore/Currency.h"
#include "Logging/ConsoleLogger.h"

using namespace CryptoNote;

namespace {

Logging::ConsoleLogger g_logger;

Currency makeCurrency() {
  return CurrencyBuilder(g_logger).currency();
}

TransactionInputCommitmentSpend makeSpend(uint64_t amount, uint64_t claimedInterest) {
  TransactionInputCommitmentSpend in;
  in.amount = amount;
  in.claimedInterest = claimedInterest;
  return in;
}

// Mirrors the binding check applied in Blockchain.cpp before pushTransaction:
// reject when the summed claimedInterest exceeds the fee pool (or overflows).
bool aggregateInterestWithinPool(const Currency& cur, const Transaction& tx, uint64_t feePool) {
  uint64_t total = 0;
  if (!cur.sumCommitmentClaimedInterest(tx, total)) return false; // overflow
  return total <= feePool;
}

} // namespace

TEST(FeePoolInterestCap, SumsAllCommitmentSpendInputs) {
  Currency cur = makeCurrency();
  Transaction tx;
  tx.inputs.push_back(makeSpend(1000, 30));
  tx.inputs.push_back(makeSpend(1000, 70));
  tx.inputs.push_back(KeyInput{}); // non-commitment input is ignored
  uint64_t total = 0;
  EXPECT_TRUE(cur.sumCommitmentClaimedInterest(tx, total));
  EXPECT_EQ(total, 100u);
}

TEST(FeePoolInterestCap, DetectsOverflow) {
  Currency cur = makeCurrency();
  Transaction tx;
  tx.inputs.push_back(makeSpend(1, std::numeric_limits<uint64_t>::max()));
  tx.inputs.push_back(makeSpend(1, 1));
  uint64_t total = 0;
  EXPECT_FALSE(cur.sumCommitmentClaimedInterest(tx, total));
}

// The exploit scenario: two inputs each individually <= pool, but whose sum
// exceeds it. The aggregate cap must reject the transaction.
TEST(FeePoolInterestCap, RejectsMultiInputOverPool) {
  Currency cur = makeCurrency();
  const uint64_t pool = 100;
  Transaction tx;
  tx.inputs.push_back(makeSpend(5000, 100)); // <= pool individually
  tx.inputs.push_back(makeSpend(5000, 100)); // <= pool individually
  EXPECT_FALSE(aggregateInterestWithinPool(cur, tx, pool)); // sum 200 > 100
}

TEST(FeePoolInterestCap, AllowsSingleInputWithinPool) {
  Currency cur = makeCurrency();
  const uint64_t pool = 100;
  Transaction tx;
  tx.inputs.push_back(makeSpend(5000, 100));
  EXPECT_TRUE(aggregateInterestWithinPool(cur, tx, pool));
}

TEST(FeePoolInterestCap, AllowsMultiInputWithinPool) {
  Currency cur = makeCurrency();
  const uint64_t pool = 100;
  Transaction tx;
  tx.inputs.push_back(makeSpend(5000, 40));
  tx.inputs.push_back(makeSpend(5000, 50));
  EXPECT_TRUE(aggregateInterestWithinPool(cur, tx, pool)); // sum 90 <= 100
}
