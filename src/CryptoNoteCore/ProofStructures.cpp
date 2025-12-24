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

#include "ProofStructures.h"
#include "Serialization/ISerializer.h"
#include "Serialization/SerializationOverloads.h"
#include <jsoncpp/json.h>

namespace CryptoNote {

bool TransactionExtraBurnProof::serialize(CryptoNote::ISerializer& serializer) const {
  auto& mutable_self = const_cast<TransactionExtraBurnProof&>(*this);
  serializer.binary(&mutable_self.proof_pubkey, sizeof(mutable_self.proof_pubkey), "proof_pubkey");
  serializeAsBinary(mutable_self.encrypted_data, "encrypted_data", serializer);
  serializer.binary(mutable_self.nonce.data(), mutable_self.nonce.size(), "nonce");
  serializer(mutable_self.timestamp, "timestamp");
  serializer(mutable_self.proof_type, "proof_type");
  serializer(mutable_self.tx_hash, "tx_hash");
  serializer(mutable_self.address, "address");
  return true;
}

bool TransactionExtraDepositProof::serialize(CryptoNote::ISerializer& serializer) const {
  auto& mutable_self = const_cast<TransactionExtraDepositProof&>(*this);
  serializer.binary(&mutable_self.proof_pubkey, sizeof(mutable_self.proof_pubkey), "proof_pubkey");
  serializeAsBinary(mutable_self.encrypted_data, "encrypted_data", serializer);
  serializer.binary(mutable_self.nonce.data(), mutable_self.nonce.size(), "nonce");
  serializer(mutable_self.timestamp, "timestamp");
  serializer(mutable_self.proof_type, "proof_type");
  serializer(mutable_self.tx_hash, "tx_hash");
  serializer(mutable_self.address, "address");
  return true;
}

void ProofVerificationData::serialize(Json::Value& json) const {
  json["amount"] = amount;
  json["recipient"] = recipient;
  json["address"] = address;
  json["timestamp"] = timestamp;
  json["commitment"] = commitment;
  json["nullifier"] = nullifier;
  json["tx_hash"] = tx_hash;
  json["proof_type"] = proof_type;
}

void ProofVerificationData::deserialize(const Json::Value& json) {
  if (json.isMember("amount")) amount = json["amount"].asUInt64();
  if (json.isMember("recipient")) recipient = json["recipient"].asString();
  if (json.isMember("address")) address = json["address"].asString();
  if (json.isMember("timestamp")) timestamp = json["timestamp"].asUInt64();
  if (json.isMember("commitment")) commitment = json["commitment"].asString();
  if (json.isMember("nullifier")) nullifier = json["nullifier"].asString();
  if (json.isMember("tx_hash")) tx_hash = json["tx_hash"].asString();
  if (json.isMember("proof_type")) proof_type = json["proof_type"].asString();
}

bool ProofVerificationData::serialize(CryptoNote::ISerializer& serializer) const {
  auto& mutable_self = const_cast<ProofVerificationData&>(*this);
  serializer(mutable_self.amount, "amount");
  serializer(mutable_self.recipient, "recipient");
  serializer(mutable_self.address, "address");
  serializer(mutable_self.timestamp, "timestamp");
  serializer(mutable_self.commitment, "commitment");
  serializer(mutable_self.nullifier, "nullifier");
  serializer(mutable_self.tx_hash, "tx_hash");
  serializer(mutable_self.proof_type, "proof_type");
  return true;
}

// Burn receipt structure
bool TransactionExtraBurnReceipt::serialize(CryptoNote::ISerializer& serializer) const {
  auto& mutable_self = const_cast<TransactionExtraBurnReceipt&>(*this);
  serializer.binary(&mutable_self.proof_pubkey, sizeof(mutable_self.proof_pubkey), "proof_pubkey");
  serializer(mutable_self.tx_hash, "tx_hash");
  serializer(mutable_self.timestamp, "timestamp");
  return true;
}

// Deposit receipt structure
bool TransactionExtraDepositReceipt::serialize(CryptoNote::ISerializer& serializer) const {
  auto& mutable_self = const_cast<TransactionExtraDepositReceipt&>(*this);
  serializer.binary(&mutable_self.proof_pubkey, sizeof(mutable_self.proof_pubkey), "proof_pubkey");
  serializer(mutable_self.tx_hash, "tx_hash");
  serializer(mutable_self.timestamp, "timestamp");
  serializer(mutable_self.term_months, "term_months");
  serializer(mutable_self.deposit_type, "deposit_type");
  return true;
}

} // namespace CryptoNote
