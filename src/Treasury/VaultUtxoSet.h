// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <CryptoTypes.h>
#include "VaultTypes.h"

namespace CryptoNote {

class ISerializer;

struct VaultKeyImageEntry {
    Crypto::KeyImage keyImage;
    uint64_t globalIndex;
};

struct KeyImageHash {
    size_t operator()(const Crypto::KeyImage& ki) const {
        size_t h = 0;
        for (int i = 0; i < 32; i += 8) {
            h ^= static_cast<size_t>(ki.data[i])   << 56;
            h ^= static_cast<size_t>(ki.data[i+1]) << 48;
            h ^= static_cast<size_t>(ki.data[i+2]) << 40;
            h ^= static_cast<size_t>(ki.data[i+3]) << 32;
            h ^= static_cast<size_t>(ki.data[i+4]) << 24;
            h ^= static_cast<size_t>(ki.data[i+5]) << 16;
            h ^= static_cast<size_t>(ki.data[i+6]) << 8;
            h ^= static_cast<size_t>(ki.data[i+7]);
        }
        return h;
    }
};

struct KeyImageEq {
    bool operator()(const Crypto::KeyImage& a, const Crypto::KeyImage& b) const {
        return memcmp(a.data, b.data, sizeof(a.data)) == 0;
    }
};

class VaultUtxoSet {
public:
    void addUtxo(uint64_t globalIndex,
                 uint64_t amount,
                 AssetType asset,
                 VaultPartition partition,
                 const Crypto::Hash& txHash,
                 const Crypto::PublicKey& outputKey);

    void markSpent(uint64_t globalIndex);

    uint64_t partitionBalance(VaultPartition partition, AssetType asset) const;
    VaultBalance allBalances() const;
    size_t   totalUtxos() const { return m_utxos.size(); }
    uint64_t spentCount() const { return m_spentCount; }

    bool isVaultOutput(uint64_t globalIndex) const;
    VaultPartition getPartition(uint64_t globalIndex) const;
    const VaultOutput* getOutput(uint64_t globalIndex) const;

    bool     isVaultKeyImage(const Crypto::KeyImage& ki) const;
    uint64_t getOutputIndexForKeyImage(const Crypto::KeyImage& ki) const;

    void registerKeyImage(const Crypto::KeyImage& ki, uint64_t globalIndex);

    void setSpendKey(const Crypto::SecretKey& spendKey);
    void rebuildKeyImageIndex();

    std::vector<VaultOutput> selectUtxos(
        VaultPartition partition,
        AssetType asset,
        uint64_t neededAmount) const;

    struct SpendResult {
        uint64_t amountSpent = 0;
        std::vector<uint64_t> spentIndices;
        // W-3 fix: when the last selected UTXO overshoots neededAmount, the
        // surplus is NOT destroyed — the caller mints it back to the same
        // partition as a change UTXO. changeAmount is the surplus to return;
        // changeSourceIndex is the overshooting UTXO's global index (0 = none).
        uint64_t changeAmount = 0;
        uint64_t changeSourceIndex = 0;
    };

    SpendResult spendUtxos(VaultPartition partition, AssetType asset, uint64_t neededAmount);

    void unSpendUtxos(const std::vector<uint64_t>& indices);

    void serialize(ISerializer& s);

    void clear();

    void removeAboveIndex(uint64_t minIndex);

    static const char* partitionName(VaultPartition p);

private:
    std::unordered_map<uint64_t, VaultOutput> m_utxos;
    std::unordered_map<Crypto::KeyImage, uint64_t, KeyImageHash, KeyImageEq> m_keyImages;
    Crypto::SecretKey m_spendKey;
    bool m_spendKeySet = false;
    uint64_t m_spentCount = 0;
};

} // namespace CryptoNote
