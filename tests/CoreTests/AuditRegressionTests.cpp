// Copyright (c) 2026 Fuego Developers
//
// Regression tests for the HEAT / Hearth / CD / atomic-swap audit remediation.
//
// These exercise the production code paths directly. The settlement pre-check
// and the Hearth seed tests construct a real Blockchain + tx_memory_pool pair
// (wired the same way core::core wires them), so they run the shipped
// implementation rather than a copy of its logic.
//
//  - Blockchain::checkTransactionSettlement: rejects only transactions that
//    block validation is certain to reject, and never an unknown limit orderId.
//  - Hearth seed: the constructor mints a locked LP share supply for the seed,
//    and does not credit it to the protocol (which would mark the bootstrap
//    debt repaid before any revenue existed).
//  - ammMintLpShares: refuses a zero-supply mint against non-empty reserves
//    (the first-LP seed-capture state).
//  - Constant-product pricing: a swap the size of the reserve pays ~half the
//    opposite reserve, not 99% as the old linear spot pricing did, and the
//    settlement fee split keeps the invariant.

#include "CryptoNoteCore/AmmPool.h"
#include "CryptoNoteCore/Blockchain.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/ITimeProvider.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "CryptoNoteCore/TransactionPool.h"
#include "CryptoNoteConfig.h"
#include "Common/Int128.h"
#include "Logging/LoggerGroup.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace CryptoNote;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
  tests_run++; \
  if (!(name)) { \
    fprintf(stderr, "FAIL: %s (%s:%d)\n", #name, __FILE__, __LINE__); \
  } else { \
    tests_passed++; \
  } \
} while(0)

namespace {

// Blockchain and tx_memory_pool hold references to each other; core::core
// resolves this through member declaration order, and so does this fixture.
// Neither constructor dereferences the other, so binding early is safe.
struct ChainFixture {
  Logging::LoggerGroup log;
  Currency currency;
  RealTimeProvider timeProvider;
  tx_memory_pool pool;
  Blockchain chain;

  ChainFixture()
    : currency(CurrencyBuilder(log).currency()),
      pool(currency, chain, timeProvider, log),
      chain(currency, pool, log, false, false) {}
};

Transaction txWithExtra(const std::vector<uint8_t>& extra) {
  Transaction tx;
  tx.version = 1;
  tx.unlockTime = 0;
  tx.extra = extra;
  return tx;
}

Crypto::Hash hashOf(uint8_t seed) {
  Crypto::Hash h{};
  for (size_t i = 0; i < sizeof(h.data); ++i) h.data[i] = static_cast<uint8_t>(seed + i);
  return h;
}

bool prechecks(Blockchain& chain, const Transaction& tx, std::string* reasonOut = nullptr) {
  std::string reason;
  const bool ok = chain.checkTransactionSettlement(tx, reason);
  if (reasonOut) *reasonOut = reason;
  return ok;
}

}  // namespace

// ── Settlement pre-check ────────────────────────────────────────────────────

void testPrecheckAllowsPlainTransaction() {
  ChainFixture f;
  TEST(prechecks(f.chain, txWithExtra({})));
}

void testPrecheckAllowsSingleSettlementClass() {
  ChainFixture f;
  std::vector<uint8_t> extra;
  TEST(addHeatMintAuthToExtra(extra, 1000 * parameters::COIN, 100 * parameters::COIN));
  // A lone mint auth is not something the pre-check can rule on; pushBlock does.
  TEST(prechecks(f.chain, txWithExtra(extra)));
}

void testPrecheckRejectsMultipleSettlementClasses() {
  ChainFixture f;
  std::vector<uint8_t> extra;
  TEST(addHeatMintAuthToExtra(extra, 1000 * parameters::COIN, 100 * parameters::COIN));
  TEST(addAmmSwapAuthToExtra(extra, 0, 10 * parameters::COIN, 1 * parameters::COIN, 0));
  std::string reason;
  TEST(!prechecks(f.chain, txWithExtra(extra), &reason));
  TEST(reason.find("multiple settlement") != std::string::npos);
}

void testPrecheckRejectsLpAddAndRemoveTogether() {
  ChainFixture f;
  std::vector<uint8_t> extra;
  TEST(addLpAddAuthToExtra(extra, 10 * parameters::COIN, 1 * parameters::COIN, 1000));
  TEST(addLpRemoveAuthToExtra(extra, 1000, 0, 0));
  std::string reason;
  TEST(!prechecks(f.chain, txWithExtra(extra), &reason));
  TEST(reason.find("LP add and remove") != std::string::npos);
}

void testPrecheckRejectsDuplicateLimitDeposits() {
  ChainFixture f;
  std::vector<uint8_t> extra;
  TEST(addLimitDepositToExtra(extra, 1, parameters::COIN, parameters::ORDER_PRICE_TICK, 0,
                              hashOf(1), hashOf(2)));
  TEST(addLimitDepositToExtra(extra, 1, parameters::COIN, parameters::ORDER_PRICE_TICK, 0,
                              hashOf(3), hashOf(4)));
  TEST(!prechecks(f.chain, txWithExtra(extra)));
}

void testPrecheckRejectsOffTickPrice() {
  ChainFixture f;
  std::vector<uint8_t> extra;
  TEST(addLimitDepositToExtra(extra, 1, parameters::COIN, parameters::ORDER_PRICE_TICK + 1, 0,
                              hashOf(1), hashOf(2)));
  std::string reason;
  TEST(!prechecks(f.chain, txWithExtra(extra), &reason));
  TEST(reason.find("tick") != std::string::npos);
}

void testPrecheckRejectsZeroPrice() {
  ChainFixture f;
  std::vector<uint8_t> extra;
  TEST(addLimitDepositToExtra(extra, 1, parameters::COIN, 0, 0, hashOf(1), hashOf(2)));
  TEST(!prechecks(f.chain, txWithExtra(extra)));
}

void testPrecheckAllowsOnTickPrice() {
  ChainFixture f;
  std::vector<uint8_t> extra;
  TEST(addLimitDepositToExtra(extra, 1, parameters::COIN, 3 * parameters::ORDER_PRICE_TICK, 0,
                              hashOf(1), hashOf(2)));
  TEST(prechecks(f.chain, txWithExtra(extra)));
}

void testPrecheckRejectsNullWithdrawOrderId() {
  ChainFixture f;
  std::vector<uint8_t> extra;
  TEST(addLimitWithdrawToExtra(extra, NULL_HASH, Crypto::PublicKey{}, Crypto::PublicKey{},
                               NULL_HASH, Crypto::Signature{}));
  TEST(!prechecks(f.chain, txWithExtra(extra)));
}

// The no-false-positive guarantee: a withdraw for an order the node has not
// seen yet (its deposit may still be in flight) must NOT be rejected — only
// states that can never become valid again may be.
void testPrecheckAllowsUnknownWithdrawOrderId() {
  ChainFixture f;
  std::vector<uint8_t> extra;
  TEST(addLimitWithdrawToExtra(extra, hashOf(9), Crypto::PublicKey{}, Crypto::PublicKey{},
                               hashOf(10), Crypto::Signature{}));
  TEST(prechecks(f.chain, txWithExtra(extra)));
}

// ── Hearth seed ─────────────────────────────────────────────────────────────

void testSeedCarriesLockedLpShares() {
  ChainFixture f;
  const AmmPoolState& pool = f.chain.getAmmPool();
  TEST(pool.reserveXfg == parameters::HEARTH_POOL_SEED_XFG * parameters::COIN);
  TEST(pool.reserveHeat == parameters::HEARTH_POOL_SEED_HEAT * parameters::COIN);
  // Non-zero supply: later deposits price pro-rata against real reserves.
  TEST(pool.totalLpShares == ammMintLpShares(pool.reserveXfg, pool.reserveHeat, 0, 0, 0));
  TEST(pool.totalLpShares > 0);
  // Unowned: crediting them to the protocol would make getTreasuryLpValue()
  // equal the seed and mark the bootstrap debt repaid immediately.
  TEST(f.chain.getProtocolLpShares() == 0);
}

// With the seed's shares in supply, a tiny first deposit gets a tiny,
// proportional share — it can no longer capture the reserve.
void testFirstDepositCannotCaptureSeed() {
  ChainFixture f;
  const AmmPoolState& pool = f.chain.getAmmPool();
  const uint64_t depX = 10 * parameters::COIN;   // 10 XFG
  const uint64_t depH = 1 * parameters::COIN;    //  1 HEAT (10:1 ratio)
  const uint64_t shares = ammMintLpShares(depX, depH, pool.totalLpShares,
                                          pool.reserveXfg, pool.reserveHeat);
  uint64_t outX = 0, outH = 0;
  ammGetWithdrawalAmounts(shares, pool.totalLpShares + shares,
                          pool.reserveXfg + depX, pool.reserveHeat + depH, outX, outH);
  // Withdrawing everything returns (at most) what was deposited.
  TEST(outX <= depX);
  TEST(outH <= depH);
}

// ── First-LP guard ──────────────────────────────────────────────────────────

void testZeroSupplyMintAgainstNonEmptyReservesRefused() {
  const uint64_t rX = 10000 * parameters::COIN, rH = 1000 * parameters::COIN;
  // The exact state the exploit used: supply 0, reserves non-zero.
  TEST(ammMintLpShares(3200, 320, 0, rX, rH) == 0);
  TEST(ammMintLpShares(10 * parameters::COIN, 1 * parameters::COIN, 0, rX, rH) == 0);
}

void testGenesisMintStillWorks() {
  const uint64_t x = 10000 * parameters::COIN, y = 1000 * parameters::COIN;
  const uint64_t shares = ammMintLpShares(x, y, 0, 0, 0);
  TEST(shares > 0);
}

// ── Constant-product pricing ────────────────────────────────────────────────

// The headline audit finding: a swap equal to the XFG reserve used to extract
// 99% of the HEAT reserve under linear spot pricing. On the curve it is ~50%.
void testReserveSizedSwapPaysHalfNotNinetyNine() {
  const uint64_t rX = 10000 * parameters::COIN, rH = 1000 * parameters::COIN;
  const uint64_t gross = ammGetOutputAmount(rX, rX, rH, 0);
  // x*y=k: out = rH * rX / (rX + rX) = rH / 2.
  TEST(gross <= rH / 2);
  TEST(gross >= rH / 2 - 1);
  const uint64_t linearNet = static_cast<uint64_t>(
      ((uint128_t)rH * (parameters::HEARTH_FEE_DIVISOR - parameters::HEARTH_FEE_BPS))
        / parameters::HEARTH_FEE_DIVISOR);
  TEST(gross < linearNet);  // strictly below the old 99% payout
}

// Settlement removes (net + 70% of the fee) from the out-side reserve. The
// post-swap product must stay at or above the pre-swap product.
void testSettlementFeeSplitKeepsInvariant() {
  const uint64_t feeDiv = parameters::HEARTH_FEE_DIVISOR;
  const uint64_t feeBps = parameters::HEARTH_FEE_BPS;
  const uint64_t rX = 10000 * parameters::COIN, rH = 1000 * parameters::COIN;
  const uint64_t sizes[] = {1, 1000, parameters::COIN, 100 * parameters::COIN,
                            rX / 10, rX, 5 * rX};
  bool allHold = true;
  for (uint64_t in : sizes) {
    const uint64_t gross = ammGetOutputAmount(in, rX, rH, 0);
    const uint64_t net = static_cast<uint64_t>(((uint128_t)gross * (feeDiv - feeBps)) / feeDiv);
    const uint64_t fee = gross - net;
    const uint64_t cdFee = static_cast<uint64_t>(
        ((uint128_t)fee * parameters::HEARTH_CD_SHARE_PCT) / 100);
    if (net + cdFee > rH) { allHold = false; continue; }
    if (!ammValidateInvariant(rX, rH, rX + in, rH - (net + cdFee))) allHold = false;
  }
  TEST(allHold);
}

// The BUY backstop walks the curve backwards when the per-block allowance
// binds; the resulting output must never exceed the allowance.
void testInverseCurveNeverExceedsAllowance() {
  const uint64_t rX = 10000 * parameters::COIN, rH = 1000 * parameters::COIN;
  const uint64_t allowances[] = {parameters::COIN, 50 * parameters::COIN, rX / 20, rX / 2};
  bool ok = true;
  for (uint64_t allowance : allowances) {
    const uint64_t heatIn = ammGetInputAmount(allowance, rH, rX, 0);
    if (heatIn == 0) { ok = false; continue; }
    const uint64_t out = ammGetOutputAmount(heatIn, rH, rX, 0);
    if (out > allowance) ok = false;
  }
  TEST(ok);
}

int main() {
  testPrecheckAllowsPlainTransaction();
  testPrecheckAllowsSingleSettlementClass();
  testPrecheckRejectsMultipleSettlementClasses();
  testPrecheckRejectsLpAddAndRemoveTogether();
  testPrecheckRejectsDuplicateLimitDeposits();
  testPrecheckRejectsOffTickPrice();
  testPrecheckRejectsZeroPrice();
  testPrecheckAllowsOnTickPrice();
  testPrecheckRejectsNullWithdrawOrderId();
  testPrecheckAllowsUnknownWithdrawOrderId();
  testSeedCarriesLockedLpShares();
  testFirstDepositCannotCaptureSeed();
  testZeroSupplyMintAgainstNonEmptyReservesRefused();
  testGenesisMintStillWorks();
  testReserveSizedSwapPaysHalfNotNinetyNine();
  testSettlementFeeSplitKeepsInvariant();
  testInverseCurveNeverExceedsAllowance();
  fprintf(stderr, "=== Audit Regression Tests ===\nPassed: %d / %d\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
