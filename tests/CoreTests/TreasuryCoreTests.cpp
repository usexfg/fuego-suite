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
#include "CryptoNoteCore/CommitmentIndex.h"
#include "CryptoNoteCore/Currency.h"
#include "Treasury/VaultUtxoSet.h"
#include "Common/Int128.h"
#include "Logging/LoggerGroup.h"
#include "Serialization/ISerializer.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
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
  uint64_t expectA = static_cast<uint64_t>(((uint128_t)100 * 1000) / 1000);
  uint64_t expectB = static_cast<uint64_t>(((uint128_t)500 * 1000) / 5000);
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

void testLimitWithdrawOwnershipProofRoundtrip() {
  Crypto::PublicKey spendPublicKey{};
  Crypto::SecretKey spendSecretKey{};
  Crypto::PublicKey viewPublicKey{};
  Crypto::SecretKey viewSecretKey{};
  Crypto::generate_keys(spendPublicKey, spendSecretKey);
  Crypto::generate_keys(viewPublicKey, viewSecretKey);
  (void)viewSecretKey;

  Crypto::Hash orderId{};
  Crypto::generate_random_bytes(sizeof(orderId.data), orderId.data);
  uint8_t addressData[sizeof(spendPublicKey.data) + sizeof(viewPublicKey.data)];
  memcpy(addressData, spendPublicKey.data, sizeof(spendPublicKey.data));
  memcpy(addressData + sizeof(spendPublicKey.data), viewPublicKey.data, sizeof(viewPublicKey.data));
  Crypto::Hash addressHash{};
  Crypto::cn_fast_hash(addressData, sizeof(addressData), addressHash);

  std::vector<TransactionOutput> outputs;
  TransactionOutput output;
  output.amount = 123;
  KeyOutput keyOutput;
  keyOutput.key = spendPublicKey;
  output.target = keyOutput;
  outputs.push_back(output);
  Crypto::Hash outputsHash = getLimitWithdrawOutputHash(outputs);
  Crypto::Hash authHash = getLimitWithdrawAuthHash(orderId, addressHash, outputsHash);
  Crypto::Signature proof{};
  Crypto::generate_signature(authHash, spendPublicKey, spendSecretKey, proof);

  std::vector<uint8_t> extra;
  TEST(addLimitWithdrawToExtra(extra, orderId, spendPublicKey, viewPublicKey, outputsHash, proof));
  std::vector<TransactionExtraField> fields;
  TEST(parseTransactionExtra(extra, fields));
  TEST(fields.size() == 1);
  TEST(fields[0].type() == typeid(TransactionExtraLimitWithdraw));
  const auto& withdraw = boost::get<TransactionExtraLimitWithdraw>(fields[0]);
  TEST(memcmp(withdraw.orderId.data, orderId.data, sizeof(orderId.data)) == 0);
  TEST(memcmp(withdraw.spendPublicKey.data, spendPublicKey.data, sizeof(spendPublicKey.data)) == 0);
  TEST(memcmp(withdraw.viewPublicKey.data, viewPublicKey.data, sizeof(viewPublicKey.data)) == 0);
  TEST(memcmp(withdraw.outputsHash.data, outputsHash.data, sizeof(outputsHash.data)) == 0);
  TEST(Crypto::check_signature(authHash, withdraw.spendPublicKey, withdraw.proof));

  Crypto::Hash wrongOrder = orderId;
  wrongOrder.data[0] ^= 1;
  TEST(!Crypto::check_signature(getLimitWithdrawAuthHash(wrongOrder, addressHash, outputsHash),
                                withdraw.spendPublicKey, withdraw.proof));
  outputs[0].amount++;
  TEST(!Crypto::check_signature(getLimitWithdrawAuthHash(
                                  orderId, addressHash, getLimitWithdrawOutputHash(outputs)),
                                withdraw.spendPublicKey, withdraw.proof));

  std::vector<uint8_t> legacyExtra;
  legacyExtra.push_back(TX_EXTRA_LIMIT_WITHDRAW);
  legacyExtra.insert(legacyExtra.end(), orderId.data, orderId.data + sizeof(orderId.data));
  std::vector<TransactionExtraField> legacyFields;
  TEST(!parseTransactionExtra(legacyExtra, legacyFields));
}

void testCdBonusClaimTagRoundtrip() {
  TransactionExtraCdBonusClaim claim;
  claim.inputIndex = 3;
  claim.claimedBonus = 0x0102030405060708ull;
  std::vector<uint8_t> extra;
  TEST(addCdBonusClaimToExtra(extra, claim));
  TEST(extra.size() == 10);  // tag + inputIndex + 8-byte LE amount

  std::vector<TransactionExtraField> fields;
  TEST(parseTransactionExtra(extra, fields));
  TEST(fields.size() == 1);
  TEST(fields[0].type() == typeid(TransactionExtraCdBonusClaim));
  const auto& parsed = boost::get<TransactionExtraCdBonusClaim>(fields[0]);
  TEST(parsed.inputIndex == 3);
  TEST(parsed.claimedBonus == 0x0102030405060708ull);

  TransactionExtraCdBonusClaim out;
  TEST(getCdBonusClaimFromExtra(extra, out));
  TEST(out.inputIndex == 3);
  TEST(out.claimedBonus == 0x0102030405060708ull);

  // Truncated payload must fail cleanly (no over-read).
  std::vector<uint8_t> truncated(extra.begin(), extra.begin() + 6);
  TEST(!getCdBonusClaimFromExtra(truncated, out));
}


void testVaultSpendNoSurplusBurn() {
  VaultUtxoSet vault;
  Crypto::Hash txHash{};
  Crypto::PublicKey key{};
  vault.addUtxo(100, 6, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  vault.addUtxo(101, 9, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 15);

  // Overshooting spend: 6 + 9 selected for 10 needed — surplus 5 must be
  // reported as change, not destroyed (W-3).
  auto r = vault.spendUtxos(VaultPartition::CD_APY_POOL, AssetType::HEAT, 10);
  TEST(r.amountSpent == 15);
  TEST(r.changeAmount == 5);
  TEST(r.changeSourceIndex == 101);
  TEST(r.spentIndices.size() == 2);
  // Caller mints the change back (mirrors Blockchain::mintVaultChangeUtxo).
  vault.addUtxo(102, r.changeAmount, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 5);

  // Exact spend → no change.
  auto r2 = vault.spendUtxos(VaultPartition::CD_APY_POOL, AssetType::HEAT, 5);
  TEST(r2.amountSpent == 5);
  TEST(r2.changeAmount == 0);

  // Pop symmetry: unspend both spends, drop the change UTXO → originals
  // restored (6 + 9), change gone with its spender.
  vault.unSpendUtxos(r2.spentIndices);
  vault.removeAboveIndex(102);
  vault.unSpendUtxos(r.spentIndices);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 15);
}





// ---------------------------------------------------------------------------
// Pop across multiple epochs: record bonus at epochs 0-4, pop twice,
// verify rollback symmetry.
// ---------------------------------------------------------------------------
void testPopBonusEpochRate() {
  Logging::LoggerGroup nullLog;
  Currency currency = CurrencyBuilder(nullLog).currency();
  CommitmentIndex ci(currency);

  ci.recordEpochFeeRate(0, 1000, 100, 1000);
  ci.recordEpochFeeRate(1, 1000, 100, 1000);
  ci.recordEpochFeeRate(2, 1000, 100, 1000);
  ci.recordEpochFeeRate(3, 1000, 100, 1000);
  ci.recordEpochFeeRate(4, 1000, 100, 1000);

  ci.recordBonusEpochRate(0, 50, 200);
  ci.recordBonusEpochRate(1, 60, 300);
  ci.recordBonusEpochRate(2, 70, 400);
  ci.recordBonusEpochRate(3, 80, 500);
  ci.recordBonusEpochRate(4, 90, 600);

  TEST(ci.getEpochCount() == 5);

  // Pop twice: removes epochs 4 and 3.
  ci.popBonusEpochRate();
  ci.popBonusEpochRate();
  TEST(ci.getBonusEpochCount() == 3);

  BonusEpochRateEntry e2 = ci.getBonusEpochRateEntry(2);
  TEST(e2.bonusHeat == 70);
  TEST(e2.weightedBase == 400);

  // Epoch 3 is now empty (popped).
  BonusEpochRateEntry e3 = ci.getBonusEpochRateEntry(3);
  TEST(e3.bonusHeat == 0);
  TEST(e3.weightedBase == 0);

  // Pop remaining: removes epochs 2, 1, 0.
  ci.popBonusEpochRate();
  ci.popBonusEpochRate();
  ci.popBonusEpochRate();
  TEST(ci.getBonusEpochCount() == 0);

  BonusEpochRateEntry e0 = ci.getBonusEpochRateEntry(0);
  TEST(e0.bonusHeat == 0);
  TEST(e0.weightedBase == 0);

  // calculateCdBonus must return 0 with no recorded epochs.
  uint64_t bonus = currency.calculateCdBonus(1000, 10, 100, ci,
      parameters::TESTNET_DEPOSIT_MAX_TERM);
  TEST(bonus == 0);
}

// ---------------------------------------------------------------------------
// Vault partition isolation: UTXOs in different partitions cannot be spent
// from the wrong partition.
// ---------------------------------------------------------------------------
void testVaultPartitionIsolation() {
  VaultUtxoSet vault;
  Crypto::Hash txHash{};
  Crypto::PublicKey key{};

  // Seed three partitions with 100 HEAT each.
  vault.addUtxo(0, 100, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  vault.addUtxo(1, 100, AssetType::HEAT, VaultPartition::BONUS_VAULT, txHash, key);
  vault.addUtxo(2, 100, AssetType::HEAT, VaultPartition::GENERAL_RESERVE, txHash, key);

  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 100);
  TEST(vault.partitionBalance(VaultPartition::BONUS_VAULT, AssetType::HEAT) == 100);
  TEST(vault.partitionBalance(VaultPartition::GENERAL_RESERVE, AssetType::HEAT) == 100);

  // Spending from CD_APY_POOL must not touch BONUS_VAULT or GENERAL_RESERVE.
  auto r1 = vault.spendUtxos(VaultPartition::CD_APY_POOL, AssetType::HEAT, 50);
  TEST(r1.amountSpent == 100);  // overshoot: only one UTXO (100) available
  TEST(r1.changeAmount == 50);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 0);
  TEST(vault.partitionBalance(VaultPartition::BONUS_VAULT, AssetType::HEAT) == 100);
  TEST(vault.partitionBalance(VaultPartition::GENERAL_RESERVE, AssetType::HEAT) == 100);

  // Spending from BONUS_VAULT.
  auto r2 = vault.spendUtxos(VaultPartition::BONUS_VAULT, AssetType::HEAT, 100);
  TEST(r2.amountSpent == 100);
  TEST(r2.changeAmount == 0);
  TEST(vault.partitionBalance(VaultPartition::BONUS_VAULT, AssetType::HEAT) == 0);
  TEST(vault.partitionBalance(VaultPartition::GENERAL_RESERVE, AssetType::HEAT) == 100);

  // Spending more than available from GENERAL_RESERVE → empty.
  auto r3 = vault.spendUtxos(VaultPartition::GENERAL_RESERVE, AssetType::HEAT, 200);
  TEST(r3.amountSpent == 100);
  TEST(r3.changeAmount == 0);
  TEST(vault.partitionBalance(VaultPartition::GENERAL_RESERVE, AssetType::HEAT) == 0);
  TEST(vault.totalUtxos() == 3);  // unspent entries still present
}

// ---------------------------------------------------------------------------
// Surplus-mint roundtrip across partitions: spend overshoots, mint change
// back to same partition, verify final balances.
// ---------------------------------------------------------------------------
void testVaultSurplusMintRoundtrip() {
  VaultUtxoSet vault;
  Crypto::Hash txHash{};
  Crypto::PublicKey key{};

  vault.addUtxo(0, 30, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  vault.addUtxo(1, 70, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 100);

  // Spend 25: selects UTXO 0 (30), surplus = 5.
  auto r = vault.spendUtxos(VaultPartition::CD_APY_POOL, AssetType::HEAT, 25);
  TEST(r.amountSpent == 30);
  TEST(r.changeAmount == 5);
  TEST(r.changeSourceIndex == 0);

  // Mint change back (index 2 = 5 HEAT).
  vault.addUtxo(2, r.changeAmount, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 75);

  // Spend 50: selectUtxos sorts ascending [5(idx2), 70(idx1)].
  // Selects both: 5 + 70 = 75, surplus = 25.
  auto r2 = vault.spendUtxos(VaultPartition::CD_APY_POOL, AssetType::HEAT, 50);
  TEST(r2.amountSpent == 75);
  TEST(r2.changeAmount == 25);
  TEST(r2.changeSourceIndex == 1);
  vault.addUtxo(3, r2.changeAmount, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 25);
}

// ---------------------------------------------------------------------------
// Pop symmetry: model the real blockchain pop pattern — remove block outputs
// first, then unspend inputs the block consumed. Verify the vault returns to
// its pre-block state.
// ---------------------------------------------------------------------------
void testVaultPopSymmetry() {
  VaultUtxoSet vault;
  Crypto::Hash txHash{};
  Crypto::PublicKey key{};

  // Pre-existing UTXO from a prior block (index 0).
  vault.addUtxo(0, 50, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 50);

  // Block connects: adds its output at index 1, and during the block's
  // transaction processing the pre-existing UTXO 0 is consumed (spent).
  vault.addUtxo(1, 50, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 100);

  // The block's claim tx spends UTXO 0 (pre-existing input consumed).
  std::vector<uint64_t> spentIndices = {0};
  vault.markSpent(0);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 50);
  TEST(vault.totalUtxos() == 2);

  // --- Pop the block ---
  // Step 1: remove block outputs (indices >= 1, i.e. index 1).
  vault.removeAboveIndex(1);
  TEST(vault.totalUtxos() == 1);
  // UTXO 0 is still spent, so balance is 0.
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 0);

  // Step 2: unspend the inputs the block consumed.
  vault.unSpendUtxos(spentIndices);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 50);
  TEST(vault.totalUtxos() == 1);
}


} // anonymous namespace

// CD interest accrues per epoch and COMPOUNDS into the principal each epoch.
// An auto-rolled CD stops compounding at its original maturity (rolloverEpoch)
// while continuing to accrue on the frozen base. Nothing else pinned this, and
// it is money math, so the exact figures are asserted rather than the shape.
void testCdInterestCompounding() {
  Logging::LoggerGroup nullLog;
  Currency currency = CurrencyBuilder(nullLog).currency();
  CommitmentIndex ci(currency);

  const uint64_t RATE = 100000;  // 10% of FEE_POOL_RATE_PRECISION (1e6)
  const uint64_t ED = parameters::EPOCH_DURATION_BLOCKS;
  for (uint64_t e = 0; e <= 5; ++e) ci.recordEpochFeeRate(e, RATE, 100, 1000);

  // 1000 over epochs 0..5 at 10%: 100,110,121,133,146,161 = 771.
  // Simple interest would be 600 — compounding is deliberate.
  uint64_t interest = currency.calculateCdInterest(1000, 0, (uint32_t)(5 * ED),
                                                   ci, 0, false);
  TEST(interest == 771);

  // Auto-rolled with a 2-epoch original term. Since AUDIT 2.4 accrual also
  // stops at maturity, so this covers epochs 0..2 only: 100 + 110 + 121 = 331.
  // (Before the maturity clamp this ran to epoch 5 and returned >600.)
  uint64_t rolled = currency.calculateCdInterest(1000, 0, (uint32_t)(5 * ED),
                                                 ci, (uint32_t)(2 * ED), true);
  TEST(rolled == 331);
  TEST(rolled < interest);

  // No elapsed time accrues nothing.
  TEST(currency.calculateCdInterest(1000, 100, 100, ci, 0, false) == 0);
  TEST(currency.calculateCdInterest(1000, 200, 100, ci, 0, false) == 0);

  // Interest scales linearly in principal at equal terms.
  uint64_t small = currency.calculateCdInterest(1000, 0, (uint32_t)(2 * ED), ci, 0, false);
  uint64_t big   = currency.calculateCdInterest(10000, 0, (uint32_t)(2 * ED), ci, 0, false);
  TEST(big >= small * 10 - 10 && big <= small * 10 + 10);
}

// AUDIT 2.1: legacy XFG term deposits earn no on-chain interest — all yield is
// via HEAT CDs. calculateInterest() returning 0 must NOT strand a matured
// legacy deposit: getTransactionInputAmount values the input at principal, so a
// principal-minus-fee withdrawal still passes money conservation. The old body
// computed a rate, a 128-bit product and an early-deposit multiplier, then
// returned a variable that was initialised to 0 and never assigned — so it
// always returned 0 too. This pins that equivalence.
void testLegacyDepositWithdrawsForPrincipal() {
  Logging::LoggerGroup nullLog;
  Currency currency = CurrencyBuilder(nullLog).currency();

  const uint64_t principal = 800000;  // 0.08 XFG in atomic units

  // Interest is zero at every term and height, including the early-deposit
  // window that used to carry a multiplier.
  TEST(currency.calculateInterest(principal, 0, 0) == 0);
  TEST(currency.calculateInterest(principal, parameters::DEPOSIT_MIN_TERM, 1) == 0);
  TEST(currency.calculateInterest(principal, parameters::DEPOSIT_MAX_TERM, 1000000) == 0);
  TEST(currency.calculateInterest(std::numeric_limits<uint64_t>::max(),
                                  parameters::DEPOSIT_MAX_TERM, 1) == 0);

  // A term deposit is valued at exactly its principal, so it remains spendable.
  MultisignatureInput term{};
  term.amount = principal;
  term.signatureCount = 1;
  term.outputIndex = 0;
  term.term = parameters::DEPOSIT_MAX_TERM;
  TransactionInput termIn = term;
  TEST(currency.getTransactionInputAmount(termIn, 1000000) == principal);

  // Term 0 (a plain multisig output, never a deposit) is unchanged.
  MultisignatureInput plain = term;
  plain.term = 0;
  TransactionInput plainIn = plain;
  TEST(currency.getTransactionInputAmount(plainIn, 1000000) == principal);

  // Valuation does not drift with height — a deposit held longer is still
  // worth its principal, which is what keeps withdrawal conservation stable.
  TEST(currency.getTransactionInputAmount(termIn, 1) ==
       currency.getTransactionInputAmount(termIn, 5000000));
}


// AUDIT 2.4: base CD interest must stop accruing at maturity. Before the fix,
// `term` had no effect on the payout — it appears only in rolloverEpoch, which
// collapses to endEpoch while auto-roll is disabled — so a 1-block commitment
// earned the same per-epoch yield as a DEPOSIT_MAX_TERM CD, defeating
// DEPOSIT_MIN_TERM. calculateCdBonus already clamped; these now agree.
void testCdInterestStopsAtMaturity() {
  Logging::LoggerGroup nullLog;
  Currency currency = CurrencyBuilder(nullLog).currency();
  CommitmentIndex ci(currency);

  const uint64_t RATE = 100000;  // 10% per epoch
  const uint64_t ED = parameters::EPOCH_DURATION_BLOCKS;
  for (uint64_t e = 0; e <= 5; ++e) ci.recordEpochFeeRate(e, RATE, 100, 1000);

  // Held six epochs. A CD whose term expires after two must not be paid for six.
  uint64_t shortTerm = currency.calculateCdInterest(1000, 0, (uint32_t)(5 * ED),
                                                    ci, (uint32_t)(2 * ED), false);
  uint64_t noTerm    = currency.calculateCdInterest(1000, 0, (uint32_t)(5 * ED),
                                                    ci, 0, false);
  TEST(shortTerm < noTerm);

  // Accrual is frozen after maturity: holding longer pays no more.
  uint64_t atMaturity = currency.calculateCdInterest(1000, 0, (uint32_t)(2 * ED),
                                                     ci, (uint32_t)(2 * ED), false);
  TEST(shortTerm == atMaturity);

  // A one-block "CD" earns essentially nothing rather than a full epoch's yield.
  uint64_t oneBlock = currency.calculateCdInterest(1000, 0, (uint32_t)(5 * ED),
                                                   ci, 1, false);
  TEST(oneBlock < noTerm);
  TEST(oneBlock <= 100);   // at most the first epoch's simple yield

  // Longer term pays more than shorter, all else equal — term now matters.
  uint64_t longTerm = currency.calculateCdInterest(1000, 0, (uint32_t)(5 * ED),
                                                   ci, (uint32_t)(4 * ED), false);
  TEST(longTerm > shortTerm);

  // Bonus and base now clamp identically.
  for (uint64_t e = 0; e <= 5; ++e) ci.recordBonusEpochRate(e, 100, 1000);
  uint64_t bonusShort = currency.calculateCdBonus(1000, 0, (uint32_t)(5 * ED), ci, (uint32_t)(2 * ED));
  uint64_t bonusLong  = currency.calculateCdBonus(1000, 0, (uint32_t)(5 * ED), ci, (uint32_t)(4 * ED));
  TEST(bonusShort <= bonusLong);
}


// The Bonus Vault is a YIELD FLOOR, not a pro-rata tier pool. In any epoch whose
// fee-derived rate fell below CD_YIELD_FLOOR_RATE, the vault tops each CD up to
// the floor. There is no shared denominator, so a CD's top-up depends only on
// its own principal and the global epoch rate — nothing another depositor does
// can change it.
void testCdYieldFloor() {
  Logging::LoggerGroup nullLog;
  Currency currency = CurrencyBuilder(nullLog).currency();
  CommitmentIndex ci(currency);

  const uint64_t FLOOR = parameters::CD_YIELD_FLOOR_RATE;
  const uint64_t PREC  = parameters::FEE_POOL_RATE_PRECISION;
  const uint64_t ED    = parameters::EPOCH_DURATION_BLOCKS;
  const uint32_t TERM  = parameters::DEPOSIT_MAX_TERM;

  // epoch 0 fat (at the floor), epochs 1-2 lean (half the floor), epoch 3 fat.
  ci.recordEpochFeeRate(0, FLOOR,     100, 1000);
  ci.recordEpochFeeRate(1, FLOOR / 2, 100, 1000);
  ci.recordEpochFeeRate(2, FLOOR / 2, 100, 1000);
  ci.recordEpochFeeRate(3, FLOOR * 4, 100, 1000);

  const uint64_t P = 1000000;  // principal

  // A fat epoch alone tops up nothing.
  TEST(currency.calculateCdBonus(P, 0, (uint32_t)(0 * ED + 1), ci, TERM) == 0);

  // One lean epoch pays exactly the shortfall on this CD's own principal.
  uint64_t oneLean = currency.calculateCdBonus(P, (uint32_t)ED, (uint32_t)(ED + 1), ci, TERM);
  uint64_t expect1 = (uint64_t)(((__uint128_t)P * (FLOOR - FLOOR / 2)) / PREC);
  TEST(oneLean == expect1);

  // Two lean epochs pay twice as much; the fat epochs contribute nothing.
  uint64_t twoLean = currency.calculateCdBonus(P, 0, (uint32_t)(3 * ED), ci, TERM);
  TEST(twoLean == expect1 * 2);

  // Linear in principal — no denominator, so no interaction between depositors.
  TEST(currency.calculateCdBonus(P * 10, 0, (uint32_t)(3 * ED), ci, TERM) == twoLean * 10);

  // Flat in term: the floor is the same guarantee whatever the lock length.
  uint64_t shortTerm = currency.calculateCdBonus(P, 0, (uint32_t)(3 * ED), ci,
                                                 (uint32_t)(6 * ED));
  uint64_t longTerm  = currency.calculateCdBonus(P, 0, (uint32_t)(3 * ED), ci, TERM);
  TEST(shortTerm == longTerm);

  // Still clamped at maturity — a matured CD stops collecting the floor.
  uint64_t matured = currency.calculateCdBonus(P, 0, (uint32_t)(3 * ED), ci, (uint32_t)ED);
  TEST(matured < twoLean);

  // No elapsed time, and unrecorded epochs, pay nothing.
  TEST(currency.calculateCdBonus(P, 100, 100, ci, TERM) == 0);
  TEST(currency.calculateCdBonus(P, 200, 100, ci, TERM) == 0);
  CommitmentIndex empty(currency);
  TEST(currency.calculateCdBonus(P, 0, (uint32_t)(3 * ED), empty, TERM) == 0);
}


int main() {
  testCdInterestCompounding();
  testLegacyDepositWithdrawsForPrincipal();
  testCdInterestStopsAtMaturity();
  testCdYieldFloor();
  testBankingIndexTallyAndReversal();
  testBankingIndexSerializationRoundtrip();
  testFiftyFiftySplitDust();
  testNoSingleSidedLpMints();
  testCanonicalSpotPriceScale();
  testTreasuryFundTagRoundtrip();
  testLimitWithdrawOwnershipProofRoundtrip();
  testCdBonusClaimTagRoundtrip();
  testVaultSpendNoSurplusBurn();
  testPopBonusEpochRate();
  testVaultPartitionIsolation();
  testVaultSurplusMintRoundtrip();
  testVaultPopSymmetry();
  fprintf(stderr, "=== Treasury/Core Tests ===\nPassed: %d / %d\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
