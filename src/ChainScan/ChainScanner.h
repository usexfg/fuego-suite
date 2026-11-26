// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolDefinitions.h"

#include "ChainScanFindings.h"

namespace CryptoNote {

// Blocks as a node's /getblocks.bin returns them: from the highest block it
// shares with knownHash (inclusive), in chain order.
struct BlockBatch {
  std::vector<block_complete_entry> blocks;
  uint64_t startHeight = 0;
  uint64_t chainHeight = 0;
};

class IBlockSource {
public:
  virtual ~IBlockSource() {}
  virtual BlockBatch blocksFrom(const Crypto::Hash& knownHash) = 0;
  virtual Crypto::Hash hashAt(uint32_t height) = 0;
};

struct ScanRange {
  uint32_t fromHeight = 0;
  uint32_t toHeight = UINT32_MAX;
};

struct ScanTotals {
  uint64_t blocks = 0;
  uint64_t transactions = 0;
  std::map<ChainScanFindingKind, uint64_t> findings;
};

using FindingReporter = std::function<void(uint32_t height, const Crypto::Hash& txHash, const ChainScanFinding&)>;
using ProgressReporter = std::function<void(const std::string& message)>;

ScanTotals scanChain(IBlockSource& source, const Currency& currency, const ScanRange& range,
                     const FindingReporter& reportFinding, const ProgressReporter& reportProgress);

} // namespace CryptoNote
