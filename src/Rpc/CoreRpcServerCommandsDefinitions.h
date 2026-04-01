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

#include "../CryptoNoteProtocol/CryptoNoteProtocolDefinitions.h"
#include "../CryptoNoteCore/CryptoNoteBasic.h"
#include "../CryptoNoteCore/Difficulty.h"
#include "../crypto/hash.h"

#include "../Serialization/SerializationOverloads.h"

namespace CryptoNote {
//-----------------------------------------------
#define CORE_RPC_STATUS_OK "OK"
#define CORE_RPC_STATUS_BUSY "BUSY"

struct EMPTY_STRUCT {
  void serialize(ISerializer &s) {}
};

struct STATUS_STRUCT {
  std::string status;

  void serialize(ISerializer &s) {
    KV_MEMBER(status)
  }
};

struct COMMAND_RPC_GET_HEIGHT {
  typedef EMPTY_STRUCT request;

  struct response {
    uint64_t height;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(height)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_BLOCKS_FAST {

  struct request {
    std::vector<Crypto::Hash> block_ids; //*first 10 blocks id goes sequential, next goes in pow(2,n) offset, like 2, 4, 8, 16, 32, 64 and so on, and the last one is always genesis block */

    void serialize(ISerializer &s) {
      serializeAsBinary(block_ids, "block_ids", s);
    }
  };

  struct response {
    std::vector<block_complete_entry> blocks;
    uint64_t start_height;
    uint64_t current_height;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(blocks)
      KV_MEMBER(start_height)
      KV_MEMBER(current_height)
      KV_MEMBER(status)
    }
  };
};
//-----------------------------------------------
struct COMMAND_RPC_GET_TRANSACTIONS {
  struct request {
    std::vector<std::string> txs_hashes;

    void serialize(ISerializer &s) {
      KV_MEMBER(txs_hashes)
    }
  };

  struct response {
    std::vector<std::string> txs_as_hex; //transactions blobs as hex
    std::vector<std::string> missed_tx;  //not found transactions
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(txs_as_hex)
      KV_MEMBER(missed_tx)
      KV_MEMBER(status)
    }
  };
};
struct DepositRpcInfo {
  uint64_t id;
  uint64_t amount;
  uint64_t term;
  uint64_t interest;
  std::string creatingTransactionHash;
  std::string spendingTransactionHash;
  bool locked;
  uint64_t height;
  uint64_t unlockHeight;
  std::string address;

  void serialize(ISerializer &s) {
    KV_MEMBER(id)
    KV_MEMBER(amount)
    KV_MEMBER(term)
    KV_MEMBER(interest)
    KV_MEMBER(creatingTransactionHash)
    KV_MEMBER(spendingTransactionHash)
    KV_MEMBER(locked)
    KV_MEMBER(height)
    KV_MEMBER(unlockHeight)
    KV_MEMBER(address)
  }
};
struct COMMAND_RPC_GET_DEPOSITS {
  struct request {
    std::vector<std::string> addresses;
    std::string blockHash;
    uint32_t firstBlockIndex;
    uint32_t blockCount;
    std::string paymentId;

    void serialize(ISerializer &s) {
      KV_MEMBER(addresses)
      KV_MEMBER(blockHash)
      KV_MEMBER(firstBlockIndex)
      KV_MEMBER(blockCount)
      KV_MEMBER(paymentId)
    }
  };

  struct response {
    std::vector<DepositRpcInfo> deposits;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(deposits)
      KV_MEMBER(status)
    }
  };
};

struct block_short_response
{
  uint64_t timestamp;
  uint32_t height;
  std::string hash;
  uint64_t transactions_count;
  uint64_t cumulative_size;
  difficulty_type difficulty;

  void serialize(ISerializer &s)
  {
    KV_MEMBER(timestamp)
    KV_MEMBER(height)
    KV_MEMBER(hash)
    KV_MEMBER(cumulative_size)
    KV_MEMBER(transactions_count)
    KV_MEMBER(difficulty)
  }
};

//-----------------------------------------------
struct COMMAND_RPC_GET_POOL_CHANGES {
  struct request {
    Crypto::Hash tailBlockId;
    std::vector<Crypto::Hash> knownTxsIds;

    void serialize(ISerializer &s) {
      KV_MEMBER(tailBlockId)
      serializeAsBinary(knownTxsIds, "knownTxsIds", s);
    }
  };

  struct response {
    bool isTailBlockActual;
    std::vector<BinaryArray> addedTxs;          // Added transactions blobs
    std::vector<Crypto::Hash> deletedTxsIds; // IDs of not found transactions
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(isTailBlockActual)
      KV_MEMBER(addedTxs)
      serializeAsBinary(deletedTxsIds, "deletedTxsIds", s);
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_ALT_BLOCKS_LIST
{
  typedef EMPTY_STRUCT request;

  struct response
  {
    std::vector<block_short_response> alt_blocks;
    std::string status;

    void serialize(ISerializer &s)
    {
      KV_MEMBER(alt_blocks)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_POOL_CHANGES_LITE {
  struct request {
    Crypto::Hash tailBlockId;
    std::vector<Crypto::Hash> knownTxsIds;

    void serialize(ISerializer &s) {
      KV_MEMBER(tailBlockId)
      serializeAsBinary(knownTxsIds, "knownTxsIds", s);
    }
  };

  struct response {
    bool isTailBlockActual;
    std::vector<TransactionPrefixInfo> addedTxs;          // Added transactions blobs
    std::vector<Crypto::Hash> deletedTxsIds; // IDs of not found transactions
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(isTailBlockActual)
      KV_MEMBER(addedTxs)
      serializeAsBinary(deletedTxsIds, "deletedTxsIds", s);
      KV_MEMBER(status)
    }
  };
};

//-----------------------------------------------
struct COMMAND_RPC_GET_TX_GLOBAL_OUTPUTS_INDEXES {

  struct request {
    Crypto::Hash txid;

    void serialize(ISerializer &s) {
      KV_MEMBER(txid)
    }
  };

  struct response {
    std::vector<uint64_t> o_indexes;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(o_indexes)
      KV_MEMBER(status)
    }
  };
};
//-----------------------------------------------
struct COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_request {
  std::vector<uint64_t> amounts;
  uint64_t outs_count;

  void serialize(ISerializer &s) {
    KV_MEMBER(amounts)
    KV_MEMBER(outs_count)
  }
};

#pragma pack(push, 1)
struct COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_out_entry {
  uint64_t global_amount_index;
  Crypto::PublicKey out_key;
};
#pragma pack(pop)

struct COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_outs_for_amount {
  uint64_t amount;
  std::vector<COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_out_entry> outs;

  void serialize(ISerializer &s) {
    KV_MEMBER(amount)
    serializeAsBinary(outs, "outs", s);
  }
};

struct COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_response {
  std::vector<COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_outs_for_amount> outs;
  std::string status;

  void serialize(ISerializer &s) {
    KV_MEMBER(outs);
    KV_MEMBER(status)
  }
};

struct COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS {
  typedef COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_request request;
  typedef COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_response response;

  typedef COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_out_entry out_entry;
  typedef COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_outs_for_amount outs_for_amount;
};

//-----------------------------------------------
// Random commitment outputs for ring-signature deposit withdrawals.
// Works like COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS but indexes m_commitmentOutputs.
#pragma pack(push, 1)
struct COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS_out_entry {
  uint32_t global_amount_index;
  Crypto::PublicKey commit_key;
};
#pragma pack(pop)

struct COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS {
  struct request {
    uint64_t amount;
    uint64_t outs_count;

    void serialize(ISerializer& s) {
      KV_MEMBER(amount)
      KV_MEMBER(outs_count)
    }
  };

  struct response {
    std::vector<COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS_out_entry> outs;
    std::string status;

    void serialize(ISerializer& s) {
      serializeAsBinary(outs, "outs", s);
      KV_MEMBER(status)
    }
  };

  typedef COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS_out_entry out_entry;
};

//-----------------------------------------------
struct COMMAND_RPC_SEND_RAW_TX {
  struct request {
    std::string tx_as_hex;

    request() {}
    explicit request(const Transaction &);

    void serialize(ISerializer &s) {
      KV_MEMBER(tx_as_hex)
    }
  };

  struct response {
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(status)
    }
  };
};
//-----------------------------------------------
struct COMMAND_RPC_START_MINING {
  struct request {
    std::string miner_address;
    uint64_t threads_count;

    void serialize(ISerializer &s) {
      KV_MEMBER(miner_address)
      KV_MEMBER(threads_count)
    }
  };

  struct response {
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(status)
    }
  };
};
//-----------------------------------------------
struct COMMAND_RPC_GET_INFO {
  typedef EMPTY_STRUCT request;

  struct response {
    std::string status;
    std::string version;
    std::string fee_address;
    std::string top_block_hash;
    uint64_t height;
    uint64_t difficulty;
    uint64_t tx_count;
    uint64_t tx_pool_size;
    uint64_t alt_blocks_count;
    uint64_t outgoing_connections_count;
    uint64_t incoming_connections_count;
    uint64_t white_peerlist_size;
    uint64_t grey_peerlist_size;
    uint8_t block_major_version;
    uint8_t block_minor_version;
    uint32_t last_known_block_index;
    uint64_t full_deposit_amount;
    uint64_t ethereal_xfg;
    uint64_t last_block_reward;
    uint64_t last_block_timestamp;
    uint64_t last_block_difficulty;
    std::vector<std::string> connections;

    void serialize(ISerializer &s) {
      KV_MEMBER(status)
      KV_MEMBER(height)
      KV_MEMBER(version)
      KV_MEMBER(difficulty)
      KV_MEMBER(top_block_hash)
      KV_MEMBER(tx_count)
      KV_MEMBER(tx_pool_size)
      KV_MEMBER(alt_blocks_count)
      KV_MEMBER(outgoing_connections_count)
      KV_MEMBER(fee_address)
      KV_MEMBER(block_major_version)
      KV_MEMBER(block_minor_version)
      KV_MEMBER(incoming_connections_count)
      KV_MEMBER(white_peerlist_size)
      KV_MEMBER(grey_peerlist_size)
      KV_MEMBER(last_known_block_index)
      KV_MEMBER(full_deposit_amount)
      KV_MEMBER(ethereal_xfg)
      KV_MEMBER(last_block_reward)
      KV_MEMBER(last_block_timestamp)
      KV_MEMBER(last_block_difficulty)
      KV_MEMBER(connections)
    }
  };
};

//-----------------------------------------------
struct COMMAND_RPC_GET_PEER_LIST {
	typedef EMPTY_STRUCT request;

	struct response {
		std::vector<std::string> peers;
		std::string status;

		void serialize(ISerializer &s) {
			KV_MEMBER(peers)
			KV_MEMBER(status)
		}
	};
};

//-----------------------------------------------
struct COMMAND_RPC_STOP_MINING {
  typedef EMPTY_STRUCT request;
  typedef STATUS_STRUCT response;
};

//-----------------------------------------------
struct COMMAND_RPC_STOP_DAEMON {
  typedef EMPTY_STRUCT request;
  typedef STATUS_STRUCT response;
};

//
struct COMMAND_RPC_GETBLOCKCOUNT {
  typedef std::vector<std::string> request;

  struct response {
    uint64_t count;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(count)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_PROVE_COLLATERAL {
  struct request {
    std::string transactionHash;
    uint8_t commitment_type;  // 136=Burn(0x08), 7=CIA(0x07), 205=CD(0xCD)
    bool commitment;          // Whether to verify commitment

    void serialize(ISerializer &s) {
      KV_MEMBER(transactionHash)
      KV_MEMBER(commitment_type)
      KV_MEMBER(commitment)
    }
  };

  struct response {
    bool exists;
    uint64_t amount;          // Actual transaction amount (atomic units)
    bool hasCommitment;
    uint8_t commitmentType;
    std::string status;
    std::string errorMessage;

    void serialize(ISerializer &s) {
      KV_MEMBER(exists)
      KV_MEMBER(amount)
      KV_MEMBER(hasCommitment)
      KV_MEMBER(commitmentType)
      KV_MEMBER(status)
      KV_MEMBER(errorMessage)
    }
  };
};

struct COMMAND_RPC_GET_FEE_ADDRESS {
  typedef EMPTY_STRUCT request;

  struct response {
    std::string fee_address;
	std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(fee_address)
	  KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GETBLOCKHASH {
  typedef std::vector<uint64_t> request;
  typedef std::string response;
};

struct COMMAND_RPC_GETBLOCKTEMPLATE {
  struct request {
    uint64_t reserve_size; //max 255 bytes
    std::string wallet_address;

    void serialize(ISerializer &s) {
      KV_MEMBER(reserve_size)
      KV_MEMBER(wallet_address)
    }
  };

  struct response {
    uint64_t difficulty;
    uint32_t height;
    uint64_t reserved_offset;
    std::string blocktemplate_blob;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(difficulty)
      KV_MEMBER(height)
      KV_MEMBER(reserved_offset)
      KV_MEMBER(blocktemplate_blob)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_CURRENCY_ID {
  typedef EMPTY_STRUCT request;

  struct response {
    std::string currency_id_blob;

    void serialize(ISerializer &s) {
      KV_MEMBER(currency_id_blob)
    }
  };
};

struct COMMAND_RPC_SUBMITBLOCK {
  typedef std::vector<std::string> request;
  typedef STATUS_STRUCT response;
};

struct block_header_response {
  uint8_t major_version;
  uint8_t minor_version;
  uint64_t timestamp;
  std::string prev_hash;
  uint32_t nonce;
  bool orphan_status;
  uint64_t height;
  uint64_t depth;
  uint64_t deposits;
  std::string hash;
  difficulty_type difficulty;
  uint64_t reward;

  void serialize(ISerializer &s) {
    KV_MEMBER(major_version)
    KV_MEMBER(minor_version)
    KV_MEMBER(timestamp)
    KV_MEMBER(prev_hash)
    KV_MEMBER(nonce)
    KV_MEMBER(orphan_status)
    KV_MEMBER(height)
    KV_MEMBER(depth)
    KV_MEMBER(deposits)
    KV_MEMBER(hash)
    KV_MEMBER(difficulty)
    KV_MEMBER(reward)
  }
};

struct BLOCK_HEADER_RESPONSE {
  std::string status;
  block_header_response block_header;

  void serialize(ISerializer &s) {
    KV_MEMBER(block_header)
    KV_MEMBER(status)
  }
};


struct f_transaction_short_response {
  std::string hash;
  uint64_t fee;
  uint64_t amount_out;
  uint64_t size;

  void serialize(ISerializer &s) {
    KV_MEMBER(hash)
    KV_MEMBER(fee)
    KV_MEMBER(amount_out)
    KV_MEMBER(size)
  }
};

struct f_transaction_details_response {
  std::string hash;
  size_t size;
  std::string paymentId;
  uint64_t mixin;
  uint64_t fee;
  uint64_t amount_out;
  std::string networkId;  // Added for STARK proof validation

  void serialize(ISerializer &s) {
    KV_MEMBER(hash)
    KV_MEMBER(size)
    KV_MEMBER(paymentId)
    KV_MEMBER(mixin)
    KV_MEMBER(fee)
    KV_MEMBER(amount_out)
    KV_MEMBER(networkId)
  }
};

struct f_block_short_response {
  uint64_t timestamp;
  uint32_t height;
  difficulty_type difficulty;
  std::string hash;
  uint64_t tx_count;
  uint64_t cumul_size;

  void serialize(ISerializer &s) {
    KV_MEMBER(timestamp)
    KV_MEMBER(height)
    KV_MEMBER(difficulty)
    KV_MEMBER(hash)
    KV_MEMBER(cumul_size)
    KV_MEMBER(tx_count)
  }
};

struct f_block_details_response {
  uint8_t major_version;
  uint8_t minor_version;
  uint64_t timestamp;
  std::string prev_hash;
  uint32_t nonce;
  bool orphan_status;
  uint64_t height;
  uint64_t depth;
  std::string hash;
  difficulty_type difficulty;
  uint64_t reward;
  uint64_t blockSize;
  size_t sizeMedian;
  uint64_t effectiveSizeMedian;
  uint64_t transactionsCumulativeSize;
  std::string alreadyGeneratedCoins;
  uint64_t alreadyGeneratedTransactions;
  uint64_t baseReward;
  double penalty;
  uint64_t totalFeeAmount;
  std::vector<f_transaction_short_response> transactions;

  void serialize(ISerializer &s) {
    KV_MEMBER(major_version)
    KV_MEMBER(minor_version)
    KV_MEMBER(timestamp)
    KV_MEMBER(prev_hash)
    KV_MEMBER(nonce)
    KV_MEMBER(orphan_status)
    KV_MEMBER(height)
    KV_MEMBER(depth)
    KV_MEMBER(hash)
    KV_MEMBER(difficulty)
    KV_MEMBER(reward)
    KV_MEMBER(blockSize)
    KV_MEMBER(sizeMedian)
    KV_MEMBER(effectiveSizeMedian)
    KV_MEMBER(transactionsCumulativeSize)
    KV_MEMBER(alreadyGeneratedCoins)
    KV_MEMBER(alreadyGeneratedTransactions)
    KV_MEMBER(baseReward)
    KV_MEMBER(penalty)
    KV_MEMBER(transactions)
    KV_MEMBER(totalFeeAmount)
  }
};
struct currency_base_coin {
  std::string name;
  std::string git;

  void serialize(ISerializer &s) {
    KV_MEMBER(name)
    KV_MEMBER(git)
  }
};

struct currency_core {
  std::vector<std::string> SEED_NODES;
  uint64_t EMISSION_SPEED_FACTOR;
  uint64_t DIFFICULTY_TARGET;
  uint64_t CRYPTONOTE_DISPLAY_DECIMAL_POINT;
  std::string MONEY_SUPPLY;
 // uint64_t GENESIS_BLOCK_REWARD;
  uint64_t DEFAULT_DUST_THRESHOLD;
  uint64_t MINIMUM_FEE;
  uint64_t CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW;
  uint64_t CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE;
//  uint64_t CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE_V1;
  uint64_t CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX;
  uint64_t P2P_DEFAULT_PORT;
  uint64_t RPC_DEFAULT_PORT;
  uint64_t MAX_BLOCK_SIZE_INITIAL;
  uint64_t EXPECTED_NUMBER_OF_BLOCKS_PER_DAY;
  uint64_t UPGRADE_HEIGHT;
  uint64_t DIFFICULTY_CUT;
  uint64_t DIFFICULTY_LAG;
  //std::string BYTECOIN_NETWORK;
  std::string CRYPTONOTE_NAME;
  std::string GENESIS_COINBASE_TX_HEX;
  std::vector<std::string> CHECKPOINTS;

  void serialize(ISerializer &s) {
    KV_MEMBER(SEED_NODES)
    KV_MEMBER(EMISSION_SPEED_FACTOR)
    KV_MEMBER(DIFFICULTY_TARGET)
    KV_MEMBER(CRYPTONOTE_DISPLAY_DECIMAL_POINT)
    KV_MEMBER(MONEY_SUPPLY)
 //   KV_MEMBER(GENESIS_BLOCK_REWARD)
    KV_MEMBER(DEFAULT_DUST_THRESHOLD)
    KV_MEMBER(MINIMUM_FEE)
    KV_MEMBER(CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW)
    KV_MEMBER(CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE)
//    KV_MEMBER(CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE_V1)
    KV_MEMBER(CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX)
    KV_MEMBER(P2P_DEFAULT_PORT)
    KV_MEMBER(RPC_DEFAULT_PORT)
    KV_MEMBER(MAX_BLOCK_SIZE_INITIAL)
    KV_MEMBER(EXPECTED_NUMBER_OF_BLOCKS_PER_DAY)
    KV_MEMBER(UPGRADE_HEIGHT)
    KV_MEMBER(DIFFICULTY_CUT)
    KV_MEMBER(DIFFICULTY_LAG)
    KV_MEMBER(CRYPTONOTE_NAME)
    KV_MEMBER(GENESIS_COINBASE_TX_HEX)
    KV_MEMBER(CHECKPOINTS)
  }
};


struct COMMAND_RPC_GET_LAST_BLOCK_HEADER {
  typedef EMPTY_STRUCT request;
  typedef BLOCK_HEADER_RESPONSE response;
};

struct COMMAND_RPC_GET_BLOCK_HEADER_BY_HASH {
  struct request {
    std::string hash;

    void serialize(ISerializer &s) {
      KV_MEMBER(hash)
    }
  };

  typedef BLOCK_HEADER_RESPONSE response;
};

struct COMMAND_RPC_GET_BLOCK_HEADER_BY_HEIGHT {
  struct request {
    uint64_t height;

    void serialize(ISerializer &s) {
      KV_MEMBER(height)
    }
  };

  typedef BLOCK_HEADER_RESPONSE response;
};



struct F_COMMAND_RPC_GET_BLOCKS_LIST {
  struct request {
    uint64_t height;

    void serialize(ISerializer &s) {
      KV_MEMBER(height)
    }
  };

  struct response {
    std::vector<f_block_short_response> blocks; //transactions blobs as hex
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(blocks)
      KV_MEMBER(status)
    }
  };
};

struct F_COMMAND_RPC_GET_BLOCK_DETAILS {
  struct request {
    std::string hash;

    void serialize(ISerializer &s) {
      KV_MEMBER(hash)
    }
  };

  struct response {
    f_block_details_response block;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(block)
      KV_MEMBER(status)
    }
  };
};

struct F_COMMAND_RPC_GET_TRANSACTION_DETAILS {
  struct request {
    std::string hash;

    void serialize(ISerializer &s) {
      KV_MEMBER(hash)
    }
  };

  struct response {
    Transaction tx;
    f_transaction_details_response txDetails;
    f_block_short_response block;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(tx)
      KV_MEMBER(txDetails)
      KV_MEMBER(block)
      KV_MEMBER(status)
    }
  };
};

struct F_COMMAND_RPC_GET_POOL {
    typedef EMPTY_STRUCT request;

    struct response {
        std::vector<f_transaction_short_response> transactions; //transactions blobs as hex
        std::string status;

        void serialize(ISerializer &s) {
            KV_MEMBER(transactions)
            KV_MEMBER(status)
        }
    };
};

struct F_COMMAND_RPC_GET_BLOCKCHAIN_SETTINGS {
  typedef EMPTY_STRUCT request;
  struct response {
    currency_base_coin base_coin;
    currency_core core;
    std::vector<std::string> extensions;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(base_coin)
      KV_MEMBER(core)
      KV_MEMBER(extensions)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_QUERY_BLOCKS {
  struct request {
    std::vector<Crypto::Hash> block_ids; //*first 10 blocks id goes sequential, next goes in pow(2,n) offset, like 2, 4, 8, 16, 32, 64 and so on, and the last one is always genesis block */
    uint64_t timestamp;

    void serialize(ISerializer &s) {
      serializeAsBinary(block_ids, "block_ids", s);
      KV_MEMBER(timestamp)
    }
  };

  struct response {
    std::string status;
    uint64_t start_height;
    uint64_t current_height;
    uint64_t full_offset;
    std::vector<BlockFullInfo> items;

    void serialize(ISerializer &s) {
      KV_MEMBER(status)
      KV_MEMBER(start_height)
      KV_MEMBER(current_height)
      KV_MEMBER(full_offset)
      KV_MEMBER(items)
    }
  };
};

struct COMMAND_RPC_QUERY_BLOCKS_LITE {
  struct request {
    std::vector<Crypto::Hash> blockIds;
    uint64_t timestamp;

    void serialize(ISerializer &s) {
      serializeAsBinary(blockIds, "block_ids", s);
      KV_MEMBER(timestamp)
    }
  };

  struct response {
    std::string status;
    uint64_t startHeight;
    uint64_t currentHeight;
    uint64_t fullOffset;
    std::vector<BlockShortInfo> items;

    void serialize(ISerializer &s) {
      KV_MEMBER(status)
      KV_MEMBER(startHeight)
      KV_MEMBER(currentHeight)
      KV_MEMBER(fullOffset)
      KV_MEMBER(items)
    }
  };
};

struct COMMAND_RPC_GEN_PAYMENT_ID {
  typedef EMPTY_STRUCT request;

  struct response {
	  std::string payment_id;

	  void serialize(ISerializer &s) {
		  KV_MEMBER(payment_id)
	  }
  };
};

struct reserve_proof_entry
{
	Crypto::Hash txid;
	uint64_t index_in_tx;
	Crypto::PublicKey shared_secret;
	Crypto::KeyImage key_image;
	Crypto::Signature shared_secret_sig;
	Crypto::Signature key_image_sig;

	void serialize(ISerializer& s)
	{
		KV_MEMBER(txid)
		KV_MEMBER(index_in_tx)
		KV_MEMBER(shared_secret)
		KV_MEMBER(key_image)
		KV_MEMBER(shared_secret_sig)
		KV_MEMBER(key_image_sig)
	}
};

struct reserve_proof {
	std::vector<reserve_proof_entry> proofs;
	Crypto::Signature signature;

	void serialize(ISerializer &s) {
		KV_MEMBER(proofs)
		KV_MEMBER(signature)
	}
};

struct K_COMMAND_RPC_CHECK_TX_PROOF {
    struct request {
        std::string tx_id;
        std::string dest_address;
        std::string signature;

        void serialize(ISerializer &s) {
            KV_MEMBER(tx_id)
            KV_MEMBER(dest_address)
            KV_MEMBER(signature)
        }
    };

    struct response {
        bool signature_valid;
        uint64_t received_amount;
		std::vector<TransactionOutput> outputs;
		uint32_t confirmations = 0;
        std::string status;
        uint64_t total;
        uint64_t spent;
        bool good;

        void serialize(ISerializer &s) {
            KV_MEMBER(signature_valid)
            KV_MEMBER(received_amount)
            KV_MEMBER(outputs)
            KV_MEMBER(confirmations)
            KV_MEMBER(status)
        }
    };
};

struct K_COMMAND_RPC_CHECK_RESERVE_PROOF {
	struct request {
		std::string address;
		std::string message;
		std::string signature;

		void serialize(ISerializer &s) {
			KV_MEMBER(address)
			KV_MEMBER(message)
			KV_MEMBER(signature)
		}
	};

	struct response	{
		bool good;
		uint64_t total;
		uint64_t spent;

		void serialize(ISerializer &s) {
			KV_MEMBER(good)
			KV_MEMBER(total)
			KV_MEMBER(spent)
		}
	};
};


//-----------------------------------------------
// Elderfier Signature Consensus RPC Endpoints
//-----------------------------------------------

struct COMMAND_RPC_GET_ELDERFIER_SIGNATURES {
	typedef EMPTY_STRUCT request;

	struct SignatureInfo {
		uint8_t elderfier_id;
		std::string signing_pubkey;  // Ed25519 pubkey hex (64 chars) — for L2 contract verification
		std::string signature;       // Ed25519 signature hex (128 chars) — for L2 contract verification
		uint64_t block_height;
		uint64_t timestamp;
		bool is_valid;

		void serialize(ISerializer& s) {
			KV_MEMBER(elderfier_id)
			KV_MEMBER(signing_pubkey)
			KV_MEMBER(signature)
			KV_MEMBER(block_height)
			KV_MEMBER(timestamp)
			KV_MEMBER(is_valid)
		}
	};

	struct response {
		std::vector<SignatureInfo> signatures;
		std::string current_merkle_root;
		uint64_t current_block_height;
		size_t total_registered_elderfiers;
		size_t signatures_received;
		uint8_t consensus_percentage;
		bool threshold_met;
		std::vector<uint8_t> signed_by;
		std::vector<uint8_t> pending;
		std::string status;

		void serialize(ISerializer& s) {
			KV_MEMBER(signatures)
			KV_MEMBER(current_merkle_root)
			KV_MEMBER(current_block_height)
			KV_MEMBER(total_registered_elderfiers)
			KV_MEMBER(signatures_received)
			KV_MEMBER(consensus_percentage)
			KV_MEMBER(threshold_met)
			KV_MEMBER(signed_by)
			KV_MEMBER(pending)
			KV_MEMBER(status)
		}
	};
};

struct COMMAND_RPC_GET_ELDERFIER_CONSENSUS_STATUS {
	typedef EMPTY_STRUCT request;

	struct response {
		std::string current_merkle_root;
		uint64_t current_block_height;
		size_t total_registered_elderfiers;
		size_t elderfiers_signed;
		uint8_t consensus_percentage;
		std::vector<uint8_t> signed_by;
		std::vector<uint8_t> pending;
		bool meets_69_percent;
		bool ready_for_user_claim;
		uint64_t blocks_until_next_flush;
		std::string status;

		void serialize(ISerializer& s) {
			KV_MEMBER(current_merkle_root)
			KV_MEMBER(current_block_height)
			KV_MEMBER(total_registered_elderfiers)
			KV_MEMBER(elderfiers_signed)
			KV_MEMBER(consensus_percentage)
			KV_MEMBER(signed_by)
			KV_MEMBER(pending)
			KV_MEMBER(meets_69_percent)
			KV_MEMBER(ready_for_user_claim)
			KV_MEMBER(blocks_until_next_flush)
			KV_MEMBER(status)
		}
	};
};

struct COMMAND_RPC_GET_ELDERFIER_FEE_BALANCE {
	struct request {
		uint8_t elderfier_id;

		void serialize(ISerializer& s) {
			KV_MEMBER(elderfier_id)
		}
	};

	struct response {
		uint8_t elderfier_id;
		uint64_t accumulated_fees;
		uint64_t total_fees_earned;
		uint64_t number_of_rounds_signed;
		std::string status;

		void serialize(ISerializer& s) {
			KV_MEMBER(elderfier_id)
			KV_MEMBER(accumulated_fees)
			KV_MEMBER(total_fees_earned)
			KV_MEMBER(number_of_rounds_signed)
			KV_MEMBER(status)
		}
	};
};

struct COMMAND_RPC_GET_ELDERFIER_NETWORK_STATS {
	typedef EMPTY_STRUCT request;

	struct response {
		uint64_t total_fees_distributed_all_time;
		uint64_t total_fees_pending_in_escrow;
		size_t total_registered_elderfiers;
		uint64_t current_block_height;
		std::string status;

		void serialize(ISerializer& s) {
			KV_MEMBER(total_fees_distributed_all_time)
			KV_MEMBER(total_fees_pending_in_escrow)
			KV_MEMBER(total_registered_elderfiers)
			KV_MEMBER(current_block_height)
			KV_MEMBER(status)
		}
	};
};

// ============================================================================
// ELDERFIER ELIGIBILITY CHECK
// ============================================================================

struct COMMAND_RPC_CHECK_ELDERFIER_ELIGIBILITY {
	struct request {
		std::string address;

		void serialize(ISerializer& s) {
			KV_MEMBER(address)
		}
	};

	struct response {
		bool eligible;
		std::string reason;
		std::string status;

		void serialize(ISerializer& s) {
			KV_MEMBER(eligible)
			KV_MEMBER(reason)
			KV_MEMBER(status)
		}
	};
};

// Look up an EFier registration by signing pubkey (hex).
// Used by elder_council wallet command to identify a registered EFier.
struct COMMAND_RPC_GET_ELDERFIER_BY_PUBKEY {
  struct request {
    std::string signing_pubkey_hex;  // 64-hex Ed25519 public key

    void serialize(ISerializer& s) {
      KV_MEMBER(signing_pubkey_hex)
    }
  };

  struct response {
    bool found = false;
    uint8_t elderfier_id = 0;
    std::string ceremony_alias;
    std::string status;  // "active" | "unstaking" | "void"

    void serialize(ISerializer& s) {
      KV_MEMBER(found)
      KV_MEMBER(elderfier_id)
      KV_MEMBER(ceremony_alias)
      KV_MEMBER(status)
    }
  };
};

// ============================================================================
// @ ALIAS SYSTEM RPC ENDPOINTS
// ============================================================================

struct COMMAND_RPC_GET_ALIAS {
	struct request {
		std::string alias;

		void serialize(ISerializer& s) {
			KV_MEMBER(alias)
		}
	};

	struct response {
		std::string alias;
		std::string address;
		std::string address_hash;
		uint32_t registered_block;
		uint8_t alias_type;
		bool found;
		std::string status;

		void serialize(ISerializer& s) {
			KV_MEMBER(alias)
			KV_MEMBER(address)
			KV_MEMBER(address_hash)
			KV_MEMBER(registered_block)
			KV_MEMBER(alias_type)
			KV_MEMBER(found)
			KV_MEMBER(status)
		}
	};
};

struct COMMAND_RPC_GET_ALIAS_BY_ADDRESS {
	struct request {
		std::string address;

		void serialize(ISerializer& s) {
			KV_MEMBER(address)
		}
	};

	struct response {
		std::string alias;
		std::string address;
		uint32_t registered_block;
		uint8_t alias_type;
		bool found;
		std::string status;

		void serialize(ISerializer& s) {
			KV_MEMBER(alias)
			KV_MEMBER(address)
			KV_MEMBER(registered_block)
			KV_MEMBER(alias_type)
			KV_MEMBER(found)
			KV_MEMBER(status)
		}
	};
};

struct COMMAND_RPC_GET_ALL_ALIASES {
	typedef EMPTY_STRUCT request;

	struct alias_entry {
		std::string alias;
		std::string address;
		uint32_t registered_block;
		uint8_t alias_type;

		void serialize(ISerializer& s) {
			KV_MEMBER(alias)
			KV_MEMBER(address)
			KV_MEMBER(registered_block)
			KV_MEMBER(alias_type)
		}
	};

	struct response {
		std::vector<alias_entry> aliases;
		uint32_t total;
		std::string status;

		void serialize(ISerializer& s) {
			KV_MEMBER(aliases)
			KV_MEMBER(total)
			KV_MEMBER(status)
		}
	};
};

// ============================================================
// Commitment Index RPC endpoints (Fuego → EVM bridge support)
// Used by xfg-stark-cli to fetch commitment data + merkle proofs
// ============================================================

struct COMMAND_RPC_GET_COMMITMENT {
  struct request {
    std::string commitment_hash;  // Hex-encoded commitment hash (64 chars)

    void serialize(ISerializer& s) {
      KV_MEMBER(commitment_hash)
    }
  };

  struct response {
    bool found;
    std::string commitment_hash;
    std::string tx_hash;
    uint32_t block_height;
    uint64_t amount;
    uint32_t term;
    uint8_t type;               // 0=HEAT, 1=YIELD/COLD, 2=ELDERFIER_STAKING
    uint32_t target_chain_id;
    uint32_t leaf_index;
    bool is_legacy;         // true only for 0xCE migrations (original tx had MultisignatureOutput)
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(found)
      KV_MEMBER(commitment_hash)
      KV_MEMBER(tx_hash)
      KV_MEMBER(block_height)
      KV_MEMBER(amount)
      KV_MEMBER(term)
      KV_MEMBER(type)
      KV_MEMBER(target_chain_id)
      KV_MEMBER(leaf_index)
      KV_MEMBER(is_legacy)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_COMMITMENT_STATS {
  typedef EMPTY_STRUCT request;

  struct response {
    uint64_t total_commitments;
    uint64_t heat_commitments;
    uint64_t cold_commitments;
    uint32_t highest_block;
    std::string merkle_root;
    uint64_t consensus_percentage;
    std::vector<uint8_t> signed_elderfier_ids;
    std::vector<uint8_t> pending_elderfier_ids;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(total_commitments)
      KV_MEMBER(heat_commitments)
      KV_MEMBER(cold_commitments)
      KV_MEMBER(highest_block)
      KV_MEMBER(merkle_root)
      KV_MEMBER(consensus_percentage)
      KV_MEMBER(signed_elderfier_ids)
      KV_MEMBER(pending_elderfier_ids)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_COMMITMENT_MERKLE_ROOT {
  typedef EMPTY_STRUCT request;

  struct response {
    std::string merkle_root;    // Hex-encoded current merkle root
    uint64_t total_leaves;
    uint32_t highest_block;
    uint64_t consensus_percentage;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(merkle_root)
      KV_MEMBER(total_leaves)
      KV_MEMBER(highest_block)
      KV_MEMBER(consensus_percentage)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_COMMITMENT_MERKLE_PROOF {
  struct request {
    std::string commitment_hash;  // Hex-encoded commitment hash

    void serialize(ISerializer& s) {
      KV_MEMBER(commitment_hash)
    }
  };

  struct response {
    bool found;
    std::string merkle_root;              // Current root
    std::string leaf_hash;                // The commitment being proved
    std::vector<std::string> proof_path;  // Sibling hashes in hex
    std::vector<uint32_t> proof_indices;  // Left(0) or right(1) at each level
    uint32_t leaf_index;
    uint64_t consensus_percentage;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(found)
      KV_MEMBER(merkle_root)
      KV_MEMBER(leaf_hash)
      KV_MEMBER(proof_path)
      KV_MEMBER(proof_indices)
      KV_MEMBER(leaf_index)
      KV_MEMBER(consensus_percentage)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_CHECK_COMMITMENT_EXISTS {
  struct request {
    std::string commitment_hash;

    void serialize(ISerializer& s) {
      KV_MEMBER(commitment_hash)
    }
  };

  struct response {
    bool exists;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(exists)
      KV_MEMBER(status)
    }
  };
};

/** @brief Get per-epoch EFier activity report for elder_council review
  * Pass epoch=0 for the most recent completed epoch.
  */
struct COMMAND_RPC_GET_EPOCH_REPORT {
  struct request {
    uint64_t epoch = 0;  // 0 = latest completed epoch

    void serialize(ISerializer& s) {
      KV_MEMBER(epoch)
    }
  };

  struct EFierActivityRpc {
    uint8_t elderfier_id;
    std::string address;
    std::string ceremony_alias;
    bool signed_this_epoch;
    uint64_t signatures_submitted;
    uint64_t fees_earned;
    bool is_slashed;
    bool is_unstaking;
    uint32_t consecutive_missed_epochs;

    void serialize(ISerializer& s) {
      KV_MEMBER(elderfier_id)
      KV_MEMBER(address)
      KV_MEMBER(ceremony_alias)
      KV_MEMBER(signed_this_epoch)
      KV_MEMBER(signatures_submitted)
      KV_MEMBER(fees_earned)
      KV_MEMBER(is_slashed)
      KV_MEMBER(is_unstaking)
      KV_MEMBER(consecutive_missed_epochs)
    }
  };

  struct DoubleSignRpc {
    uint8_t elderfier_id;
    std::string root_a;
    std::string root_b;
    uint64_t block_height;
    uint64_t detected_at_block;

    void serialize(ISerializer& s) {
      KV_MEMBER(elderfier_id)
      KV_MEMBER(root_a)
      KV_MEMBER(root_b)
      KV_MEMBER(block_height)
      KV_MEMBER(detected_at_block)
    }
  };

  struct response {
    bool found;
    uint64_t epoch_number;
    uint64_t epoch_start_block;
    uint64_t epoch_end_block;
    uint64_t generated_at_block;
    uint64_t active_efer_count;
    uint64_t participating_efer_count;
    uint64_t total_fees_distributed;
    std::vector<uint8_t> signing_efier_ids;
    std::vector<uint8_t> missing_efier_ids;
    std::vector<EFierActivityRpc> efier_activity;
    std::vector<DoubleSignRpc> double_sign_events;
    std::vector<std::string> slash_advisory;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(found)
      KV_MEMBER(epoch_number)
      KV_MEMBER(epoch_start_block)
      KV_MEMBER(epoch_end_block)
      KV_MEMBER(generated_at_block)
      KV_MEMBER(active_efer_count)
      KV_MEMBER(participating_efer_count)
      KV_MEMBER(total_fees_distributed)
      KV_MEMBER(signing_efier_ids)
      KV_MEMBER(missing_efier_ids)
      KV_MEMBER(efier_activity)
      KV_MEMBER(double_sign_events)
      KV_MEMBER(slash_advisory)
      KV_MEMBER(status)
    }
  };
};

// ============================================================================
// SWAP ORDERBOOK RPC ENDPOINTS
// ============================================================================

struct swap_offer_rpc_entry {
  std::string offerId;
  uint64_t xfgAmount;
  uint64_t rateNum;
  uint8_t pair;
  std::string makerPubKey;    // hex
  uint64_t timestamp;
  uint32_t ttlBlocks;
  uint32_t postedHeight;

  void serialize(ISerializer& s) {
    KV_MEMBER(offerId)
    KV_MEMBER(xfgAmount)
    KV_MEMBER(rateNum)
    KV_MEMBER(pair)
    KV_MEMBER(makerPubKey)
    KV_MEMBER(timestamp)
    KV_MEMBER(ttlBlocks)
    KV_MEMBER(postedHeight)
  }
};

struct COMMAND_RPC_GET_SWAP_OFFERS {
  struct request {
    uint8_t pair;

    void serialize(ISerializer& s) {
      KV_MEMBER(pair)
    }
  };

  struct response {
    std::vector<swap_offer_rpc_entry> offers;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(offers)
      KV_MEMBER(status)
    }
  };
};

// Individual price source in composite breakdown
struct price_source_rpc_entry {
  std::string name;
  uint8_t     pair;
  std::string weight;
  std::string rate;
  uint64_t    updatedAt;
  bool        stale;

  void serialize(ISerializer& s) {
    KV_MEMBER(name)
    KV_MEMBER(pair)
    KV_MEMBER(weight)
    KV_MEMBER(rate)
    KV_MEMBER(updatedAt)
    KV_MEMBER(stale)
  }
};

// Per-pair implied USD price
struct pair_implied_rpc_entry {
  uint8_t     pair;
  std::string impliedUsd;

  void serialize(ISerializer& s) {
    KV_MEMBER(pair)
    KV_MEMBER(impliedUsd)
  }
};

struct COMMAND_RPC_GET_SWAP_PRICE {
  struct request {
    uint8_t pair;

    void serialize(ISerializer& s) {
      KV_MEMBER(pair)
    }
  };

  struct response {
    std::string twap;             // atomic swap TWAP (double as string)
    std::string seedRate;         // bootstrap seed rate
    std::string compositeRate;    // weighted avg across all sources
    uint32_t    sourceCount;      // how many sources contributed
    std::vector<price_source_rpc_entry> sources;  // source breakdown

    // Cross-pair native XFG price range (USD)
    std::string xfgUsdLow;
    std::string xfgUsdHigh;
    std::string xfgUsdMid;
    std::vector<pair_implied_rpc_entry> pairImplied;

    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(twap)
      KV_MEMBER(seedRate)
      KV_MEMBER(compositeRate)
      KV_MEMBER(sourceCount)
      KV_MEMBER(sources)
      KV_MEMBER(xfgUsdLow)
      KV_MEMBER(xfgUsdHigh)
      KV_MEMBER(xfgUsdMid)
      KV_MEMBER(pairImplied)
      KV_MEMBER(status)
    }
  };
};

struct swap_trade_rpc_entry {
  uint8_t pair;
  uint64_t xfgAmount;
  uint64_t ctrAmount;
  std::string rate;       // double as string
  uint32_t blockHeight;
  uint64_t timestamp;

  void serialize(ISerializer& s) {
    KV_MEMBER(pair)
    KV_MEMBER(xfgAmount)
    KV_MEMBER(ctrAmount)
    KV_MEMBER(rate)
    KV_MEMBER(blockHeight)
    KV_MEMBER(timestamp)
  }
};

struct COMMAND_RPC_GET_SWAP_TRADES {
  struct request {
    uint8_t pair;
    uint32_t limit;

    void serialize(ISerializer& s) {
      KV_MEMBER(pair)
      KV_MEMBER(limit)
    }
  };

  struct response {
    std::vector<swap_trade_rpc_entry> trades;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(trades)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_SUBMIT_SWAP_OFFER {
  struct request {
    std::string offerId;
    uint64_t xfgAmount;
    uint64_t rateNum;
    uint8_t pair;
    std::string makerPubKey;  // hex
    std::string signature;    // hex
    uint32_t ttlBlocks;

    void serialize(ISerializer& s) {
      KV_MEMBER(offerId)
      KV_MEMBER(xfgAmount)
      KV_MEMBER(rateNum)
      KV_MEMBER(pair)
      KV_MEMBER(makerPubKey)
      KV_MEMBER(signature)
      KV_MEMBER(ttlBlocks)
    }
  };

  struct response {
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_CANCEL_SWAP_OFFER {
  struct request {
    std::string offerId;
    std::string makerPubKey;  // hex
    std::string signature;    // hex

    void serialize(ISerializer& s) {
      KV_MEMBER(offerId)
      KV_MEMBER(makerPubKey)
      KV_MEMBER(signature)
    }
  };

  struct response {
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(status)
    }
  };
};

/** @brief Current fee pool state snapshot */
struct COMMAND_RPC_GET_FEE_POOL_INFO {
  typedef EMPTY_STRUCT request;

  struct response {
    uint64_t fee_pool_balance;
    uint64_t current_epoch_swap_fees;
    uint64_t total_cd_locked;
    uint64_t current_epoch_number;
    uint64_t active_efier_count;
    uint64_t efier_swap_reward_per_block;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(fee_pool_balance)
      KV_MEMBER(current_epoch_swap_fees)
      KV_MEMBER(total_cd_locked)
      KV_MEMBER(current_epoch_number)
      KV_MEMBER(active_efier_count)
      KV_MEMBER(efier_swap_reward_per_block)
      KV_MEMBER(status)
    }
  };
};

/** @brief List of past epoch summaries */
struct COMMAND_RPC_GET_EPOCH_HISTORY {
  struct request {
    uint32_t count = 10;

    void serialize(ISerializer& s) {
      KV_MEMBER(count)
    }
  };

  struct epoch_summary {
    uint64_t epoch_number;
    uint64_t swap_fees_collected;
    uint64_t total_cd_locked_at_start;
    uint64_t fee_rate_fixed_point;
    uint64_t total_fees_distributed;
    uint64_t active_efier_count;

    void serialize(ISerializer& s) {
      KV_MEMBER(epoch_number)
      KV_MEMBER(swap_fees_collected)
      KV_MEMBER(total_cd_locked_at_start)
      KV_MEMBER(fee_rate_fixed_point)
      KV_MEMBER(total_fees_distributed)
      KV_MEMBER(active_efier_count)
    }
  };

  struct response {
    std::vector<epoch_summary> epochs;
    uint64_t total_epochs;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(epochs)
      KV_MEMBER(total_epochs)
      KV_MEMBER(status)
    }
  };
};

/** @brief Estimate interest for a given CD */
struct COMMAND_RPC_ESTIMATE_CD_YIELD {
  struct request {
    uint64_t amount;
    uint32_t creation_height;
    uint32_t current_height = 0;

    void serialize(ISerializer& s) {
      KV_MEMBER(amount)
      KV_MEMBER(creation_height)
      KV_MEMBER(current_height)
    }
  };

  struct response {
    uint64_t estimated_interest;
    uint64_t effective_epochs;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(estimated_interest)
      KV_MEMBER(effective_epochs)
      KV_MEMBER(status)
    }
  };
};

/** @brief Treasury balance snapshot */
struct COMMAND_RPC_GET_TREASURY_INFO {
  typedef EMPTY_STRUCT request;

  struct response {
    uint64_t treasury_balance;
    std::string status;

    void serialize(ISerializer& s) {
      KV_MEMBER(treasury_balance)
      KV_MEMBER(status)
    }
  };
};

/** @brief List all persisted swaps (from SwapDaemon database) */
struct COMMAND_RPC_LIST_SWAPS {
  struct request { void serialize(ISerializer&) {} };
  struct response {
    struct swap_summary {
      std::string swap_id;
      std::string state;
      std::string pair;
      std::string role;
      uint64_t xfg_amount = 0;
      uint64_t created_at = 0;
      uint64_t updated_at = 0;
      bool is_terminal = false;
      void serialize(ISerializer& s) {
        KV_MEMBER(swap_id)
        KV_MEMBER(state)
        KV_MEMBER(pair)
        KV_MEMBER(role)
        KV_MEMBER(xfg_amount)
        KV_MEMBER(created_at)
        KV_MEMBER(updated_at)
        KV_MEMBER(is_terminal)
      }
    };
    std::vector<swap_summary> swaps;
    std::string status;
    void serialize(ISerializer& s) {
      KV_MEMBER(swaps)
      KV_MEMBER(status)
    }
  };
};

/** @brief Get status of a single persisted swap by swap_id */
struct COMMAND_RPC_GET_SWAP_STATUS {
  struct request {
    std::string swap_id;
    void serialize(ISerializer& s) { KV_MEMBER(swap_id) }
  };
  struct response {
    std::string swap_id;
    std::string state;
    std::string pair;
    std::string role;
    uint64_t xfg_amount = 0;
    std::string ctr_address;
    std::string peer_endpoint;
    uint64_t created_at = 0;
    uint64_t updated_at = 0;
    bool is_terminal = false;
    bool found = false;
    std::string status;
    void serialize(ISerializer& s) {
      KV_MEMBER(swap_id)
      KV_MEMBER(state)
      KV_MEMBER(pair)
      KV_MEMBER(role)
      KV_MEMBER(xfg_amount)
      KV_MEMBER(ctr_address)
      KV_MEMBER(peer_endpoint)
      KV_MEMBER(created_at)
      KV_MEMBER(updated_at)
      KV_MEMBER(is_terminal)
      KV_MEMBER(found)
      KV_MEMBER(status)
    }
  };
};

/** @brief Get total burned XFG amount (eternal flame)
  */
 struct COMMAND_RPC_GET_ETHERNAL_FLAME {
  typedef EMPTY_STRUCT request;

  struct response {
    uint64_t ethereal_xfg;
    std::string formattedAmount;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(ethereal_xfg)
      KV_MEMBER(formattedAmount)
      KV_MEMBER(status)
    }
  };
 };

}
