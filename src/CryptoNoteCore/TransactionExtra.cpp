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

#include "TransactionExtra.h"
#include "CryptoNoteTools.h"
#include "CryptoNoteConfig.h"
#include "crypto/hash.h"
#include "crypto/crypto.h"
#include "crypto/chacha8.h"
#include "Common/int-util.h"
#include "Common/MemoryInputStream.h"
#include "Common/StreamTools.h"
#include "Common/StringTools.h"
#include "Common/Varint.h"
#include "Serialization/BinaryOutputStreamSerializer.h"
#include "Serialization/BinaryInputStreamSerializer.h"
#include "crypto/keccak.h"
#include <memory>
#include <sstream>
#include <chrono>
#include <iostream>

using namespace Crypto;
using namespace Common;

namespace CryptoNote
{

  bool parseTransactionExtra(const std::vector<uint8_t> &transactionExtra, std::vector<TransactionExtraField> &transactionExtraFields)
  {
    transactionExtraFields.clear();

    if (transactionExtra.empty())
      return true;

    try
    {
      MemoryInputStream iss(transactionExtra.data(), transactionExtra.size());
      BinaryInputStreamSerializer ar(iss);

      int c = 0;

      while (!iss.endOfStream())
      {
        c = read<uint8_t>(iss);
        switch (c)
        {
        case TX_EXTRA_TAG_PADDING:
        {
          size_t size = 1;
          for (; !iss.endOfStream() && size <= TX_EXTRA_PADDING_MAX_COUNT; ++size)
          {
            if (read<uint8_t>(iss) != 0)
            {
              return false; // all bytes should be zero
            }
          }

          if (size > TX_EXTRA_PADDING_MAX_COUNT)
          {
            return false;
          }

          transactionExtraFields.push_back(TransactionExtraPadding{size});
          break;
        }

        case TX_EXTRA_TAG_PUBKEY:
        {
          TransactionExtraPublicKey extraPk;
          ar(extraPk.publicKey, "public_key");
          transactionExtraFields.push_back(extraPk);
          break;
        }

        case TX_EXTRA_NONCE:
        {
          TransactionExtraNonce extraNonce;
          uint8_t size = read<uint8_t>(iss);
          if (size > 0)
          {
            extraNonce.nonce.resize(size);
            read(iss, extraNonce.nonce.data(), extraNonce.nonce.size());
          }

          transactionExtraFields.push_back(extraNonce);
          break;
        }

        case TX_EXTRA_MERGE_MINING_TAG:
        {
          TransactionExtraMergeMiningTag mmTag;
          ar(mmTag, "mm_tag");
          transactionExtraFields.push_back(mmTag);
          break;
        }

        case TX_EXTRA_MESSAGE_TAG:
        {
          tx_extra_message message;
          ar(message.data, "message");
          transactionExtraFields.push_back(message);
          break;
        }

        case TX_EXTRA_TTL:
        {
          uint8_t size;
          readVarint(iss, size);
          TransactionExtraTTL ttl;
          readVarint(iss, ttl.ttl);
          transactionExtraFields.push_back(ttl);
          break;
        }

        case TX_EXTRA_HEAT_COMMITMENT:
        {
          // Read directly from stream to keep iss position correct.
          // Format: [commitment: 32] [amount: 8 LE] [meta_len: 1] [meta: N]
          TransactionExtraHeatCommitment heatCommitment;
          read(iss, heatCommitment.commitment.data, sizeof(heatCommitment.commitment.data));
          heatCommitment.amount = 0;
          for (int i = 0; i < 8; ++i) {
            heatCommitment.amount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
          }
          uint8_t heatMetaSize = read<uint8_t>(iss);
          if (heatMetaSize > 0) {
            heatCommitment.metadata.resize(heatMetaSize);
            read(iss, heatCommitment.metadata.data(), heatMetaSize);
          }
          transactionExtraFields.push_back(heatCommitment);
          break;
        }

        case TX_EXTRA_YIELD_COMMITMENT:
        {
          // Format: [commitment: 32] [amount: 8 LE] [term: 4 LE] [chain: 1]
          //         [cia_len: 1] [cia: N] [meta_len: 1] [meta: M] [gift_len: 1] [gift: P]
          TransactionExtraYieldCommitment yieldCommitment;
          read(iss, yieldCommitment.commitment.data, sizeof(yieldCommitment.commitment.data));
          yieldCommitment.amount = 0;
          for (int i = 0; i < 8; ++i) {
            yieldCommitment.amount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
          }
          yieldCommitment.term = 0;
          for (int i = 0; i < 4; ++i) {
            yieldCommitment.term |= static_cast<uint32_t>(read<uint8_t>(iss)) << (i * 8);
          }
          yieldCommitment.claimChainCode = read<uint8_t>(iss);
          uint8_t ciaLen = read<uint8_t>(iss);
          if (ciaLen > 0) {
            std::vector<char> ciaBuf(ciaLen);
            read(iss, ciaBuf.data(), ciaLen);
            yieldCommitment.CIAId.assign(ciaBuf.begin(), ciaBuf.end());
          }
          uint8_t yieldMetaSize = read<uint8_t>(iss);
          if (yieldMetaSize > 0) {
            yieldCommitment.metadata.resize(yieldMetaSize);
            read(iss, yieldCommitment.metadata.data(), yieldMetaSize);
          }
          uint8_t yieldGiftSize = read<uint8_t>(iss);
          if (yieldGiftSize > 0) {
            yieldCommitment.gift_secret.resize(yieldGiftSize);
            read(iss, yieldCommitment.gift_secret.data(), yieldGiftSize);
          }
          transactionExtraFields.push_back(yieldCommitment);
          break;
        }

        case TX_EXTRA_COLD_COMMITMENT:
        {
          // Format: [commitment: 32] [amount: 8 LE] [term: 4 LE] [chain: 1]
          //         [meta_len: 1] [meta: N] [gift_len: 1] [gift: M]
          TransactionExtraColdCommitment coldCommitment;
          read(iss, coldCommitment.commitment.data, sizeof(coldCommitment.commitment.data));
          coldCommitment.amount = 0;
          for (int i = 0; i < 8; ++i) {
            coldCommitment.amount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
          }
          coldCommitment.term = 0;
          for (int i = 0; i < 4; ++i) {
            coldCommitment.term |= static_cast<uint32_t>(read<uint8_t>(iss)) << (i * 8);
          }
          coldCommitment.claimChainCode = read<uint8_t>(iss);
          uint8_t coldMetaSize = read<uint8_t>(iss);
          if (coldMetaSize > 0) {
            coldCommitment.metadata.resize(coldMetaSize);
            read(iss, coldCommitment.metadata.data(), coldMetaSize);
          }
          uint8_t coldGiftSize = read<uint8_t>(iss);
          if (coldGiftSize > 0) {
            coldCommitment.gift_secret.resize(coldGiftSize);
            read(iss, coldCommitment.gift_secret.data(), coldGiftSize);
          }
          transactionExtraFields.push_back(coldCommitment);
          break;
        }

        case TX_EXTRA_COLD_MIGRATION:
        {
          // Format: [originalTxHash: 32] [commitment: 32] [amount: 8 LE] [term: 4 LE] [chain: 1]
          TransactionExtraColdMigration migration;
          read(iss, migration.originalTxHash.data, sizeof(migration.originalTxHash.data));
          read(iss, migration.commitment.data, sizeof(migration.commitment.data));
          migration.amount = 0;
          for (int i = 0; i < 8; ++i) {
            migration.amount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
          }
          migration.term = 0;
          for (int i = 0; i < 4; ++i) {
            migration.term |= static_cast<uint32_t>(read<uint8_t>(iss)) << (i * 8);
          }
          migration.claimChainCode = read<uint8_t>(iss);
          transactionExtraFields.push_back(migration);
          break;
        }

        case TX_EXTRA_BURN_RECEIPT:
        {
          TransactionExtraBurnReceipt burnReceipt;
          if (getBurnReceiptFromExtra(transactionExtra, burnReceipt)) {
            transactionExtraFields.push_back(burnReceipt);
          } else {
            return false;
          }
          break;
        }

        case TX_EXTRA_COLD_RECEIPT:
        {
          TransactionExtraDepositReceipt depositReceipt;
          if (getDepositReceiptFromExtra(transactionExtra, depositReceipt)) {
            transactionExtraFields.push_back(depositReceipt);
          } else {
            return false;
          }
          break;
        }

        case TX_EXTRA_DEPOSIT_SECRET:
        {
          // 0xD5: encrypted deposit secret. Read directly via getDepositSecretFromExtra();
          // parser skips bytes here so unknown-tag early exit doesn't occur.
          uint8_t dsLen = read<uint8_t>(iss);
          if (dsLen > 0) {
            for (uint8_t i = 0; i < dsLen; ++i)
              read<uint8_t>(iss);
          }
          break;
        }

        case TX_EXTRA_ALIAS:
        {
          // 0xEA: @ alias registration. Format: varint(size) + BinaryArray(struct).
          uint64_t aliasSize = 0;
          readVarint(iss, aliasSize);
          BinaryArray aliasBa(aliasSize);
          if (aliasSize > 0) {
            read(iss, aliasBa.data(), aliasSize);
          }
          TransactionExtraAliasRegistration aliasReg;
          if (!fromBinaryArray(aliasReg, aliasBa)) {
            return false;
          }
          transactionExtraFields.push_back(aliasReg);
          break;
        }

        default:
        {
          // Forward-compatible TLV skip for unknown tags (prevents stream desync)
          uint64_t unknownSize = 0;
          readVarint(iss, unknownSize);
          for (uint64_t i = 0; i < unknownSize; ++i) {
            read<uint8_t>(iss);
          }
          break;
        }
      }
    }
    }
    catch (std::exception &)
    {
      return false;
    }

    return true;
  }

  struct ExtraSerializerVisitor : public boost::static_visitor<bool>
  {
    std::vector<uint8_t> &extra;

    ExtraSerializerVisitor(std::vector<uint8_t> &tx_extra)
        : extra(tx_extra) {}

    bool operator()(const TransactionExtraPadding &t)
    {
      if (t.size > TX_EXTRA_PADDING_MAX_COUNT)
      {
        return false;
      }
      extra.insert(extra.end(), t.size, 0);
      return true;
    }

    bool operator()(const TransactionExtraPublicKey &t)
    {
      return addTransactionPublicKeyToExtra(extra, t.publicKey);
    }

    bool operator()(const TransactionExtraNonce &t)
    {
      return addExtraNonceToTransactionExtra(extra, t.nonce);
    }

    bool operator()(const TransactionExtraMergeMiningTag &t)
    {
      return appendMergeMiningTagToExtra(extra, t);
    }

    bool operator()(const tx_extra_message &t)
    {
      return append_message_to_extra(extra, t);
    }

    bool operator()(const TransactionExtraTTL &t)
    {
      appendTTLToExtra(extra, t.ttl);
      return true;
    }

    bool operator()(const TransactionExtraHeatCommitment &t)
    {
      return addHeatCommitmentToExtra(extra, t);
    }

    bool operator()(const TransactionExtraYieldCommitment &t)
    {
      return addYieldCommitmentToExtra(extra, t);
    }

    bool operator()(const TransactionExtraCDDepositSecret &t)
    {
      return addCDDepositSecretToExtra(extra, t);
    }

    bool operator()(const TransactionExtraBurnReceipt &t)
    {
      return addBurnReceiptToExtra(extra, t);
    }

    bool operator()(const TransactionExtraDepositReceipt &t)
    {
      return addDepositReceiptToExtra(extra, t);
    }

    bool operator()(const TransactionExtraAliasRegistration &t)
    {
      return addAliasToExtra(extra, t);
    }

    bool operator()(const TransactionExtraColdMigration &t)
    {
      return addColdMigrationToExtra(extra, t);
    }

  };

  bool writeTransactionExtra(std::vector<uint8_t> &tx_extra, const std::vector<TransactionExtraField> &tx_extra_fields)
  {
    ExtraSerializerVisitor visitor(tx_extra);

    for (const auto &tag : tx_extra_fields)
    {
      if (!boost::apply_visitor(visitor, tag))
      {
        return false;
      }
    }

    return true;
  }

  PublicKey getTransactionPublicKeyFromExtra(const std::vector<uint8_t> &tx_extra)
  {
    std::vector<TransactionExtraField> tx_extra_fields;
    parseTransactionExtra(tx_extra, tx_extra_fields);

    TransactionExtraPublicKey pub_key_field;
    if (!findTransactionExtraFieldByType(tx_extra_fields, pub_key_field))
      return boost::value_initialized<PublicKey>();

    return pub_key_field.publicKey;
  }

  bool addTransactionPublicKeyToExtra(std::vector<uint8_t> &tx_extra, const PublicKey &tx_pub_key)
  {
    tx_extra.resize(tx_extra.size() + 1 + sizeof(PublicKey));
    tx_extra[tx_extra.size() - 1 - sizeof(PublicKey)] = TX_EXTRA_TAG_PUBKEY;
    *reinterpret_cast<PublicKey *>(&tx_extra[tx_extra.size() - sizeof(PublicKey)]) = tx_pub_key;
    return true;
  }

  bool addExtraNonceToTransactionExtra(std::vector<uint8_t> &tx_extra, const BinaryArray &extra_nonce)
  {
    if (extra_nonce.size() > TX_EXTRA_NONCE_MAX_COUNT)
    {
      return false;
    }

    size_t start_pos = tx_extra.size();
    tx_extra.resize(tx_extra.size() + 2 + extra_nonce.size());
    //write tag
    tx_extra[start_pos] = TX_EXTRA_NONCE;
    //write len
    ++start_pos;
    tx_extra[start_pos] = static_cast<uint8_t>(extra_nonce.size());
    //write data
    ++start_pos;
    memcpy(&tx_extra[start_pos], extra_nonce.data(), extra_nonce.size());
    return true;
  }

  bool appendMergeMiningTagToExtra(std::vector<uint8_t> &tx_extra, const TransactionExtraMergeMiningTag &mm_tag)
  {
    BinaryArray blob;
    if (!toBinaryArray(mm_tag, blob))
    {
      return false;
    }

    tx_extra.push_back(TX_EXTRA_MERGE_MINING_TAG);
    std::copy(reinterpret_cast<const uint8_t *>(blob.data()), reinterpret_cast<const uint8_t *>(blob.data() + blob.size()), std::back_inserter(tx_extra));
    return true;
  }

  bool getMergeMiningTagFromExtra(const std::vector<uint8_t> &tx_extra, TransactionExtraMergeMiningTag &mm_tag)
  {
    std::vector<TransactionExtraField> tx_extra_fields;
    parseTransactionExtra(tx_extra, tx_extra_fields);

    return findTransactionExtraFieldByType(tx_extra_fields, mm_tag);
  }

  bool append_message_to_extra(std::vector<uint8_t> &tx_extra, const tx_extra_message &message)
  {
    BinaryArray blob;
    if (!toBinaryArray(message, blob))
    {
      return false;
    }

    tx_extra.reserve(tx_extra.size() + 1 + blob.size());
    tx_extra.push_back(TX_EXTRA_MESSAGE_TAG);
    std::copy(reinterpret_cast<const uint8_t *>(blob.data()), reinterpret_cast<const uint8_t *>(blob.data() + blob.size()), std::back_inserter(tx_extra));

    return true;
  }

  std::vector<std::string> get_messages_from_extra(const std::vector<uint8_t> &extra, const Crypto::PublicKey &txkey, const Crypto::SecretKey *recepient_secret_key)
  {
    std::vector<TransactionExtraField> tx_extra_fields;
    std::vector<std::string> result;
    if (!parseTransactionExtra(extra, tx_extra_fields))
    {
      return result;
    }
    size_t i = 0;
    for (const auto &f : tx_extra_fields)
    {
      if (f.type() != typeid(tx_extra_message))
      {
        continue;
      }
      std::string res;
      if (boost::get<tx_extra_message>(f).decrypt(i, txkey, recepient_secret_key, res))
      {
        result.push_back(res);
      }
      ++i;
    }
    return result;
  }

  void appendTTLToExtra(std::vector<uint8_t> &tx_extra, uint64_t ttl)
  {
    std::string ttlData = Tools::get_varint_data(ttl);
    std::string extraFieldSize = Tools::get_varint_data(ttlData.size());

    tx_extra.reserve(tx_extra.size() + 1 + extraFieldSize.size() + ttlData.size());
    tx_extra.push_back(TX_EXTRA_TTL);
    std::copy(extraFieldSize.begin(), extraFieldSize.end(), std::back_inserter(tx_extra));
    std::copy(ttlData.begin(), ttlData.end(), std::back_inserter(tx_extra));
  }

  void setPaymentIdToTransactionExtraNonce(std::vector<uint8_t> &extra_nonce, const Hash &payment_id)
  {
    extra_nonce.clear();
    extra_nonce.push_back(TX_EXTRA_NONCE_PAYMENT_ID);
    const uint8_t *payment_id_ptr = reinterpret_cast<const uint8_t *>(&payment_id);
    std::copy(payment_id_ptr, payment_id_ptr + sizeof(payment_id), std::back_inserter(extra_nonce));
  }

  bool getPaymentIdFromTransactionExtraNonce(const std::vector<uint8_t> &extra_nonce, Hash &payment_id)
  {
    if (sizeof(Hash) + 1 != extra_nonce.size())
      return false;
    if (TX_EXTRA_NONCE_PAYMENT_ID != extra_nonce[0])
      return false;
    payment_id = *reinterpret_cast<const Hash *>(extra_nonce.data() + 1);
    return true;
  }

  bool parsePaymentId(const std::string &paymentIdString, Hash &paymentId)
  {
    return Common::podFromHex(paymentIdString, paymentId);
  }

  bool createTxExtraWithPaymentId(const std::string &paymentIdString, std::vector<uint8_t> &extra)
  {
    Hash paymentIdBin;

    if (!parsePaymentId(paymentIdString, paymentIdBin))
    {
      return false;
    }

    std::vector<uint8_t> extraNonce;
    CryptoNote::setPaymentIdToTransactionExtraNonce(extraNonce, paymentIdBin);

    if (!CryptoNote::addExtraNonceToTransactionExtra(extra, extraNonce))
    {
      return false;
    }

    return true;
  }

  bool getPaymentIdFromTxExtra(const std::vector<uint8_t> &extra, Hash &paymentId)
  {
    std::vector<TransactionExtraField> tx_extra_fields;
    if (!parseTransactionExtra(extra, tx_extra_fields))
    {
      return false;
    }

    TransactionExtraNonce extra_nonce;
    if (findTransactionExtraFieldByType(tx_extra_fields, extra_nonce))
    {
      if (!getPaymentIdFromTransactionExtraNonce(extra_nonce.nonce, paymentId))
      {
        return false;
      }
    }
    else
    {
      return false;
    }

    return true;
  }

#define TX_EXTRA_MESSAGE_CHECKSUM_SIZE 4

#pragma pack(push, 1)
  struct message_key_data
  {
    KeyDerivation derivation;
    uint8_t magic1, magic2;
  };
#pragma pack(pop)
  static_assert(sizeof(message_key_data) == 34, "Invalid structure size");

  bool tx_extra_message::encrypt(size_t index, const std::string &message, const AccountPublicAddress *recipient, const KeyPair &txkey)
  {
    size_t mlen = message.size();
    std::unique_ptr<char[]> buf(new char[mlen + TX_EXTRA_MESSAGE_CHECKSUM_SIZE]);
    memcpy(buf.get(), message.data(), mlen);
    memset(buf.get() + mlen, 0, TX_EXTRA_MESSAGE_CHECKSUM_SIZE);
    mlen += TX_EXTRA_MESSAGE_CHECKSUM_SIZE;
    if (recipient)
    {
      message_key_data key_data;
      if (!generate_key_derivation(recipient->spendPublicKey, txkey.secretKey, key_data.derivation))
      {
        return false;
      }
      key_data.magic1 = 0x80;
      key_data.magic2 = 0;
      Hash h = cn_fast_hash(&key_data, sizeof(message_key_data));
      uint64_t nonce = SWAP64LE(index);
      chacha8(buf.get(), mlen, reinterpret_cast<uint8_t *>(&h), reinterpret_cast<uint8_t *>(&nonce), buf.get());
    }
    data.assign(buf.get(), mlen);
    return true;
  }

  bool tx_extra_message::decrypt(size_t index, const Crypto::PublicKey &txkey, const Crypto::SecretKey *recepient_secret_key, std::string &message) const
  {
    size_t mlen = data.size();
    if (mlen < TX_EXTRA_MESSAGE_CHECKSUM_SIZE)
    {
      return false;
    }
    const char *buf;
    std::unique_ptr<char[]> ptr;
    if (recepient_secret_key != nullptr)
    {
      ptr.reset(new char[mlen]);
      assert(ptr);
      message_key_data key_data;
      if (!generate_key_derivation(txkey, *recepient_secret_key, key_data.derivation))
      {
        return false;
      }
      key_data.magic1 = 0x80;
      key_data.magic2 = 0;
      Hash h = cn_fast_hash(&key_data, sizeof(message_key_data));
      uint64_t nonce = SWAP64LE(index);
      chacha8(data.data(), mlen, reinterpret_cast<uint8_t *>(&h), reinterpret_cast<uint8_t *>(&nonce), ptr.get());
      buf = ptr.get();
    }
    else
    {
      buf = data.data();
    }
    mlen -= TX_EXTRA_MESSAGE_CHECKSUM_SIZE;
    for (size_t i = 0; i < TX_EXTRA_MESSAGE_CHECKSUM_SIZE; i++)
    {
      if (buf[mlen + i] != 0)
      {
        return false;
      }
    }
    message.assign(buf, mlen);
    return true;
  }

  bool tx_extra_message::serialize(ISerializer &s)
  {
    s(data, "data");
    return true;
  }

  // HEAT commitment serialization
  bool TransactionExtraHeatCommitment::serialize(ISerializer &s)
  {
    s(commitment, "commitment");   // 🔒 SECURE: Only commitment hash
    s(amount, "amount");
    s(metadata, "metadata");
    return true;
  }

  // Yield commitment serialization
  bool TransactionExtraYieldCommitment::serialize(ISerializer &s)
  {
    s(commitment, "commitment");
    s(amount, "amount");
    s(term, "term");
    s(claimChainCode, "claimChainCode");
    s(CIAId, "CIAId");
    s(metadata, "metadata");
    s(gift_secret, "gift_secret");
    return true;
  }


  // HEAT commitment helper functions
  bool addHeatCommitmentToExtra(std::vector<uint8_t> &tx_extra, const TransactionExtraHeatCommitment &commitment)
  {
    tx_extra.push_back(TX_EXTRA_HEAT_COMMITMENT);

    // Serialize commitment hash (32 bytes)
    tx_extra.insert(tx_extra.end(), commitment.commitment.data, commitment.commitment.data + sizeof(commitment.commitment.data));

    // Serialize amount (8 bytes, little-endian)
    uint64_t amount = commitment.amount;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF));
      amount >>= 8;
    }

    // Serialize metadata size and data
    uint8_t metadataSize = static_cast<uint8_t>(commitment.metadata.size());
    tx_extra.push_back(metadataSize);

    if (metadataSize > 0) {
      tx_extra.insert(tx_extra.end(), commitment.metadata.begin(), commitment.metadata.end());
    }

    return true;
  }

  bool createTxExtraWithHeatCommitment(const Crypto::Hash &commitment, uint64_t amount, const std::vector<uint8_t> &metadata, std::vector<uint8_t> &extra)
  {
    TransactionExtraHeatCommitment heatCommitment;
    heatCommitment.commitment = commitment;       //  Only commitment hash
    heatCommitment.amount = amount;
    heatCommitment.metadata = metadata;

    return addHeatCommitmentToExtra(extra, heatCommitment);
  }

  bool getHeatCommitmentFromExtra(const std::vector<uint8_t> &tx_extra, TransactionExtraHeatCommitment &commitment)
  {
    // Find the 0x08 tag in tx_extra
    size_t pos = 0;
    bool found = false;

    while (pos < tx_extra.size()) {
      if (tx_extra[pos] == TX_EXTRA_HEAT_COMMITMENT) {
        found = true;
        pos++; // Skip tag
        break;
      }
      pos++;
    }

    if (!found) return false;

    // Deserialize commitment hash (32 bytes)
    if (pos + 32 > tx_extra.size()) return false;
    std::memcpy(commitment.commitment.data, &tx_extra[pos], 32);
    pos += 32;

    // Deserialize amount (8 bytes, little-endian)
    if (pos + 8 > tx_extra.size()) return false;
    commitment.amount = 0;
    for (int i = 0; i < 8; ++i) {
      commitment.amount |= static_cast<uint64_t>(tx_extra[pos + i]) << (i * 8);
    }
    pos += 8;

    // Deserialize metadata size and data
    if (pos >= tx_extra.size()) return false;
    uint8_t metadataSize = tx_extra[pos];
    pos += 1;

    if (metadataSize > 0) {
      if (pos + metadataSize > tx_extra.size()) return false;
      commitment.metadata.assign(tx_extra.begin() + pos, tx_extra.begin() + pos + metadataSize);
    } else {
      commitment.metadata.clear();
    }

    return true;
  }

  // Yield commitment helper functions
  bool addYieldCommitmentToExtra(std::vector<uint8_t> &tx_extra, const TransactionExtraYieldCommitment &commitment)
  {
    tx_extra.push_back(TX_EXTRA_YIELD_COMMITMENT);

    // Serialize commitment hash (32 bytes)
    tx_extra.insert(tx_extra.end(), commitment.commitment.data, commitment.commitment.data + sizeof(commitment.commitment.data));

    // Serialize amount (8 bytes, little-endian)
    uint64_t amount = commitment.amount;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF));
      amount >>= 8;
    }

    // Serialize term (4 bytes, little-endian)
    uint32_t term = commitment.term;
    for (int i = 0; i < 4; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(term & 0xFF));
      term >>= 8;
    }

    // Serialize claimChainCode (1 byte)
    tx_extra.push_back(commitment.claimChainCode);

    // Serialize CIAId length and string
    uint8_t assetIdLen = static_cast<uint8_t>(commitment.CIAId.size());
    tx_extra.push_back(assetIdLen);
    tx_extra.insert(tx_extra.end(), commitment.CIAId.begin(), commitment.CIAId.end());

    // Serialize metadata size and data
    uint8_t metadataSize = static_cast<uint8_t>(commitment.metadata.size());
    tx_extra.push_back(metadataSize);

    if (metadataSize > 0) {
      tx_extra.insert(tx_extra.end(), commitment.metadata.begin(), commitment.metadata.end());
    }

    // Serialize gift_secret size and data
    uint8_t giftSecretSize = static_cast<uint8_t>(commitment.gift_secret.size());
    tx_extra.push_back(giftSecretSize);

    if (giftSecretSize > 0) {
      tx_extra.insert(tx_extra.end(), commitment.gift_secret.begin(), commitment.gift_secret.end());
    }

    return true;
}

  bool createTxExtraWithYieldCommitment(const Crypto::Hash &commitment, uint64_t amount, uint32_t term, const std::string &CIAId, const std::vector<uint8_t> &metadata, uint8_t claimChainCode, const std::vector<uint8_t> &gift_secret, std::vector<uint8_t> &extra)
  {
    TransactionExtraYieldCommitment yieldCommitment;
    yieldCommitment.commitment = commitment;
    yieldCommitment.amount = amount;
    yieldCommitment.term = term;
    yieldCommitment.CIAId = CIAId;
    yieldCommitment.metadata = metadata;
    yieldCommitment.claimChainCode = claimChainCode;
    yieldCommitment.gift_secret = gift_secret;

    return addYieldCommitmentToExtra(extra, yieldCommitment);
  }

  bool getYieldCommitmentFromExtra(const std::vector<uint8_t> &tx_extra, TransactionExtraYieldCommitment &commitment)
  {
    if (tx_extra.empty() || tx_extra[0] != TX_EXTRA_YIELD_COMMITMENT) {
      return false;
    }

    size_t pos = 1;

    // Deserialize commitment hash (32 bytes)
    if (pos + 32 > tx_extra.size()) return false;
    std::memcpy(commitment.commitment.data, &tx_extra[pos], 32);
    pos += 32;

    // Deserialize amount (8 bytes, little-endian)
    if (pos + 8 > tx_extra.size()) return false;
    pos += 8;

    // Deserialize term (4 bytes, little-endian)
    if (pos + 4 > tx_extra.size()) return false;
    commitment.term = 0;
    for (int i = 0; i < 4; ++i) {
      commitment.term |= static_cast<uint32_t>(tx_extra[pos + i]) << (i * 8);
    }
    pos += 4;

    // Deserialize claimChainCode (1 byte)
    if (pos >= tx_extra.size()) return false;
    commitment.claimChainCode = tx_extra[pos];
    pos += 1;

    // Deserialize CIAId length and string
    if (pos >= tx_extra.size()) return false;
    uint8_t assetIdLen = tx_extra[pos];
    pos += 1;

    if (pos + assetIdLen > tx_extra.size()) return false;
    if (assetIdLen > 0) {
      commitment.CIAId.assign(reinterpret_cast<const char*>(&tx_extra[pos]), assetIdLen);
      pos += assetIdLen;
    } else {
      commitment.CIAId.clear();
    }

    // Deserialize metadata size and data
    if (pos >= tx_extra.size()) return false;
    uint8_t metadataSize = tx_extra[pos];
    pos += 1;

    if (pos + metadataSize > tx_extra.size()) return false;
    if (metadataSize > 0) {
      commitment.metadata.assign(&tx_extra[pos], &tx_extra[pos] + metadataSize);
      pos += metadataSize;
    } else {
      commitment.metadata.clear();
    }

    // Deserialize gift_secret size and data
    if (pos >= tx_extra.size()) return false;
    uint8_t giftSecretSize = tx_extra[pos];
    pos += 1;

    if (pos + giftSecretSize > tx_extra.size()) return false;
    if (giftSecretSize > 0) {
      commitment.gift_secret.assign(&tx_extra[pos], &tx_extra[pos] + giftSecretSize);
    } else {
      commitment.gift_secret.clear();
    }

    return true;
  }

  // ---------------- UNIFIED COMMITMENT FORMAT ----------------
  // PRIVACY MODEL: No recipient in commitment.
  // Contract mints to msg.sender, nullifier prevents replay.
  // Whoever has the secret owns the deposit.
  //
  // UNIFIED PREIMAGE (88 bytes):
  //   keccak256(secret || le64(amount) || tx_prefix_hash || network_id || target_chain_id || version || le32(term))
  //
  // HEAT burns use: term = DEPOSIT_TERM_FOREVER (0xFFFFFFFF)
  // COLD deposits use: actual term in blocks
  //
  // This unified format allows both HEAT and COLD to use the same verification logic,
  // differing only in the term value.

  Crypto::Hash computeCommitment(const std::array<uint8_t, 32> &secret,
                                 uint64_t amount_atomic,
                                 const Crypto::Hash &tx_prefix_hash,
                                 uint32_t network_id,
                                 uint32_t target_chain_id,
                                 uint32_t commitment_version,
                                 uint32_t term)
  {
    std::vector<uint8_t> preimage;
    // 32 (secret) + 8 (amount) + 32 (tx_hash) + 4 (network) + 4 (chain) + 4 (version) + 4 (term) = 88 bytes
    preimage.reserve(88);

    // Secret (32 bytes)
    preimage.insert(preimage.end(), secret.begin(), secret.end());

    // Amount (8 bytes, LE)
    uint64_t amt = amount_atomic;
    for (int i = 0; i < 8; ++i) {
      preimage.push_back(static_cast<uint8_t>(amt & 0xFF));
      amt >>= 8;
    }

    // Tx prefix hash (32 bytes)
    preimage.insert(preimage.end(), reinterpret_cast<const uint8_t*>(&tx_prefix_hash), reinterpret_cast<const uint8_t*>(&tx_prefix_hash) + sizeof(tx_prefix_hash));

    // NO recipient_hash - privacy preserving! Recipient binding at STARK proof time.

    // Network ID (4 bytes, LE)
    uint32_t net_id = network_id;
    for (int i = 0; i < 4; ++i) {
      preimage.push_back(static_cast<uint8_t>(net_id & 0xFF));
      net_id >>= 8;
    }

    // Target chain ID (4 bytes, LE)
    uint32_t target_id = target_chain_id;
    for (int i = 0; i < 4; ++i) {
      preimage.push_back(static_cast<uint8_t>(target_id & 0xFF));
      target_id >>= 8;
    }

    // Commitment version (4 bytes, LE)
    uint32_t version = commitment_version;
    for (int i = 0; i < 4; ++i) {
      preimage.push_back(static_cast<uint8_t>(version & 0xFF));
      version >>= 8;
    }

    // Term (4 bytes, LE) - UNIFIED for HEAT and COLD
    // HEAT: 0xFFFFFFFF (DEPOSIT_TERM_FOREVER)
    // COLD: actual term in blocks
    uint32_t t = term;
    for (int i = 0; i < 4; ++i) {
      preimage.push_back(static_cast<uint8_t>(t & 0xFF));
      t >>= 8;
    }

    uint8_t md[32];
    keccak(preimage.data(), static_cast<int>(preimage.size()), md, sizeof(md));
    Crypto::Hash out{};
    memcpy(&out, md, sizeof(out));
    return out;
  }

  // HEAT convenience wrapper - uses DEPOSIT_TERM_FOREVER for term
  Crypto::Hash computeHeatCommitment(const std::array<uint8_t, 32> &secret,
                                     uint64_t amount_atomic,
                                     const Crypto::Hash &tx_prefix_hash,
                                     uint32_t network_id,
                                     uint32_t target_chain_id,
                                     uint32_t commitment_version)
  {
    // Use DEPOSIT_TERM_FOREVER (0xFFFFFFFF) for HEAT burns
    return computeCommitment(secret, amount_atomic, tx_prefix_hash, network_id, target_chain_id, commitment_version, parameters::DEPOSIT_TERM_FOREVER);
  }

  // Builds tx.extra with TX_EXTRA_HEAT_COMMITMENT (0x08)
  // PRIVACY MODEL: No ETH address - recipient binding at STARK proof time
  bool buildHeatExtra(const std::array<uint8_t, 32> &secret,
                      uint64_t amount_atomic,
                      const Crypto::Hash &tx_prefix_hash,
                      uint32_t network_id,
                      uint32_t target_chain_id,
                      uint32_t commitment_version,
                      const std::vector<uint8_t> &metadata,
                      std::vector<uint8_t> &extra)
  {
    // Compute commitment (no recipient - privacy preserving)
    Crypto::Hash commitment = computeHeatCommitment(secret, amount_atomic, tx_prefix_hash, network_id, target_chain_id, commitment_version);

    // If commitment is zero (failed), bail
    const Crypto::Hash zero = {};
    if (!memcmp(&commitment, &zero, sizeof(zero))) {
      return false;
    }

    return CryptoNote::createTxExtraWithHeatCommitment(commitment, amount_atomic, metadata, extra);
  }

  // COLD convenience wrapper - same as computeCommitment, named for clarity
  Crypto::Hash computeColdCommitment(const std::array<uint8_t, 32> &secret,
                                     uint64_t amount_atomic,
                                     const Crypto::Hash &tx_prefix_hash,
                                     uint32_t network_id,
                                     uint32_t target_chain_id,
                                     uint32_t commitment_version,
                                     uint32_t term)
  {
    // Use the unified commitment format
    return computeCommitment(secret, amount_atomic, tx_prefix_hash, network_id, target_chain_id, commitment_version, term);
  }

  // Builds tx.extra with TX_EXTRA_COLD_COMMITMENT (0xCD)
  // PRIVACY MODEL: No ETH address parameter - recipient binding at STARK proof time
  bool buildColdExtra(const std::array<uint8_t, 32> &secret,
                      uint64_t amount_atomic,
                      const Crypto::Hash &tx_prefix_hash,
                      uint32_t network_id,
                      uint32_t target_chain_id,
                      uint32_t commitment_version,
                      uint32_t term,
                      uint8_t claimChainCode,
                      const std::vector<uint8_t> &metadata,
                      const std::vector<uint8_t> &gift_secret,
                      std::vector<uint8_t> &extra)
  {
    // Compute commitment (no recipient - privacy preserving)
    Crypto::Hash commitment = computeColdCommitment(secret, amount_atomic, tx_prefix_hash, network_id, target_chain_id, commitment_version, term);

    // If commitment is zero (failed), bail
    const Crypto::Hash zero = {};
    if (!memcmp(&commitment, &zero, sizeof(zero))) {
      return false;
    }

    return CryptoNote::createTxExtraWithColdCommitment(commitment, amount_atomic, term, claimChainCode, metadata, gift_secret, extra);
  }

  // COLD Commitment helper functions - unified format matching HEAT style
  bool addColdCommitmentToExtra(std::vector<uint8_t> &tx_extra, const CryptoNote::TransactionExtraColdCommitment &commitment)
  {
    tx_extra.push_back(TX_EXTRA_COLD_COMMITMENT);

    // Commitment hash (32 bytes) - real keccak256 hash, not dummy zeros
    tx_extra.insert(tx_extra.end(), commitment.commitment.data, commitment.commitment.data + 32);

    // Amount (8 bytes, little-endian)
    uint64_t amount = commitment.amount;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF));
      amount >>= 8;
    }

    // Term (4 bytes, little-endian) - in blocks
    uint32_t term = commitment.term;
    for (int i = 0; i < 4; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(term & 0xFF));
      term >>= 8;
    }

    // Chain code (1 byte)
    tx_extra.push_back(commitment.claimChainCode);

    // Metadata size and data
    uint8_t metadataSize = static_cast<uint8_t>(commitment.metadata.size());
    tx_extra.push_back(metadataSize);
    if (metadataSize > 0) {
      tx_extra.insert(tx_extra.end(), commitment.metadata.begin(), commitment.metadata.end());
    }

    // Gift secret size and data (0 if not gifting)
    uint8_t giftSecretSize = static_cast<uint8_t>(commitment.gift_secret.size());
    tx_extra.push_back(giftSecretSize);
    if (giftSecretSize > 0) {
      tx_extra.insert(tx_extra.end(), commitment.gift_secret.begin(), commitment.gift_secret.end());
    }

    return true;
  }

  bool addColdMigrationToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraColdMigration& migration) {
    tx_extra.push_back(TX_EXTRA_COLD_MIGRATION);

    // Original tx hash (32 bytes)
    tx_extra.insert(tx_extra.end(), migration.originalTxHash.data, migration.originalTxHash.data + 32);

    // V3 commitment hash (32 bytes)
    tx_extra.insert(tx_extra.end(), migration.commitment.data, migration.commitment.data + 32);

    // Amount (8 bytes LE)
    uint64_t amount = migration.amount;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF));
      amount >>= 8;
    }

    // Term (4 bytes LE)
    uint32_t term = migration.term;
    for (int i = 0; i < 4; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(term & 0xFF));
      term >>= 8;
    }

    // Chain code (1 byte)
    tx_extra.push_back(migration.claimChainCode);

    return true;
  }

  bool createTxExtraWithColdCommitment(const Crypto::Hash &commitment, uint64_t amount, uint32_t term,
                                        uint8_t claimChainCode, const std::vector<uint8_t> &metadata,
                                        const std::vector<uint8_t> &gift_secret, std::vector<uint8_t> &extra)
  {
    TransactionExtraColdCommitment coldCommitment;
    coldCommitment.commitment = commitment;
    coldCommitment.amount = amount;
    coldCommitment.term = term;
    coldCommitment.claimChainCode = claimChainCode;
    coldCommitment.metadata = metadata;
    coldCommitment.gift_secret = gift_secret;

    return addColdCommitmentToExtra(extra, coldCommitment);
  }

  bool getColdCommitmentFromExtra(const std::vector<uint8_t> &tx_extra, TransactionExtraColdCommitment &commitment)
  {
    // Find the 0xCD tag in tx_extra
    size_t pos = 0;
    bool found = false;

    while (pos < tx_extra.size()) {
      if (tx_extra[pos] == TX_EXTRA_COLD_COMMITMENT) {
        found = true;
        pos++; // Skip tag
        break;
      }
      // Skip other tags (simplified - just look for 0xCD)
      pos++;
    }

    if (!found || pos >= tx_extra.size()) {
      return false;
    }

    // Parse COLD commitment data in new format:
    // [commitment: 32 bytes]
    // [amount: 8 bytes LE]
    // [term: 4 bytes LE]
    // [chain_code: 1 byte]
    // [metadata_len: 1 byte]
    // [metadata: variable]
    // [gift_secret_len: 1 byte]
    // [gift_secret: variable]

    // Commitment hash (32 bytes)
    if (pos + 32 > tx_extra.size()) return false;
    std::memcpy(commitment.commitment.data, &tx_extra[pos], 32);
    pos += 32;

    // Amount (8 bytes, little-endian)
    if (pos + 8 > tx_extra.size()) return false;
    commitment.amount = 0;
    for (int i = 0; i < 8; ++i) {
      commitment.amount |= static_cast<uint64_t>(tx_extra[pos + i]) << (i * 8);
    }
    pos += 8;

    // Term (4 bytes, little-endian)
    if (pos + 4 > tx_extra.size()) return false;
    commitment.term = 0;
    for (int i = 0; i < 4; ++i) {
      commitment.term |= static_cast<uint32_t>(tx_extra[pos + i]) << (i * 8);
    }
    pos += 4;

    // Chain code (1 byte)
    if (pos >= tx_extra.size()) return false;
    commitment.claimChainCode = tx_extra[pos];
    pos += 1;

    // Metadata size and data
    if (pos >= tx_extra.size()) return false;
    uint8_t metadataSize = tx_extra[pos];
    pos += 1;

    if (pos + metadataSize > tx_extra.size()) return false;
    if (metadataSize > 0) {
      commitment.metadata.assign(&tx_extra[pos], &tx_extra[pos] + metadataSize);
      pos += metadataSize;
    } else {
      commitment.metadata.clear();
    }

    // Gift secret size and data
    if (pos >= tx_extra.size()) return false;
    uint8_t giftSecretSize = tx_extra[pos];
    pos += 1;

    if (pos + giftSecretSize > tx_extra.size()) return false;
    if (giftSecretSize > 0) {
      commitment.gift_secret.assign(&tx_extra[pos], &tx_extra[pos] + giftSecretSize);
    } else {
      commitment.gift_secret.clear();
    }

    return true;
  }



  // ---------------- Secret encryption helpers ----------------

  // Size of the appended authentication tag (cn_fast_hash of ciphertext || key).
  static constexpr size_t GIFT_SECRET_MAC_SIZE = 32;

  // Encrypt secret with recipient's view key using ChaCha8 + HMAC-style authentication tag.
  // Output layout: [ ciphertext (secret.size() bytes) | MAC (32 bytes) ]
  bool encryptSecretWithViewKey(const std::vector<uint8_t>& secret, const Crypto::PublicKey& recipientViewKey, std::vector<uint8_t>& gift_secret)
  {
    try {
      // Derive encryption key and nonce from recipient's view key via Keccak.
      Crypto::Hash keyHash;
      keccak(recipientViewKey.data, sizeof(recipientViewKey.data), keyHash.data, sizeof(keyHash.data));

      // First 32 bytes → ChaCha8 key; next 8 bytes → IV.
      Crypto::chacha8_key chachaKey;
      memcpy(chachaKey.data, keyHash.data, CHACHA8_KEY_SIZE);

      Crypto::chacha8_iv chachaIV;
      memcpy(chachaIV.data, keyHash.data + CHACHA8_KEY_SIZE, CHACHA8_IV_SIZE);

      // Encrypt plaintext with ChaCha8.
      std::vector<uint8_t> ciphertext(secret.size());
      Crypto::chacha8(secret.data(), secret.size(), chachaKey, chachaIV,
                      reinterpret_cast<char*>(ciphertext.data()));

      // Compute authentication tag: cn_fast_hash( ciphertext || key ).
      std::vector<uint8_t> macInput(ciphertext.size() + CHACHA8_KEY_SIZE);
      memcpy(macInput.data(), ciphertext.data(), ciphertext.size());
      memcpy(macInput.data() + ciphertext.size(), chachaKey.data, CHACHA8_KEY_SIZE);
      Crypto::Hash macHash;
      Crypto::cn_fast_hash(macInput.data(), macInput.size(), macHash);

      // Output: ciphertext | MAC
      gift_secret.resize(ciphertext.size() + GIFT_SECRET_MAC_SIZE);
      memcpy(gift_secret.data(), ciphertext.data(), ciphertext.size());
      memcpy(gift_secret.data() + ciphertext.size(), macHash.data, GIFT_SECRET_MAC_SIZE);

      return true;
    } catch (...) {
      return false;
    }
  }

  // Decrypt secret with recipient's view key using ChaCha8 + HMAC verification.
  // Expects gift_secret layout: [ ciphertext | MAC (32 bytes) ]
  // Returns false if the MAC is invalid (authentication failure).
  bool decryptSecretWithViewKey(const std::vector<uint8_t>& gift_secret, const Crypto::SecretKey& viewSecretKey, std::vector<uint8_t>& secret)
  {
    try {
      // gift_secret must contain at least the MAC tag.
      if (gift_secret.size() < GIFT_SECRET_MAC_SIZE) {
        return false;
      }

      // Derive encryption key and nonce from the corresponding public view key.
      Crypto::PublicKey viewPublicKey;
      Crypto::secret_key_to_public_key(viewSecretKey, viewPublicKey);

      Crypto::Hash keyHash;
      keccak(viewPublicKey.data, sizeof(viewPublicKey.data), keyHash.data, sizeof(keyHash.data));

      Crypto::chacha8_key chachaKey;
      memcpy(chachaKey.data, keyHash.data, CHACHA8_KEY_SIZE);

      Crypto::chacha8_iv chachaIV;
      memcpy(chachaIV.data, keyHash.data + CHACHA8_KEY_SIZE, CHACHA8_IV_SIZE);

      // Split gift_secret into ciphertext and stored MAC.
      const size_t ciphertextLen = gift_secret.size() - GIFT_SECRET_MAC_SIZE;
      const uint8_t* ciphertextPtr = gift_secret.data();
      const uint8_t* storedMac    = gift_secret.data() + ciphertextLen;

      // Recompute MAC and verify BEFORE decrypting (authenticate-then-decrypt).
      std::vector<uint8_t> macInput(ciphertextLen + CHACHA8_KEY_SIZE);
      memcpy(macInput.data(), ciphertextPtr, ciphertextLen);
      memcpy(macInput.data() + ciphertextLen, chachaKey.data, CHACHA8_KEY_SIZE);
      Crypto::Hash macHash;
      Crypto::cn_fast_hash(macInput.data(), macInput.size(), macHash);

      if (memcmp(macHash.data, storedMac, GIFT_SECRET_MAC_SIZE) != 0) {
        return false; // Authentication failure — reject ciphertext.
      }

      // MAC is valid; decrypt with ChaCha8.
      secret.resize(ciphertextLen);
      Crypto::chacha8(ciphertextPtr, ciphertextLen, chachaKey, chachaIV,
                      reinterpret_cast<char*>(secret.data()));

      return true;
    } catch (...) {
      return false;
    }
  }


// Helper functions for handling gift_secret field
  bool isDummyGiftSecret(const std::vector<uint8_t>& gift_secret)
  {
    if (gift_secret.size() != 32) {
      return gift_secret.empty(); // Only empty or 32-byte patterns are valid
    }

    // Check if this looks like dummy data based on statistical patterns
    // Real encrypted data should have roughly uniform distribution
    if (gift_secret.size() != 32) {
      return false; // Must be exactly 32 bytes
    }

    // Check for pattern that suggests deterministic dummy data
    uint8_t patternCount = 0;
    for (size_t i = 1; i < 32; ++i) {
      if (gift_secret[i] == gift_secret[0]) {
        patternCount++;
      }
    }

    // If more than 50% of bytes are identical to the first byte, likely dummy
    return patternCount > 16;
  }

  // Helper function to create dummy gift secret that resembles real encrypted data
  std::vector<uint8_t> createDummyGiftSecret()
  {
    std::vector<uint8_t> dummy(32);

    // Seed based on current time (microseconds mod prime) to create variation between dummy secrets
    // but deterministic within the same run
    static uint32_t dummyCounter = 0xF5E8D3C1; // Start with arbitrary seed
    dummyCounter = (dummyCounter * 16777619) ^ 0x9E3779B9; // Mix in a constant

    // Generate pseudorandom-looking bytes with some patterns similar to real encrypted data
    for (size_t i = 0; i < 32; ++i) {
      uint32_t shifted = dummyCounter >> (i % 3 * 3);
      dummy[i] = static_cast<uint8_t>((shifted ^ i) & 0xFF);
      // Ensure we don't get repeating patterns
      if (i > 0 && dummy[i] == dummy[i-1]) {
        dummy[i] = static_cast<uint8_t>((dummy[i] + 0x57) & 0xFF);
      }
    }

    return dummy;
  }


  // Burn receipt functions
  bool getBurnReceiptFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraBurnReceipt& burnReceipt)
  {
    if (tx_extra.empty() || tx_extra[0] != TX_EXTRA_BURN_RECEIPT) {
      return false;
    }

    size_t pos = 1;

    // Parse proof_pubkey (32 bytes)
    if (pos + sizeof(Crypto::PublicKey) > tx_extra.size()) return false;
    std::memcpy(&burnReceipt.proof_pubkey, &tx_extra[pos], sizeof(Crypto::PublicKey));
    pos += sizeof(Crypto::PublicKey);

    // Parse tx_hash (variable length)
    if (pos >= tx_extra.size()) return false;
    uint32_t hashLen = 0;
    for (int i = 0; i < 4 && pos < tx_extra.size(); ++i, ++pos) {
      hashLen |= static_cast<uint32_t>(tx_extra[pos]) << (i * 8);
    }
    if (pos + hashLen > tx_extra.size()) return false;
    burnReceipt.tx_hash.assign(reinterpret_cast<const char*>(&tx_extra[pos]), hashLen);
    pos += hashLen;

    // Parse timestamp (8 bytes)
    if (pos + 8 > tx_extra.size()) return false;
    burnReceipt.timestamp = 0;
    for (int i = 0; i < 8; ++i) {
      burnReceipt.timestamp |= static_cast<uint64_t>(tx_extra[pos + i]) << (i * 8);
    }

    return true;
  }

  bool addBurnReceiptToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraBurnReceipt& burnReceipt)
  {
    tx_extra.push_back(TX_EXTRA_BURN_RECEIPT);

    // Add proof_pubkey
    tx_extra.insert(tx_extra.end(), reinterpret_cast<const uint8_t*>(&burnReceipt.proof_pubkey),
                    reinterpret_cast<const uint8_t*>(&burnReceipt.proof_pubkey) + sizeof(Crypto::PublicKey));

    // Add tx_hash length and data
    uint32_t hashLen = static_cast<uint32_t>(burnReceipt.tx_hash.length());
    for (int i = 0; i < 4; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(hashLen & 0xFF));
      hashLen >>= 8;
    }
    tx_extra.insert(tx_extra.end(), burnReceipt.tx_hash.begin(), burnReceipt.tx_hash.end());

    // Add timestamp
    uint64_t timestamp = burnReceipt.timestamp;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(timestamp & 0xFF));
      timestamp >>= 8;
    }

    return true;
  }

  // Deposit receipt functions
  bool getDepositReceiptFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraDepositReceipt& depositReceipt)
  {
    if (tx_extra.empty() || tx_extra[0] != TX_EXTRA_COLD_RECEIPT) {
      return false;
    }

    size_t pos = 1;

    // Parse proof_pubkey (32 bytes)
    if (pos + sizeof(Crypto::PublicKey) > tx_extra.size()) return false;
    std::memcpy(&depositReceipt.proof_pubkey, &tx_extra[pos], sizeof(Crypto::PublicKey));
    pos += sizeof(Crypto::PublicKey);

    // Parse tx_hash (variable length)
    if (pos >= tx_extra.size()) return false;
    uint32_t hashLen = 0;
    for (int i = 0; i < 4 && pos < tx_extra.size(); ++i, ++pos) {
      hashLen |= static_cast<uint32_t>(tx_extra[pos]) << (i * 8);
    }
    if (pos + hashLen > tx_extra.size()) return false;
    depositReceipt.tx_hash.assign(reinterpret_cast<const char*>(&tx_extra[pos]), hashLen);
    pos += hashLen;

    // Parse timestamp (8 bytes)
    if (pos + 8 > tx_extra.size()) return false;
    depositReceipt.timestamp = 0;
    for (int i = 0; i < 8; ++i) {
      depositReceipt.timestamp |= static_cast<uint64_t>(tx_extra[pos + i]) << (i * 8);
    }
    pos += 8;

    // Parse term (4 bytes)
    if (pos + 4 > tx_extra.size()) return false;
    depositReceipt.term = 0;
    for (int i = 0; i < 4; ++i) {
      depositReceipt.term |= static_cast<uint32_t>(tx_extra[pos + i]) << (i * 8);
    }
    pos += 4;

    // Parse deposit_type (variable length)
    if (pos >= tx_extra.size()) return false;
    uint32_t typeLen = 0;
    for (int i = 0; i < 4 && pos < tx_extra.size(); ++i, ++pos) {
      typeLen |= static_cast<uint32_t>(tx_extra[pos]) << (i * 8);
    }
    if (pos + typeLen > tx_extra.size()) return false;
    depositReceipt.deposit_type.assign(reinterpret_cast<const char*>(&tx_extra[pos]), typeLen);

    return true;
  }

  bool addDepositReceiptToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraDepositReceipt& depositReceipt)
  {
    tx_extra.push_back(TX_EXTRA_COLD_RECEIPT);

    // Add proof_pubkey
    tx_extra.insert(tx_extra.end(), reinterpret_cast<const uint8_t*>(&depositReceipt.proof_pubkey),
                    reinterpret_cast<const uint8_t*>(&depositReceipt.proof_pubkey) + sizeof(Crypto::PublicKey));

    // Add tx_hash length and data
    uint32_t hashLen = static_cast<uint32_t>(depositReceipt.tx_hash.length());
    for (int i = 0; i < 4; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(hashLen & 0xFF));
      hashLen >>= 8;
    }
    tx_extra.insert(tx_extra.end(), depositReceipt.tx_hash.begin(), depositReceipt.tx_hash.end());

    // Add timestamp
    uint64_t timestamp = depositReceipt.timestamp;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(timestamp & 0xFF));
      timestamp >>= 8;
    }

    // Add term
    uint32_t termValue = depositReceipt.term;
    for (int i = 0; i < 4; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(termValue & 0xFF));
      termValue >>= 8;
    }

    // Add deposit_type length and data
    uint32_t typeLen = static_cast<uint32_t>(depositReceipt.deposit_type.length());
    for (int i = 0; i < 4; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(typeLen & 0xFF));
      typeLen >>= 8;
    }
    tx_extra.insert(tx_extra.end(), depositReceipt.deposit_type.begin(), depositReceipt.deposit_type.end());

    return true;
  }

  bool createTxExtraWithBurnReceipt(const TransactionExtraBurnReceipt& burnReceipt, std::vector<uint8_t>& extra)
  {
    extra.clear();
    return addBurnReceiptToExtra(extra, burnReceipt);
  }

  bool createTxExtraWithDepositReceipt(const TransactionExtraDepositReceipt& depositReceipt, std::vector<uint8_t>& extra)
  {
    extra.clear();
    return addDepositReceiptToExtra(extra, depositReceipt);
  }

  // COLD Deposit term utility functions
  // Note: APR is now derived from tier in smart contract, not stored on-chain
  uint64_t getColdTermBlocks(uint8_t term_code) {
    switch (term_code) {
      case 1: return 16440;   // 3 months (~5480 blocks/month)
      case 2: return 49320;   // 9 months
      case 3: return 65760;   // 1 year
      case 4: return 197280;  // 3 years
      case 5: return 328800;  // 5 years
      default: return 0;
    }
  }

  uint64_t getColdTermDays(uint8_t term_code) {
    switch (term_code) {
      case 1: return 90;    // 3 months
      case 2: return 270;   // 9 months
      case 3: return 365;   // 1 year
      case 4: return 1095;  // 3 years
      case 5: return 1825;  // 5 years
      default: return 0;
    }
  }

  // ============================================================================
  // @ ALIAS REGISTRATION (0xEA)
  // ============================================================================

  bool TransactionExtraAliasRegistration::serialize(ISerializer& s) {
    s(version, "version");
    s(alias, "alias");
    s(aliasHash, "aliasHash");
    s(addressHash, "addressHash");
    s(ownerAddress, "ownerAddress");
    s(aliasType, "aliasType");
    return true;
  }

  bool TransactionExtraAliasRegistration::isValid() const {
    if (alias.length() != 8) {
      return false;
    }

    if (aliasType == 1) {
      // Regular user alias: [a-z0-9&] only
      for (char c : alias) {
        bool isLower = (c >= 'a' && c <= 'z');
        bool isDigit = (c >= '0' && c <= '9');
        bool isAmpersand = (c == '&');
        if (!isLower && !isDigit && !isAmpersand) return false;
      }
    } else {
      return false;
    }

    return true;
  }

  bool addAliasToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraAliasRegistration& alias) {
    if (!alias.isValid()) {
      return false;
    }

    // Write tag
    tx_extra.push_back(TX_EXTRA_ALIAS);

    // Serialize the alias registration
    BinaryArray ba;
    bool r = toBinaryArray(alias, ba);
    if (!r) return false;

    // Write size + data
    Tools::write_varint(std::back_inserter(tx_extra), ba.size());
    tx_extra.insert(tx_extra.end(), ba.begin(), ba.end());

    return true;
  }

  bool getAliasFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraAliasRegistration& alias) {
    // Find the 0xEA tag in extra
    for (size_t i = 0; i < tx_extra.size(); ++i) {
      if (tx_extra[i] == TX_EXTRA_ALIAS) {
        // Read size
        size_t offset = i + 1;
        if (offset >= tx_extra.size()) return false;

        uint64_t size = 0;
        auto begin = tx_extra.begin() + offset;
        auto end = tx_extra.end();
        int bytes_read = Tools::read_varint<64, std::vector<uint8_t>::const_iterator, uint64_t>(std::move(begin), std::move(end), size);
        if (bytes_read <= 0) return false;
        offset += bytes_read;

        if (offset + size > tx_extra.size()) return false;

        // Deserialize
        BinaryArray ba(tx_extra.begin() + offset, tx_extra.begin() + offset + size);
        return fromBinaryArray(alias, ba);
      }
    }
    return false;
  }

// ============================================================
// Fuego DepositCommitment Key Derivation
// ============================================================
DepositCommitmentKeys deriveCommitmentKeys(const std::array<uint8_t, 32>& depositSecret) {
  DepositCommitmentKeys keys;

  // keyScalar = hash_to_scalar("fuego_commit_key" || depositSecret)
  // hash_to_scalar = cn_fast_hash + sc_reduce32 to always produce a valid Ed25519 scalar.
  // The 16-byte label + 32-byte secret = 48 bytes total preimage.
  static const char label[] = "fuego_commit_key";  // exactly 16 bytes
  uint8_t preimage[48];
  memcpy(preimage,      label,               16);
  memcpy(preimage + 16, depositSecret.data(), 32);

  // hash_to_scalar applies cn_fast_hash then sc_reduce32 (mod l), so the
  // result is always < l and is a valid Ed25519 scalar.
  Crypto::hash_to_scalar(preimage, sizeof(preimage),
    reinterpret_cast<Crypto::EllipticCurveScalar&>(keys.keyScalar));

  // This can no longer fail because hash_to_scalar guarantees a valid scalar.
  Crypto::secret_key_to_public_key(keys.keyScalar, keys.commitKey);

  // keyImage = H_p(commitKey) * keyScalar  for standard Fuego key image
  Crypto::generate_key_image(keys.commitKey, keys.keyScalar, keys.keyImage);

  // amountMask = hash_to_scalar("fuego_amount_mask" || depositSecret) mod l
  {
    static const char amLabel[] = "fuego_amount_mask";  // 17 bytes
    uint8_t amPre[49];
    memcpy(amPre,      amLabel,              17);
    memcpy(amPre + 17, depositSecret.data(), 32);
    Crypto::hash_to_scalar(amPre, sizeof(amPre),
      reinterpret_cast<Crypto::EllipticCurveScalar&>(keys.amountMask));
  }

  return keys;
}

// ============================================================
// Unified Deposit Secret Encryption (0xD5 tag)
// ============================================================
//
// Key derivation: ECDH shared secret via generate_key_derivation, then
//   cn_fast_hash(derivation || domain_tag) → 32-byte chacha8 key.
// IV: first 8 bytes of txPubKey (already on-chain as tag 0x01 — no extra data).

namespace {

struct DepositKeyData {
  Crypto::KeyDerivation derivation;
  uint8_t tag[2];  // domain separator: {0xD5, 0x00}
};
static_assert(sizeof(DepositKeyData) == sizeof(Crypto::KeyDerivation) + 2, "");

// Derive chacha8 key from ECDH derivation.
static Crypto::chacha8_key depositEncKey(const Crypto::KeyDerivation& derivation) {
  DepositKeyData kd;
  kd.derivation = derivation;
  kd.tag[0] = 0xD5;
  kd.tag[1] = 0x00;
  Crypto::Hash h = Crypto::cn_fast_hash(&kd, sizeof(kd));
  Crypto::chacha8_key out;
  memcpy(out.data, &h, sizeof(out.data));
  return out;
}

// IV = first 8 bytes of txPubKey.
static Crypto::chacha8_iv depositEncIV(const Crypto::PublicKey& txPubKey) {
  Crypto::chacha8_iv iv;
  memcpy(iv.data, &txPubKey, sizeof(iv.data));
  return iv;
}

} // anonymous namespace

bool encryptDepositSecret(const DepositSecretPayload& plaintext,
                          const Crypto::PublicKey& recipientViewPubKey,
                          TransactionExtraDepositSecret& out) {
  // Ephemeral key generated internally to prevent nonce reuse across deposits
  Crypto::SecretKey ephSecKey;
  Crypto::generate_keys(out.ephPubKey, ephSecKey);

  Crypto::KeyDerivation derivation;
  if (!Crypto::generate_key_derivation(recipientViewPubKey, ephSecKey, derivation))
    return false;

  Crypto::chacha8_key encKey = depositEncKey(derivation);
  Crypto::chacha8_iv  encIV  = depositEncIV(out.ephPubKey);

  out.encryptedPayload.resize(sizeof(DepositSecretPayload));
  Crypto::chacha8(&plaintext, sizeof(DepositSecretPayload),
                  encKey, encIV,
                  reinterpret_cast<char*>(out.encryptedPayload.data()));
  return true;
}

bool decryptDepositSecret(const TransactionExtraDepositSecret& encrypted,
                          const Crypto::SecretKey& walletViewSecKey,
                          DepositSecretPayload& out) {
  if (encrypted.encryptedPayload.size() != sizeof(DepositSecretPayload))
    return false;

  Crypto::KeyDerivation derivation;
  if (!Crypto::generate_key_derivation(encrypted.ephPubKey, walletViewSecKey, derivation))
    return false;

  Crypto::chacha8_key encKey = depositEncKey(derivation);
  Crypto::chacha8_iv  encIV  = depositEncIV(encrypted.ephPubKey);

  Crypto::chacha8(encrypted.encryptedPayload.data(), sizeof(DepositSecretPayload),
                  encKey, encIV,
                  reinterpret_cast<char*>(&out));
  return true;
}

bool addDepositSecretToExtra(std::vector<uint8_t>& tx_extra,
                             const TransactionExtraDepositSecret& secret) {
  // Format: [0xD5][len=77][ephPubKey:32][ciphertext:45]
  if (secret.encryptedPayload.size() != sizeof(DepositSecretPayload))
    return false;
  const uint8_t totalLen = 32 + static_cast<uint8_t>(secret.encryptedPayload.size()); // 77
  tx_extra.push_back(TX_EXTRA_DEPOSIT_SECRET);
  tx_extra.push_back(totalLen);
  const auto* pubBytes = reinterpret_cast<const uint8_t*>(&secret.ephPubKey);
  tx_extra.insert(tx_extra.end(), pubBytes, pubBytes + 32);
  tx_extra.insert(tx_extra.end(),
                  secret.encryptedPayload.begin(),
                  secret.encryptedPayload.end());
  return true;
}

bool getDepositSecretFromExtra(const std::vector<uint8_t>& tx_extra,
                               TransactionExtraDepositSecret& out) {
  // Format: [0xD5][len=77][ephPubKey:32][ciphertext:45]
  const size_t expectedLen = 32 + sizeof(DepositSecretPayload); // 77
  for (size_t i = 0; i + 1 < tx_extra.size(); ++i) {
    if (tx_extra[i] != TX_EXTRA_DEPOSIT_SECRET)
      continue;
    uint8_t len = tx_extra[i + 1];
    if (len != expectedLen || i + 2 + len > tx_extra.size())
      return false;
    memcpy(&out.ephPubKey, &tx_extra[i + 2], 32);
    out.encryptedPayload.assign(tx_extra.begin() + i + 2 + 32,
                                tx_extra.begin() + i + 2 + len);
    return true;
  }
  return false;
}

} // namespace CryptoNote
