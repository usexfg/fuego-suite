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

// Every v11 tx may carry a lone 0x09 (F8 filler), so only a funsa recipient of a
// single-key tx decrypts messages with it; everyone else uses the main key.
Crypto::PublicKey messageTransactionKey(
    const std::vector<uint8_t>& extra,
    const Crypto::PublicKey& mainTxPubKey,
    bool recipientIsSubaddress);

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

// F8: give a tx that has no 0x09 the filler janus base of its tx secret. Leaves a
// tx that already carries 0x09 untouched; fails on an extra that does not parse.
bool addJanusFillerWhenMissing(std::vector<uint8_t>& extra, const Crypto::SecretKey& txSecret);

bool validateSubaddressTxExtra(
    const Transaction& tx,
    uint8_t blockMajorVersion,
    bool janusBaseRequired,
    std::string* error);

} // namespace CryptoNote
