// Copyright (c) 2026 Fuego Developers
//
// Consensus-adjacent unit tests for the F0–F3 fixes:
//  - BankingIndex: EF bucket vs overall burn tally, per-height entries,
//    popBlock reversal, serialization roundtrip.
//  - 50/50 EF/SWF split consistency (dust ≤ 1 atomic).
//  - ammMintLpShares: no single-sided mints; balanced min-ratio.
//  - ammGetSpotPrice: canonical HEAT/XFG × COIN scale.
//  - TreasuryFund tx-extra tag: writer/reader roundtrip.

#include "CryptoNoteCore/BankingIndex.h"
#include "CryptoNoteCore/AmmPool.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "Common/Int128.h"
#include "Serialization/ISerializer.h"

#include <cassert>
#include <cstdio>
#include <cstring>
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

// Minimal in-memory serializer for roundtrip tests.
class MemorySerializer : public ISerializer {
public:
  std::string buffer;
  bool output;

  explicit MemorySerializer(bool out) : output(out), pos(0) {}

  SerializerType type() const override { return output ? SerializerType::OUTPUT : SerializerType::INPUT; }

  bool beginObject(Common::StringView) override { return true; }
  void endObject() override {}

  bool beginArray(size_t& size, Common::StringView) override {
    if (output) {
      uint64_t v = size;
      writeBytes(&v, sizeof(v));
    } else {
      uint64_t v = 0;
      if (!readBytes(&v, sizeof(v))) return false;
      size = (size_t)v;
    }
    return true;
  }
  void endArray() override {}

  bool operator()(uint8_t& v, Common::StringView) override { return scalar(v); }
  bool operator()(int16_t& v, Common::StringView) override { return scalar(v); }
  bool operator()(uint16_t& v, Common::StringView) override { return scalar(v); }
  bool operator()(int32_t& v, Common::StringView) override { return scalar(v); }
  bool operator()(uint32_t& v, Common::StringView) override { return scalar(v); }
  bool operator()(int64_t& v, Common::StringView) override { return scalar(v); }
  bool operator()(uint64_t& v, Common::StringView) override { return scalar(v); }
  bool operator()(double& v, Common::StringView) override { return scalar(v); }
  bool operator()(bool& v, Common::StringView) override {
    uint8_t b = v ? 1 : 0;
    if (!scalar(b)) return false;
    v = (b != 0);
    return true;
  }
  bool operator()(std::string& v, Common::StringView) override {
    if (output) {
      uint64_t len = v.size();
      scalar(len);
      buffer.append(v.data(), v.size());
    } else {
      uint64_t len = 0;
      scalar(len);
      if (pos + len > buffer.size()) return false;
      v.assign(buffer.data() + pos, (size_t)len);
      pos += (size_t)len;
    }
    return true;
  }

  bool binary(void* v, size_t sz, Common::StringView) override {
    if (output) { buffer.append((const char*)v, sz); return true; }
    return readBytes(v, sz);
  }
  bool binary(std::string& v, Common::StringView) override {
    if (output) { buffer += v; return true; }
    if (pos + v.size() > buffer.size()) return false;
    memcpy(&v[0], buffer.data() + pos, v.size());
    pos += v.size();
    return true;
  }

private:
  size_t pos;

  bool readBytes(void* v, size_t sz) {
    if (pos + sz > buffer.size()) return false;
    memcpy(v, buffer.data() + pos, sz);
    pos += sz;
    return true;
  }
  void writeBytes(const void* v, size_t sz) {
    buffer.append((const char*)v, sz);
  }

  template <typename T>
  bool scalar(T& v) {
    if (output) {
      writeBytes(&v, sizeof(T));
    } else {
      if (!readBytes(&v, sizeof(T))) return false;
    }
    return true;
  }
};

void testBankingIndexTallyAndReversal() {
  // Burn entries are keyed by the height they occur at and reversed by
  // popBlock — interleave adds with pushes exactly like block processing.
  BankingIndex idx;
  idx.pushBlock(0, 0);  // block at height 0
  idx.addForeverDeposit(50, 0);   // EF share
  idx.addTotalBurn(100, 0);       // full burn
  idx.pushBlock(0, 0);  // block at height 1
  idx.addForeverDeposit(50, 1);
  idx.addTotalBurn(100, 1);
  idx.pushBlock(0, 0);  // block at height 2
  idx.addForeverDeposit(60, 2);
  idx.addTotalBurn(120, 2);

  TEST(idx.getBurnedXfgAmount() == 160);
  TEST(idx.getTotalBurnedXfg() == 320);
  TEST(idx.getBurnedXfgAtHeight(0) == 50);
  TEST(idx.getBurnedXfgAtHeight(1) == 100);
  TEST(idx.getBurnedXfgAtHeight(2) == 160);
  TEST(idx.getTotalBurnedXfgAtHeight(2) == 320);

  idx.popBlock();
  TEST(idx.getBurnedXfgAmount() == 100);
  TEST(idx.getTotalBurnedXfg() == 200);
  idx.popBlock();
  TEST(idx.getBurnedXfgAmount() == 50);
  TEST(idx.getTotalBurnedXfg() == 100);
  idx.popBlock();
  TEST(idx.getBurnedXfgAmount() == 0);
  TEST(idx.getTotalBurnedXfg() == 0);
}

void testBankingIndexSerializationRoundtrip() {
  BankingIndex idx;
  idx.pushBlock(0, 0);
  idx.addForeverDeposit(50, 0);
  idx.addTotalBurn(100, 0);
  idx.pushBlock(0, 0);
  idx.addForeverDeposit(75, 1);
  idx.addTotalBurn(150, 1);

  MemorySerializer out(true);
  idx.serialize(out);

  BankingIndex restored;
  MemorySerializer in(false);
  in.buffer = out.buffer;
  restored.serialize(in);

  TEST(restored.getBurnedXfgAmount() == idx.getBurnedXfgAmount());
  TEST(restored.getTotalBurnedXfg() == idx.getTotalBurnedXfg());
  TEST(restored.getBurnedXfgAtHeight(0) == idx.getBurnedXfgAtHeight(0));
  TEST(restored.getBurnedXfgAtHeight(1) == idx.getBurnedXfgAtHeight(1));
  TEST(restored.getTotalBurnedXfgAtHeight(1) == idx.getTotalBurnedXfgAtHeight(1));
}

void testFiftyFiftySplitDust() {
  // ef + swf must equal total or total-1 (single odd atomic) — never more.
  for (uint64_t amount : {1ULL, 2ULL, 99ULL, 100ULL, 101ULL, 999999999ULL, 1000000001ULL}) {
    uint64_t ef = (amount * parameters::MINT_BURN_EF_PCT) / 100;
    uint64_t swf = (amount * parameters::MINT_BURN_TREASURY_PCT) / 100;
    TEST(ef + swf == amount || ef + swf + 1 == amount);
  }
}

void testNoSingleSidedLpMints() {
  TEST(ammMintLpShares(0, 100, 1000, 1000, 2000) == 0);
  TEST(ammMintLpShares(100, 0, 1000, 1000, 2000) == 0);
  TEST(ammMintLpShares(0, 100, 0, 0, 0) == 0);
  TEST(ammMintLpShares(100, 0, 0, 0, 0) == 0);
  // Balanced first deposit works.
  TEST(ammMintLpShares(10000, 5000, 0, 0, 0) > 0);
  // Balanced proportional: min ratio.
  uint64_t shares = ammMintLpShares(100, 500, 1000, 1000, 5000);
  uint64_t expectA = ((uint128_t)100 * 1000) / 1000;
  uint64_t expectB = ((uint128_t)500 * 1000) / 5000;
  TEST(shares == (expectA < expectB ? expectA : expectB));
  // Zero reserve with live shares mints nothing (no div-by-zero).
  TEST(ammMintLpShares(10, 10, 1000, 0, 5000) == 0);
}

void testCanonicalSpotPriceScale() {
  // Seed pool 10000 XFG : 1000 HEAT → 10 XFG per HEAT → HEAT/XFG = 0.1
  // Canonical price = HEAT atomics per XFG atomic × COIN = 0.1 × 1e7 = 1e6.
  uint64_t price = ammGetSpotPrice(10000 * parameters::COIN, 1000 * parameters::COIN);
  TEST(price == parameters::COIN / 10);
  // Zero XFG reserve → no price (fail closed).
  TEST(ammGetSpotPrice(0, 1000 * parameters::COIN) == 0);
}

void testTreasuryFundTagRoundtrip() {
  // Write a TreasuryFund extra via the writer and parse it back.
  std::vector<uint8_t> extra;
  TEST(addTreasuryFundToExtra(extra, 1, 123456789ULL));
  std::vector<TransactionExtraField> fields;
  TEST(parseTransactionExtra(extra, fields));
  TEST(fields.size() == 1);
  TEST(fields[0].type() == typeid(TransactionExtraTreasuryFund));
  const auto& fund = boost::get<TransactionExtraTreasuryFund>(fields[0]);
  TEST(fund.asset == 1);
  TEST(fund.amount == 123456789ULL);

  // XFG variant.
  std::vector<uint8_t> extra2;
  TEST(addTreasuryFundToExtra(extra2, 0, 42));
  std::vector<TransactionExtraField> fields2;
  TEST(parseTransactionExtra(extra2, fields2));
  TEST(fields2.size() == 1);
  const auto& fund2 = boost::get<TransactionExtraTreasuryFund>(fields2[0]);
  TEST(fund2.asset == 0);
  TEST(fund2.amount == 42);
}

} // anonymous namespace

int main() {
  testBankingIndexTallyAndReversal();
  testBankingIndexSerializationRoundtrip();
  testFiftyFiftySplitDust();
  testNoSingleSidedLpMints();
  testCanonicalSpotPriceScale();
  testTreasuryFundTagRoundtrip();
  fprintf(stderr, "=== Treasury/Core Tests ===\nPassed: %d / %d\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
