// Copyright (c) 2017-2025 Fuego Developers
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

#include <cstdint>
#include <vector>
#include <string>
#include "../../include/CryptoTypes.h"

namespace CryptoNote {

// Forward declarations
struct Transaction;
struct Block;

// DIGM Token Structures
struct DIGMTokenInfo {
    uint64_t token_id;
    std::string token_name;
    uint64_t total_supply;
    uint64_t amount_per_output;
    uint8_t tx_extra_tag;
    bool is_minted;
    uint32_t mint_height;
    Crypto::Hash mint_transaction_hash;
};

struct DIGMOutput {
    uint64_t token_id;
    uint64_t amount;
    uint32_t output_index;
    Crypto::Hash transaction_hash;
    uint32_t block_height;
};

// DIGM Transaction Types
enum class DIGMTransactionType {
    MINT = 1,           // Mint new DIGM tokens
    TRANSFER = 2,       // Transfer DIGM tokens
    ALBUM_RELEASE = 3,  // Release album with DIGM signature
    ALBUM_UPDATE = 4    // Update album marketplace metadata
};

// Album Release Information
struct DIGMAlbumRelease {
    uint64_t album_id;              // Unique album identifier
    uint64_t price_atomic;          // Price in atomic XFG units
    uint64_t timestamp;             // Release timestamp
    std::string artist_address;     // Artist's address
    std::vector<uint8_t> signature; // Artist signature
    std::string metadata_hash;      // Hash of album metadata
    bool is_active;                 // Whether album is active for sale
};

// Album Update Information
struct DIGMAlbumUpdate {
    uint64_t album_id;              // Album identifier to update
    uint64_t new_price_atomic;      // New price in atomic XFG units
    uint64_t timestamp;             // Update timestamp
    std::string artist_address;     // Artist's address
    std::vector<uint8_t> signature; // Artist signature
    std::string new_metadata_hash;  // Hash of new album metadata
    uint32_t update_reason;         // Reason for update (1=price, 2=metadata, 3=both)
};

struct DIGMTransaction {
    uint64_t token_id;
    std::vector<DIGMOutput> inputs;
    std::vector<DIGMOutput> outputs;
    uint64_t fee;
    bool is_mint;
    bool is_album_release;
    bool is_album_update;
    DIGMAlbumRelease album_release;
    DIGMAlbumUpdate album_update;
};

// DIGM Token Manager Class
class DIGMTokenManager {
public:
    DIGMTokenManager();
    ~DIGMTokenManager();

    // Token validation
    bool isValidDIGMToken(const DIGMTokenInfo& token);
    bool isValidDIGMOutput(const DIGMOutput& output);
    bool isValidDIGMTransaction(const DIGMTransaction& transaction);

    // Token operations
    bool createDIGMToken(DIGMTokenInfo& token);
    bool mintDIGMTokens(uint64_t amount, uint32_t height, const Crypto::Hash& tx_hash);
    bool transferDIGMTokens(const std::vector<DIGMOutput>& inputs, 
                           const std::vector<DIGMOutput>& outputs);

    // Token queries
    uint64_t getDIGMBalance(const Crypto::PublicKey& address);
    std::vector<DIGMOutput> getDIGMOutputs(const Crypto::PublicKey& address);
    DIGMTokenInfo getDIGMTokenInfo();
    uint64_t getDIGMTotalSupply();
    uint64_t getDIGMCirculatingSupply();

    // Transaction validation
    bool validateDIGMMintTransaction(const std::vector<uint8_t>& tx_extra);
    bool validateDIGMTransferTransaction(const std::vector<uint8_t>& tx_extra);
    bool validateDIGMAlbumReleaseTransaction(const std::vector<uint8_t>& tx_extra);
    bool validateDIGMAlbumUpdateTransaction(const std::vector<uint8_t>& tx_extra);

    // Utility functions
    bool isDIGMTransaction(const std::vector<uint8_t>& tx_extra);
    uint64_t parseDIGMAmount(const std::vector<uint8_t>& tx_extra);
    std::vector<uint8_t> createDIGMTxExtra(uint64_t amount, bool is_mint = false);
    
    // Album operations
    bool createAlbumRelease(const DIGMAlbumRelease& album_release);
    bool updateAlbumMetadata(const DIGMAlbumUpdate& album_update);
    DIGMAlbumRelease getAlbumRelease(uint64_t album_id);
    std::vector<DIGMAlbumRelease> getArtistAlbums(const std::string& artist_address);

private:
    DIGMTokenInfo m_digm_token;
    std::vector<DIGMOutput> m_digm_outputs;
    bool m_is_initialized;

    // Internal validation
    bool validateDIGMTokenId(uint64_t token_id);
    bool validateDIGMAmount(uint64_t amount);
    bool validateDIGMTotalSupply(uint64_t amount);
};

// DIGM Constants
namespace DIGMConstants {
    const uint64_t DIGM_TOKEN_ID = 0x4449474D00000000;  // "DIGM" in hex
    const uint64_t DIGM_TOTAL_SUPPLY = 100000;          // 100,000 DIGM tokens
    const uint64_t DIGM_AMOUNT_PER_OUTPUT = 10;         // 10 heat (0.000001 XFG) = 1 DIGM
    const uint64_t DIGM_TOTAL_XFG_AMOUNT = 100000;      // 0.1 XFG total (100,000 * 0.000001)
    const uint8_t DIGM_TX_EXTRA_TAG = 0x0A;             // Album tag for DIGM in tx_extra
    
    const std::string DIGM_TOKEN_NAME = "DIGM";
    const uint32_t DIGM_MINT_HEIGHT = 1000000; // Example mint height
    
    // Atomic unit conversion
    const uint64_t XFG_TO_HEAT = 10000000;              // 1 XFG = 10,000,000 heat
    const uint64_t HEAT_TO_DIGM = 10;                   // 10 heat = 1 DIGM
    
    // Transaction type constants
    const uint8_t DIGM_TX_TYPE_MINT = 1;
    const uint8_t DIGM_TX_TYPE_TRANSFER = 2;
    const uint8_t DIGM_TX_TYPE_ALBUM_RELEASE = 3;
    const uint8_t DIGM_TX_TYPE_ALBUM_UPDATE = 4;
}

} // namespace CryptoNote
