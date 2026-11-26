// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Prints funsa V2 golden vectors as JSON for other implementations (fuego-valise's
// Rust SDK) to test against byte for byte: key derivations, per-output tx secrets,
// the F8 filler, address strings, and transactions built by constructTransaction.

#include <iostream>
#include <string>
#include <vector>

#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteBasicImpl.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/Currency.h"
#include "Logging/ConsoleLogger.h"
#include "crypto/subaddress.h"

extern "C" {
#include "crypto/crypto-ops.h"
}

using namespace CryptoNote;

namespace {

Crypto::SecretKey repeatedScalar(uint8_t byte) {
  Crypto::SecretKey key;
  memset(&key, byte, sizeof(key));
  sc_reduce32(reinterpret_cast<unsigned char*>(&key));
  return key;
}

AccountKeys accountFrom(uint8_t viewByte, uint8_t spendByte) {
  AccountKeys keys;
  keys.viewSecretKey = repeatedScalar(viewByte);
  keys.spendSecretKey = repeatedScalar(spendByte);
  Crypto::secret_key_to_public_key(keys.viewSecretKey, keys.address.viewPublicKey);
  Crypto::secret_key_to_public_key(keys.spendSecretKey, keys.address.spendPublicKey);
  return keys;
}

std::string hex(const void* data, size_t size) { return Common::toHex(data, size); }
template <typename T> std::string hex(const T& pod) { return hex(&pod, sizeof(pod)); }

TransactionSourceEntry ownedSource(const AccountKeys& owner, uint64_t amount, uint8_t txKeyByte) {
  const Crypto::SecretKey previousTxSecret = repeatedScalar(txKeyByte);
  Crypto::PublicKey previousTxPublic;
  Crypto::secret_key_to_public_key(previousTxSecret, previousTxPublic);
  Crypto::KeyDerivation derivation;
  Crypto::PublicKey outputKey;
  Crypto::generate_key_derivation(previousTxPublic, owner.viewSecretKey, derivation);
  Crypto::derive_public_key(derivation, 0, owner.address.spendPublicKey, outputKey);
  TransactionSourceEntry source;
  source.outputs.push_back({0, outputKey});
  source.realOutput = 0;
  source.realTransactionPublicKey = previousTxPublic;
  source.realOutputIndexInTransaction = 0;
  source.amount = amount;
  return source;
}

std::string builtTransaction(const AccountKeys& sender, const std::vector<TransactionDestinationEntry>& destinations,
                             uint64_t inputAmount, uint8_t sourceByte, Logging::ILogger& logger) {
  Transaction tx;
  Crypto::SecretKey txSecret;
  if (!constructTransaction(sender, {ownedSource(sender, inputAmount, sourceByte)}, destinations, {}, 0, {}, tx, 0,
                            logger, txSecret, true)) {
    throw std::runtime_error("constructTransaction failed");
  }
  const BinaryArray blob = toBinaryArray(tx);
  return "{\"tx_secret\":\"" + hex(txSecret) + "\",\"blob\":\"" + Common::toHex(blob) + "\"}";
}

} // namespace

int main() {
  Logging::ConsoleLogger logger(Logging::ERROR);
  const Currency mainnet = CurrencyBuilder(logger).currency();
  const Currency testnet = CurrencyBuilder(logger).testnet(true).currency();
  const AccountKeys recipient = accountFrom(0x11, 0x22);
  const AccountKeys sender = accountFrom(0x44, 0x55);

  std::cout << "{\n  \"recipient\":{\"view_secret\":\"" << hex(recipient.viewSecretKey) << "\",\"spend_secret\":\""
            << hex(recipient.spendSecretKey) << "\",\"view_public\":\"" << hex(recipient.address.viewPublicKey)
            << "\",\"spend_public\":\"" << hex(recipient.address.spendPublicKey) << "\",\"address\":\""
            << mainnet.accountAddressAsString(recipient.address) << "\"},\n";

  std::cout << "  \"subaddresses\":[";
  const std::vector<std::pair<uint32_t, uint32_t>> indices = {{0, 1}, {0, 2}, {0, 7}, {1, 0}, {3, 42}};
  for (size_t i = 0; i < indices.size(); ++i) {
    const auto keys = Crypto::deriveSubAddressKeysV2(recipient.viewSecretKey, recipient.address.spendPublicKey,
                                                     &recipient.spendSecretKey, indices[i].first, indices[i].second);
    const AccountPublicAddress published{keys.spendPublicKey, keys.viewPublicKey};
    std::cout << (i ? "," : "") << "\n    {\"major\":" << indices[i].first << ",\"minor\":" << indices[i].second
              << ",\"spend_public\":\"" << hex(keys.spendPublicKey) << "\",\"view_public\":\"" << hex(keys.viewPublicKey)
              << "\",\"spend_secret\":\"" << hex(keys.spendSecretKey) << "\",\"address\":\""
              << mainnet.subAddressAsString(published) << "\",\"testnet_address\":\""
              << testnet.subAddressAsString(published) << "\"}";
  }
  std::cout << "\n  ],\n";

  const Crypto::SecretKey txSecret = repeatedScalar(0x33);
  const auto sub01 = Crypto::deriveSubAddressKeysV2(recipient.viewSecretKey, recipient.address.spendPublicKey, nullptr, 0, 1);
  Crypto::PublicKey subaddressTxKey;
  Crypto::subaddressTxPubKey(txSecret, sub01.spendPublicKey, subaddressTxKey);
  Crypto::PublicKey filler;
  Crypto::janusFillerForTxSecret(txSecret, filler);
  std::cout << "  \"tx_secret\":\"" << hex(txSecret) << "\",\n  \"additional_tx_secrets\":[";
  for (uint64_t n = 0; n < 3; ++n) {
    const Crypto::SecretKey rn = Crypto::additionalTxSecret(txSecret, n);
    std::cout << (n ? "," : "") << "\"" << hex(rn) << "\"";
  }
  std::cout << "],\n  \"subaddress_tx_pub_key_0_1\":\"" << hex(subaddressTxKey) << "\",\n  \"janus_filler\":\"" << hex(filler)
            << "\",\n";

  const auto sub02 = Crypto::deriveSubAddressKeysV2(recipient.viewSecretKey, recipient.address.spendPublicKey, nullptr, 0, 2);
  const AccountPublicAddress sub01Address{sub01.spendPublicKey, sub01.viewPublicKey};
  const AccountPublicAddress sub02Address{sub02.spendPublicKey, sub02.viewPublicKey};
  std::cout << "  \"sender\":{\"view_secret\":\"" << hex(sender.viewSecretKey) << "\",\"spend_secret\":\""
            << hex(sender.spendSecretKey) << "\"},\n";
  std::cout << "  \"single_key_tx\":" << builtTransaction(sender, {{700000, sub01Address, true}}, 1000000, 0x61, logger)
            << ",\n";
  std::cout << "  \"per_output_tx\":"
            << builtTransaction(sender, {{300000, sub02Address, true}, {200000, recipient.address, false}}, 1000000, 0x62, logger)
            << ",\n";
  std::cout << "  \"standard_tx\":" << builtTransaction(sender, {{400000, recipient.address, false}}, 1000000, 0x63, logger)
            << "\n}\n";
  return 0;
}
