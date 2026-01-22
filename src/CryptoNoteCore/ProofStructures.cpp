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
#include "Common/JsonValue.h"

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

void ProofVerificationData::serialize(Common::JsonValue& json) const {
  json("amount") = Common::JsonValue(static_cast<int64_t>(amount));
  json("recipient") = Common::JsonValue(recipient);
  json("address") = Common::JsonValue(address);
  json("timestamp") = Common::JsonValue(static_cast<int64_t>(timestamp));
  json("commitment") = Common::JsonValue(commitment);
  json("nullifier") = Common::JsonValue(nullifier);
  json("tx_hash") = Common::JsonValue(tx_hash);
  json("proof_type") = Common::JsonValue(proof_type);
}

void ProofVerificationData::deserialize(const Common::JsonValue& json) {
  if (json.contains("amount")) amount = static_cast<uint64_t>(json("amount").getInteger());
  if (json.contains("recipient")) recipient = json("recipient").getString();
  if (json.contains("address")) address = json("address").getString();
  if (json.contains("timestamp")) timestamp = static_cast<uint64_t>(json("timestamp").getInteger());
  if (json.contains("commitment")) commitment = json("commitment").getString();
  if (json.contains("nullifier")) nullifier = json("nullifier").getString();
  if (json.contains("tx_hash")) tx_hash = json("tx_hash").getString();
  if (json.contains("proof_type")) proof_type = json("proof_type").getString();
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
  serializer(mutable_self.term, "term");
  serializer(mutable_self.deposit_type, "deposit_type");
  return true;
}

} // namespace CryptoNote
