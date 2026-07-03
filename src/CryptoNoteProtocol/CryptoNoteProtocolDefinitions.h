// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2011-2017 The Cryptonote developers
// Copyright (c) 2017-2018 The Circle Foundation & Conceal Devs
// Copyright (c) 2018-2019 The TurtleCoin developers
// Copyright (c) 2016-2020 The Karbo developers
// Copyright (c) 2018-2021 Conceal Network & Conceal Devs
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <list>
#include "../CryptoNoteCore/CryptoNoteBasic.h"

// ISerializer-based serialization
#include "../Serialization/ISerializer.h"
#include "../Serialization/SerializationOverloads.h"
#include "../CryptoNoteCore/CryptoNoteSerialization.h"

namespace CryptoNote
{

#define BC_COMMANDS_POOL_BASE 2000

  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  struct block_complete_entry
  {
    std::string block;
    std::vector<std::string> txs;

    void serialize(ISerializer& s) {
      KV_MEMBER(block);
      KV_MEMBER(txs);
    }

  };

  struct BlockFullInfo : public block_complete_entry
  {
    Crypto::Hash block_id;

    void serialize(ISerializer& s) {
      KV_MEMBER(block_id);
      KV_MEMBER(block);
      KV_MEMBER(txs);
    }
  };

  struct TransactionPrefixInfo {
    Crypto::Hash txHash;
    TransactionPrefix txPrefix;

    void serialize(ISerializer& s) {
      KV_MEMBER(txHash);
      KV_MEMBER(txPrefix);
    }
  };

  struct BlockShortInfo {
    Crypto::Hash blockId;
    std::string block;
    std::vector<TransactionPrefixInfo> txPrefixes;

    void serialize(ISerializer& s) {
      KV_MEMBER(blockId);
      KV_MEMBER(block);
      KV_MEMBER(txPrefixes);
    }
  };

  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  struct NOTIFY_NEW_BLOCK_request
  {
    block_complete_entry b;
    uint32_t current_blockchain_height;
    uint32_t hop;

    void serialize(ISerializer& s) {
      KV_MEMBER(b)
      KV_MEMBER(current_blockchain_height)
      KV_MEMBER(hop)
    }
  };

  struct NOTIFY_NEW_BLOCK
  {
    const static int ID = BC_COMMANDS_POOL_BASE + 1;
    typedef NOTIFY_NEW_BLOCK_request request;
  };

  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  struct NOTIFY_NEW_TRANSACTIONS_request
  {
    std::vector<std::string> txs;
    bool dandelion_stem = false;
    uint32_t hop_count = 0;

    void serialize(ISerializer& s) {
      KV_MEMBER(txs);
      KV_MEMBER(dandelion_stem);
      KV_MEMBER(hop_count);
    }

  };

  struct NOTIFY_NEW_TRANSACTIONS
  {
    const static int ID = BC_COMMANDS_POOL_BASE + 2;
    typedef NOTIFY_NEW_TRANSACTIONS_request request;
  };

  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  struct NOTIFY_REQUEST_GET_OBJECTS_request
  {
    std::vector<Crypto::Hash> txs;
    std::vector<Crypto::Hash> blocks;

    void serialize(ISerializer& s) {
      serializeAsBinary(txs, "txs", s);
      serializeAsBinary(blocks, "blocks", s);
    }
  };

  struct NOTIFY_REQUEST_GET_OBJECTS
  {
    const static int ID = BC_COMMANDS_POOL_BASE + 3;
    typedef NOTIFY_REQUEST_GET_OBJECTS_request request;
  };

  struct NOTIFY_RESPONSE_GET_OBJECTS_request
  {
    std::vector<std::string> txs;
    std::vector<block_complete_entry> blocks;
    std::vector<Crypto::Hash> missed_ids;
    uint32_t current_blockchain_height;

    void serialize(ISerializer& s) {
      KV_MEMBER(txs)
      KV_MEMBER(blocks)
      serializeAsBinary(missed_ids, "missed_ids", s);
      KV_MEMBER(current_blockchain_height)
    }

  };

  struct NOTIFY_RESPONSE_GET_OBJECTS
  {
    const static int ID = BC_COMMANDS_POOL_BASE + 4;
    typedef NOTIFY_RESPONSE_GET_OBJECTS_request request;
  };

  struct NOTIFY_REQUEST_CHAIN
  {
    const static int ID = BC_COMMANDS_POOL_BASE + 6;

    struct request
    {
      std::vector<Crypto::Hash> block_ids; /*IDs of the first 10 blocks are sequential, next goes with pow(2,n) offset, like 2, 4, 8, 16, 32, 64 and so on, and the last one is always genesis block */

      void serialize(ISerializer& s) {
        serializeAsBinary(block_ids, "block_ids", s);
      }
    };
  };

  struct NOTIFY_RESPONSE_CHAIN_ENTRY_request
  {
    uint32_t start_height;
    uint32_t total_height;
    std::vector<Crypto::Hash> m_block_ids;

    void serialize(ISerializer& s) {
      KV_MEMBER(start_height)
      KV_MEMBER(total_height)
      serializeAsBinary(m_block_ids, "m_block_ids", s);
    }
  };

  struct NOTIFY_RESPONSE_CHAIN_ENTRY
  {
    const static int ID = BC_COMMANDS_POOL_BASE + 7;
    typedef NOTIFY_RESPONSE_CHAIN_ENTRY_request request;
  };

  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  struct NOTIFY_REQUEST_TX_POOL_request {
    std::vector<Crypto::Hash> txs;

    void serialize(ISerializer& s) {
      serializeAsBinary(txs, "txs", s);
    }
  };

  struct NOTIFY_REQUEST_TX_POOL {
    const static int ID = BC_COMMANDS_POOL_BASE + 8;
    typedef NOTIFY_REQUEST_TX_POOL_request request;
  };

  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  struct NOTIFY_NEW_LITE_BLOCK_request
  {
    std::string block;
    uint32_t current_blockchain_height;
    uint32_t hop;

    void serialize(ISerializer &s)
    {
      KV_MEMBER(block)
      KV_MEMBER(current_blockchain_height)
      KV_MEMBER(hop)
    }
  };

  struct NOTIFY_NEW_LITE_BLOCK
  {
    const static int ID = BC_COMMANDS_POOL_BASE + 9;
    typedef NOTIFY_NEW_LITE_BLOCK_request request;
  };

  struct NOTIFY_MISSING_TXS_request
  {
    Crypto::Hash blockHash;
    uint32_t current_blockchain_height;
    std::vector<Crypto::Hash> missing_txs;

    void serialize(ISerializer &s)
    {
      KV_MEMBER(blockHash)
      KV_MEMBER(current_blockchain_height)
      serializeAsBinary(missing_txs, "missing_txs", s);
    }
  };

  struct NOTIFY_MISSING_TXS
  {
    const static int ID = BC_COMMANDS_POOL_BASE + 10;
    typedef NOTIFY_MISSING_TXS_request request;
  };

  /************************************************************************/
  /* v13+ Orderbook P2P (off-chain order gossip)                          */
  /************************************************************************/

  struct NOTIFY_ORDER_PLACE_request {
    // Full order data carried in the P2P message body.
    // Recipient validates pre-signed partial signature before accepting.
    uint8_t  side;           // 0 = BUY_XFG, 1 = SELL_XFG
    uint64_t amount;         // atomic units
    uint64_t price;          // × COIN
    uint32_t expiration;
    Crypto::Hash orderId;
    Crypto::Hash utxoTxHash;
    uint32_t outputIndex;
    Crypto::PublicKey spendKey;
    Crypto::PublicKey viewKey;
    std::vector<Crypto::Signature> partialSigs;

    void serialize(ISerializer& s) {
      KV_MEMBER(side)
      KV_MEMBER(amount)
      KV_MEMBER(price)
      KV_MEMBER(expiration)
      s(orderId, "orderId");
      s(utxoTxHash, "utxoTxHash");
      KV_MEMBER(outputIndex)
      s(spendKey, "spendKey");
      s(viewKey, "viewKey");
      s(partialSigs, "partialSigs");
    }
  };

  struct NOTIFY_ORDER_PLACE {
    const static int ID = BC_COMMANDS_POOL_BASE + 11;
    typedef NOTIFY_ORDER_PLACE_request request;
  };

  struct NOTIFY_ORDER_CANCEL_request {
    Crypto::Hash orderId;

    void serialize(ISerializer& s) {
      s(orderId, "orderId");
    }
  };

  struct NOTIFY_ORDER_CANCEL {
    const static int ID = BC_COMMANDS_POOL_BASE + 12;
    typedef NOTIFY_ORDER_CANCEL_request request;
  };

  struct NOTIFY_ORDERBOOK_RECEIPT_request {
    Crypto::Hash settlementTxHash;
    uint64_t clearingPrice;
    uint32_t numBidLevels;
    uint32_t numAskLevels;
    std::vector<uint64_t> bidPrices;
    std::vector<uint64_t> bidDepths;
    std::vector<uint64_t> askPrices;
    std::vector<uint64_t> askDepths;

    void serialize(ISerializer& s) {
      s(settlementTxHash, "settlementTxHash");
      KV_MEMBER(clearingPrice)
      KV_MEMBER(numBidLevels)
      KV_MEMBER(numAskLevels)
      KV_MEMBER(bidPrices)
      KV_MEMBER(bidDepths)
      KV_MEMBER(askPrices)
      KV_MEMBER(askDepths)
    }
  };

  struct NOTIFY_ORDERBOOK_RECEIPT {
    const static int ID = BC_COMMANDS_POOL_BASE + 13;
    typedef NOTIFY_ORDERBOOK_RECEIPT_request request;
  };

} // namespace CryptoNote
