// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "ChainScanFindings.h"

#include <algorithm>

#include "CryptoNoteCore/SubaddressTx.h"
#include "CryptoNoteCore/TransactionExtra.h"

namespace CryptoNote {

namespace {

void addParsedSubaddressFields(const Transaction& tx, std::vector<ChainScanFinding>& findings) {
  std::vector<TransactionExtraField> fields;
  if (!parseTransactionExtra(tx.extra, fields)) {
    return;
  }
  TransactionExtraAdditionalPubKeys additional;
  if (findTransactionExtraFieldByType(fields, additional)) {
    findings.push_back({ChainScanFindingKind::AdditionalPubKeys, std::to_string(additional.keys.size()) + " keys"});
  }
  TransactionExtraJanusBase janus;
  if (findTransactionExtraFieldByType(fields, janus)) {
    findings.push_back({ChainScanFindingKind::JanusBase, std::to_string(janus.keys.size()) + " keys"});
  }
}

void addCommitmentOutputs(const Transaction& tx, std::vector<ChainScanFinding>& findings) {
  for (size_t i = 0; i < tx.outputs.size(); ++i) {
    if (tx.outputs[i].target.type() == typeid(TransactionOutputCommitment)) {
      findings.push_back({ChainScanFindingKind::CommitmentOutput, "output " + std::to_string(i)});
    }
  }
}

} // namespace

std::vector<ChainScanFinding> inspectTransaction(const Transaction& tx, uint8_t blockMajorVersion,
                                                 bool janusBaseRequired, bool isCoinbase) {
  std::vector<ChainScanFinding> findings;
  addParsedSubaddressFields(tx, findings);
  addCommitmentOutputs(tx, findings);
  std::string reason;
  if (!isCoinbase && !validateSubaddressTxExtra(tx, blockMajorVersion, janusBaseRequired, &reason)) {
    findings.push_back({ChainScanFindingKind::RejectedByRules, reason});
  }
  return findings;
}

const char* findingName(ChainScanFindingKind kind) {
  switch (kind) {
    case ChainScanFindingKind::AdditionalPubKeys: return "0x06";
    case ChainScanFindingKind::JanusBase: return "0x09";
    case ChainScanFindingKind::CommitmentOutput: return "commitment-output";
    case ChainScanFindingKind::RejectedByRules: return "rejected";
  }
  return "unknown";
}

} // namespace CryptoNote
