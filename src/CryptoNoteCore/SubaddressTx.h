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

struct OutputTxKey {
  Crypto::PublicKey txPublicKey;
  Crypto::PublicKey janusBase;
  Crypto::KeyDerivation derivation;
};

struct SubaddressTxKeys {
  std::vector<Crypto::PublicKey> additionalPubKeys;
  std::vector<Crypto::PublicKey> janusBases;
};

struct SubaddressOutputPlan {
  Crypto::PublicKey mainTxPubKey;
  bool singleSubaddressTx = false;
  SubaddressTxKeys txKeys;
  std::vector<Crypto::KeyDerivation> outputDerivations;
};

bool deriveOutputTxKey(
    const Crypto::SecretKey& txSecret,
    size_t outputIndex,
    const AccountPublicAddress& to,
    bool toSubaddress,
    OutputTxKey& key);

Crypto::PublicKey unlinkableJanusFiller();

SubaddressTxKeys readSubaddressTxKeys(const std::vector<uint8_t>& extra);

bool janusBaseForOutput(
    const SubaddressTxKeys& keys,
    size_t outputIndex,
    bool matchedViaAdditionalKey,
    Crypto::PublicKey& janusBase);

Crypto::PublicKey messageTransactionKey(
    const std::vector<uint8_t>& extra,
    const Crypto::PublicKey& mainTxPubKey);

bool writeSubaddressTxKeys(std::vector<uint8_t>& extra, const SubaddressTxKeys& keys);

bool encodeSubaddressTxKeys(const SubaddressTxKeys& keys, std::vector<uint8_t>& encoded);

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
