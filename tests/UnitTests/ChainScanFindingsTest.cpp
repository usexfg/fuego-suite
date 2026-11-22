// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// fuego-chainscan's per-transaction checks (F1).

#include <algorithm>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "ChainScan/ChainScanFindings.h"
#include "ChainScan/ChainScanner.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "Logging/ConsoleLogger.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "crypto/crypto.h"

using namespace CryptoNote;

namespace {

Crypto::PublicKey randomPoint() {
  Crypto::PublicKey point;
  Crypto::SecretKey unused;
  Crypto::generate_keys(point, unused);
  return point;
}

Transaction keyInputTransaction(size_t outputs) {
  Transaction tx;
  tx.version = TRANSACTION_VERSION_1;
  tx.unlockTime = 0;
  tx.signatures.push_back(std::vector<Crypto::Signature>(1));
  KeyInput input;
  input.amount = 10;
  input.outputIndexes.push_back(0);
  tx.inputs.push_back(input);
  tx.outputs.resize(outputs);
  for (auto& output : tx.outputs) {
    output.target = KeyOutput{randomPoint()};
  }
  addTransactionPublicKeyToExtra(tx.extra, randomPoint());
  return tx;
}

std::vector<ChainScanFindingKind> kindsOf(const Transaction& tx, uint8_t version, bool janusRequired, bool coinbase) {
  std::vector<ChainScanFindingKind> kinds;
  for (const auto& finding : inspectTransaction(tx, version, janusRequired, coinbase)) {
    kinds.push_back(finding.kind);
  }
  return kinds;
}

using Kinds = std::vector<ChainScanFindingKind>;

TEST(ChainScanFindings, OrdinaryTransactionHasNoFindings) {
  EXPECT_TRUE(kindsOf(keyInputTransaction(2), BLOCK_MAJOR_VERSION_10, false, false).empty());
}

TEST(ChainScanFindings, JanusBaseBeforeV11IsReportedAndRejected) {
  Transaction tx = keyInputTransaction(2);
  addJanusBasesToExtra(tx.extra, {randomPoint()});
  EXPECT_EQ(kindsOf(tx, BLOCK_MAJOR_VERSION_10, false, false),
            (Kinds{ChainScanFindingKind::JanusBase, ChainScanFindingKind::RejectedByRules}));
}

TEST(ChainScanFindings, WellFormedPerOutputKeysAtV11AreReportedNotRejected) {
  Transaction tx = keyInputTransaction(2);
  addAdditionalPubKeysToExtra(tx.extra, {randomPoint(), randomPoint()});
  addJanusBasesToExtra(tx.extra, {randomPoint(), randomPoint()});
  EXPECT_EQ(kindsOf(tx, BLOCK_MAJOR_VERSION_11, false, false),
            (Kinds{ChainScanFindingKind::AdditionalPubKeys, ChainScanFindingKind::JanusBase}));
}

TEST(ChainScanFindings, CommitmentOutputIsReported) {
  Transaction tx = keyInputTransaction(1);
  TransactionOutput commitment;
  commitment.target = TransactionOutputCommitment{};
  tx.outputs.push_back(commitment);
  EXPECT_EQ(kindsOf(tx, BLOCK_MAJOR_VERSION_10, false, false), (Kinds{ChainScanFindingKind::CommitmentOutput}));
}

TEST(ChainScanFindings, CoinbaseFieldsAreReportedButNotJudgedByTheTxRule) {
  Transaction coinbase;
  coinbase.inputs.push_back(BaseInput{5});
  addJanusBasesToExtra(coinbase.extra, {randomPoint()});
  EXPECT_EQ(kindsOf(coinbase, BLOCK_MAJOR_VERSION_10, false, true), (Kinds{ChainScanFindingKind::JanusBase}));
}

TEST(ChainScanFindings, MissingJanusBaseAfterTheGraceWindowIsRejected) {
  EXPECT_EQ(kindsOf(keyInputTransaction(1), BLOCK_MAJOR_VERSION_11, true, false),
            (Kinds{ChainScanFindingKind::RejectedByRules}));
}

// A chain held in memory that answers like /getblocks.bin, a few blocks per page.
class InMemoryChain : public IBlockSource {
public:
  InMemoryChain(const Currency& currency, size_t pageSize) : m_pageSize(pageSize) {
    Block genesis = currency.genesisBlock();
    append(genesis, {});
  }

  // Appends a block whose one transaction carries a 0x09 key when asked to.
  void extend(bool withJanusBase) {
    Block block{};
    block.majorVersion = BLOCK_MAJOR_VERSION_1;
    block.minorVersion = 0;
    block.nonce = 0;
    block.baseTransaction.version = TRANSACTION_VERSION_1;
    block.baseTransaction.unlockTime = 0;
    block.timestamp = m_blocks.size();
    block.previousBlockHash = m_hashes.back();
    block.baseTransaction.inputs.push_back(BaseInput{static_cast<uint32_t>(m_blocks.size())});
    addTransactionPublicKeyToExtra(block.baseTransaction.extra, randomPoint());
    std::vector<Transaction> txs;
    if (withJanusBase) {
      Transaction tx = keyInputTransaction(1);
      addJanusBasesToExtra(tx.extra, {randomPoint()});
      txs.push_back(tx);
    }
    append(block, txs);
  }

  // Drops every block above the given height, so later blocks form a fork.
  void truncateAbove(size_t height) {
    m_blocks.resize(height + 1);
    m_hashes.resize(height + 1);
  }

  BlockBatch blocksFrom(const Crypto::Hash& knownHash) override {
    const auto known = std::find(m_hashes.begin(), m_hashes.end(), knownHash);
    const size_t start = known == m_hashes.end() ? 0 : static_cast<size_t>(known - m_hashes.begin());
    BlockBatch batch;
    batch.startHeight = start;
    batch.chainHeight = m_blocks.size();
    for (size_t i = start; i < m_blocks.size() && batch.blocks.size() < m_pageSize; ++i) {
      batch.blocks.push_back(m_blocks[i]);
    }
    return batch;
  }

  Crypto::Hash hashAt(uint32_t height) override { return m_hashes.at(height); }

private:
  void append(const Block& block, const std::vector<Transaction>& txs) {
    block_complete_entry entry;
    entry.block = Common::asString(toBinaryArray(block));
    for (const auto& tx : txs) entry.txs.push_back(Common::asString(toBinaryArray(tx)));
    m_blocks.push_back(entry);
    m_hashes.push_back(get_block_hash(block));
  }

  size_t m_pageSize;
  std::vector<block_complete_entry> m_blocks;
  std::vector<Crypto::Hash> m_hashes;
};

struct ScanRecorder {
  std::vector<uint32_t> janusHeights;
  std::vector<std::string> progress;
  FindingReporter findings() {
    return [this](uint32_t height, const Crypto::Hash&, const ChainScanFinding& finding) {
      if (finding.kind == ChainScanFindingKind::JanusBase) janusHeights.push_back(height);
    };
  }
  ProgressReporter messages() {
    return [this](const std::string& message) { progress.push_back(message); };
  }
};

class ChainScannerLoop : public ::testing::Test {
protected:
  ChainScannerLoop() : logger(Logging::ERROR), currency(CurrencyBuilder(logger).testnet(true).currency()) {}
  Logging::ConsoleLogger logger;
  Currency currency;
};

TEST_F(ChainScannerLoop, PagesThroughTheWholeChainOnce) {
  InMemoryChain chain(currency, 3);
  for (size_t height = 1; height <= 10; ++height) chain.extend(height == 4 || height == 9);
  ScanRecorder recorder;
  const ScanTotals totals = scanChain(chain, currency, ScanRange{}, recorder.findings(), recorder.messages());
  EXPECT_EQ(totals.blocks, 11u);
  EXPECT_EQ(recorder.janusHeights, (std::vector<uint32_t>{4, 9}));
  EXPECT_EQ(totals.findings.at(ChainScanFindingKind::RejectedByRules), 2u);
}

TEST_F(ChainScannerLoop, HonoursTheRange) {
  InMemoryChain chain(currency, 4);
  for (size_t height = 1; height <= 10; ++height) chain.extend(height == 2 || height == 6 || height == 9);
  ScanRecorder recorder;
  const ScanTotals totals = scanChain(chain, currency, ScanRange{5, 8}, recorder.findings(), recorder.messages());
  EXPECT_EQ(totals.blocks, 4u);
  EXPECT_EQ(recorder.janusHeights, (std::vector<uint32_t>{6}));
}

TEST_F(ChainScannerLoop, RescansAForkThatReplacesInspectedBlocks) {
  InMemoryChain chain(currency, 2);
  for (size_t height = 1; height <= 3; ++height) chain.extend(false);
  ScanRecorder recorder;
  bool forked = false;
  const ProgressReporter forkAfterFirstPage = [&](const std::string& message) {
    recorder.progress.push_back(message);
    if (!forked) {
      forked = true;
      chain.truncateAbove(0);
      for (size_t height = 1; height <= 3; ++height) chain.extend(height == 1);
    }
  };
  const ScanTotals totals = scanChain(chain, currency, ScanRange{}, recorder.findings(), forkAfterFirstPage);
  EXPECT_EQ(recorder.janusHeights, (std::vector<uint32_t>{1}));
  EXPECT_TRUE(std::any_of(recorder.progress.begin(), recorder.progress.end(),
                          [](const std::string& m) { return m.find("reorganised") != std::string::npos; }));
  EXPECT_GE(totals.blocks, 4u);
}

} // namespace
