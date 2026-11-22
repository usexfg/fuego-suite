// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CryptoNote.h"

namespace CryptoNote {

enum class ChainScanFindingKind {
  AdditionalPubKeys,   // a parsed 0x06 field
  JanusBase,           // a parsed 0x09 field
  CommitmentOutput,    // a TransactionOutputCommitment output
  RejectedByRules,     // validateSubaddressTxExtra refuses the tx at its block
};

struct ChainScanFinding {
  ChainScanFindingKind kind;
  std::string detail;
};

// Everything a node built from this tree would object to, or that F5 would
// strand, in one transaction of a block at the given version and height.
std::vector<ChainScanFinding> inspectTransaction(const Transaction& tx, uint8_t blockMajorVersion,
                                                 bool janusBaseRequired, bool isCoinbase);

const char* findingName(ChainScanFindingKind kind);

} // namespace CryptoNote
