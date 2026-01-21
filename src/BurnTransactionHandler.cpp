// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2025 Elderfire Privacy Group
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

#include "BurnTransactionHandler.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "Common/StringTools.h"
#include "Common/FileSystem.h"
#include <fstream>
#include <thread>
#include <future>
#include <regex>

namespace CryptoNote {

class BurnTransactionHandler::Impl {
public:
    BurnTransactionHandler::BurnDetectedCallback burnDetectedCallback;
    BurnTransactionHandler::StarkProofGeneratedCallback starkProofGeneratedCallback;
    BurnTransactionHandler::ErrorCallback errorCallback;

    bool autoGenerateProofs = true;
    std::string starkCliPath;

    Impl() {
        // Try to find STARK CLI in common locations
        findStarkCli();
    }

    void findStarkCli() {
        std::vector<std::string> possiblePaths = {
            "./xfg-stark-cli",
            "../xfgwin/target/debug/xfg-stark-cli",
            "/usr/local/bin/xfg-stark-cli",
            "/opt/homebrew/bin/xfg-stark-cli"
        };

        for (const auto& path : possiblePaths) {
            if (Common::fileExists(path)) {
                starkCliPath = path;
                break;
            }
        }
    }
};

BurnTransactionHandler::BurnTransactionHandler() : m_impl(std::unique_ptr<Impl>(new Impl())) {
}

BurnTransactionHandler::~BurnTransactionHandler() = default;

bool BurnTransactionHandler::isBurnTransaction(const std::vector<uint8_t>& txExtra) {
    if (txExtra.empty()) {
        return false;
    }

    try {
        // Parse tx_extra to look for HEAT commitment (0x08 tag) or YIELD commitment (0x07 tag)
        return parseBurnTransaction(txExtra).isValid;
    } catch (const std::exception&) {
        return false;
    }
}

BurnTransactionHandler::BurnTransactionData BurnTransactionHandler::parseBurnTransaction(const std::vector<uint8_t>& txExtra) {
    BurnTransactionData data;

    if (txExtra.empty()) {
        return data;
    }

    try {
        size_t pos = 0;
        while (pos < txExtra.size()) {
            if (pos >= txExtra.size()) {
                break;
            }

            // Read tag
            uint8_t tag = txExtra[pos];
            pos += 1;

            if (tag == TX_EXTRA_HEAT_COMMITMENT) {
                // Found HEAT commitment tag (0x08)
                return parseHeatCommitment(txExtra, pos);
            } else if (tag == TX_EXTRA_YIELD_COMMITMENT) {
                // Found YIELD commitment tag (0x07) for YIELD_DEPOSITS / FuegoMob
                return parseYieldCommitment(txExtra, pos);
            } else if (tag == 0x00) { // TX_EXTRA_TAG_PADDING
                // Skip padding
                size_t paddingSize = 1;
                while (pos < txExtra.size() && paddingSize <= 255) {
                    if (txExtra[pos] != 0) {
                        break;
                    }
                    pos += 1;
                    paddingSize += 1;
                }
            } else {
                // Skip other tags - read varint size
                if (pos >= txExtra.size()) {
                    break;
                }

                // Read varint size
                size_t size = 0;
                size_t shift = 0;
                while (pos < txExtra.size()) {
                    uint8_t byte = txExtra[pos];
                    pos += 1;
                    size |= (byte & 0x7F) << shift;
                    if ((byte & 0x80) == 0) {
                        break;
                    }
                    shift += 7;
                    if (shift >= 32) { // Prevent infinite loop
                        return data;
                    }
                }

                // Skip the data
                pos += size;
            }
        }
    } catch (const std::exception&) {
        // Parsing failed
    }

    return data;
}

BurnTransactionHandler::BurnTransactionData BurnTransactionHandler::parseHeatCommitment(const std::vector<uint8_t>& txExtra, size_t pos) {
    BurnTransactionData data;

    try {
        // Read commitment hash (32 bytes)
        if (pos + 32 > txExtra.size()) {
            return data;
        }

        data.commitmentHash = Common::toHex(txExtra.data() + pos, 32);
        pos += 32;

        // Read amount (8 bytes, little-endian)
        if (pos + 8 > txExtra.size()) {
            return data;
        }

        data.amount = 0;
        for (int i = 0; i < 8; ++i) {
            data.amount |= static_cast<uint64_t>(txExtra[pos + i]) << (i * 8);
        }
        pos += 8;

        // Read metadata size (1 byte)
        if (pos >= txExtra.size()) {
            return data;
        }

        uint8_t metadataSize = txExtra[pos];
        pos += 1;

        // Read metadata
        if (metadataSize > 0) {
            if (pos + metadataSize > txExtra.size()) {
                return data;
            }
            data.metadata = std::string(reinterpret_cast<const char*>(txExtra.data() + pos), metadataSize);

            // Try to extract Ethereum address from metadata
            data.ethAddress = BurnTransactionHandler::extractEthereumAddress(data.metadata);
        }

        data.isValid = true;

    } catch (const std::exception&) {
        // Parsing failed
    }

    return data;
}

BurnTransactionHandler::BurnTransactionData BurnTransactionHandler::parseYieldCommitment(const std::vector<uint8_t>& txExtra, size_t pos) {
    BurnTransactionData data;

    try {
        // Read commitment hash (32 bytes)
        if (pos + 32 > txExtra.size()) {
            return data;
        }

        data.commitmentHash = Common::toHex(txExtra.data() + pos, 32);
        pos += 32;

        // Read amount (8 bytes, little-endian)
        if (pos + 8 > txExtra.size()) {
            return data;
        }

        data.amount = 0;
        for (int i = 0; i < 8; ++i) {
            data.amount |= static_cast<uint64_t>(txExtra[pos + i]) << (i * 8);
        }
        pos += 8;

        // Skip term_months (4 bytes) and yield_scheme (variable length) for yield deposits
        if (pos + 4 > txExtra.size()) {
            return data;
        }
        pos += 4; // Skip term_months

        // Skip yield_scheme length and string
        if (pos >= txExtra.size()) {
            return data;
        }
        uint8_t schemeLen = txExtra[pos];
        pos += 1;
        if (pos + schemeLen > txExtra.size()) {
            return data;
        }
        pos += schemeLen;

        // Read metadata size (1 byte)
        if (pos >= txExtra.size()) {
            return data;
        }

        uint8_t metadataSize = txExtra[pos];
        pos += 1;

        // Read metadata
        if (metadataSize > 0) {
            if (pos + metadataSize > txExtra.size()) {
                return data;
            }
            data.metadata = std::string(reinterpret_cast<const char*>(txExtra.data() + pos), metadataSize);

            // Try to extract Ethereum address from metadata
            data.ethAddress = BurnTransactionHandler::extractEthereumAddress(data.metadata);
        }

        data.isValid = true;

    } catch (const std::exception&) {
        // Parsing failed
    }

    return data;
}


std::string BurnTransactionHandler::extractEthereumAddress(const std::string& metadata) {
    // Look for Ethereum address pattern (0x followed by 40 hex chars)
    std::regex ethAddressPattern(R"(0x[a-fA-F0-9]{40})");
    std::smatch match;

    if (std::regex_search(metadata, match, ethAddressPattern)) {
        return match.str();
    }

    return "";
}

bool BurnTransactionHandler::generateStarkProof(const std::string& txHash,
                                              const std::string& ethAddress,
                                              uint64_t amount,
                                              uint64_t blockHeight) {
    if (m_impl->starkCliPath.empty()) {
        if (m_impl->errorCallback) {
            m_impl->errorCallback("STARK CLI not found. Please set the path to xfg-stark-cli.");
        }
        return false;
    }

    // Run STARK proof generation in a separate thread
    std::thread([this, txHash, ethAddress, amount, blockHeight]() {
        try {
            // Create temporary directory for proof files
            std::string tempDir = "/tmp/fuego-stark-proofs";
            Common::createDirectory(tempDir);

            // Create package file
            std::string packageFile = tempDir + "/package_" + txHash + ".json";
            std::string proofFile = tempDir + "/proof_" + txHash + ".json";

            // Create JSON package
            std::ofstream package(packageFile);
            package << "{\n";
            package << "  \"burn_transaction\": {\n";
            package << "    \"transaction_hash\": \"" << txHash << "\",\n";
            package << "    \"burn_amount_xfg\": " << amount << ",\n";
            package << "    \"block_height\": " << blockHeight << "\n";
            package << "  },\n";
            package << "  \"recipient\": {\n";
            package << "    \"ethereum_address\": \"" << ethAddress << "\"\n";
            package << "  },\n";
            package << "  \"secret\": {\n";
            package << "    \"secret_key\": \"\"\n";
            package << "  },\n";
            package << "  \"metadata\": {\n";
            package << "    \"created_at\": \"" << std::time(nullptr) << "\",\n";
            package << "    \"description\": \"Auto-generated for burn transaction " << txHash << "\"\n";
            package << "  }\n";
            package << "}\n";
            package.close();

            // Run the STARK CLI
            std::string command = m_impl->starkCliPath + " generate " + packageFile + " " + proofFile;
            int result = std::system(command.c_str());

            if (result == 0) {
                // Read the generated proof
                std::ifstream proof(proofFile);
                std::string proofData((std::istreambuf_iterator<char>(proof)), std::istreambuf_iterator<char>());
                proof.close();

                if (m_impl->starkProofGeneratedCallback) {
                    m_impl->starkProofGeneratedCallback(txHash, proofData);
                }
            } else {
                if (m_impl->errorCallback) {
                    m_impl->errorCallback("STARK proof generation failed for transaction: " + txHash);
                }
            }

        } catch (const std::exception& e) {
            if (m_impl->errorCallback) {
                m_impl->errorCallback("Error generating STARK proof: " + std::string(e.what()));
            }
        }
    }).detach();

    return true;
}

void BurnTransactionHandler::setBurnDetectedCallback(BurnDetectedCallback callback) {
    m_impl->burnDetectedCallback = callback;
}

void BurnTransactionHandler::setStarkProofGeneratedCallback(StarkProofGeneratedCallback callback) {
    m_impl->starkProofGeneratedCallback = callback;
}

void BurnTransactionHandler::setErrorCallback(ErrorCallback callback) {
    m_impl->errorCallback = callback;
}

void BurnTransactionHandler::setAutoGenerateProofs(bool enabled) {
    m_impl->autoGenerateProofs = enabled;
}

bool BurnTransactionHandler::isAutoGenerateProofsEnabled() const {
    return m_impl->autoGenerateProofs;
}

std::string BurnTransactionHandler::getStarkCliPath() const {
    return m_impl->starkCliPath;
}

void BurnTransactionHandler::setStarkCliPath(const std::string& path) {
    m_impl->starkCliPath = path;
}

bool BurnTransactionHandler::hasBurnDetectedCallback() const {
    return static_cast<bool>(m_impl->burnDetectedCallback);
}

void BurnTransactionHandler::triggerBurnDetectedCallback(const std::string& txHash, uint64_t amount, const std::string& ethAddress) {
    if (m_impl->burnDetectedCallback) {
        m_impl->burnDetectedCallback(txHash, amount, ethAddress);
    }
};

// BurnTransactionManager implementation

// BurnTransactionManager implementation
class BurnTransactionManager::Impl {
public:
    std::unique_ptr<BurnTransactionHandler> handler;
    bool autoProcessing = true;

    Impl() : handler(std::unique_ptr<BurnTransactionHandler>(new BurnTransactionHandler())) {}
};

BurnTransactionManager::BurnTransactionManager() : m_impl(std::unique_ptr<Impl>(new Impl())) {
}

BurnTransactionManager::~BurnTransactionManager() = default;

bool BurnTransactionManager::initialize(const std::string& starkCliPath) {
    if (!starkCliPath.empty()) {
        m_impl->handler->setStarkCliPath(starkCliPath);
    }
    return true;
}

void BurnTransactionManager::processTransaction(const std::string& txHash,
                                               const std::vector<uint8_t>& txExtra,
                                               uint64_t amount) {
    if (!m_impl->autoProcessing) {
        return;
    }

    if (m_impl->handler->isBurnTransaction(txExtra)) {
        auto burnData = m_impl->handler->parseBurnTransaction(txExtra);
        if (burnData.isValid) {
            // Notify that a burn transaction was detected
            m_impl->handler->triggerBurnDetectedCallback(txHash, burnData.amount, burnData.ethAddress);

            // Auto-generate STARK proof if enabled
            if (m_impl->handler->isAutoGenerateProofsEnabled() && !burnData.ethAddress.empty()) {
                m_impl->handler->generateStarkProof(txHash, burnData.ethAddress, burnData.amount);
            }
        }
    }
}

BurnTransactionHandler& BurnTransactionManager::getHandler() {
    return *m_impl->handler;
}

void BurnTransactionManager::setBurnDetectedCallback(BurnTransactionHandler::BurnDetectedCallback callback) {
    m_impl->handler->setBurnDetectedCallback(callback);
}

void BurnTransactionManager::setStarkProofGeneratedCallback(BurnTransactionHandler::StarkProofGeneratedCallback callback) {
    m_impl->handler->setStarkProofGeneratedCallback(callback);
}

void BurnTransactionManager::setErrorCallback(BurnTransactionHandler::ErrorCallback callback) {
    m_impl->handler->setErrorCallback(callback);
}

void BurnTransactionManager::setAutoProcessing(bool enabled) {
    m_impl->autoProcessing = enabled;
}

bool BurnTransactionManager::isAutoProcessingEnabled() const {
    return m_impl->autoProcessing;
}

} // namespace CryptoNote
