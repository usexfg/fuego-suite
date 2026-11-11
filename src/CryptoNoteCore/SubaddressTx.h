// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "CryptoNoteFormatUtils.h"
#include "crypto/crypto.h"

namespace CryptoNote {

struct SubaddressOutputPlan {
  Crypto::PublicKey mainTxPubKey;
  Crypto::PublicKey janusBase;
  bool hasJanusBase = false;
  bool singleSubaddressTx = false;
  std::vector<Crypto::PublicKey> additionalPubKeys;
  std::vector<Crypto::SecretKey> outputSecrets;
};

bool planSubaddressOutputs(
    const Crypto::SecretKey& txSecret,
    const Crypto::PublicKey& txPubKeyRG,
    const Crypto::SecretKey& senderViewSecret,
    const Crypto::PublicKey& senderSpendPublicKey,
    const std::vector<TransactionDestinationEntry>& dests,
    SubaddressOutputPlan& plan);

bool derivePlannedOutputKey(
    const TransactionDestinationEntry& dest,
    const SubaddressOutputPlan& plan,
    const Crypto::SecretKey& senderViewSecret,
    size_t outputIndex,
    Crypto::PublicKey& ephemeral);

bool applySubaddressTxExtra(
    std::vector<uint8_t>& extra,
    const SubaddressOutputPlan& plan);

bool validateSubaddressTxExtra(
    const Transaction& tx,
    uint8_t blockMajorVersion,
    std::string* error);

} // namespace CryptoNote
