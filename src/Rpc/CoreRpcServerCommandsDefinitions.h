// Copyright (c) 2017-2025 Elderfire Privacy Council
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
#include <string>
#include <cstdint>
#include "CryptoNoteCore/TransactionExtra.h"
#include "Serialization/ISerializer.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolDefinitions.h"
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "CryptoNoteCore/Difficulty.h"
#include "crypto/hash.h"

#include "Serialization/SerializationOverloads.h"

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
  uint8_t  assetId = 0;

  void serialize(ISerializer &s) {
    KV_MEMBER(amounts)
    KV_MEMBER(outs_count)
    KV_MEMBER(assetId)
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

  void serialize(ISerializer &s) {
    KV_MEMBER(hash)
    KV_MEMBER(size)
    KV_MEMBER(paymentId)
    KV_MEMBER(mixin)
    KV_MEMBER(fee)
    KV_MEMBER(amount_out)
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
//    KV_MEMBER(BYTECOIN_NETWORK)
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

// DIGM Token RPC Commands
struct COMMAND_RPC_GET_DIGM_INFO {
  typedef EMPTY_STRUCT request;

  struct response {
    uint64_t token_id;
    std::string token_name;
    uint64_t total_supply;
    uint64_t circulating_supply;
    uint64_t amount_per_output;
    bool is_minted;
    uint32_t mint_height;
    std::string mint_transaction_hash;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(token_id)
      KV_MEMBER(token_name)
      KV_MEMBER(total_supply)
      KV_MEMBER(circulating_supply)
      KV_MEMBER(amount_per_output)
      KV_MEMBER(is_minted)
      KV_MEMBER(mint_height)
      KV_MEMBER(mint_transaction_hash)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_DIGM_BALANCE {
  struct request {
    std::string address;

    void serialize(ISerializer &s) {
      KV_MEMBER(address)
    }
  };

  struct response {
    uint64_t balance;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(balance)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_MINT_DIGM_TOKENS {
  struct request {
    uint64_t amount;
    std::string destination_address;

    void serialize(ISerializer &s) {
      KV_MEMBER(amount)
      KV_MEMBER(destination_address)
    }
  };

  struct response {
    std::string transaction_hash;
    std::string status;
    std::string status_message;

    void serialize(ISerializer &s) {
      KV_MEMBER(transaction_hash)
      KV_MEMBER(status)
      KV_MEMBER(status_message)
    }
  };
};

struct COMMAND_RPC_TRANSFER_DIGM_TOKENS {
  struct request {
    std::string source_address;
    std::string destination_address;
    uint64_t amount;
    uint64_t fee;

    void serialize(ISerializer &s) {
      KV_MEMBER(source_address)
      KV_MEMBER(destination_address)
      KV_MEMBER(amount)
      KV_MEMBER(fee)
    }
  };

  struct response {
    std::string transaction_hash;
    std::string status;
    std::string status_message;

    void serialize(ISerializer &s) {
      KV_MEMBER(transaction_hash)
      KV_MEMBER(status)
      KV_MEMBER(status_message)
    }
  };
};

struct COMMAND_RPC_RELEASE_ALBUM {
  struct request {
    std::string artist_address;
    uint64_t album_id;
    uint64_t price_atomic;
    std::string metadata_hash;
    std::vector<uint8_t> signature;

    void serialize(ISerializer &s) {
      KV_MEMBER(artist_address)
      KV_MEMBER(album_id)
      KV_MEMBER(price_atomic)
      KV_MEMBER(metadata_hash)
      KV_MEMBER(signature)
    }
  };

  struct response {
    std::string transaction_hash;
    std::string status;
    std::string status_message;

    void serialize(ISerializer &s) {
      KV_MEMBER(transaction_hash)
      KV_MEMBER(status)
      KV_MEMBER(status_message)
    }
  };
};

struct COMMAND_RPC_UPDATE_ALBUM {
  struct request {
    std::string artist_address;
    uint64_t album_id;
    uint64_t new_price_atomic;
    std::string new_metadata_hash;
    uint32_t update_reason;
    std::vector<uint8_t> signature;

    void serialize(ISerializer &s) {
      KV_MEMBER(artist_address)
      KV_MEMBER(album_id)
      KV_MEMBER(new_price_atomic)
      KV_MEMBER(new_metadata_hash)
      KV_MEMBER(update_reason)
      KV_MEMBER(signature)
    }
  };

  struct response {
    std::string transaction_hash;
    std::string status;
    std::string status_message;

    void serialize(ISerializer &s) {
      KV_MEMBER(transaction_hash)
      KV_MEMBER(status)
      KV_MEMBER(status_message)
    }
  };
};

struct COMMAND_RPC_GET_ALBUM_INFO {
  struct request {
    uint64_t album_id;

    void serialize(ISerializer &s) {
      KV_MEMBER(album_id)
    }
  };

  struct response {
    uint64_t album_id;
    uint64_t price_atomic;
    uint64_t timestamp;
    std::string artist_address;
    std::string metadata_hash;
    bool is_active;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(album_id)
      KV_MEMBER(price_atomic)
      KV_MEMBER(timestamp)
      KV_MEMBER(artist_address)
      KV_MEMBER(metadata_hash)
      KV_MEMBER(is_active)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_ARTIST_ALBUMS {
  struct request {
    std::string artist_address;

    void serialize(ISerializer &s) {
      KV_MEMBER(artist_address)
    }
  };

  struct response {
    std::vector<uint64_t> album_ids;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(album_ids)
      KV_MEMBER(status)
    }
  };
};

// HEAT metrics
struct COMMAND_RPC_GET_HEAT_METRICS {
  typedef EMPTY_STRUCT request;

  struct response {
    uint64_t heat_supply;
    uint64_t burned_xfg;
    uint64_t redemption_price_num;
    uint64_t redemption_price_denom;
    uint64_t redemption_rate_num;
    uint64_t redemption_rate_denom;
    uint64_t treasury_balance;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(heat_supply)
      KV_MEMBER(burned_xfg)
      KV_MEMBER(redemption_price_num)
      KV_MEMBER(redemption_price_denom)
      KV_MEMBER(redemption_rate_num)
      KV_MEMBER(redemption_rate_denom)
      KV_MEMBER(treasury_balance)
      KV_MEMBER(status)
    }
  };
};

// AMM pool quote
struct COMMAND_RPC_AMM_QUOTE {
  struct request {
    uint64_t input_amount;
    uint8_t  direction; // 0 = XFG→HEAT, 1 = HEAT→XFG

    void serialize(ISerializer &s) {
      KV_MEMBER(input_amount)
      KV_MEMBER(direction)
    }
  };

  struct response {
    uint64_t expected_output;
    uint64_t price_impact_bps;
    uint64_t fee;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(expected_output)
      KV_MEMBER(price_impact_bps)
      KV_MEMBER(fee)
      KV_MEMBER(status)
    }
  };
};

// AMM pool info
struct COMMAND_RPC_AMM_POOL_INFO {
  typedef EMPTY_STRUCT request;

  struct response {
    uint64_t reserve_xfg;
    uint64_t reserve_heat;
    uint64_t total_lp_shares;
    uint64_t spot_price;   // price * 1e18
    uint64_t fee_accumulator;
    uint64_t epoch_swap_fees;
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(reserve_xfg)
      KV_MEMBER(reserve_heat)
      KV_MEMBER(total_lp_shares)
      KV_MEMBER(spot_price)
      KV_MEMBER(fee_accumulator)
      KV_MEMBER(epoch_swap_fees)
      KV_MEMBER(status)
    }
  };
};

}
