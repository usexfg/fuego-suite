// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2025 Elderfire Privacy Council
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

// ============================================================
// DEPRECATED — old MVP BPDF model (pre-v3)
// ============================================================
// This file generated JSON "Burn Proof Data Files" using the old
// keccak(secret || "commitment") formula without amount/network/term binding.
// Commitment and nullifier algorithms here DO NOT match the v3 unified
// STARK AIR (burn_mint_air.rs).
//
// Replaced by: StarkCommitmentGenerator in DepositCommitment.h
// Proof gen:   xfg-stark-cli (Winterfell, uses unified v3 preimage)
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "../Common/JsonValue.h"
#include <system_error>
#include "../../include/CryptoNote.h"

namespace CryptoNote {

// Helper structure for BPDF validation
struct BPDFData {
    std::string version;
    std::string proofType;
    std::string transactionHash;
    uint64_t createdAt;
    std::string formatVersion;

    std::string secret;
    std::string nullifier;
    std::string commitment;
    uint32_t blockHeight;
    uint64_t xfgAmount;
    std::string txExtraHash;

    std::string recipientAddress;
    std::string recipientHash;
    uint64_t heatAmount;
    std::string xfgAmountFormatted;
    std::string heatAmountFormatted;
    uint64_t transactionTimestamp;

    std::string signature;
    std::string checksum;
    std::string signaturePubkey;
    std::string integrityHash;

    std::string genesisTransactionHash;
    std::string genesisBlockHash;
    uint64_t genesisTimestamp;
    std::string genesisValidationHash;
    uint64_t fuegoNetworkId;
    std::string networkValidationHash;
};

// Burn Proof Data File Generator
// Generates JSON files compatible with xfgwinter proof generation
class BurnProofDataFileGenerator {
public:
    // Generate burn proof data file (BPDF)
    static std::error_code generateBPDF(
        const std::string& txHash,
        const Crypto::SecretKey& secret,
        const std::string& recipientAddress,
        uint64_t amount,
        const std::string& outputPath);

    // Extract secret from transaction
    static std::error_code extractSecretFromTransaction(
        const std::string& txHash,
        Crypto::SecretKey& secret,
        uint64_t& amount);

    // Validate burn proof data file
    static bool validateBPDF(const std::string& filePath);

    // nullifier from secret (same as xfgwinter)
    static Crypto::Hash calculateNullifier(const Crypto::SecretKey& secret);

    // commitment from secret and amount (pure, no recipient)
    static Crypto::Hash calculateCommitment(const Crypto::SecretKey& secret, uint64_t amount);

    // recipient hash from address
    static Crypto::Hash calculateRecipientHash(const std::string& recipientAddress);

    // transaction extra hash (just the secret)
    static Crypto::Hash calculateTxExtraHash(const Crypto::SecretKey& secret);

    // network validation hash
    static Crypto::Hash calculateNetworkValidationHash(uint64_t networkId, const std::string& genesisTx);

    // Validate Arbitrum address format
    static bool isValidArbitrumAddress(const std::string& address);

    // Validate XFG amount (supports 4 tiers: 0.8, 8, 80, 800 XFG)
    static bool isValidXfgAmount(uint64_t amount);

private:
    // BPDF validation helper functions
    static bool validateJsonStructure(const Common::JsonValue& json);
    static bool extractBPDFData(const Common::JsonValue& json, BPDFData& data);
    static bool validateCryptographicHashes(const BPDFData& data);
    static bool validateDataIntegrity(const BPDFData& data);
    static bool validateFormatConstraints(const BPDFData& data);
    static bool isValidHexString(const std::string& str, size_t expectedLength);
    // Generate filename from transaction hash
    static std::string generateFilename(const std::string& txHash);

    // Save JSON to file
    static std::error_code saveToFile(const std::string& jsonData, const std::string& outputPath);
};

} // namespace CryptoNote
