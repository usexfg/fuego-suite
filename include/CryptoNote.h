// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2020-2025 Elderfire Privacy Group
// Copyright (c) 2011-2017 The Cryptonote developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful- but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You are encouraged to redistribute it and/or modify it
// under the terms of the GNU General Public License v3 or later
// versions as published by the Free Software Foundation.
// You should receive a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>

#pragma once

#include <vector>
#include <boost/variant.hpp>
#include "CryptoTypes.h"

namespace CryptoNote {

struct BaseInput {
  uint32_t blockIndex;
};

struct KeyInput {
  uint64_t amount;
  std::vector<uint32_t> outputIndexes;
  Crypto::KeyImage keyImage;
};

struct MultisignatureInput {
  uint64_t amount;
  uint8_t signatureCount;
  uint32_t outputIndex;
  uint32_t term;
};

struct KeyOutput {
  Crypto::PublicKey key;
};

struct MultisignatureOutput {
  std::vector<Crypto::PublicKey> keys;
  uint8_t requiredSignatureCount;
  uint32_t term;
};

// v10+ ring-signature deposit output.
// Replaces MultisignatureOutput for ALL deposit types: COLD, HEAT burns, Elderfier stakes.
// HEAT burns use throwaway commitKey (secret discarded) and never withdraw but
// serve as excellent decoys, bulking up decoy pool for COLD/EF withdrawal rings.
// Ring selection draws from the global CommitmentIndex by amount —
// all commitment outputs of matching amount are eligible decoys regardless of term.
//
// Amount privacy (v10):
//   amountCommitment — Pedersen C = amount*H + amountMask*G (hides amount)
//   amountProof      — 1-of-4 OR proof: amount ∈ {TIER_0, TIER_1, TIER_2, TIER_3}
//
// Term remains visible as required for node-enforced timelock
struct TransactionOutputCommitment {
  Crypto::PublicKey commitKey;                  // ring-sig spend key
  uint32_t term;                                // lock term in blocks (visible — enforces timelock)
  Crypto::EllipticCurvePoint amountCommitment; // C = amount*H + amountMask*G
  Crypto::MembershipProof    amountProof;       // proves amount ∈ {TIER_0..TIER_3}
};

// v10+ ring-signature withdrawal input.
// Replaces MultisignatureInput for COLD/Elderfier withdrawals.
// outputIndexes are GLOBAL commitment output indices (like KeyInput for key outputs).
struct TransactionInputCommitmentSpend {
  uint64_t amount;                      // must match referenced commitment output amount
  std::vector<uint32_t> outputIndexes;  // ring: global commitment output indices (relative offsets, decoded absolute on verify)
  Crypto::KeyImage keyImage;            // H_p(commitKey) * keyScalar — double-spend prevention via m_spent_keys
  uint64_t claimedInterest = 0;         // declared interest from fee pool (0 for pre-activation)
};

// v11+ commitment deposit transfer — transfers CD ownership without redeeming.
// Ring signature proves spend authority, key image prevents double-transfer.
// Does NOT require maturity — the CD stays locked with remaining term.
// Must produce exactly one TransactionOutputCommitment at the same amount.
struct TransactionInputCommitmentTransfer {
  uint64_t amount;                      // must match referenced CD amount
  std::vector<uint32_t> outputIndexes;  // ring: global commitment output indices (relative offsets)
  Crypto::KeyImage keyImage;            // H_p(commitKey) * keyScalar
  uint32_t newTerm;                     // new CD's term (spender-declared, >= 1)
};

// v11+ unified output — replaces KeyOutput + TransactionOutputCommitment.
// ALL v11 transaction outputs (transfers, deposits, burns) use this type.
// Amount hidden in Pedersen commitment; denomination proved by 1-of-N membership proof.
// term=0: regular transfer. term>0: locked deposit (blocks).
// TransactionOutput.amount is 0 on the wire for this type (amount is in commitment).
struct TransactionOutputUnified {
  Crypto::PublicKey key;                    // stealth address (regular) or commitKey (deposit)
  uint32_t term;                            // 0 = regular transfer, >0 = deposit lock (blocks)
  Crypto::EllipticCurvePoint commitment;    // C = amount*H + mask*G
  Crypto::MembershipProof proof;            // 1-of-N: amount is a valid denomination
};

// v11+ unified input — replaces KeyInput + TransactionInputCommitmentSpend.
// ALL v11 inputs (transfers, deposit withdrawals) use this type.
// Amount hidden; MLSAG proves spend authority + commitment balance.
// MLSAG response scalars stored in tx.signatures[input_idx]:
//   signatures[input_idx][j] = {s[j][0], s[j][1]} for ring member j
//   (each 64-byte Signature packs both layer responses for one ring member)
struct TransactionInputUnified {
  std::vector<uint32_t> outputIndexes;              // global ring (relative offsets)
  Crypto::KeyImage keyImage;                         // double-spend prevention
  Crypto::EllipticCurvePoint pseudoCommitment;       // C_pseudo for MLSAG balance
  Crypto::EllipticCurveScalar sigC0;                 // MLSAG initial challenge scalar
};

typedef boost::variant<BaseInput, KeyInput, MultisignatureInput, TransactionInputCommitmentSpend, TransactionInputCommitmentTransfer, TransactionInputUnified> TransactionInput;

typedef boost::variant<KeyOutput, MultisignatureOutput, TransactionOutputCommitment, TransactionOutputUnified> TransactionOutputTarget;

struct TransactionOutput {
  uint64_t amount;
  TransactionOutputTarget target;
};

using TransactionInputs = std::vector<TransactionInput>;

struct TransactionPrefix {
  uint8_t version;
  uint64_t unlockTime;
  TransactionInputs inputs;
  std::vector<TransactionOutput> outputs;
  std::vector<uint8_t> extra;
};

struct Transaction : public TransactionPrefix {
  std::vector<std::vector<Crypto::Signature>> signatures;
};

struct ParentBlock {
  uint8_t majorVersion;
  uint8_t minorVersion;
  Crypto::Hash previousBlockHash;
  uint16_t transactionCount;
  std::vector<Crypto::Hash> baseTransactionBranch;
  Transaction baseTransaction;
  std::vector<Crypto::Hash> blockchainBranch;
};

struct BlockHeader {
  uint8_t majorVersion;
  uint8_t minorVersion;
  uint32_t nonce;
  uint64_t timestamp;
  Crypto::Hash previousBlockHash;
};

struct Block : public BlockHeader {
  ParentBlock parentBlock;
  Transaction baseTransaction;
  std::vector<Crypto::Hash> transactionHashes;
};

struct AccountPublicAddress {
  Crypto::PublicKey spendPublicKey;
  Crypto::PublicKey viewPublicKey;
};

struct AccountKeys {
  AccountPublicAddress address;
  Crypto::SecretKey spendSecretKey;
  Crypto::SecretKey viewSecretKey;
};

struct KeyPair {
  Crypto::PublicKey publicKey;
  Crypto::SecretKey secretKey;
};

using BinaryArray = std::vector<uint8_t>;

}
