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
#include "../Common/JsonValue.h"
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
// Sidecar lookup: get block heights for a list of (amount, global_index) outputs.
// Used by wallets to enrich OSPEAD-aware decoy selection without breaking the
// packed binary wire format of COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS.
// Queries are sent as two parallel vectors (amounts[i], global_indices[i]);
// heights[i] corresponds to that pair. Height 0 means unknown/invalid.
struct COMMAND_RPC_GET_OUTPUTS_HEIGHTS {
  struct request {
    std::vector<uint64_t> amounts;
    std::vector<uint32_t> global_indices;  // must be amounts.size()-aligned

    void serialize(ISerializer &s) {
      KV_MEMBER(amounts)
      KV_MEMBER(global_indices)
    }
  };

  struct response {
    std::vector<uint32_t> heights;  // 1:1 with request.amounts; 0 means unknown/invalid
    std::string status;

    void serialize(ISerializer &s) {
      KV_MEMBER(heights)
      KV_MEMBER(status)
    }
  };
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
    uint32_t max_height = 0;

    void serialize(ISerializer& s) {
      KV_MEMBER(amount)
      KV_MEMBER(outs_count)
      KV_MEMBER(max_height)
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
}
