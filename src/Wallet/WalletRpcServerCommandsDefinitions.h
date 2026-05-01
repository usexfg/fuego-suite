// Copyright (c) 2017-2022 Fuego Developers
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
#include "CryptoNoteProtocol/CryptoNoteProtocolDefinitions.h"
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "crypto/hash.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"
#include "WalletRpcServerErrorCodes.h"

namespace Tools
{
namespace wallet_rpc
{

using CryptoNote::ISerializer;

#define WALLET_RPC_STATUS_OK      "OK"
#define WALLET_RPC_STATUS_BUSY    "BUSY"

  struct COMMAND_RPC_GET_BALANCE
  {
    typedef CryptoNote::EMPTY_STRUCT request;

    struct response
    {
      uint64_t locked_amount;
      uint64_t available_balance;
      uint64_t balance;            //<! \deprecated Use locked_amount + available_balance
      uint64_t unlocked_balance;   //<! \deprecated Use available_balance

      void serialize(ISerializer& s) {
        KV_MEMBER(locked_amount)
        KV_MEMBER(available_balance)
        KV_MEMBER(balance)
        KV_MEMBER(unlocked_balance)
      }
    };
  };

  struct COMMAND_RPC_GET_ADDRESS {
    typedef CryptoNote::EMPTY_STRUCT request;

    struct response {
      std::string address;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(address)
        KV_MEMBER(status)
      }
    };
  };

  struct transfer_destination
  {
    uint64_t amount;
    std::string address;
    std::string message;

    void serialize(ISerializer& s) {
      KV_MEMBER(amount)
      KV_MEMBER(address)
      KV_MEMBER(message)
    }
  };

  struct TransferMessage {
    std::string address;
    std::string message;

    void serialize(ISerializer& s) {
      KV_MEMBER(address)
      KV_MEMBER(message)
    }
  };

  struct COMMAND_RPC_TRANSFER
  {
    struct request
    {
      std::list<transfer_destination> destinations;
      uint64_t fee;
      uint64_t mixin;
      uint64_t unlock_time = 0;
      std::string payment_id;
      std::list<TransferMessage> messages;
      uint64_t ttl = 0;

      void serialize(ISerializer& s) {
        KV_MEMBER(destinations)
        KV_MEMBER(fee)
        KV_MEMBER(mixin)
        KV_MEMBER(unlock_time)
        KV_MEMBER(payment_id)
        KV_MEMBER(messages)
        KV_MEMBER(ttl)
      }
    };

    struct response
    {
      std::string tx_hash;
      std::string tx_secret_key;

      void serialize(ISerializer& s) {
        KV_MEMBER(tx_hash)
        KV_MEMBER(tx_secret_key)
      }
    };
  };

  struct COMMAND_RPC_STORE
  {
    typedef CryptoNote::EMPTY_STRUCT request;
    typedef CryptoNote::EMPTY_STRUCT response;
  };

  struct transaction_messages {
    std::string tx_hash;
    uint64_t tx_id;
    uint32_t block_height;
    uint64_t timestamp;
    std::list<std::string> messages;

    void serialize(ISerializer& s) {
      KV_MEMBER(tx_hash);
      KV_MEMBER(tx_id);
      KV_MEMBER(block_height);
      KV_MEMBER(timestamp);
      KV_MEMBER(messages);
    }
  };

  struct COMMAND_RPC_CREATE_INTEGRATED
    {
      struct request
      {
        std::string payment_id;
        std::string address;      

        void serialize(ISerializer& s) {
          KV_MEMBER(payment_id)
          KV_MEMBER(address)         
        }
      };

      struct response
      {
        std::string integrated_address;

        void serialize(ISerializer& s) 
        {

          KV_MEMBER(integrated_address)
        }
    };
  };

  struct COMMAND_RPC_GET_MESSAGES {
    struct request {
      uint64_t first_tx_id = 0;
      uint32_t tx_limit = std::numeric_limits<uint32_t>::max();

      void serialize(ISerializer& s) {
        KV_MEMBER(first_tx_id);
        KV_MEMBER(tx_limit);
      }
    };

    struct response {
      uint64_t total_tx_count;
      std::list<transaction_messages> tx_messages;

      void serialize(ISerializer& s) {
        KV_MEMBER(total_tx_count);
        KV_MEMBER(tx_messages);
      }
    };
  };

  struct payment_details
  {
    std::string tx_hash;
    uint64_t amount;
    uint64_t block_height;
    uint64_t unlock_time;

    void serialize(ISerializer& s) {
      KV_MEMBER(tx_hash)
      KV_MEMBER(amount)
      KV_MEMBER(block_height)
      KV_MEMBER(unlock_time)
    }
  };

  struct COMMAND_RPC_GET_PAYMENTS
  {
    struct request
    {
      std::string payment_id;

      void serialize(ISerializer& s) {
        KV_MEMBER(payment_id)
      }
    };

    struct response
    {
      std::list<payment_details> payments;

      void serialize(ISerializer& s) {
        KV_MEMBER(payments)
      }
    };
  };

  struct Transfer {
    uint64_t time;
    bool output;
    std::string transactionHash;
    uint64_t amount;
    uint64_t fee;
    std::string paymentId;
    std::string address;
    uint64_t blockIndex;
    uint64_t unlockTime;

    void serialize(ISerializer& s) {
      KV_MEMBER(time)
      KV_MEMBER(output)
      KV_MEMBER(transactionHash)
      KV_MEMBER(amount)
      KV_MEMBER(fee)
      KV_MEMBER(paymentId)
      KV_MEMBER(address)
      KV_MEMBER(blockIndex)
      KV_MEMBER(unlockTime)
    }
  };

  struct COMMAND_RPC_GET_TRANSFERS {
    typedef CryptoNote::EMPTY_STRUCT request;

    struct response {
      std::list<Transfer> transfers;

      void serialize(ISerializer& s) {
        KV_MEMBER(transfers)
      }
    };
  };

  struct COMMAND_RPC_GET_HEIGHT {
    typedef CryptoNote::EMPTY_STRUCT request;

    struct response {
      uint64_t height;

      void serialize(ISerializer& s) {
        KV_MEMBER(height)
      }
    };
  };

  struct COMMAND_RPC_GET_OUTPUTS
  {
    typedef CryptoNote::EMPTY_STRUCT request;

    struct response
    {
      size_t num_unlocked_outputs;

      void serialize(ISerializer& s) {
        KV_MEMBER(num_unlocked_outputs)
      }
    };
  };

  struct COMMAND_RPC_OPTIMIZE
  {
    typedef CryptoNote::EMPTY_STRUCT request;

    struct response
    {
      std::string tx_hash;
      std::string tx_secret_key;

      void serialize(ISerializer& s) {
        KV_MEMBER(tx_hash)
        KV_MEMBER(tx_secret_key)
      }
    };
  };

  struct COMMAND_RPC_ESTIMATE_FUSION
  {
    struct request
    {
      uint64_t threshold;

      void serialize(ISerializer& s)
      {
        KV_MEMBER(threshold)
      }
    };

    struct response
    {
      size_t fusion_ready_count;

      void serialize(ISerializer& s) {
        KV_MEMBER(fusion_ready_count)
      }
    };
  };

  struct COMMAND_RPC_RESET {
    typedef CryptoNote::EMPTY_STRUCT request;
    typedef CryptoNote::EMPTY_STRUCT response;
  };

	struct COMMAND_RPC_GET_TX_PROOF
	{
		struct request
		{
			std::string tx_hash;
			std::string dest_address;
			std::string tx_key;

			void serialize(ISerializer& s)
			{
				KV_MEMBER(tx_hash);
				KV_MEMBER(dest_address);
				KV_MEMBER(tx_key);
			}
		};

		struct response
		{
			std::string signature;

			void serialize(ISerializer& s)
			{
				KV_MEMBER(signature);
			}
		};
	};

	struct COMMAND_RPC_GET_BALANCE_PROOF
	{
		struct request
		{
			uint64_t amount = 0;
			std::string message;

			void serialize(ISerializer& s)
			{
				KV_MEMBER(amount);
				KV_MEMBER(message);
			}
		};

		struct response
		{
			std::string signature;

			void serialize(ISerializer& s)
			{
				KV_MEMBER(signature);
			}
		};
	};

  struct COMMAND_RPC_SEND_FUSION
  {
    struct request
    {
      uint64_t mixin = 0;
      uint64_t threshold;
      uint64_t unlock_time = 0;

      void serialize(ISerializer& s)
      {
        KV_MEMBER(mixin)
        KV_MEMBER(threshold)
        KV_MEMBER(unlock_time)
      }
    };
    struct response
    {
      std::string tx_hash;

      void serialize(ISerializer& s)
      {
        KV_MEMBER(tx_hash)
      }
    };
  };

  // Initiate an adaptor-signature atomic swap (Musig2 escrow).
  // Generates keys, aggregates Musig2 key, generates nonces.
  // Returns our pubkey, nonces, and (if role=bob) adaptor point for sharing with counterparty.
  struct COMMAND_RPC_INITIATE_SWAP {
    struct request {
      uint64_t    xfgAmount;
      std::string peerPubKey;   // 64-char hex Ed25519 pubkey of counterparty
      std::string pair;         // "SOL", "ETH", "XMR", "BCH"
      std::string role;         // "alice" or "bob" (default: alice)

      void serialize(ISerializer& s) {
        KV_MEMBER(xfgAmount)
        KV_MEMBER(peerPubKey)
        KV_MEMBER(pair)
        KV_MEMBER(role)
      }
    };

    struct response {
      std::string swapId;
      std::string ourPubKey;
      std::string nonce0;
      std::string nonce1;
      std::string escrowKey;    // Musig2 aggregated pubkey (escrow address)
      std::string adaptorPoint; // only set when role=bob
      std::string dleqChallenge;
      std::string dleqResponse;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(swapId)
        KV_MEMBER(ourPubKey)
        KV_MEMBER(nonce0)
        KV_MEMBER(nonce1)
        KV_MEMBER(escrowKey)
        KV_MEMBER(adaptorPoint)
        KV_MEMBER(dleqChallenge)
        KV_MEMBER(dleqResponse)
        KV_MEMBER(status)
      }
    };
  };

  // Complete a swap after receiving the counterparty's adaptor secret.
  struct COMMAND_RPC_COMPLETE_SWAP {
    struct request {
      std::string swapId;
      std::string adaptorSecret;    // 64-char hex adaptor secret t
      std::string peerPartialSig;   // 64-char hex peer's partial Musig2 sig
      std::string txPrefixHash;     // 64-char hex hash of the escrow tx prefix

      void serialize(ISerializer& s) {
        KV_MEMBER(swapId)
        KV_MEMBER(adaptorSecret)
        KV_MEMBER(peerPartialSig)
        KV_MEMBER(txPrefixHash)
      }
    };

    struct response {
      std::string txHash;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(txHash)
        KV_MEMBER(status)
      }
    };
  };

  // Cooperatively refund a swap (both parties agree to unwind).
  struct COMMAND_RPC_REFUND_SWAP {
    struct request {
      std::string swapId;
      std::string peerPartialSig;   // peer's partial sig over refund tx
      std::string txPrefixHash;

      void serialize(ISerializer& s) {
        KV_MEMBER(swapId)
        KV_MEMBER(peerPartialSig)
        KV_MEMBER(txPrefixHash)
      }
    };

    struct response {
      std::string txHash;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(txHash)
        KV_MEMBER(status)
      }
    };
  };

  // Sign a swap offer payload with the wallet's spend key.
  // offerId is the hex-encoded offer ID (SHA-256 of offer fields).
  // Returns the maker's public key and signature for submission to the network.
  struct COMMAND_RPC_SIGN_OFFER {
    struct request {
      uint64_t    xfgAmount;
      uint64_t    rateNum;
      uint8_t     pair;
      uint32_t    ttlBlocks;
      bool        isSell;

      void serialize(ISerializer& s) {
        KV_MEMBER(xfgAmount)
        KV_MEMBER(rateNum)
        KV_MEMBER(pair)
        KV_MEMBER(ttlBlocks)
        KV_MEMBER(isSell)
      }
    };

    struct response {
      std::string offerId;       // hex-encoded offer ID
      std::string makerPubKey;   // hex-encoded spend public key
      std::string signature;     // hex-encoded signature over offerId hash
      uint64_t    timestamp;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(offerId)
        KV_MEMBER(makerPubKey)
        KV_MEMBER(signature)
        KV_MEMBER(timestamp)
        KV_MEMBER(status)
      }
    };
  };

  // ── Phase 7: CD / COLD wallet RPC bridges ─────────────────────────────────

  struct COMMAND_RPC_CREATE_AFK_LOCK {
    struct request {
      uint64_t amount;
      uint32_t timeout_hours;
      uint8_t pair;

      void serialize(ISerializer& s) {
        KV_MEMBER(amount)
        KV_MEMBER(timeout_hours)
        KV_MEMBER(pair)
      }
    };

    struct response {
      std::string lockId;
      std::string adaptorPoint;
      std::string preSig;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(lockId)
        KV_MEMBER(adaptorPoint)
        KV_MEMBER(preSig)
        KV_MEMBER(status)
      }
    };
  };

  struct COMMAND_RPC_CLAIM_AFK_SWAP {
    struct request {
      std::string swapId;
      std::string secret_s;
      std::string target_chain;

      void serialize(ISerializer& s) {
        KV_MEMBER(swapId)
        KV_MEMBER(secret_s)
        KV_MEMBER(target_chain)
      }
    };

    struct response {
      std::string txHash;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(txHash)
        KV_MEMBER(status)
      }
    };
  };

  struct COMMAND_RPC_LIST_CDS {
    typedef CryptoNote::EMPTY_STRUCT request;

    struct deposit_entry {
      uint64_t deposit_id;
      uint64_t amount;
      uint32_t term;
      uint64_t unlock_height;
      uint64_t creation_height;
      std::string deposit_type;  // "COLD" or "HEAT"
      bool locked;

      void serialize(ISerializer& s) {
        KV_MEMBER(deposit_id)
        KV_MEMBER(amount)
        KV_MEMBER(term)
        KV_MEMBER(unlock_height)
        KV_MEMBER(creation_height)
        KV_MEMBER(deposit_type)
        KV_MEMBER(locked)
      }
    };

    struct response {
      std::vector<deposit_entry> deposits;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(deposits)
        KV_MEMBER(status)
      }
    };
  };

  struct COMMAND_RPC_CREATE_CD {
    struct request {
      uint64_t amount;
      uint32_t term;
      uint32_t deposit_type;  // HEAT=0x08, xCD=0xCD, YIELD=0x07; validated in handler

      void serialize(ISerializer& s) {
        KV_MEMBER(amount)
        KV_MEMBER(term)
        KV_MEMBER(deposit_type)
      }
    };

    struct response {
      std::string tx_hash;
      uint64_t deposit_id;
      uint64_t unlock_height;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(tx_hash)
        KV_MEMBER(deposit_id)
        KV_MEMBER(unlock_height)
        KV_MEMBER(status)
      }
    };
  };

  struct COMMAND_RPC_WITHDRAW_CD {
    struct request {
      uint64_t deposit_id;

      void serialize(ISerializer& s) {
        KV_MEMBER(deposit_id)
      }
    };

    struct response {
      std::string tx_hash;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(tx_hash)
        KV_MEMBER(status)
      }
    };
  };

  struct COMMAND_RPC_ROLLOVER_CD {
    struct request {
      uint64_t deposit_id;
      uint32_t new_term;  // 0 = same term as original

      void serialize(ISerializer& s) {
        KV_MEMBER(deposit_id)
        KV_MEMBER(new_term)
      }
    };

    struct response {
      std::string tx_hash;
      uint64_t new_amount;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(tx_hash)
        KV_MEMBER(new_amount)
        KV_MEMBER(status)
      }
    };
  };

  struct COMMAND_RPC_ESTIMATE_CD_YIELD {
    struct request {
      uint64_t amount;
      uint32_t creation_height;
      uint32_t current_height;  // 0 = use current blockchain height

      void serialize(ISerializer& s) {
        KV_MEMBER(amount)
        KV_MEMBER(creation_height)
        KV_MEMBER(current_height)
      }
    };

    struct response {
      uint64_t estimated_interest;
      uint32_t effective_epochs;
      std::string status;

      void serialize(ISerializer& s) {
        KV_MEMBER(estimated_interest)
        KV_MEMBER(effective_epochs)
        KV_MEMBER(status)
      }
    };
  };

}
}
