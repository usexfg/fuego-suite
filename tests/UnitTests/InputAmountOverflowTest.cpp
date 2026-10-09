// Aggregate uint64 overflow in input-money accumulation.
//
// The defect under test: Currency::getTransactionAllInputsAmount accumulated
// input amounts with a bare `amount +=` across every input of a transaction.
// getTransactionInputAmount already refused its own two-operand add
// (amount + interest), so each input was individually valid, but the running
// total across inputs could still wrap past UINT64_MAX. A wrapped total is
// small, and every caller compares the total against the outputs to decide
// whether the transaction is funded -- so a wrapped total read as "barely
// funded" and the transaction passed with no real backing.
//
// get_inputs_money_amount had the same gap but already returned bool, so the
// check existed and simply never fired.
//
// Cases are deterministic: fixed amounts, no randomness, no clock.

#include <cstdint>
#include <limits>

#include "gtest/gtest.h"

#include "crypto/crypto.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteConfig.h"
#include "Logging/ConsoleLogger.h"

using namespace CryptoNote;

namespace {

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

class InputAmountOverflowTest : public testing::Test {
public:
  InputAmountOverflowTest() :
      builder(m_logger),
      currency(builder.depositMinTerm(1).depositMaxTerm(401).currency()) {}

  // A KeyInput contributes exactly its amount, with no interest and no height
  // dependence, so these cases stay pinned to pure arithmetic.
  void addKeyInput(uint64_t amount) {
    KeyInput in;
    in.amount = amount;
    in.outputIndexes = {0};
    tx.inputs.push_back(in);
  }

  Logging::ConsoleLogger m_logger;
  CurrencyBuilder builder;
  Currency currency;
  Transaction tx;
};

// The exact wrap: two inputs whose sum exceeds UINT64_MAX. Before the fix the
// accumulator wrapped to 1, which is a plausible-looking small total.
TEST_F(InputAmountOverflowTest, TwoKeyInputsWrapTheTotal) {
  addKeyInput(kMax);
  addKeyInput(2);

  uint64_t total = 0;
  EXPECT_FALSE(currency.getTransactionAllInputsAmountChecked(tx, 0, total))
      << "two inputs summing past UINT64_MAX must be refused";
}

// Same wrap reached through three inputs, so the failure is in the running
// total rather than a single pairwise add.
TEST_F(InputAmountOverflowTest, ThreeKeyInputsWrapTheTotal) {
  addKeyInput(kMax / 2 + 1);
  addKeyInput(kMax / 2 + 1);
  addKeyInput(2);

  uint64_t total = 0;
  EXPECT_FALSE(currency.getTransactionAllInputsAmountChecked(tx, 0, total));
}

// A total that exactly reaches UINT64_MAX is representable and must NOT be
// refused -- the guard has to accept the boundary, not just reject the
// obviously-huge cases.
TEST_F(InputAmountOverflowTest, TotalExactlyMaxIsAccepted) {
  addKeyInput(kMax - 1);
  addKeyInput(1);

  uint64_t total = 0;
  ASSERT_TRUE(currency.getTransactionAllInputsAmountChecked(tx, 0, total));
  EXPECT_EQ(total, kMax);
}

// One past the boundary is the smallest possible overflow, one unit over.
TEST_F(InputAmountOverflowTest, TotalOneOverMaxIsRefused) {
  addKeyInput(kMax - 1);
  addKeyInput(2);

  uint64_t total = 0;
  EXPECT_FALSE(currency.getTransactionAllInputsAmountChecked(tx, 0, total));
}

// The unchecked accessor is still used for reporting (BlockchainExplorer,
// RpcServer), so it must keep returning the wrapped value rather than
// changing behaviour for those callers. This pins the relationship the
// validation path now relies on.
TEST_F(InputAmountOverflowTest, CheckedAndUncheckedAgreeWhenNoOverflow) {
  addKeyInput(1000);
  addKeyInput(2000);
  addKeyInput(3000);

  uint64_t checked = 0;
  ASSERT_TRUE(currency.getTransactionAllInputsAmountChecked(tx, 0, checked));
  EXPECT_EQ(checked, 6000);
  EXPECT_EQ(currency.getTransactionAllInputsAmount(tx, 0), 6000);
}

// get_inputs_money_amount already returned bool for this; the test proves the
// bool is load-bearing rather than vestigial.
TEST_F(InputAmountOverflowTest, GetInputsMoneyAmountRefusesOverflow) {
  addKeyInput(kMax);
  addKeyInput(1);

  uint64_t money = 0;
  EXPECT_FALSE(get_inputs_money_amount(tx, money));
}

TEST_F(InputAmountOverflowTest, GetInputsMoneyAmountAcceptsExactMax) {
  addKeyInput(kMax - 1);
  addKeyInput(1);

  uint64_t money = 0;
  ASSERT_TRUE(get_inputs_money_amount(tx, money));
  EXPECT_EQ(money, kMax);
}

} // namespace
