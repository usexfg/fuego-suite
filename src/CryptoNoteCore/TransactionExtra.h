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

#include <algorithm>
#include <string>
#include <vector>
#include <boost/variant.hpp>

#include "../../include/CryptoNote.h"
#include "ProofStructures.h"

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
#define TX_EXTRA_DIGM_MINT                  0xA8  // DIGM coin mint (33% BURN / digm treasury 33% \ devs 33%)

// 0xEF tag: Elderfier staking
#define TX_EXTRA_ELDERFIER_DEPOSIT          0xEF  // Elderfier staking deposit (no banking fee)

// 0x_A tags: DIGM Artist related meta/msgs/txns
#define TX_EXTRA_DIGM_ALBUM                 0x0A  // Album metadata
// Future: 0x1A, 0x2A, 0x3A, etc.

// 0x_B tags: DIGM Listener related txns
#define TX_EXTRA_DIGM_LISTEN_RIGHTS         0x0B  // Listen rights purchase
// Future: 0x1B, 0x2B, 0x3B, etc.

// 0x_C tags: DIGM Curator related txns
#define TX_EXTRA_DIGM_CURATOR               0x0C  // Curator operations
#define TX_EXTRA_DIGM_CURATOR_COIN          0x1C  // CURA coin operations

// 0xCD tag: COLD (CD) deposits
#define TX_EXTRA_COLD_COMMITMENT            0xCD  // COLD CD deposits
// 0x69 tag: Receipts for both COLD & YIELD
#define TX_EXTRA_COLD_RECEIPT               0x69  // All Deposits receipt

// 0x07 FUEGO MOB Custom Interest Assets   Check full compatibility -
#define TX_EXTRA_YIELD_COMMITMENT           0x07  //  yield commitment

// 0x_C tags: Elderfier system (consensus/messaging)
#define TX_EXTRA_ELDERFIER_MESSAGE          0xEC  // Elderfier messaging/consensus

// 0xEA tag: @ Alias registration (on-chain)
#define TX_EXTRA_ALIAS                      0xEA  // @ alias registration

// 0xCE tag: COLD migration (register v3 commitment for a pre-v3 legacy deposit)
#define TX_EXTRA_COLD_MIGRATION             0xCE

// 0xD5 tag: Encrypted deposit secret (for COLD withdrawal_commitment_output recovery from seed)
#define TX_EXTRA_DEPOSIT_SECRET             0xD5

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

// @ Alias registration structure (0xEA)
struct TransactionExtraAliasRegistration {
  uint8_t version = 1;             // Schema version
  std::string alias;               // Exactly 8 chars: [A-Z0-9] for EFiers, [a-z0-9] for regular users
  Crypto::Hash aliasHash;          // cn_fast_hash(alias) for fast lookup
  Crypto::Hash addressHash;        // cn_fast_hash(spendKey||viewKey) for privacy (v2 scheme)
  std::string ownerAddress;        // Full wallet address (optional: can be empty for privacy)
  uint8_t aliasType = 0;           // 0 = Elderfier (ALLCAPS [A-Z0-9]), 1 = Regular user (lowercase [a-z0-9])
  uint32_t networkId = 0;          // Fuego network identifier — prevents testnet-to-mainnet replay attacks

  bool serialize(ISerializer& serializer);
  bool isValid() const;
};

// DIGM transaction extra structures will be implemented later
// Reserved tags: 0x0A (Album), 0x0B (Listen Rights), 0x0C (Curator), 0x1C (CURA Coin), 0xA8 (DIGM Mint)

// COLD commitment structure - mirrors HEAT but includes term in commitment preimage
struct TransactionExtraColdCommitment {
  Crypto::Hash commitment;       // 🔒 SECURE: keccak256(secret+amount+tx_hash+recipient+network+chain+version+term)
  uint64_t amount;               // Principal amount in atomic units
  uint32_t term;                 // Deposit term in blocks (differentiates from HEAT which is FOREVER)
  std::vector<uint8_t> metadata; // Chain info, EVM address encoded
  uint8_t claimChainCode;        // Claim chain (1=ETH, 2=ARB, 3=SOL, etc.)
  std::vector<uint8_t> gift_secret;  // Secret key encrypted with recipient's view key
                                     // Only used for gifted deposits, empty if not gifting

  bool serialize(ISerializer& serializer);
};

// Legacy alias for backward compatibility
using TransactionExtraCDDepositSecret = TransactionExtraColdCommitment;

// COLD migration: register a v3 commitment for a pre-v3 legacy deposit.
// Attached to a regular self-transfer (no deposit output needed).
// Blockchain validates that originalTxHash is a real deposit with matching amount/term.
struct TransactionExtraColdMigration {
  Crypto::Hash originalTxHash;    // 32 bytes: tx hash of the original pre-v3 deposit
  Crypto::Hash commitment;        // 32 bytes: v3 commitment (keccak256 of preimage)
  uint64_t amount;                // 8 bytes: original deposit amount (must match)
  uint32_t term;                  // 4 bytes: original deposit term (must match)
  uint8_t claimChainCode;         // 1 byte: claim chain (1=ETH, 2=ARB, etc.)

  bool serialize(ISerializer& serializer);
};

// ============================================================
// Fuego Ring-Signature Commitment Key Derivation
// ============================================================

// Keys derived from a 32-byte depositSecret for commitment outputs.
// All values are deterministic from depositSecret — nothing extra needs storing.
//
//   commitKey  = H("fuego_commit_key"  || depositSecret) * G  — on-chain spend key
//   keyScalar  = the scalar for commitKey                      — spend secret
//   keyImage   = H_p(commitKey) * keyScalar                   — double-spend nullifier
//   amountMask = H("fuego_amount_mask" || depositSecret) mod l — Pedersen blinding factor for amountCommitment
struct DepositCommitmentKeys {
  Crypto::PublicKey           commitKey;
  Crypto::SecretKey           keyScalar;
  Crypto::KeyImage            keyImage;
  Crypto::EllipticCurveScalar amountMask; // blinding factor for amountCommitment
};

// Derive all commitment keys from a 32-byte deposit secret.
// For HEAT burns: caller discards keyScalar (permanently non-spendable).
// For COLD/EFier: store depositSecret encrypted in tx_extra (TX_EXTRA_DEPOSIT_SECRET).
// The masks let the wallet compute + verify amountCommitment and termCommitment.
DepositCommitmentKeys deriveCommitmentKeys(const std::array<uint8_t, 32>& depositSecret);

// ============================================================
// Unified Deposit Secret for v10+ Commitment Outputs (0xD5)
// ============================================================
// All v10+ deposit types (COLD, HEAT, EFier, Yield) write a SINGLE 0xD5 tag.
// The deposit type is encoded inside the encrypted payload — no type-revealing
// tag appears on-chain. Old tags (0x08, 0xCD, 0xEF) remain for legacy multisig
// deposits only.

enum class DepositType : uint8_t {
  COLD      = 0x01,  // COLD CD deposit — withdrawable after term
  HEAT      = 0x02,  // HEAT burn — permanent, key discarded
  YIELD     = 0x03,  // Yield / CIA deposit
};

// Fixed-size plaintext payload encrypted under the wallet's view key.
// 46 bytes total, encrypted with chacha8.
#pragma pack(push, 1)
struct DepositSecretPayload {
  uint8_t  depositType;        // DepositType enum (type-erased for on-chain privacy)
  uint64_t amount;             // deposit amount in atomic units (wallet display)
  uint32_t term;               // lock term in blocks (wallet display)
  uint8_t  depositSecret[32];  // random 32-byte secret (source of all derived keys)
};
#pragma pack(pop)
static_assert(sizeof(DepositSecretPayload) == 45, "DepositSecretPayload size mismatch");

// On-chain representation: ephemeral pubkey (32) + encrypted payload (45) = 77 bytes.
// Node stores without decrypting; only the owning wallet can read.
struct TransactionExtraDepositSecret {
  Crypto::PublicKey ephPubKey;            // one-time key for ECDH
  std::vector<uint8_t> encryptedPayload; // exactly 45 bytes (sizeof DepositSecretPayload)
};

// Encrypt a DepositSecretPayload for the wallet owner.
// Generates an ephemeral keypair internally — no tx secret key needed.
// Encryption: chacha8(key=ECDH(recipientViewPub, ephSecKey), iv=ephPubKey[0:8])
bool encryptDepositSecret(const DepositSecretPayload& plaintext,
                          const Crypto::PublicKey& recipientViewPubKey,
                          TransactionExtraDepositSecret& out);

// Decrypt a TransactionExtraDepositSecret using the wallet's view key.
// Uses the embedded ephPubKey for ECDH — no tx pubkey needed.
bool decryptDepositSecret(const TransactionExtraDepositSecret& encrypted,
                          const Crypto::SecretKey& walletViewSecKey,
                          DepositSecretPayload& out);

// Write a TransactionExtraDepositSecret into tx_extra bytes (tag 0xD5 + len + ciphertext).
bool addDepositSecretToExtra(std::vector<uint8_t>& tx_extra,
                             const TransactionExtraDepositSecret& secret);

// Find and return the first 0xD5 record from tx_extra bytes (encrypted, not decrypted).
bool getDepositSecretFromExtra(const std::vector<uint8_t>& tx_extra,
                               TransactionExtraDepositSecret& out);

bool addColdMigrationToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraColdMigration& migration);


typedef boost::variant<CryptoNote::TransactionExtraPadding, CryptoNote::TransactionExtraPublicKey, CryptoNote::TransactionExtraNonce, CryptoNote::TransactionExtraMergeMiningTag, CryptoNote::tx_extra_message, CryptoNote::TransactionExtraTTL, CryptoNote::TransactionExtraAliasRegistration, CryptoNote::TransactionExtraHeatCommitment, CryptoNote::TransactionExtraYieldCommitment, CryptoNote::TransactionExtraColdCommitment, CryptoNote::TransactionExtraColdMigration, CryptoNote::TransactionExtraBurnReceipt, CryptoNote::TransactionExtraDepositReceipt> TransactionExtraField;



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

// @ Alias registration helper functions
bool addAliasToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraAliasRegistration& alias);
bool getAliasFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraAliasRegistration& alias);

// DIGM helper functions will be implemented later

// COLD Commitment helper functions (unified with HEAT style)
bool createTxExtraWithColdCommitment(const Crypto::Hash& commitment, uint64_t amount, uint32_t term,
                                      uint8_t claimChainCode, const std::vector<uint8_t>& metadata,
                                      const std::vector<uint8_t>& gift_secret, std::vector<uint8_t>& extra);
bool addColdCommitmentToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraColdCommitment& commitment);
bool getColdCommitmentFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraColdCommitment& commitment);

// Legacy aliases for backward compatibility
inline bool addCDDepositSecretToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraColdCommitment& c) {
  return addColdCommitmentToExtra(tx_extra, c);
}
inline bool getCDDepositSecretFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraColdCommitment& c) {
  return getColdCommitmentFromExtra(tx_extra, c);
}

// Secret encryption helper functions
bool encryptSecretWithViewKey(const std::vector<uint8_t>& secret, const Crypto::PublicKey& recipientViewKey, std::vector<uint8_t>& gift_secret);
bool decryptSecretWithViewKey(const std::vector<uint8_t>& gift_secret, const Crypto::SecretKey& viewSecretKey, std::vector<uint8_t>& secret);

// Helper functions for handling gift_secret field
bool isDummyGiftSecret(const std::vector<uint8_t>& gift_secret);
std::vector<uint8_t> createDummyGiftSecret();

// COLD Deposit validation and utility functions
// Note: APR is now derived from tier in smart contract, not stored on-chain
uint64_t getColdTermBlocks(uint8_t term_code);
uint64_t getColdTermDays(uint8_t term_code);

// ---------------- UNIFIED COMMITMENT FORMAT ----------------
// Both HEAT and COLD use the SAME 88-byte preimage:
//   keccak256(secret || le64(amount) || tx_prefix_hash || network_id || target_chain_id || version || le32(term))
//
// HEAT burns use term = DEPOSIT_TERM_FOREVER (0xFFFFFFFF)
// COLD deposits use their actual term in blocks
//
// PRIVACY MODEL: No recipient in commitment - contract mints to msg.sender, nullifier prevents replay

// Unified commitment computation for BOTH HEAT and COLD
// Uses 88-byte preimage: 32 + 8 + 32 + 4 + 4 + 4 + 4 = 88 bytes
// For HEAT: pass term = parameters::DEPOSIT_TERM_FOREVER (0xFFFFFFFF)
// For COLD: pass actual term in blocks
Crypto::Hash computeCommitment(const std::array<uint8_t, 32>& secret,
                               uint64_t amount_atomic,
                               const Crypto::Hash& tx_prefix_hash,
                               uint32_t network_id,
                               uint32_t target_chain_id,
                               uint32_t commitment_version,
                               uint32_t term);

// HEAT convenience wrapper - uses DEPOSIT_TERM_FOREVER for term
// Computes: keccak256(secret || amount || tx_hash || network || chain || version || 0xFFFFFFFF)
Crypto::Hash computeHeatCommitment(const std::array<uint8_t, 32>& secret,
                                   uint64_t amount_atomic,
                                   const Crypto::Hash& tx_prefix_hash,
                                   uint32_t network_id,
                                   uint32_t target_chain_id,
                                   uint32_t commitment_version);

// Builds tx.extra with TX_EXTRA_HEAT_COMMITMENT (0x08) given inputs
// PRIVACY MODEL: No recipient - contract mints to msg.sender
bool buildHeatExtra(const std::array<uint8_t, 32>& secret,
                    uint64_t amount_atomic,
                    const Crypto::Hash& tx_prefix_hash,
                    uint32_t network_id,
                    uint32_t target_chain_id,
                    uint32_t commitment_version,
                    const std::vector<uint8_t>& metadata,
                    std::vector<uint8_t>& extra);

// COLD convenience wrapper - same as computeCommitment but named for clarity
// Computes: keccak256(secret || amount || tx_hash || network || chain || version || term)
Crypto::Hash computeColdCommitment(const std::array<uint8_t, 32>& secret,
                                   uint64_t amount_atomic,
                                   const Crypto::Hash& tx_prefix_hash,
                                   uint32_t network_id,
                                   uint32_t target_chain_id,
                                   uint32_t commitment_version,
                                   uint32_t term);

// Builds tx.extra with TX_EXTRA_COLD_COMMITMENT (0xCD) given inputs
// PRIVACY MODEL: No recipient - contract mints to msg.sender
bool buildColdExtra(const std::array<uint8_t, 32>& secret,
                    uint64_t amount_atomic,
                    const Crypto::Hash& tx_prefix_hash,
                    uint32_t network_id,
                    uint32_t target_chain_id,
                    uint32_t commitment_version,
                    uint32_t term,
                    uint8_t claimChainCode,
                    const std::vector<uint8_t>& metadata,
                    const std::vector<uint8_t>& gift_secret,
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
