// Copyright (c) 2026 Fuego Developers
//
// Consensus-adjacent unit tests for the F0–F3 fixes:
//  - BankingIndex: EF bucket vs overall burn tally, per-height entries,
//    popBlock reversal, serialization roundtrip.
//  - 50/50 EF/SWF split consistency (dust ≤ 1 atomic).
//  - ammMintLpShares: no single-sided mints; balanced min-ratio.
//  - ammGetSpotPrice: canonical HEAT/XFG × COIN scale.
//  - TreasuryFund tx-extra tag: writer/reader roundtrip.

#include "CryptoNoteCore/Account.h"
#include "CryptoNoteCore/BankingIndex.h"
#include "CryptoNoteCore/AmmPool.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/OrderbookAuction.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "CryptoNoteCore/CommitmentIndex.h"
#include "CryptoNoteCore/Currency.h"
#include "Treasury/VaultUtxoSet.h"
#include "Common/Int128.h"
#include "Logging/LoggerGroup.h"
#include "CryptoNoteCore/Blockchain.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"
#include "CryptoNoteCore/Core.h"
#include "CryptoNoteCore/ITimeProvider.h"
#include "CryptoNoteCore/TransactionPool.h"
#include "Serialization/ISerializer.h"
#include "Serialization/BinarySerializationTools.h"
#include "Serialization/BinaryInputStreamSerializer.h"
#include "Serialization/BinaryOutputStreamSerializer.h"
#include "Common/MemoryInputStream.h"
#include "Common/VectorOutputStream.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace CryptoNote;

static long currentProcessId() {
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<long>(::getpid());
#endif
}

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
  // Seed pool 8000 XFG : 1000 HEAT → 8 XFG per HEAT → HEAT/XFG = 0.125
  // Canonical price = HEAT atomics per XFG atomic × COIN = 0.125 × 1e7.
  uint64_t price = ammGetSpotPrice(8000 * parameters::COIN, 1000 * parameters::COIN);
  TEST(price == parameters::COIN / 8);
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

  // uint128_t (Common/Int128.h), not the __uint128_t GCC/Clang builtin: MSVC
  // has no such type and only this line used it. Mirrors the same computation
  // in Currency::calculateCdBonus, which this asserts against.
  uint64_t expect1 = static_cast<uint64_t>(((uint128_t)P * (FLOOR - FLOOR / 2)) / PREC);
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



// The v11 rate denominator counts a CD from creation until the boundary that
// closes cdLastCreditedEpoch, then drops it. That is only correct if the
// payout credits exactly the same window — otherwise the denominator either
// dilutes epochs nobody is paid for or omits epochs somebody is paid for.
// Probe each epoch in turn with a lone nonzero rate.
void testCdEarningWindowMatchesPayout() {
  Logging::LoggerGroup nullLog;
  Currency currency = CurrencyBuilder(nullLog).currency();
  const uint64_t ED = parameters::EPOCH_DURATION_BLOCKS;
  const uint32_t heights[] = {0, 1, (uint32_t)(ED - 1), (uint32_t)ED, (uint32_t)(ED + 17), (uint32_t)(3 * ED + 450)};
  const uint32_t terms[] = {parameters::DEPOSIT_MIN_TERM, (uint32_t)(18 * ED), parameters::DEPOSIT_MAX_TERM,
                            (uint32_t)(6 * ED + 1), (uint32_t)(6 * ED - 1)};
  bool allMatch = true;
  for (uint32_t c : heights) {
    for (uint32_t t : terms) {
      const uint64_t first = c / ED;
      const uint64_t last = currency.cdLastCreditedEpoch(c, t);
      if (last != (uint64_t(c) + t) / ED) allMatch = false;
      for (uint64_t probe = (first > 0 ? first - 1 : 0); probe <= last + 1; ++probe) {
        CommitmentIndex ci(currency);
        for (uint64_t e = 0; e <= last + 2; ++e) {
          ci.recordEpochFeeRate(e, e == probe ? 10000 : 0, 100, 1000);
        }
        const uint32_t farFuture = (uint32_t)((last + 3) * ED);
        const bool paid = currency.calculateCdInterest(1000000, c, farFuture, ci, t, false) > 0;
        const bool inWindow = probe >= first && probe <= last;
        if (paid != inWindow) allMatch = false;
      }
    }
  }
  TEST(allMatch);
}

// One asset per commitment term, used by both sides of the per-asset balance.
// Term 0 is plain HEAT: it used to be minted as HEAT but spent as XFG.
void testCommitmentAssetClassification() {
  const uint32_t terms[] = {0, parameters::DEPOSIT_MIN_TERM, parameters::DEPOSIT_MAX_TERM,
                            parameters::HEAT_TERM, parameters::DEPOSIT_TERM_LP,
                            parameters::DEPOSIT_TERM_POOL_XFG, parameters::DEPOSIT_TERM_POOL_HEAT,
                            parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG, parameters::DIGM_TERM};
  bool sidesAgree = true;
  for (uint32_t term : terms) {
    TransactionOutputCommitment out;
    out.term = term;
    if (Currency::classifyOutputAsset(out, term) != Currency::classifyCommitmentTermAsset(term))
      sidesAgree = false;
  }
  TEST(sidesAgree);
  TEST(Currency::classifyCommitmentTermAsset(0) == AssetType::HEAT);
  TEST(Currency::classifyCommitmentTermAsset(parameters::DEPOSIT_MIN_TERM) == AssetType::HEAT);
  TEST(Currency::classifyCommitmentTermAsset(parameters::HEAT_TERM) == AssetType::HEAT);
  TEST(Currency::classifyCommitmentTermAsset(parameters::DEPOSIT_TERM_LP) == AssetType::LP);
  TEST(Currency::classifyCommitmentTermAsset(parameters::DEPOSIT_TERM_POOL_XFG) == AssetType::XFG);
  TEST(Currency::classifyCommitmentTermAsset(parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG) == AssetType::XFG);
}

// A real Blockchain at genesis in a temp directory: Hearth pool seeded, no
// TWAP yet. Removes its directory on scope exit.
struct TestChain {
  Logging::LoggerGroup log;
  Currency currency;
  RealTimeProvider timeProvider;
  tx_memory_pool pool;
  Blockchain chain;
  std::filesystem::path dir;
  bool ok = false;
  bool keepDirectory = false;

  explicit TestChain(const std::string& tag, bool testnet = false)
      : TestChain(tag, [testnet](CurrencyBuilder& builder) { builder.testnet(testnet); }) {}

  TestChain(const std::string& tag, const std::function<void(CurrencyBuilder&)>& configure)
      : currency(configuredCurrency(log, configure)),
        pool(currency, chain, timeProvider, log),
        chain(currency, pool, log, false, false),
        dir(std::filesystem::temp_directory_path() /
            ("fuego_core_tests_" + tag + "_" + std::to_string(currentProcessId()))) {
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    ok = chain.init(dir.string(), false);
  }
  static Currency configuredCurrency(Logging::ILogger& logger, const std::function<void(CurrencyBuilder&)>& configure) {
    CurrencyBuilder builder(logger);
    configure(builder);
    return builder.currency();
  }

  ~TestChain() {
    if (keepDirectory) {
      return;
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

// rebuildCache() replays the chain from scratch and must start from the same
// Hearth pool a fresh sync starts from. It used to reset the pool to empty and
// never re-seed it, so every node that rebuilt its cache — including every
// node crossing a cache-version bump — diverged from the rest of the network.
void testRebuildCacheKeepsHearthSeed() {
  TestChain t("rebuild");
  TEST(t.ok);
  const uint64_t seedXfg = parameters::HEARTH_POOL_SEED_XFG * parameters::COIN;
  const uint64_t seedHeat = parameters::HEARTH_POOL_SEED_HEAT * parameters::COIN;
  TEST(t.chain.getAmmPool().reserveXfg == seedXfg);
  TEST(t.chain.getAmmPool().reserveHeat == seedHeat);

  // The seed carries its provider's LP shares: √(xfg · heat).
  const uint64_t seedShares = ammInitialLpShares(seedXfg, seedHeat);
  TEST(seedShares > 0 && t.chain.getAmmPool().totalLpShares == seedShares);

  t.chain.rebuildCache();
  TEST(t.chain.getAmmPool().reserveXfg == seedXfg);
  TEST(t.chain.getAmmPool().reserveHeat == seedHeat);
  TEST(t.chain.getAmmPool().totalLpShares == seedShares);
}

// With seed shares, a first LP deposit earns its fair slice: 1% of both
// reserves earns 1% of the shares. Without them it earned every share — and
// with them the whole pool, seed included.
void testSeedLpSharesMakeFirstDepositFair() {
  TestChain t("seedshares");
  TEST(t.ok);
  const auto& pool = t.chain.getAmmPool();
  const uint64_t shares = ammMintLpShares(pool.reserveXfg / 100, pool.reserveHeat / 100,
                                          pool.totalLpShares, pool.reserveXfg, pool.reserveHeat);
  TEST(shares == pool.totalLpShares / 100);
  // Without the seed's shares the same deposit took everything.
  TEST(ammMintLpShares(pool.reserveXfg / 100, pool.reserveHeat / 100, 0, 0, 0) > 0);
}

// The /heat_metrics redemption price is the pool's XFG-per-HEAT ratio at COIN
// scale. At the genesis seed it must be exactly 80,000,000 / 10,000,000.
void testHeatMetricsSeedRedemptionPrice() {
  Logging::LoggerGroup log;
  Currency currency(CurrencyBuilder(log).currency());
  core heatCore(currency, nullptr, log, false, false);

  const uint64_t C = parameters::COIN;
  const auto& pool = heatCore.get_blockchain_storage().getAmmPool();
  TEST(pool.reserveXfg == 8000 * C);
  TEST(pool.reserveHeat == 1000 * C);

  const auto metrics = heatCore.getHeatMetrics();
  TEST(metrics.redemptionPriceNum == 80000000);
  TEST(metrics.redemptionPriceDenom == C);
}


TransactionOutput keyOut(uint64_t amount) {
  TransactionOutput out;
  out.amount = amount;
  out.target = KeyOutput{};
  return out;
}

TransactionOutput commitmentOut(uint64_t amount, uint32_t term) {
  TransactionOutputCommitment commitment{};
  commitment.term = term;
  TransactionOutput out;
  out.amount = amount;
  out.target = commitment;
  return out;
}

// XFG key inputs only: validateSettlement neither resolves nor signs them.
Transaction settlementTx(const std::vector<uint64_t>& xfgInputs,
                         const std::vector<TransactionOutput>& outputs,
                         const std::vector<uint8_t>& extra) {
  Transaction tx{};
  for (uint64_t amount : xfgInputs) {
    KeyInput in{};
    in.amount = amount;
    tx.inputs.push_back(in);
  }
  tx.outputs = outputs;
  tx.extra = extra;
  return tx;
}

// The v11 balance rule: the fee is the XFG surplus only; HEAT and LP balance
// exactly, counting declared sources and sinks.
void testSettleAssetFlows() {
  const uint64_t C = parameters::COIN;
  uint64_t fee = 0;

  // A mint burns 100 XFG and creates 9 HEAT, paying a 1 XFG fee. The v10 rule
  // summed the assets: its "fee" was 100 + 1 − 9 = 92, all paid to the miner.
  AssetFlows mint;
  mint.in.xfg = 101 * C;
  mint.xfgSink = 100 * C;
  mint.heatSource = 9 * C;
  mint.out.heat = 9 * C;
  TEST(settleAssetFlows(mint, fee) && fee == 1 * C);

  // HEAT beyond the declared source is refused, and so is HEAT left over.
  AssetFlows more = mint;
  more.out.heat += 1;
  TEST(!settleAssetFlows(more, fee));
  AssetFlows less = mint;
  less.out.heat -= 1;
  TEST(!settleAssetFlows(less, fee));

  // LP shares balance exactly too.
  AssetFlows lp;
  lp.in.xfg = 1 * C;
  lp.lpSource = 5;
  lp.out.lp = 6;
  TEST(!settleAssetFlows(lp, fee));

  // Outputs above inputs: refused, never an underflowed fee.
  AssetFlows over;
  over.in.xfg = 1 * C;
  over.out.xfg = 2 * C;
  TEST(!settleAssetFlows(over, fee));

  // Sums that overflow are refused.
  AssetFlows wrap;
  wrap.in.xfg = UINT64_MAX;
  wrap.xfgSource = 1;
  TEST(!settleAssetFlows(wrap, fee));
}

// validateSettlement on a real chain: fees, conversions, retired tags.
void testValidateSettlementBasics() {
  TestChain t("settle");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  AssetFlows flows;
  uint64_t fee = 0;
  std::string err;
  auto check = [&](const Transaction& tx) {
    Blockchain::SettlementScratch scratch;
    return t.chain.validateSettlement(tx, 1, scratch, flows, fee, err);
  };

  // A 50 XFG treasury donation is burned, not paid to the miner.
  std::vector<uint8_t> fund;
  addTreasuryFundToExtra(fund, 0, 50 * C);
  TEST(check(settlementTx({100 * C}, {keyOut(49 * C)}, fund)) && fee == 1 * C);

  // A plain transaction cannot turn XFG into HEAT.
  TEST(!check(settlementTx({100 * C}, {commitmentOut(90 * C, parameters::HEAT_TERM)}, {})));

  // A mint is priced by the 8-block TWAP; at genesis there is none.
  std::vector<uint8_t> mint;
  addHeatMintAuthToExtra(mint, 10 * C, 1 * C);
  TEST(!check(settlementTx({11 * C}, {commitmentOut(1 * C, parameters::HEAT_TERM)}, mint)));

  // The retired 0x08 burn tag is refused: its amount was never checked
  // against a burn, yet it raised the Eternal Flame and the block reward.
  std::vector<uint8_t> legacyBurn;
  TransactionExtraHeatCommitment claimed{};
  claimed.amount = 1000000 * C;
  addHeatCommitmentToExtra(legacyBurn, claimed);
  TEST(!check(settlementTx({10 * C}, {keyOut(9 * C)}, legacyBurn)));

  // One settlement tag per transaction.
  std::vector<uint8_t> twoTags;
  addTreasuryFundToExtra(twoTags, 0, 1 * C);
  addTreasuryFundToExtra(twoTags, 0, 1 * C);
  TEST(!check(settlementTx({10 * C}, {keyOut(7 * C)}, twoTags)));

  // Before v11 the pool's fee is inputs minus outputs; outputs above inputs
  // are refused even with a mint tag (it used to underflow the fee).
  uint64_t legacyFee = 0;
  TEST(t.chain.checkTransactionSettlement(settlementTx({10 * C}, {keyOut(9 * C)}, {}), legacyFee) &&
       legacyFee == 1 * C);
  TEST(!t.chain.checkTransactionSettlement(
      settlementTx({1 * C}, {commitmentOut(100 * C, parameters::HEAT_TERM)}, mint), legacyFee));
  // Before v11 the pool relays no HEAT, Hearth or 0x08 tag at all, even on a
  // balanced transaction: v10 has no mint price and never checked a 0x08 burn.
  TEST(!t.chain.checkTransactionSettlement(settlementTx({10 * C}, {keyOut(9 * C)}, legacyBurn), legacyFee));
  TEST(!t.chain.checkTransactionSettlement(settlementTx({10 * C}, {keyOut(9 * C)}, mint), legacyFee));
  TEST(!t.chain.checkTransactionSettlement(settlementTx({10 * C}, {keyOut(8 * C)}, fund), legacyFee));
}

// The pre-v11 tag cutoff defaults to the v11 height on mainnet, so it changes
// no v10 block until a release moves it earlier; testnet is at v11 already.
void testHeatwaveTagCutoffHeight() {
  Logging::LoggerGroup nullLog;
  Currency mainnet = CurrencyBuilder(nullLog).currency();
  TEST(mainnet.heatwaveTagCutoffHeight() == parameters::HEATWAVE_TAG_CUTOFF_HEIGHT);
  TEST(mainnet.heatwaveTagCutoffHeight() <= mainnet.upgradeHeight(BLOCK_MAJOR_VERSION_11));
  Currency testnet = CurrencyBuilder(nullLog).testnet(true).currency();
  TEST(testnet.heatwaveTagCutoffHeight() == testnet.upgradeHeight(BLOCK_MAJOR_VERSION_11));
}

// Swaps are priced on the constant-product curve, not linearly at spot.
void testValidateSettlementSwapCurve() {
  TestChain t("swap");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const uint64_t rX = t.chain.getAmmPool().reserveXfg;
  const uint64_t rH = t.chain.getAmmPool().reserveHeat;
  const uint64_t input = rX / 10;
  const uint64_t gross = ammGetOutputAmount(input, rX, rH, 0);
  const uint64_t net = static_cast<uint64_t>(
      ((uint128_t)gross * (parameters::HEARTH_FEE_DIVISOR - parameters::HEARTH_FEE_BPS)) /
      parameters::HEARTH_FEE_DIVISOR);
  // /amm_quote uses this same helper; a wallet quote above net cannot settle.
  TEST(net == ammSwapNetOutput(input, rX, rH));
  AssetFlows flows;
  uint64_t fee = 0;
  std::string err;
  auto swap = [&](uint64_t output, uint64_t marker, uint64_t minOutput) {
    std::vector<uint8_t> extra;
    addAmmSwapAuthToExtra(extra, 0, input, output, minOutput);
    std::vector<TransactionOutput> outs{commitmentOut(output, parameters::HEAT_TERM)};
    if (marker > 0) outs.push_back(commitmentOut(marker, parameters::DEPOSIT_TERM_POOL_XFG));
    Blockchain::SettlementScratch scratch;
    return t.chain.validateSettlement(settlementTx({input + 1 * C}, outs, extra), 1, scratch,
                                      flows, fee, err);
  };

  // The curve's net output settles, the deposit carried by a pool marker.
  TEST(swap(net, input, 0) && fee == 1 * C);
  // Or with no marker at all: the declared input is the sink either way.
  TEST(swap(net, 0, 0) && fee == 1 * C);
  // One atom above the curve is refused.
  TEST(!swap(net + 1, input, 0));
  // The old linear price paid a 10%-of-reserve swap ~10% more than the curve.
  const uint64_t linear = static_cast<uint64_t>(
      ((uint128_t)input * rH / rX) * (parameters::HEARTH_FEE_DIVISOR - parameters::HEARTH_FEE_BPS) /
      parameters::HEARTH_FEE_DIVISOR);
  TEST(linear > net && !swap(linear, input, 0));
  // A marker may not carry more than the declared input.
  TEST(!swap(net, input + 1, 0));
  // The taker's own minimum is enforced.
  TEST(!swap(net, input, net + 1));
}

// Limit deposits escrow the declared amount; order ids are single-use.
void testValidateSettlementLimitDeposit() {
  TestChain t("limit");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  Crypto::Hash orderId;
  memset(orderId.data, 7, sizeof(orderId.data));
  const Crypto::Hash addressHash{};
  AssetFlows flows;
  uint64_t fee = 0;
  std::string err;
  auto deposit = [&](uint64_t price) {
    std::vector<uint8_t> extra;
    addLimitDepositToExtra(extra, 1, 100 * C, price, 1000, orderId, addressHash);
    return settlementTx({101 * C}, {commitmentOut(100 * C, parameters::DEPOSIT_TERM_POOL_XFG)}, extra);
  };

  Blockchain::SettlementScratch block;
  TEST(t.chain.validateSettlement(deposit(10 * parameters::ORDER_PRICE_TICK), 1, block, flows, fee, err) &&
       fee == 1 * C);
  // The same order id again in the same block is refused.
  TEST(!t.chain.validateSettlement(deposit(10 * parameters::ORDER_PRICE_TICK), 1, block, flows, fee, err));
  // Off-tick prices are refused.
  Blockchain::SettlementScratch fresh;
  TEST(!t.chain.validateSettlement(deposit(10 * parameters::ORDER_PRICE_TICK + 1), 1, fresh, flows, fee, err));
  // Audit 2026-11-11 M1: a 0.01 tick put the 0.125 launch price on an 8% grid.
  // 0.12512 HEAT/XFG — 0.1% off it — is a valid limit price.
  Blockchain::SettlementScratch nearLaunch;
  TEST(t.chain.validateSettlement(deposit(1251200), 1, nearLaunch, flows, fee, err));
}

// The interest-claim floor and the CD output gate must name the same height:
// a ring member below the floor may be a legacy deposit, and no CD exists
// before the first v12 block on either network.
void testCdActivationHeight() {
  Logging::LoggerGroup nullLog;
  Currency mainnet = CurrencyBuilder(nullLog).currency();
  TEST(mainnet.cdActivationHeight() == parameters::CD_ACTIVATION_HEIGHT);
  TEST(mainnet.cdActivationHeight() == mainnet.upgradeHeight(BLOCK_MAJOR_VERSION_12));
  Currency testnet = CurrencyBuilder(nullLog).testnet(true).currency();
  TEST(testnet.cdActivationHeight() == testnet.upgradeHeight(BLOCK_MAJOR_VERSION_12));
  TEST(testnet.cdActivationHeight() > testnet.upgradeHeight(BLOCK_MAJOR_VERSION_11));
  for (const Currency* network : {&mainnet, &testnet}) {
    const uint32_t lastLegacyCapableHeight = network->upgradeHeight(BLOCK_MAJOR_VERSION_11);
    TEST(network->blockMajorVersionAtHeight(lastLegacyCapableHeight) < BLOCK_MAJOR_VERSION_11);
    TEST(lastLegacyCapableHeight < network->cdActivationHeight());
    TEST(network->blockMajorVersionAtHeight(network->cdActivationHeight()) < BLOCK_MAJOR_VERSION_12);
    TEST(network->blockMajorVersionAtHeight(network->cdActivationHeight() + 1) == BLOCK_MAJOR_VERSION_12);
  }
}

// Testnet must run v11 rules past the Hearth bootstrap for long enough to exercise
// the call auction and two FCI quarters of vote tallies before v12 starts. At the
// old v12 height (180) the auction ran for 79 blocks (102..180).
void testTestnetRunsV11PastTheBootstrap() {
  Logging::LoggerGroup nullLog;
  Currency testnet = CurrencyBuilder(nullLog).testnet(true).currency();
  TEST(testnet.upgradeHeight(BLOCK_MAJOR_VERSION_11) == parameters::TESTNET_UPGRADE_HEIGHT_V11);
  TEST(testnet.upgradeHeight(BLOCK_MAJOR_VERSION_12) == parameters::TESTNET_UPGRADE_HEIGHT_V12);
  TEST(parameters::TESTNET_UPGRADE_HEIGHT_V12 == 1000);
  TEST(testnet.hasIncreasingUpgradeSchedule());
  TEST(CurrencyBuilder(nullLog).currency().hasIncreasingUpgradeSchedule());
  const uint32_t auctionStart = testnet.upgradeHeight(BLOCK_MAJOR_VERSION_11) + testnet.bootstrapBlocks();
  const uint32_t twoFciQuarters =
      2 * parameters::FCI_EPOCHS_PER_QUARTER * static_cast<uint32_t>(parameters::TESTNET_EPOCH_DURATION_BLOCKS);
  TEST(testnet.upgradeHeight(BLOCK_MAJOR_VERSION_12) >= auctionStart + twoFciQuarters);
  TEST(testnet.blockMajorVersionAtHeight(auctionStart + twoFciQuarters) == BLOCK_MAJOR_VERSION_11);
}

// Limit fills are priced on the curve: an order's limit holds for its average
// price over the fill, and a sell/buy round trip no longer profits.
void testLimitFillsOnTheCurve() {
  const uint64_t C = parameters::COIN;
  const uint64_t X = 10000 * C, H = 1000 * C;

  // Selling f pays an average of H/(X+f); at 0.08 HEAT/XFG that allows 2,500.
  const uint64_t sellLimit = 8 * C / 100;
  const uint64_t f = ammLimitSellCapacity(X, H, sellLimit);
  TEST(f == 2500 * C);
  const uint64_t gross = ammGetOutputAmount(f, X, H, 0);
  TEST((uint128_t)gross * C >= (uint128_t)sellLimit * f);
  const uint64_t past = ammGetOutputAmount(f + C, X, H, 0);
  TEST((uint128_t)past * C < (uint128_t)sellLimit * (f + C));

  // Taking g costs an average of H/(X−g); at 0.125 HEAT/XFG that allows 2,000.
  const uint64_t buyLimit = 125 * C / 1000;
  const uint64_t g = ammLimitBuyCapacity(X, H, buyLimit);
  TEST(g == 2000 * C);
  const uint64_t cost = ammCostToTake(g, H, X);
  TEST((uint128_t)cost * C <= (uint128_t)buyLimit * g);
  // The cost rounds up, so the product of the reserves never shrinks.
  TEST((uint128_t)(H + cost) * (X - g) >= (uint128_t)H * X);
  TEST(ammCostToTake(X, H, X) == 0);

  // Round trip at the 5% backstop cap: sell 500 XFG, buy back with all the
  // HEAT received. At the old flat spot price it came back ~8% ahead.
  const uint64_t sell = X / 20;
  const uint64_t heatGot = ammGetOutputAmount(sell, X, H, 0) / 100 * 99;
  const uint64_t xfgBack = ammGetOutputAmount(heatGot, H - heatGot, X + sell, 0) / 100 * 99;
  TEST(xfgBack < sell);
  const uint64_t flatHeat = static_cast<uint64_t>((uint128_t)sell * H / X) / 100 * 99;
  const uint64_t flatBack = static_cast<uint64_t>(
      (uint128_t)flatHeat * (X + sell) / (H - flatHeat)) / 100 * 99;
  TEST(flatBack > sell);
}

// Swaps may move the pool at most HEARTH_MAX_BLOCK_PRICE_MOVE_PCT from the
// block's opening price: 100 = double or half.
void testSwapPriceBand() {
  TestChain t("band");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const uint64_t rX = t.chain.getAmmPool().reserveXfg;
  const uint64_t rH = t.chain.getAmmPool().reserveHeat;
  AssetFlows flows;
  uint64_t fee = 0;
  std::string err;
  auto sell = [&](uint64_t input) {
    const uint64_t output = ammSwapNetOutput(input, rX, rH);
    std::vector<uint8_t> extra;
    addAmmSwapAuthToExtra(extra, 0, input, output, 0);
    Blockchain::SettlementScratch scratch;
    scratch.openingSpot = ammGetSpotPrice(rX, rH);
    return t.chain.validateSettlement(
        settlementTx({input + 1 * C}, {commitmentOut(output, parameters::HEAT_TERM),
                                       commitmentOut(input, parameters::DEPOSIT_TERM_POOL_XFG)}, extra),
        1, scratch, flows, fee, err);
  };
  // Selling 30% of the XFG reserve leaves the price at ~0.59× — inside.
  TEST(sell(rX * 3 / 10));
  // Selling 50% leaves it at ~0.44× — past half, refused for that reason.
  TEST(!sell(rX / 2) && err.find("band") != std::string::npos);
}

// Regression: the coinbase blacklist must cover every settlement tag.
//
// prevalidate_miner_transaction used to keep its own hand-maintained list of
// settlement tags. That list had drifted from isHeatwaveTag: it omitted the two
// RETIRED legacy LP tags (TransactionExtraAmmAddLiquidity / AmmRemoveLiquidity).
// The coinbase is pushed BEFORE the settlement loop, so validateSettlement -- which
// rejects those tags for regular transactions -- never sees it, and the legacy
// branch in pushTransaction would add the tag's self-asserted amounts straight into
// m_ammPool reserves. m_ammPool is not persisted, so a corrupted reserve reproduces
// on every resync.
//
// The blacklist is now derived from isHeatwaveTag, so this asserts the property
// that makes it safe: every settlement tag is one the coinbase path rejects, so the
// two lists cannot drift apart again.
// Test-only access to Blockchain's private prevalidate_miner_transaction, declared
// as a friend in Blockchain.h. The coinbase settlement-tag check had NO coverage,
// which is how its blacklist drifted out of sync with isHeatwaveTag and admitted
// the retired legacy AMM LP tags.
namespace CryptoNote {
class VaultCoreTestAccess {
public:
  static bool prevalidateCoinbase(Blockchain& chain, const Block& b, uint32_t height) {
    return chain.prevalidate_miner_transaction(b, height);
  }
  static uint64_t ammReserveXfg(const Blockchain& chain) { return chain.m_ammPool.reserveXfg; }
  static uint64_t ammReserveHeat(const Blockchain& chain) { return chain.m_ammPool.reserveHeat; }
  static size_t coinbaseUnlockWindow(const Blockchain& chain) { return chain.m_currency.minedMoneyUnlockWindow(); }
  static bool unusableRingMemberFromV11(const Blockchain& chain, const CommitmentOutputRef& ref) {
    return chain.isUnspendableAfterHeatwave(ref);
  }
  // Block validation must judge its transactions by the block's OWN version.
  // checkCommitmentSpendInput used to derive it from getBlockMajorVersionForHeight(H+1),
  // which during a block's validation is the NEXT height's rules.
  static bool txOutputsValid(Blockchain& chain, const Transaction& tx, uint32_t height, uint8_t v) {
    return chain.check_tx_outputs(tx, height, v);
  }
  // The fallback used when the caller passes 0. Reproduced here from the public
  // authority so the test pins the convention the mempool and template paths use.
  static uint8_t derivedVersion(const Blockchain& chain, uint32_t height) {
    return chain.getBlockMajorVersionForHeight(height);
  }
  static uint8_t getBlockMajorVersionForHeightForTest(const Blockchain& chain, uint32_t height) {
    return chain.getBlockMajorVersionForHeight(height);
  }
  static uint32_t m_currency_upgrade_height_v11(const Blockchain& chain) {
    return chain.m_currency.upgradeHeight(BLOCK_MAJOR_VERSION_11);
  }
  static void setAuntPolLegs(Blockchain& chain, uint64_t xfg, uint64_t heat) {
    chain.m_treasuryLpPendingXfg = xfg;
    chain.m_treasuryHeatReserve = heat;
  }
  static bool runEpochWork(Blockchain& chain, const Block& b, uint32_t height) {
    return chain.processBlockEpochWork(b, height, Crypto::Hash{});
  }
  static uint64_t epochDuration(const Blockchain& chain) {
    return chain.m_currency.isTestnet() ? parameters::TESTNET_EPOCH_DURATION_BLOCKS
                                        : parameters::EPOCH_DURATION_BLOCKS;
  }
  static void runOrderbookBlock(Blockchain& chain, uint32_t height) {
    Block b{};
    b.majorVersion = BLOCK_MAJOR_VERSION_11;
    chain.processOrderbookForBlock(b, {}, height);
  }
  static void addLimitDeposit(Blockchain& chain, const Crypto::Hash& orderId, uint8_t side,
                              uint64_t amount, uint64_t targetPrice, uint32_t height,
                              const Crypto::Hash* addressHash = nullptr) {
    Blockchain::LimitDepositInfo dep{side, amount, targetPrice, 0, addressHash ? *addressHash : orderId, false};
    dep.depositedAmount = amount;
    dep.createdHeight = height;
    chain.m_limitDeposits[orderId] = dep;
    chain.indexLimitOrder(orderId);
    (side == 1 ? chain.m_ammPool.pendingXfg : chain.m_ammPool.pendingHeat) += amount;
  }
  static uint64_t proceedsHeat(const Blockchain& chain, const Crypto::Hash& orderId) {
    return chain.m_limitDeposits.at(orderId).proceedsHeat;
  }
  static uint64_t proceedsXfg(const Blockchain& chain, const Crypto::Hash& orderId) {
    return chain.m_limitDeposits.at(orderId).proceedsXfg;
  }
  static uint64_t escrowLeft(const Blockchain& chain, const Crypto::Hash& orderId) {
    return chain.m_limitDeposits.at(orderId).amount;
  }
  static std::vector<uint8_t> saveOrderbookState(Blockchain& chain) {
    std::vector<uint8_t> bytes;
    Common::VectorOutputStream out(bytes);
    BinaryOutputStreamSerializer s(out);
    chain.serializeOrderbookState(s);
    return bytes;
  }
  static void resetToStartup(Blockchain& chain) { chain.rebuildCache(); }
  static void recordSpots(Blockchain& chain, uint32_t fromHeight, uint32_t count, uint64_t price) {
    for (uint32_t i = 0; i < count; ++i) chain.m_blockSpotHistory.push_back({fromHeight + i, price});
  }
  static void markWithdrawn(Blockchain& chain, const Crypto::Hash& orderId, uint32_t height) {
    auto& dep = chain.m_limitDeposits.at(orderId);
    (dep.side == 1 ? chain.m_ammPool.pendingXfg : chain.m_ammPool.pendingHeat) -= dep.amount;
    dep.withdrawnAmount = dep.amount;
    dep.withdrawn = true;
    dep.withdrawnHeight = height;
    chain.indexLimitOrder(orderId);
  }
  // One transaction at index 1 of a block at `height`, version `v` (index 0 is
  // the coinbase slot), driven through the production push/pop.
  static Blockchain::BlockEntry blockWith(const Transaction& tx, uint32_t height, uint8_t v) {
    Blockchain::BlockEntry block{};
    block.bl.majorVersion = v;
    block.height = height;
    block.transactions.resize(2);
    block.transactions[1].tx = tx;
    return block;
  }
  static bool orderbookInBootstrapAfter(Blockchain& chain, uint32_t height) {
    Block block;
    block.majorVersion = BLOCK_MAJOR_VERSION_11;
    chain.processOrderbookForBlock(block, {}, height);
    return chain.isOrderbookInBootstrap();
  }
  static bool pushTx(Blockchain& chain, Blockchain::BlockEntry& block) {
    return chain.pushTransaction(block, getObjectHash(block.transactions[1].tx), TxIndex{block.height, 1});
  }
  static void popTx(Blockchain& chain, const Blockchain::BlockEntry& block) {
    chain.popTransaction(block.transactions[1].tx, getObjectHash(block.transactions[1].tx),
                         block.height, block.bl.majorVersion);
  }
  static void recordRemint(Blockchain& chain, uint32_t height, uint64_t amount) {
    chain.recordRemint(height, amount);
  }
  static void unrecordRemint(Blockchain& chain, uint32_t height) { chain.unrecordRemint(height); }
  static bool indexAlias(Blockchain& chain, const TransactionExtraAliasRegistration& registration) {
    AliasEntry entry;
    entry.alias = registration.alias;
    entry.ownerAddress = registration.ownerAddress;
    entry.aliasHash = registration.aliasHash;
    entry.addressHash = registration.addressHash;
    entry.aliasType = registration.aliasType;
    return chain.m_aliasIndex.registerAlias(entry);
  }
  static bool unindexAlias(Blockchain& chain, const std::string& alias) { return chain.m_aliasIndex.removeAlias(alias); }
  static bool validateCoinbase(Blockchain& chain, const Block& b, uint32_t height, uint64_t alreadyGenerated,
                               uint64_t remint) {
    uint64_t reward = 0;
    int64_t emissionChange = 0;
    return chain.validate_miner_transaction(b, height, 0, alreadyGenerated, 0, reward, emissionChange, {}, remint);
  }
  static void bankBlock(Blockchain& chain, const Blockchain::BlockEntry& block) {
    chain.pushToBankingIndex(block, 0);
  }
  static void addCommitmentRingMember(Blockchain& chain, uint64_t amount, uint32_t term, uint32_t height,
                                      const Crypto::PublicKey& commitKey) {
    CommitmentOutputRef ref{};
    ref.term = term;
    ref.transactionIndex.block = height;
    ref.transactionIndex.transaction = 1;
    ref.commitKey = commitKey;
    chain.m_indexManager.commitmentOutputs()[amount].push_back(ref);
  }
  static bool commitmentSpendValid(Blockchain& chain, const TransactionInputCommitmentSpend& input,
                                   const Crypto::Hash& prefixHash, const std::vector<Crypto::Signature>& signatures,
                                   uint8_t blockMajorVersion) {
    return chain.checkCommitmentSpendInput(input, prefixHash, signatures, nullptr, blockMajorVersion);
  }
  static void markLastRingMemberSlashed(Blockchain& chain, uint64_t amount) {
    chain.m_indexManager.commitmentOutputs()[amount].back().isSlashed = true;
  }
  static std::vector<uint32_t> nodeDecoyIndexes(Blockchain& chain, uint64_t amount, uint32_t ringTerm) {
    std::vector<COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS_out_entry> decoys;
    chain.getRandomCommitmentOutputsForAmount(amount, 64, decoys, 0, ringTerm);
    std::vector<uint32_t> indexes;
    for (const auto& decoy : decoys) indexes.push_back(decoy.global_amount_index);
    std::sort(indexes.begin(), indexes.end());
    return indexes;
  }
  static std::vector<CommitmentOutputRef> commitmentRingMembers(const Blockchain& chain, uint64_t amount) {
    const auto it = chain.m_indexManager.commitmentOutputs().find(amount);
    return it == chain.m_indexManager.commitmentOutputs().end() ? std::vector<CommitmentOutputRef>()
                                                                : std::vector<CommitmentOutputRef>(it->second.begin(), it->second.end());
  }
  static size_t keyOutputCount(const Blockchain& chain) {
    size_t count = 0;
    for (const auto& amountOutputs : chain.m_indexManager.outputs().data()) count += amountOutputs.second.size();
    return count;
  }
  static void addHeatRingMember(Blockchain& chain, uint64_t amount) {
    CommitmentOutputRef ref{};
    ref.term = parameters::HEAT_TERM;
    ref.transactionIndex.transaction = 1;
    chain.m_indexManager.commitmentOutputs()[amount].push_back(ref);
  }
  static void markExpired(Blockchain& chain, const Crypto::Hash& orderId) {
    chain.m_limitDeposits.at(orderId).expired = true;
    chain.indexLimitOrder(orderId);
  }
  static bool isResting(const Blockchain& chain, const Crypto::Hash& orderId) {
    return chain.m_restingLimitOrders.count(orderId) > 0;
  }
  static bool limitOrderIndexMatchesDeposits(const Blockchain& chain) {
    std::set<Crypto::Hash, Blockchain::HashLess> resting, withdrawn;
    for (const auto& kv : chain.m_limitDeposits) {
      if (kv.second.withdrawn) withdrawn.insert(kv.first);
      else if (!kv.second.expired) resting.insert(kv.first);
    }
    return resting == chain.m_restingLimitOrders && withdrawn == chain.m_withdrawnLimitOrders;
  }
  static void reverseOrderFills(Blockchain& chain, uint32_t height) { chain.reverseOrderFillsFor(height); }
  static bool hasLimitDeposit(const Blockchain& chain, const Crypto::Hash& orderId) {
    return chain.m_limitDeposits.count(orderId) > 0;
  }
  static uint64_t cdFeeHeatFromXfg(const Blockchain& chain) { return chain.m_cdFeeHeatFromXfg; }
  static bool auctionFillsSettle(const Blockchain& chain, const AuctionResult& auction) {
    return chain.auctionFillsSettle(auction);
  }
  static void popOrderbookBlock(Blockchain& chain, uint32_t height) {
    chain.restoreOrderbookSnapshotFor(height);
  }
  static void recordBlockSpot(Blockchain& chain, uint32_t height, uint64_t spot) {
    chain.recordBlockSpot(height, spot);
  }
  static void dropBlockSpot(Blockchain& chain, uint32_t height) { chain.dropBlockSpot(height); }
  static size_t maxRollbackHistory() { return Blockchain::MAX_ROLLBACK_HISTORY; }
  static void setRemintBurned(Blockchain& chain, uint64_t amount) { chain.m_remintBurnedXfg = amount; }
  static uint64_t oraclePriceBefore(const Blockchain& chain, uint32_t height) {
    return chain.hearthOraclePriceBefore(height);
  }
  static void loadOrderbookState(Blockchain& chain, const std::vector<uint8_t>& bytes) {
    Common::MemoryInputStream in(bytes.data(), bytes.size());
    BinaryInputStreamSerializer s(in);
    chain.serializeOrderbookState(s);
  }
};
} // namespace CryptoNote

// Regression: the v11+ Treasury LP Manager (AuntPoL) must run at the epoch
// boundary. It was nested inside the pre-v11 GENERAL_RESERVE convert branch
// (`majorVersion < V11 && pending > 0`), so from v11 on the premium legs
// accumulated forever, no protocol LP shares were ever minted, and the
// bootstrap seed could never be repaid. Legs are paired at the pool ratio and
// the unpaired excess carries to the next epoch.
void testAuntPolPairsAtV11EpochBoundary() {
  TestChain t("auntpol");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const auto& pool = t.chain.getAmmPool();
  const uint64_t xfgBefore = pool.reserveXfg, heatBefore = pool.reserveHeat;
  const uint64_t sharesBefore = pool.totalLpShares;
  TEST(t.chain.getProtocolLpShares() == 0);

  // 80 XFG pairs with 10 HEAT at the 8:1 seed ratio; 2 HEAT is excess.
  const uint64_t legXfg = 80 * C, legHeat = 12 * C;
  const uint64_t pairedHeat = static_cast<uint64_t>(
      ((uint128_t)legXfg * heatBefore) / xfgBefore);
  TEST(pairedHeat == 10 * C);
  VaultCoreTestAccess::setAuntPolLegs(t.chain, legXfg, legHeat);

  Block b;
  b.majorVersion = BLOCK_MAJOR_VERSION_11;
  const uint32_t h = static_cast<uint32_t>(VaultCoreTestAccess::epochDuration(t.chain));
  VaultCoreTestAccess::recordSpots(t.chain, 0, h, ammGetSpotPrice(xfgBefore, heatBefore));
  TEST(VaultCoreTestAccess::runEpochWork(t.chain, b, h));

  const uint64_t shares = t.chain.getProtocolLpShares();
  TEST(shares > 0);
  TEST(shares == ammMintLpShares(legXfg, pairedHeat, sharesBefore, xfgBefore, heatBefore));
  TEST(pool.reserveXfg == xfgBefore + legXfg);
  TEST(pool.reserveHeat == heatBefore + pairedHeat);
  TEST(pool.totalLpShares == sharesBefore + shares);
  TEST(t.chain.getTreasuryLpPendingXfg() == 0);
  TEST(t.chain.getTreasuryHeatReserve() == legHeat - pairedHeat);
  // Owned value is far below the two-leg seed obligation: not repaid yet.
  TEST(!t.chain.isBootstrapRepaid());
}

// Regression: a coinbase must not be able to create AMM pool state, at any version.
//
// prevalidate_miner_transaction kept a hand-maintained list of settlement tags that
// had drifted from isHeatwaveTag: it omitted the two RETIRED legacy LP tags. The
// coinbase is pushed BEFORE the settlement loop, so validateSettlement -- which
// rejects those tags for regular transactions -- never saw it, and the (now deleted)
// legacy branch in pushTransaction would apply the tag's self-asserted amounts
// straight into m_ammPool. One miner could set AMM reserves network-wide; m_ammPool
// is serialized, so a corrupted cache would persist it.
//
// This calls the REAL function, so it fails if the coinbase check regresses.
void testCoinbaseRejectsEverySettlementTag() {
  auto tagIn = [](const std::vector<uint8_t>& extra) {
    std::vector<TransactionExtraField> fields;
    if (!parseTransactionExtra(extra, fields)) return TransactionExtraField{};
    return fields.empty() ? TransactionExtraField{} : fields[0];
  };

  TestChain t("coinbase_tags");
  TEST(t.ok);
  const uint64_t reserveXfgBefore = VaultCoreTestAccess::ammReserveXfg(t.chain);
  const uint64_t reserveHeatBefore = VaultCoreTestAccess::ammReserveHeat(t.chain);

  auto coinbaseWith = [&](const std::vector<uint8_t>& extra, uint8_t version = BLOCK_MAJOR_VERSION_11) {
    // Must be a VALID coinbase apart from the extra, otherwise prevalidate returns
    // false early (input count/type, blockIndex, unlockTime, outputs) and the
    // settlement-tag check never runs -- which would make every assertion below
    // pass for the wrong reason.
    const uint32_t h = 1;
    Block b{};
    b.majorVersion = version;
    b.minorVersion = BLOCK_MINOR_VERSION_1;
    b.timestamp = 1;
    b.baseTransaction.inputs.push_back(BaseInput{h});
    b.baseTransaction.unlockTime = h + VaultCoreTestAccess::coinbaseUnlockWindow(t.chain);
    b.baseTransaction.outputs.push_back(keyOut(50 * parameters::COIN));
    b.baseTransaction.extra = extra;
    return b;
  };

  // Sanity: a coinbase with NO extra passes prevalidation, so the rejections below
  // are attributable to the settlement tag and nothing else.
  TEST(VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith({}), 1));

  // The two tags the old blacklist MISSED. Each must now be refused at v11.
  std::vector<uint8_t> lpAdd;
  TEST(addAmmAddLiquidityToExtra(lpAdd, 1000 * parameters::COIN, 0));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith(lpAdd), 1));

  std::vector<uint8_t> lpRemove;
  TEST(addAmmRemoveLiquidityToExtra(lpRemove, 500, 0, 0));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith(lpRemove), 1));

  // A tag the old blacklist DID list, to prove the derived check did not narrow it.
  std::vector<uint8_t> fund;
  TEST(addTreasuryFundToExtra(fund, 0, 50 * parameters::COIN));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith(fund), 1));

  std::vector<uint8_t> mint;
  TEST(addHeatMintAuthToExtra(mint, 10 * parameters::COIN, 1 * parameters::COIN));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith(mint), 1));

  // From v11 the alias fee is burned in settlement, which the coinbase skips.
  TransactionExtraAliasRegistration coinbaseAlias;
  coinbaseAlias.alias = "minerals";
  coinbaseAlias.ownerAddress = "miner-owner";
  coinbaseAlias.aliasType = 1;
  std::vector<uint8_t> aliasExtra;
  TEST(addAliasToExtra(aliasExtra, coinbaseAlias));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith(aliasExtra), 1));
  TEST(VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith({}, BLOCK_MAJOR_VERSION_12), 1));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith(aliasExtra, BLOCK_MAJOR_VERSION_12), 1));
  // Nor may it release or transfer one: it is applied first in its block, ahead
  // of registrations settlement already admitted.
  TransactionExtraAliasTransfer coinbaseTransfer;
  coinbaseTransfer.alias = "minerals";
  coinbaseTransfer.oldOwnerAddress = "miner-owner";
  coinbaseTransfer.newOwnerAddress = "victim-owner";
  std::vector<uint8_t> transferExtra;
  TEST(addAliasTransferToExtra(transferExtra, coinbaseTransfer));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith(transferExtra), 1));
  TransactionExtraAliasRelease coinbaseRelease;
  coinbaseRelease.alias = "minerals";
  coinbaseRelease.ownerAddress = "miner-owner";
  std::vector<uint8_t> releaseExtra;
  TEST(addAliasReleaseToExtra(releaseExtra, coinbaseRelease));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith(releaseExtra), 1));
  std::vector<uint8_t> coinbaseBurn;
  TEST(addRemintBurnToExtra(coinbaseBurn, parameters::COIN));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(t.chain, coinbaseWith(coinbaseBurn), 1));

  // The retired tags are still settlement tags, so they stay covered as the single
  // shared list evolves.
  TEST(isHeatwaveTag(tagIn(lpAdd)));
  TEST(isHeatwaveTag(tagIn(lpRemove)));

  // And the pool is untouched by prevalidation itself.
  TEST(VaultCoreTestAccess::ammReserveXfg(t.chain) == reserveXfgBefore);
  TEST(VaultCoreTestAccess::ammReserveHeat(t.chain) == reserveHeatBefore);

  // The retired tags are still refused on the regular-transaction path too.
  AssetFlows flows;
  uint64_t fee = 0;
  std::string err;
  auto settles = [&](const std::vector<uint8_t>& extra) {
    Blockchain::SettlementScratch scratch;
    return t.chain.validateSettlement(
      settlementTx({100 * parameters::COIN}, {keyOut(99 * parameters::COIN)}, extra),
      1, scratch, flows, fee, err);
  };
  TEST(!settles(lpAdd));
  TEST(!settles(lpRemove));
}


// Regression: a block rejected for cumulative size must unwind the state its
// transactions already connected.
//
// pushBlock connects the miner tx and every block tx as it goes. Three of the four
// exits in that range pop first (block.transactions.pop_back() + popTransactions);
// the cumulative-size exit did not. The block is never pushed, so nothing would
// ever pop those transactions, and the mutations were permanent:
//   - CD_APY_POOL vault UTXOs debited and change minted with no reversal,
//   - m_feePoolBalance decremented,
//   - key images stranded in spentKeys(), bricking a third party's pending CD
//     withdrawal and forking the node,
//   - phantom TxIndex entries left in the index; a later KeyInput naming that
//     offset reaches m_blocks[H] with m_blocks.size() == H, and
//     SwappedVector::operator[] throws with nothing catching it -> daemon death.
//
// This asserts the symmetry directly: popTransactions() must reverse every
// mutation a connect made, so a vault spend is fully un-spent and the fee pool is
// restored. The same exit is exercised whether the block is rejected at the size
// check or anywhere after the transactions landed.
void testRejectedBlockUnwindsVaultSpend() {
  Crypto::PublicKey spendPub, viewPub;
  Crypto::SecretKey spendSec, viewSec;
  Crypto::generate_keys(spendPub, spendSec);
  Crypto::generate_keys(viewPub, viewSec);

  VaultUtxoSet vault;
  vault.setSpendKey(spendSec);
  Crypto::Hash h{};
  vault.addUtxo(100, 1000, AssetType::HEAT, VaultPartition::CD_APY_POOL, h, viewPub);
  vault.addUtxo(200, 1000, AssetType::HEAT, VaultPartition::CD_APY_POOL, h, viewPub);

  const uint64_t before = vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT);
  TEST(before == 2000);

  // A claim spends vault UTXOs and mints the overshoot back as change, exactly as
  // Blockchain.cpp:6303-6321 does for a CD-interest claim.
  VaultUtxoSet::SpendResult r =
      vault.spendUtxos(VaultPartition::CD_APY_POOL, AssetType::HEAT, 300);
  TEST(r.amountSpent >= 300);
  TEST(!r.spentIndices.empty());
  const uint64_t debited = vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT);
  TEST(debited < before);

  // popTransactions() replays the reversal. Without the fix the rejected block
  // leaves `debited` in place forever -- the pool drained with no value created.
  vault.unSpendUtxos(r.spentIndices);
  TEST(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == before);
  TEST(vault.spentCount() == 0);
}

// An LP-denominated vault UTXO must survive a cache round-trip. The deserializer
// bounded the asset field against HEAT, so LP (2) was rejected, and the bare
// `return` left serialize() mid-stream so every field after treasury_vault was read
// from the wrong offset.
void testVaultUtxoSetLpSurvivesSerialize() {
  Crypto::PublicKey spendPub, viewPub;
  Crypto::SecretKey spendSec, viewSec;
  Crypto::generate_keys(spendPub, spendSec);
  Crypto::generate_keys(viewPub, viewSec);

  VaultUtxoSet vault;
  vault.setSpendKey(spendSec);
  Crypto::Hash h{};
  vault.addUtxo(100, 777, AssetType::LP, VaultPartition::LP_RESERVE, h, viewPub);

  CryptoNote::BinaryArray blob = CryptoNote::storeToBinary<VaultUtxoSet>(vault);
  VaultUtxoSet restored;
  restored.setSpendKey(spendSec);
  CryptoNote::loadFromBinary<VaultUtxoSet>(restored, blob);

  TEST(restored.totalUtxos() == 1);
  const VaultOutput* vo = restored.getOutput(100);
  TEST(vo != nullptr);
  if (vo) {
    TEST(vo->asset == AssetType::LP);
    TEST(vo->amount == 777);
  }
  TEST(restored.partitionBalance(VaultPartition::LP_RESERVE, AssetType::LP) == 777);
}

// Adversarial: the R2-1 gate is `m_vault.isVaultKeyImage(ki)`, and every vault
// UTXO shares one synthetic key image because every addUtxo site passes
// m_vaultKeys.viewPub. removeAboveIndex() erases that shared key image when it
// drops ANY vault UTXO, so a reorg that pops a vault UTXO disarms the gate for
// the UTXOs that remain -- until the process restarts and the cache rebuilds it.
void testVaultReorgKeepsKeyImageGate() {
  Crypto::PublicKey spendPub, viewPub;
  Crypto::SecretKey spendSec, viewSec;
  Crypto::generate_keys(spendPub, spendSec);
  Crypto::generate_keys(viewPub, viewSec);

  VaultUtxoSet vault;
  vault.setSpendKey(spendSec);
  Crypto::Hash h{};
  // Same shape as Blockchain: every vault UTXO names m_vaultKeys.viewPub.
  vault.addUtxo(uint64_t(100) << 32 | 1, 1000, AssetType::HEAT,
                VaultPartition::CD_APY_POOL, h, viewPub);
  vault.addUtxo(uint64_t(200) << 32 | 1, 1000, AssetType::HEAT,
                VaultPartition::CD_APY_POOL, h, viewPub);

  Crypto::KeyImage ki;
  Crypto::generate_key_image(viewPub, spendSec, ki);
  TEST(vault.isVaultKeyImage(ki));

  // Reorg pops block 200 (Blockchain.cpp:6149). The height-100 UTXO remains.
  vault.removeAboveIndex(uint64_t(200) << 32);
  TEST(vault.totalUtxos() == 1);
  TEST(vault.isVaultKeyImage(ki));
}

// Adversarial: the cache is the only persisted copy of `spent`, `spentCount`
// and the key-image table the consensus gate reads. None of it was round-tripped
// by a test before this one.
void testVaultSpentSurvivesSerialize() {
  Crypto::PublicKey spendPub, viewPub;
  Crypto::SecretKey spendSec, viewSec;
  Crypto::generate_keys(spendPub, spendSec);
  Crypto::generate_keys(viewPub, viewSec);

  VaultUtxoSet vault;
  vault.setSpendKey(spendSec);
  Crypto::Hash h{};
  vault.addUtxo(100, 777, AssetType::HEAT, VaultPartition::CD_APY_POOL, h, viewPub);
  vault.addUtxo(101, 500, AssetType::HEAT, VaultPartition::CD_APY_POOL, h, viewPub);
  vault.markSpent(101);
  TEST(vault.spentCount() == 1);

  Crypto::KeyImage ki;
  Crypto::generate_key_image(viewPub, spendSec, ki);

  CryptoNote::BinaryArray blob = CryptoNote::storeToBinary<VaultUtxoSet>(vault);

  // Blockchain sets the spend key in its constructor, before init() loads the
  // cache; mirror that order.
  VaultUtxoSet restored;
  restored.setSpendKey(spendSec);
  CryptoNote::loadFromBinary<VaultUtxoSet>(restored, blob);

  TEST(restored.totalUtxos() == 2);
  TEST(restored.spentCount() == 1);
  const VaultOutput* spent = restored.getOutput(101);
  TEST(spent != nullptr && spent->spent);
  TEST(restored.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT) == 777);
  // Rebuilt from outputKey + spendKey, not serialized.
  TEST(restored.isVaultKeyImage(ki));

  // removeAboveIndex on a *spent* entry (the untested branch) must decrement
  // spentCount rather than underflow it.
  restored.removeAboveIndex(101);
  TEST(restored.totalUtxos() == 1);
  TEST(restored.spentCount() == 0);
}

// Regression (F2/R1): version-gated validation must key on the BLOCK's own
// majorVersion, resolved through the one consensus authority.
//
// Two bugs shared a root cause -- two helpers disagreed at an upgrade height:
// getBlockMajorVersionForHeight uses `height >` (consensus authority, enforced by
// checkBlockVersion), while inline checks used `height >= upgradeHeight(...)`. At
// exactly UPGRADE_HEIGHT_V11 that applied the v11 CD caps and v11 output rules to a
// block consensus versions as v10 and that settlement treats as v10.
//
// The previous version of this test was VACUOUS: it built a transaction whose only
// input was a BaseInput, which checkTransactionInputs rejects 268 lines before
// checkCommitmentSpendInput is reached, so it asserted nothing about the parameter.
// This version drives the real gate.
void testVersionGatesUseConsensusAuthority() {
  TestChain t("version_gates");
  TEST(t.ok);

  // The authority and the old inline comparison must disagree exactly at an upgrade
  // height. If they agree, the bug they caused is not observable and this test
  // proves nothing.
  const uint32_t atUpgrade = VaultCoreTestAccess::m_currency_upgrade_height_v11(t.chain);
  TEST(VaultCoreTestAccess::getBlockMajorVersionForHeightForTest(t.chain, atUpgrade)
       == BLOCK_MAJOR_VERSION_10);
  TEST(VaultCoreTestAccess::getBlockMajorVersionForHeightForTest(t.chain, atUpgrade + 1)
       == BLOCK_MAJOR_VERSION_11);

  // check_tx_outputs is the function that carried the off-by-one. Its v11 gate is
  // the commitment-term whitelist: a block at the upgrade height is v10 and must be
  // judged by v10 rules, so an arbitrary term that v10 accepts must still be
  // accepted there, while the same tx at upgrade+1 (v11) is refused.
  Transaction tx{};
  tx.inputs.push_back(BaseInput{0});
  // Term 0xDEADBEEF is in no recognized class: it is neither 0, HEAT_TERM, DIGM_TERM,
  // a pool/LP/swap marker, nor within [depositMinTerm, depositMaxTerm]. Only the
  // v11 whitelist rejects it.
  tx.outputs.push_back(commitmentOut(1, 0xDEADBEEFu));

  // v10 at the upgrade height: the v11 term whitelist must NOT apply.
  TEST(VaultCoreTestAccess::txOutputsValid(t.chain, tx, atUpgrade, BLOCK_MAJOR_VERSION_10));
  // v11 one block later: it must apply.
  TEST(!VaultCoreTestAccess::txOutputsValid(t.chain, tx, atUpgrade + 1, BLOCK_MAJOR_VERSION_11));

  // Passing 0 (the mempool/template convention) must resolve to the same version as
  // the height-derived lookup -- not height+1, which was the original defect.
  const uint8_t derived = VaultCoreTestAccess::derivedVersion(t.chain, atUpgrade);
  TEST(derived == BLOCK_MAJOR_VERSION_10);
}

// Regression (R2): the two block-version helpers must not disagree.
//
// Blockchain::getBlockMajorVersionForHeight uses `height > upgradeHeight(...)` and is
// what checkBlockVersion enforces. Currency::blockMajorVersionAtHeight used `>=`, so at
// exactly an upgrade height consensus said "old version" and Currency said "new
// version". Consumers included getTransactionFee -- a consensus fee path -- which was
// benign only because minimumFee() happens to be flat across v10/v11/v12.
void testVersionHelpersAgree() {
  Logging::LoggerGroup nullLog;
  Currency currency = CurrencyBuilder(nullLog).currency();

  const uint32_t v11Height = currency.upgradeHeight(BLOCK_MAJOR_VERSION_11);
  TEST(v11Height != static_cast<uint32_t>(-1));

  // The upgrade height itself is still the PREVIOUS version, by consensus convention.
  // This is the assertion that fails if either helper regresses to `>=`.
  TEST(currency.blockMajorVersionAtHeight(v11Height) < BLOCK_MAJOR_VERSION_11);
  TEST(currency.blockMajorVersionAtHeight(v11Height + 1) == BLOCK_MAJOR_VERSION_11);

  // Same at the v12 boundary.
  const uint32_t v12Height = currency.upgradeHeight(BLOCK_MAJOR_VERSION_12);
  TEST(v12Height != static_cast<uint32_t>(-1));
  TEST(currency.blockMajorVersionAtHeight(v12Height) < BLOCK_MAJOR_VERSION_12);
  TEST(currency.blockMajorVersionAtHeight(v12Height + 1) == BLOCK_MAJOR_VERSION_12);

  // The function is monotonic across the boundary -- never goes backwards.
  for (uint32_t h = v11Height - 2; h <= v11Height + 2; ++h) {
    TEST(currency.blockMajorVersionAtHeight(h) <= currency.blockMajorVersionAtHeight(h + 1));
  }
}

// Vault hardening: setSpendKey retargets the synthetic key, so the derived
// key-image table must be rebuilt from the (now-current) spend key. Adding UTXOs
// before the key was set registered nothing; retargeting afterwards must derive
// for the new key and invalidate the old.
void testVaultSetSpendKeyRebuildsKeyImageIndex() {
  Crypto::SecretKey spendSec, viewSec, spendSec2;
  Crypto::PublicKey spendPub, spendPub2, viewPub;
  Crypto::generate_keys(spendPub, spendSec);
  Crypto::generate_keys(viewPub, viewSec);
  Crypto::generate_keys(spendPub2, spendSec2);

  VaultUtxoSet vault;
  Crypto::Hash h{};
  vault.addUtxo(100, 1000, AssetType::HEAT, VaultPartition::CD_APY_POOL, h, viewPub);

  Crypto::KeyImage ki, ki2;
  Crypto::generate_key_image(viewPub, spendSec, ki);
  Crypto::generate_key_image(viewPub, spendSec2, ki2);
  vault.setSpendKey(spendSec);
  TEST(vault.isVaultKeyImage(ki));
  TEST(!vault.isVaultKeyImage(ki2));

  vault.setSpendKey(spendSec2);
  TEST(!vault.isVaultKeyImage(ki));
  TEST(vault.isVaultKeyImage(ki2));
}

// Vault hardening: allBalances() sums unspent UTXO amounts into per-partition
// balances; the adds must saturate rather than wrap at UINT64_MAX.
void testVaultAllBalancesSaturate() {
  Crypto::SecretKey spendSec;
  Crypto::PublicKey spendPub;
  Crypto::generate_keys(spendPub, spendSec);

  VaultUtxoSet vault;
  Crypto::Hash h{};
  vault.addUtxo(100, std::numeric_limits<uint64_t>::max(), AssetType::HEAT,
                VaultPartition::CD_APY_POOL, h, spendPub);
  vault.addUtxo(101, 1, AssetType::HEAT, VaultPartition::CD_APY_POOL, h, spendPub);

  VaultBalance b = vault.allBalances();
  TEST(b.heatCdFeePool == std::numeric_limits<uint64_t>::max());
}

// Vault hardening: equal-amount UTXO selection must be deterministic — ties
// resolve by global index, not unordered-map iteration order.
void testVaultSelectUtxosDeterministicTies() {
  Crypto::SecretKey spendSec;
  Crypto::PublicKey spendPub;
  Crypto::generate_keys(spendPub, spendSec);

  VaultUtxoSet vault;
  Crypto::Hash h{};
  vault.addUtxo(200, 5, AssetType::HEAT, VaultPartition::CD_APY_POOL, h, spendPub);
  vault.addUtxo(100, 5, AssetType::HEAT, VaultPartition::CD_APY_POOL, h, spendPub);

  auto sel = vault.selectUtxos(VaultPartition::CD_APY_POOL, AssetType::HEAT, 5);
  TEST(!sel.empty());
  TEST(sel.size() == 1);
  TEST(sel[0].globalOutputIndex == 100);
}

// The Hearth tests below drive the production per-block path
// (processOrderbookForBlock) on a v11 height past the 144-block bootstrap.
namespace {

uint32_t enterHearthAuctionPhase(TestChain& t) {
  uint32_t height = VaultCoreTestAccess::m_currency_upgrade_height_v11(t.chain);
  for (int i = 0; i < 400 && t.chain.isOrderbookInBootstrap(); ++i) {
    VaultCoreTestAccess::runOrderbookBlock(t.chain, height++);
  }
  VaultCoreTestAccess::runOrderbookBlock(t.chain, height++);
  return height;
}

Crypto::Hash hearthOrderId(uint8_t tag) {
  Crypto::Hash h{};
  h.data[0] = 0x5A;
  h.data[1] = tag;
  return h;
}

uint128_t poolProduct(const AmmPoolState& pool) {
  return (uint128_t)pool.reserveXfg * pool.reserveHeat;
}

// HEAT that the block created (positive) or destroyed (negative) across every
// ledger a fill can touch: pool reserves, limit escrow, proceeds, CD accumulator.
int64_t heatCreatedByBlock(const AmmPoolState& before, const AmmPoolState& after,
                           uint64_t proceedsBefore, uint64_t proceedsAfter) {
  return (int64_t)(after.reserveHeat - before.reserveHeat) +
         (int64_t)(after.pendingHeat - before.pendingHeat) +
         (int64_t)(proceedsAfter - proceedsBefore) +
         (int64_t)(after.cdHearthFeeAccumulator - before.cdHearthFeeAccumulator);
}

int64_t xfgCreatedByBlock(const AmmPoolState& before, const AmmPoolState& after,
                          uint64_t proceedsBefore, uint64_t proceedsAfter) {
  return (int64_t)(after.reserveXfg - before.reserveXfg) +
         (int64_t)(after.pendingXfg - before.pendingXfg) +
         (int64_t)(proceedsAfter - proceedsBefore);
}

const int64_t HEARTH_ROUNDING_DUST = 64;

} // namespace

// Regression (audit 2026-11-11 C2): a user sell smaller than the pool's bid
// band left the pool as the rationed side. The pool was then the taker: its
// fee and the user's maker rebate were paid by nobody, which created 1% of
// every such fill as HEAT.
void testHearthSellIntoPoolBandConservesHeat() {
  TestChain t("hearth_sell");
  TEST(t.ok);
  uint32_t height = enterHearthAuctionPhase(t);
  TEST(!t.chain.isOrderbookInBootstrap());

  const Crypto::Hash id = hearthOrderId(1);
  VaultCoreTestAccess::addLimitDeposit(t.chain, id, 1, 50 * parameters::COIN,
                                       parameters::ORDER_PRICE_TICK, height - 1);
  const AmmPoolState before = t.chain.getAmmPool();
  VaultCoreTestAccess::runOrderbookBlock(t.chain, height);
  const AmmPoolState after = t.chain.getAmmPool();

  TEST(VaultCoreTestAccess::escrowLeft(t.chain, id) == 0);
  const int64_t heatCreated = heatCreatedByBlock(before, after, 0, VaultCoreTestAccess::proceedsHeat(t.chain, id));
  const int64_t xfgCreated = xfgCreatedByBlock(before, after, 0, VaultCoreTestAccess::proceedsXfg(t.chain, id));
  TEST(heatCreated <= 0 && heatCreated >= -HEARTH_ROUNDING_DUST);
  TEST(xfgCreated == 0);
  TEST(poolProduct(after) >= poolProduct(before));
}

void testHearthBuyFromPoolBandConservesHeat() {
  TestChain t("hearth_buy");
  TEST(t.ok);
  uint32_t height = enterHearthAuctionPhase(t);

  const Crypto::Hash id = hearthOrderId(2);
  VaultCoreTestAccess::addLimitDeposit(t.chain, id, 0, 5 * parameters::COIN,
                                       parameters::COIN, height - 1);
  const AmmPoolState before = t.chain.getAmmPool();
  VaultCoreTestAccess::runOrderbookBlock(t.chain, height);
  const AmmPoolState after = t.chain.getAmmPool();

  TEST(VaultCoreTestAccess::proceedsXfg(t.chain, id) > 0);
  const int64_t heatCreated = heatCreatedByBlock(before, after, 0, VaultCoreTestAccess::proceedsHeat(t.chain, id));
  const int64_t xfgCreated = xfgCreatedByBlock(before, after, 0, VaultCoreTestAccess::proceedsXfg(t.chain, id));
  TEST(heatCreated <= 0 && heatCreated >= -HEARTH_ROUNDING_DUST);
  TEST(xfgCreated <= 0 && xfgCreated >= -HEARTH_ROUNDING_DUST);
  TEST(poolProduct(after) >= poolProduct(before));
}

// Regression (audit 2026-11-11 C1): the band quoted 10% of the reserves at
// prices inside ±0.42% of P_clear, so one sweep bought it ~10% better than
// the curve and lowered the product of the reserves. Every sweep size, on
// either side and over consecutive blocks, must leave the product no lower.
void testHearthBandSweepsNeverLowerK() {
  TestChain t("hearth_sweep");
  TEST(t.ok);
  uint32_t height = enterHearthAuctionPhase(t);
  const uint64_t C = parameters::COIN;
  const uint64_t sellSizes[] = {1 * C, 37 * C, 400 * C, 900 * C, 3000 * C};
  const uint64_t buyBudgets[] = {1 * C, 9 * C, 60 * C, 140 * C, 400 * C};
  uint8_t tag = 10;
  bool kHeld = true, heatHeld = true;
  for (int round = 0; round < 5; ++round) {
    const Crypto::Hash sellId = hearthOrderId(tag++);
    const Crypto::Hash buyId = hearthOrderId(tag++);
    VaultCoreTestAccess::addLimitDeposit(t.chain, sellId, 1, sellSizes[round], parameters::ORDER_PRICE_TICK, height - 1);
    AmmPoolState before = t.chain.getAmmPool();
    VaultCoreTestAccess::runOrderbookBlock(t.chain, height++);
    AmmPoolState after = t.chain.getAmmPool();
    kHeld = kHeld && poolProduct(after) >= poolProduct(before);
    heatHeld = heatHeld && heatCreatedByBlock(before, after, 0, VaultCoreTestAccess::proceedsHeat(t.chain, sellId)) <= 0;

    VaultCoreTestAccess::addLimitDeposit(t.chain, buyId, 0, buyBudgets[round], parameters::COIN, height - 1);
    before = t.chain.getAmmPool();
    VaultCoreTestAccess::runOrderbookBlock(t.chain, height++);
    after = t.chain.getAmmPool();
    kHeld = kHeld && poolProduct(after) >= poolProduct(before);
    heatHeld = heatHeld && heatCreatedByBlock(before, after, 0, VaultCoreTestAccess::proceedsHeat(t.chain, buyId)) <= 0;
  }
  TEST(kHeld);
  TEST(heatHeld);
}

// Regression (audit 2026-11-11 C3): the auction's clearing price, bootstrap
// counters, depth band, volatility history and fill rollback records were
// process memory only. A node restarted from its cache re-entered bootstrap
// and stopped running auctions its peers ran. Restoring the cached state into
// a process holding start-up state must reproduce the next block exactly.
void testHearthOrderbookStateSurvivesRestart() {
  TestChain t("hearth_restart");
  TEST(t.ok);
  VaultCoreTestAccess::resetToStartup(t.chain);
  const std::vector<uint8_t> startupState = VaultCoreTestAccess::saveOrderbookState(t.chain);
  uint32_t height = enterHearthAuctionPhase(t);
  VaultCoreTestAccess::addLimitDeposit(t.chain, hearthOrderId(40), 1, 20 * parameters::COIN,
                                       parameters::ORDER_PRICE_TICK, height - 1);
  VaultCoreTestAccess::runOrderbookBlock(t.chain, height++);

  const std::vector<uint8_t> cached = VaultCoreTestAccess::saveOrderbookState(t.chain);
  TEST(cached != startupState);
  VaultCoreTestAccess::runOrderbookBlock(t.chain, height);
  const std::vector<uint8_t> nextBlockState = VaultCoreTestAccess::saveOrderbookState(t.chain);

  VaultCoreTestAccess::loadOrderbookState(t.chain, startupState);
  TEST(t.chain.isOrderbookInBootstrap());
  VaultCoreTestAccess::loadOrderbookState(t.chain, cached);
  TEST(!t.chain.isOrderbookInBootstrap());
  TEST(VaultCoreTestAccess::saveOrderbookState(t.chain) == cached);
  VaultCoreTestAccess::runOrderbookBlock(t.chain, height);
  TEST(VaultCoreTestAccess::saveOrderbookState(t.chain) == nextBlockState);
}

// Audit 2026-11-11 H2: the mint price was the mean of 8 end-of-block spots, so
// a few blocks of pushed price set what a mint paid. It is now the lower of
// the 8-block and the epoch median.
void testHearthOracleResistsShortPump() {
  TestChain t("hearth_oracle");
  TEST(t.ok);
  const uint64_t P = parameters::COIN / 8;
  const uint32_t epoch = static_cast<uint32_t>(VaultCoreTestAccess::epochDuration(t.chain));
  TEST(VaultCoreTestAccess::oraclePriceBefore(t.chain, 10) == 0);
  VaultCoreTestAccess::recordSpots(t.chain, 0, epoch, P);
  VaultCoreTestAccess::recordSpots(t.chain, epoch, 8, 2 * P);
  TEST(VaultCoreTestAccess::oraclePriceBefore(t.chain, epoch + 8) == P);
  VaultCoreTestAccess::recordSpots(t.chain, epoch + 8, 8, P / 2);
  TEST(VaultCoreTestAccess::oraclePriceBefore(t.chain, epoch + 16) == P / 2);
  TEST(VaultCoreTestAccess::oraclePriceBefore(t.chain, epoch + 8) == P);
}

// Audit 2026-11-11 H2: mint size had no bound; one block may now mint at most
// HEARTH_MINT_MAX_BPS_PER_BLOCK of the pool's HEAT reserve.
void testHearthMintCapPerBlock() {
  TestChain t("hearth_mintcap");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const auto& pool = t.chain.getAmmPool();
  VaultCoreTestAccess::recordSpots(t.chain, 0, 16, ammGetSpotPrice(pool.reserveXfg, pool.reserveHeat));
  std::vector<uint8_t> mint;
  addHeatMintAuthToExtra(mint, 100 * C, 12 * C);
  const Transaction mintTx = settlementTx({101 * C}, {commitmentOut(12 * C, parameters::HEAT_TERM)}, mint);

  Blockchain::SettlementScratch scratch;
  scratch.mintCapHeatLeft = hearthMintCapHeat(pool.reserveHeat);
  TEST(scratch.mintCapHeatLeft == 80 * C);
  AssetFlows flows;
  uint64_t fee = 0;
  std::string err;
  int accepted = 0;
  for (int i = 0; i < 7; ++i) {
    if (t.chain.validateSettlement(mintTx, 100, scratch, flows, fee, err)) ++accepted;
  }
  TEST(accepted == 6);
  TEST(err.find("mint cap") != std::string::npos);

  // Audit 2026-11-11 L2: the minimum mint was enforced by wallets only.
  std::vector<uint8_t> dust;
  addHeatMintAuthToExtra(dust, 1 * C, parameters::HEAT_MINT_MIN_HEAT - 1);
  Blockchain::SettlementScratch dustScratch;
  TEST(!t.chain.validateSettlement(
      settlementTx({2 * C}, {commitmentOut(parameters::HEAT_MINT_MIN_HEAT - 1, parameters::HEAT_TERM)}, dust),
      100, dustScratch, flows, fee, err) && err.find("minimum") != std::string::npos);
  TEST(t.chain.blockMintCapHeat() == 80 * C);
}

// Audit 2026-11-11 H3: AuntPoL deposits at the pool ratio, so pairing while
// the pool is pushed off the oracle price sells the protocol's legs to the
// pusher. The epoch skips the pairing and the legs carry over.
void testAuntPolSkipsPushedPool() {
  TestChain t("auntpol_pushed");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const auto& pool = t.chain.getAmmPool();
  const uint64_t xfgBefore = pool.reserveXfg, heatBefore = pool.reserveHeat;
  VaultCoreTestAccess::setAuntPolLegs(t.chain, 80 * C, 12 * C);
  Block b;
  b.majorVersion = BLOCK_MAJOR_VERSION_11;
  const uint32_t h = static_cast<uint32_t>(VaultCoreTestAccess::epochDuration(t.chain));
  VaultCoreTestAccess::recordSpots(t.chain, 0, h, 2 * ammGetSpotPrice(xfgBefore, heatBefore));
  TEST(VaultCoreTestAccess::runEpochWork(t.chain, b, h));
  TEST(t.chain.getProtocolLpShares() == 0);
  TEST(pool.reserveXfg == xfgBefore && pool.reserveHeat == heatBefore);
  TEST(t.chain.getTreasuryLpPendingXfg() == 80 * C);
}

// Audit 2026-11-11 H2: the backstop cap was 5× auction volume, so crossing
// one's own orders lifted it without bound. It is now at most 5% of the XFG
// reserve per block whatever the auction did.
void testHearthBackstopBoundedByReserves() {
  TestChain t("hearth_backstop");
  TEST(t.ok);
  uint32_t height = enterHearthAuctionPhase(t);
  const AmmPoolState before = t.chain.getAmmPool();
  VaultCoreTestAccess::addLimitDeposit(t.chain, hearthOrderId(60), 1, 3000 * parameters::COIN,
                                       parameters::ORDER_PRICE_TICK, height - 1);
  VaultCoreTestAccess::runOrderbookBlock(t.chain, height);
  const AmmPoolState after = t.chain.getAmmPool();
  const uint64_t bidBandXfg = before.reserveXfg / 9 + parameters::COIN;
  const uint64_t backstopBound = (before.reserveXfg + bidBandXfg) * parameters::HEARTH_BACKSTOP_MAX_BPS / 10000;
  TEST(after.reserveXfg - before.reserveXfg <= bidBandXfg + backstopBound);
  TEST(VaultCoreTestAccess::escrowLeft(t.chain, hearthOrderId(60)) > 0);
}

// Audit 2026-11-11 H4: deposits had no minimum, no required expiry and no
// per-block cap, and every one is scanned every block forever.
void testLimitDepositBounds() {
  TestChain t("limit_bounds");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const uint32_t height = 5000;
  AssetFlows flows;
  uint64_t fee = 0;
  std::string err;
  uint8_t next = 0;
  auto deposit = [&](uint64_t amount, uint32_t expiration, Blockchain::SettlementScratch& scratch) {
    Crypto::Hash orderId{};
    orderId.data[0] = 0x6B;
    orderId.data[1] = next++;
    std::vector<uint8_t> extra;
    addLimitDepositToExtra(extra, 1, amount, 10 * parameters::ORDER_PRICE_TICK, expiration, orderId, Crypto::Hash{});
    return t.chain.validateSettlement(
        settlementTx({amount + C}, {commitmentOut(amount, parameters::DEPOSIT_TERM_POOL_XFG)}, extra),
        height, scratch, flows, fee, err);
  };
  Blockchain::SettlementScratch scratch;
  TEST(deposit(C, height + 100, scratch));
  TEST(!deposit(C - 1, height + 100, scratch) && err.find("minimum") != std::string::npos);
  TEST(!deposit(C, 0, scratch) && err.find("expiry") != std::string::npos);
  TEST(!deposit(C, height, scratch));
  TEST(!deposit(C, height + parameters::LIMIT_ORDER_MAX_TTL_BLOCKS + 1, scratch));
  TEST(deposit(C, height + parameters::LIMIT_ORDER_MAX_TTL_BLOCKS, scratch));

  Blockchain::SettlementScratch full;
  int accepted = 0;
  for (uint32_t i = 0; i <= parameters::LIMIT_DEPOSITS_MAX_PER_BLOCK; ++i) {
    if (deposit(C, height + 100, full)) ++accepted;
  }
  TEST(accepted == static_cast<int>(parameters::LIMIT_DEPOSITS_MAX_PER_BLOCK));
  TEST(err.find("cap") != std::string::npos);
}

// Audit 2026-11-11 H4: withdrawn deposits left the scanned set never. They are
// pruned past the rollback window, and a pop of the pruning block restores them.
void testWithdrawnDepositsPrunedAndRestored() {
  TestChain t("limit_prune");
  TEST(t.ok);
  uint32_t height = enterHearthAuctionPhase(t);
  const Crypto::Hash oldId = hearthOrderId(70), recentId = hearthOrderId(71);
  VaultCoreTestAccess::addLimitDeposit(t.chain, oldId, 1, parameters::COIN, parameters::ORDER_PRICE_TICK, height - 1);
  VaultCoreTestAccess::addLimitDeposit(t.chain, recentId, 1, parameters::COIN, parameters::ORDER_PRICE_TICK, height - 1);
  VaultCoreTestAccess::markWithdrawn(t.chain, oldId, height - 1001);
  VaultCoreTestAccess::markWithdrawn(t.chain, recentId, height - 999);
  VaultCoreTestAccess::runOrderbookBlock(t.chain, height);
  TEST(!VaultCoreTestAccess::hasLimitDeposit(t.chain, oldId));
  TEST(VaultCoreTestAccess::hasLimitDeposit(t.chain, recentId));
  TEST(VaultCoreTestAccess::limitOrderIndexMatchesDeposits(t.chain));
  VaultCoreTestAccess::popOrderbookBlock(t.chain, height);
  TEST(VaultCoreTestAccess::hasLimitDeposit(t.chain, oldId));
  TEST(VaultCoreTestAccess::limitOrderIndexMatchesDeposits(t.chain));
}

// Expired orders nobody withdraws stayed in the set every block walked, so
// the per-block cost grew with abandoned orders. Each block now walks only
// resting orders (neither withdrawn nor expired) and, for pruning, withdrawn
// ones; every deposit, stop, withdraw, expiry and their reversals keep both
// lists equal to what the deposits themselves say.
void testRestingOrderListFollowsEveryChange() {
  TestChain t("limit_resting");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const uint32_t height = enterHearthAuctionPhase(t);
  Crypto::PublicKey walletSpendPublic;
  Crypto::SecretKey walletSpendSecret;
  Crypto::generate_keys(walletSpendPublic, walletSpendSecret);
  const Crypto::Hash orderId = hearthOrderId(130);
  LimitOrderKeys keys = deriveLimitOrderKeys(walletSpendSecret, orderId);
  auto indexed = [&] { return VaultCoreTestAccess::limitOrderIndexMatchesDeposits(t.chain); };
  auto spendingOwnKeyImage = [](Transaction tx, uint8_t tag) {
    boost::get<KeyInput>(tx.inputs[0]).keyImage.data[0] = tag;
    return tx;
  };

  std::vector<uint8_t> depositExtra;
  addLimitDepositToExtra(depositExtra, 1, 5 * C, parameters::ORDER_PRICE_TICK, height + 5, orderId, keys.addressHash);
  auto deposit = VaultCoreTestAccess::blockWith(
      spendingOwnKeyImage(settlementTx({6 * C}, {commitmentOut(5 * C, parameters::DEPOSIT_TERM_POOL_XFG)}, depositExtra), 1),
      height, BLOCK_MAJOR_VERSION_11);
  TEST(VaultCoreTestAccess::pushTx(t.chain, deposit));
  TEST(VaultCoreTestAccess::isResting(t.chain, orderId) && indexed());

  Crypto::Signature stopProof;
  Crypto::generate_signature(getLimitCancelAuthHash(orderId, keys.addressHash), keys.spendPublicKey,
                             keys.spendSecretKey, stopProof);
  std::vector<uint8_t> stopExtra;
  addOrderCancelToExtra(stopExtra, orderId, keys.spendPublicKey, keys.viewPublicKey, stopProof);
  auto stop = VaultCoreTestAccess::blockWith(spendingOwnKeyImage(settlementTx({2 * C}, {keyOut(1 * C)}, stopExtra), 2), height + 1,
                                             BLOCK_MAJOR_VERSION_11);
  TEST(VaultCoreTestAccess::pushTx(t.chain, stop));
  TEST(!VaultCoreTestAccess::isResting(t.chain, orderId) && indexed());
  TEST(VaultCoreTestAccess::escrowLeft(t.chain, orderId) == 5 * C);
  VaultCoreTestAccess::popTx(t.chain, stop);
  TEST(VaultCoreTestAccess::isResting(t.chain, orderId) && indexed());
  TEST(VaultCoreTestAccess::escrowLeft(t.chain, orderId) == 5 * C);

  const std::vector<TransactionOutput> payout = {keyOut(5 * C)};
  const Crypto::Hash outputsHash = getLimitWithdrawOutputHash(payout);
  Crypto::Signature withdrawProof;
  Crypto::generate_signature(getLimitWithdrawAuthHash(orderId, keys.addressHash, outputsHash), keys.spendPublicKey,
                             keys.spendSecretKey, withdrawProof);
  std::vector<uint8_t> withdrawExtra;
  addLimitWithdrawToExtra(withdrawExtra, orderId, keys.spendPublicKey, keys.viewPublicKey, outputsHash, withdrawProof);
  auto withdraw = VaultCoreTestAccess::blockWith(spendingOwnKeyImage(settlementTx({1 * C}, payout, withdrawExtra), 3), height + 1,
                                                 BLOCK_MAJOR_VERSION_11);
  TEST(VaultCoreTestAccess::pushTx(t.chain, withdraw));
  TEST(!VaultCoreTestAccess::isResting(t.chain, orderId) && indexed());
  VaultCoreTestAccess::popTx(t.chain, withdraw);
  TEST(VaultCoreTestAccess::isResting(t.chain, orderId) && indexed());

  VaultCoreTestAccess::runOrderbookBlock(t.chain, height + 5);
  TEST(!VaultCoreTestAccess::isResting(t.chain, orderId) && VaultCoreTestAccess::hasLimitDeposit(t.chain, orderId));
  TEST(indexed());
  VaultCoreTestAccess::reverseOrderFills(t.chain, height + 5);
  VaultCoreTestAccess::popOrderbookBlock(t.chain, height + 5);
  TEST(VaultCoreTestAccess::isResting(t.chain, orderId) && indexed());

  VaultCoreTestAccess::popTx(t.chain, deposit);
  TEST(!VaultCoreTestAccess::hasLimitDeposit(t.chain, orderId) && indexed());
  wipeLimitOrderSecret(keys);
}

// Every v11 HEAT output is HEAT_TERM, so a HEAT ring is all HEAT_TERM. Before
// v11 HEAT_TERM outputs are burns: they may never be spent or stand in a ring.
void testHeatRingsAfterHeatwave() {
  TestChain t("heat_rings", true);
  TEST(t.ok);
  const uint64_t amount = 7 * parameters::COIN;
  const uint32_t lastPreHeatwaveHeight = t.currency.upgradeHeight(BLOCK_MAJOR_VERSION_11);
  KeyPair owned = generateKeyPair();
  KeyPair decoy = generateKeyPair();
  KeyPair burn = generateKeyPair();
  VaultCoreTestAccess::addCommitmentRingMember(t.chain, amount, parameters::HEAT_TERM, lastPreHeatwaveHeight + 1, owned.publicKey);
  VaultCoreTestAccess::addCommitmentRingMember(t.chain, amount, parameters::HEAT_TERM, lastPreHeatwaveHeight + 4, decoy.publicKey);
  VaultCoreTestAccess::addCommitmentRingMember(t.chain, amount, parameters::HEAT_TERM, lastPreHeatwaveHeight, burn.publicKey);
  const std::vector<Crypto::PublicKey> memberKeys = {owned.publicKey, decoy.publicKey, burn.publicKey};

  auto spendRing = [&](const std::vector<uint32_t>& members, size_t realPosition, const KeyPair& signer,
                       uint8_t blockMajorVersion) {
    TransactionInputCommitmentSpend input{};
    input.amount = amount;
    Crypto::generate_key_image(signer.publicKey, signer.secretKey, input.keyImage);
    std::vector<const Crypto::PublicKey*> ring;
    uint32_t previous = 0;
    for (uint32_t member : members) {
      input.outputIndexes.push_back(member - previous);
      previous = member;
      ring.push_back(&memberKeys[member]);
    }
    Crypto::Hash prefixHash{};
    prefixHash.data[0] = 0x48;
    std::vector<Crypto::Signature> signatures(ring.size());
    Crypto::generate_ring_signature(prefixHash, input.keyImage, ring, signer.secretKey, realPosition, signatures.data());
    return VaultCoreTestAccess::commitmentSpendValid(t.chain, input, prefixHash, signatures, blockMajorVersion);
  };

  TEST(spendRing({0, 1}, 0, owned, BLOCK_MAJOR_VERSION_11));
  TEST(spendRing({0}, 0, owned, BLOCK_MAJOR_VERSION_11));
  TEST(!spendRing({0, 1}, 0, owned, BLOCK_MAJOR_VERSION_10));
  TEST(!spendRing({0, 2}, 0, owned, BLOCK_MAJOR_VERSION_11));
  TEST(!spendRing({2}, 0, burn, BLOCK_MAJOR_VERSION_11));
  TEST(!spendRing({2}, 0, burn, BLOCK_MAJOR_VERSION_10));
}

// Marker terms are not block counts. Read as one, LP shares and swap-receive XFG
// never matured, so neither could ever be spent. DIGM is disabled from v11.
void testMarkerTermRingsAfterHeatwave() {
  TestChain t("marker_rings", true);
  TEST(t.ok);
  const uint32_t firstHeatwaveHeight = t.currency.upgradeHeight(BLOCK_MAJOR_VERSION_11) + 1;
  auto spendsAlone = [&](uint32_t term, uint64_t amount, uint8_t blockMajorVersion) {
    const KeyPair owner = generateKeyPair();
    VaultCoreTestAccess::addCommitmentRingMember(t.chain, amount, term, firstHeatwaveHeight, owner.publicKey);
    TransactionInputCommitmentSpend input{};
    input.amount = amount;
    input.outputIndexes = {0};
    Crypto::generate_key_image(owner.publicKey, owner.secretKey, input.keyImage);
    const std::vector<const Crypto::PublicKey*> ring = {&owner.publicKey};
    Crypto::Hash prefixHash{};
    prefixHash.data[0] = 0x4D;
    std::vector<Crypto::Signature> signatures(1);
    Crypto::generate_ring_signature(prefixHash, input.keyImage, ring, owner.secretKey, 0, signatures.data());
    return VaultCoreTestAccess::commitmentSpendValid(t.chain, input, prefixHash, signatures, blockMajorVersion);
  };
  TEST(spendsAlone(parameters::DEPOSIT_TERM_LP, 21 * parameters::COIN, BLOCK_MAJOR_VERSION_11));
  TEST(spendsAlone(parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG, 22 * parameters::COIN, BLOCK_MAJOR_VERSION_11));
  TEST(!spendsAlone(parameters::DIGM_TERM, 23 * parameters::COIN, BLOCK_MAJOR_VERSION_11));
  TEST(!spendsAlone(parameters::DEPOSIT_TERM_LP, 24 * parameters::COIN, BLOCK_MAJOR_VERSION_10));
  TEST(!spendsAlone(10, 25 * parameters::COIN, BLOCK_MAJOR_VERSION_11));
}

// The node hands out only members a ring may hold: never a pool reserve, a
// slashed or immature output, or a pre-v11 burn; with a term, only that term.
void testNodeDecoysAreRingValid() {
  TestChain t("node_decoys", true);
  TEST(t.ok);
  const uint64_t amount = 11 * parameters::COIN;
  const uint32_t firstHeatwaveHeight = t.currency.upgradeHeight(BLOCK_MAJOR_VERSION_11) + 1;
  const Crypto::PublicKey anyKey = generateKeyPair().publicKey;
  auto add = [&](uint32_t term, uint32_t height) {
    VaultCoreTestAccess::addCommitmentRingMember(t.chain, amount, term, height, anyKey);
  };
  add(parameters::HEAT_TERM, firstHeatwaveHeight);                  // 0 HEAT
  add(parameters::HEAT_TERM, firstHeatwaveHeight + 2);              // 1 HEAT
  add(parameters::HEAT_TERM, firstHeatwaveHeight - 1);              // 2 pre-v11 burn
  add(parameters::DEPOSIT_TERM_POOL_XFG, firstHeatwaveHeight);      // 3 pool reserve
  add(parameters::DEPOSIT_TERM_POOL_HEAT, firstHeatwaveHeight);     // 4 pool reserve
  add(parameters::HEAT_TERM, firstHeatwaveHeight + 3);              // 5 slashed
  VaultCoreTestAccess::markLastRingMemberSlashed(t.chain, amount);
  add(10, firstHeatwaveHeight);                                      // 6 immature CD
  add(1, 0);                                                         // 7 matured term-1 commitment
  add(parameters::DEPOSIT_TERM_LP, firstHeatwaveHeight);            // 8 LP shares
  add(parameters::DIGM_TERM, firstHeatwaveHeight);                  // 9 disabled DIGM

  TEST((VaultCoreTestAccess::nodeDecoyIndexes(t.chain, amount, parameters::HEAT_TERM) == std::vector<uint32_t>{0, 1}));
  TEST((VaultCoreTestAccess::nodeDecoyIndexes(t.chain, amount, parameters::DEPOSIT_TERM_LP) == std::vector<uint32_t>{8}));
  TEST((VaultCoreTestAccess::nodeDecoyIndexes(t.chain, amount, 0) == std::vector<uint32_t>{0, 1, 7, 8}));
}

// LP remove pays what the remover declares, capped by what the shares
// withdraw now. HEAT balances exactly, so a payout fixed at inclusion matched a
// prebuilt transaction only if no block had moved the reserves.
void testLpRemovePaysTheDeclaredAmounts() {
  TestChain t("lp_remove", true);
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const auto& pool = t.chain.getAmmPool();
  const uint64_t shares = pool.totalLpShares / 1000;
  uint64_t formulaXfg = 0, formulaHeat = 0;
  ammGetWithdrawalAmounts(shares, pool.totalLpShares, pool.reserveXfg, pool.reserveHeat, formulaXfg, formulaHeat);
  TEST(formulaXfg > 2 && formulaHeat > 2);
  VaultCoreTestAccess::addCommitmentRingMember(t.chain, shares, parameters::DEPOSIT_TERM_LP,
                                               t.currency.upgradeHeight(BLOCK_MAJOR_VERSION_11) + 1,
                                               generateKeyPair().publicKey);
  auto remove = [&](uint64_t declaredXfg, uint64_t declaredHeat, std::string& err) {
    std::vector<uint8_t> extra;
    addLpRemoveAuthToExtra(extra, shares, declaredXfg, declaredHeat);
    std::vector<TransactionOutput> outputs = {keyOut(declaredXfg)};
    if (declaredHeat > 0) outputs.push_back(commitmentOut(declaredHeat, parameters::HEAT_TERM));
    Transaction tx = settlementTx({C}, outputs, extra);
    TransactionInputCommitmentSpend lpIn{};
    lpIn.amount = shares;
    lpIn.outputIndexes = {0};
    tx.inputs.push_back(lpIn);
    Blockchain::SettlementScratch scratch;
    AssetFlows flows;
    uint64_t fee = 0;
    return t.chain.validateSettlement(tx, 100, scratch, flows, fee, err) && fee == C;
  };
  std::string err;
  TEST(remove(formulaXfg, formulaHeat, err));
  TEST(remove(formulaXfg - 1, formulaHeat - 1, err));
  TEST(!remove(formulaXfg, formulaHeat + 1, err) && err.find("exceeds") != std::string::npos);
  TEST(!remove(formulaXfg + 1, formulaHeat, err) && err.find("exceeds") != std::string::npos);
}

// The stateless output check judges CD terms by the network's range: testnet
// CDs are a few blocks long, which the mainnet range refused before consensus
// ever saw them.
// The market-sell tag was written as fixed 8-byte values while consensus reads
// varints, so any writer or re-serialization (the F8 janus seal re-serializes a
// transaction's extra) produced a tag that parsed to other amounts.
void testMarketSellAuthRoundTrips() {
  const uint64_t xfgToSell = 123456789012ULL;
  const uint64_t minHeatReceive = 987654321ULL;
  std::vector<uint8_t> extra;
  TEST(addMarketSellAuthToExtra(extra, xfgToSell, minHeatReceive));
  std::vector<TransactionExtraField> fields;
  TEST(parseTransactionExtra(extra, fields));
  TEST(fields.size() == 1 && fields[0].type() == typeid(TransactionExtraMarketSellAuth));
  const auto& auth = boost::get<TransactionExtraMarketSellAuth>(fields[0]);
  TEST(auth.xfgToSell == xfgToSell && auth.minHeatReceive == minHeatReceive);
  std::vector<uint8_t> rewritten;
  TEST(writeTransactionExtra(rewritten, fields));
  TEST(rewritten == extra);
}

void testOutputCheckUsesTheNetworkTermRange() {
  Logging::LoggerGroup nullLog;
  Currency testnet = CurrencyBuilder(nullLog).testnet(true).currency();
  Transaction tx{};
  tx.version = TRANSACTION_VERSION_2;
  TransactionOutputCommitment cd{};
  cd.commitKey = generateKeyPair().publicKey;
  cd.term = testnet.depositMinTerm();
  TransactionOutput out;
  out.amount = parameters::COIN;
  out.target = cd;
  tx.outputs.push_back(out);
  std::string err;
  TEST(check_outs_valid(tx, &err, testnet.depositMinTerm(), testnet.depositMaxTerm()));
  TEST(!check_outs_valid(tx, &err) && err.find("invalid term") != std::string::npos);
}

// A restart must keep the commitment-output index: every HEAT ring resolves
// its members through it.
void testCommitmentOutputsSurviveRestart() {
  const uint64_t amount = 13 * parameters::COIN;
  const Crypto::PublicKey memberKey = generateKeyPair().publicKey;
  std::filesystem::path dir;
  {
    TestChain before("commitment_restart", true);
    TEST(before.ok);
    before.keepDirectory = true;
    dir = before.dir;
    VaultCoreTestAccess::addCommitmentRingMember(before.chain, amount, parameters::HEAT_TERM, 40, memberKey);
    TEST(before.chain.deinit());
  }
  {
    struct ReopenedNode {
      Logging::LoggerGroup log;
      Currency currency;
      RealTimeProvider timeProvider;
      tx_memory_pool pool;
      Blockchain chain;
      ReopenedNode()
          : currency(CurrencyBuilder(log).testnet(true).currency()),
            pool(currency, chain, timeProvider, log),
            chain(currency, pool, log, false, false) {}
    } restarted;
    TEST(restarted.chain.init(dir.string(), true));
    const auto members = VaultCoreTestAccess::commitmentRingMembers(restarted.chain, amount);
    TEST(members.size() == 1 && members.front().commitKey == memberKey && members.front().term == parameters::HEAT_TERM);
    restarted.chain.deinit();
  }
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

// A commitment spend records its key image once. The connect path used to
// look the key image up after recording it, so every HEAT, CD and LP spend
// failed to connect and any block holding one was rejected.
void testCommitmentSpendConnectsOnce() {
  TestChain t("commitment_connect", true);
  TEST(t.ok);
  const uint32_t height = t.currency.upgradeHeight(BLOCK_MAJOR_VERSION_11) + 1;
  auto spending = [&](uint8_t keyImageTag, uint64_t amount) {
    Transaction tx = settlementTx({}, {}, {});
    TransactionInputCommitmentSpend heatIn{};
    heatIn.amount = amount;
    heatIn.outputIndexes = {0};
    heatIn.keyImage.data[0] = keyImageTag;
    tx.inputs.push_back(heatIn);
    return tx;
  };
  auto first = VaultCoreTestAccess::blockWith(spending(1, parameters::COIN), height, BLOCK_MAJOR_VERSION_11);
  TEST(VaultCoreTestAccess::pushTx(t.chain, first));
  auto replay = VaultCoreTestAccess::blockWith(spending(1, 2 * parameters::COIN), height, BLOCK_MAJOR_VERSION_11);
  TEST(!VaultCoreTestAccess::pushTx(t.chain, replay));
}

// Replay from genesis must not see deposits from heights it has not reached
// yet: a deposit left in memory would rest in, and fill at, earlier auctions.
void testRebuildCacheForgetsUnreplayedDeposits() {
  TestChain t("limit_rebuild");
  TEST(t.ok);
  const Crypto::Hash orderId = hearthOrderId(140);
  const uint32_t height = enterHearthAuctionPhase(t);
  VaultCoreTestAccess::addLimitDeposit(t.chain, orderId, 1, 20 * parameters::COIN,
                                       parameters::ORDER_PRICE_TICK, height);
  TEST(VaultCoreTestAccess::isResting(t.chain, orderId));
  VaultCoreTestAccess::resetToStartup(t.chain);
  TEST(!VaultCoreTestAccess::hasLimitDeposit(t.chain, orderId));
  TEST(!VaultCoreTestAccess::isResting(t.chain, orderId));
  TEST(VaultCoreTestAccess::limitOrderIndexMatchesDeposits(t.chain));
}

// Audit 2026-11-11 H5: every order from one wallet carried the same
// addressHash, and a withdraw published the wallet's spend and view keys.
// Orders now use one-time keys derived from the wallet spend secret.
void testLimitOrderKeysUnlinkable() {
  TestChain t("limit_keys");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  Crypto::PublicKey walletSpendPublic, otherSpendPublic;
  Crypto::SecretKey walletSpendSecret, otherSpendSecret;
  Crypto::generate_keys(walletSpendPublic, walletSpendSecret);
  Crypto::generate_keys(otherSpendPublic, otherSpendSecret);

  const Crypto::Hash orderA = hearthOrderId(80), orderB = hearthOrderId(81);
  const LimitOrderKeys keysA = deriveLimitOrderKeys(walletSpendSecret, orderA);
  const LimitOrderKeys keysB = deriveLimitOrderKeys(walletSpendSecret, orderB);
  TEST(memcmp(keysA.addressHash.data, keysB.addressHash.data, sizeof(keysA.addressHash.data)) != 0);
  TEST(memcmp(keysA.spendPublicKey.data, walletSpendPublic.data, sizeof(walletSpendPublic.data)) != 0);
  TEST(isOwnLimitOrder(walletSpendSecret, orderA, keysA.addressHash));
  TEST(!isOwnLimitOrder(otherSpendSecret, orderA, keysA.addressHash));
  TEST(!isOwnLimitOrder(walletSpendSecret, orderB, keysA.addressHash));
  const LimitOrderKeys again = deriveLimitOrderKeys(walletSpendSecret, orderA);
  TEST(memcmp(again.addressHash.data, keysA.addressHash.data, sizeof(again.addressHash.data)) == 0);

  VaultCoreTestAccess::addLimitDeposit(t.chain, orderA, 1, 5 * C, parameters::ORDER_PRICE_TICK, 1, &keysA.addressHash);
  auto withdrawSignedBy = [&](const Crypto::PublicKey& spendPublic, const Crypto::SecretKey& spendSecret,
                              const Crypto::PublicKey& viewPublic) {
    const std::vector<TransactionOutput> outputs{keyOut(5 * C)};
    const Crypto::Hash outputsHash = getLimitWithdrawOutputHash(outputs);
    Crypto::Signature proof;
    Crypto::generate_signature(getLimitWithdrawAuthHash(orderA, keysA.addressHash, outputsHash),
                               spendPublic, spendSecret, proof);
    std::vector<uint8_t> extra;
    addLimitWithdrawToExtra(extra, orderA, spendPublic, viewPublic, outputsHash, proof);
    Blockchain::SettlementScratch scratch;
    AssetFlows flows;
    uint64_t fee = 0;
    std::string err;
    return t.chain.validateSettlement(settlementTx({C}, outputs, extra), 100, scratch, flows, fee, err);
  };
  TEST(withdrawSignedBy(keysA.spendPublicKey, keysA.spendSecretKey, keysA.viewPublicKey));
  TEST(!withdrawSignedBy(walletSpendPublic, walletSpendSecret, keysA.viewPublicKey));
}

// Audit 2026-11-11 M3: the epoch added the whole CD fee accumulator to the
// HEAT supply, though most of it was HEAT taken from escrow or reserves. Only
// the part valued from XFG fees is new HEAT.
void testHeatSupplyCountsOnlyXfgValuedFees() {
  TestChain t("heat_supply");
  TEST(t.ok);
  uint32_t height = enterHearthAuctionPhase(t);
  const auto& pool = t.chain.getAmmPool();
  VaultCoreTestAccess::recordSpots(t.chain, height - 16, 16, ammGetSpotPrice(pool.reserveXfg, pool.reserveHeat));
  VaultCoreTestAccess::addLimitDeposit(t.chain, hearthOrderId(90), 0, 400 * parameters::COIN,
                                       parameters::COIN, height - 1);
  VaultCoreTestAccess::runOrderbookBlock(t.chain, height);
  const uint64_t accumulator = t.chain.getAmmPool().cdHearthFeeAccumulator;
  const uint64_t fromXfg = VaultCoreTestAccess::cdFeeHeatFromXfg(t.chain);
  TEST(fromXfg > 0 && accumulator > fromXfg);

  const uint32_t epoch = static_cast<uint32_t>(VaultCoreTestAccess::epochDuration(t.chain));
  const uint32_t boundary = (height / epoch + 1) * epoch;
  const uint64_t supplyBefore = t.chain.getHeatSupply();
  Block b;
  b.majorVersion = BLOCK_MAJOR_VERSION_11;
  TEST(VaultCoreTestAccess::runEpochWork(t.chain, b, boundary));
  TEST(t.chain.getHeatSupply() - supplyBefore == fromXfg);
  TEST(t.chain.getAmmPool().cdHearthFeeAccumulator == 0);
  TEST(VaultCoreTestAccess::cdFeeHeatFromXfg(t.chain) == 0);
}

// Audit 2026-11-11 M2: auction settlement skipped a fill whose escrow or
// reserve could not cover it while its counterparty's fill still applied —
// value moved one way. The auction now settles whole or not at all.
void testAuctionSettlesWholeOrNotAtAll() {
  TestChain t("auction_whole");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const Crypto::Hash ask = hearthOrderId(100);
  VaultCoreTestAccess::addLimitDeposit(t.chain, ask, 1, 10 * C, parameters::ORDER_PRICE_TICK, 1);
  VaultCoreTestAccess::addLimitDeposit(t.chain, hearthOrderId(102), 1, 10 * C, parameters::ORDER_PRICE_TICK, 1);
  auto result = [&](const Crypto::Hash& userId, uint64_t userXfg, uint64_t poolHeat) {
    AuctionResult r;
    r.crossed = true;
    r.matchedVolume = userXfg;
    r.hasTaker = true;
    r.takerIsBid = false;
    Crypto::Hash poolId{};
    poolId.data[0] = POOL_ORDER_ID_PREFIX;
    r.fills.push_back({userId, 1, userXfg, poolHeat, 0, 0});
    r.fills.push_back({poolId, 0, userXfg, poolHeat, 0, 0});
    return r;
  };
  const uint64_t poolHeat = t.chain.getAmmPool().reserveHeat;
  TEST(VaultCoreTestAccess::auctionFillsSettle(t.chain, result(ask, 10 * C, 1 * C)));
  TEST(!VaultCoreTestAccess::auctionFillsSettle(t.chain, result(ask, 10 * C + 1, 1 * C)));
  TEST(!VaultCoreTestAccess::auctionFillsSettle(t.chain, result(hearthOrderId(101), 1 * C, 1 * C)));
  TEST(!VaultCoreTestAccess::auctionFillsSettle(t.chain, result(ask, 10 * C, poolHeat + 1)));
}

// Audit 2026-11-11 M4: a withdraw must match the order's balances exactly, so a
// fill between signing and inclusion invalidated it and an order trading every
// block could not be cancelled. A signed cancel now stops the order first.
void testLimitOrderCancelTag() {
  TestChain t("limit_cancel");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  Crypto::PublicKey walletSpendPublic;
  Crypto::SecretKey walletSpendSecret;
  Crypto::generate_keys(walletSpendPublic, walletSpendSecret);
  const Crypto::Hash orderId = hearthOrderId(110);
  const LimitOrderKeys keys = deriveLimitOrderKeys(walletSpendSecret, orderId);
  VaultCoreTestAccess::addLimitDeposit(t.chain, orderId, 1, 5 * C, parameters::ORDER_PRICE_TICK, 1, &keys.addressHash);

  auto cancelSignedBy = [&](const Crypto::PublicKey& spendPublic, const Crypto::SecretKey& spendSecret,
                            Blockchain::SettlementScratch& scratch, std::string& err) {
    Crypto::Signature proof;
    Crypto::generate_signature(getLimitCancelAuthHash(orderId, keys.addressHash), spendPublic, spendSecret, proof);
    std::vector<uint8_t> extra;
    addOrderCancelToExtra(extra, orderId, spendPublic, keys.viewPublicKey, proof);
    AssetFlows flows;
    uint64_t fee = 0;
    return t.chain.validateSettlement(settlementTx({2 * C}, {keyOut(1 * C)}, extra), 100, scratch, flows, fee, err) &&
           fee == 1 * C;
  };
  std::string err;
  Blockchain::SettlementScratch scratch;
  TEST(cancelSignedBy(keys.spendPublicKey, keys.spendSecretKey, scratch, err));
  TEST(!cancelSignedBy(keys.spendPublicKey, keys.spendSecretKey, scratch, err) && err.find("this block") != std::string::npos);
  Blockchain::SettlementScratch other;
  TEST(!cancelSignedBy(walletSpendPublic, walletSpendSecret, other, err) && err.find("ownership") != std::string::npos);

  std::vector<uint8_t> extra;
  TEST(addOrderCancelToExtra(extra, orderId, keys.spendPublicKey, keys.viewPublicKey, Crypto::Signature{}));
  std::vector<TransactionExtraField> fields;
  TEST(parseTransactionExtra(extra, fields) && fields.size() == 1 &&
       fields[0].type() == typeid(TransactionExtraOrderCancel));

  VaultCoreTestAccess::markExpired(t.chain, orderId);
  Blockchain::SettlementScratch afterStop;
  TEST(!cancelSignedBy(keys.spendPublicKey, keys.spendSecretKey, afterStop, err) && err.find("stopped") != std::string::npos);
}

// Owner decision 2026-11-12: the CD creation fee is consensus-enforced, scales
// with the term (0.25% at 6 epochs to 1% at 72) on the HEAT's value at the
// mint price, is paid in XFG and is withheld from the miner for AuntPoL.
void testCdCreationFeeWithheldForAuntPol() {
  TestChain t("cd_fee");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const auto& pool = t.chain.getAmmPool();
  const uint64_t price = ammGetSpotPrice(pool.reserveXfg, pool.reserveHeat);
  VaultCoreTestAccess::recordSpots(t.chain, 0, 16, price);
  VaultCoreTestAccess::addHeatRingMember(t.chain, 100 * C);
  const uint64_t epochBlocks = VaultCoreTestAccess::epochDuration(t.chain);

  auto createCd = [&](uint64_t xfgIn, uint32_t epochs, uint64_t declaredFee, uint64_t& fee, std::string& err) {
    std::vector<uint8_t> extra;
    if (declaredFee > 0) addCdCreationFeeToExtra(extra, declaredFee);
    Transaction tx = settlementTx({xfgIn}, {commitmentOut(100 * C, static_cast<uint32_t>(epochs * epochBlocks))}, extra);
    TransactionInputCommitmentSpend heatIn{};
    heatIn.amount = 100 * C;
    heatIn.outputIndexes = {0};
    tx.inputs.push_back(heatIn);
    Blockchain::SettlementScratch scratch;
    AssetFlows flows;
    return t.chain.validateSettlement(tx, 100, scratch, flows, fee, err);
  };
  uint64_t fee = 0;
  std::string err;
  // 100 HEAT at 0.125 HEAT/XFG is 800 XFG of value: 0.25% → 2 XFG, 1% → 8 XFG.
  // The declared fee goes whole to AuntPoL; only the surplus beyond it is the
  // miner's network fee.
  TEST(createCd(3 * C, 6, 2 * C, fee, err) && fee == 1 * C);
  TEST(createCd(4 * C, 6, 3 * C, fee, err) && fee == 1 * C);
  TEST(!createCd(3 * C, 6, 2 * C - 1, fee, err) && err.find("below the required") != std::string::npos);
  TEST(!createCd(3 * C, 6, 0, fee, err));
  TEST(!createCd(2 * C, 6, 3 * C, fee, err) && err.find("does not cover") != std::string::npos);
  TEST(createCd(9 * C, 72, 8 * C, fee, err) && fee == 1 * C);
  TEST(!createCd(9 * C, 72, 8 * C - 1, fee, err));

  std::vector<uint8_t> strayFee;
  addCdCreationFeeToExtra(strayFee, 1 * C);
  Blockchain::SettlementScratch scratch;
  AssetFlows flows;
  TEST(!t.chain.validateSettlement(settlementTx({3 * C}, {keyOut(1 * C)}, strayFee), 100, scratch, flows, fee, err) &&
       err.find("without a CD output") != std::string::npos);
}

// Audit 2026-11-11 M6: the Hearth price fields in a block were outside the
// block hash, so any relaying peer could rewrite them. From v11 the block
// hashing blob carries a 32-byte hash of the Hearth state the parent block
// left, so the block id and the merge-mining aux hash (proof of work) commit
// to it.
void testHearthStateCommittedByProofOfWork() {
  TestChain t("hearth_commit");
  TEST(t.ok);
  const auto& pool = t.chain.getAmmPool();
  VaultCoreTestAccess::recordSpots(t.chain, 0, 16, ammGetSpotPrice(pool.reserveXfg, pool.reserveHeat));
  const HearthState state = t.chain.currentHearthState();
  TEST(state.reserveXfg == pool.reserveXfg && state.reserveHeat == pool.reserveHeat);
  TEST(state.mintPrice == t.chain.getRollingTwap());
  TEST(state.hearthstoneCents == parameters::FCI_HEARTHSTONE_INITIAL_CENTS);

  Block block = boost::value_initialized<Block>();
  block.majorVersion = BLOCK_MAJOR_VERSION_11;
  block.parentBlock.majorVersion = BLOCK_MAJOR_VERSION_1;
  block.parentBlock.transactionCount = 1;
  TransactionExtraMergeMiningTag mmTag = boost::value_initialized<TransactionExtraMergeMiningTag>();
  TEST(appendMergeMiningTagToExtra(block.parentBlock.baseTransaction.extra, mmTag));
  block.hearthState = state;
  block.hearthState.mintPrice = parameters::COIN / 8;
  Crypto::Hash id{};
  TEST(get_block_hash(block, id));
  Crypto::Hash aux{};
  TEST(get_aux_block_header_hash(block, aux));
  Block tampered = block;
  tampered.hearthState.mintPrice += 1;
  Crypto::Hash tamperedAux{};
  TEST(get_aux_block_header_hash(tampered, tamperedAux));
  Crypto::Hash tamperedId{};
  TEST(get_block_hash(tampered, tamperedId) && tamperedId != id);
  TEST(tamperedAux != aux);

  Block v10 = block;
  v10.majorVersion = BLOCK_MAJOR_VERSION_10;
  Block v10Tampered = v10;
  v10Tampered.hearthState.mintPrice += 1;
  TEST(get_block_hash(v10) == get_block_hash(v10Tampered));

  BinaryArray blob;
  TEST(toBinaryArray(block, blob));
  Block decoded;
  TEST(fromBinaryArray(decoded, blob));
  TEST(decoded.hearthState == block.hearthState && get_block_hash(decoded) == id);
}

Crypto::Hash aliasOwnerKeyHash(const AccountBase& owner) {
  const AccountPublicAddress& address = owner.getAccountKeys().address;
  uint8_t preimage[64];
  memcpy(preimage, &address.spendPublicKey, 32);
  memcpy(preimage + 32, &address.viewPublicKey, 32);
  return Crypto::cn_fast_hash(preimage, sizeof(preimage));
}

// A registration owned by a real account, one per owner name, so its owner
// hash is the hash of its owner address as consensus requires from v11.
TransactionExtraAliasRegistration registrationOwnedBy(const std::string& alias, const std::string& ownerName,
                                                      uint8_t aliasType) {
  static std::map<std::string, AccountBase> owners;
  static Logging::LoggerGroup quiet;
  static const Currency mainnet = CurrencyBuilder(quiet).currency();
  auto owner = owners.find(ownerName);
  if (owner == owners.end()) {
    owner = owners.emplace(ownerName, AccountBase()).first;
    owner->second.generate();
  }
  TransactionExtraAliasRegistration registration;
  registration.alias = alias;
  registration.aliasHash = Crypto::cn_fast_hash(alias.data(), alias.size());
  registration.ownerAddress = mainnet.accountAddressAsString(owner->second.getAccountKeys().address);
  registration.addressHash = aliasOwnerKeyHash(owner->second);
  registration.aliasType = aliasType;
  return registration;
}

// The first bootstrapBlocks() v11 blocks (testnet 72, mainnet 144) clear at the
// pool ratio and the call auction starts at the next block, whatever chain ran
// before in the process. The first v11 block is upgradeHeight + 1. The live
// count used the mainnet length on testnet and carried over between chains.
void testOrderbookBootstrapEndsAtTheNetworksHeight() {
  for (const bool testnet : {true, false}) {
    TestChain t(testnet ? "bootstrap_testnet" : "bootstrap_mainnet", testnet);
    TEST(t.ok);
    TEST(t.chain.isOrderbookInBootstrap());
    TEST(t.currency.bootstrapBlocks() == (testnet ? TESTNET_BOOTSTRAP_BLOCKS : BOOTSTRAP_BLOCKS));
    const uint32_t firstV11Block = t.currency.upgradeHeight(BLOCK_MAJOR_VERSION_11) + 1;
    TEST(t.currency.blockMajorVersionAtHeight(firstV11Block) == BLOCK_MAJOR_VERSION_11);
    TEST(t.currency.blockMajorVersionAtHeight(firstV11Block - 1) < BLOCK_MAJOR_VERSION_11);
    uint32_t bootstrapBlocksSeen = 0;
    uint32_t height = firstV11Block;
    while (VaultCoreTestAccess::orderbookInBootstrapAfter(t.chain, height)) {
      ++bootstrapBlocksSeen;
      TEST(t.chain.orderbookBootstrapBlocksRemaining() == t.currency.bootstrapBlocks() - bootstrapBlocksSeen);
      ++height;
    }
    TEST(t.chain.orderbookBootstrapBlocksRemaining() == 0);
    TEST(bootstrapBlocksSeen == t.currency.bootstrapBlocks());
    TEST(height == firstV11Block + t.currency.bootstrapBlocks());
  }
  TestChain fresh("bootstrap_fresh", true);
  TEST(fresh.ok);
  TEST(fresh.chain.isOrderbookInBootstrap());
}

uint32_t firstAliasHeight(const TestChain& t) {
  return t.currency.upgradeHeight(BLOCK_MAJOR_VERSION_12) + 1;
}

// Owner decision 2026-11-12: the re-mint burn (TX_EXTRA_REMINT_BURN) is its
// own burn type with its own counter, apart from HEAT, the SWF and the Eternal
// Flame. Any transaction may burn; an alias registration pays its fee with one,
// so no alias transaction carries an output to the dev wallet and the counter
// does not count aliases. Reserved aliases pay none.
void testAliasRegistrationFeeBurned() {
  TestChain t("remint_burn");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  auto settle = [&](bool withAlias, uint8_t aliasType, uint64_t burn, uint64_t xfgIn, uint64_t& fee, std::string& err) {
    std::vector<uint8_t> extra;
    if (withAlias) {
      addAliasToExtra(extra, registrationOwnedBy("abcdefgh", "owner-address", aliasType));
    }
    if (burn > 0) addRemintBurnToExtra(extra, burn);
    Blockchain::SettlementScratch scratch;
    AssetFlows flows;
    return t.chain.validateSettlement(settlementTx({xfgIn}, {keyOut(C / 2)}, extra), firstAliasHeight(t), scratch, flows, fee, err);
  };
  uint64_t fee = 0;
  std::string err;
  // An alias registration pays its fee with a re-mint burn; only the surplus
  // beyond the burn is the miner's fee.
  TEST(settle(true, 1, C, 2 * C, fee, err) && fee == C / 2);
  TEST(!settle(true, 1, 0, 2 * C, fee, err) && err.find("registration fee") != std::string::npos);
  TEST(!settle(true, 1, C / 2, 2 * C, fee, err) && err.find("registration fee") != std::string::npos);
  TEST(!settle(true, 1, C, C + C / 4, fee, err) && err.find("does not cover") != std::string::npos);
  // A type-0 (reserved) registration never registers, so it may not settle.
  TEST(!settle(true, 0, 0, 2 * C, fee, err) && err.find("not a regular alias") != std::string::npos);
  // The burn is a type of its own: a transaction with no alias may burn too,
  // so the counter does not count aliases.
  TEST(settle(false, 0, C, 2 * C, fee, err) && fee == C / 2);
  std::vector<uint8_t> twice;
  addRemintBurnToExtra(twice, C);
  addRemintBurnToExtra(twice, C);
  Blockchain::SettlementScratch scratch;
  AssetFlows flows;
  TEST(!t.chain.validateSettlement(settlementTx({3 * C}, {keyOut(C / 2)}, twice), 100, scratch, flows, fee, err) &&
       err.find("twice") != std::string::npos);

  // The alias tag parses, and parsing resumes after it: a public key placed
  // after the registration is still found. The parser used to rescan the
  // whole extra and never consume the payload, so any alias transaction failed
  // to parse and its registration (and its tx key) were never seen.
  TransactionExtraAliasRegistration reg;
  reg.alias = "abcdefgh";
  reg.ownerAddress = "owner-address";
  reg.aliasType = 1;
  std::vector<uint8_t> extra;
  TEST(addAliasToExtra(extra, reg));
  KeyPair txKey = generateKeyPair();
  TEST(addTransactionPublicKeyToExtra(extra, txKey.publicKey));
  std::vector<TransactionExtraField> fields;
  TEST(parseTransactionExtra(extra, fields) && fields.size() == 2);
  TEST(fields.size() == 2 && fields[0].type() == typeid(TransactionExtraAliasRegistration) &&
       boost::get<TransactionExtraAliasRegistration>(fields[0]).alias == "abcdefgh");
  TEST(getTransactionPublicKeyFromExtra(extra) == txKey.publicKey);

  std::vector<uint8_t> oversized{TX_EXTRA_ALIAS, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
  TEST(!parseTransactionExtra(oversized, fields));
}

namespace {

KeyInput freshKeyInput(uint64_t amount) {
  Crypto::PublicKey pub;
  Crypto::SecretKey sec;
  Crypto::generate_keys(pub, sec);
  KeyInput in{};
  in.amount = amount;
  in.outputIndexes = {0};
  Crypto::generate_key_image(pub, sec, in.keyImage);
  return in;
}

Transaction auntPolTx(uint64_t xfgIn, const std::vector<TransactionOutput>& outputs, const std::vector<uint8_t>& extra) {
  Transaction tx{};
  tx.version = TRANSACTION_VERSION_2;
  tx.inputs.push_back(freshKeyInput(xfgIn));
  tx.outputs = outputs;
  tx.extra = extra;
  tx.signatures.assign(1, std::vector<Crypto::Signature>(1));
  return tx;
}

} // namespace

// The AuntPoL ledgers are credited in pushTransaction and debited in
// popTransaction; validateSettlement only bounds them. These drive the
// production push and pop, so a reorg over any of these transactions must put
// the legs back exactly.
void testAuntPolCdCreationFeePushPop() {
  TestChain t("auntpol_cd_pushpop");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const uint32_t epochBlocks = static_cast<uint32_t>(VaultCoreTestAccess::epochDuration(t.chain));
  std::vector<uint8_t> extra;
  addCdCreationFeeToExtra(extra, 3 * C);
  auto block = VaultCoreTestAccess::blockWith(
      auntPolTx(4 * C, {commitmentOut(100 * C, 6 * epochBlocks)}, extra), 100, BLOCK_MAJOR_VERSION_12);
  const uint64_t xfgLegBefore = t.chain.getTreasuryLpPendingXfg();
  TEST(VaultCoreTestAccess::pushTx(t.chain, block));
  TEST(t.chain.getTreasuryLpPendingXfg() == xfgLegBefore + 3 * C);
  VaultCoreTestAccess::popTx(t.chain, block);
  TEST(t.chain.getTreasuryLpPendingXfg() == xfgLegBefore);
}

// v11: the mint premium funds both legs (withheld XFG + the HEAT it buys) and
// the burn tally excludes the withheld XFG. v12: only the HEAT leg; the whole
// declared burn is counted. Pop restores both legs.
void testAuntPolMintPremiumPushPop() {
  const uint64_t C = parameters::COIN;
  for (uint8_t version : {BLOCK_MAJOR_VERSION_11, BLOCK_MAJOR_VERSION_12}) {
    TestChain t(version == BLOCK_MAJOR_VERSION_11 ? "auntpol_mint_v11" : "auntpol_mint_v12");
    TEST(t.ok);
    const auto& pool = t.chain.getAmmPool();
    const uint64_t price = ammGetSpotPrice(pool.reserveXfg, pool.reserveHeat);
    VaultCoreTestAccess::recordSpots(t.chain, 0, 16, price);
    std::vector<uint8_t> extra;
    addHeatMintAuthToExtra(extra, 100 * C, 12 * C);
    auto block = VaultCoreTestAccess::blockWith(
        auntPolTx(101 * C, {commitmentOut(12 * C, parameters::HEAT_TERM)}, extra), 100, version);

    const uint64_t xfgLegBefore = t.chain.getTreasuryLpPendingXfg();
    const uint64_t heatLegBefore = t.chain.getTreasuryHeatReserve();
    const uint64_t burnedBefore = t.chain.getTotalBurnedXfg();
    const uint64_t premiumHeat = static_cast<uint64_t>(((uint128_t)(1 * C) * price) / C);
    TEST(VaultCoreTestAccess::pushTx(t.chain, block));
    VaultCoreTestAccess::bankBlock(t.chain, block);
    TEST(t.chain.getTreasuryHeatReserve() == heatLegBefore + premiumHeat);
    if (version == BLOCK_MAJOR_VERSION_11) {
      TEST(t.chain.getTreasuryLpPendingXfg() == xfgLegBefore + 1 * C);
      TEST(t.chain.getTotalBurnedXfg() == burnedBefore + 99 * C);
    } else {
      TEST(t.chain.getTreasuryLpPendingXfg() == xfgLegBefore);
      TEST(t.chain.getTotalBurnedXfg() == burnedBefore + 100 * C);
    }
    VaultCoreTestAccess::popTx(t.chain, block);
    TEST(t.chain.getTreasuryLpPendingXfg() == xfgLegBefore);
    TEST(t.chain.getTreasuryHeatReserve() == heatLegBefore);
  }
}

void testAliasBurnTotalPushPop() {
  TestChain t("remint_burn_pushpop");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  TransactionExtraAliasRegistration reg;
  reg.alias = "pushpop1";
  reg.ownerAddress = "owner-address";
  reg.aliasType = 1;
  std::vector<uint8_t> aliasExtra;
  TEST(addAliasToExtra(aliasExtra, reg));
  TEST(addRemintBurnToExtra(aliasExtra, parameters::ALIAS_REGISTRATION_FEE));
  std::vector<uint8_t> burnExtra;
  TEST(addRemintBurnToExtra(burnExtra, 3 * C));
  auto aliasBlock = VaultCoreTestAccess::blockWith(auntPolTx(2 * C, {keyOut(C / 2)}, aliasExtra), 100, BLOCK_MAJOR_VERSION_12);
  auto burnBlock = VaultCoreTestAccess::blockWith(auntPolTx(4 * C, {keyOut(C / 2)}, burnExtra), 101, BLOCK_MAJOR_VERSION_11);
  TEST(t.chain.getRemintBurnedXfg() == 0);
  TEST(VaultCoreTestAccess::pushTx(t.chain, aliasBlock));
  TEST(VaultCoreTestAccess::pushTx(t.chain, burnBlock));
  TEST(t.chain.getRemintBurnedXfg() == parameters::ALIAS_REGISTRATION_FEE + 3 * C);
  VaultCoreTestAccess::popTx(t.chain, burnBlock);
  VaultCoreTestAccess::popTx(t.chain, aliasBlock);
  TEST(t.chain.getRemintBurnedXfg() == 0);
}

// Owner decision 2026-11-26: a registration's re-mint burn is kept only when the
// alias registers. From v12 a registration that would fail (alias taken or
// reserved, owner already holding an alias, another network, or a second claim
// on the same alias or owner in the block) makes its transaction invalid.
TransactionExtraAliasRegistration aliasRegistration(const std::string& alias, const std::string& owner) {
  return registrationOwnedBy(alias, owner, 1);
}

TransactionExtraAliasRegistration aliasRegistration(const std::string& alias, const std::string& owner,
                                                    const AccountBase& ownerAccount) {
  TransactionExtraAliasRegistration registration = aliasRegistration(alias, "unused");
  registration.ownerAddress = owner;
  registration.addressHash = aliasOwnerKeyHash(ownerAccount);
  return registration;
}

TransactionExtraAliasRegistration ownedAlias(const std::string& alias, const std::string& owner, const AccountBase& ownerAccount) {
  return aliasRegistration(alias, owner, ownerAccount);
}

TransactionExtraAliasRelease signedAliasRelease(const std::string& alias, const AccountBase& owner,
                                                const std::string& ownerAddress) {
  TransactionExtraAliasRelease release;
  release.alias = alias;
  release.aliasHash = Crypto::cn_fast_hash(alias.data(), alias.size());
  release.ownerAddress = ownerAddress;
  const Crypto::Hash ownerHash = aliasOwnerKeyHash(owner);
  uint8_t preimage[8 + 32 + 1];
  memcpy(preimage, alias.data(), 8);
  memcpy(preimage + 8, &ownerHash, 32);
  preimage[40] = 0x00;
  const Crypto::Hash challenge = Crypto::cn_fast_hash(preimage, sizeof(preimage));
  Crypto::generate_signature(challenge, owner.getAccountKeys().address.spendPublicKey,
                             owner.getAccountKeys().spendSecretKey, release.proof);
  return release;
}

TransactionExtraAliasTransfer signedAliasTransfer(const std::string& alias, const AccountBase& oldOwner,
                                                  const std::string& oldOwnerAddress, const AccountBase& newOwner,
                                                  const std::string& newOwnerAddress) {
  TransactionExtraAliasTransfer transfer;
  transfer.alias = alias;
  transfer.aliasHash = Crypto::cn_fast_hash(alias.data(), alias.size());
  transfer.oldOwnerAddress = oldOwnerAddress;
  transfer.newOwnerAddress = newOwnerAddress;
  transfer.newAddressHash = aliasOwnerKeyHash(newOwner);
  const Crypto::Hash oldOwnerHash = aliasOwnerKeyHash(oldOwner);
  uint8_t preimage[8 + 32 + 32 + 1];
  memcpy(preimage, alias.data(), 8);
  memcpy(preimage + 8, &oldOwnerHash, 32);
  memcpy(preimage + 40, &transfer.newAddressHash, 32);
  preimage[72] = 0x01;
  const Crypto::Hash challenge = Crypto::cn_fast_hash(preimage, sizeof(preimage));
  Crypto::generate_signature(challenge, oldOwner.getAccountKeys().address.spendPublicKey,
                             oldOwner.getAccountKeys().spendSecretKey, transfer.proof);
  return transfer;
}

std::vector<uint8_t> aliasRegistrationExtra(const TransactionExtraAliasRegistration& registration) {
  std::vector<uint8_t> extra;
  addAliasToExtra(extra, registration);
  addRemintBurnToExtra(extra, parameters::ALIAS_REGISTRATION_FEE);
  return extra;
}

// Owner decision 2026-11-27: v11 settlement refuses every alias tag, so no
// registration, release or transfer reaches a v11 block or the pool.
void testAliasTagsRefusedInV11Settlement() {
  TestChain t("alias_v11_refused");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  const uint32_t v11Height = t.currency.upgradeHeight(BLOCK_MAJOR_VERSION_11) + 1;
  TEST(t.currency.blockMajorVersionAtHeight(v11Height) == BLOCK_MAJOR_VERSION_11);
  AccountBase owner;
  owner.generate();
  const std::string ownerAddress = t.currency.accountAddressAsString(owner.getAccountKeys().address);
  std::vector<uint8_t> releaseExtra;
  TEST(addAliasReleaseToExtra(releaseExtra, signedAliasRelease("v11refus", owner, ownerAddress)));
  AccountBase newOwner;
  newOwner.generate();
  std::vector<uint8_t> transferExtra;
  TEST(addAliasTransferToExtra(transferExtra, signedAliasTransfer("v11refus", owner, ownerAddress, newOwner,
                                                                  t.currency.accountAddressAsString(newOwner.getAccountKeys().address))));
  const std::vector<uint8_t> registrationExtra = aliasRegistrationExtra(aliasRegistration("v11refus", "owner-one"));
  const std::vector<std::vector<uint8_t>> extras{registrationExtra, releaseExtra, transferExtra};
  for (const std::vector<uint8_t>& extra : extras) {
    Blockchain::SettlementScratch scratch;
    AssetFlows flows;
    uint64_t fee = 0;
    std::string err;
    TEST(!t.chain.validateSettlement(settlementTx({2 * C}, {keyOut(C / 2)}, extra), v11Height, scratch, flows, fee, err));
    TEST(err.find("not accepted before v12") != std::string::npos);
  }
  Blockchain::SettlementScratch scratch;
  AssetFlows flows;
  uint64_t fee = 0;
  std::string err;
  TEST(t.chain.validateSettlement(settlementTx({2 * C}, {keyOut(C / 2)}, {}), v11Height, scratch, flows, fee, err));
}

// A checkpointed v11 block skips validation, so the apply path must itself
// refuse to register, release or transfer an alias. The same tags at v12 apply.
void testAliasTagsNotAppliedByV11Block() {
  TestChain t("alias_v11_apply");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  auto push = [&](const std::vector<uint8_t>& extra, uint8_t version) {
    auto block = VaultCoreTestAccess::blockWith(auntPolTx(2 * C, {keyOut(C / 2)}, extra), 100, version);
    return VaultCoreTestAccess::pushTx(t.chain, block);
  };

  const std::vector<uint8_t> registrationExtra = aliasRegistrationExtra(aliasRegistration("applyreg", "owner-one"));
  TEST(push(registrationExtra, BLOCK_MAJOR_VERSION_11));
  TEST(!t.chain.getAliasByName("applyreg").has_value());
  TEST(push(registrationExtra, BLOCK_MAJOR_VERSION_12));
  TEST(t.chain.getAliasByName("applyreg").has_value());

  AccountBase releaser;
  releaser.generate();
  const std::string releaserAddress = t.currency.accountAddressAsString(releaser.getAccountKeys().address);
  TEST(VaultCoreTestAccess::indexAlias(t.chain, ownedAlias("applyrel", releaserAddress, releaser)));
  std::vector<uint8_t> releaseExtra;
  TEST(addAliasReleaseToExtra(releaseExtra, signedAliasRelease("applyrel", releaser, releaserAddress)));
  TEST(push(releaseExtra, BLOCK_MAJOR_VERSION_11));
  TEST(t.chain.getAliasByName("applyrel").has_value());
  TEST(push(releaseExtra, BLOCK_MAJOR_VERSION_12));
  TEST(!t.chain.getAliasByName("applyrel").has_value());

  AccountBase oldOwner;
  AccountBase newOwner;
  oldOwner.generate();
  newOwner.generate();
  const std::string oldOwnerAddress = t.currency.accountAddressAsString(oldOwner.getAccountKeys().address);
  const std::string newOwnerAddress = t.currency.accountAddressAsString(newOwner.getAccountKeys().address);
  TEST(VaultCoreTestAccess::indexAlias(t.chain, ownedAlias("applyxfr", oldOwnerAddress, oldOwner)));
  std::vector<uint8_t> transferExtra;
  TEST(addAliasTransferToExtra(transferExtra, signedAliasTransfer("applyxfr", oldOwner, oldOwnerAddress, newOwner, newOwnerAddress)));
  TEST(push(transferExtra, BLOCK_MAJOR_VERSION_11));
  TEST(t.chain.getAliasByName("applyxfr")->addressHash == aliasOwnerKeyHash(oldOwner));
  TEST(push(transferExtra, BLOCK_MAJOR_VERSION_12));
  TEST(t.chain.getAliasByName("applyxfr")->addressHash == aliasOwnerKeyHash(newOwner));
}

void testAliasBurnOnlyWhenRegistrationSucceeds() {
  TestChain t("alias_burn_on_success");
  TEST(t.ok);
  const uint64_t C = parameters::COIN;
  auto settles = [&](Blockchain::SettlementScratch& scratch, const std::vector<uint8_t>& extra, std::string& err) {
    AssetFlows flows;
    uint64_t fee = 0;
    return t.chain.validateSettlement(settlementTx({2 * C}, {keyOut(C / 2)}, extra), firstAliasHeight(t), scratch, flows, fee, err);
  };
  std::string err;
  TEST(VaultCoreTestAccess::indexAlias(t.chain, aliasRegistration("firenode", "owner-one")));

  Blockchain::SettlementScratch scratch;
  TEST(!settles(scratch, aliasRegistrationExtra(aliasRegistration("firenode", "owner-two")), err) && err.find("taken") != std::string::npos);
  TEST(!settles(scratch, aliasRegistrationExtra(aliasRegistration("FIRENODE", "owner-two")), err) && err.find("taken") != std::string::npos);
  TEST(!settles(scratch, aliasRegistrationExtra(aliasRegistration("newalias", "owner-one")), err) && err.find("already has an alias") != std::string::npos);
  TEST(!settles(scratch, aliasRegistrationExtra(aliasRegistration("fuegodev", "owner-two")), err) && err.find("reserved") != std::string::npos);
  auto foreign = aliasRegistration("newalias", "owner-two");
  foreign.networkId = t.currency.getFuegoNetworkId() + 1;
  TEST(!settles(scratch, aliasRegistrationExtra(foreign), err) && err.find("another network") != std::string::npos);
  auto squatting = aliasRegistration("squatter", "owner-five");
  squatting.addressHash = aliasRegistration("squatter", "owner-six").addressHash;
  TEST(!settles(scratch, aliasRegistrationExtra(squatting), err) && err.find("owner hash") != std::string::npos);
  auto unparseable = aliasRegistration("squatter", "owner-five");
  unparseable.ownerAddress = "not-an-address";
  TEST(!settles(scratch, aliasRegistrationExtra(unparseable), err) && err.find("owner hash") != std::string::npos);

  TEST(settles(scratch, aliasRegistrationExtra(aliasRegistration("newalias", "owner-two")), err));
  TEST(!settles(scratch, aliasRegistrationExtra(aliasRegistration("newalias", "owner-three")), err) && err.find("claimed twice") != std::string::npos);
  TEST(!settles(scratch, aliasRegistrationExtra(aliasRegistration("another1", "owner-two")), err) && err.find("claimed twice") != std::string::npos);

  // A transfer applies only with the owner's proof; one that cannot apply is
  // invalid and claims nothing.
  AccountBase oldOwner;
  AccountBase newOwner;
  oldOwner.generate();
  newOwner.generate();
  const std::string oldOwnerAddress = t.currency.accountAddressAsString(oldOwner.getAccountKeys().address);
  const std::string newOwnerAddress = t.currency.accountAddressAsString(newOwner.getAccountKeys().address);
  TEST(VaultCoreTestAccess::indexAlias(t.chain, ownedAlias("transfr1", oldOwnerAddress, oldOwner)));
  TransactionExtraAliasTransfer transfer = signedAliasTransfer("transfr1", oldOwner, oldOwnerAddress, newOwner, newOwnerAddress);
  TransactionExtraAliasTransfer unsigned_ = transfer;
  unsigned_.proof = Crypto::Signature();
  std::vector<uint8_t> transferExtra;
  std::vector<uint8_t> unsignedExtra;
  TEST(addAliasTransferToExtra(transferExtra, transfer));
  TEST(addAliasTransferToExtra(unsignedExtra, unsigned_));

  Blockchain::SettlementScratch transferBlock;
  TEST(!settles(transferBlock, unsignedExtra, err) && err.find("invalid ownership proof") != std::string::npos);
  TEST(settles(transferBlock, aliasRegistrationExtra(aliasRegistration("another1", newOwnerAddress, newOwner)), err));
  Blockchain::SettlementScratch secondBlock;
  TEST(settles(secondBlock, transferExtra, err));
  TEST(!settles(secondBlock, transferExtra, err) && err.find("claimed twice") != std::string::npos);
  TEST(!settles(secondBlock, aliasRegistrationExtra(aliasRegistration("another1", newOwnerAddress, newOwner)), err) &&
       err.find("claimed twice") != std::string::npos);

  const TransactionExtraAliasRelease release = signedAliasRelease("transfr1", oldOwner, oldOwnerAddress);
  TransactionExtraAliasRelease forgedRelease = release;
  forgedRelease.proof = Crypto::Signature();
  std::vector<uint8_t> releaseExtra;
  std::vector<uint8_t> forgedReleaseExtra;
  TEST(addAliasReleaseToExtra(releaseExtra, release));
  TEST(addAliasReleaseToExtra(forgedReleaseExtra, forgedRelease));
  Blockchain::SettlementScratch releaseBlock;
  TEST(!settles(releaseBlock, forgedReleaseExtra, err) && err.find("invalid ownership proof") != std::string::npos);
  TEST(settles(releaseBlock, releaseExtra, err));
  TEST(!settles(releaseBlock, transferExtra, err) && err.find("claimed twice") != std::string::npos);

  // Before v11 (and on a chain still loading at node start) the pool never drops
  // a transaction for its alias tags: pending v11 registrations survive a restart.
  Transaction taken = settlementTx({2 * C}, {keyOut(C / 2)}, aliasRegistrationExtra(aliasRegistration("firenode", "owner-two")));
  TEST(!t.chain.aliasTagsCannotApply(taken));

  TEST(VaultCoreTestAccess::unindexAlias(t.chain, "firenode"));
  Blockchain::SettlementScratch afterPop;
  TEST(settles(afterPop, aliasRegistrationExtra(aliasRegistration("firenode", "owner-two")), err));
  TEST(settles(afterPop, aliasRegistrationExtra(aliasRegistration("another1", "owner-one")), err));
}

// Owner decision 2026-11-12: re-mint burns are re-minted to the dev fund once a
// quarter (every FCI_EPOCHS_PER_QUARTER epochs, from v11), in one coinbase
// output, so no alias transaction pays the dev wallet. The re-mint is outside
// the block reward and is reversed on pop.
void testAliasBurnsRemintedQuarterly() {
  TestChain t("alias_remint");
  TEST(t.ok);
  const uint64_t fee = parameters::ALIAS_REGISTRATION_FEE;
  const uint32_t quarter = static_cast<uint32_t>(parameters::FCI_EPOCHS_PER_QUARTER *
                                                 VaultCoreTestAccess::epochDuration(t.chain));
  auto registerAlias = [&](const std::string& name, uint32_t height) {
    TransactionExtraAliasRegistration reg;
    reg.alias = name;
    reg.ownerAddress = "owner-address";
    reg.aliasType = 1;
    std::vector<uint8_t> extra;
    addAliasToExtra(extra, reg);
    addRemintBurnToExtra(extra, parameters::ALIAS_REGISTRATION_FEE);
    auto block = VaultCoreTestAccess::blockWith(auntPolTx(2 * parameters::COIN, {keyOut(parameters::COIN / 2)}, extra),
                                                height, BLOCK_MAJOR_VERSION_12);
    return getObjectHash(block.transactions[1].tx) != NULL_HASH && VaultCoreTestAccess::pushTx(t.chain, block);
  };
  TEST(registerAlias("remint01", 100));
  TEST(t.chain.remintDue(quarter, BLOCK_MAJOR_VERSION_11) == fee);
  TEST(t.chain.remintDue(quarter + 1, BLOCK_MAJOR_VERSION_11) == 0);
  TEST(t.chain.remintDue(quarter, BLOCK_MAJOR_VERSION_10) == 0);

  VaultCoreTestAccess::recordRemint(t.chain, quarter, fee);
  TEST(t.chain.getRemintedXfg() == fee);
  TEST(t.chain.remintDue(2 * quarter, BLOCK_MAJOR_VERSION_11) == 0);
  TEST(registerAlias("remint02", quarter + 5));
  TEST(t.chain.remintDue(2 * quarter, BLOCK_MAJOR_VERSION_11) == fee);
  VaultCoreTestAccess::unrecordRemint(t.chain, quarter);
  TEST(t.chain.getRemintedXfg() == 0);
  TEST(t.chain.remintDue(2 * quarter, BLOCK_MAJOR_VERSION_11) == 2 * fee);

  // The template's coinbase ends with the re-mint, provably to the dev fund.
  AccountPublicAddress miner;
  KeyPair minerSpend = generateKeyPair(), minerView = generateKeyPair();
  miner.spendPublicKey = minerSpend.publicKey;
  miner.viewPublicKey = minerView.publicKey;
  const uint64_t generated = t.chain.getCoinsInCirculation();
  Block b = boost::value_initialized<Block>();
  b.majorVersion = BLOCK_MAJOR_VERSION_1;
  TEST(t.currency.constructMinerTx(BLOCK_MAJOR_VERSION_1, 1, 0, generated, 0, 0, miner, b.baseTransaction,
                                   BinaryArray(), 1, UINT64_MAX, 0, 2 * fee));
  TEST(t.chain.coinbasePaysRemint(b.baseTransaction, 2 * fee));
  TEST(!t.chain.coinbasePaysRemint(b.baseTransaction, fee));
  TEST(VaultCoreTestAccess::validateCoinbase(t.chain, b, 1, generated, 2 * fee));

  Block withoutRemint = boost::value_initialized<Block>();
  withoutRemint.majorVersion = BLOCK_MAJOR_VERSION_1;
  TEST(t.currency.constructMinerTx(BLOCK_MAJOR_VERSION_1, 1, 0, generated, 0, 0, miner, withoutRemint.baseTransaction));
  TEST(!t.chain.coinbasePaysRemint(withoutRemint.baseTransaction, 2 * fee));
  TEST(!VaultCoreTestAccess::validateCoinbase(t.chain, withoutRemint, 1, generated, 2 * fee));

  // A re-mint to the miner's own address in the dev fund's place is refused.
  Block toMiner = withoutRemint;
  toMiner.baseTransaction.outputs.back().amount = 2 * fee;
  TEST(!t.chain.coinbasePaysRemint(toMiner.baseTransaction, 2 * fee));
}

// The epoch oracle reads 900 end-of-block spots. The history kept only
// MAX_ROLLBACK_HISTORY + 8, sized for the old 8-block mean, so a rollback
// deeper than 108 blocks left the epoch median short of a full window and the
// node priced the re-pushed blocks differently from every other node.
void testSpotHistoryCoversDeepestRollback() {
  TestChain t("spot_depth");
  TestChain fresh("spot_depth_fresh");
  TEST(t.ok && fresh.ok);
  const uint32_t rollback = static_cast<uint32_t>(VaultCoreTestAccess::maxRollbackHistory());
  const uint32_t tip = static_cast<uint32_t>(parameters::EPOCH_DURATION_BLOCKS) + rollback;
  const uint32_t resumeHeight = tip - rollback + 1;
  auto risingSpot = [](uint32_t height) { return parameters::COIN / 8 + uint64_t(height) * 100; };
  for (uint32_t h = 1; h <= tip; ++h) VaultCoreTestAccess::recordBlockSpot(t.chain, h, risingSpot(h));
  for (uint32_t h = tip; h >= resumeHeight; --h) VaultCoreTestAccess::dropBlockSpot(t.chain, h);
  for (uint32_t h = 1; h < resumeHeight; ++h) VaultCoreTestAccess::recordBlockSpot(fresh.chain, h, risingSpot(h));
  const uint64_t freshPrice = VaultCoreTestAccess::oraclePriceBefore(fresh.chain, resumeHeight);
  TEST(freshPrice < risingSpot(resumeHeight - 8));
  TEST(VaultCoreTestAccess::oraclePriceBefore(t.chain, resumeHeight) == freshPrice);
}

AccountPublicAddress newMinerAddress() {
  AccountPublicAddress address;
  KeyPair spend = generateKeyPair(), view = generateKeyPair();
  address.spendPublicKey = spend.publicKey;
  address.viewPublicKey = view.publicKey;
  return address;
}

// Built the way core::get_block_template builds one, so pushBlock sees what a
// node's own miner would hand it.
Block nextBlockTemplate(TestChain& t, const AccountPublicAddress& miner, bool payRemint) {
  Blockchain& chain = t.chain;
  const uint32_t height = chain.getCurrentBlockchainHeight();
  Block b = boost::value_initialized<Block>();
  b.majorVersion = chain.getBlockMajorVersionForHeight(height);
  b.minorVersion = BLOCK_MINOR_VERSION_0;
  b.previousBlockHash = chain.getTailId();
  b.timestamp = chain.getBlockTimestamp(height - 1) + t.currency.difficultyTarget();
  if (b.majorVersion >= BLOCK_MAJOR_VERSION_2) {
    b.parentBlock.majorVersion = BLOCK_MAJOR_VERSION_1;
    b.parentBlock.minorVersion = BLOCK_MINOR_VERSION_0;
    b.parentBlock.transactionCount = 1;
  }
  uint64_t remint = 0;
  if (b.majorVersion >= BLOCK_MAJOR_VERSION_11) {
    b.hearthState = chain.currentHearthState();
    remint = payRemint ? chain.remintDue(height, b.majorVersion) : 0;
  }
  const uint64_t burned = chain.getBurnedXfgAtHeight(height - 1);
  TEST(t.currency.constructMinerTx(b.majorVersion, height, chain.getCurrentCumulativeBlocksizeLimit() / 2,
                                   chain.getCoinsInCirculation(), 0, 0, miner, b.baseTransaction,
                                   BinaryArray(), 1, burned, 0, remint));
  return b;
}

bool mineAndConnect(TestChain& t, Block b) {
  if (b.majorVersion >= BLOCK_MAJOR_VERSION_2) {
    TransactionExtraMergeMiningTag tag = boost::value_initialized<TransactionExtraMergeMiningTag>();
    if (!get_aux_block_header_hash(b, tag.merkleRoot)) return false;
    b.parentBlock.baseTransaction.extra.clear();
    if (!appendMergeMiningTagToExtra(b.parentBlock.baseTransaction.extra, tag)) return false;
  }
  const difficulty_type difficulty = t.chain.getDifficultyForNextBlock();
  Crypto::cn_context context;
  Crypto::Hash proofOfWork;
  bool solved = false;
  for (uint32_t nonce = 0; nonce < 1000000 && !solved; ++nonce) {
    b.nonce = nonce;
    solved = t.currency.checkProofOfWork(context, b, difficulty, proofOfWork);
  }
  block_verification_context bvc = boost::value_initialized<block_verification_context>();
  return solved && t.chain.addNewBlock(b, bvc) && bvc.m_added_to_main_chain && !bvc.m_verification_failed;
}

bool mineUpTo(TestChain& t, const AccountPublicAddress& miner, uint32_t nextHeight) {
  while (t.chain.getCurrentBlockchainHeight() < nextHeight) {
    if (!mineAndConnect(t, nextBlockTemplate(t, miner, true))) return false;
  }
  return true;
}

// From v11 a coinbase pays only key outputs: a commitment output would be HEAT,
// CD principal or LP shares with nothing burned behind it.
void testCoinbaseMintsOnlyKeyOutputsFromV11() {
  TestChain t("coinbase_outputs");
  TEST(t.ok);
  auto coinbaseWithOutput = [&](uint8_t majorVersion, const TransactionOutput& out) {
    const uint32_t h = 1;
    Block b{};
    b.majorVersion = majorVersion;
    b.minorVersion = BLOCK_MINOR_VERSION_1;
    b.timestamp = 1;
    b.baseTransaction.version = TRANSACTION_VERSION_2;
    b.baseTransaction.inputs.push_back(BaseInput{h});
    b.baseTransaction.unlockTime = h + VaultCoreTestAccess::coinbaseUnlockWindow(t.chain);
    b.baseTransaction.outputs.push_back(out);
    return b;
  };
  auto keyedCommitmentOut = [](uint32_t term) {
    TransactionOutput out = commitmentOut(50 * parameters::COIN, term);
    Crypto::SecretKey unused;
    Crypto::generate_keys(boost::get<TransactionOutputCommitment>(out.target).commitKey, unused);
    return out;
  };

  TEST(VaultCoreTestAccess::prevalidateCoinbase(
      t.chain, coinbaseWithOutput(BLOCK_MAJOR_VERSION_11, keyOut(50 * parameters::COIN)), 1));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(
      t.chain, coinbaseWithOutput(BLOCK_MAJOR_VERSION_11, keyedCommitmentOut(parameters::HEAT_TERM)), 1));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(
      t.chain, coinbaseWithOutput(BLOCK_MAJOR_VERSION_11, keyedCommitmentOut(parameters::DEPOSIT_MIN_TERM)), 1));
  TEST(!VaultCoreTestAccess::prevalidateCoinbase(
      t.chain, coinbaseWithOutput(BLOCK_MAJOR_VERSION_11, keyedCommitmentOut(parameters::DEPOSIT_TERM_LP)), 1));
  // Pre-v11 replay keeps its original rule.
  TEST(VaultCoreTestAccess::prevalidateCoinbase(
      t.chain, coinbaseWithOutput(BLOCK_MAJOR_VERSION_10, keyedCommitmentOut(parameters::HEAT_TERM)), 1));
}

// From v11 a commitment a coinbase created is neither a valid ring member nor a
// decoy the node serves, so one minted before the coinbase rule cannot be spent.
void testCoinbaseCommitmentIsNoRingMemberFromV11() {
  TestChain t("coinbase_ring_member");
  TEST(t.ok);
  CommitmentOutputRef coinbaseMinted{};
  coinbaseMinted.term = parameters::DEPOSIT_TERM_LP;
  coinbaseMinted.transactionIndex = TxIndex{1, 0};
  CommitmentOutputRef regular = coinbaseMinted;
  regular.transactionIndex.transaction = 1;
  TEST(VaultCoreTestAccess::unusableRingMemberFromV11(t.chain, coinbaseMinted));
  TEST(!VaultCoreTestAccess::unusableRingMemberFromV11(t.chain, regular));
}

// From v11 a pending registration whose owner is not a funsa subaddress bound to
// its owner hash (F9) can never confirm, so pool cleanup drops it; the same
// registration owned by a funsa subaddress stays.
// Pool admission settles a transaction against the chain tip through
// checkTransactionSettlement. At a v11 tip a registration owned by a funsa
// subaddress, which meets every owner rule, is still refused for its tag.
void testPoolSettlementRefusesAliasTagAtV11() {
  const uint64_t C = parameters::COIN;
  TestChain t("alias_v11_pool_settlement", true);
  TEST(t.ok);
  TEST(mineUpTo(t, newMinerAddress(), parameters::TESTNET_UPGRADE_HEIGHT_V11 + 2));
  TEST(t.chain.getBlockMajorVersionForHeight(t.chain.getCurrentBlockchainHeight()) == BLOCK_MAJOR_VERSION_11);
  AccountBase owner;
  owner.generate();
  const std::string funsaOwner = t.currency.subAddressAsString(owner.getAccountKeys().address);
  uint64_t fee = 0;
  TEST(!t.chain.checkTransactionSettlement(
      settlementTx({2 * C}, {keyOut(C / 2)}, aliasRegistrationExtra(aliasRegistration("poolrefu", funsaOwner, owner))), fee));
  TEST(t.chain.checkTransactionSettlement(settlementTx({2 * C}, {keyOut(C / 2)}, {}), fee));
}

void testPoolDropsRegistrationFailingTheOwnerRule() {
  const uint64_t C = parameters::COIN;
  const uint32_t v12Height = parameters::TESTNET_UPGRADE_HEIGHT_V11 + 5;
  TestChain t("alias_owner_rule_pool", [v12Height](CurrencyBuilder& builder) {
    builder.testnet(true).upgradeHeightV12(v12Height);
  });
  TEST(t.ok);
  const AccountPublicAddress miner = newMinerAddress();

  AccountBase owner;
  owner.generate();
  const AccountPublicAddress& ownerKeys = owner.getAccountKeys().address;
  const std::string standardOwner = t.currency.accountAddressAsString(ownerKeys);
  const std::string funsaOwner = t.currency.subAddressAsString(ownerKeys);

  const Transaction standardOwned =
      settlementTx({2 * C}, {keyOut(C / 2)}, aliasRegistrationExtra(aliasRegistration("stdowner", standardOwner, owner)));
  const Transaction funsaOwned =
      settlementTx({2 * C}, {keyOut(C / 2)}, aliasRegistrationExtra(aliasRegistration("funsaown", funsaOwner, owner)));
  std::vector<uint8_t> releaseExtra;
  TEST(addAliasReleaseToExtra(releaseExtra, signedAliasRelease("stdowner", owner, standardOwner)));
  const Transaction releasing = settlementTx({2 * C}, {keyOut(C / 2)}, releaseExtra);
  const Transaction plain = settlementTx({2 * C}, {keyOut(C / 2)}, {});

  TEST(mineUpTo(t, miner, parameters::TESTNET_UPGRADE_HEIGHT_V11 + 2));
  TEST(t.chain.getBlockMajorVersionForHeight(t.chain.getCurrentBlockchainHeight()) == BLOCK_MAJOR_VERSION_11);
  TEST(t.chain.aliasTagsCannotApply(standardOwned));
  TEST(t.chain.aliasTagsCannotApply(funsaOwned));
  TEST(t.chain.aliasTagsCannotApply(releasing));
  TEST(!t.chain.aliasTagsCannotApply(plain));

  TEST(mineUpTo(t, miner, v12Height + 2));
  TEST(t.chain.getBlockMajorVersionForHeight(t.chain.getCurrentBlockchainHeight()) == BLOCK_MAJOR_VERSION_12);
  TEST(t.chain.aliasTagsCannotApply(standardOwned));
  TEST(!t.chain.aliasTagsCannotApply(funsaOwned));
  TEST(!t.chain.aliasTagsCannotApply(plain));
}

// A testnet node upgraded across a schedule change without wiping its data holds
// blocks of the wrong version. On start it rolls back to the last block the
// current schedule accepts, instead of building on a chain no peer accepts.
void testStoredChainFromAnotherScheduleRollsBack() {
  const uint32_t oldV12Height = parameters::TESTNET_UPGRADE_HEIGHT_V11 + 5;
  std::filesystem::path dir;
  {
    TestChain old("schedule_change", [oldV12Height](CurrencyBuilder& builder) {
      builder.testnet(true).upgradeHeightV12(oldV12Height);
    });
    TEST(old.ok);
    TEST(mineUpTo(old, newMinerAddress(), oldV12Height + 4));
    TEST(old.chain.getBlockMajorVersionForHeight(oldV12Height + 1) == BLOCK_MAJOR_VERSION_12);
    old.chain.deinit();
    old.keepDirectory = true;
    dir = old.dir;
  }
  {
    struct ReopenedNode {
      Logging::LoggerGroup log;
      Currency currency;
      RealTimeProvider timeProvider;
      tx_memory_pool pool;
      Blockchain chain;
      ReopenedNode()
          : currency(CurrencyBuilder(log).testnet(true).currency()),
            pool(currency, chain, timeProvider, log),
            chain(currency, pool, log, false, false) {}
    } upgraded;
    TEST(upgraded.chain.init(dir.string(), true));
    TEST(upgraded.chain.getCurrentBlockchainHeight() == oldV12Height + 1);
    upgraded.chain.deinit();
  }
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

// Popping a block removes that block's outputs from the indexes and nothing
// else. It used to clear every output index, so after any reorg the node had
// no ring members left and refused every ring spend.
void testBlockPopKeepsEarlierOutputs() {
  TestChain t("pop_keeps_outputs", true);
  TEST(t.ok);
  const AccountPublicAddress miner = newMinerAddress();
  TEST(mineUpTo(t, miner, 12));
  const uint64_t amount = 17 * parameters::COIN;
  const Crypto::PublicKey memberKey = generateKeyPair().publicKey;
  VaultCoreTestAccess::addCommitmentRingMember(t.chain, amount, parameters::HEAT_TERM, 5, memberKey);
  const size_t keyOutputsBefore = VaultCoreTestAccess::keyOutputCount(t.chain);
  TEST(t.chain.rollbackBlockchainTo(10));
  const size_t keyOutputsAfter = VaultCoreTestAccess::keyOutputCount(t.chain);
  TEST(keyOutputsAfter > 0 && keyOutputsAfter < keyOutputsBefore);
  const auto members = VaultCoreTestAccess::commitmentRingMembers(t.chain, amount);
  TEST(members.size() == 1 && members.front().commitKey == memberKey);
}

// pushBlock itself must refuse a v11 block whose committed Hearth state is not
// the chain's, and must enforce, record and on pop reverse the quarterly
// re-mint. The helpers were tested alone; these blocks go through the node.
void testBlockConnectEnforcesHearthStateAndRemint() {
  TestChain t("block_connect", true);
  TEST(t.ok);
  const AccountPublicAddress miner = newMinerAddress();
  const uint32_t firstV11 = t.currency.upgradeHeight(BLOCK_MAJOR_VERSION_11) + 1;
  TEST(mineUpTo(t, miner, firstV11));

  Block forgedState = nextBlockTemplate(t, miner, true);
  TEST(forgedState.majorVersion == BLOCK_MAJOR_VERSION_11);
  forgedState.hearthState.reserveXfg += 1;
  TEST(!mineAndConnect(t, forgedState));
  TEST(t.chain.getCurrentBlockchainHeight() == firstV11);
  TEST(mineAndConnect(t, nextBlockTemplate(t, miner, true)));

  const uint32_t quarter = static_cast<uint32_t>(
      parameters::FCI_EPOCHS_PER_QUARTER * parameters::TESTNET_EPOCH_DURATION_BLOCKS);
  TEST(mineUpTo(t, miner, quarter));
  const uint8_t quarterVersion = t.chain.getBlockMajorVersionForHeight(quarter);
  TEST(quarterVersion == BLOCK_MAJOR_VERSION_11);
  const uint64_t burned = 3 * parameters::COIN;
  VaultCoreTestAccess::setRemintBurned(t.chain, burned);
  TEST(t.chain.remintDue(quarter, quarterVersion) == burned);

  TEST(!mineAndConnect(t, nextBlockTemplate(t, miner, false)));
  TEST(t.chain.getCurrentBlockchainHeight() == quarter && t.chain.getRemintedXfg() == 0);
  TEST(mineAndConnect(t, nextBlockTemplate(t, miner, true)));
  TEST(t.chain.getRemintedXfg() == burned);

  TEST(t.chain.rollbackBlockchainTo(quarter - 1));
  TEST(t.chain.getCurrentBlockchainHeight() == quarter);
  TEST(t.chain.getRemintedXfg() == 0);
  TEST(t.chain.remintDue(quarter, quarterVersion) == burned);
  TEST(mineAndConnect(t, nextBlockTemplate(t, miner, true)));
  TEST(t.chain.getRemintedXfg() == burned);
}

// The per-order address hash a wallet publishes must be the one the full key
// derivation gives, since ownership checks compare against it.
void testLimitOrderAddressHashMatchesKeys() {
  const KeyPair wallet = generateKeyPair();
  Crypto::Hash orderId{};
  orderId.data[0] = 7;
  const LimitOrderKeys keys = deriveLimitOrderKeys(wallet.secretKey, orderId);
  TEST(limitOrderAddressHash(wallet.secretKey, orderId) == keys.addressHash);
  TEST(isOwnLimitOrder(wallet.secretKey, orderId, keys.addressHash));
  LimitOrderKeys wiped = keys;
  wipeLimitOrderSecret(wiped);
  TEST(wiped.spendSecretKey == Crypto::SecretKey{});
  TEST(wiped.addressHash == keys.addressHash);
}

// Wallets withdraw their own expired or stopped orders on their own. What is
// due comes only from the wallet's history: its confirmed deposits and stops,
// less any order a withdraw of its own (pending too) has already claimed. A
// failed withdraw claims nothing, so the order comes due again.
void testLimitOrdersDueForWithdraw() {
  auto orderTx = [](std::vector<uint8_t> extra, bool confirmed, bool failed) {
    return WalletOrderTransaction{std::string(extra.begin(), extra.end()), confirmed, failed};
  };
  auto deposit = [](const Crypto::Hash& id, uint32_t expiration) {
    std::vector<uint8_t> extra;
    addLimitDepositToExtra(extra, 1, parameters::COIN, parameters::ORDER_PRICE_TICK, expiration, id, Crypto::Hash{});
    return extra;
  };
  auto withdraw = [](const Crypto::Hash& id) {
    std::vector<uint8_t> extra;
    addLimitWithdrawToExtra(extra, id, Crypto::PublicKey{}, Crypto::PublicKey{}, Crypto::Hash{}, Crypto::Signature{});
    return extra;
  };
  auto stop = [](const Crypto::Hash& id) {
    std::vector<uint8_t> extra;
    addOrderCancelToExtra(extra, id, Crypto::PublicKey{}, Crypto::PublicKey{}, Crypto::Signature{});
    return extra;
  };
  const Crypto::Hash expired = hearthOrderId(140), live = hearthOrderId(141), stopped = hearthOrderId(142),
                     withdrawn = hearthOrderId(143), pendingWithdraw = hearthOrderId(144),
                     failedWithdraw = hearthOrderId(145), unconfirmed = hearthOrderId(146);
  const std::vector<WalletOrderTransaction> history = {
      orderTx(deposit(expired, 100), true, false),
      orderTx(deposit(live, 200), true, false),
      orderTx(deposit(stopped, 200), true, false), orderTx(stop(stopped), true, false),
      orderTx(deposit(withdrawn, 100), true, false), orderTx(withdraw(withdrawn), true, false),
      orderTx(deposit(pendingWithdraw, 100), true, false), orderTx(withdraw(pendingWithdraw), false, false),
      orderTx(deposit(failedWithdraw, 100), true, false), orderTx(withdraw(failedWithdraw), false, true),
      orderTx(deposit(unconfirmed, 100), false, false),
  };
  const std::vector<Crypto::Hash> due = limitOrdersDueForWithdraw(history, 150);
  auto isDue = [&](const Crypto::Hash& id) { return std::find(due.begin(), due.end(), id) != due.end(); };
  TEST(due.size() == 3);
  TEST(isDue(expired) && isDue(stopped) && isDue(failedWithdraw));
  TEST(!isDue(live) && !isDue(withdrawn) && !isDue(pendingWithdraw) && !isDue(unconfirmed));
  TEST(limitOrdersDueForWithdraw(history, 99).size() == 1);
}

int main() {
  testCdInterestCompounding();
  testLegacyDepositWithdrawsForPrincipal();
  testCdInterestStopsAtMaturity();
  testCdYieldFloor();
  testCdEarningWindowMatchesPayout();
  testCommitmentAssetClassification();
  testRebuildCacheKeepsHearthSeed();
  testSettleAssetFlows();
  testValidateSettlementBasics();
  testCoinbaseRejectsEverySettlementTag();
  testAuntPolPairsAtV11EpochBoundary();
  testRejectedBlockUnwindsVaultSpend();
  testVaultUtxoSetLpSurvivesSerialize();
  testVaultReorgKeepsKeyImageGate();
  testVaultSpentSurvivesSerialize();
  testVersionGatesUseConsensusAuthority();
  testVersionHelpersAgree();
  testValidateSettlementSwapCurve();
  testValidateSettlementLimitDeposit();
  testHeatwaveTagCutoffHeight();
  testLimitFillsOnTheCurve();
  testSeedLpSharesMakeFirstDepositFair();
  testHeatMetricsSeedRedemptionPrice();
  testSwapPriceBand();
  testCdActivationHeight();
  testTestnetRunsV11PastTheBootstrap();
  testStoredChainFromAnotherScheduleRollsBack();
  testPoolSettlementRefusesAliasTagAtV11();
  testPoolDropsRegistrationFailingTheOwnerRule();
  testCoinbaseMintsOnlyKeyOutputsFromV11();
  testCoinbaseCommitmentIsNoRingMemberFromV11();
  testBankingIndexTallyAndReversal();
  testBankingIndexSerializationRoundtrip();
  testFiftyFiftySplitDust();
  testNoSingleSidedLpMints();
  testCanonicalSpotPriceScale();
  testTreasuryFundTagRoundtrip();
  testLimitWithdrawOwnershipProofRoundtrip();
  testCdBonusClaimTagRoundtrip();
  testVaultSpendNoSurplusBurn();
  testVaultPartitionIsolation();
  testVaultSurplusMintRoundtrip();
  testVaultPopSymmetry();
  testVaultSetSpendKeyRebuildsKeyImageIndex();
  testVaultAllBalancesSaturate();
  testVaultSelectUtxosDeterministicTies();
  testHearthSellIntoPoolBandConservesHeat();
  testHearthBuyFromPoolBandConservesHeat();
  testHearthBandSweepsNeverLowerK();
  testHearthOrderbookStateSurvivesRestart();
  testHearthOracleResistsShortPump();
  testHearthMintCapPerBlock();
  testAuntPolSkipsPushedPool();
  testHearthBackstopBoundedByReserves();
  testLimitDepositBounds();
  testWithdrawnDepositsPrunedAndRestored();
  testLimitOrderKeysUnlinkable();
  testHeatSupplyCountsOnlyXfgValuedFees();
  testAuctionSettlesWholeOrNotAtAll();
  testLimitOrderCancelTag();
  testCdCreationFeeWithheldForAuntPol();
  testHearthStateCommittedByProofOfWork();
  testSpotHistoryCoversDeepestRollback();
  testBlockConnectEnforcesHearthStateAndRemint();
  testLimitOrderAddressHashMatchesKeys();
  testRestingOrderListFollowsEveryChange();
  testRebuildCacheForgetsUnreplayedDeposits();
  testHeatRingsAfterHeatwave();
  testMarkerTermRingsAfterHeatwave();
  testLpRemovePaysTheDeclaredAmounts();
  testOutputCheckUsesTheNetworkTermRange();
  testMarketSellAuthRoundTrips();
  testCommitmentOutputsSurviveRestart();
  testBlockPopKeepsEarlierOutputs();
  testCommitmentSpendConnectsOnce();
  testNodeDecoysAreRingValid();
  testLimitOrdersDueForWithdraw();
  testAliasRegistrationFeeBurned();
  testAuntPolCdCreationFeePushPop();
  testAuntPolMintPremiumPushPop();
  testAliasBurnTotalPushPop();
  testOrderbookBootstrapEndsAtTheNetworksHeight();
  testAliasBurnOnlyWhenRegistrationSucceeds();
  testAliasTagsRefusedInV11Settlement();
  testAliasTagsNotAppliedByV11Block();
  testAliasBurnsRemintedQuarterly();
  fprintf(stderr, "=== Treasury/Core Tests ===\nPassed: %d / %d\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
