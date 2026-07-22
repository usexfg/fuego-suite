// Copyright (c) 2017-2026 Fuego Developers
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

#include "SiaRpcClient.h"
#include "SiaHtlcScript.h"
#include <httplib.h>
#include <stdexcept>
#include <regex>

namespace XfgSwap {

SiaRpcClient::SiaRpcClient(const std::string& host, uint16_t port,
                           const std::string& apiPassword)
  : m_host(host), m_port(port), m_apiPassword(apiPassword) {
  m_baseUrl = "http://" + host + ":" + std::to_string(port);
}

// =============================================================================
// HTTP helpers
// =============================================================================

std::string SiaRpcClient::httpGet(const std::string& path) {
  httplib::Client cli(m_host, m_port);

  httplib::Headers headers;
  if (!m_apiPassword.empty()) {
    headers.emplace("User-Agent", "Sia-Agent");
    // Sia uses basic auth with empty username and the password
    std::string auth = ":" + m_apiPassword;
    // Base64 encode
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    int val = 0, valb = -6;
    for (unsigned char c : auth) {
      val = (val << 8) + c;
      valb += 8;
      while (valb >= 0) {
        encoded.push_back(chars[(val >> valb) & 0x3F]);
        valb -= 6;
      }
    }
    while (valb > -6) {
      encoded.push_back('=');
      valb += 8;
    }
    headers.emplace("Authorization", "Basic " + encoded);
  }

  auto res = cli.Get(path.c_str(), headers);
  if (!res || res->status != 200) {
    return "";
  }
  return res->body;
}

std::string SiaRpcClient::httpPost(const std::string& path, const std::string& body) {
  httplib::Client cli(m_host, m_port);

  httplib::Headers headers;
  headers.emplace("Content-Type", "application/x-www-form-urlencoded");
  if (!m_apiPassword.empty()) {
    headers.emplace("User-Agent", "Sia-Agent");
    // Base64 encode for basic auth
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string auth = ":" + m_apiPassword;
    std::string encoded;
    int val = 0, valb = -6;
    for (unsigned char c : auth) {
      val = (val << 8) + c;
      valb += 8;
      while (valb >= 0) {
        encoded.push_back(chars[(val >> valb) & 0x3F]);
        valb -= 6;
      }
    }
    while (valb > -6) {
      encoded.push_back('=');
      valb += 8;
    }
    headers.emplace("Authorization", "Basic " + encoded);
  }

  auto res = cli.Post(path.c_str(), headers, body, "application/x-www-form-urlencoded");
  if (!res || res->status != 200) {
    return "";
  }
  return res->body;
}

std::string SiaRpcClient::httpPut(const std::string& path, const std::string& body) {
  httplib::Client cli(m_host, m_port);

  httplib::Headers headers;
  headers.emplace("Content-Type", "application/x-www-form-urlencoded");
  if (!m_apiPassword.empty()) {
    headers.emplace("User-Agent", "Sia-Agent");
    // Base64 encode for basic auth
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string auth = ":" + m_apiPassword;
    std::string encoded;
    int val = 0, valb = -6;
    for (unsigned char c : auth) {
      val = (val << 8) + c;
      valb += 8;
      while (valb >= 0) {
        encoded.push_back(chars[(val >> valb) & 0x3F]);
        valb -= 6;
      }
    }
    while (valb > -6) {
      encoded.push_back('=');
      valb += 8;
    }
    headers.emplace("Authorization", "Basic " + encoded);
  }

  auto res = cli.Put(path.c_str(), headers, body, "application/x-www-form-urlencoded");
  if (!res || res->status != 200) {
    return "";
  }
  return res->body;
}

std::string SiaRpcClient::httpDelete(const std::string& path) {
  httplib::Client cli(m_host, m_port);

  httplib::Headers headers;
  if (!m_apiPassword.empty()) {
    headers.emplace("User-Agent", "Sia-Agent");
    // Base64 encode for basic auth
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string auth = ":" + m_apiPassword;
    std::string encoded;
    int val = 0, valb = -6;
    for (unsigned char c : auth) {
      val = (val << 8) + c;
      valb += 8;
      while (valb >= 0) {
        encoded.push_back(chars[(val >> valb) & 0x3F]);
        valb -= 6;
      }
    }
    while (valb > -6) {
      encoded.push_back('=');
      valb += 8;
    }
    headers.emplace("Authorization", "Basic " + encoded);
  }

  auto res = cli.Delete(path.c_str(), headers);
  if (!res || res->status != 200) {
    return "";
  }
  return res->body;
}

// =============================================================================
// JSON parsing helpers (simple regex-based for now)
// =============================================================================

bool SiaRpcClient::parseJsonString(const std::string& json, const std::string& key, std::string& value) {
  // Simple regex to find "key":"value" or "key": "value"
  std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
  std::smatch match;
  if (std::regex_search(json, match, pattern)) {
    value = match[1].str();
    return true;
  }
  return false;
}

bool SiaRpcClient::parseJsonUint64(const std::string& json, const std::string& key, uint64_t& value) {
  std::regex pattern("\"" + key + "\"\\s*:\\s*(\\d+)");
  std::smatch match;
  if (std::regex_search(json, match, pattern)) {
    value = std::stoull(match[1].str());
    return true;
  }
  return false;
}

bool SiaRpcClient::parseJsonUint32(const std::string& json, const std::string& key, uint32_t& value) {
  uint64_t val = 0;
  if (parseJsonUint64(json, key, val)) {
    value = static_cast<uint32_t>(val);
    return true;
  }
  return false;
}

// =============================================================================
// Basic queries
// =============================================================================

bool SiaRpcClient::getBlockCount(uint64_t& height) {
  SiaConsensus consensus;
  if (!getConsensus(consensus)) {
    return false;
  }
  height = consensus.height;
  return true;
}

bool SiaRpcClient::getConsensus(SiaConsensus& consensus) {
  std::string response = httpGet("/consensus");
  if (response.empty()) {
    return false;
  }

  if (!parseJsonUint64(response, "height", consensus.height)) {
    return false;
  }
  if (!parseJsonString(response, "currentblockid", consensus.currentBlockId)) {
    return false;
  }
  if (!parseJsonString(response, "previousblockid", consensus.previousBlockId)) {
    return false;
  }
  if (!parseJsonUint64(response, "target", consensus.target)) {
    return false;
  }

  return true;
}

bool SiaRpcClient::getTransaction(const std::string& txid, SiaTxInfo& info) {
  // Sia doesn't have a direct gettransaction endpoint like Bitcoin
  // We need to check the transaction pool or scan the blockchain
  // For now, we'll use the transaction pool endpoint
  std::string response = httpGet("/tpool/transactions");
  if (response.empty()) {
    return false;
  }

  // Check if txid is in the transaction pool
  if (response.find(txid) != std::string::npos) {
    info.txid = txid;
    info.confirmations = 0;
    info.blockHeight = 0;
    info.inMempool = true;
    return true;
  }

  return false;
}

bool SiaRpcClient::getBalance(const std::string& address, uint64_t& hastings) {
  // Sia doesn't have a direct getbalance endpoint like Bitcoin
  // We need to scan all UTXOs and sum them for the address
  // For now, we'll use a simplified approach
  std::vector<SiaUtxo> utxos;
  if (!listUnspent(address, utxos)) {
    return false;
  }

  hastings = 0;
  for (const auto& utxo : utxos) {
    hastings += utxo.hastings;
  }
  return true;
}

// =============================================================================
// UTXO queries
// =============================================================================

bool SiaRpcClient::listUnspent(const std::string& address, std::vector<SiaUtxo>& utxos) {
  // Use the wallet/coins endpoint to get all UTXOs
  std::string response = httpGet("/wallet/coins");
  if (response.empty()) return false;

  // Parse JSON response to extract UTXOs
  // The response format is: {"coins": [{"parentid": "...", "value": "...", "unlockhash": "...", ...}]}
  // For simplicity, we'll use regex to extract values (in production, use proper JSON parser)

  // Extract coins array (simplified parsing)
  size_t coinsPos = response.find("\"coins\"");
  if (coinsPos == std::string::npos) return false;

  // Find all coin entries
  size_t pos = coinsPos;
  while ((pos = response.find("{", pos)) != std::string::npos) {
    size_t end = response.find("}", pos);
    if (end == std::string::npos) break;

    std::string coin = response.substr(pos, end - pos + 1);

    SiaUtxo utxo;

    // Extract parentid (txid)
    std::regex txidRegex("\"parentid\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch txidMatch;
    if (std::regex_search(coin, txidMatch, txidRegex)) {
      utxo.txid = txidMatch[1].str();
    }

    // Extract value (hastings)
    std::regex valueRegex("\"value\"\\s*:\\s*\"(\\d+)\"");
    std::smatch valueMatch;
    if (std::regex_search(coin, valueMatch, valueRegex)) {
      utxo.hastings = std::stoull(valueMatch[1].str());
    }

    // Extract unlockhash (address)
    std::regex addrRegex("\"unlockhash\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch addrMatch;
    if (std::regex_search(coin, addrMatch, addrRegex)) {
      utxo.address = addrMatch[1].str();
    }

    // Only add UTXOs that match the requested address
    if (utxo.address == address) {
      utxo.confirmations = 0;  // Would need to calculate from current height
      utxos.push_back(utxo);
    }

    pos = end + 1;
  }

  return true;
}

// =============================================================================
// Raw transaction
// =============================================================================

bool SiaRpcClient::getRawTransaction(const std::string& txid, std::string& rawTxHex) {
  // Sia doesn't have a direct getrawtransaction endpoint like Bitcoin
  // We need to use the explorer API or scan the blockchain
  std::string response = httpGet("/explorer/transactions/" + txid);
  if (response.empty()) {
    return false;
  }

  // Parse the response (simplified)
  // In a real implementation, we would extract the raw transaction hex
  return false;
}

bool SiaRpcClient::sendRawTransaction(const std::string& rawTxHex, std::string& txid) {
  // Sia uses a different endpoint for submitting transactions
  std::string response = httpPost("/tpool/transactions", rawTxHex);
  if (response.empty()) {
    return false;
  }

  // Parse the response to get the txid
  return parseJsonString(response, "transactionid", txid);
}

// =============================================================================
// Address utilities
// =============================================================================

bool SiaRpcClient::validateAddress(const std::string& address, bool& isValid) {
  // Sia addresses are 76-character hex strings starting with "00" for mainnet
  if (address.empty() || address.size() != 76) {
    isValid = false;
    return true;
  }

  // Check that it's valid hex
  for (char c : address) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      isValid = false;
      return true;
    }
  }

  // Check prefix (should start with "00" for mainnet)
  if (address.substr(0, 2) != "00") {
    isValid = false;
    return true;
  }

  // Try to decode as hex and verify checksum
  try {
    std::vector<uint8_t> decoded = SiaHtlcScript::hexToBytes(address);
    if (decoded.size() != 38) {  // 1 version + 32 unlock hash + 5 checksum
      isValid = false;
      return true;
    }

    // Verify checksum
    std::vector<uint8_t> unlockHash(decoded.begin() + 1, decoded.begin() + 33);
    std::vector<uint8_t> checksum(decoded.begin() + 33, decoded.end());
    std::vector<uint8_t> expectedChecksum = SiaHtlcScript::sha256(unlockHash);

    for (int i = 0; i < 5; ++i) {
      if (checksum[i] != expectedChecksum[i]) {
        isValid = false;
        return true;
      }
    }

    isValid = true;
    return true;
  } catch (...) {
    isValid = false;
    return true;
  }
}

bool SiaRpcClient::importAddress(const std::string& address, const std::string& label, bool rescan) {
  // Sia doesn't have a direct importaddress endpoint like Bitcoin
  // We need to use the renter API or wallet API
  std::string response = httpPost("/wallet/addresses", "address=" + address + "&label=" + label);
  return !response.empty();
}

// =============================================================================
// HTLC operations
// =============================================================================

bool SiaRpcClient::lockHtlc(const std::string& senderPrivKeyHex,
                            const std::string& recipientAddress,
                            const std::string& hashLockSha256Hex,
                            uint32_t timeoutBlock,
                            uint64_t amountHastings,
                            std::string& lockTxId,
                            std::string& redeemScriptHex) {
  // Convert hex strings to bytes
  auto hashLockBytes = SiaHtlcScript::hexToBytes(hashLockSha256Hex);
  if (hashLockBytes.size() != 32) return false;

  // Derive sender's ed25519 public key from private key (placeholder - needs actual derivation)
  // In production, use libsodium or similar for ed25519 key derivation
  std::vector<uint8_t> senderPubKey(32, 0x00);  // Placeholder
  std::vector<uint8_t> recipientPubKey(32, 0x00);  // Placeholder (from recipientAddress)

  // Decode recipient address to get their public key
  std::vector<uint8_t> recipientUnlockHash;
  if (!SiaHtlcScript::decodeAddress(recipientAddress, recipientUnlockHash)) return false;
  // For now, use unlock hash as public key placeholder (in reality, need to exchange pubkeys)
  recipientPubKey = recipientUnlockHash;

  // Create the HTLC redeem script
  auto redeemScript = SiaHtlcScript::createRedeemScript(
      hashLockBytes, recipientPubKey, senderPubKey, timeoutBlock);
  redeemScriptHex = SiaHtlcScript::bytesToHex(redeemScript);

  // Compute HTLC address from redeem script hash
  std::vector<uint8_t> scriptHash = SiaHtlcScript::sha256(redeemScript);
  std::string htlcAddress = SiaHtlcScript::computeAddress(scriptHash);

  // Fund the HTLC address using the wallet API
  // Sia uses hastings (1 SC = 10^24 hastings)
  std::string amountStr = std::to_string(amountHastings);
  std::string response = httpPost("/wallet/send",
      "amount=" + amountStr + "&destination=" + htlcAddress);
  if (response.empty()) return false;

  // Extract transaction ID from response
  if (!parseJsonString(response, "transactionid", lockTxId)) return false;

  return !lockTxId.empty();
}

bool SiaRpcClient::verifyLock(const std::string& htlcAddress,
                               uint64_t expectedHastings,
                               uint32_t minConfirms) {
  // Get UTXOs at the HTLC address
  std::vector<SiaUtxo> utxos;
  if (!listUnspent(htlcAddress, utxos)) return false;

  // Check for matching UTXO
  for (const auto& utxo : utxos) {
    if (utxo.hastings >= expectedHastings &&
        utxo.confirmations >= minConfirms) {
      return true;
    }
  }
  return false;
}

bool SiaRpcClient::claim(const std::string& claimerPrivKeyHex,
                        const std::string& htlcTxid,
                        uint32_t htlcVout,
                        uint64_t htlcAmount,
                        const std::string& redeemScriptHex,
                        const std::string& preimageHex,
                        const std::string& destAddress,
                        std::string& claimTxId) {
  // Convert hex strings to bytes
  auto redeemScript = SiaHtlcScript::hexToBytes(redeemScriptHex);
  auto preimage = SiaHtlcScript::hexToBytes(preimageHex);

  // Verify preimage matches hash lock
  auto preimageHash = SiaHtlcScript::sha256(preimage);
  // Extract hash lock from redeem script (bytes 2-33)
  if (redeemScript.size() < 34) return false;
  std::vector<uint8_t> hashLock(redeemScript.begin() + 2, redeemScript.begin() + 34);
  if (preimageHash != hashLock) return false;

  // Get current block height for CLTV
  uint64_t currentHeight = 0;
  if (!getBlockCount(currentHeight)) return false;

  // Build spending transaction
  // Use the wallet API to sign and broadcast
  std::string response = httpPost("/wallet/send",
      "amount=" + std::to_string(htlcAmount) +
      "&destination=" + destAddress);
  if (response.empty()) return false;

  // For HTLC claim, we need to provide the preimage
  // The actual implementation would need to:
  // 1. Create a raw transaction spending from the HTLC
  // 2. Include the preimage in the script
  // 3. Sign with the claimer's key
  // 4. Broadcast

  // For now, use the wallet's send API (simplified)
  if (!parseJsonString(response, "transactionid", claimTxId)) return false;

  return !claimTxId.empty();
}

bool SiaRpcClient::refundHtlc(const std::string& senderPrivKeyHex,
                               const std::string& htlcTxid,
                               uint32_t htlcVout,
                               uint64_t htlcAmount,
                               const std::string& redeemScriptHex,
                               uint32_t timeoutBlock,
                               const std::string& destAddress,
                               std::string& refundTxId) {
  // Check if timelock has expired
  uint64_t currentHeight = 0;
  if (!getBlockCount(currentHeight)) return false;
  if (currentHeight < timeoutBlock) return false;  // Timelock not yet expired

  // Get UTXOs from the HTLC address
  // For refund, we need to spend from the HTLC using the sender's key after timeout
  auto redeemScript = SiaHtlcScript::hexToBytes(redeemScriptHex);

  // Build the refund transaction
  // The refund uses the sender's key and requires nLocktime >= timeoutBlock
  // In Sia, we use the wallet API which handles signing internally
  std::string response = httpPost("/wallet/send",
      "amount=" + std::to_string(htlcAmount) +
      "&destination=" + destAddress);
  if (response.empty()) return false;

  // For HTLC refund, we need to:
  // 1. Create a raw transaction spending from the HTLC
  // 2. Set nLocktime >= timeoutBlock
  // 3. Sign with the sender's key
  // 4. Broadcast

  // For now, use the wallet's send API (simplified)
  if (!parseJsonString(response, "transactionid", refundTxId)) return false;

  return !refundTxId.empty();
}

} // namespace XfgSwap
