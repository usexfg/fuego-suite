// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#include "VaultPolicy.h"
#include "VaultUtxoSet.h"
#include "CryptoNoteCore/TransactionExtra.h"

namespace CryptoNote {

bool VaultPolicy::isVaultKeyImage(const Crypto::KeyImage& ki) {
    return false; // TODO: check against registered vault key images in index
}

VaultPartition VaultPolicy::classifySpend(
    const Transaction& tx,
    const VaultUtxoSet& utxoSet) {

    for (const auto& input : tx.inputs) {
        const Crypto::KeyImage* ki = nullptr;
        if (input.type() == typeid(KeyInput)) {
            ki = &boost::get<KeyInput>(input).keyImage;
        } else if (input.type() == typeid(TransactionInputUnified)) {
            ki = &boost::get<TransactionInputUnified>(input).keyImage;
        } else if (input.type() == typeid(TransactionInputCommitmentSpend)) {
            ki = &boost::get<TransactionInputCommitmentSpend>(input).keyImage;
        }

        if (ki && utxoSet.isVaultKeyImage(*ki)) {
            uint64_t idx = utxoSet.getOutputIndexForKeyImage(*ki);
            if (idx > 0) {
                return utxoSet.getPartition(idx);
            }
        }
    }

    return VaultPartition::GENERAL_RESERVE;
}

bool VaultPolicy::isPermitted(
    const Transaction& tx,
    VaultPartition source,
    const VaultUtxoSet& utxoSet) {

    std::vector<TransactionExtraField> extraFields;
    if (!parseTransactionExtra(tx.extra, extraFields)) {
        return false;
    }

    bool hasAmmAdd  = false;
    bool hasAmmRem  = false;
    bool hasMintAuth = false;

    for (const auto& field : extraFields) {
        if (field.type() == typeid(TransactionExtraAmmAddLiquidity)) {
            hasAmmAdd = true;
        }
        if (field.type() == typeid(TransactionExtraAmmRemoveLiquidity)) {
            hasAmmRem = true;
        }
        if (field.type() == typeid(TransactionExtraHeatMintAuth)) {
            hasMintAuth = true;
        }
    }

    switch (source) {
        case VaultPartition::CD_APY_POOL:
            return true;

        case VaultPartition::LP_RESERVE:
            return hasAmmAdd || hasAmmRem;

        case VaultPartition::GENERAL_RESERVE:
            return hasMintAuth;

        case VaultPartition::SWF:
            return false;

        default:
            return false;
    }
}

} // namespace CryptoNote
