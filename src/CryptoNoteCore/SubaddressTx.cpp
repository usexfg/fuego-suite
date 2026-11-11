// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "SubaddressTx.h"

#include <typeinfo>

#include "CryptoNoteConfig.h"
#include "TransactionExtra.h"
#include "crypto/crypto.h"
#include "crypto/subaddress.h"

namespace CryptoNote {

namespace {

size_t countFieldsOfType(const std::vector<TransactionExtraField>& fields, const std::type_info& type) {
  size_t n = 0;
  for (const auto& f : fields) {
    if (f.type() == type) {
      ++n;
    }
  }
  return n;
}

} // namespace

bool planSubaddressOutputs(
    const Crypto::SecretKey& txSecret,
    const Crypto::PublicKey& txPubKeyRG,
    const Crypto::SecretKey& senderViewSecret,
    const Crypto::PublicKey& senderSpendPublicKey,
    const std::vector<TransactionDestinationEntry>& dests,
    SubaddressOutputPlan& plan)
{
  plan = SubaddressOutputPlan{};
  plan.mainTxPubKey = txPubKeyRG;
  plan.outputSecrets.assign(dests.size(), txSecret);

  size_t nonChangeSub = 0;
  size_t nonChangeStd = 0;
  size_t firstSub = dests.size();
  for (size_t i = 0; i < dests.size(); ++i) {
    const bool isChange = dests[i].addr.spendPublicKey == senderSpendPublicKey && !dests[i].isSubaddress;
    if (isChange) {
      continue;
    }
    if (dests[i].isSubaddress) {
      ++nonChangeSub;
      if (firstSub == dests.size()) {
        firstSub = i;
      }
    } else {
      ++nonChangeStd;
    }
  }

  if (nonChangeSub == 0) {
    return true;
  }

  if (nonChangeSub == 1 && nonChangeStd == 0) {
    plan.singleSubaddressTx = true;
    plan.hasJanusBase = true;
    plan.janusBase = txPubKeyRG;
    if (!Crypto::subaddressTxPubKey(txSecret, dests[firstSub].addr.spendPublicKey, plan.mainTxPubKey)) {
      return false;
    }
    (void)senderViewSecret;
    return true;
  }

  plan.additionalPubKeys.resize(dests.size());
  for (size_t i = 0; i < dests.size(); ++i) {
    Crypto::PublicKey dummyPub;
    Crypto::SecretKey r_i;
    Crypto::generate_keys(dummyPub, r_i);
    plan.outputSecrets[i] = r_i;
    if (dests[i].isSubaddress) {
      if (!Crypto::subaddressTxPubKey(r_i, dests[i].addr.spendPublicKey, plan.additionalPubKeys[i])) {
        return false;
      }
    } else {
      if (!Crypto::secret_key_to_public_key(r_i, plan.additionalPubKeys[i])) {
        return false;
      }
    }
  }
  return true;
}

bool derivePlannedOutputKey(
    const TransactionDestinationEntry& dest,
    const SubaddressOutputPlan& plan,
    const Crypto::SecretKey& senderViewSecret,
    size_t outputIndex,
    Crypto::PublicKey& ephemeral)
{
  Crypto::KeyDerivation derivation;
  const bool isChange = plan.singleSubaddressTx && !dest.isSubaddress;

  if (isChange) {
    if (!Crypto::generate_key_derivation(plan.mainTxPubKey, senderViewSecret, derivation)) {
      return false;
    }
  } else {
    const Crypto::SecretKey& r = plan.outputSecrets[outputIndex];
    if (!Crypto::generate_key_derivation(dest.addr.viewPublicKey, r, derivation)) {
      return false;
    }
  }

  return Crypto::derive_public_key(derivation, outputIndex, dest.addr.spendPublicKey, ephemeral);
}

bool applySubaddressTxExtra(std::vector<uint8_t>& extra, const SubaddressOutputPlan& plan) {
  if (plan.singleSubaddressTx) {
    std::vector<TransactionExtraField> fields;
    if (!extra.empty() && !parseTransactionExtra(extra, fields)) {
      return false;
    }
    bool replaced = false;
    for (auto& f : fields) {
      if (f.type() == typeid(TransactionExtraPublicKey)) {
        f = TransactionExtraPublicKey{plan.mainTxPubKey};
        replaced = true;
      }
    }
    if (!replaced) {
      fields.insert(fields.begin(), TransactionExtraPublicKey{plan.mainTxPubKey});
    }
    extra.clear();
    if (!writeTransactionExtra(extra, fields)) {
      return false;
    }
    if (plan.hasJanusBase && !addJanusBaseToExtra(extra, plan.janusBase)) {
      return false;
    }
    return true;
  }

  if (!plan.additionalPubKeys.empty()) {
    return addAdditionalPubKeysToExtra(extra, plan.additionalPubKeys);
  }
  return true;
}

bool validateSubaddressTxExtra(
    const Transaction& tx,
    uint8_t blockMajorVersion,
    std::string* error)
{
  auto fail = [&](const char* msg) {
    if (error) {
      *error = msg;
    }
    return false;
  };

  std::vector<TransactionExtraField> fields;
  const bool parsed = parseTransactionExtra(tx.extra, fields);

  if (!parsed) {
    return true;
  }

  const size_t additionalCount = countFieldsOfType(fields, typeid(TransactionExtraAdditionalPubKeys));
  const size_t janusCount = countFieldsOfType(fields, typeid(TransactionExtraJanusBase));
  if (additionalCount > 1) {
    return fail("duplicate TX_EXTRA_ADDITIONAL_PUBKEYS");
  }
  if (janusCount > 1) {
    return fail("duplicate TX_EXTRA_JANUS_BASE");
  }

  TransactionExtraAdditionalPubKeys addl;
  const bool hasAdditional = findTransactionExtraFieldByType(fields, addl);
  TransactionExtraJanusBase janus;
  const bool hasJanus = findTransactionExtraFieldByType(fields, janus);

  if ((hasAdditional || hasJanus) && !funsaSubaddressActivated(blockMajorVersion)) {
    return fail("subaddress extra tags not active before block major version 11");
  }

  if (hasAdditional) {
    if (addl.keys.size() != tx.outputs.size()) {
      return fail("additional tx pubkeys count must equal output count");
    }
    if (tx.extra.size() > parameters::MAX_TX_EXTRA_SIZE) {
      return fail("tx extra exceeds MAX_TX_EXTRA_SIZE");
    }
    for (const auto& key : addl.keys) {
      if (!Crypto::check_key(key)) {
        return fail("additional tx pubkey is not a valid curve point");
      }
    }
  }

  if (hasJanus && !Crypto::check_key(janus.publicKey)) {
    return fail("janus base is not a valid curve point");
  }

  return true;
}

} // namespace CryptoNote
