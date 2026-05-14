// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <boost/utility/value_init.hpp>
#include "../../include/CryptoNote.h"
#include "CryptoNoteBasic.h"
#include "CryptoNoteSerialization.h"

#include "../Serialization/BinaryOutputStreamSerializer.h"
#include "../Serialization/BinaryInputStreamSerializer.h"

namespace Logging {
class ILogger;
}

namespace CryptoNote {

bool parseAndValidateTransactionFromBinaryArray(const BinaryArray& transactionBinaryArray, Transaction& transaction, Crypto::Hash& transactionHash, Crypto::Hash& transactionPrefixHash);

struct TransactionSourceEntry {
  typedef std::pair<uint32_t, Crypto::PublicKey> OutputEntry;

  std::vector<OutputEntry> outputs;           //index + key
  size_t realOutput;                          //index in outputs vector of real output_entry
  Crypto::PublicKey realTransactionPublicKey; //incoming real tx public key
  size_t realOutputIndexInTransaction;        //index in transaction outputs vector
  uint64_t amount;                            //money

  // for sub-address inputs spend key= b_ij as opposed to main wallet key b.
  // hasCustomKeys = true for sub-address AccountKeys so constructTransaction signs with the correct key image for the input
  bool hasCustomKeys = false; // Defaults to false (main address inputs use sender_account_keys as normal)
  AccountKeys customKeys;
};

struct TransactionDestinationEntry {
  uint64_t amount;                    //money
  AccountPublicAddress addr;          //destination address

  TransactionDestinationEntry() : amount(0), addr(boost::value_initialized<AccountPublicAddress>()) {}
  TransactionDestinationEntry(uint64_t amount, const AccountPublicAddress &addr) : amount(amount), addr(addr) {}
};

struct tx_message_entry
{
  std::string message;
  bool encrypt;
  AccountPublicAddress addr;
};

bool generateDeterministicTransactionKeys(const Crypto::Hash &inputsHash, const Crypto::SecretKey &viewSecretKey, KeyPair &generatedKeys);
bool generateDeterministicTransactionKeys(const Transaction &tx, const Crypto::SecretKey &viewSecretKey, KeyPair &generatedKeys);

bool constructTransaction(
  const AccountKeys& senderAccountKeys,
  const std::vector<TransactionSourceEntry>& sources,
  const std::vector<TransactionDestinationEntry>& destinations,
  const std::vector<tx_message_entry>& messages,
  uint64_t ttl, std::vector<uint8_t> extra, Transaction& transaction, uint64_t unlock_time, Logging::ILogger& log, Crypto::SecretKey& transactionSK);

inline bool constructTransaction(
  const AccountKeys& sender_account_keys,
  const std::vector<TransactionSourceEntry>& sources,
  const std::vector<TransactionDestinationEntry>& destinations,
  std::vector<uint8_t> extra, Transaction& tx, uint64_t unlock_time, Logging::ILogger& log, Crypto::SecretKey& transactionSK) {

  return constructTransaction(sender_account_keys, sources, destinations, std::vector<tx_message_entry>(), 0, extra, tx, unlock_time, log, transactionSK);
}

bool is_out_to_acc(const AccountKeys& acc, const KeyOutput& out_key, const Crypto::PublicKey& tx_pub_key, size_t keyIndex);
bool is_out_to_acc(const AccountKeys& acc, const KeyOutput& out_key, const Crypto::KeyDerivation& derivation, size_t keyIndex);
bool lookup_acc_outs(const AccountKeys& acc, const Transaction& tx, const Crypto::PublicKey& tx_pub_key, std::vector<size_t>& outs, uint64_t& money_transfered);
bool lookup_acc_outs(const AccountKeys& acc, const Transaction& tx, std::vector<size_t>& outs, uint64_t& money_transfered);
bool generate_key_image_helper(const AccountKeys& ack, const Crypto::PublicKey& tx_public_key, size_t real_output_index, KeyPair& in_ephemeral, Crypto::KeyImage& ki);
std::string short_hash_str(const Crypto::Hash& h);

bool get_block_hashing_blob(const Block& b, BinaryArray& blob);
bool get_parent_block_hashing_blob(const Block& b, BinaryArray& blob);
bool get_aux_block_header_hash(const Block& b, Crypto::Hash& res);
bool get_block_hash(const Block& b, Crypto::Hash& res);
Crypto::Hash get_block_hash(const Block& b);
bool get_block_longhash(Crypto::cn_context &context, const Block& b, Crypto::Hash& res);
bool get_inputs_money_amount(const Transaction& tx, uint64_t& money);
uint64_t get_outs_money_amount(const Transaction& tx);
bool check_inputs_types_supported(const TransactionPrefix& tx);
bool check_outs_valid(const TransactionPrefix& tx, std::string* error = 0);
bool checkMultisignatureInputsDiff(const TransactionPrefix& tx);

bool check_money_overflow(const TransactionPrefix& tx);
bool check_outs_overflow(const TransactionPrefix& tx);
bool check_inputs_overflow(const TransactionPrefix& tx);
uint32_t get_block_height(const Block& b);
std::vector<uint32_t> relative_output_offsets_to_absolute(const std::vector<uint32_t>& off);
std::vector<uint32_t> absolute_output_offsets_to_relative(const std::vector<uint32_t>& off);


// 62387455827 -> 455827 + 7000000 + 80000000 + 300000000 + 2000000000 + 60000000000, where 455827 <= dust_threshold
template<typename chunk_handler_t, typename dust_handler_t>
void decompose_amount_into_digits(uint64_t amount, uint64_t dust_threshold, const chunk_handler_t& chunk_handler, const dust_handler_t& dust_handler) {
  if (0 == amount) {
    return;
  }

  bool is_dust_handled = false;
  uint64_t dust = 0;
  uint64_t order = 1;
  while (0 != amount) {
    uint64_t chunk = (amount % 10) * order;
    amount /= 10;
    order *= 10;

    if (dust + chunk <= dust_threshold) {
      dust += chunk;
    } else {
      if (!is_dust_handled && 0 != dust) {
        dust_handler(dust);
        is_dust_handled = true;
      }
      if (0 != chunk) {
        chunk_handler(chunk);
      }
    }
  }

  if (!is_dust_handled && 0 != dust) {
    dust_handler(dust);
  }
}

// Uniform denomination decomposition for coinbase privacy.
// Breaks amount into multiple outputs at power-of-10 tiers (>= dust_threshold).
// Unlike decompose_amount_into_digits which produces one output per decimal digit,
// this produces N outputs at each tier, making all coinbase outputs indistinguishable
// across blocks regardless of varying reward amounts.
inline void decompose_amount_uniform(uint64_t amount, uint64_t dust_threshold, std::vector<uint64_t>& out) {
  if (amount == 0) return;

  // Find the largest power-of-10 tier <= amount >= dust_threshold
  // 1 COIN = 10,000,000 ℏeat so max tier is 10Mℏ (1 XFG), ((really moreso ~1M (0.1 XFG) in this case, unless burns push blkrewards > COIN))
  static const uint64_t tiers[] = {
    10000000, // 1.0 XFG
    1000000,  // 0.1 XFG
    100000,   // 0.01 XFG
    10000,    // 0.001 XFG
    1000,     // 0.0001 XFG (dust threshold)
  };

  for (uint64_t tier : tiers) {
    if (tier < dust_threshold) break;
    while (amount >= tier) {
      out.push_back(tier);
      amount -= tier;
    }
  }

  // Sub-dust remainder: merge into last output (shouldn't happen with 1000 dust)
  if (amount > 0) {
    if (!out.empty()) {
      out.back() += amount;
    } else {
      out.push_back(amount);
    }
  }
}

void get_tx_tree_hash(const std::vector<Crypto::Hash>& tx_hashes, Crypto::Hash& h);
Crypto::Hash get_tx_tree_hash(const std::vector<Crypto::Hash>& tx_hashes);
Crypto::Hash get_tx_tree_hash(const Block& b);
bool is_valid_decomposed_amount(uint64_t amount);


}
