// Copyright (c) 2017-2025 Fuego Developers
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

#include <algorithm>
#include <string>
#include <vector>
#include <boost/variant.hpp>

#include <CryptoNote.h>
#include "ProofStructures.h"
#include "../include/EldernodeIndexTypes.h"

#define TX_EXTRA_PADDING_MAX_COUNT          255
#define TX_EXTRA_NONCE_MAX_COUNT            255

// Transaction Extra Tag Categories:
//
// 0x_0 tags: Core system tags
#define TX_EXTRA_TAG_PADDING                0x00
#define TX_EXTRA_TAG_PUBKEY                 0x01
#define TX_EXTRA_NONCE                      0x02
#define TX_EXTRA_MERGE_MINING_TAG           0x03
#define TX_EXTRA_MESSAGE_TAG                0x04
#define TX_EXTRA_TTL                        0x05

// 0x_8 tags: Burn-related deposit types
#define TX_EXTRA_HEAT_COMMITMENT            0x08  // Heat commitment (burn)
#define TX_EXTRA_BURN_RECEIPT               0x18  // Burn transaction receipt
#define TX_EXTRA_ELDERFIER_DEPOSIT          0xE8  // Elderfier staking (moved from 0x06)
#define TX_EXTRA_DIGM_MINT                  0xA8  // DIGM coin mint by burn (Split 3 ways dev, digm treasury, burn)

// 0x_A tags: DIGM Artist related meta/msgs/txns
#define TX_EXTRA_DIGM_ALBUM                 0x0A  // Album metadata
// Future: 0x1A, 0x2A, 0x3A, etc.

// 0x_B tags: DIGM Listener related txns
#define TX_EXTRA_DIGM_LISTEN_RIGHTS         0x0B  // Listen rights purchase
// Future: 0x1B, 0x2B, 0x3B, etc.

// 0x_C tags: DIGM Curator related txns
#define TX_EXTRA_DIGM_CURATOR               0x0C  // Curator operations
#define TX_EXTRA_DIGM_CURATOR_COIN          0x1C  // CURA coin operations

// 0xCD tags: COLD (CD) yield deposits
#define TX_EXTRA_CD_DEPOSIT_SECRET          0xCD  // COLD yield deposits


// 0x_E tags: Elderfier system (consensus/messaging)
#define TX_EXTRA_ELDERFIER_MESSAGE          0xEF  // Elderfier messaging/consensus

// 0x07 FUEGO MOB Custom Interest Assets   Check full compatibility -
#define TX_EXTRA_YIELD_COMMITMENT           0x07  //  yield commitment
#define TX_EXTRA_DEPOSIT_RECEIPT            0x69  // Deposit receipt

#define TX_EXTRA_NONCE_PAYMENT_ID           0x00

namespace CryptoNote {

class ISerializer;

struct TransactionExtraPadding {
  size_t size;
};

struct TransactionExtraPublicKey {
  Crypto::PublicKey publicKey;
};

struct TransactionExtraNonce {
  std::vector<uint8_t> nonce;
};

struct TransactionExtraMergeMiningTag {
  size_t depth;
  Crypto::Hash merkleRoot;
};

struct tx_extra_message {
  std::string data;

  bool encrypt(std::size_t index, const std::string &message, const AccountPublicAddress* recipient, const KeyPair &txkey);
  bool decrypt(std::size_t index, const Crypto::PublicKey &txkey, const Crypto::SecretKey *recepient_secret_key, std::string &message) const;

  bool serialize(ISerializer& serializer);
};

struct TransactionExtraTTL {
  uint64_t ttl;
};

struct TransactionExtraHeatCommitment {
  Crypto::Hash commitment;       // 🔒 SECURE: Only commitment hash on blockchain
  uint64_t amount;
  std::vector<uint8_t> metadata;

  bool serialize(ISerializer& serializer);
};

struct TransactionExtraYieldCommitment {
  Crypto::Hash commitment;       // 🔒 SECURE: Only commitment hash on blockchain
  uint64_t amount;               // Principal amount in XFG
  uint32_t term;                 // Deposit term in blocks
  std::vector<uint8_t> metadata;
  uint8_t claimChainCode;        // Claim chain (1=ETH, 2=SOL, 3=C0DL)
  std::string CIAId;             // Crypto Interest Asset ID (hash of token/asset)
  std::vector<uint8_t> gift_secret;        // Secret key encrypted with recipient's view key
                                            // Only used for gifted deposits, otherwise dummy data with pattern


  bool serialize(ISerializer& serializer);
};

struct TransactionExtraElderfierDeposit {
  Crypto::Hash depositHash;         // Unique deposit identifier
  uint64_t depositAmount;           // XFG amount (minimum 800 XFG)
  std::string elderfierAddress;     // Elderfier node address
  uint32_t securityWindow;          // Security window in seconds (8 hours = 28800)
  std::vector<uint8_t> metadata;   // Additional metadata
  std::vector<uint8_t> signature;   // Deposit signature
  bool isSlashable;                // True - deposits can be slashed by Elder Council

  bool serialize(ISerializer& serializer);
  bool isValid() const;
  std::string toString() const;
};

struct TransactionExtraElderfierMessage {
  Crypto::PublicKey senderKey;         // Elderfier node public key
  Crypto::PublicKey recipientKey;      // Target Elderfier node public key (or broadcast)
  uint32_t messageType;                // Message type (consensus, slashing, monitoring, etc.)
  uint64_t timestamp;                  // Message timestamp
  std::vector<uint8_t> messageData;    // Encrypted message payload
  std::vector<uint8_t> signature;      // Message signature

  // Consensus requirements (for 0xEF transactions)
  bool consensusRequired;              // Whether this message requires consensus validation
  ElderfierConsensusType consensusType; // Type of consensus required (QUORUM, PROOF, WITNESS)
  uint32_t requiredThreshold;          // Threshold required (e.g., 80 for quorum)
  Crypto::Hash targetDepositHash;      // Target 0xE8 deposit hash (for slashing messages)

  bool serialize(ISerializer& serializer);
  bool isValid() const;
  bool requiresQuorumConsensus() const; // Check if this message requires quorum
  std::string toString() const;
};

// DIGM transaction extra structures will be implemented later
// Reserved tags: 0x0A (Album), 0x0B (Listen Rights), 0x0C (Curator), 0x1C (CURA Coin), 0xA8 (DIGM Mint)

struct TransactionExtraCDDepositSecret {
  Crypto::Hash commitment;       // 🔒 SECURE: Only commitment hash on blockchain
  uint64_t amount;               // Principal amount in XFG
  uint32_t term;                 // Deposit term in blocks
  std::vector<uint8_t> metadata;
  uint8_t claimChainCode;        // Claim chain (1=ETH, 2=SOL, 3=C0DL)
  uint32_t apr_basis_points;     // APR in basis points for CD
  std::vector<uint8_t> gift_secret;        // Secret key encrypted with recipient's view key
                                            // Only used for gifted deposits, otherwise dummy data with pattern

  bool serialize(ISerializer& serializer);
};


typedef boost::variant<CryptoNote::TransactionExtraPadding, CryptoNote::TransactionExtraPublicKey, CryptoNote::TransactionExtraNonce, CryptoNote::TransactionExtraMergeMiningTag, CryptoNote::tx_extra_message, CryptoNote::TransactionExtraTTL, CryptoNote::TransactionExtraElderfierDeposit, CryptoNote::TransactionExtraElderfierMessage, CryptoNote::TransactionExtraHeatCommitment, CryptoNote::TransactionExtraYieldCommitment, CryptoNote::TransactionExtraCDDepositSecret, CryptoNote::TransactionExtraBurnReceipt, CryptoNote::TransactionExtraDepositReceipt> TransactionExtraField;



template<typename T>
bool findTransactionExtraFieldByType(const std::vector<TransactionExtraField>& tx_extra_fields, T& field) {
  auto it = std::find_if(tx_extra_fields.begin(), tx_extra_fields.end(),
    [](const TransactionExtraField& f) { return typeid(T) == f.type(); });

  if (tx_extra_fields.end() == it)
    return false;

  field = boost::get<T>(*it);
  return true;
}

bool parseTransactionExtra(const std::vector<uint8_t>& tx_extra, std::vector<TransactionExtraField>& tx_extra_fields);
bool writeTransactionExtra(std::vector<uint8_t>& tx_extra, const std::vector<TransactionExtraField>& tx_extra_fields);

Crypto::PublicKey getTransactionPublicKeyFromExtra(const std::vector<uint8_t>& tx_extra);
bool addTransactionPublicKeyToExtra(std::vector<uint8_t>& tx_extra, const Crypto::PublicKey& tx_pub_key);
bool addExtraNonceToTransactionExtra(std::vector<uint8_t>& tx_extra, const BinaryArray& extra_nonce);
void setPaymentIdToTransactionExtraNonce(BinaryArray& extra_nonce, const Crypto::Hash& payment_id);
bool getPaymentIdFromTransactionExtraNonce(const BinaryArray& extra_nonce, Crypto::Hash& payment_id);
bool appendMergeMiningTagToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraMergeMiningTag& mm_tag);
bool append_message_to_extra(std::vector<uint8_t>& tx_extra, const tx_extra_message& message);
std::vector<std::string> get_messages_from_extra(const std::vector<uint8_t>& extra, const Crypto::PublicKey &txkey, const Crypto::SecretKey *recepient_secret_key);
void appendTTLToExtra(std::vector<uint8_t>& tx_extra, uint64_t ttl);
bool getMergeMiningTagFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraMergeMiningTag& mm_tag);

bool createTxExtraWithPaymentId(const std::string& paymentIdString, std::vector<uint8_t>& extra);
//returns false if payment id is not found or parse error
bool getPaymentIdFromTxExtra(const std::vector<uint8_t>& extra, Crypto::Hash& paymentId);
bool parsePaymentId(const std::string& paymentIdString, Crypto::Hash& paymentId);

// HEAT commitment helper functions
bool createTxExtraWithHeatCommitment(const Crypto::Hash& commitment, uint64_t amount, const std::vector<uint8_t>& metadata, std::vector<uint8_t>& extra);
bool addHeatCommitmentToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraHeatCommitment& commitment);
bool getHeatCommitmentFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraHeatCommitment& commitment);

// Yield commitment helper functions
bool createTxExtraWithYieldCommitment(const Crypto::Hash& commitment, uint64_t amount, uint32_t term, const std::string& CIAId, const std::vector<uint8_t>& metadata, uint8_t claimChainCode, const std::vector<uint8_t>& gift_secret, std::vector<uint8_t>& extra);
bool addYieldCommitmentToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraYieldCommitment& commitment);
bool getYieldCommitmentFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraYieldCommitment& commitment);

// Elderfier Deposit helper functions (contingency-based)
bool createTxExtraWithElderfierDeposit(const Crypto::Hash& depositHash, uint64_t depositAmount, const std::string& elderfierAddress, uint32_t securityWindow, const std::vector<uint8_t>& metadata, std::vector<uint8_t>& extra);
bool addElderfierDepositToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraElderfierDeposit& deposit);
bool getElderfierDepositFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraElderfierDeposit& deposit);

// Elderfier Message helper functions (messaging/monitoring)
bool createTxExtraWithElderfierMessage(const Crypto::PublicKey& senderKey, const Crypto::PublicKey& recipientKey, uint32_t messageType, uint64_t timestamp, const std::vector<uint8_t>& messageData, std::vector<uint8_t>& extra);
bool addElderfierMessageToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraElderfierMessage& message);
bool getElderfierMessageFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraElderfierMessage& message);

// Consensus-specific message creation functions
bool createElderfierQuorumMessage(const Crypto::PublicKey& senderKey, const Crypto::PublicKey& recipientKey, const Crypto::Hash& targetDepositHash, uint32_t messageType, const std::vector<uint8_t>& messageData, uint64_t timestamp, TransactionExtraElderfierMessage& message);
bool createElderfierProofMessage(const Crypto::PublicKey& senderKey, const Crypto::PublicKey& recipientKey, uint32_t messageType, const std::vector<uint8_t>& messageData, uint64_t timestamp, TransactionExtraElderfierMessage& message);
bool createElderfierWitnessMessage(const Crypto::PublicKey& senderKey, const Crypto::PublicKey& recipientKey, uint32_t messageType, const std::vector<uint8_t>& messageData, uint64_t timestamp, TransactionExtraElderfierMessage& message);

// DIGM helper functions will be implemented later

// CD Deposit Secret helper functions
bool createTxExtraWithCDDepositSecret(const std::vector<uint8_t>& secret_key, uint64_t amount, uint32_t apr_basis_points, uint8_t term_code, uint8_t chain_code, const std::vector<uint8_t>& metadata, std::vector<uint8_t>& extra);
bool addCDDepositSecretToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraCDDepositSecret& deposit_secret);
bool getCDDepositSecretFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraCDDepositSecret& deposit_secret);

// Secret encryption helper functions
bool encryptSecretWithViewKey(const std::vector<uint8_t>& secret, const Crypto::PublicKey& recipientViewKey, std::vector<uint8_t>& gift_secret);
bool decryptSecretWithViewKey(const std::vector<uint8_t>& gift_secret, const Crypto::SecretKey& viewSecretKey, std::vector<uint8_t>& secret);

// Helper functions for handling gift_secret field
bool isDummyGiftSecret(const std::vector<uint8_t>& gift_secret);
std::vector<uint8_t> createDummyGiftSecret();

// CD Deposit validation and utility functions
bool validateCDTermAndAPR(uint8_t term_code, uint32_t apr_basis_points);
uint64_t getCDTermDays(uint8_t term_code);
double getCDAPRPercent(uint8_t term_code);

// Helper APIs for wallet integration
// Computes Keccak256(address || "recipient") into out_hash
bool computeHeatRecipientHash(const std::string& eth_address, Crypto::Hash& out_hash);
// Computes Keccak256(secret || le64(amount) || tx_prefix_hash || recipient_hash || network_id || target_chain_id || version)
Crypto::Hash computeHeatCommitment(const std::array<uint8_t, 32>& secret,
                                   uint64_t amount_atomic,
                                   const Crypto::Hash& tx_prefix_hash,
                                   const std::string& eth_address,
                                   uint32_t network_id,
                                   uint32_t target_chain_id,
                                   uint32_t commitment_version);
// Builds tx.extra with TX_EXTRA_HEAT_COMMITMENT (0x08) given inputs
bool buildHeatExtra(const std::array<uint8_t, 32>& secret,
                    uint64_t amount_atomic,
                    const Crypto::Hash& tx_prefix_hash,
                    const std::string& eth_address,
                    uint32_t network_id,
                    uint32_t target_chain_id,
                    uint32_t commitment_version,
                    const std::vector<uint8_t>& metadata,
                    std::vector<uint8_t>& extra);




// Burn receipt helper functions
bool getBurnReceiptFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraBurnReceipt& burnReceipt);
bool addBurnReceiptToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraBurnReceipt& burnReceipt);
bool createTxExtraWithBurnReceipt(const TransactionExtraBurnReceipt& burnReceipt, std::vector<uint8_t>& extra);





// Deposit receipt helper functions
bool getDepositReceiptFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraDepositReceipt& depositReceipt);
bool addDepositReceiptToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraDepositReceipt& depositReceipt);
bool createTxExtraWithDepositReceipt(const TransactionExtraDepositReceipt& depositReceipt, std::vector<uint8_t>& extra);

// Cold Deposit (CD) term codes and APR rates
enum CDTermCode {
  CD_TERM_3MO_8PCT = 1,      // 3 months / 8% APR (90 days)
  CD_TERM_9MO_18PCT = 2,     // 9 months / 18% APR (270 days)
  CD_TERM_1YR_21PCT = 3,     // 1 year / 21% APR (365 days)
  CD_TERM_3YR_33PCT = 4,     // 3 years / 33% APR (1095 days)
  CD_TERM_5YR_80PCT = 5      // 5 years / 80% APR (1825 days)
};

// Cold Deposit APR rates in basis points (1% = 100 basis points)
enum CDAPRRate {
  CD_APR_8PCT = 800,         // 8% APR = 800 basis points
  CD_APR_18PCT = 1800,       // 18% APR = 1800 basis points
  CD_APR_21PCT = 2100,       // 21% APR = 2100 basis points
  CD_APR_33PCT = 3300,       // 33% APR = 3300 basis points
  CD_APR_80PCT = 8000        // 80% APR = 8000 basis points
};

}
