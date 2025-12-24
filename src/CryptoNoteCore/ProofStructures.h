// Copyright (c) 2017-2025 Fuego Developers
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

#include <json/json.h>
#include "crypto/crypto.h"
#include "Serialization/ISerializer.h"

namespace CryptoNote {

// Burn proof structure for burn transactions
struct TransactionExtraBurnProof {
  Crypto::PublicKey proof_pubkey;
  std::vector<uint8_t> encrypted_data;
  std::array<uint8_t, 12> nonce;
  uint64_t timestamp;
  std::string proof_type;

  bool serialize(CryptoNote::ISerializer& serializer) const;

  // Additional fields for serialize/deserialize functions
  std::string tx_hash;
  std::string address;
};

// Deposit proof structure for time-locked deposits
struct TransactionExtraDepositProof {
  Crypto::PublicKey proof_pubkey;
  std::vector<uint8_t> encrypted_data;
  std::array<uint8_t, 12> nonce;
  uint64_t timestamp;
  std::string proof_type;

  bool serialize(CryptoNote::ISerializer& serializer) const;

  // Additional fields for serialize/deserialize functions
  std::string tx_hash;
  std::string address;
};

// Data structure for proof verification
struct ProofVerificationData {
  uint64_t amount;
  std::string recipient;
  std::string address;
  uint64_t timestamp;
  std::string commitment;
  std::string nullifier;
  std::string tx_hash;
  std::string proof_type;

  void serialize(Json::Value& json) const;
  void deserialize(const Json::Value& json);

  bool serialize(CryptoNote::ISerializer& serializer) const;
};

  // Burn receipt structure
  struct TransactionExtraBurnReceipt {
    Crypto::PublicKey proof_pubkey;
    std::string tx_hash;
    uint64_t timestamp;

    bool serialize(CryptoNote::ISerializer& serializer) const;
  };

  // Deposit receipt structure
  struct TransactionExtraDepositReceipt {
    Crypto::PublicKey proof_pubkey;
    std::string tx_hash;
    uint64_t timestamp;
    uint32_t term_months;
    std::string deposit_type;

    bool serialize(CryptoNote::ISerializer& serializer) const;
  };

}
