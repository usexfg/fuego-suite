// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// End-to-end HEAT scenarios: a real node (core, P2P, in-process INode), real
// WalletLegacy wallets, blocks mined with proof of work, and every result checked
// against the node's own state. Each layer has unit tests; these cover the path
// between them, where the 2026-10-22 readiness check had none.

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <map>
#include <memory>
#include <sstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/filesystem.hpp>

#include "gtest/gtest.h"

#include "Common/Int128.h"
#include "Common/StringTools.h"
#include "CryptoNoteCore/Core.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/SubaddressTx.h"
#include "IntegrationTestLib/InProcTestNode.h"
#include "IWalletLegacy.h"
#include "Logging/ConsoleLogger.h"
#include "WalletLegacy/HearthQuote.h"
#include "WalletLegacy/WalletLegacy.h"

using namespace CryptoNote;

namespace {

const std::chrono::seconds kWaitLimit(120);

// WalletLegacy reports sync progress at the tip before a rescan has processed the
// blocks, so a rescan is awaited by its result, with a time limit.
template <class Condition>
bool eventually(Condition condition) {
  const auto deadline = std::chrono::steady_clock::now() + kWaitLimit;
  while (!condition()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  return true;
}
const uint16_t kFirstP2pPort = 27200;
const uint64_t kMintBurnXfg = 400 * parameters::COIN;
const uint64_t kActorFunding = 700 * parameters::COIN;
const uint32_t kConfirmationBlocks = 4;
const uint32_t kFundingHeightLimit = 600;
// Wallets scan from a day before their creation, so the scenario chain's clock
// starts inside that window and advances one difficulty target per block.
const uint64_t kChainClockLeadSeconds = 20 * 60 * 60;

class WalletEvents : public IWalletLegacyObserver {
public:
  void synchronizationProgressUpdated(uint32_t current, uint32_t) override {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_syncedHeight = current;
    m_changed.notify_all();
  }

  void synchronizationCompleted(std::error_code result) override {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_syncCompleted = m_syncCompleted || !result;
    m_changed.notify_all();
  }

  // The synchronizer reports completion once the wallet has caught up with the
  // node; with the in-process node's known height wrapped it never did.
  bool waitSyncCompleted() {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_changed.wait_for(lock, kWaitLimit, [&] { return m_syncCompleted; });
  }

  void saveCompleted(std::error_code result) override {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_saved = !result;
    m_changed.notify_all();
  }

  bool waitSaved() {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_changed.wait_for(lock, kWaitLimit, [&] { return m_saved; });
  }

  void initCompleted(std::error_code result) override {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_initResult = result;
    m_initCompleted = true;
    m_changed.notify_all();
  }

  std::error_code waitInitialised() {
    std::unique_lock<std::mutex> lock(m_mutex);
    const bool completed = m_changed.wait_for(lock, kWaitLimit, [&] { return m_initCompleted; });
    return completed ? m_initResult : std::make_error_code(std::errc::timed_out);
  }

  void sendTransactionCompleted(TransactionId transactionId, std::error_code result) override {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sendResults[transactionId] = result;
    m_changed.notify_all();
  }

  bool waitSyncedTo(uint32_t height) {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_changed.wait_for(lock, kWaitLimit, [&] { return m_syncedHeight >= height; });
  }

  std::error_code waitSendResult(TransactionId transactionId) {
    std::unique_lock<std::mutex> lock(m_mutex);
    const bool completed = m_changed.wait_for(lock, kWaitLimit, [&] { return m_sendResults.count(transactionId) != 0; });
    return completed ? m_sendResults[transactionId] : std::make_error_code(std::errc::timed_out);
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_changed;
  uint32_t m_syncedHeight = 0;
  bool m_saved = false;
  bool m_syncCompleted = false;
  bool m_initCompleted = false;
  std::error_code m_initResult;
  std::map<TransactionId, std::error_code> m_sendResults;
};

struct ScenarioWallet {
  ScenarioWallet(const Currency& currency, INode& node, Logging::ILogger& logger)
      : wallet(currency, node, logger) {
    wallet.addObserver(&events);
    wallet.initAndGenerate("scenario");
  }

  ~ScenarioWallet() {
    wallet.removeObserver(&events);
    wallet.shutdown();
  }

  WalletLegacy wallet;
  WalletEvents events;
};

class HeatWalletScenario : public ::testing::Test {
protected:
  HeatWalletScenario()
      : logger(Logging::WARNING),
        currency(CurrencyBuilder(logger).testnet(true).currency()) {}

  void SetUp() override {
    dataDir = (boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("heat-e2e-%%%%%%%%")).string();
    boost::filesystem::create_directories(dataDir);
    Tests::TestNodeConfiguration cfg;
    cfg.nodeType = Tests::NodeType::InProcess;
    cfg.testnet = true;
    cfg.p2pPort = kFirstP2pPort;
    cfg.rpcPort = 0;
    cfg.dataDir = dataDir;
    node.reset(new Tests::InProcTestNode(cfg, currency));
    ASSERT_TRUE(node->makeINode(inode));
  }

  void TearDown() override {
    wallets.clear();
    inode.reset();
    if (node) {
      node->stopDaemon();
      node.reset();
    }
    boost::system::error_code ignored;
    boost::filesystem::remove_all(dataDir, ignored);
  }

  ScenarioWallet& addWallet() {
    wallets.emplace_back(new ScenarioWallet(currency, *inode, logger));
    return *wallets.back();
  }

  // Block timestamps advance by the difficulty target, as a network mining at the
  // target rate would produce, so difficulty stays at its floor however fast this
  // machine finds blocks. The merge-mining tag commits to the header, so it is
  // rebuilt after the timestamp changes.
  bool mineBlock(const std::string& minerAddress) {
    Block block;
    uint64_t difficulty = 0;
    if (!node->getBlockTemplate(minerAddress, block, difficulty)) {
      return false;
    }
    block.timestamp = nextBlockTimestamp();
    if (!retagMergeMining(block)) {
      return false;
    }
    Crypto::cn_context context;
    Crypto::Hash proofOfWork;
    for (block.nonce = 0; !currency.checkProofOfWork(context, block, difficulty, proofOfWork); ++block.nonce) {
    }
    return node->submitBlock(Common::toHex(toBinaryArray(block)));
  }

  uint64_t nextBlockTimestamp() {
    core& c = nodeCore();
    const uint32_t tipHeight = c.get_current_blockchain_height() - 1;
    Block tip;
    c.getBlockByHash(c.getBlockIdByHeight(tipHeight), tip);
    return std::max<uint64_t>(tip.timestamp + currency.difficultyTarget(), chainClockStart);
  }

  static bool retagMergeMining(Block& block) {
    if (block.majorVersion < BLOCK_MAJOR_VERSION_2) {
      return true;
    }
    TransactionExtraMergeMiningTag tag = boost::value_initialized<TransactionExtraMergeMiningTag>();
    if (!get_aux_block_header_hash(block, tag.merkleRoot)) {
      return false;
    }
    block.parentBlock.baseTransaction.extra.clear();
    return appendMergeMiningTagToExtra(block.parentBlock.baseTransaction.extra, tag);
  }

  bool mineBlocks(const std::string& minerAddress, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
      if (!mineBlock(minerAddress)) {
        return false;
      }
    }
    return true;
  }

  bool mineTo(const std::string& minerAddress, uint32_t height) {
    while (nodeHeight() < height) {
      if (!mineBlock(minerAddress)) {
        return false;
      }
    }
    return true;
  }

  uint32_t nodeHeight() { return static_cast<uint32_t>(node->getLocalHeight()); }

  bool syncAll() {
    const uint32_t tip = nodeHeight();
    for (auto& w : wallets) {
      if (!w->events.waitSyncedTo(tip) || !w->events.waitSyncCompleted()) {
        return false;
      }
    }
    return true;
  }

  core& nodeCore() { return node->getCore(); }

  // Once the tip is v11 every key-input transaction must carry a janus base (F8),
  // enforced after a grace window this chain does not reach; the check here
  // holds every wallet-built transaction to the enforced rule.
  void expectSent(ScenarioWallet& sender, TransactionId transactionId) {
    ASSERT_NE(WALLET_LEGACY_INVALID_TRANSACTION_ID, transactionId);
    const std::error_code result = sender.events.waitSendResult(transactionId);
    ASSERT_FALSE(result) << result.message();
    WalletLegacyTransaction sent;
    ASSERT_TRUE(sender.wallet.getTransaction(transactionId, sent));
    Transaction tx;
    ASSERT_TRUE(nodeCore().getTransaction(sent.hash, tx, true));
    std::string error;
    EXPECT_TRUE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, true, &error)) << error;
  }

  void confirm() {
    ASSERT_TRUE(mineBlocks(minerAddress, kConfirmationBlocks));
    ASSERT_TRUE(syncAll());
  }

  // A miner wallet mines into v11; an actor wallet receives XFG from it, so the
  // actor's balances change only through the operation under test.
  ScenarioWallet& fundedActor(uint64_t xfg) {
    ScenarioWallet& miner = addWallet();
    minerAddress = miner.wallet.getAddress();
    ScenarioWallet& actor = addWallet();
    EXPECT_TRUE(mineTo(minerAddress, firstMintHeight()));
    EXPECT_TRUE(syncAll());
    while (miner.wallet.actualBalance() < xfg + currency.minimumFee() && nodeHeight() < kFundingHeightLimit) {
      EXPECT_TRUE(mineBlocks(minerAddress, kConfirmationBlocks));
      EXPECT_TRUE(syncAll());
    }
    Crypto::SecretKey transactionSecret;
    WalletLegacyTransfer funding{actor.wallet.getAddress(), static_cast<int64_t>(xfg)};
    expectSent(miner, miner.wallet.sendTransaction(transactionSecret, funding, currency.minimumFee(), "", 0));
    confirm();
    EXPECT_EQ(xfg, actor.wallet.actualBalance());
    return actor;
  }

  uint64_t mintFor(ScenarioWallet& actor, uint64_t xfgBurned) {
    const HeatMintQuote quote = quoteHeatMint(*inode, xfgBurned);
    expectSent(actor, actor.wallet.mintHeatV10(xfgBurned, quote.heatToMinter, currency.minimumFee(), 0));
    confirm();
    return quote.heatToMinter;
  }

  AccountPublicAddress addressOf(ScenarioWallet& w) {
    AccountPublicAddress address;
    bool isSubaddress = false;
    EXPECT_TRUE(currency.parseAccountAddressString(w.wallet.getAddress(), address, isSubaddress));
    return address;
  }

  ICore::AmmPoolInfo pool() {
    ICore::AmmPoolInfo info;
    EXPECT_FALSE(nodeCore().getAmmPoolInfo(info));
    return info;
  }

  // Shares held by liquidity providers. AuntPoL compounds the mint premium into
  // its own LP position at epoch boundaries, which grows the pool's total.
  uint64_t providerLpShares() {
    return pool().totalLpShares - nodeCore().get_blockchain_storage().getProtocolLpShares();
  }

  std::string minerAddress;

  uint32_t firstMintHeight() const {
    return currency.upgradeHeight(BLOCK_MAJOR_VERSION_11) + 3;
  }

  Logging::ConsoleLogger logger;
  Currency currency;
  const uint64_t chainClockStart = static_cast<uint64_t>(std::time(nullptr)) - kChainClockLeadSeconds;
  std::string dataDir;
  std::unique_ptr<Tests::InProcTestNode> node;
  std::unique_ptr<INode> inode;
  std::vector<std::unique_ptr<ScenarioWallet>> wallets;
};

}  // namespace

// Mint: the minter's wallet shows exactly the HEAT it asked for, and the node's
// HEAT supply grows by that HEAT plus the premium HEAT minted to AuntPoL.
TEST_F(HeatWalletScenario, MintedHeatReachesTheWalletAndTheSupply) {
  ScenarioWallet& minter = addWallet();
  const std::string address = minter.wallet.getAddress();
  ASSERT_TRUE(mineTo(address, firstMintHeight()));
  ASSERT_TRUE(syncAll());
  ASSERT_GT(minter.wallet.actualBalance(), kMintBurnXfg + currency.minimumFee());

  const HeatMintQuote quote = quoteHeatMint(*inode, kMintBurnXfg);
  ASSERT_GT(quote.heatToMinter, 0u);
  const uint64_t supplyBefore = nodeCore().getHeatMetrics().heatSupply;

  const TransactionId mint = minter.wallet.mintHeatV10(kMintBurnXfg, quote.heatToMinter, currency.minimumFee(), 0);
  ASSERT_NE(WALLET_LEGACY_INVALID_TRANSACTION_ID, mint);
  ASSERT_FALSE(minter.events.waitSendResult(mint));

  ASSERT_TRUE(mineBlocks(address, 2));
  ASSERT_TRUE(syncAll());

  EXPECT_EQ(quote.heatToMinter, minter.wallet.actualHeatBalance() + minter.wallet.pendingHeatBalance());
  const uint64_t supplyAfter = nodeCore().getHeatMetrics().heatSupply;
  EXPECT_EQ(quote.heatToMinter + quote.premiumHeat, supplyAfter - supplyBefore);
}

// Send HEAT: the recipient's wallet shows exactly the amount sent, and the
// sender keeps the rest of its HEAT.
TEST_F(HeatWalletScenario, SentHeatArrivesInTheOtherWallet) {
  ScenarioWallet& sender = fundedActor(kActorFunding);
  ScenarioWallet& recipient = addWallet();
  ASSERT_TRUE(syncAll());
  const uint64_t minted = mintFor(sender, kMintBurnXfg);
  ASSERT_EQ(minted, sender.wallet.actualHeatBalance());

  const uint64_t sent = minted / 3;
  expectSent(sender, sender.wallet.sendHeatV10(addressOf(recipient), sent, currency.minimumFee(), 0));
  confirm();

  EXPECT_EQ(sent, recipient.wallet.actualHeatBalance());
  EXPECT_EQ(minted - sent, sender.wallet.actualHeatBalance() + sender.wallet.pendingHeatBalance());
}

// Sell HEAT on Hearth: the pool takes the HEAT, and the seller receives the
// quoted XFG and pays only the network fee.
TEST_F(HeatWalletScenario, SoldHeatPaysXfgFromThePool) {
  ScenarioWallet& seller = fundedActor(kActorFunding);
  const uint64_t minted = mintFor(seller, kMintBurnXfg);
  const uint64_t xfgBefore = seller.wallet.actualBalance() + seller.wallet.pendingBalance();
  const ICore::AmmPoolInfo poolBefore = pool();

  const uint64_t sold = minted / 2;
  const uint64_t xfgOut = quoteSwapOutput(*inode, 1, sold);
  ASSERT_GT(xfgOut, 0u);
  expectSent(seller, seller.wallet.ammSwapV10(1, sold, xfgOut, xfgOut, currency.minimumFee(), 0));
  confirm();

  EXPECT_EQ(minted - sold, seller.wallet.actualHeatBalance() + seller.wallet.pendingHeatBalance());
  EXPECT_EQ(xfgBefore + xfgOut - currency.minimumFee(), seller.wallet.actualBalance() + seller.wallet.pendingBalance());
  const ICore::AmmPoolInfo poolAfter = pool();
  EXPECT_GT(poolAfter.reserveHeat, poolBefore.reserveHeat);
  EXPECT_LE(poolAfter.reserveXfg, poolBefore.reserveXfg - xfgOut);
}

// LP add then remove: the pool mints the quoted shares, and removing them pays
// the quoted XFG and HEAT back to the provider.
TEST_F(HeatWalletScenario, LiquidityAddedThenRemovedReturnsToTheProvider) {
  ScenarioWallet& provider = fundedActor(kActorFunding);
  const uint64_t minted = mintFor(provider, kMintBurnXfg);
  const ICore::AmmPoolInfo poolBefore = pool();
  const uint64_t providerSharesBefore = providerLpShares();

  const uint64_t heatIn = minted / 2;
  const uint64_t xfgIn = static_cast<uint64_t>(
      (static_cast<uint128_t>(heatIn) * poolBefore.reserveXfg) / poolBefore.reserveHeat);
  const uint64_t shares = quoteLpAddShares(*inode, xfgIn, heatIn);
  ASSERT_GT(shares, 0u);
  expectSent(provider, provider.wallet.lpAddV10(xfgIn, heatIn, shares, currency.minimumFee(), 0));
  confirm();
  EXPECT_EQ(providerSharesBefore + shares, providerLpShares());
  EXPECT_EQ(minted - heatIn, provider.wallet.actualHeatBalance() + provider.wallet.pendingHeatBalance());

  const uint64_t xfgBeforeRemove = provider.wallet.actualBalance() + provider.wallet.pendingBalance();
  const LpRemovePayout payout = quoteLpRemovePayout(*inode, shares);
  expectSent(provider, provider.wallet.lpRemoveV10(shares, payout.xfg, payout.heat, currency.minimumFee(), 0));
  confirm();
  EXPECT_EQ(providerSharesBefore, providerLpShares());
  EXPECT_EQ(minted - heatIn + payout.heat, provider.wallet.actualHeatBalance() + provider.wallet.pendingHeatBalance());
  EXPECT_EQ(xfgBeforeRemove + payout.xfg - currency.minimumFee(),
            provider.wallet.actualBalance() + provider.wallet.pendingBalance());
}

// A wallet saved and reopened shows the same XFG and HEAT without a rescan.
TEST_F(HeatWalletScenario, ReopenedWalletKeepsItsHeat) {
  ScenarioWallet& holder = fundedActor(kActorFunding);
  const uint64_t minted = mintFor(holder, kMintBurnXfg);
  const uint64_t xfg = holder.wallet.actualBalance();

  std::stringstream saved;
  {
    WalletEvents saveEvents;
    holder.wallet.addObserver(&saveEvents);
    holder.wallet.save(saved, true, true);
    ASSERT_TRUE(saveEvents.waitSaved());
    holder.wallet.removeObserver(&saveEvents);
  }

  WalletLegacy reopened(currency, *inode, logger);
  WalletEvents reopenedEvents;
  reopened.addObserver(&reopenedEvents);
  reopened.initAndLoad(saved, "scenario");
  const std::error_code loadResult = reopenedEvents.waitInitialised();
  ASSERT_FALSE(loadResult) << loadResult.message();
  // A wallet reopened at the tip has nothing to sync and reports no progress.
  const auto heatNow = [&] { return reopened.actualHeatBalance() + reopened.pendingHeatBalance(); };
  EXPECT_TRUE(eventually([&] { return heatNow() == minted && reopened.actualBalance() == xfg; }));
  EXPECT_EQ(minted, heatNow());
  EXPECT_EQ(xfg, reopened.actualBalance());
  reopened.removeObserver(&reopenedEvents);
  reopened.shutdown();
}

// Reset rescans the chain and rebuilds the same XFG and HEAT, including after a
// HEAT send renumbered the wallet's records (reset used to keep stale deposits).
TEST_F(HeatWalletScenario, ResetWalletRescansToTheSameBalances) {
  ScenarioWallet& holder = fundedActor(kActorFunding);
  ScenarioWallet& recipient = addWallet();
  ASSERT_TRUE(syncAll());
  const uint64_t minted = mintFor(holder, kMintBurnXfg);
  expectSent(holder, holder.wallet.sendHeatV10(addressOf(recipient), minted / 4, currency.minimumFee(), 0));
  confirm();
  const uint64_t heat = holder.wallet.actualHeatBalance() + holder.wallet.pendingHeatBalance();
  const uint64_t xfg = holder.wallet.actualBalance() + holder.wallet.pendingBalance();

  const size_t transactions = holder.wallet.getTransactionCount();
  holder.wallet.reset();
  const auto heatNow = [&] { return holder.wallet.actualHeatBalance() + holder.wallet.pendingHeatBalance(); };
  const auto xfgNow = [&] { return holder.wallet.actualBalance() + holder.wallet.pendingBalance(); };
  EXPECT_TRUE(eventually([&] {
    return holder.wallet.getTransactionCount() == transactions && heatNow() == heat && xfgNow() == xfg;
  }));
  EXPECT_EQ(transactions, holder.wallet.getTransactionCount());
  EXPECT_EQ(heat, heatNow());
  EXPECT_EQ(xfg, xfgNow());
}

