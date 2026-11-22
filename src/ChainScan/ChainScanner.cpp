// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "ChainScanner.h"

#include <stdexcept>

#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"

namespace CryptoNote {

namespace {

class BlockInspector {
public:
  BlockInspector(const Currency& currency, const FindingReporter& reportFinding, ScanTotals& totals)
      : m_currency(currency), m_reportFinding(reportFinding), m_totals(totals) {}

  // Returns the block's hash, the next request's starting point.
  Crypto::Hash inspect(const block_complete_entry& entry, uint32_t height) {
    Block block;
    if (!fromBinaryArray(block, Common::asBinaryArray(entry.block))) {
      throw std::runtime_error("block at height " + std::to_string(height) + " does not parse");
    }
    const bool janusBaseRequired = m_currency.janusBaseRequiredAtHeight(height);
    inspectTransaction(block.baseTransaction, height, block.majorVersion, janusBaseRequired, true);
    for (const std::string& txBlob : entry.txs) {
      Transaction tx;
      if (!fromBinaryArray(tx, Common::asBinaryArray(txBlob))) {
        throw std::runtime_error("a transaction at height " + std::to_string(height) + " does not parse");
      }
      inspectTransaction(tx, height, block.majorVersion, janusBaseRequired, false);
    }
    ++m_totals.blocks;
    return get_block_hash(block);
  }

private:
  void inspectTransaction(const Transaction& tx, uint32_t height, uint8_t majorVersion, bool janusBaseRequired,
                          bool isCoinbase) {
    ++m_totals.transactions;
    for (const ChainScanFinding& finding : CryptoNote::inspectTransaction(tx, majorVersion, janusBaseRequired, isCoinbase)) {
      ++m_totals.findings[finding.kind];
      m_reportFinding(height, getObjectHash(tx), finding);
    }
  }

  const Currency& m_currency;
  const FindingReporter& m_reportFinding;
  ScanTotals& m_totals;
};

} // namespace

ScanTotals scanChain(IBlockSource& source, const Currency& currency, const ScanRange& range,
                     const FindingReporter& reportFinding, const ProgressReporter& reportProgress) {
  ScanTotals totals;
  BlockInspector inspector(currency, reportFinding, totals);
  uint32_t nextHeight = range.fromHeight;
  Crypto::Hash knownHash = range.fromHeight == 0 ? currency.genesisBlockHash() : source.hashAt(range.fromHeight);

  for (;;) {
    const BlockBatch batch = source.blocksFrom(knownHash);
    // Below the last inspected block means the chain reorganised: inspect the new branch.
    if (nextHeight > 0 && batch.startHeight + 1 < nextHeight) {
      reportProgress("chain reorganised below height " + std::to_string(nextHeight - 1) + "; rescanning from " +
                     std::to_string(batch.startHeight));
      nextHeight = static_cast<uint32_t>(batch.startHeight);
    }
    bool progressed = false;
    for (size_t i = 0; i < batch.blocks.size(); ++i) {
      const uint64_t height = batch.startHeight + i;
      if (height < nextHeight) {
        continue;
      }
      if (height > range.toHeight) {
        return totals;
      }
      knownHash = inspector.inspect(batch.blocks[i], static_cast<uint32_t>(height));
      nextHeight = static_cast<uint32_t>(height) + 1;
      progressed = true;
    }
    if (!progressed) {
      return totals;
    }
    reportProgress("scanned to height " + std::to_string(nextHeight - 1) + " of " +
                   std::to_string(batch.chainHeight - 1));
  }
}

} // namespace CryptoNote
