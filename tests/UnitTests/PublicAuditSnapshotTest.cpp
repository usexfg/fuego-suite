// Copyright (c) 2026 Fuego Developers
//
// Coverage for Blockchain::getPublicAuditSnapshot() and the RPC boundary built
// on it. Three things can silently corrupt a public accounting API and each is
// pinned here: a field sourced from the wrong counter, an amount that loses
// precision crossing the JSON boundary, and a rollback that leaves a counter
// where the block that moved it no longer exists.

#include "gtest/gtest.h"

#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/Blockchain.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/ITimeProvider.h"
#include "CryptoNoteCore/TransactionPool.h"
#include "Logging/LoggerGroup.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"
#include "Treasury/VaultUtxoSet.h"

#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <type_traits>
#include <unistd.h>

using namespace CryptoNote;

namespace {

// A real Blockchain at genesis in a temp directory, removed on scope exit.
struct AuditChain {
  Logging::LoggerGroup log;
  Currency currency;
  RealTimeProvider timeProvider;
  tx_memory_pool pool;
  Blockchain chain;
  std::filesystem::path dir;
  bool ok = false;

  explicit AuditChain(const std::string& tag)
      : currency(CurrencyBuilder(log).currency()),
        pool(currency, chain, timeProvider, log),
        chain(currency, pool, log, false, false),
        dir(std::filesystem::temp_directory_path() /
            ("fuego_public_audit_tests_" + tag + "_" + std::to_string(::getpid()))) {
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    ok = chain.init(dir.string(), false);
  }

  ~AuditChain() {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

} // namespace

// Every published field must equal the source the accounting contract names for
// it. A refactor that repoints one field at a neighbouring counter changes a
// number an auditor would rely on without changing any type.
TEST(PublicAuditSnapshot, EveryFieldMatchesItsNamedSource) {
  AuditChain t("mapping");
  ASSERT_TRUE(t.ok);
  Blockchain& chain = t.chain;
  const Blockchain::PublicAuditSnapshot snap = chain.getPublicAuditSnapshot();

  EXPECT_EQ(snap.snapshotHeight, chain.getCurrentBlockchainHeight() - 1);
  EXPECT_TRUE(snap.snapshotBlockHash == chain.getTailId());

  EXPECT_EQ(snap.xfgRemovedFromCirculation, chain.getTotalBurnedXfg());
  EXPECT_EQ(snap.eternalFlameBurnTotal, chain.getBurnedXfgAmount());
  EXPECT_EQ(snap.swfCollateralPendingXfg, chain.getSwfBurnedXfgPendingHeat());
  EXPECT_EQ(snap.swfXfgPermanentlyRetiredTotal, chain.getPermanentlyBurnedXfg());

  EXPECT_EQ(snap.treasuryLegacyCounterXfg, chain.getTreasuryBalance());
  EXPECT_EQ(snap.treasuryPendingXfg, chain.getTreasuryLpPendingXfg());
  EXPECT_EQ(snap.treasuryHeatReserve, chain.getTreasuryHeatReserve());
  EXPECT_EQ(snap.bonusVaultCounterHeat, chain.getBonusVaultBalance());

  const VaultUtxoSet& vault = chain.getVault();
  EXPECT_EQ(snap.vaultGeneralXfg, vault.partitionBalance(VaultPartition::GENERAL_RESERVE, AssetType::XFG));
  EXPECT_EQ(snap.vaultGeneralHeat, vault.partitionBalance(VaultPartition::GENERAL_RESERVE, AssetType::HEAT));
  EXPECT_EQ(snap.vaultLpReserveXfg, vault.partitionBalance(VaultPartition::LP_RESERVE, AssetType::XFG));
  EXPECT_EQ(snap.vaultLpReserveHeat, vault.partitionBalance(VaultPartition::LP_RESERVE, AssetType::HEAT));
  EXPECT_EQ(snap.vaultSwfXfg, vault.partitionBalance(VaultPartition::SWF, AssetType::XFG));
  EXPECT_EQ(snap.vaultSwfHeat, vault.partitionBalance(VaultPartition::SWF, AssetType::HEAT));
  EXPECT_EQ(snap.cdApyPoolVaultHeat, vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT));

  EXPECT_EQ(snap.cdYieldPendingXfg, chain.getCdYieldPool());
  EXPECT_EQ(snap.cdYieldClaimsCounterHeat, chain.getFeePoolBalance());
  EXPECT_EQ(snap.cdBaseClaimCapHeat,
            std::min(snap.cdYieldClaimsCounterHeat, snap.cdApyPoolVaultHeat));

  const auto& pool = chain.getAmmPool();
  EXPECT_EQ(snap.poolReserveXfg, pool.reserveXfg);
  EXPECT_EQ(snap.poolReserveHeat, pool.reserveHeat);
  EXPECT_EQ(snap.poolTotalLpShares, pool.totalLpShares);
  EXPECT_EQ(snap.protocolLpShares, chain.getProtocolLpShares());
  const Blockchain::TreasuryLpValue lp = chain.getTreasuryLpValue();
  EXPECT_EQ(snap.protocolLpValueXfg, lp.xfg);
  EXPECT_EQ(snap.protocolLpValueHeat, lp.heat);

  EXPECT_EQ(snap.swfHeatCounter, chain.getSwfHeatBalance());
}

// Genesis is not a useful value test: it proves the mapping, not that a
// populated chain produces distinct numbers per category. The separation the
// response depends on is that the aggregate is NOT the sum of its components,
// because pending SWF collateral left circulation but is not retired.
TEST(PublicAuditSnapshot, AggregateIsNotTheSumOfItsLifecycleComponents) {
  BankingIndex idx;
  idx.pushBlock(0, 0);
  idx.addTotalBurn(1000, 0);
  idx.addForeverDeposit(500, 0);
  idx.addPermanentBurn(300, 0);
  idx.pushBlock(0, 0);
  idx.addTotalBurn(2000, 1);
  idx.addForeverDeposit(1000, 1);
  idx.addPermanentBurn(400, 1);

  const uint64_t aggregate = idx.getTotalBurnedXfg();
  EXPECT_EQ(aggregate, 3000);
  EXPECT_EQ(idx.getBurnedXfgAmount(), 1500);
  EXPECT_EQ(idx.getPermanentlyBurnedXfg(), 700);
  // 200 XFG is pending SWF collateral: in the aggregate, in neither component.
  EXPECT_EQ(aggregate - idx.getBurnedXfgAmount() - idx.getPermanentlyBurnedXfg(), 800);
}

// Atomic amounts exceed 2^53-1. A JSON number would round them, so the wire
// type must be a decimal string. These assertions are compile-time: changing a
// field back to uint64 breaks the build instead of quietly rounding balances.
TEST(PublicAuditRpc, EveryMonetaryFieldIsADecimalString) {
  using Bal = COMMAND_RPC_GET_PUBLIC_AUDIT;
  using res = Bal::response;

  static_assert(std::is_same<decltype(res::xfg_removed_from_circulation_total), std::string>::value, "");
  static_assert(std::is_same<decltype(res::eternal_flame_burn_total_xfg), std::string>::value, "");
  static_assert(std::is_same<decltype(res::swf_xfg_permanently_retired_total), std::string>::value, "");
  static_assert(std::is_same<decltype(res::treasury_accounts), Bal::treasury>::value, "");
  static_assert(std::is_same<decltype(res::cd_yield_accounting), Bal::cd_yield>::value, "");
  static_assert(std::is_same<decltype(res::hearth_pool), Bal::hearth>::value, "");
  static_assert(std::is_same<decltype(res::swf_accounts), Bal::swf>::value, "");

  static_assert(std::is_same<decltype(Bal::treasury::legacy_counter_xfg), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::treasury::pending_xfg), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::treasury::heat_reserve), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::treasury::bonus_vault_counter_heat), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::treasury::vault_general_xfg), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::treasury::vault_general_heat), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::treasury::vault_lp_reserve_xfg), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::treasury::vault_lp_reserve_heat), std::string>::value, "");

  static_assert(std::is_same<decltype(Bal::cd_yield::pending_xfg), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::cd_yield::claims_counter_heat), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::cd_yield::apy_pool_vault_heat), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::cd_yield::base_claim_cap_heat), std::string>::value, "");

  static_assert(std::is_same<decltype(Bal::hearth::pool_reserve_xfg), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::hearth::pool_reserve_heat), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::hearth::pool_total_lp_shares), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::hearth::protocol_lp_shares), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::hearth::protocol_lp_value_xfg), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::hearth::protocol_lp_value_heat), std::string>::value, "");

  static_assert(std::is_same<decltype(Bal::swf::collateral_pending_xfg), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::swf::heat_counter), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::swf::vault_swf_xfg), std::string>::value, "");
  static_assert(std::is_same<decltype(Bal::swf::vault_swf_heat), std::string>::value, "");
}

TEST(PublicAuditRpc, AmountsAboveTwoToTheFiftyThreeSurviveTheStringBoundary) {
  const uint64_t above = (1ULL << 53) + 1;  // 9007199254740993
  ASSERT_GT(above, (1ULL << 53) - 1);

  const std::string wire = std::to_string(above);
  EXPECT_EQ(wire, "9007199254740993");
  EXPECT_EQ(std::strtoull(wire.c_str(), nullptr, 10), above);

  // The reason the field is a string: a JSON consumer using a double loses it.
  EXPECT_NE(static_cast<uint64_t>(static_cast<double>(above)), above);

  // Full-width atomics do too.
  const uint64_t max = std::numeric_limits<uint64_t>::max();
  EXPECT_EQ(std::strtoull(std::to_string(max).c_str(), nullptr, 10), max);
  EXPECT_NE(static_cast<uint64_t>(static_cast<double>(max)), max);
}

// A block that moves a counter and is then popped must leave no trace of it in
// the values the snapshot reports. This drives the same reversal calls
// Blockchain::popBlock makes on the indexes the snapshot reads.
TEST(PublicAuditSnapshot, RollbackRestoresTheReportedValues) {
  BankingIndex idx;
  VaultUtxoSet vault;
  Crypto::Hash txHash{};
  Crypto::PublicKey key{};

  vault.addUtxo(1, 4000, AssetType::HEAT, VaultPartition::CD_APY_POOL, txHash, key);
  vault.addUtxo(2, 9000, AssetType::XFG, VaultPartition::LP_RESERVE, txHash, key);

  const uint64_t beforeAggregate = idx.getTotalBurnedXfg();
  const uint64_t beforeEf = idx.getBurnedXfgAmount();
  const uint64_t beforeRetired = idx.getPermanentlyBurnedXfg();
  const uint64_t beforeApyPool = vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT);
  const uint64_t beforeLpReserve = vault.partitionBalance(VaultPartition::LP_RESERVE, AssetType::XFG);

  // Block connects: burns routed three ways, and a CD claim draws on the vault.
  idx.pushBlock(0, 0);
  idx.addTotalBurn(12000, 1);
  idx.addForeverDeposit(6000, 1);
  idx.addPermanentBurn(4000, 1);
  const uint64_t claimsCounter = idx.getTotalBurnedXfg() - 6000;  // HEAT, as consensus mints it
  vault.spendUtxos(VaultPartition::CD_APY_POOL, AssetType::HEAT, 1500);
  vault.addUtxo(3, 700, AssetType::XFG, VaultPartition::SWF, txHash, key);

  EXPECT_EQ(idx.getTotalBurnedXfg(), beforeAggregate + 12000);
  EXPECT_EQ(idx.getBurnedXfgAmount(), beforeEf + 6000);
  EXPECT_EQ(idx.getPermanentlyBurnedXfg(), beforeRetired + 4000);
  EXPECT_EQ(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT), beforeApyPool - 1500);
  EXPECT_EQ(vault.partitionBalance(VaultPartition::SWF, AssetType::XFG), 700);
  EXPECT_EQ(std::min(claimsCounter, beforeApyPool - 1500), claimsCounter);

  // Block pops: drop its outputs, restore the inputs it consumed, then reverse
  // the tallies. This is the order Blockchain::popBlock uses.
  vault.removeAboveIndex(3);
  vault.unSpendUtxos(std::vector<uint64_t>{1});
  idx.popBlock();

  EXPECT_EQ(idx.getTotalBurnedXfg(), beforeAggregate);
  EXPECT_EQ(idx.getBurnedXfgAmount(), beforeEf);
  EXPECT_EQ(idx.getPermanentlyBurnedXfg(), beforeRetired);
  EXPECT_EQ(vault.partitionBalance(VaultPartition::CD_APY_POOL, AssetType::HEAT), beforeApyPool);
  EXPECT_EQ(vault.partitionBalance(VaultPartition::LP_RESERVE, AssetType::XFG), beforeLpReserve);
  EXPECT_EQ(vault.partitionBalance(VaultPartition::SWF, AssetType::XFG), 0);
}
