// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "SubaddressTx.h"

#include <algorithm>
#include <unordered_set>
#include <typeinfo>

#include "CryptoNoteConfig.h"
#include "TransactionExtra.h"
#include "crypto/crypto.h"
#include "crypto/subaddress.h"

namespace CryptoNote {

namespace {

size_t countFieldsOfType(const std::vector<TransactionExtraField>& fields, const std::type_info& type) {
  return static_cast<size_t>(std::count_if(fields.begin(), fields.end(),
      [&type](const TransactionExtraField& f) { return f.type() == type; }));
}

bool isNonChangeDestination(const TransactionDestinationEntry& dest, const Crypto::PublicKey& senderSpendPublicKey) {
  return dest.isSubaddress || dest.addr.spendPublicKey != senderSpendPublicKey;
}

bool replaceSubaddressFields(
    std::vector<uint8_t>& extra,
    const Crypto::PublicKey* replacementMainTxPubKey,
    const SubaddressTxKeys& keys) {
  std::vector<TransactionExtraField> fields;
  if (!extra.empty() && !parseTransactionExtra(extra, fields)) {
    return false;
  }

  fields.erase(std::remove_if(fields.begin(), fields.end(), [](const TransactionExtraField& f) {
    return f.type() == typeid(TransactionExtraAdditionalPubKeys) || f.type() == typeid(TransactionExtraJanusBase);
  }), fields.end());

  if (replacementMainTxPubKey != nullptr) {
    auto mainKey = std::find_if(fields.begin(), fields.end(),
        [](const TransactionExtraField& f) { return f.type() == typeid(TransactionExtraPublicKey); });
    if (mainKey == fields.end()) {
      fields.insert(fields.begin(), TransactionExtraPublicKey{*replacementMainTxPubKey});
    } else {
      *mainKey = TransactionExtraPublicKey{*replacementMainTxPubKey};
    }
  }

  if (!keys.additionalPubKeys.empty()) {
    fields.push_back(TransactionExtraAdditionalPubKeys{keys.additionalPubKeys});
  }
  if (!keys.janusBases.empty()) {
    fields.push_back(TransactionExtraJanusBase{keys.janusBases});
  }

  std::vector<uint8_t> rewritten;
  if (!writeTransactionExtra(rewritten, fields)) {
    return false;
  }
  extra.swap(rewritten);
  return true;
}

size_t countDistinctSubaddresses(const std::vector<TransactionDestinationEntry>& dests) {
  std::unordered_set<Crypto::PublicKey> spendKeys;
  for (const auto& dest : dests) {
    if (dest.isSubaddress) {
      spendKeys.insert(dest.addr.spendPublicKey);
    }
  }
  return spendKeys.size();
}

bool planSingleSubaddressTx(
    const Crypto::SecretKey& txSecret,
    const Crypto::PublicKey& txPubKeyRG,
    const Crypto::SecretKey& senderViewSecret,
    const std::vector<TransactionDestinationEntry>& dests,
    SubaddressOutputPlan& plan) {
  const auto subaddress = std::find_if(dests.begin(), dests.end(),
      [](const TransactionDestinationEntry& d) { return d.isSubaddress; });
  if (!Crypto::subaddressTxPubKey(txSecret, subaddress->addr.spendPublicKey, plan.mainTxPubKey)) {
    return false;
  }
  plan.singleSubaddressTx = true;
  plan.txKeys.janusBases = {txPubKeyRG};

  for (const auto& dest : dests) {
    Crypto::KeyDerivation derivation;
    const bool derived = dest.isSubaddress
        ? Crypto::generate_key_derivation(dest.addr.viewPublicKey, txSecret, derivation)
        : Crypto::generate_key_derivation(plan.mainTxPubKey, senderViewSecret, derivation);
    if (!derived) {
      return false;
    }
    plan.outputDerivations.push_back(derivation);
  }
  return true;
}

bool planPerOutputTxKeys(
    const Crypto::SecretKey& txSecret,
    const std::vector<TransactionDestinationEntry>& dests,
    SubaddressOutputPlan& plan) {
  for (size_t i = 0; i < dests.size(); ++i) {
    OutputTxKey key;
    if (!deriveOutputTxKey(txSecret, i, dests[i].addr, dests[i].isSubaddress, key)) {
      return false;
    }
    plan.txKeys.additionalPubKeys.push_back(key.txPublicKey);
    plan.txKeys.janusBases.push_back(key.janusBase);
    plan.outputDerivations.push_back(key.derivation);
  }
  return true;
}

bool planMainKeyOnly(
    const Crypto::SecretKey& txSecret,
    const std::vector<TransactionDestinationEntry>& dests,
    SubaddressOutputPlan& plan) {
  for (const auto& dest : dests) {
    Crypto::KeyDerivation derivation;
    if (!Crypto::generate_key_derivation(dest.addr.viewPublicKey, txSecret, derivation)) {
      return false;
    }
    plan.outputDerivations.push_back(derivation);
  }
  return true;
}

} // namespace

Crypto::PublicKey unlinkableJanusFiller() {
  Crypto::PublicKey filler;
  Crypto::SecretKey discarded;
  Crypto::generate_keys(filler, discarded);
  return filler;
}

bool deriveOutputTxKey(
    const Crypto::SecretKey& txSecret,
    size_t outputIndex,
    const AccountPublicAddress& to,
    bool toSubaddress,
    OutputTxKey& key) {
  const Crypto::SecretKey outputSecret = Crypto::additionalTxSecret(txSecret, outputIndex);
  if (toSubaddress) {
    if (!Crypto::subaddressTxPubKey(outputSecret, to.spendPublicKey, key.txPublicKey) ||
        !Crypto::secret_key_to_public_key(outputSecret, key.janusBase)) {
      return false;
    }
  } else {
    if (!Crypto::secret_key_to_public_key(outputSecret, key.txPublicKey)) {
      return false;
    }
    key.janusBase = unlinkableJanusFiller();
  }
  return Crypto::generate_key_derivation(to.viewPublicKey, outputSecret, key.derivation);
}

SubaddressTxKeys readSubaddressTxKeys(const std::vector<uint8_t>& extra) {
  SubaddressTxKeys keys;
  std::vector<TransactionExtraField> fields;
  if (!parseTransactionExtra(extra, fields)) {
    return keys;
  }
  TransactionExtraAdditionalPubKeys additional;
  if (findTransactionExtraFieldByType(fields, additional)) {
    keys.additionalPubKeys = additional.keys;
  }
  TransactionExtraJanusBase janus;
  if (findTransactionExtraFieldByType(fields, janus)) {
    keys.janusBases = janus.keys;
  }
  return keys;
}

bool janusBaseForOutput(
    const SubaddressTxKeys& keys,
    size_t outputIndex,
    bool matchedViaAdditionalKey,
    Crypto::PublicKey& janusBase) {
  if (matchedViaAdditionalKey) {
    if (keys.janusBases.size() != keys.additionalPubKeys.size() || outputIndex >= keys.janusBases.size()) {
      return false;
    }
    janusBase = keys.janusBases[outputIndex];
    return true;
  }
  if (!keys.additionalPubKeys.empty() || keys.janusBases.size() != 1) {
    return false;
  }
  janusBase = keys.janusBases.front();
  return true;
}

Crypto::PublicKey messageTransactionKey(const std::vector<uint8_t>& extra, const Crypto::PublicKey& mainTxPubKey) {
  const SubaddressTxKeys keys = readSubaddressTxKeys(extra);
  if (keys.additionalPubKeys.empty() && keys.janusBases.size() == 1) {
    return keys.janusBases.front();
  }
  return mainTxPubKey;
}

bool writeSubaddressTxKeys(std::vector<uint8_t>& extra, const SubaddressTxKeys& keys) {
  return replaceSubaddressFields(extra, nullptr, keys);
}

bool encodeSubaddressTxKeys(const SubaddressTxKeys& keys, std::vector<uint8_t>& encoded) {
  return addAdditionalPubKeysToExtra(encoded, keys.additionalPubKeys) &&
         addJanusBasesToExtra(encoded, keys.janusBases);
}

bool planSubaddressOutputs(
    const Crypto::SecretKey& txSecret,
    const Crypto::PublicKey& txPubKeyRG,
    const Crypto::SecretKey& senderViewSecret,
    const Crypto::PublicKey& senderSpendPublicKey,
    const std::vector<TransactionDestinationEntry>& dests,
    SubaddressOutputPlan& plan) {
  plan = SubaddressOutputPlan{};
  plan.mainTxPubKey = txPubKeyRG;

  const size_t nonChangeStandard = static_cast<size_t>(std::count_if(dests.begin(), dests.end(),
      [&senderSpendPublicKey](const TransactionDestinationEntry& d) {
        return !d.isSubaddress && isNonChangeDestination(d, senderSpendPublicKey);
      }));
  const size_t distinctSubaddresses = countDistinctSubaddresses(dests);

  if (distinctSubaddresses == 0) {
    return planMainKeyOnly(txSecret, dests, plan);
  }
  if (distinctSubaddresses == 1 && nonChangeStandard == 0) {
    return planSingleSubaddressTx(txSecret, txPubKeyRG, senderViewSecret, dests, plan);
  }
  return planPerOutputTxKeys(txSecret, dests, plan);
}

bool derivePlannedOutputKey(
    const TransactionDestinationEntry& dest,
    const SubaddressOutputPlan& plan,
    size_t outputIndex,
    Crypto::PublicKey& ephemeral) {
  if (outputIndex >= plan.outputDerivations.size()) {
    return false;
  }
  return Crypto::derive_public_key(plan.outputDerivations[outputIndex], outputIndex, dest.addr.spendPublicKey, ephemeral);
}

bool applySubaddressTxExtra(std::vector<uint8_t>& extra, const SubaddressOutputPlan& plan) {
  if (plan.singleSubaddressTx) {
    return replaceSubaddressFields(extra, &plan.mainTxPubKey, plan.txKeys);
  }
  if (plan.txKeys.additionalPubKeys.empty()) {
    return true;
  }
  return replaceSubaddressFields(extra, nullptr, plan.txKeys);
}

bool validateSubaddressTxExtra(
    const Transaction& tx,
    uint8_t blockMajorVersion,
    std::string* error) {
  auto fail = [&](const char* msg) {
    if (error) {
      *error = msg;
    }
    return false;
  };

  std::vector<TransactionExtraField> fields;
  if (!parseTransactionExtra(tx.extra, fields)) {
    return true;
  }

  if (countFieldsOfType(fields, typeid(TransactionExtraAdditionalPubKeys)) > 1) {
    return fail("duplicate TX_EXTRA_ADDITIONAL_PUBKEYS");
  }
  if (countFieldsOfType(fields, typeid(TransactionExtraJanusBase)) > 1) {
    return fail("duplicate TX_EXTRA_JANUS_BASE");
  }

  TransactionExtraAdditionalPubKeys additional;
  const bool hasAdditional = findTransactionExtraFieldByType(fields, additional);
  TransactionExtraJanusBase janus;
  const bool hasJanus = findTransactionExtraFieldByType(fields, janus);

  if ((hasAdditional || hasJanus) && !funsaSubaddressActivated(blockMajorVersion)) {
    return fail("subaddress extra tags not active before block major version 11");
  }

  if (hasAdditional) {
    if (additional.keys.size() != tx.outputs.size()) {
      return fail("additional tx pubkeys count must equal output count");
    }
    if (!hasJanus || janus.keys.size() != tx.outputs.size()) {
      return fail("additional tx pubkeys need one janus base per output");
    }
    if (tx.extra.size() > parameters::MAX_TX_EXTRA_SIZE) {
      return fail("tx extra exceeds MAX_TX_EXTRA_SIZE");
    }
    for (const auto& key : additional.keys) {
      if (!Crypto::check_key(key)) {
        return fail("additional tx pubkey is not a valid curve point");
      }
    }
  } else if (hasJanus && janus.keys.size() != 1) {
    return fail("a single-key transaction carries exactly one janus base");
  }

  for (const auto& key : janus.keys) {
    if (!Crypto::check_key(key)) {
      return fail("janus base is not a valid curve point");
    }
  }

  return true;
}

} // namespace CryptoNote
