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
  // Sia doesn't have a direct listunspent endpoint like Bitcoin
  // We need to scan the blockchain for UTXOs
  // For now, we'll use a simplified approach
  std::string response = httpGet("/explorer/addresses/" + address);
  if (response.empty()) {
    return false;
  }

  // Parse the response (simplified)
  // In a real implementation, we would parse the JSON properly
  // and extract UTXOs

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
  // Sia addresses are base64-encoded and start with "a" for mainnet
  if (address.empty() || address[0] != 'a') {
    isValid = false;
    return true;
  }

  // Check length (should be 76 characters for mainnet)
  if (address.size() != 76) {
    isValid = false;
    return true;
  }

  // Try to decode as base64
  try {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> char_map(256, -1);
    for (int i = 0; i < 64; ++i) {
      char_map[static_cast<unsigned char>(chars[i])] = i;
    }

    std::vector<uint8_t> decoded;
    int val = 0, valb = -8;
    for (unsigned char c : address) {
      if (char_map[c] == -1) {
        isValid = false;
        return true;
      }
      val = (val << 6) + char_map[c];
      valb += 6;
      if (valb >= 0) {
        decoded.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
        valb -= 8;
      }
    }

    // Check decoded length (should be 33 bytes: 1 version + 32 unlock hash)
    isValid = (decoded.size() == 33);
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
  // TODO: Implement HTLC locking for Sia
  // This requires:
  // 1. Create the HTLC redeem script using SiaHtlcScript::createRedeemScript
  // 2. Build the transaction using SiaHtlcScript::buildRawTransaction
  // 3. Sign the transaction with the sender's private key
  // 4. Submit the transaction to the network
  // 5. Extract the txid and redeem script

  return false;
}

bool SiaRpcClient::verifyLock(const std::string& htlcAddress,
                              uint64_t expectedHastings,
                              uint32_t minConfirms) {
  // TODO: Implement HTLC lock verification for Sia
  // This requires:
  // 1. Get the current block height
  // 2. Scan the blockchain for UTXOs at the HTLC address
  // 3. Verify that the amount matches
  // 4. Verify that the confirmations meet the minimum

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
  // TODO: Implement HTLC claim for Sia
  // This requires:
  // 1. Create the claim condition using SiaHtlcScript::createClaimCondition
  // 2. Build the transaction using SiaHtlcScript::buildRawTransaction
  // 3. Sign the transaction with the claimer's private key
  // 4. Submit the transaction to the network
  // 5. Extract the txid

  return false;
}

bool SiaRpcClient::refundHtlc(const std::string& senderPrivKeyHex,
                              const std::string& htlcTxid,
                              uint32_t htlcVout,
                              uint64_t htlcAmount,
                              const std::string& redeemScriptHex,
                              uint32_t timeoutBlock,
                              const std::string& destAddress,
                              std::string& refundTxId) {
  // TODO: Implement HTLC refund for Sia
  // This requires:
  // 1. Create the refund condition using SiaHtlcScript::createRefundCondition
  // 2. Build the transaction using SiaHtlcScript::buildRawTransaction
  // 3. Sign the transaction with the sender's private key
  // 4. Submit the transaction to the network
  // 5. Extract the txid

  return false;
}

} // namespace XfgSwap
