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

#include <map>
#include <unordered_map>

#include "../../include/ITransfersContainer.h"
#include "../../include/IWallet.h"
#include "../../include/IWalletLegacy.h" //TODO: make common types for all of our APIs (such as PublicKey, KeyPair, etc)

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>

#include "../Common/FileMappedVector.h"
#include "../crypto/chacha8.h"

namespace CryptoNote
{

    const uint64_t ACCOUNT_CREATE_TIME_ACCURACY = 60 * 60 * 24;

    struct WalletRecord
    {
        Crypto::PublicKey spendPublicKey;
        Crypto::SecretKey spendSecretKey;
        CryptoNote::ITransfersContainer *container = nullptr;
        uint64_t pendingBalance = 0;
        uint64_t actualBalance = 0;
        uint64_t lockedDepositBalance = 0;
        uint64_t unlockedDepositBalance = 0;
        time_t creationTimestamp;

        // Sub-address fields. (0,0) = master address (primary).
        uint32_t subaddrMajor = 0;
        uint32_t subaddrMinor = 0;
        bool isSubAddress = false;
    };

#pragma pack(push, 1)
struct EncryptedWalletRecord {
  Crypto::chacha8_iv iv;
  // Secret key, public key and creation timestamp
  uint8_t data[sizeof(Crypto::PublicKey) + sizeof(Crypto::SecretKey) + sizeof(uint64_t)];
};
#pragma pack(pop)

    struct RandomAccessIndex
    {
    };
    struct KeysIndex
    {
    };
    struct TransfersContainerIndex
    {
    };

    struct WalletIndex
    {
    };
    struct TransactionOutputIndex
    {
    };
    struct BlockHeightIndex
    {
    };

    struct TransactionHashIndex
    {
    };
    struct TransactionIndex
    {
    };
    struct BlockHashIndex
    {
    };

    typedef boost::multi_index_container<
        WalletRecord,
        boost::multi_index::indexed_by<
            boost::multi_index::random_access<boost::multi_index::tag<RandomAccessIndex>>,
            boost::multi_index::hashed_unique<boost::multi_index::tag<KeysIndex>,
                                              BOOST_MULTI_INDEX_MEMBER(WalletRecord, Crypto::PublicKey, spendPublicKey)>,
            boost::multi_index::hashed_unique<boost::multi_index::tag<TransfersContainerIndex>,
                                              BOOST_MULTI_INDEX_MEMBER(WalletRecord, CryptoNote::ITransfersContainer *, container)>>>
        WalletsContainer;

    struct UnlockTransactionJob
    {
        uint32_t blockHeight;
        CryptoNote::ITransfersContainer *container;
        Crypto::Hash transactionHash;
    };

    typedef boost::multi_index_container<
        UnlockTransactionJob,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_non_unique<boost::multi_index::tag<BlockHeightIndex>,
                                                   BOOST_MULTI_INDEX_MEMBER(UnlockTransactionJob, uint32_t, blockHeight)>,
            boost::multi_index::hashed_non_unique<boost::multi_index::tag<TransactionHashIndex>,
                                                  BOOST_MULTI_INDEX_MEMBER(UnlockTransactionJob, Crypto::Hash, transactionHash)>>>
        UnlockTransactionJobs;

    typedef boost::multi_index_container<
        CryptoNote::Deposit,
        boost::multi_index::indexed_by<
            boost::multi_index::random_access<boost::multi_index::tag<RandomAccessIndex>>,
            boost::multi_index::hashed_unique<boost::multi_index::tag<TransactionIndex>,
                                              boost::multi_index::member<CryptoNote::Deposit, Crypto::Hash, &CryptoNote::Deposit::transactionHash>>,
            boost::multi_index::ordered_non_unique<boost::multi_index::tag<BlockHeightIndex>,
                                                   boost::multi_index::member<CryptoNote::Deposit, uint64_t, &CryptoNote::Deposit::height>>>>
         WalletDeposits;

    typedef boost::multi_index_container<
        CryptoNote::WalletTransaction,
        boost::multi_index::indexed_by<
            boost::multi_index::random_access<boost::multi_index::tag<RandomAccessIndex>>,
            boost::multi_index::hashed_unique<boost::multi_index::tag<TransactionIndex>,
                                              boost::multi_index::member<CryptoNote::WalletTransaction, Crypto::Hash, &CryptoNote::WalletTransaction::hash>>,
            boost::multi_index::ordered_non_unique<boost::multi_index::tag<BlockHeightIndex>,
                                                   boost::multi_index::member<CryptoNote::WalletTransaction, uint32_t, &CryptoNote::WalletTransaction::blockHeight>>>>
        WalletTransactions;

    typedef Common::FileMappedVector<EncryptedWalletRecord> ContainerStorage;
    typedef std::pair<size_t, CryptoNote::WalletTransfer> TransactionTransferPair;
    typedef std::vector<TransactionTransferPair> WalletTransfers;

    typedef std::map<size_t, CryptoNote::Transaction> UncommitedTransactions;

    typedef boost::multi_index_container<
        Crypto::Hash,
        boost::multi_index::indexed_by<
            boost::multi_index::random_access<
                boost::multi_index::tag<BlockHeightIndex>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<BlockHashIndex>,
                boost::multi_index::identity<Crypto::Hash>>>>
        BlockHashesContainer;

} // namespace CryptoNote
