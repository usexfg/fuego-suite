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

#include "TransactionExtra.h"
#include "CryptoNoteTools.h"
#include "../CryptoNoteConfig.h"
#include "../crypto/hash.h"
#include "../crypto/crypto.h"
#include "../crypto/chacha8.h"
#include "Common/int-util.h"
#include "Common/MemoryInputStream.h"
#include "Common/StreamTools.h"
#include "Common/StringTools.h"
#include "Common/Varint.h"
#include "Serialization/BinaryOutputStreamSerializer.h"
#include "Serialization/BinaryInputStreamSerializer.h"
#include "crypto/keccak.h"
#include <memory>
#include <sstream>
#include <chrono>
#include <iostream>

using namespace Crypto;
using namespace Common;

namespace CryptoNote
{

  bool parseTransactionExtra(const std::vector<uint8_t> &transactionExtra, std::vector<TransactionExtraField> &transactionExtraFields)
  {
    transactionExtraFields.clear();

    if (transactionExtra.empty())
      return true;

    try
    {
      MemoryInputStream iss(transactionExtra.data(), transactionExtra.size());
      BinaryInputStreamSerializer ar(iss);

      int c = 0;

      while (!iss.endOfStream())
      {
        c = read<uint8_t>(iss);
        switch (c)
        {
        case TX_EXTRA_TAG_PADDING:
        {
          size_t size = 1;
          for (; !iss.endOfStream() && size <= TX_EXTRA_PADDING_MAX_COUNT; ++size)
          {
            if (read<uint8_t>(iss) != 0)
            {
              return false;
            }
          }

          if (size > TX_EXTRA_PADDING_MAX_COUNT)
          {
            return false;
          }

          transactionExtraFields.push_back(TransactionExtraPadding{size});
          break;
        }

        case TX_EXTRA_TAG_PUBKEY:
        {
          TransactionExtraPublicKey extraPk;
          ar(extraPk.publicKey, "public_key");
          transactionExtraFields.push_back(extraPk);
          break;
        }

        case TX_EXTRA_NONCE:
        {
          TransactionExtraNonce extraNonce;
          uint8_t size = read<uint8_t>(iss);
          if (size > 0)
          {
            extraNonce.nonce.resize(size);
            read(iss, extraNonce.nonce.data(), extraNonce.nonce.size());
          }

          transactionExtraFields.push_back(extraNonce);
          break;
        }

        case TX_EXTRA_MERGE_MINING_TAG:
        {
          TransactionExtraMergeMiningTag mmTag;
          ar(mmTag, "mm_tag");
          transactionExtraFields.push_back(mmTag);
          break;
        }

        case TX_EXTRA_MESSAGE_TAG:
        {
          tx_extra_message message;
          ar(message.data, "message");
          transactionExtraFields.push_back(message);
          break;
        }

        case TX_EXTRA_TTL:
        {
          uint8_t size;
          readVarint(iss, size);
          TransactionExtraTTL ttl;
          readVarint(iss, ttl.ttl);
          transactionExtraFields.push_back(ttl);
          break;
        }

        case TX_EXTRA_HEAT_COMMITMENT:
        {
          TransactionExtraHeatCommitment heatCommitment;
          read(iss, heatCommitment.commitment.data, sizeof(heatCommitment.commitment.data));
          heatCommitment.amount = 0;
          for (int i = 0; i < 8; ++i) {
            heatCommitment.amount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
          }
          uint8_t heatMetaSize = read<uint8_t>(iss);
          if (heatMetaSize > 0) {
            heatCommitment.metadata.resize(heatMetaSize);
            read(iss, heatCommitment.metadata.data(), heatMetaSize);
          }
          transactionExtraFields.push_back(heatCommitment);
          break;
        }
        // REMOVED: COLD migration parsing
        // case TX_EXTRA_COLD_MIGRATION:
        // {
        //   TransactionExtraColdMigration migration;
        //   read(iss, migration.originalTxHash.data, sizeof(migration.originalTxHash.data));
        //   read(iss, migration.commitment.data, sizeof(migration.commitment.data));
        //   migration.amount = 0;
        //   for (int i = 0; i < 8; ++i) {
        //     migration.amount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
        //   }
        //   migration.term = 0;
        //   for (int i = 0; i < 4; ++i) {
        //     migration.term |= static_cast<uint32_t>(read<uint8_t>(iss)) << (i * 8);
        //   }
        //   migration.targetChainCode = read<uint8_t>(iss);
        //   transactionExtraFields.push_back(migration);
        //   break;
        // }

        case TX_EXTRA_LEGACY_BOND:
        {
          TransactionExtraLegacyBond bond;
          read(iss, bond.originalTxHash.data, sizeof(bond.originalTxHash.data));
          bond.amount = 0;
          for (int i = 0; i < 8; ++i) {
            bond.amount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
          }
          bond.originalCreationHeight = 0;
          for (int i = 0; i < 4; ++i) {
            bond.originalCreationHeight |= static_cast<uint32_t>(read<uint8_t>(iss)) << (i * 8);
          }
          transactionExtraFields.push_back(bond);
          break;
        }

        case TX_EXTRA_LEGACY_BOND_CLAIM:
        {
          TransactionExtraLegacyBondClaim claim;
          claim.claimedInterest = 0;
          for (int i = 0; i < 8; ++i) {
            claim.claimedInterest |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
          }
          transactionExtraFields.push_back(claim);
          break;
        }

        case TX_EXTRA_BURN_RECEIPT:
        {
          TransactionExtraBurnReceipt burnReceipt;
          if (getBurnReceiptFromExtra(transactionExtra, burnReceipt)) {
            transactionExtraFields.push_back(burnReceipt);
          } else {
            return false;
          }
          break;
        }

         case TX_EXTRA_DEPOSIT_SECRET:
         {
           uint8_t dsLen = read<uint8_t>(iss);
           if (dsLen > 0) {
             for (uint8_t i = 0; i < dsLen; ++i)
               read<uint8_t>(iss);
           }
           break;
         }

         case TX_EXTRA_AMM_SWAP:
         {
           TransactionExtraAmmSwap swap;
           swap.direction = read<uint8_t>(iss);
           swap.inputAmount = 0;
           for (int i = 0; i < 8; ++i)
             swap.inputAmount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
           swap.minOutput = 0;
           for (int i = 0; i < 8; ++i)
             swap.minOutput |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
           transactionExtraFields.push_back(swap);
           break;
         }

         case TX_EXTRA_AMM_ADD_LIQ:
         {
           TransactionExtraAmmAddLiquidity add;
           add.amountXfg = 0;
           for (int i = 0; i < 8; ++i)
             add.amountXfg |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
           add.amountHeat = 0;
           for (int i = 0; i < 8; ++i)
             add.amountHeat |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
           transactionExtraFields.push_back(add);
           break;
         }

          case TX_EXTRA_AMM_REM_LIQ:
          {
            TransactionExtraAmmRemoveLiquidity rem;
            rem.lpSharesBurned = 0;
            for (int i = 0; i < 8; ++i)
              rem.lpSharesBurned |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
            rem.minAmountXfg = 0;
            for (int i = 0; i < 8; ++i)
              rem.minAmountXfg |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
            rem.minAmountHeat = 0;
            for (int i = 0; i < 8; ++i)
              rem.minAmountHeat |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
            transactionExtraFields.push_back(rem);
            break;
          }

          case TX_EXTRA_AMM_COMPOUND:
          {
            TransactionExtraAmmCompound compound;
            transactionExtraFields.push_back(compound);
            break;
          }

          case TX_EXTRA_AMM_CLAIM:
          {
            TransactionExtraAmmClaim claim;
            claim.lpShares = 0;
            for (int i = 0; i < 8; ++i)
              claim.lpShares |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
            claim.minAmountXfg = 0;
            for (int i = 0; i < 8; ++i)
              claim.minAmountXfg |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
            claim.minAmountHeat = 0;
            for (int i = 0; i < 8; ++i)
              claim.minAmountHeat |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
            transactionExtraFields.push_back(claim);
             break;
           }

           case TX_EXTRA_HEAT_MINT_AUTH:
           {
             TransactionExtraHeatMintAuth auth;
             auth.xfgBurned = 0;
             for (int i = 0; i < 8; ++i)
               auth.xfgBurned |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
             auth.heatMinted = 0;
             for (int i = 0; i < 8; ++i)
               auth.heatMinted |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
             transactionExtraFields.push_back(auth);
              break;
            }

            case TX_EXTRA_HEAT_SEND_AUTH:
            {
              TransactionExtraHeatSendAuth auth;
              auth.heatAmount = 0;
              for (int i = 0; i < 8; ++i)
                auth.heatAmount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
              transactionExtraFields.push_back(auth);
              break;
            }

            case TX_EXTRA_AMM_SWAP_AUTH:
           {
             TransactionExtraAmmSwapAuth auth;
             auth.direction = read<uint8_t>(iss);
             auth.inputAmount = 0;
             for (int i = 0; i < 8; ++i)
               auth.inputAmount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
             auth.outputAmount = 0;
             for (int i = 0; i < 8; ++i)
               auth.outputAmount |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
             auth.minOutput = 0;
             for (int i = 0; i < 8; ++i)
               auth.minOutput |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
             transactionExtraFields.push_back(auth);
             break;
           }

           case TX_EXTRA_AMM_LP_ADD_AUTH:
           {
             TransactionExtraLpAddAuth auth;
             auth.amountXfg = 0;
             for (int i = 0; i < 8; ++i)
               auth.amountXfg |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
             auth.amountHeat = 0;
             for (int i = 0; i < 8; ++i)
               auth.amountHeat |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
             auth.lpShares = 0;
             for (int i = 0; i < 8; ++i)
               auth.lpShares |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
             transactionExtraFields.push_back(auth);
             break;
           }

            case TX_EXTRA_AMM_LP_REM_AUTH:
            {
              TransactionExtraLpRemoveAuth auth;
              auth.lpSharesBurned = 0;
              for (int i = 0; i < 8; ++i)
                auth.lpSharesBurned |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
              auth.minAmountXfg = 0;
              for (int i = 0; i < 8; ++i)
                auth.minAmountXfg |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
              auth.minAmountHeat = 0;
              for (int i = 0; i < 8; ++i)
                auth.minAmountHeat |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
              transactionExtraFields.push_back(auth);
              break;
            }

            case TX_EXTRA_ORDER_PLACE:
            {
              TransactionExtraOrderPlace order;
              order.side = read<uint8_t>(iss);
              order.price = 0;
              for (int i = 0; i < 8; ++i)
                order.price |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
              order.expiration = 0;
              for (int i = 0; i < 4; ++i)
                order.expiration |= static_cast<uint32_t>(read<uint8_t>(iss)) << (i * 8);
              transactionExtraFields.push_back(order);
              break;
            }

            case TX_EXTRA_ORDER_CANCEL:
            {
              TransactionExtraOrderCancel cancel;
              read(iss, cancel.orderId.data, sizeof(cancel.orderId.data));
              transactionExtraFields.push_back(cancel);
              break;
            }

            case TX_EXTRA_MARKET_BUY_AUTH:
            {
              TransactionExtraMarketBuyAuth auth;
              auth.xfgWanted = 0;
              for (int i = 0; i < 8; ++i)
                auth.xfgWanted |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
              auth.maxHeatCost = 0;
              for (int i = 0; i < 8; ++i)
                auth.maxHeatCost |= static_cast<uint64_t>(read<uint8_t>(iss)) << (i * 8);
              transactionExtraFields.push_back(auth);
              break;
            }

            case TX_EXTRA_MARKET_SELL_AUTH:
            {
              TransactionExtraMarketSellAuth auth;
              readVarint(iss, auth.xfgToSell);
              readVarint(iss, auth.minHeatReceive);
              transactionExtraFields.push_back(auth);
              break;
            }

            case TX_EXTRA_LIMIT_DEPOSIT:
            {
              TransactionExtraLimitDeposit deposit;
              deposit.side = read<uint8_t>(iss);
              readVarint(iss, deposit.amount);
              readVarint(iss, deposit.targetPrice);
              readVarint(iss, deposit.expiration);
              read(iss, deposit.orderId.data, sizeof(deposit.orderId.data));
              read(iss, deposit.addressHash.data, sizeof(deposit.addressHash.data));
              transactionExtraFields.push_back(deposit);
              break;
            }

            case TX_EXTRA_LIMIT_WITHDRAW:
            {
              TransactionExtraLimitWithdraw withdraw;
              read(iss, withdraw.orderId.data, sizeof(withdraw.orderId.data));
              transactionExtraFields.push_back(withdraw);
              break;
            }

            case TX_EXTRA_ALIAS_RELEASE:
           {
             TransactionExtraAliasRelease release;
             if (getAliasReleaseFromExtra(transactionExtra, release)) {
               transactionExtraFields.push_back(release);
             } else {
               return false;
             }
             break;
           }

           case TX_EXTRA_ALIAS_TRANSFER:
           {
             TransactionExtraAliasTransfer transfer;
             if (getAliasTransferFromExtra(transactionExtra, transfer)) {
               transactionExtraFields.push_back(transfer);
             } else {
               return false;
             }
             break;
           }

           default:
             return false;
       }
     }
     }
     catch (std::exception &)
    {
      return false;
    }

    return true;
  }

  struct ExtraSerializerVisitor : public boost::static_visitor<bool>
  {
    std::vector<uint8_t> &extra;

    ExtraSerializerVisitor(std::vector<uint8_t> &tx_extra)
        : extra(tx_extra) {}

    bool operator()(const TransactionExtraPadding &t)
    {
      if (t.size > TX_EXTRA_PADDING_MAX_COUNT)
      {
        return false;
      }
      extra.insert(extra.end(), t.size, 0);
      return true;
    }

    bool operator()(const TransactionExtraPublicKey &t)
    {
      return addTransactionPublicKeyToExtra(extra, t.publicKey);
    }

    bool operator()(const TransactionExtraNonce &t)
    {
      return addExtraNonceToTransactionExtra(extra, t.nonce);
    }

    bool operator()(const TransactionExtraMergeMiningTag &t)
    {
      return appendMergeMiningTagToExtra(extra, t);
    }

    bool operator()(const tx_extra_message &t)
    {
      return append_message_to_extra(extra, t);
    }

    bool operator()(const TransactionExtraTTL &t)
    {
      appendTTLToExtra(extra, t.ttl);
      return true;
    }

    bool operator()(const TransactionExtraHeatCommitment &t)
    {
      return addHeatCommitmentToExtra(extra, t);
    }

    bool operator()(const TransactionExtraBurnReceipt &t)
    {
      return addBurnReceiptToExtra(extra, t);
    }
    
    // REMOVED: COLD SimpleCD writer
    // bool operator()(const TransactionExtraSimpleCD &t)
    // {
    //   extra.push_back(TX_EXTRA_SIMPLE_CD);
    //   extra.insert(extra.end(), t.commitment.data, t.commitment.data + sizeof(t.commitment.data));
    //   uint64_t amount = t.amount;
    //   for (int i = 0; i < 8; ++i) { extra.push_back(static_cast<uint8_t>(amount & 0xFF)); amount >>= 8; }
    //   uint32_t term = t.term;
    //   for (int i = 0; i < 4; ++i) { extra.push_back(static_cast<uint8_t>(term & 0xFF)); term >>= 8; }
    //   return true;
    // }

    // REMOVED: COLD commitment writer
    // bool operator()(const TransactionExtraColdCommitment &t)
    // {
    //   return addColdCommitmentToExtra(extra, t);
    // }

    // REMOVED: COLD deposit receipt — TransactionExtraDepositReceipt removed from variant

    bool operator()(const TransactionExtraAliasRegistration &t)
    {
      return addAliasToExtra(extra, t);
    }

    bool operator()(const TransactionExtraAliasRelease &t)
    {
      return addAliasReleaseToExtra(extra, t);
    }

    bool operator()(const TransactionExtraAliasTransfer &t)
    {
      return addAliasTransferToExtra(extra, t);
    }

    // REMOVED: COLD migration writer
    // bool operator()(const TransactionExtraColdMigration &t)
    // {
    //   return addColdMigrationToExtra(extra, t);
    // }

    bool operator()(const TransactionExtraLegacyBond &t)
    {
      return addLegacyBondToExtra(extra, t);
    }

    bool operator()(const TransactionExtraLegacyBondClaim &t)
    {
      return addLegacyBondClaimToExtra(extra, t);
    }

    bool operator()(const TransactionExtraAmmSwap &t)
    {
      return addAmmSwapToExtra(extra, t.direction, t.inputAmount, t.minOutput);
    }

    bool operator()(const TransactionExtraAmmAddLiquidity &t)
    {
      return addAmmAddLiquidityToExtra(extra, t.amountXfg, t.amountHeat);
    }

    bool operator()(const TransactionExtraAmmRemoveLiquidity &t)
    {
      return addAmmRemoveLiquidityToExtra(extra, t.lpSharesBurned, t.minAmountXfg, t.minAmountHeat);
    }

    bool operator()(const TransactionExtraAmmCompound &t)
    {
      return addAmmCompoundToExtra(extra);
    }

    bool operator()(const TransactionExtraAmmClaim &t)
    {
      return addAmmClaimToExtra(extra, t.lpShares, t.minAmountXfg, t.minAmountHeat);
    }

    bool operator()(const TransactionExtraHeatMintAuth &t)
    {
      return addHeatMintAuthToExtra(extra, t.xfgBurned, t.heatMinted);
    }

    bool operator()(const TransactionExtraHeatSendAuth &t)
    {
      return addHeatSendAuthToExtra(extra, t.heatAmount);
    }

    bool operator()(const TransactionExtraAmmSwapAuth &t)
    {
      return addAmmSwapAuthToExtra(extra, t.direction, t.inputAmount, t.outputAmount,
                                   t.minOutput);
    }

    bool operator()(const TransactionExtraLpAddAuth &t)
    {
      return addLpAddAuthToExtra(extra, t.amountXfg, t.amountHeat, t.lpShares);
    }

    bool operator()(const TransactionExtraLpRemoveAuth &t)
    {
      return addLpRemoveAuthToExtra(extra, t.lpSharesBurned, t.minAmountXfg, t.minAmountHeat);
    }

    bool operator()(const TransactionExtraOrderPlace &t)
    {
      return addOrderPlaceToExtra(extra, t.side, t.price, t.expiration);
    }

    bool operator()(const TransactionExtraOrderCancel &t)
    {
      return addOrderCancelToExtra(extra, t.orderId);
    }

    bool operator()(const TransactionExtraMarketBuyAuth &t)
    {
      return addMarketBuyAuthToExtra(extra, t.xfgWanted, t.maxHeatCost);
    }

    bool operator()(const TransactionExtraMarketSellAuth &t)
    {
      return addMarketSellAuthToExtra(extra, t.xfgToSell, t.minHeatReceive);
    }

    bool operator()(const TransactionExtraLimitDeposit &t)
    {
      return addLimitDepositToExtra(extra, t.side, t.amount, t.targetPrice, t.expiration, t.orderId, t.addressHash);
    }

    bool operator()(const TransactionExtraLimitWithdraw &t)
    {
      return addLimitWithdrawToExtra(extra, t.orderId);
    }

  };

  bool writeTransactionExtra(std::vector<uint8_t> &tx_extra, const std::vector<TransactionExtraField> &tx_extra_fields)
  {
    ExtraSerializerVisitor visitor(tx_extra);

    for (const auto &tag : tx_extra_fields)
    {
      if (!boost::apply_visitor(visitor, tag))
      {
        return false;
      }
    }

    return true;
  }

  PublicKey getTransactionPublicKeyFromExtra(const std::vector<uint8_t> &tx_extra)
  {
    std::vector<TransactionExtraField> tx_extra_fields;
    parseTransactionExtra(tx_extra, tx_extra_fields);

    TransactionExtraPublicKey pub_key_field;
    if (!findTransactionExtraFieldByType(tx_extra_fields, pub_key_field))
      return boost::value_initialized<PublicKey>();

    return pub_key_field.publicKey;
  }

  bool addTransactionPublicKeyToExtra(std::vector<uint8_t> &tx_extra, const PublicKey &tx_pub_key)
  {
    tx_extra.resize(tx_extra.size() + 1 + sizeof(PublicKey));
    tx_extra[tx_extra.size() - 1 - sizeof(PublicKey)] = TX_EXTRA_TAG_PUBKEY;
    *reinterpret_cast<PublicKey *>(&tx_extra[tx_extra.size() - sizeof(PublicKey)]) = tx_pub_key;
    return true;
  }

  bool addExtraNonceToTransactionExtra(std::vector<uint8_t> &tx_extra, const BinaryArray &extra_nonce)
  {
    if (extra_nonce.size() > TX_EXTRA_NONCE_MAX_COUNT)
    {
      return false;
    }

    size_t start_pos = tx_extra.size();
    tx_extra.resize(tx_extra.size() + 2 + extra_nonce.size());
    tx_extra[start_pos] = TX_EXTRA_NONCE;
    ++start_pos;
    tx_extra[start_pos] = static_cast<uint8_t>(extra_nonce.size());
    ++start_pos;
    memcpy(&tx_extra[start_pos], extra_nonce.data(), extra_nonce.size());
    return true;
  }

  bool appendMergeMiningTagToExtra(std::vector<uint8_t> &tx_extra, const TransactionExtraMergeMiningTag &mm_tag)
  {
    BinaryArray blob;
    if (!toBinaryArray(mm_tag, blob))
    {
      return false;
    }

    tx_extra.push_back(TX_EXTRA_MERGE_MINING_TAG);
    std::copy(reinterpret_cast<const uint8_t *>(blob.data()), reinterpret_cast<const uint8_t *>(blob.data() + blob.size()), std::back_inserter(tx_extra));
    return true;
  }

  bool getMergeMiningTagFromExtra(const std::vector<uint8_t> &tx_extra, TransactionExtraMergeMiningTag &mm_tag)
  {
    std::vector<TransactionExtraField> tx_extra_fields;
    parseTransactionExtra(tx_extra, tx_extra_fields);

    return findTransactionExtraFieldByType(tx_extra_fields, mm_tag);
  }

  bool append_message_to_extra(std::vector<uint8_t> &tx_extra, const tx_extra_message &message)
  {
    BinaryArray blob;
    if (!toBinaryArray(message, blob))
    {
      return false;
    }

    tx_extra.reserve(tx_extra.size() + 1 + blob.size());
    tx_extra.push_back(TX_EXTRA_MESSAGE_TAG);
    std::copy(reinterpret_cast<const uint8_t *>(blob.data()), reinterpret_cast<const uint8_t *>(blob.data() + blob.size()), std::back_inserter(tx_extra));

    return true;
  }

  std::vector<std::string> get_messages_from_extra(const std::vector<uint8_t> &extra, const Crypto::PublicKey &txkey, const Crypto::SecretKey *recepient_secret_key)
  {
    std::vector<TransactionExtraField> tx_extra_fields;
    std::vector<std::string> result;
    if (!parseTransactionExtra(extra, tx_extra_fields))
    {
      return result;
    }
    size_t i = 0;
    for (const auto &f : tx_extra_fields)
    {
      if (f.type() != typeid(tx_extra_message))
      {
        continue;
      }
      std::string res;
      if (boost::get<tx_extra_message>(f).decrypt(i, txkey, recepient_secret_key, res))
      {
        result.push_back(res);
      }
      ++i;
    }
    return result;
  }

  void appendTTLToExtra(std::vector<uint8_t> &tx_extra, uint64_t ttl)
  {
    std::string ttlData = Tools::get_varint_data(ttl);
    std::string extraFieldSize = Tools::get_varint_data(ttlData.size());

    tx_extra.reserve(tx_extra.size() + 1 + extraFieldSize.size() + ttlData.size());
    tx_extra.push_back(TX_EXTRA_TTL);
    std::copy(extraFieldSize.begin(), extraFieldSize.end(), std::back_inserter(tx_extra));
    std::copy(ttlData.begin(), ttlData.end(), std::back_inserter(tx_extra));
  }

  void setPaymentIdToTransactionExtraNonce(std::vector<uint8_t> &extra_nonce, const Hash &payment_id)
  {
    extra_nonce.clear();
    extra_nonce.push_back(TX_EXTRA_NONCE_PAYMENT_ID);
    const uint8_t *payment_id_ptr = reinterpret_cast<const uint8_t *>(&payment_id);
    std::copy(payment_id_ptr, payment_id_ptr + sizeof(payment_id), std::back_inserter(extra_nonce));
  }

  bool getPaymentIdFromTransactionExtraNonce(const std::vector<uint8_t> &extra_nonce, Hash &payment_id)
  {
    if (sizeof(Hash) + 1 != extra_nonce.size())
      return false;
    if (TX_EXTRA_NONCE_PAYMENT_ID != extra_nonce[0])
      return false;
    payment_id = *reinterpret_cast<const Hash *>(extra_nonce.data() + 1);
    return true;
  }

  bool parsePaymentId(const std::string &paymentIdString, Hash &paymentId)
  {
    return Common::podFromHex(paymentIdString, paymentId);
  }

  bool createTxExtraWithPaymentId(const std::string &paymentIdString, std::vector<uint8_t> &extra)
  {
    Hash paymentIdBin;

    if (!parsePaymentId(paymentIdString, paymentIdBin))
    {
      return false;
    }

    std::vector<uint8_t> extraNonce;
    CryptoNote::setPaymentIdToTransactionExtraNonce(extraNonce, paymentIdBin);

    if (!CryptoNote::addExtraNonceToTransactionExtra(extra, extraNonce))
    {
      return false;
    }

    return true;
  }

  bool getPaymentIdFromTxExtra(const std::vector<uint8_t> &extra, Hash &paymentId)
  {
    std::vector<TransactionExtraField> tx_extra_fields;
    if (!parseTransactionExtra(extra, tx_extra_fields))
    {
      return false;
    }

    TransactionExtraNonce extra_nonce;
    if (findTransactionExtraFieldByType(tx_extra_fields, extra_nonce))
    {
      if (!getPaymentIdFromTransactionExtraNonce(extra_nonce.nonce, paymentId))
      {
        return false;
      }
    }
    else
    {
      return false;
    }

    return true;
  }

#define TX_EXTRA_MESSAGE_CHECKSUM_SIZE 4

#pragma pack(push, 1)
  struct message_key_data
  {
    KeyDerivation derivation;
    uint8_t magic1, magic2;
  };
#pragma pack(pop)
  static_assert(sizeof(message_key_data) == 34, "Invalid structure size");

  bool tx_extra_message::encrypt(size_t index, const std::string &message, const AccountPublicAddress *recipient, const KeyPair &txkey)
  {
    size_t mlen = message.size();
    std::unique_ptr<char[]> buf(new char[mlen + TX_EXTRA_MESSAGE_CHECKSUM_SIZE]);
    memcpy(buf.get(), message.data(), mlen);
    memset(buf.get() + mlen, 0, TX_EXTRA_MESSAGE_CHECKSUM_SIZE);
    mlen += TX_EXTRA_MESSAGE_CHECKSUM_SIZE;
    if (recipient)
    {
      message_key_data key_data;
      if (!generate_key_derivation(recipient->spendPublicKey, txkey.secretKey, key_data.derivation))
      {
        return false;
      }
      key_data.magic1 = 0x80;
      key_data.magic2 = 0;
      Hash h = cn_fast_hash(&key_data, sizeof(message_key_data));
      uint64_t nonce = SWAP64LE(index);
      chacha8(buf.get(), mlen, reinterpret_cast<uint8_t *>(&h), reinterpret_cast<uint8_t *>(&nonce), buf.get());
    }
    data.assign(buf.get(), mlen);
    return true;
  }

  bool tx_extra_message::decrypt(size_t index, const Crypto::PublicKey &txkey, const Crypto::SecretKey *recepient_secret_key, std::string &message) const
  {
    size_t mlen = data.size();
    if (mlen < TX_EXTRA_MESSAGE_CHECKSUM_SIZE)
    {
      return false;
    }
    const char *buf;
    std::unique_ptr<char[]> ptr;
    if (recepient_secret_key != nullptr)
    {
      ptr.reset(new char[mlen]);
      assert(ptr);
      message_key_data key_data;
      if (!generate_key_derivation(txkey, *recepient_secret_key, key_data.derivation))
      {
        return false;
      }
      key_data.magic1 = 0x80;
      key_data.magic2 = 0;
      Hash h = cn_fast_hash(&key_data, sizeof(message_key_data));
      uint64_t nonce = SWAP64LE(index);
      chacha8(data.data(), mlen, reinterpret_cast<uint8_t *>(&h), reinterpret_cast<uint8_t *>(&nonce), ptr.get());
      buf = ptr.get();
    }
    else
    {
      buf = data.data();
    }
    mlen -= TX_EXTRA_MESSAGE_CHECKSUM_SIZE;
    for (size_t i = 0; i < TX_EXTRA_MESSAGE_CHECKSUM_SIZE; i++)
    {
      if (buf[mlen + i] != 0)
      {
        return false;
      }
    }
    message.assign(buf, mlen);
    return true;
  }

  bool tx_extra_message::serialize(ISerializer &s)
  {
    s(data, "data");
    return true;
  }

  bool TransactionExtraHeatCommitment::serialize(ISerializer &s)
  {
    s(commitment, "commitment");
    s(amount, "amount");
    s(metadata, "metadata");
    return true;
  }

  bool TransactionExtraYieldCommitment::serialize(ISerializer &s)
  {
    s(commitment, "commitment");
    s(amount, "amount");
    s(term, "term");
    s(claimChainCode, "claimChainCode");
    s(CIAId, "CIAId");
    s(metadata, "metadata");
    s(gift_secret, "gift_secret");
    return true;
  }

  // REMOVED: COLD commitment serialize
  // bool TransactionExtraColdCommitment::serialize(ISerializer &s)
  // {
  //   s(commitment, "commitment");
  //   s(amount, "amount");
  //   s(term, "term");
  //   s(claimChainCode, "claimChainCode");
  //   s(metadata, "metadata");
  //   s(gift_secret, "gift_secret");
  //   return true;
  // }

  // REMOVED: COLD migration serialize
  // bool TransactionExtraColdMigration::serialize(ISerializer &s)
  // {
  //   s(originalTxHash, "originalTxHash");
  //   s(commitment, "commitment");
  //   s(amount, "amount");
  //   s(term, "term");
  //   s(targetChainId, "targetChainId");
  //   return true;
  // }

  bool TransactionExtraLegacyBond::serialize(ISerializer &s)
  {
    s(originalTxHash, "originalTxHash");
    s(amount, "amount");
    s(originalCreationHeight, "originalCreationHeight");
    return true;
  }

  bool TransactionExtraLegacyBondClaim::serialize(ISerializer &s)
  {
    s(claimedInterest, "claimedInterest");
    return true;
  }

  bool TransactionExtraAliasRegistration::serialize(ISerializer &s)
  {
    s(version, "version");
    s(alias, "alias");
    s(aliasHash, "aliasHash");
    s(addressHash, "addressHash");
    s(ownerAddress, "ownerAddress");
    s(aliasType, "aliasType");
    s(networkId, "networkId");
    return true;
  }

  bool TransactionExtraAliasRegistration::isValid() const
  {
    return !alias.empty() && !ownerAddress.empty();
  }

  bool TransactionExtraAliasRelease::serialize(ISerializer &s)
  {
    s(version, "version");
    s(alias, "alias");
    s(aliasHash, "aliasHash");
    s(ownerAddress, "ownerAddress");
    s(proof, "proof");
    return true;
  }

  bool TransactionExtraAliasRelease::isValid() const
  {
    return !alias.empty() && !ownerAddress.empty();
  }

  bool TransactionExtraAliasTransfer::serialize(ISerializer &s)
  {
    s(version, "version");
    s(alias, "alias");
    s(aliasHash, "aliasHash");
    s(oldOwnerAddress, "oldOwnerAddress");
    s(newOwnerAddress, "newOwnerAddress");
    s(newAddressHash, "newAddressHash");
    s(proof, "proof");
    return true;
  }

  bool TransactionExtraAliasTransfer::isValid() const
  {
    return !alias.empty() && !oldOwnerAddress.empty() && !newOwnerAddress.empty();
  }

  bool addHeatCommitmentToExtra(std::vector<uint8_t> &tx_extra, const TransactionExtraHeatCommitment &commitment)
  {
    tx_extra.push_back(TX_EXTRA_HEAT_COMMITMENT);
    tx_extra.insert(tx_extra.end(), commitment.commitment.data, commitment.commitment.data + sizeof(commitment.commitment.data));
    uint64_t amount = commitment.amount;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF));
      amount >>= 8;
    }
    size_t rawSize = commitment.metadata.size();
    uint8_t metadataSize = static_cast<uint8_t>(rawSize > 128 ? 128 : rawSize);
    tx_extra.push_back(metadataSize);
    if (metadataSize > 0) {
      tx_extra.insert(tx_extra.end(), commitment.metadata.begin(), commitment.metadata.begin() + metadataSize);
    }
    return true;
  }

  bool createTxExtraWithHeatCommitment(const Crypto::Hash &commitment, uint64_t amount, const std::vector<uint8_t> &metadata, std::vector<uint8_t> &extra)
  {
    TransactionExtraHeatCommitment heatCommitment;
    heatCommitment.commitment = commitment;
    heatCommitment.amount = amount;
    heatCommitment.metadata = metadata;
    return addHeatCommitmentToExtra(extra, heatCommitment);
  }

  bool getHeatCommitmentFromExtra(const std::vector<uint8_t> &tx_extra, TransactionExtraHeatCommitment &commitment)
  {
    size_t pos = 0;
    bool found = false;
    while (pos < tx_extra.size()) {
      if (tx_extra[pos] == TX_EXTRA_HEAT_COMMITMENT) {
        found = true;
        pos++;
        break;
      }
      pos++;
    }
    if (!found) return false;
    if (pos + 32 > tx_extra.size()) return false;
    std::memcpy(commitment.commitment.data, &tx_extra[pos], 32);
    pos += 32;
    if (pos + 8 > tx_extra.size()) return false;
    commitment.amount = 0;
    for (int i = 0; i < 8; ++i) {
      commitment.amount |= static_cast<uint64_t>(tx_extra[pos + i]) << (i * 8);
    }
    pos += 8;
    if (pos >= tx_extra.size()) return false;
    uint8_t metadataSize = tx_extra[pos];
    pos += 1;
    if (metadataSize > 0) {
      if (pos + metadataSize > tx_extra.size()) return false;
      commitment.metadata.assign(tx_extra.begin() + pos, tx_extra.begin() + pos + metadataSize);
    } else {
      commitment.metadata.clear();
    }
    return true;
  }

  bool addYieldCommitmentToExtra(std::vector<uint8_t> &tx_extra, const TransactionExtraYieldCommitment &commitment)
  {
    tx_extra.push_back(TX_EXTRA_YIELD_COMMITMENT);
    tx_extra.insert(tx_extra.end(), commitment.commitment.data, commitment.commitment.data + sizeof(commitment.commitment.data));
    uint64_t amount = commitment.amount;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF));
      amount >>= 8;
    }
    uint32_t term = commitment.term;
    for (int i = 0; i < 4; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(term & 0xFF));
      term >>= 8;
    }
    tx_extra.push_back(commitment.claimChainCode);
    uint8_t assetIdLen = static_cast<uint8_t>(commitment.CIAId.size());
    tx_extra.push_back(assetIdLen);
    tx_extra.insert(tx_extra.end(), commitment.CIAId.begin(), commitment.CIAId.end());
    uint8_t metadataSize = static_cast<uint8_t>(commitment.metadata.size());
    tx_extra.push_back(metadataSize);
    if (metadataSize > 0) {
      tx_extra.insert(tx_extra.end(), commitment.metadata.begin(), commitment.metadata.end());
    }
    uint8_t giftSecretSize = static_cast<uint8_t>(commitment.gift_secret.size());
    tx_extra.push_back(giftSecretSize);
    if (giftSecretSize > 0) {
      tx_extra.insert(tx_extra.end(), commitment.gift_secret.begin(), commitment.gift_secret.end());
    }
    return true;
}

  bool createTxExtraWithYieldCommitment(const Crypto::Hash &commitment, uint64_t amount, uint32_t term, const std::string &CIAId, const std::vector<uint8_t> &metadata, uint8_t claimChainCode, const std::vector<uint8_t> &gift_secret, std::vector<uint8_t> &extra)
  {
    TransactionExtraYieldCommitment yieldCommitment;
    yieldCommitment.commitment = commitment;
    yieldCommitment.amount = amount;
    yieldCommitment.term = term;
    yieldCommitment.CIAId = CIAId;
    yieldCommitment.metadata = metadata;
    yieldCommitment.claimChainCode = claimChainCode;
    yieldCommitment.gift_secret = gift_secret;
    return addYieldCommitmentToExtra(extra, yieldCommitment);
  }

  bool getYieldCommitmentFromExtra(const std::vector<uint8_t> &tx_extra, TransactionExtraYieldCommitment &commitment)
  {
    if (tx_extra.empty() || tx_extra[0] != TX_EXTRA_YIELD_COMMITMENT) {
      return false;
    }
    size_t pos = 1;
    if (pos + 32 > tx_extra.size()) return false;
    std::memcpy(commitment.commitment.data, &tx_extra[pos], 32);
    pos += 32;
    if (pos + 8 > tx_extra.size()) return false;
    pos += 8;
    if (pos + 4 > tx_extra.size()) return false;
    commitment.term = 0;
    for (int i = 0; i < 4; ++i) {
      commitment.term |= static_cast<uint32_t>(tx_extra[pos + i]) << (i * 8);
    }
    pos += 4;
    if (pos >= tx_extra.size()) return false;
    commitment.claimChainCode = tx_extra[pos];
    pos += 1;
    if (pos >= tx_extra.size()) return false;
    uint8_t assetIdLen = tx_extra[pos];
    pos += 1;
    if (pos + assetIdLen > tx_extra.size()) return false;
    if (assetIdLen > 0) {
      commitment.CIAId.assign(reinterpret_cast<const char*>(&tx_extra[pos]), assetIdLen);
      pos += assetIdLen;
    } else {
      commitment.CIAId.clear();
    }
    if (pos >= tx_extra.size()) return false;
    uint8_t metadataSize = tx_extra[pos];
    pos += 1;
    if (pos + metadataSize > tx_extra.size()) return false;
    if (metadataSize > 0) {
      commitment.metadata.assign(&tx_extra[pos], &tx_extra[pos] + metadataSize);
      pos += metadataSize;
    } else {
      commitment.metadata.clear();
    }
    if (pos >= tx_extra.size()) return false;
    uint8_t giftSecretSize = tx_extra[pos];
    pos += 1;
    if (pos + giftSecretSize > tx_extra.size()) return false;
    if (giftSecretSize > 0) {
      commitment.gift_secret.assign(&tx_extra[pos], &tx_extra[pos] + giftSecretSize);
    } else {
      commitment.gift_secret.clear();
    }
    return true;
  }

  Crypto::Hash computeCommitment(const std::array<uint8_t, 32> &secret,
                                 uint64_t amount_atomic,
                                 const Crypto::Hash &tx_prefix_hash,
                                 uint32_t network_id,
                                 uint32_t target_chain_id,
                                 uint32_t commitment_version,
                                 uint32_t term)
  {
    std::vector<uint8_t> preimage;
    preimage.reserve(88);
    preimage.insert(preimage.end(), secret.begin(), secret.end());
    uint64_t amt = amount_atomic;
    for (int i = 0; i < 8; ++i) {
      preimage.push_back(static_cast<uint8_t>(amt & 0xFF));
      amt >>= 8;
    }
    preimage.insert(preimage.end(), reinterpret_cast<const uint8_t*>(&tx_prefix_hash), reinterpret_cast<const uint8_t*>(&tx_prefix_hash) + sizeof(tx_prefix_hash));
    uint32_t net_id = network_id;
    for (int i = 0; i < 4; ++i) {
      preimage.push_back(static_cast<uint8_t>(net_id & 0xFF));
      net_id >>= 8;
    }
    uint32_t target_id = target_chain_id;
    for (int i = 0; i < 4; ++i) {
      preimage.push_back(static_cast<uint8_t>(target_id & 0xFF));
      target_id >>= 8;
    }
    uint32_t version = commitment_version;
    for (int i = 0; i < 4; ++i) {
      preimage.push_back(static_cast<uint8_t>(version & 0xFF));
      version >>= 8;
    }
    uint32_t t = term;
    for (int i = 0; i < 4; ++i) {
      preimage.push_back(static_cast<uint8_t>(t & 0xFF));
      t >>= 8;
    }
    uint8_t md[32];
    keccak(preimage.data(), static_cast<int>(preimage.size()), md, sizeof(md));
    Crypto::Hash out{};
    memcpy(&out, md, sizeof(out));
    return out;
  }

  Crypto::Hash computeHeatCommitment(const std::array<uint8_t, 32> &secret,
                                     uint64_t amount_atomic,
                                     const Crypto::Hash &tx_prefix_hash,
                                     uint32_t network_id,
                                     uint32_t target_chain_id,
                                     uint32_t commitment_version)
  {
    return computeCommitment(secret, amount_atomic, tx_prefix_hash, network_id, target_chain_id, commitment_version, parameters::HEAT_TERM);
  }

  bool buildHeatExtra(const std::array<uint8_t, 32> &secret,
                      uint64_t amount_atomic,
                      const Crypto::Hash &tx_prefix_hash,
                      uint32_t network_id,
                      uint32_t target_chain_id,
                      uint32_t commitment_version,
                      const std::vector<uint8_t> &metadata,
                      std::vector<uint8_t> &extra)
  {
    Crypto::Hash commitment = computeHeatCommitment(secret, amount_atomic, tx_prefix_hash, network_id, target_chain_id, commitment_version);
    const Crypto::Hash zero = {};
    if (!memcmp(&commitment, &zero, sizeof(zero))) {
      return false;
    }
    return CryptoNote::createTxExtraWithHeatCommitment(commitment, amount_atomic, metadata, extra);
  }

  // REMOVED: COLD commitment computation
  // Crypto::Hash computeColdCommitment(const std::array<uint8_t, 32> &secret,
  //                                    uint64_t amount_atomic,
  //                                    const Crypto::Hash &tx_prefix_hash,
  //                                    uint32_t network_id,
  //                                    uint32_t target_chain_id,
  //                                    uint32_t commitment_version,
  //                                    uint32_t term)
  // {
  //   return computeCommitment(secret, amount_atomic, tx_prefix_hash, network_id, target_chain_id, commitment_version, term);
  // }

  // REMOVED: COLD extra builder
  // bool buildColdExtra(const std::array<uint8_t, 32> &secret,
  //                     uint64_t amount_atomic,
  //                     const Crypto::Hash &tx_prefix_hash,
  //                     uint32_t network_id,
  //                     uint32_t target_chain_id,
  //                     uint32_t commitment_version,
  //                     uint32_t term,
  //                     uint8_t claimChainCode,
  //                     const std::vector<uint8_t> &metadata,
  //                     const std::vector<uint8_t> &gift_secret,
  //                     std::vector<uint8_t> &extra)
  // {
  //   Crypto::Hash commitment = computeColdCommitment(secret, amount_atomic, tx_prefix_hash, network_id, target_chain_id, commitment_version, term);
  //   const Crypto::Hash zero = {};
  //   if (!memcmp(&commitment, &zero, sizeof(zero))) {
  //     return false;
  //   }
  //   return CryptoNote::createTxExtraWithColdCommitment(commitment, amount_atomic, term, claimChainCode, metadata, gift_secret, extra);
  // }

  // REMOVED: COLD commitment serialization
  // bool addColdCommitmentToExtra(std::vector<uint8_t> &tx_extra, const CryptoNote::TransactionExtraColdCommitment &commitment)
  // {
  //   tx_extra.push_back(TX_EXTRA_COLD_COMMITMENT);
  //   tx_extra.insert(tx_extra.end(), commitment.commitment.data, commitment.commitment.data + 32);
  //   uint64_t amount = commitment.amount;
  //   for (int i = 0; i < 8; ++i) {
  //     tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF));
  //     amount >>= 8;
  //   }
  //   uint32_t term = commitment.term;
  //   for (int i = 0; i < 4; ++i) {
  //     tx_extra.push_back(static_cast<uint8_t>(term & 0xFF));
  //     term >>= 8;
  //   }
  //   tx_extra.push_back(commitment.claimChainCode);
  //   uint8_t metadataSize = static_cast<uint8_t>(commitment.metadata.size());
  //   tx_extra.push_back(metadataSize);
  //   if (metadataSize > 0) {
  //     tx_extra.insert(tx_extra.end(), commitment.metadata.begin(), commitment.metadata.end());
  //   }
  //   uint8_t giftSecretSize = static_cast<uint8_t>(commitment.gift_secret.size());
  //   tx_extra.push_back(giftSecretSize);
  //   if (giftSecretSize > 0) {
  //     tx_extra.insert(tx_extra.end(), commitment.gift_secret.begin(), commitment.gift_secret.end());
  //   }
  //   return true;
  // }

  // REMOVED: COLD migration serialization
  // bool addColdMigrationToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraColdMigration& migration) {
  //   tx_extra.push_back(TX_EXTRA_COLD_MIGRATION);
  //   tx_extra.insert(tx_extra.end(), migration.originalTxHash.data, migration.originalTxHash.data + 32);
  //   tx_extra.insert(tx_extra.end(), migration.commitment.data, migration.commitment.data + 32);
  //   uint64_t amount = migration.amount;
  //   for (int i = 0; i < 8; ++i) {
  //     tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF));
  //     amount >>= 8;
  //   }
  //   uint32_t term = migration.term;
  //   for (int i = 0; i < 4; ++i) {
  //     tx_extra.push_back(static_cast<uint8_t>(term & 0xFF));
  //     term >>= 8;
  //   }
  //   tx_extra.push_back(migration.targetChainId);
  //   return true;
  // }

  bool addLegacyBondToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraLegacyBond& bond) {
    tx_extra.push_back(TX_EXTRA_LEGACY_BOND);
    tx_extra.insert(tx_extra.end(), bond.originalTxHash.data, bond.originalTxHash.data + 32);
    uint64_t amount = bond.amount;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF));
      amount >>= 8;
    }
    uint32_t height = bond.originalCreationHeight;
    for (int i = 0; i < 4; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(height & 0xFF));
      height >>= 8;
    }
    return true;
  }

  bool getLegacyBondFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraLegacyBond& bond) {
    if (tx_extra.empty() || tx_extra[0] != TX_EXTRA_LEGACY_BOND) {
      return false;
    }
    size_t pos = 1;
    if (pos + sizeof(Crypto::Hash) > tx_extra.size()) return false;
    std::memcpy(&bond.originalTxHash, &tx_extra[pos], sizeof(Crypto::Hash));
    pos += sizeof(Crypto::Hash);
    if (pos + 8 > tx_extra.size()) return false;
    bond.amount = 0;
    for (int i = 0; i < 8; ++i, ++pos) {
      bond.amount |= static_cast<uint64_t>(tx_extra[pos]) << (i * 8);
    }
    if (pos + 4 > tx_extra.size()) return false;
    bond.originalCreationHeight = 0;
    for (int i = 0; i < 4; ++i, ++pos) {
      bond.originalCreationHeight |= static_cast<uint32_t>(tx_extra[pos]) << (i * 8);
    }
    return true;
  }

  bool addLegacyBondClaimToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraLegacyBondClaim& claim) {
    tx_extra.push_back(TX_EXTRA_LEGACY_BOND_CLAIM);
    uint64_t v = claim.claimedInterest;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(v & 0xFF));
      v >>= 8;
    }
    return true;
  }

  bool getLegacyBondClaimFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraLegacyBondClaim& claim) {
    if (tx_extra.empty() || tx_extra[0] != TX_EXTRA_LEGACY_BOND_CLAIM) {
      return false;
    }
    size_t pos = 1;
    if (pos + 8 > tx_extra.size()) return false;
    claim.claimedInterest = 0;
    for (int i = 0; i < 8; ++i, ++pos) {
      claim.claimedInterest |= static_cast<uint64_t>(tx_extra[pos]) << (i * 8);
    }
    return true;
  }

  // REMOVED: COLD commitment creation
  // bool createTxExtraWithColdCommitment(const Crypto::Hash &commitment, uint64_t amount, uint32_t term,
  //                                     uint8_t claimChainCode, const std::vector<uint8_t> &metadata,
  //                                     const std::vector<uint8_t> &gift_secret, std::vector<uint8_t> &extra)
  // {
  //   TransactionExtraColdCommitment coldCommitment;
  //   coldCommitment.commitment = commitment;
  //   coldCommitment.amount = amount;
  //   coldCommitment.term = term;
  //   coldCommitment.claimChainCode = claimChainCode;
  //   coldCommitment.metadata = metadata;
  //   coldCommitment.gift_secret = gift_secret;
  //   return addColdCommitmentToExtra(extra, coldCommitment);
  // }

  // REMOVED: COLD commitment reader
  // bool getColdCommitmentFromExtra(const std::vector<uint8_t> &tx_extra, TransactionExtraColdCommitment &commitment)
  // {
  //   if (tx_extra.empty() || tx_extra[0] != TX_EXTRA_COLD_COMMITMENT) {
  //     return false;
  //   }
  //   size_t pos = 1;
  //   if (pos + 32 > tx_extra.size()) return false;
  //   std::memcpy(commitment.commitment.data, &tx_extra[pos], 32);
  //   pos += 32;
  //   if (pos + 8 > tx_extra.size()) return false;
  //   commitment.amount = 0;
  //   for (int i = 0; i < 8; ++i) {
  //     commitment.amount |= static_cast<uint64_t>(tx_extra[pos + i]) << (i * 8);
  //   }
  //   pos += 8;
  //   if (pos + 4 > tx_extra.size()) return false;
  //   commitment.term = 0;
  //   for (int i = 0; i < 4; ++i) {
  //     commitment.term |= static_cast<uint32_t>(tx_extra[pos + i]) << (i * 8);
  //   }
  //   pos += 4;
  //   if (pos >= tx_extra.size()) return false;
  //   commitment.claimChainCode = tx_extra[pos];
  //   pos++;
  //   if (pos >= tx_extra.size()) return false;
  //   uint8_t metadataSize = tx_extra[pos];
  //   pos++;
  //   if (pos + metadataSize > tx_extra.size()) return false;
  //   if (metadataSize > 0) {
  //     commitment.metadata.assign(&tx_extra[pos], &tx_extra[pos] + metadataSize);
  //     pos += metadataSize;
  //   } else {
  //     commitment.metadata.clear();
  //   }
  //   if (pos >= tx_extra.size()) return false;
  //   uint8_t giftSecretSize = tx_extra[pos];
  //   pos++;
  //   if (pos + giftSecretSize > tx_extra.size()) return false;
  //   if (giftSecretSize > 0) {
  //     commitment.gift_secret.assign(&tx_extra[pos], &tx_extra[pos] + giftSecretSize);
  //   } else {
  //     commitment.gift_secret.clear();
  //   }
  //   return true;
  // }

  bool getBurnReceiptFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraBurnReceipt& burnReceipt) {
    if (tx_extra.empty() || tx_extra[0] != TX_EXTRA_BURN_RECEIPT) {
      return false;
    }

    size_t pos = 1;

    // Parse proof_pubkey (32 bytes)
    if (pos + sizeof(Crypto::PublicKey) > tx_extra.size()) return false;
    std::memcpy(&burnReceipt.proof_pubkey, &tx_extra[pos], sizeof(Crypto::PublicKey));
    pos += sizeof(Crypto::PublicKey);

    // Parse tx_hash (variable length)
    if (pos >= tx_extra.size()) return false;
    uint32_t hashLen = 0;
    for (int i = 0; i < 4 && pos < tx_extra.size(); ++i, ++pos) {
      hashLen |= static_cast<uint32_t>(tx_extra[pos]) << (i * 8);
    }
    if (pos + hashLen > tx_extra.size()) return false;
    burnReceipt.tx_hash.assign(reinterpret_cast<const char*>(&tx_extra[pos]), hashLen);
    pos += hashLen;

    // Parse timestamp (8 bytes)
    if (pos + 8 > tx_extra.size()) return false;
    burnReceipt.timestamp = 0;
    for (int i = 0; i < 8; ++i) {
      burnReceipt.timestamp |= static_cast<uint64_t>(tx_extra[pos + i]) << (i * 8);
    }

    return true;
  }

  bool addBurnReceiptToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraBurnReceipt& burnReceipt) {
    tx_extra.push_back(TX_EXTRA_BURN_RECEIPT);

    // Add proof_pubkey
    tx_extra.insert(tx_extra.end(), reinterpret_cast<const uint8_t*>(&burnReceipt.proof_pubkey),
                    reinterpret_cast<const uint8_t*>(&burnReceipt.proof_pubkey) + sizeof(Crypto::PublicKey));

    // Add tx_hash length and data
    uint32_t hashLen = static_cast<uint32_t>(burnReceipt.tx_hash.length());
    for (int i = 0; i < 4; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(hashLen & 0xFF));
      hashLen >>= 8;
    }
    tx_extra.insert(tx_extra.end(), burnReceipt.tx_hash.begin(), burnReceipt.tx_hash.end());

    // Add timestamp
    uint64_t timestamp = burnReceipt.timestamp;
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(timestamp & 0xFF));
      timestamp >>= 8;
    }

    return true;
  }

  bool createTxExtraWithBurnReceipt(const TransactionExtraBurnReceipt& burnReceipt, std::vector<uint8_t>& extra) {
    extra.clear();
    return addBurnReceiptToExtra(extra, burnReceipt);
  }

  // REMOVED: COLD deposit receipt functions
  // bool getDepositReceiptFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraDepositReceipt& depositReceipt) {
  //   if (tx_extra.empty() || tx_extra[0] != TX_EXTRA_COLD_RECEIPT) {
  //     return false;
  //   }
  //   // ... (full implementation removed)
  //   return true;
  // }

  // bool addDepositReceiptToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraDepositReceipt& depositReceipt) {
  //   tx_extra.push_back(TX_EXTRA_COLD_RECEIPT);
  //   // ... (full implementation removed)
  //   return true;
  // }

  // bool createTxExtraWithDepositReceipt(const TransactionExtraDepositReceipt& depositReceipt, std::vector<uint8_t>& extra) {
  //   extra.clear();
  //   return addDepositReceiptToExtra(extra, depositReceipt);
  // }

  bool addAliasToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraAliasRegistration& alias) {
    if (!alias.isValid()) {
      return false;
    }

    // Write tag
    tx_extra.push_back(TX_EXTRA_ALIAS);

    // Serialize the alias registration
    BinaryArray ba;
    bool r = toBinaryArray(alias, ba);
    if (!r) return false;

    // Write size + data
    Tools::write_varint(std::back_inserter(tx_extra), ba.size());
    tx_extra.insert(tx_extra.end(), ba.begin(), ba.end());

    return true;
  }

  bool getAliasFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraAliasRegistration& alias) {
    // Find the 0xEA tag in extra
    for (size_t i = 0; i < tx_extra.size(); ++i) {
      if (tx_extra[i] == TX_EXTRA_ALIAS) {
        // Read size
        size_t offset = i + 1;
        if (offset >= tx_extra.size()) return false;

        uint64_t size = 0;
        auto begin = tx_extra.begin() + offset;
        auto end = tx_extra.end();
        int bytes_read = Tools::read_varint<64, std::vector<uint8_t>::const_iterator, uint64_t>(std::move(begin), std::move(end), size);
        if (bytes_read <= 0) return false;
        offset += bytes_read;

        if (offset + size > tx_extra.size()) return false;

        // Deserialize
        BinaryArray ba(tx_extra.begin() + offset, tx_extra.begin() + offset + size);
        return fromBinaryArray(alias, ba);
      }
    }
    return false;
  }

  bool addAliasReleaseToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraAliasRelease& release) {
    if (!release.isValid()) return false;

    tx_extra.push_back(TX_EXTRA_ALIAS_RELEASE);
    BinaryArray ba;
    if (!toBinaryArray(release, ba)) return false;
    Tools::write_varint(std::back_inserter(tx_extra), ba.size());
    tx_extra.insert(tx_extra.end(), ba.begin(), ba.end());
    return true;
  }

  bool getAliasReleaseFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraAliasRelease& release) {
    for (size_t i = 0; i < tx_extra.size(); ++i) {
      if (tx_extra[i] == TX_EXTRA_ALIAS_RELEASE) {
        size_t offset = i + 1;
        if (offset >= tx_extra.size()) return false;
        uint64_t size = 0;
        auto begin = tx_extra.begin() + offset;
        auto end = tx_extra.end();
        int bytes_read = Tools::read_varint<64, std::vector<uint8_t>::const_iterator, uint64_t>(std::move(begin), std::move(end), size);
        if (bytes_read <= 0) return false;
        offset += bytes_read;
        if (offset + size > tx_extra.size()) return false;
        BinaryArray ba(tx_extra.begin() + offset, tx_extra.begin() + offset + size);
        return fromBinaryArray(release, ba);
      }
    }
    return false;
  }

  bool addAliasTransferToExtra(std::vector<uint8_t>& tx_extra, const TransactionExtraAliasTransfer& transfer) {
    if (!transfer.isValid()) return false;

    tx_extra.push_back(TX_EXTRA_ALIAS_TRANSFER);
    BinaryArray ba;
    if (!toBinaryArray(transfer, ba)) return false;
    Tools::write_varint(std::back_inserter(tx_extra), ba.size());
    tx_extra.insert(tx_extra.end(), ba.begin(), ba.end());
    return true;
  }

  bool getAliasTransferFromExtra(const std::vector<uint8_t>& tx_extra, TransactionExtraAliasTransfer& transfer) {
    for (size_t i = 0; i < tx_extra.size(); ++i) {
      if (tx_extra[i] == TX_EXTRA_ALIAS_TRANSFER) {
        size_t offset = i + 1;
        if (offset >= tx_extra.size()) return false;
        uint64_t size = 0;
        auto begin = tx_extra.begin() + offset;
        auto end = tx_extra.end();
        int bytes_read = Tools::read_varint<64, std::vector<uint8_t>::const_iterator, uint64_t>(std::move(begin), std::move(end), size);
        if (bytes_read <= 0) return false;
        offset += bytes_read;
        if (offset + size > tx_extra.size()) return false;
        BinaryArray ba(tx_extra.begin() + offset, tx_extra.begin() + offset + size);
        return fromBinaryArray(transfer, ba);
      }
    }
    return false;
  }

  // REMOVED: COLD SimpleCD commitment creation
  // bool createTxExtraWithSimpleCDCommitment(const Crypto::Hash& commitment, uint64_t amount, uint32_t term, std::vector<uint8_t>& extra) {
  //   TransactionExtraSimpleCD cdCommitment;
  //   cdCommitment.commitment = commitment;
  //   cdCommitment.amount = amount;
  //   cdCommitment.term = term;
  //   extra.push_back(TX_EXTRA_SIMPLE_CD);
  //   extra.insert(extra.end(), cdCommitment.commitment.data, cdCommitment.commitment.data + 32);
  //   for (int i = 0; i < 8; ++i) { extra.push_back(static_cast<uint8_t>((cdCommitment.amount >> (i*8)) & 0xFF)); }
  //   for (int i = 0; i < 4; ++i) { extra.push_back(static_cast<uint8_t>((cdCommitment.term >> (i*8)) & 0xFF)); }
  //   return true;
  // }

  bool addAmmSwapToExtra(std::vector<uint8_t>& tx_extra, uint8_t direction, uint64_t inputAmount, uint64_t minOutput) {
    tx_extra.push_back(TX_EXTRA_AMM_SWAP);
    tx_extra.push_back(direction);
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((inputAmount >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((minOutput >> (i*8)) & 0xFF));
    return true;
  }

  bool addAmmAddLiquidityToExtra(std::vector<uint8_t>& tx_extra, uint64_t amountXfg, uint64_t amountHeat) {
    tx_extra.push_back(TX_EXTRA_AMM_ADD_LIQ);
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((amountXfg >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((amountHeat >> (i*8)) & 0xFF));
    return true;
  }

  bool addAmmRemoveLiquidityToExtra(std::vector<uint8_t>& tx_extra, uint64_t lpSharesBurned, uint64_t minXfg, uint64_t minHeat) {
    tx_extra.push_back(TX_EXTRA_AMM_REM_LIQ);
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((lpSharesBurned >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((minXfg >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((minHeat >> (i*8)) & 0xFF));
    return true;
  }

  bool addAmmCompoundToExtra(std::vector<uint8_t>& tx_extra) {
    tx_extra.push_back(TX_EXTRA_AMM_COMPOUND);
    return true;
  }

  bool addAmmClaimToExtra(std::vector<uint8_t>& tx_extra, uint64_t lpShares, uint64_t minXfg, uint64_t minHeat) {
    tx_extra.push_back(TX_EXTRA_AMM_CLAIM);
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((lpShares >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((minXfg >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((minHeat >> (i*8)) & 0xFF));
    return true;
  }

  bool addHeatMintAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t xfgBurned, uint64_t heatMinted) {
    tx_extra.push_back(TX_EXTRA_HEAT_MINT_AUTH);
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((xfgBurned >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((heatMinted >> (i*8)) & 0xFF));
    return true;
  }

  bool addHeatSendAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t heatAmount) {
    tx_extra.push_back(TX_EXTRA_HEAT_SEND_AUTH);
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((heatAmount >> (i*8)) & 0xFF));
    return true;
  }

  bool addAmmSwapAuthToExtra(std::vector<uint8_t>& tx_extra, uint8_t direction, uint64_t inputAmount,
                             uint64_t outputAmount, uint64_t minOutput) {
    tx_extra.push_back(TX_EXTRA_AMM_SWAP_AUTH);
    tx_extra.push_back(direction);
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((inputAmount >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((outputAmount >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((minOutput >> (i*8)) & 0xFF));
    return true;
  }

  bool addLpAddAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t amountXfg, uint64_t amountHeat,
                           uint64_t lpShares) {
    tx_extra.push_back(TX_EXTRA_AMM_LP_ADD_AUTH);
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((amountXfg >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((amountHeat >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((lpShares >> (i*8)) & 0xFF));
    return true;
  }

  bool addLpRemoveAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t lpSharesBurned,
                              uint64_t minXfg, uint64_t minHeat) {
    tx_extra.push_back(TX_EXTRA_AMM_LP_REM_AUTH);
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((lpSharesBurned >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((minXfg >> (i*8)) & 0xFF));
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((minHeat >> (i*8)) & 0xFF));
    return true;
  }

  bool addOrderPlaceToExtra(std::vector<uint8_t>& tx_extra, uint8_t side, uint64_t price, uint32_t expiration) {
    tx_extra.push_back(TX_EXTRA_ORDER_PLACE);
    tx_extra.push_back(side);
    for (int i = 0; i < 8; ++i) tx_extra.push_back(static_cast<uint8_t>((price >> (i*8)) & 0xFF));
    for (int i = 0; i < 4; ++i) tx_extra.push_back(static_cast<uint8_t>((expiration >> (i*8)) & 0xFF));
    return true;
  }

  bool addOrderCancelToExtra(std::vector<uint8_t>& tx_extra, const Crypto::Hash& orderId) {
    tx_extra.push_back(TX_EXTRA_ORDER_CANCEL);
    tx_extra.insert(tx_extra.end(), orderId.data, orderId.data + sizeof(orderId.data));
    return true;
  }

  bool addMarketBuyAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t xfgWanted, uint64_t maxHeatCost) {
    tx_extra.push_back(TX_EXTRA_MARKET_BUY_AUTH);
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(xfgWanted & 0xFF));
      xfgWanted >>= 8;
    }
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(maxHeatCost & 0xFF));
      maxHeatCost >>= 8;
    }
    return true;
  }

  bool addMarketSellAuthToExtra(std::vector<uint8_t>& tx_extra, uint64_t xfgToSell, uint64_t minHeatReceive) {
    tx_extra.push_back(TX_EXTRA_MARKET_SELL_AUTH);
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(xfgToSell & 0xFF));
      xfgToSell >>= 8;
    }
    for (int i = 0; i < 8; ++i) {
      tx_extra.push_back(static_cast<uint8_t>(minHeatReceive & 0xFF));
      minHeatReceive >>= 8;
    }
    return true;
  }

  bool addLimitDepositToExtra(std::vector<uint8_t>& tx_extra, uint8_t side, uint64_t amount, uint64_t targetPrice, uint32_t expiration, const Crypto::Hash& orderId, const Crypto::Hash& addressHash) {
    tx_extra.push_back(TX_EXTRA_LIMIT_DEPOSIT);
    tx_extra.push_back(side);
    for (int i = 0; i < 8; ++i) { tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF)); amount >>= 8; }
    for (int i = 0; i < 8; ++i) { tx_extra.push_back(static_cast<uint8_t>(targetPrice & 0xFF)); targetPrice >>= 8; }
    for (int i = 0; i < 4; ++i) { tx_extra.push_back(static_cast<uint8_t>(expiration & 0xFF)); expiration >>= 8; }
    for (size_t i = 0; i < sizeof(orderId.data); ++i) tx_extra.push_back(orderId.data[i]);
    for (size_t i = 0; i < sizeof(addressHash.data); ++i) tx_extra.push_back(addressHash.data[i]);
    return true;
  }
  
  bool addLimitWithdrawToExtra(std::vector<uint8_t>& tx_extra, const Crypto::Hash& orderId) {
    tx_extra.push_back(TX_EXTRA_LIMIT_WITHDRAW);
    for (size_t i = 0; i < sizeof(orderId.data); ++i) tx_extra.push_back(orderId.data[i]);
    return true;
  }

  DepositCommitmentKeys deriveCommitmentKeys(const std::array<uint8_t, 32>& depositSecret) {
    DepositCommitmentKeys keys;
    static const char label[] = "fuego_commit_key";
    uint8_t preimage[48];
    memcpy(preimage,      label,               16);
    memcpy(preimage + 16, depositSecret.data(), 32);
    Crypto::hash_to_scalar(preimage, sizeof(preimage),
      reinterpret_cast<Crypto::EllipticCurveScalar&>(keys.keyScalar));
    Crypto::secret_key_to_public_key(keys.keyScalar, keys.commitKey);
    Crypto::generate_key_image(keys.commitKey, keys.keyScalar, keys.keyImage);
    {
      static const char amLabel[] = "fuego_amount_mask";
      uint8_t amPre[49];
      memcpy(amPre,      amLabel,              17);
      memcpy(amPre + 17, depositSecret.data(), 32);
      Crypto::hash_to_scalar(amPre, sizeof(amPre),
        reinterpret_cast<Crypto::EllipticCurveScalar&>(keys.amountMask));
    }
    return keys;
  }

  namespace {
  struct DepositKeyData {
    Crypto::KeyDerivation derivation;
    uint8_t tag[2];
  };
  static_assert(sizeof(DepositKeyData) == sizeof(Crypto::KeyDerivation) + 2, "");

  static Crypto::chacha8_key depositEncKey(const Crypto::KeyDerivation& derivation) {
    DepositKeyData kd;
    kd.derivation = derivation;
    kd.tag[0] = 0xD5;
    kd.tag[1] = 0x00;
    Crypto::Hash h = Crypto::cn_fast_hash(&kd, sizeof(kd));
    Crypto::chacha8_key out;
    memcpy(out.data, &h, sizeof(out.data));
    return out;
  }

  static Crypto::chacha8_iv depositEncIV(const Crypto::PublicKey& txPubKey) {
    Crypto::chacha8_iv iv;
    memcpy(iv.data, &txPubKey, sizeof(iv.data));
    return iv;
  }
  } // anonymous namespace

  bool encryptDepositSecret(const DepositSecretPayload& plaintext,
                            const Crypto::PublicKey& recipientViewPubKey,
                            TransactionExtraDepositSecret& out) {
    Crypto::SecretKey ephSecKey;
    Crypto::generate_keys(out.ephPubKey, ephSecKey);
    Crypto::KeyDerivation derivation;
    if (!Crypto::generate_key_derivation(recipientViewPubKey, ephSecKey, derivation))
      return false;
    Crypto::chacha8_key encKey = depositEncKey(derivation);
    Crypto::chacha8_iv  encIV  = depositEncIV(out.ephPubKey);
    out.encryptedPayload.resize(sizeof(DepositSecretPayload));
    Crypto::chacha8(&plaintext, sizeof(DepositSecretPayload),
                    encKey, encIV,
                    reinterpret_cast<char*>(out.encryptedPayload.data()));
    Crypto::Hash ckHash;
    keccak(out.encryptedPayload.data(), out.encryptedPayload.size(), ckHash.data, sizeof(ckHash.data));
    memcpy(out.checksum, ckHash.data, 4);
    return true;
  }

  bool decryptDepositSecret(const TransactionExtraDepositSecret& encrypted,
                            const Crypto::SecretKey& walletViewSecKey,
                            DepositSecretPayload& out) {
    if (encrypted.encryptedPayload.size() != sizeof(DepositSecretPayload))
      return false;
    Crypto::Hash ckHash;
    keccak(encrypted.encryptedPayload.data(), encrypted.encryptedPayload.size(),
           ckHash.data, sizeof(ckHash.data));
    if (memcmp(encrypted.checksum, ckHash.data, 4) != 0)
      return false;
    Crypto::KeyDerivation derivation;
    if (!Crypto::generate_key_derivation(encrypted.ephPubKey, walletViewSecKey, derivation))
      return false;
    Crypto::chacha8_key encKey = depositEncKey(derivation);
    Crypto::chacha8_iv  encIV  = depositEncIV(encrypted.ephPubKey);
    Crypto::chacha8(encrypted.encryptedPayload.data(), sizeof(DepositSecretPayload),
                    encKey, encIV,
                    reinterpret_cast<char*>(&out));
    return true;
  }

  bool addDepositSecretToExtra(std::vector<uint8_t>& tx_extra,
                               const TransactionExtraDepositSecret& secret) {
    if (secret.encryptedPayload.size() != sizeof(DepositSecretPayload))
      return false;
    const uint8_t totalLen = 32 + 4 + static_cast<uint8_t>(secret.encryptedPayload.size());
    tx_extra.push_back(TX_EXTRA_DEPOSIT_SECRET);
    tx_extra.push_back(totalLen);
    const auto* pubBytes = reinterpret_cast<const uint8_t*>(&secret.ephPubKey);
    tx_extra.insert(tx_extra.end(), pubBytes, pubBytes + 32);
    tx_extra.insert(tx_extra.end(), secret.checksum, secret.checksum + 4);
    tx_extra.insert(tx_extra.end(),
                    secret.encryptedPayload.begin(),
                    secret.encryptedPayload.end());
    return true;
  }

  bool getDepositSecretFromExtra(const std::vector<uint8_t>& tx_extra,
                                 TransactionExtraDepositSecret& out) {
    const size_t expectedLen = 32 + 4 + sizeof(DepositSecretPayload);
    for (size_t i = 0; i + 1 < tx_extra.size(); ++i) {
      if (tx_extra[i] != TX_EXTRA_DEPOSIT_SECRET)
        continue;
      uint8_t len = tx_extra[i + 1];
      if (len != expectedLen || i + 2 + len > tx_extra.size())
        return false;
      memcpy(&out.ephPubKey, &tx_extra[i + 2], 32);
      memcpy(out.checksum,  &tx_extra[i + 2 + 32], 4);
      out.encryptedPayload.assign(tx_extra.begin() + i + 2 + 32 + 4,
                                  tx_extra.begin() + i + 2 + len);
      return true;
    }
    return false;
  }

  Crypto::PublicKey computePoolCommitKey() {
    static const char SEED[] = "fuego.hearth.pool.commit.key.v1";
    Crypto::Hash h;
    Crypto::cn_fast_hash(SEED, sizeof(SEED) - 1, h);
    Crypto::SecretKey scalar;
    Crypto::hash_to_scalar(&h, sizeof(h), scalar);
    Crypto::PublicKey pub;
    Crypto::secret_key_to_public_key(scalar, pub);
    return pub;
  }

  Crypto::Hash hashOutput(const TransactionOutput& output) {
    return getObjectHash(output);
  }

} // namespace CryptoNote
