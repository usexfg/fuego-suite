// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "SubaddressTxProof.h"

#include <cstring>
#include <set>

#include "Common/Base58.h"
#include "SubaddressTx.h"
#include "TransactionExtra.h"
#include "crypto/subaddress.h"

namespace CryptoNote {

namespace {

const char PROOF_V2_HEADER[] = "ProofV2";
const size_t PROOF_V2_HEADER_LENGTH = sizeof(PROOF_V2_HEADER) - 1;
const size_t PROOF_ITEM_SIZE = 1 + sizeof(Crypto::PublicKey) + sizeof(Crypto::Signature);
const size_t MAX_PROOF_ITEMS = 1 + MAIN_TX_KEY_INDEX;

struct ProofItem {
  uint8_t txKeyIndex;
  Crypto::PublicKey sharedSecret;
  Crypto::Signature signature;
};

const Crypto::PublicKey& addressViewKey(const AccountPublicAddress& address) {
  return address.viewPublicKey;
}

bool txKeyBaseMatches(const Crypto::SecretKey& secret, const Crypto::PublicKey& txKey,
                      const AccountPublicAddress& address, bool isSubaddress) {
  Crypto::PublicKey expected;
  const bool derived = isSubaddress
      ? Crypto::subaddressTxPubKey(secret, address.spendPublicKey, expected)
      : Crypto::secret_key_to_public_key(secret, expected);
  return derived && expected == txKey;
}

bool sharedSecretDerivation(const Crypto::PublicKey& sharedSecret, Crypto::KeyDerivation& derivation) {
  return Crypto::generate_key_derivation(sharedSecret, Crypto::EllipticCurveScalar2SecretKey(Crypto::I), derivation);
}

bool keyOutputPaysAddress(const TransactionOutput& output, size_t outputIndex,
                          const Crypto::KeyDerivation& derivation, const AccountPublicAddress& address) {
  if (output.target.type() != typeid(KeyOutput)) {
    return false;
  }
  Crypto::PublicKey expected;
  return Crypto::derive_public_key(derivation, outputIndex, address.spendPublicKey, expected) &&
         expected == boost::get<KeyOutput>(output.target).key;
}

bool anyOutputPaysAddress(const Transaction& tx, const Crypto::KeyDerivation& derivation,
                          const AccountPublicAddress& address, uint8_t txKeyIndex) {
  for (size_t i = 0; i < tx.outputs.size(); ++i) {
    const bool inScope = txKeyIndex == MAIN_TX_KEY_INDEX || i == txKeyIndex;
    if (inScope && keyOutputPaysAddress(tx.outputs[i], i, derivation, address)) {
      return true;
    }
  }
  return false;
}

bool proveTxKey(const Transaction& tx, const Crypto::Hash& txid, const AccountPublicAddress& address,
                bool isSubaddress, const Crypto::SecretKey& secret, const Crypto::PublicKey& txKey,
                uint8_t txKeyIndex, std::vector<ProofItem>& items) {
  if (!txKeyBaseMatches(secret, txKey, address, isSubaddress)) {
    return true;
  }
  ProofItem item{txKeyIndex, {}, {}};
  Crypto::KeyDerivation derivation;
  if (!Crypto::scalarmultKeyNoCofactor(addressViewKey(address), secret, item.sharedSecret) ||
      !sharedSecretDerivation(item.sharedSecret, derivation)) {
    return false;
  }
  if (!anyOutputPaysAddress(tx, derivation, address, txKeyIndex)) {
    return true;
  }
  if (isSubaddress) {
    Crypto::generate_tx_proof_subaddress(txid, txKey, address.spendPublicKey, address.viewPublicKey,
                                         item.sharedSecret, secret, item.signature);
  } else {
    Crypto::generate_tx_proof(txid, txKey, address.viewPublicKey, item.sharedSecret, secret, item.signature);
  }
  items.push_back(item);
  return true;
}

std::string encodeItems(const std::vector<ProofItem>& items) {
  std::string blob;
  for (const auto& item : items) {
    blob.push_back(static_cast<char>(item.txKeyIndex));
    blob.append(reinterpret_cast<const char*>(&item.sharedSecret), sizeof(item.sharedSecret));
    blob.append(reinterpret_cast<const char*>(&item.signature), sizeof(item.signature));
  }
  return std::string(PROOF_V2_HEADER) + Tools::Base58::encode(blob);
}

bool decodeItems(const std::string& proof, std::vector<ProofItem>& items) {
  if (!isPaymentProofV2(proof) ||
      proof.size() > PROOF_V2_HEADER_LENGTH + Tools::Base58::encode(std::string(MAX_PROOF_ITEMS * PROOF_ITEM_SIZE, '\xFF')).size()) {
    return false;
  }
  std::string blob;
  if (!Tools::Base58::decode(proof.substr(PROOF_V2_HEADER_LENGTH), blob) ||
      blob.empty() || blob.size() % PROOF_ITEM_SIZE != 0 || blob.size() / PROOF_ITEM_SIZE > MAX_PROOF_ITEMS) {
    return false;
  }
  for (size_t offset = 0; offset < blob.size(); offset += PROOF_ITEM_SIZE) {
    ProofItem item;
    item.txKeyIndex = static_cast<uint8_t>(blob[offset]);
    memcpy(&item.sharedSecret, blob.data() + offset + 1, sizeof(item.sharedSecret));
    memcpy(&item.signature, blob.data() + offset + 1 + sizeof(item.sharedSecret), sizeof(item.signature));
    items.push_back(item);
  }
  return true;
}

bool txKeyForItem(const ProofItem& item, const Crypto::PublicKey& mainTxKey,
                  const SubaddressTxKeys& keys, Crypto::PublicKey& txKey) {
  if (item.txKeyIndex == MAIN_TX_KEY_INDEX) {
    txKey = mainTxKey;
    return true;
  }
  if (item.txKeyIndex >= keys.additionalPubKeys.size()) {
    return false;
  }
  txKey = keys.additionalPubKeys[item.txKeyIndex];
  return true;
}

} // namespace

bool isPaymentProofV2(const std::string& proof) {
  return proof.compare(0, PROOF_V2_HEADER_LENGTH, PROOF_V2_HEADER) == 0;
}

bool generatePaymentProof(
    const Transaction& tx,
    const Crypto::Hash& txid,
    const AccountPublicAddress& address,
    bool isSubaddress,
    const Crypto::SecretKey& txSecret,
    std::string& proof) {
  std::vector<ProofItem> items;
  const Crypto::PublicKey mainTxKey = getTransactionPublicKeyFromExtra(tx.extra);
  if (!proveTxKey(tx, txid, address, isSubaddress, txSecret, mainTxKey, MAIN_TX_KEY_INDEX, items)) {
    return false;
  }

  const SubaddressTxKeys keys = readSubaddressTxKeys(tx.extra);
  const size_t perOutputKeys = std::min<size_t>(keys.additionalPubKeys.size(), MAIN_TX_KEY_INDEX);
  for (size_t n = 0; n < perOutputKeys; ++n) {
    if (!proveTxKey(tx, txid, address, isSubaddress, Crypto::additionalTxSecret(txSecret, n),
                    keys.additionalPubKeys[n], static_cast<uint8_t>(n), items)) {
      return false;
    }
  }

  if (items.empty()) {
    return false;
  }
  proof = encodeItems(items);
  return true;
}

bool checkPaymentProof(
    const Transaction& tx,
    const Crypto::Hash& txid,
    const AccountPublicAddress& address,
    bool isSubaddress,
    const std::string& proof,
    PaymentProofResult& result) {
  result = PaymentProofResult{};
  std::vector<ProofItem> items;
  if (!decodeItems(proof, items)) {
    return false;
  }

  const Crypto::PublicKey mainTxKey = getTransactionPublicKeyFromExtra(tx.extra);
  const SubaddressTxKeys keys = readSubaddressTxKeys(tx.extra);
  std::set<size_t> countedOutputs;

  for (const auto& item : items) {
    Crypto::PublicKey txKey;
    if (!txKeyForItem(item, mainTxKey, keys, txKey)) {
      return false;
    }
    const bool signatureValid = isSubaddress
        ? Crypto::check_tx_proof_subaddress(txid, txKey, address.spendPublicKey, address.viewPublicKey,
                                            item.sharedSecret, item.signature)
        : Crypto::check_tx_proof(txid, txKey, address.viewPublicKey, item.sharedSecret, item.signature);
    Crypto::KeyDerivation derivation;
    if (!signatureValid || !sharedSecretDerivation(item.sharedSecret, derivation)) {
      return false;
    }

    for (size_t i = 0; i < tx.outputs.size(); ++i) {
      const bool inScope = item.txKeyIndex == MAIN_TX_KEY_INDEX || i == item.txKeyIndex;
      if (inScope && countedOutputs.count(i) == 0 && keyOutputPaysAddress(tx.outputs[i], i, derivation, address)) {
        countedOutputs.insert(i);
        result.receivedAmount += tx.outputs[i].amount;
        result.outputs.push_back(tx.outputs[i]);
      }
    }
  }
  return true;
}

} // namespace CryptoNote
