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
#include <map>
#include <string>
#include <vector>
#include <boost/variant.hpp>

#include "../../include/CryptoNote.h"
#include "ProofStructures.h"

#define TX_EXTRA_PADDING_MAX_COUNT          255
#define TX_EXTRA_NONCE_MAX_COUNT            255

// Transaction Extra Tag Categories:
// 0x_0 tags: Core system tags
#define TX_EXTRA_TAG_PADDING                0x00
#define TX_EXTRA_TAG_PUBKEY                 0x01
#define TX_EXTRA_NONCE                      0x02
#define TX_EXTRA_MERGE_MINING_TAG           0x03
#define TX_EXTRA_MESSAGE_TAG                0x04
#define TX_EXTRA_TTL                        0x05
// 0x_8 tags: Burn-related deposit types
#define TX_EXTRA_HEAT_COMMITMENT            0x08
#define TX_EXTRA_BURN_RECEIPT               0x18
#define TX_EXTRA_DIGM_MINT                  0xA8
// 0x_A tags: DIGM Artist related meta/msgs/txns
#define TX_EXTRA_DIGM_ALBUM                 0x0A
// 0x_B tags: DIGM Listener related txns
#define TX_EXTRA_DIGM_LISTEN_RIGHTS         0x0B
// 0x_C tags: DIGM Curator related txns
#define TX_EXTRA_DIGM_CURATOR               0x0C
#define TX_EXTRA_DIGM_CURATOR_COIN          0x1C
// 0xCD tag: COLD (CD) deposits
#define TX_EXTRA_SIMPLE_CD                0xCD
// #define TX_EXTRA_COLD_COMMITMENT            0xCD  // REMOVED: COLD deposit type
// #define TX_EXTRA_COLD_RECEIPT               0x69  // REMOVED: COLD deposit type
#define TX_EXTRA_YIELD_COMMITMENT           0x07
#define TX_EXTRA_ALIAS                      0xEA
#define TX_EXTRA_ALIAS_RELEASE              0xEC
#define TX_EXTRA_ALIAS_TRANSFER             0xED
// #define TX_EXTRA_COLD_MIGRATION             0xCE  // REMOVED: COLD migration
#define TX_EXTRA_LEGACY_BOND               0xCB
#define TX_EXTRA_LEGACY_BOND_CLAIM         0xCC
#define TX_EXTRA_DEPOSIT_SECRET             0xD5
#define TX_EXTRA_CD_BONUS_CLAIM             0xD6  // v11+: per-input BV-backed bonus claim
// 0xF_ tags: Hearth AMM (v11+)
#define TX_EXTRA_AMM_SWAP                   0xF0
#define TX_EXTRA_AMM_ADD_LIQ                0xF1
#define TX_EXTRA_AMM_REM_LIQ                0xF2
#define TX_EXTRA_AMM_COMPOUND               0xF3
#define TX_EXTRA_AMM_CLAIM                  0xF4
// 0xF_ tags: v12 per-asset auth
#define TX_EXTRA_HEAT_MINT_AUTH             0xF5
#define TX_EXTRA_AMM_SWAP_AUTH              0xF6
#define TX_EXTRA_AMM_LP_ADD_AUTH            0xF7
#define TX_EXTRA_AMM_LP_REM_AUTH            0xF8
#define TX_EXTRA_HEAT_SEND_AUTH             0xF9
// 0xFA: Orderbook limit order placement (v11+)
#define TX_EXTRA_ORDER_PLACE                0xFA
#define TX_EXTRA_LIMIT_DEPOSIT              0xFB
#define TX_EXTRA_ORDER_CANCEL               0x0F
#define TX_EXTRA_MARKET_BUY_AUTH            0xFC
#define TX_EXTRA_MARKET_SELL_AUTH           0xFD
#define TX_EXTRA_LIMIT_WITHDRAW             0xFE
#define TX_EXTRA_TREASURY_FUND              0xFF
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
  Crypto::Hash commitment;       // Opaque commitment hash
  uint64_t amount;               // Plaintext amount (privacy: visible on-chain)
  std::vector<uint8_t> metadata; // Metadata blob (max 128 bytes on wire).
                                 // WARNING: This field is NOT encrypted on-chain.
                                 // Do NOT embed PII, secrets, or identifiable data.
                                 // Prefer empty or use only for public protocol hints.
  bool serialize(ISerializer& serializer);
};

// REMOVED: COLD deposit type structs
// struct TransactionExtraSimpleCD {
//   Crypto::Hash commitment;
//   uint64_t amount;
//   uint32_t term;
//   bool serialize(ISerializer& serializer);
// };
//
// struct TransactionExtraColdCommitment {
//   Crypto::Hash commitment;
//   uint64_t amount;
//   uint32_t term;
//   uint8_t claimChainCode;
//   std::vector<uint8_t> metadata;
//   std::vector<uint8_t> gift_secret;
//   bool serialize(ISerializer& serializer);
// };

struct TransactionExtraYieldCommitment {
  Crypto::Hash commitment;
  uint64_t amount;
  uint32_t term;
  std::vector<uint8_t> metadata;
  uint8_t claimChainCode;
  std::string CIAId;
  std::vector<uint8_t> gift_secret;
  bool serialize(ISerializer& serializer);
};

// @ Alias registration structure (0xEA)
struct TransactionExtraAliasRegistration {
  uint8_t version = 1;             // Schema version
  std::string alias;               // Exactly 8 chars: [a-z0-9] for regular users
  Crypto::Hash aliasHash;          // cn_fast_hash(alias) for fast lookup
  Crypto::Hash addressHash;        // cn_fast_hash(spendKey||viewKey) for privacy (v2 scheme)
  std::string ownerAddress;        // Full wallet address (optional: can be empty for privacy)
  uint8_t aliasType = 0;           // 0 = reserved (deprecated), 1 = Regular user (lowercase [a-z0-9])
  uint32_t networkId = 0;          // Fuego network identifier — prevents testnet-to-mainnet replay attacks
  bool serialize(ISerializer& serializer);
  bool isValid() const;
};

// @ Alias release structure (0xEC) — void/delete an alias, requires ownership proof
struct TransactionExtraAliasRelease {
  uint8_t version = 1;
  std::string alias;               // Exactly 8 chars — the alias to release
  Crypto::Hash aliasHash;          // cn_fast_hash(alias) for fast lookup
  std::string ownerAddress;        // Full wallet address — needed to verify ownership
  Crypto::Signature proof;         // Signature over cn_fast_hash(alias || addressHash || 0x00) with spend key
  bool serialize(ISerializer& serializer);
  bool isValid() const;
};

struct TransactionExtraAliasTransfer {
  uint8_t version = 1;
  std::string alias;
  Crypto::Hash aliasHash;
  std::string oldOwnerAddress;
  std::string newOwnerAddress;
  Crypto::Hash newAddressHash;
  Crypto::Signature proof;
  bool serialize(ISerializer& serializer);
  bool isValid() const;
};

// REMOVED: COLD migration struct
// struct TransactionExtraColdMigration {
//   Crypto::Hash originalTxHash;
//   Crypto::Hash commitment;
//   uint64_t amount;
//   uint32_t term;
//   uint8_t targetChainId;
//   bool serialize(ISerializer& serializer);
// };

struct TransactionExtraLegacyBond {
  Crypto::Hash originalTxHash;
  uint64_t amount;
  uint32_t originalCreationHeight;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraLegacyBondClaim {
  uint64_t claimedInterest;
  bool serialize(ISerializer& serializer);
};

// v11+: per-input BV-backed CD bonus claim. The input at `inputIndex` must be a
// TransactionInputCommitmentSpend; `claimedBonus` is drawn from the BONUS_VAULT
// partition instead of the CD yield pool. Base interest stays in claimedInterest.
struct TransactionExtraCdBonusClaim {
  uint8_t  inputIndex;
  uint64_t claimedBonus;
};

struct TransactionExtraAmmSwap {
  uint8_t  direction;
  uint64_t inputAmount;
  uint64_t minOutput;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraAmmAddLiquidity {
  uint64_t amountXfg;
  uint64_t amountHeat;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraAmmRemoveLiquidity {
  uint64_t lpSharesBurned;
  uint64_t minAmountXfg;
  uint64_t minAmountHeat;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraAmmCompound {
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraAmmClaim {
  uint64_t lpShares;
  uint64_t minAmountXfg;
  uint64_t minAmountHeat;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraHeatMintAuth {
  uint64_t xfgBurned;
  uint64_t heatMinted;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraHeatSendAuth {
  uint64_t heatAmount;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraAmmSwapAuth {
  uint8_t  direction;
  uint64_t inputAmount;
  uint64_t outputAmount;
  uint64_t minOutput;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraLpAddAuth {
  uint64_t amountXfg;
  uint64_t amountHeat;
  uint64_t lpShares;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraLpRemoveAuth {
  uint64_t lpSharesBurned;
  uint64_t minAmountXfg;
  uint64_t minAmountHeat;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraOrderPlace {
  uint8_t  side;
  uint64_t price;
  uint32_t expiration;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraOrderCancel {
  Crypto::Hash orderId;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraMarketBuyAuth {
  uint64_t xfgWanted;
  uint64_t maxHeatCost;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraMarketSellAuth {
  uint64_t xfgToSell;
  uint64_t minHeatReceive;
  bool serialize(ISerializer& serializer);
};

struct TransactionExtraLimitDeposit {
  uint8_t  side;
  uint64_t amount;
  uint64_t targetPrice;
  uint32_t expiration;
  Crypto::Hash orderId;
  Crypto::Hash addressHash;  // cn_fast_hash(spendKey||viewKey)
};

struct TransactionExtraLimitWithdraw {
  Crypto::Hash orderId;
  // v11+ ownership proof: addressHash = H(spendPublicKey || viewPublicKey),
  // proof signs the domain-separated (orderId, addressHash, outputsHash)
  // tuple, preventing proof replay with attacker-chosen destinations.
  Crypto::PublicKey spendPublicKey;
  Crypto::PublicKey viewPublicKey;
  Crypto::Hash outputsHash;
  Crypto::Signature proof;
};

// v12+: burns `amount` of `asset` (0 = XFG, 1 = HEAT) as a contribution to the
// Treasury LP Manager (counter-funded LP provisioning). No output is created —
// the amount is destroyed and credited to the treasury counter.
struct TransactionExtraTreasuryFund {
  uint8_t  asset;
  uint64_t amount;
};

struct DepositCommitmentKeys {
  Crypto::PublicKey           commitKey;
  Crypto::SecretKey           keyScalar;
  Crypto::KeyImage            keyImage;
  Crypto::EllipticCurveScalar amountMask;
};

DepositCommitmentKeys deriveCommitmentKeys(const std::array<uint8_t, 32>& depositSecret);

enum class DepositType : uint8_t {
  HEAT      = 0x02,
  // COLD = 0xCD,  // REMOVED: COLD deposit type
};

#pragma pack(push, 1)
struct DepositSecretPayload {
  uint8_t  depositType;
  uint64_t amount;
  uint32_t term;
  uint8_t  depositSecret[32];
};
#pragma pack(pop)
static_assert(sizeof(DepositSecretPayload) == 45, "DepositSecretPayload size mismatch");

struct TransactionExtraDepositSecret {
  Crypto::PublicKey ephPubKey;
  unsigned char     checksum[4];
  std::vector<uint8_t> encryptedPayload;
};

bool encryptDepositSecret(const DepositSecretPayload& plaintext,
                          const Crypto::PublicKey& recipientViewPubKey,
                          TransactionExtraDepositSecret& out);

bool decryptDepositSecret(const TransactionExtraDepositSecret& encrypted,
                          const Crypto::SecretKey& walletViewSecKey,
                          DepositSecretPayload& out);

bool addDepositSecretToExtra(std::vector<uint8_t>& tx_extra,
                             const TransactionExtraDepositSecret& secret);

bool getDepositSecretFromExtra(const std::vector<uint8_t>& tx_extra,
                                TransactionExtraDepositSecret& out);

typedef boost::variant<CryptoNote::TransactionExtraPadding, CryptoNote::TransactionExtraPublicKey, CryptoNote::TransactionExtraNonce, CryptoNote::TransactionExtraMergeMiningTag, CryptoNote::tx_extra_message, CryptoNote::TransactionExtraTTL, CryptoNote::TransactionExtraAliasRegistration, CryptoNote::TransactionExtraAliasRelease, CryptoNote::TransactionExtraAliasTransfer, CryptoNote::TransactionExtraHeatCommitment, /* TransactionExtraSimpleCD REMOVED */ /* TransactionExtraColdCommitment REMOVED */ /* TransactionExtraColdMigration REMOVED */ /* TransactionExtraDepositReceipt REMOVED */ CryptoNote::TransactionExtraBurnReceipt, CryptoNote::TransactionExtraLegacyBond, CryptoNote::TransactionExtraLegacyBondClaim, CryptoNote::TransactionExtraCdBonusClaim, CryptoNote::TransactionExtraAmmSwap, CryptoNote::TransactionExtraAmmAddLiquidity, CryptoNote::TransactionExtraAmmRemoveLiquidity, CryptoNote::TransactionExtraAmmCompound, CryptoNote::TransactionExtraAmmClaim, CryptoNote::TransactionExtraHeatMintAuth, CryptoNote::TransactionExtraHeatSendAuth, CryptoNote::TransactionExtraAmmSwapAuth, CryptoNote::TransactionExtraLpAddAuth, CryptoNote::TransactionExtraLpRemoveAuth, CryptoNote::TransactionExtraOrderPlace, CryptoNote::TransactionExtraOrderCancel, CryptoNote::TransactionExtraMarketBuyAuth, CryptoNote::TransactionExtraMarketSellAuth, CryptoNote::TransactionExtraLimitDeposit, CryptoNote::TransactionExtraLimitWithdraw, CryptoNote::TransactionExtraTreasuryFund> TransactionExtraField;

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
// bool addColdMigrationToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraColdMigration& migration);  // REMOVED: COLD migration
bool addLegacyBondToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraLegacyBond& bond);
bool getLegacyBondFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraLegacyBond& bond);
bool addLegacyBondClaimToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraLegacyBondClaim& claim);
bool getLegacyBondClaimFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraLegacyBondClaim& claim);
bool addCdBonusClaimToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraCdBonusClaim& claim);
bool getCdBonusClaimFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraCdBonusClaim& claim);

// v11+: extracts CdBonusClaim extras into bonusByInput (inputIndex → claimedBonus)
// and sums totalBonus. Returns false on malformed fields: parse failure, bad
// input index, non-CommitmentSpend target, duplicate entries, or overflow.
bool getCdBonusClaims(const Transaction& tx,
                      std::map<uint32_t, uint64_t>& bonusByInput,
                      uint64_t& totalBonus);
bool addAmmSwapToExtra(std::vector<uint8_t>& tx_extra, uint8_t direction, uint64_t inputAmount, uint64_t minOutput);
bool addAmmAddLiquidityToExtra(std::vector<uint8_t>& tx_extra, uint64_t amountXfg, uint64_t amountHeat);
bool addAmmRemoveLiquidityToExtra(std::vector<uint8_t>& tx_extra, uint64_t lpSharesBurned, uint64_t minXfg, uint64_t minHeat);
bool addAmmCompoundToExtra(std::vector<uint8_t>& tx_extra);
bool addAmmClaimToExtra(std::vector<uint8_t>& tx_extra, uint64_t lpShares, uint64_t minXfg, uint64_t minHeat);
bool addHeatMintAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t xfgBurned, uint64_t heatMinted);
bool addHeatSendAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t heatAmount);
bool addAmmSwapAuthToExtra(std::vector<uint8_t>& tx_extra, uint8_t direction, uint64_t inputAmount,
                           uint64_t outputAmount, uint64_t minOutput);
bool addLpAddAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t amountXfg, uint64_t amountHeat,
                         uint64_t lpShares);
bool addLpRemoveAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t lpSharesBurned,
                            uint64_t minXfg, uint64_t minHeat);
bool addOrderPlaceToExtra(std::vector<uint8_t>& tx_extra, uint8_t side, uint64_t price, uint32_t expiration);
bool addOrderCancelToExtra(std::vector<uint8_t>& tx_extra, const Crypto::Hash& orderId);
bool addMarketBuyAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t xfgWanted, uint64_t maxHeatCost);
bool addMarketSellAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t xfgToSell, uint64_t minHeatReceive);
bool addLimitDepositToExtra(std::vector<uint8_t>& tx_extra, uint8_t side, uint64_t amount, uint64_t targetPrice, uint32_t expiration, const Crypto::Hash& orderId, const Crypto::Hash& addressHash);
Crypto::Hash getLimitWithdrawOutputHash(const std::vector<CryptoNote::TransactionOutput>& outputs);
Crypto::Hash getLimitWithdrawAuthHash(const Crypto::Hash& orderId,
                                      const Crypto::Hash& addressHash,
                                      const Crypto::Hash& outputsHash);
bool addLimitWithdrawToExtra(std::vector<uint8_t>& tx_extra,
                             const Crypto::Hash& orderId,
                             const Crypto::PublicKey& spendPublicKey,
                             const Crypto::PublicKey& viewPublicKey,
                             const Crypto::Hash& outputsHash,
                             const Crypto::Signature& proof);
bool addTreasuryFundToExtra(std::vector<uint8_t>& tx_extra, uint8_t asset, uint64_t amount);
Crypto::PublicKey computePoolCommitKey();
Crypto::Hash hashOutput(const TransactionOutput& output);
std::vector<std::string> get_messages_from_extra(const std::vector<uint8_t>& extra, const Crypto::PublicKey &txkey, const Crypto::SecretKey *recepient_secret_key);
void appendTTLToExtra(std::vector<uint8_t>& tx_extra, uint64_t ttl);
bool getMergeMiningTagFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraMergeMiningTag& mm_tag);
bool createTxExtraWithPaymentId(const std::string& paymentIdString, std::vector<uint8_t>& extra);
bool getPaymentIdFromTxExtra(const std::vector<uint8_t>& extra, Crypto::Hash& paymentId);
bool parsePaymentId(const std::string& paymentIdString, Crypto::Hash& paymentId);
bool createTxExtraWithHeatCommitment(const Crypto::Hash& commitment, uint64_t amount, const std::vector<uint8_t>& metadata, std::vector<uint8_t>& extra);
bool addHeatCommitmentToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraHeatCommitment& commitment);
bool getHeatCommitmentFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraHeatCommitment& commitment);
bool createTxExtraWithYieldCommitment(const Crypto::Hash& commitment, uint64_t amount, uint32_t term, const std::string& CIAId, const std::vector<uint8_t>& metadata, uint8_t claimChainCode, const std::vector<uint8_t>& gift_secret, std::vector<uint8_t>& extra);
bool addYieldCommitmentToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraYieldCommitment& commitment);
bool getYieldCommitmentFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraYieldCommitment& commitment);
bool addAliasToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraAliasRegistration& alias);
bool getAliasFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraAliasRegistration& alias);
bool addAliasReleaseToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraAliasRelease& release);
bool getAliasReleaseFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraAliasRelease& release);
bool addAliasTransferToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraAliasTransfer& transfer);
bool getAliasTransferFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraAliasTransfer& transfer);
// REMOVED: COLD commitment functions
// bool createTxExtraWithColdCommitment(const Crypto::Hash& commitment, uint64_t amount, uint32_t term, uint8_t claimChainCode, const std::vector<uint8_t>& metadata, const std::vector<uint8_t>& gift_secret, std::vector<uint8_t>& extra);
// bool addColdCommitmentToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraColdCommitment& commitment);
// bool getColdCommitmentFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraColdCommitment& commitment);
bool encryptSecretWithViewKey(const std::vector<uint8_t>& secret, const Crypto::PublicKey& recipientViewKey, std::vector<uint8_t>& gift_secret);
bool decryptSecretWithViewKey(const std::vector<uint8_t>& gift_secret, const Crypto::SecretKey& viewSecretKey, std::vector<uint8_t>& secret);
bool isDummyGiftSecret(const std::vector<uint8_t>& gift_secret);
std::vector<uint8_t> createDummyGiftSecret();
Crypto::Hash computeCommitment(const std::array<uint8_t, 32>& secret,
                                 uint64_t amount_atomic,
                                 const Crypto::Hash& tx_prefix_hash,
                                 uint32_t network_id,
                                 uint32_t target_chain_id,
                                 uint32_t commitment_version,
                                 uint32_t term);
Crypto::Hash computeHeatCommitment(const std::array<uint8_t, 32>& secret,
                                   uint64_t amount_atomic,
                                   const Crypto::Hash& tx_prefix_hash,
                                   uint32_t network_id,
                                   uint32_t target_chain_id,
                                   uint32_t commitment_version);
bool buildHeatExtra(const std::array<uint8_t, 32>& secret,
                    uint64_t amount_atomic,
                    const Crypto::Hash& tx_prefix_hash,
                    uint32_t network_id,
                    uint32_t target_chain_id,
                    uint32_t commitment_version,
                    const std::vector<uint8_t>& metadata,
                    std::vector<uint8_t>& extra);
// REMOVED: COLD commitment computation functions
// Crypto::Hash computeColdCommitment(const std::array<uint8_t, 32>& secret,
//                                    uint64_t amount_atomic,
//                                    const Crypto::Hash& tx_prefix_hash,
//                                    uint32_t network_id,
//                                    uint32_t target_chain_id,
//                                    uint32_t commitment_version,
//                                    uint32_t term);
// bool buildColdExtra(const std::array<uint8_t, 32>& secret,
//                     uint64_t amount_atomic,
//                     const Crypto::Hash& tx_prefix_hash,
//                     uint32_t network_id,
//                     uint32_t target_chain_id,
//                     uint32_t commitment_version,
//                     uint32_t term,
//                     uint8_t claimChainCode,
//                     const std::vector<uint8_t>& metadata,
//                     const std::vector<uint8_t>& gift_secret,
//                     std::vector<uint8_t>& extra);
bool getBurnReceiptFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraBurnReceipt& burnReceipt);
bool addBurnReceiptToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraBurnReceipt& burnReceipt);
bool createTxExtraWithBurnReceipt(const TransactionExtraBurnReceipt& burnReceipt, std::vector<uint8_t>& extra);
// REMOVED: COLD deposit receipt functions (uses COLD receipt tag 0x69)
// bool getDepositReceiptFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraDepositReceipt& depositReceipt);
// bool addDepositReceiptToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraDepositReceipt& depositReceipt);
// bool createTxExtraWithDepositReceipt(const TransactionExtraDepositReceipt& depositReceipt, std::vector<uint8_t>& extra);
// bool createTxExtraWithSimpleCDCommitment(const Crypto::Hash& commitment, uint64_t amount, uint32_t term, std::vector<uint8_t>& extra);
enum CDTermCode {
  CD_TERM_3MO_8PCT = 1,
  CD_TERM_9MO_18PCT = 2,
  CD_TERM_1YR_21PCT = 3,
  CD_TERM_3YR_33PCT = 4,
  CD_TERM_5YR_80PCT = 5
};
enum CDAPRRate {
  CD_APR_8PCT = 800,
  CD_APR_18PCT = 1800,
  CD_APR_21PCT = 2100,
  CD_APR_33PCT = 3300,
  CD_APR_80PCT = 8000
};

}
