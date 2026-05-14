// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2025 Elderfire Privacy Group
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

#include "BurnProofDataFileGenerator.h"
#include "../crypto/keccak.h"
#include "../Common/StringTools.h"
#include "../Common/JsonValue.h"
#include <fstream>
#include <sstream>
#include <chrono>


namespace CryptoNote {

// Helper structure for BPDF validation


std::error_code BurnProofDataFileGenerator::generateBPDF(
    const std::string& txHash,
    const Crypto::SecretKey& secret,
    const std::string& recipientAddress,
    uint64_t amount,
    const std::string& outputPath) {

    // Validate Arbitrum address
    if (!isValidArbitrumAddress(recipientAddress)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    // Validate XFG amount (supports both 0.8 XFG and 800 XFG)
    if (!isValidXfgAmount(amount)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    // Calculate cryptographic hashes (same as xfgwinter)
    Crypto::Hash nullifier = calculateNullifier(secret);
    Crypto::Hash commitment = calculateCommitment(secret, amount);
    Crypto::Hash recipientHash = calculateRecipientHash(recipientAddress);
    Crypto::Hash txExtraHash = calculateTxExtraHash(secret);

    // Calculate network validation hash
    std::string genesisTx = "013c01ff0001b4bcc29101029b2e4c0281c0b02e7c53291a94d1d0cbff8883f8024f5142ee494ffbbd0880712101bd4e0bf284c04d004fd016a21405046e8267ef81328cabf3017c4c24b273b25a";
    // Fuego Network ID: 93385046440755750514194170694064996624
    Crypto::Hash networkValidationHash = calculateNetworkValidationHash(0, genesisTx); // TODO: Fix large integer

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    // Create JSON structure compatible with xfgwinter
    std::ostringstream json;
    json << "{\n";
    json << "  \"metadata\": {\n";
    json << "    \"version\": \"1.0\",\n";
    json << "    \"proof_type\": \"XFG_BURN\",\n";
    json << "    \"transaction_hash\": \"" << txHash << "\",\n";
    json << "    \"created_at\": " << timestamp << ",\n";
    json << "    \"format_version\": \"1.0\"\n";
    json << "  },\n";
    json << "  \"cryptographic_data\": {\n";
    json << "    \"secret\": \"0x" << Common::podToHex(secret) << "\",\n";
    json << "    \"nullifier\": \"0x" << Common::podToHex(nullifier) << "\",\n";
    json << "    \"commitment\": \"0x" << Common::podToHex(commitment) << "\",\n";
    json << "    \"block_height\": 0,\n";
    json << "    \"xfg_amount\": " << amount << ",\n";
    json << "    \"tx_extra_hash\": \"0x0000000000000000000000000000000000000000000000000000000000000000\"\n";
    json << "  },\n";
    json << "  \"user_data\": {\n";
    json << "    \"recipient_address\": \"" << recipientAddress << "\",\n";
    json << "    \"recipient_hash\": \"0x" << Common::podToHex(recipientHash) << "\",\n";
    json << "    \"heat_amount\": " << (amount * 10) << ",\n";
    json << "    \"xfg_amount_formatted\": \"" << (amount / 10000000.0) << " XFG\",\n";
    json << "    \"heat_amount_formatted\": \"" << (amount * 10) << " HEAT\",\n";
    json << "    \"transaction_timestamp\": " << timestamp << "\n";
    json << "  },\n";
    json << "  \"security\": {\n";
    json << "    \"signature\": \"\",\n";
    json << "    \"checksum\": \"\",\n";
    json << "    \"signature_pubkey\": \"\",\n";
    json << "    \"integrity_hash\": \"\",\n";
    json << "    \"genesis_validation\": {\n";
    json << "      \"genesis_transaction_hash\": \"0x013c01ff0001b4bcc29101029b2e4c0281c0b02e7c53291a94d1d0cbff8883f8024f5142ee494ffbbd0880712101bd4e0bf284c04d004fd016a21405046e8267ef81328cabf3017c4c24b273b25a\",\n";
    json << "      \"genesis_block_hash\": \"0x0000000000000000000000000000000000000000000000000000000000000000\",\n";
    json << "      \"genesis_timestamp\": 0,\n";
    json << "      \"genesis_validation_hash\": \"0x0000000000000000000000000000000000000000000000000000000000000000\",\n";
    json << "      \"fuego_network_id\": 93385046440755750514194170694064996624,\n";
    json << "      \"network_validation_hash\": \"0x" << Common::podToHex(networkValidationHash) << "\"\n";
    json << "    }\n";
    json << "  }\n";
    json << "}\n";

    // Save to file
    return saveToFile(json.str(), outputPath);
}

std::error_code BurnProofDataFileGenerator::extractSecretFromTransaction(
    const std::string& txHash,
    Crypto::SecretKey& secret,
    uint64_t& amount) {

    // TODO: Implement transaction parsing to extract secret and amount
    // This would parse the transaction extra field to get the secret
    // For now, return error (placeholder implementation)
    return std::make_error_code(std::errc::not_supported);
}

bool BurnProofDataFileGenerator::validateBPDF(const std::string& filePath) {
    try {
        // Read the BPDF file
        std::ifstream file(filePath);
        if (!file.is_open()) {
            return false;
        }

        std::string jsonContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        // Parse JSON content
        Common::JsonValue json;
        try {
            json = Common::JsonValue::fromString(jsonContent);
        } catch (...) {
            return false;
        }

        // Validate JSON structure
        if (!validateJsonStructure(json)) {
            return false;
        }

        // Extract data for validation
        BPDFData data;
        if (!extractBPDFData(json, data)) {
            return false;
        }

        // Validate cryptographic hashes
        if (!validateCryptographicHashes(data)) {
            return false;
        }

        // Validate data integrity
        if (!validateDataIntegrity(data)) {
            return false;
        }

        // Validate format and constraints
        if (!validateFormatConstraints(data)) {
            return false;
        }

        return true;

    } catch (const std::exception&) {
        return false;
    }
}

Crypto::Hash BurnProofDataFileGenerator::calculateNullifier(const Crypto::SecretKey& secret) {
    // Same algorithm as xfgwinter: keccak256(secret + "nullifier")
    std::vector<uint8_t> data;
    data.insert(data.end(), secret.data, secret.data + sizeof(secret.data));
    data.insert(data.end(), (uint8_t*)"nullifier", (uint8_t*)"nullifier" + 9);

    Crypto::Hash nullifier;
    keccak(data.data(), data.size(), nullifier.data, sizeof(nullifier.data));
    return nullifier;
}

Crypto::Hash BurnProofDataFileGenerator::calculateCommitment(const Crypto::SecretKey& secret, uint64_t amount) {
    // Match xfgwinter: keccak256(secret + "commitment")
    std::vector<uint8_t> data;
    data.insert(data.end(), secret.data, secret.data + sizeof(secret.data));
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&amount), reinterpret_cast<const uint8_t*>(&amount) + sizeof(amount));
    data.insert(data.end(), (uint8_t*)"commitment", (uint8_t*)"commitment" + 10);

    Crypto::Hash commitment;
    keccak(data.data(), data.size(), commitment.data, sizeof(commitment.data));
    return commitment;
}

Crypto::Hash BurnProofDataFileGenerator::calculateRecipientHash(const std::string& recipientAddress) {
    // Same algorithm as xfgwinter: keccak256(recipient_address)
    std::vector<uint8_t> addressData(recipientAddress.begin(), recipientAddress.end());
    Crypto::Hash recipientHash;
    keccak(addressData.data(), addressData.size(), recipientHash.data, sizeof(recipientHash.data));
    return recipientHash;
}

Crypto::Hash BurnProofDataFileGenerator::calculateTxExtraHash(const Crypto::SecretKey& secret) {
    // Match xfgwinter: keccak256(secret) - just the secret
    Crypto::Hash txExtraHash;
    keccak(secret.data, sizeof(secret.data), txExtraHash.data, sizeof(txExtraHash.data));
    return txExtraHash;
}

Crypto::Hash BurnProofDataFileGenerator::calculateNetworkValidationHash(uint64_t networkId, const std::string& genesisTx) {
    // Match xfgwinter: keccak256(network_id + genesis_tx)
    std::vector<uint8_t> data;

    // Add network ID as string
    std::string networkIdStr = std::to_string(networkId);
    data.insert(data.end(), networkIdStr.begin(), networkIdStr.end());

    // Add genesis transaction hash
    data.insert(data.end(), genesisTx.begin(), genesisTx.end());

    Crypto::Hash networkHash;
    keccak(data.data(), data.size(), networkHash.data, sizeof(networkHash.data));
    return networkHash;
}

bool BurnProofDataFileGenerator::isValidArbitrumAddress(const std::string& address) {
    // Basic Arbitrum address validation
    if (address.length() != 42) return false;  // 0x + 40 hex chars
    if (address.substr(0, 2) != "0x") return false;

    // Check if all characters after 0x are hex
    for (size_t i = 2; i < address.length(); i++) {
        if (!isxdigit(address[i])) return false;
    }

    return true;
}

bool BurnProofDataFileGenerator::isValidXfgAmount(uint64_t amount) {
    // Validate XFG amount - supports all 4 tiers for both HEAT and COLD
    // XFG has 7 decimal places, so 1 XFG = 10,000,000 atomic units
    switch (amount) {
        case 8000000ULL:      // 0.8 XFG  (Tier 0)
        case 80000000ULL:     // 8 XFG    (Tier 1)
        case 800000000ULL:    // 80 XFG   (Tier 2)
        case 8000000000ULL:   // 800 XFG  (Tier 3)
            return true;
        default:
            return false;
    }
}

std::string BurnProofDataFileGenerator::generateFilename(const std::string& txHash) {
    // Generate filename from transaction hash
    std::string shortHash = txHash.substr(0, 8);
    return "burn_proof_" + shortHash + ".json";
}

std::error_code BurnProofDataFileGenerator::saveToFile(const std::string& jsonData, const std::string& outputPath) {
    try {
        std::ofstream file(outputPath);
        if (!file.is_open()) {
            return std::make_error_code(std::errc::io_error);
        }

        file << jsonData;
        file.close();

        return std::error_code();
    } catch (const std::exception&) {
        return std::make_error_code(std::errc::io_error);
    }
}

// BPDF Validation Helper Functions

bool BurnProofDataFileGenerator::validateJsonStructure(const Common::JsonValue& json) {
    // Check if root is an object
    if (json.getType() != Common::JsonValue::OBJECT) {
        return false;
    }

    // Validate required top-level sections
    const std::vector<std::string> requiredSections = {
        "metadata", "cryptographic_data", "user_data", "security"
    };

    for (const auto& section : requiredSections) {
        if (!json.contains(section) || json(section).getType() != Common::JsonValue::OBJECT) {
            return false;
        }
    }

    // Validate metadata section
    const Common::JsonValue& metadata = json("metadata");
    const std::vector<std::string> metadataFields = {
        "version", "proof_type", "transaction_hash", "created_at", "format_version"
    };

    for (const auto& field : metadataFields) {
        if (!metadata.contains(field)) {
            return false;
        }
    }

    // Validate cryptographic_data section
    const Common::JsonValue& cryptoData = json("cryptographic_data");
    const std::vector<std::string> cryptoFields = {
        "secret", "nullifier", "commitment", "block_height", "xfg_amount", "tx_extra_hash"
    };

    for (const auto& field : cryptoFields) {
        if (!cryptoData.contains(field)) {
            return false;
        }
    }

    // Validate user_data section
    const Common::JsonValue& userData = json("user_data");
    const std::vector<std::string> userFields = {
        "recipient_address", "recipient_hash", "heat_amount",
        "xfg_amount_formatted", "heat_amount_formatted", "transaction_timestamp"
    };

    for (const auto& field : userFields) {
        if (!userData.contains(field)) {
            return false;
        }
    }

    // Validate security section
    const Common::JsonValue& security = json("security");
    if (!security.contains("genesis_validation") || security("genesis_validation").getType() != Common::JsonValue::OBJECT) {
        return false;
    }

    const Common::JsonValue& genesisValidation = security("genesis_validation");
    const std::vector<std::string> genesisFields = {
        "genesis_transaction_hash", "genesis_block_hash", "genesis_timestamp",
        "genesis_validation_hash", "fuego_network_id", "network_validation_hash"
    };

    for (const auto& field : genesisFields) {
        if (!genesisValidation.contains(field)) {
            return false;
        }
    }

    return true;
}

bool BurnProofDataFileGenerator::extractBPDFData(const Common::JsonValue& json, BPDFData& data) {
    try {
        // Extract metadata
        const Common::JsonValue& metadata = json("metadata");
        data.version = metadata("version").getString();
        data.proofType = metadata("proof_type").getString();
        data.transactionHash = metadata("transaction_hash").getString();
        data.createdAt = static_cast<uint64_t>(metadata("created_at").getInteger());
        data.formatVersion = metadata("format_version").getString();

        // Extract cryptographic data
        const Common::JsonValue& cryptoData = json("cryptographic_data");
        data.secret = cryptoData("secret").getString();
        data.nullifier = cryptoData("nullifier").getString();
        data.commitment = cryptoData("commitment").getString();
        data.blockHeight = static_cast<uint32_t>(cryptoData("block_height").getInteger());
        data.xfgAmount = static_cast<uint64_t>(cryptoData("xfg_amount").getInteger());
        data.txExtraHash = cryptoData("tx_extra_hash").getString();

        // Extract user data
        const Common::JsonValue& userData = json("user_data");
        data.recipientAddress = userData("recipient_address").getString();
        data.recipientHash = userData("recipient_hash").getString();
        data.heatAmount = static_cast<uint64_t>(userData("heat_amount").getInteger());
        data.xfgAmountFormatted = userData("xfg_amount_formatted").getString();
        data.heatAmountFormatted = userData("heat_amount_formatted").getString();
        data.transactionTimestamp = static_cast<uint64_t>(userData("transaction_timestamp").getInteger());

        // Extract security data
        const Common::JsonValue& security = json("security");
        data.signature = security("signature").getString();
        data.checksum = security("checksum").getString();
        data.signaturePubkey = security("signature_pubkey").getString();
        data.integrityHash = security("integrity_hash").getString();

        // Extract genesis validation data
        const Common::JsonValue& genesisValidation = security("genesis_validation");
        data.genesisTransactionHash = genesisValidation("genesis_transaction_hash").getString();
        data.genesisBlockHash = genesisValidation("genesis_block_hash").getString();
        data.genesisTimestamp = static_cast<uint64_t>(genesisValidation("genesis_timestamp").getInteger());
        data.genesisValidationHash = genesisValidation("genesis_validation_hash").getString();
        data.fuegoNetworkId = static_cast<uint64_t>(genesisValidation("fuego_network_id").getInteger());
        data.networkValidationHash = genesisValidation("network_validation_hash").getString();

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool BurnProofDataFileGenerator::validateCryptographicHashes(const BPDFData& data) {
    // Validate secret format (should be 0x + 64 hex chars)
    if (!isValidHexString(data.secret, 66)) { // 0x + 64 chars
        return false;
    }

    // Extract secret key
    std::string secretHex = data.secret.substr(2); // Remove 0x prefix
    Crypto::SecretKey secret;
    if (!Common::fromHex(secretHex, secret.data, sizeof(secret.data))) {
        return false;
    }

    // Validate nullifier calculation
    Crypto::Hash expectedNullifier = calculateNullifier(secret);
    std::string expectedNullifierHex = "0x" + Common::podToHex(expectedNullifier);
    if (data.nullifier != expectedNullifierHex) {
        return false;
    }

    // Validate commitment calculation
    Crypto::Hash expectedCommitment = calculateCommitment(secret, data.xfgAmount);
    std::string expectedCommitmentHex = "0x" + Common::podToHex(expectedCommitment);
    if (data.commitment != expectedCommitmentHex) {
        return false;
    }

    // Validate recipient hash calculation
    Crypto::Hash expectedRecipientHash = calculateRecipientHash(data.recipientAddress);
    std::string expectedRecipientHashHex = "0x" + Common::podToHex(expectedRecipientHash);
    if (data.recipientHash != expectedRecipientHashHex) {
        return false;
    }

    // Validate tx extra hash calculation
    Crypto::Hash expectedTxExtraHash = calculateTxExtraHash(secret);
    std::string expectedTxExtraHashHex = "0x" + Common::podToHex(expectedTxExtraHash);
    if (data.txExtraHash != expectedTxExtraHashHex) {
        return false;
    }

    return true;
}

bool BurnProofDataFileGenerator::validateDataIntegrity(const BPDFData& data) {
    // Validate proof type
    if (data.proofType != "XFG_BURN") {
        return false;
    }

    // Validate version format
    if (data.version != "1.0") {
        return false;
    }

    // Validate format version
    if (data.formatVersion != "1.0") {
        return false;
    }

    // Validate timestamps are reasonable (not in future, not too old)
    uint64_t currentTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (data.createdAt > currentTime || data.createdAt < (currentTime - 31536000)) { // 1 year ago
        return false;
    }

    if (data.transactionTimestamp > currentTime || data.transactionTimestamp < (currentTime - 31536000)) {
        return false;
    }

    // Validate XFG amount
    if (!isValidXfgAmount(data.xfgAmount)) {
        return false;
    }

    // Validate heat amount calculation (should be 10x XFG amount)
    if (data.heatAmount != (data.xfgAmount * 10)) {
        return false;
    }

    // Validate formatted amounts
    double expectedXfgFormatted = data.xfgAmount / 10000000.0;
    std::ostringstream expectedXfgStr;
    expectedXfgStr << expectedXfgFormatted << " XFG";
    if (data.xfgAmountFormatted != expectedXfgStr.str()) {
        return false;
    }

    std::ostringstream expectedHeatStr;
    expectedHeatStr << data.heatAmount << " HEAT";
    if (data.heatAmountFormatted != expectedHeatStr.str()) {
        return false;
    }

    return true;
}

bool BurnProofDataFileGenerator::validateFormatConstraints(const BPDFData& data) {
    // Validate Arbitrum address format
    if (!isValidArbitrumAddress(data.recipientAddress)) {
        return false;
    }

    // Validate transaction hash format (should be hex string)
    if (!isValidHexString(data.transactionHash, 64)) {
        return false;
    }

    // Validate genesis transaction hash format
    if (!isValidHexString(data.genesisTransactionHash, 64)) {
        return false;
    }

    // Validate genesis block hash format
    if (!isValidHexString(data.genesisBlockHash, 64)) {
        return false;
    }

    // Validate network ID
    if (std::to_string(data.fuegoNetworkId) != "93385046440755750514194170694064996624") {
        return false;
    }

    // Validate genesis timestamp (should be 0 for Fuego)
    if (data.genesisTimestamp != 0) {
        return false;
    }

    // Validate block height (should be 0 for burn transactions)
    if (data.blockHeight != 0) {
        return false;
    }

    return true;
}

bool BurnProofDataFileGenerator::isValidHexString(const std::string& str, size_t expectedLength) {
    if (str.length() != expectedLength) {
        return false;
    }

    // Check if all characters are hex
    for (char c : str) {
        if (!isxdigit(c)) {
            return false;
        }
    }

    return true;
}

} // namespace CryptoNote
