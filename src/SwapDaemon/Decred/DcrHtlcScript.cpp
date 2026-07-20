#include "DcrHtlcScript.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/ripemd.h>

namespace XfgSwap {

// Decred address version bytes
static constexpr uint8_t DCR_P2PKH_VERSION = 0x07;  // mainnet P2PKH
static constexpr uint8_t DCR_P2SH_VERSION  = 0x0A;  // mainnet P2SH
static constexpr uint8_t DCR_P2PKH_TEST    = 0x1E;  // testnet P2PKH
static constexpr uint8_t DCR_P2SH_TEST     = 0x13;  // testnet P2SH

// Bitcoin Script opcodes (same as BCH/KMD)
static constexpr uint8_t OP_FALSE            = 0x00;
static constexpr uint8_t OP_TRUE             = 0x51;
static constexpr uint8_t OP_IF               = 0x63;
static constexpr uint8_t OP_ELSE             = 0x67;
static constexpr uint8_t OP_ENDIF            = 0x68;
static constexpr uint8_t OP_DROP             = 0x75;
static constexpr uint8_t OP_DUP              = 0x76;
static constexpr uint8_t OP_EQUAL            = 0x87;
static constexpr uint8_t OP_EQUALVERIFY      = 0x88;
static constexpr uint8_t OP_SHA256           = 0xA8;
static constexpr uint8_t OP_HASH160          = 0xA9;
static constexpr uint8_t OP_CHECKSIG         = 0xAC;
static constexpr uint8_t OP_CHECKLOCKTIMEVERIFY = 0xB1;

// ---- Hex conversion ---------------------------------------------------------

std::vector<uint8_t> DcrHtlcScript::hexToBytes(const std::string& hex) {
  std::vector<uint8_t> bytes;
  bytes.reserve(hex.size() / 2);
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    uint8_t hi = 0, lo = 0;
    auto hexDigit = [](char c, uint8_t& out) -> bool {
      if (c >= '0' && c <= '9') { out = c - '0'; return true; }
      if (c >= 'a' && c <= 'f') { out = c - 'a' + 10; return true; }
      if (c >= 'A' && c <= 'F') { out = c - 'A' + 10; return true; }
      return false;
    };
    if (!hexDigit(hex[i], hi) || !hexDigit(hex[i + 1], lo)) return {};
    bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return bytes;
}

std::string DcrHtlcScript::bytesToHex(const std::vector<uint8_t>& bytes) {
  static const char hexChars[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(bytes.size() * 2);
  for (uint8_t b : bytes) {
    hex.push_back(hexChars[(b >> 4) & 0x0F]);
    hex.push_back(hexChars[b & 0x0F]);
  }
  return hex;
}

// ---- Hash functions ---------------------------------------------------------

std::vector<uint8_t> DcrHtlcScript::sha256(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> hash(32);
  SHA256(data.data(), data.size(), hash.data());
  return hash;
}

std::vector<uint8_t> DcrHtlcScript::hash160(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> sha(32);
  SHA256(data.data(), data.size(), sha.data());
  std::vector<uint8_t> hash(20);
  RIPEMD160(sha.data(), sha.size(), hash.data());
  return hash;
}

// ---- Base58Check ------------------------------------------------------------

static const char kBase58Chars[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

std::string DcrHtlcScript::base58CheckEncode(uint8_t version,
                                              const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> data;
  data.push_back(version);
  data.insert(data.end(), payload.begin(), payload.end());

  // Double SHA256 for checksum
  auto c1 = sha256(data);
  auto c2 = sha256(c1);
  data.insert(data.end(), c2.begin(), c2.begin() + 4);

  // Encode
  size_t leadingZeros = 0;
  for (uint8_t b : data) { if (b == 0) leadingZeros++; else break; }

  std::string result(leadingZeros, '1');

  std::vector<uint64_t> num;
  for (uint8_t b : data) {
    uint32_t carry = b;
    for (auto& v : num) {
      carry += v << 8;
      v = carry % 58;
      carry /= 58;
    }
    while (carry > 0) {
      num.push_back(carry % 58);
      carry /= 58;
    }
  }

  for (auto it = num.rbegin(); it != num.rend(); ++it) {
    result.push_back(kBase58Chars[*it]);
  }
  return result;
}

bool DcrHtlcScript::base58CheckDecode(const std::string& encoded,
                                       uint8_t& version,
                                       std::vector<uint8_t>& payload) {
  std::vector<uint64_t> num;
  for (char c : encoded) {
    const char* p = std::find(kBase58Chars, kBase58Chars + 58, c);
    if (p == kBase58Chars + 58) return false;
    uint64_t val = p - kBase58Chars;
    for (auto& v : num) {
      val += v * 58;
      v = val & 0xFF;
      val >>= 8;
    }
    while (val > 0) {
      num.push_back(val & 0xFF);
      val >>= 8;
    }
  }

  // Count leading '1' chars — each produces a leading zero byte in decoded output
  size_t leadingZeros = 0;
  for (char c : encoded) { if (c == '1') leadingZeros++; else break; }

  std::vector<uint8_t> data(leadingZeros, 0);
  data.insert(data.end(), num.rbegin(), num.rend());
  if (data.size() < 5) return false;

  std::vector<uint8_t> payloadAndChecksum(data.begin(), data.end() - 4);
  std::vector<uint8_t> checksum(data.end() - 4, data.end());

  auto c1 = sha256(payloadAndChecksum);
  auto c2 = sha256(c1);
  if (std::memcmp(c2.data(), checksum.data(), 4) != 0) return false;

  version = data[0];
  payload.assign(data.begin() + 1, data.end() - 4);
  return true;
}

// ---- Script helpers ---------------------------------------------------------

void DcrHtlcScript::pushData(std::vector<uint8_t>& script,
                              const std::vector<uint8_t>& data) {
  if (data.size() <= 75) {
    script.push_back(static_cast<uint8_t>(data.size()));
  } else if (data.size() <= 0xFF) {
    script.push_back(0x4C);  // OP_PUSHDATA1
    script.push_back(static_cast<uint8_t>(data.size()));
  } else if (data.size() <= 0xFFFF) {
    script.push_back(0x4D);  // OP_PUSHDATA2
    script.push_back(data.size() & 0xFF);
    script.push_back((data.size() >> 8) & 0xFF);
  } else {
    script.push_back(0x4E);  // OP_PUSHDATA4
    script.push_back(data.size() & 0xFF);
    script.push_back((data.size() >> 8) & 0xFF);
    script.push_back((data.size() >> 16) & 0xFF);
    script.push_back((data.size() >> 24) & 0xFF);
  }
  script.insert(script.end(), data.begin(), data.end());
}

void DcrHtlcScript::writeCompactSize(std::vector<uint8_t>& out, uint64_t n) {
  if (n < 0xFD) {
    out.push_back(static_cast<uint8_t>(n));
  } else if (n <= 0xFFFF) {
    out.push_back(0xFD);
    out.push_back(n & 0xFF);
    out.push_back((n >> 8) & 0xFF);
  } else if (n <= 0xFFFFFFFF) {
    out.push_back(0xFE);
    out.push_back(n & 0xFF);
    out.push_back((n >> 8) & 0xFF);
    out.push_back((n >> 16) & 0xFF);
    out.push_back((n >> 24) & 0xFF);
  } else {
    out.push_back(0xFF);
    for (int i = 0; i < 8; ++i) {
      out.push_back((n >> (8 * i)) & 0xFF);
    }
  }
}

void DcrHtlcScript::writeLE32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(v & 0xFF);
  out.push_back((v >> 8) & 0xFF);
  out.push_back((v >> 16) & 0xFF);
  out.push_back((v >> 24) & 0xFF);
}

void DcrHtlcScript::writeLE64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
  }
}

// ---- HTLC -------------------------------------------------------------------

std::vector<uint8_t> DcrHtlcScript::createRedeemScript(
    const std::vector<uint8_t>& hashLockSha256,
    const std::vector<uint8_t>& recipientPubKey,
    const std::vector<uint8_t>& senderPubKey,
    uint32_t timeoutBlock) {
  if (hashLockSha256.size() != 32)
    throw std::runtime_error("DcrHtlcScript::createRedeemScript: hashLock must be 32 bytes");
  if (recipientPubKey.size() != 33)
    throw std::runtime_error("DcrHtlcScript::createRedeemScript: recipientPubKey must be 33 bytes");
  if (senderPubKey.size() != 33)
    throw std::runtime_error("DcrHtlcScript::createRedeemScript: senderPubKey must be 33 bytes");

  std::vector<uint8_t> script;
  script.reserve(128);

  script.push_back(OP_IF);
  pushData(script, hashLockSha256);
  script.push_back(OP_SHA256);
  script.push_back(OP_EQUALVERIFY);
  pushData(script, recipientPubKey);
  script.push_back(OP_CHECKSIG);
  script.push_back(OP_ELSE);
  // Encode timeoutBlock as Bitcoin CScriptNum
  if (timeoutBlock < 0x80) {
    script.push_back(static_cast<uint8_t>(timeoutBlock));
  } else if (timeoutBlock <= 0x7FFF) {
    script.push_back(0x02);
    script.push_back(timeoutBlock & 0xFF);
    script.push_back((timeoutBlock >> 8) & 0xFF);
  } else {
    script.push_back(0x03);
    script.push_back(timeoutBlock & 0xFF);
    script.push_back((timeoutBlock >> 8) & 0xFF);
    script.push_back((timeoutBlock >> 16) & 0xFF);
  }
  script.push_back(OP_CHECKLOCKTIMEVERIFY);
  script.push_back(OP_DROP);
  pushData(script, senderPubKey);
  script.push_back(OP_CHECKSIG);
  script.push_back(OP_ENDIF);

  return script;
}

std::vector<uint8_t> DcrHtlcScript::redeemScriptToP2shScriptPubKey(
    const std::vector<uint8_t>& redeemScript) {
  auto h = hash160(redeemScript);
  std::vector<uint8_t> spk;
  spk.reserve(23);
  spk.push_back(OP_HASH160);
  spk.push_back(0x14);
  spk.insert(spk.end(), h.begin(), h.end());
  spk.push_back(OP_EQUAL);
  return spk;
}

// ---- Address encoding -------------------------------------------------------

std::string DcrHtlcScript::scriptHashToAddress(const std::vector<uint8_t>& scriptHash,
                                                bool testnet) {
  return base58CheckEncode(testnet ? DCR_P2SH_TEST : DCR_P2SH_VERSION, scriptHash);
}

std::string DcrHtlcScript::pubkeyHashToAddress(const std::vector<uint8_t>& pubkeyHash,
                                                bool testnet) {
  return base58CheckEncode(testnet ? DCR_P2PKH_TEST : DCR_P2PKH_VERSION, pubkeyHash);
}

// ---- Preimage extraction ----------------------------------------------------

std::vector<uint8_t> DcrHtlcScript::parseClaimPreimage(
    const std::vector<uint8_t>& rawTx,
    const std::vector<uint8_t>& htlcP2shScriptPubKey) {
  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  auto readCompactSize = [&](uint64_t& out) -> bool {
    if (p >= end) return false;
    uint8_t first = *p++;
    if (first < 0xFD) { out = first; return true; }
    if (first == 0xFD) {
      if (p + 2 > end) return false;
      out = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
      p += 2; return true;
    }
    if (first == 0xFE) {
      if (p + 4 > end) return false;
      out = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
            (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
      p += 4; return true;
    }
    if (p + 8 > end) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) out |= static_cast<uint64_t>(p[i]) << (8 * i);
    p += 8; return true;
  };

  auto skipVarIntData = [&]() -> bool {
    uint64_t len = 0;
    if (!readCompactSize(len)) return false;
    if (p + len > end) return false;
    p += len;
    return true;
  };

  // Skip version (4 bytes)
  if (p + 4 > end) return {};
  p += 4;

  // Read vin count
  uint64_t vinCount = 0;
  if (!readCompactSize(vinCount)) return {};

  // Check each input's scriptSig for a matching P2SH redeemScript
  for (uint64_t i = 0; i < vinCount; ++i) {
    if (p + 36 > end) return {};
    p += 36;  // prev txid + vout

    // Read scriptSig
    uint64_t sigLen = 0;
    if (!readCompactSize(sigLen)) return {};
    if (p + sigLen > end) return {};

    // Scan scriptSig for data pushes — the last push should be the redeemScript
    const uint8_t* sigStart = p;
    const uint8_t* sigEnd = p + sigLen;
    const uint8_t* sp = sigStart;

    while (sp < sigEnd) {
      uint8_t pushByte = *sp++;
      if (pushByte >= 0x01 && pushByte <= 0x4B) {
        // Simple data push
        if (sp + pushByte > sigEnd) break;
        sp += pushByte;
      } else if (pushByte == 0x4C) {  // OP_PUSHDATA1
        if (sp >= sigEnd) break;
        uint8_t len = *sp++;
        sp += len;
      } else if (pushByte == 0x4D) {  // OP_PUSHDATA2
        if (sp + 2 > sigEnd) break;
        uint16_t len = static_cast<uint16_t>(sp[0]) | (static_cast<uint16_t>(sp[1]) << 8);
        sp += 2;
        sp += len;
      } else if (pushByte == 0x4E) {  // OP_PUSHDATA4
        if (sp + 4 > sigEnd) break;
        uint32_t len = static_cast<uint32_t>(sp[0]) | (static_cast<uint32_t>(sp[1]) << 8) |
                       (static_cast<uint32_t>(sp[2]) << 16) | (static_cast<uint32_t>(sp[3]) << 24);
        sp += 4;
        sp += len;
      } else {
        break;  // non-push opcode
      }
    }

    // The last item in scriptSig is the redeemScript
    // Rewind to find it
    const uint8_t* rsStart = sigStart;
    while (rsStart < sigEnd) {
      uint8_t b = *rsStart;
      uint64_t pushLen = 0;
      if (b >= 0x01 && b <= 0x4B) {
        pushLen = b;
        rsStart++;
      } else if (b == 0x4C) {
        rsStart++;
        if (rsStart >= sigEnd) break;
        pushLen = *rsStart++;
      } else {
        break;
      }

      const uint8_t* pushData = rsStart;
      if (rsStart + pushLen > sigEnd) break;

      // Check if this push is the redeemScript by hashing it
      auto h = hash160(std::vector<uint8_t>(pushData, pushData + pushLen));
      if (h.size() == 20 && htlcP2shScriptPubKey.size() >= 22 &&
          htlcP2shScriptPubKey[0] == OP_HASH160 && htlcP2shScriptPubKey[1] == 0x14) {
        if (std::memcmp(h.data(), htlcP2shScriptPubKey.data() + 2, 20) == 0) {
          // Found the redeemScript — the preimage is the item before it
          // In standard HTLC claim: <sig> <preimage> <redeemScript>
          // Walk sigStart to find the preimage push
          const uint8_t* walk = sigStart;
          std::vector<uint8_t> lastPush;
          while (walk < pushData) {
            uint8_t wb = *walk;
            uint64_t wlen = 0;
            if (wb >= 0x01 && wb <= 0x4B) {
              wlen = wb;
              walk++;
            } else if (wb == 0x4C) {
              walk++;
              if (walk >= pushData) break;
              wlen = *walk++;
            } else if (wb == 0x4D) {
              walk++;
              if (walk + 2 > pushData) break;
              wlen = static_cast<uint64_t>(walk[0]) | (static_cast<uint64_t>(walk[1]) << 8);
              walk += 2;
            } else if (wb == 0x4E) {
              walk++;
              if (walk + 4 > pushData) break;
              wlen = static_cast<uint64_t>(walk[0]) | (static_cast<uint64_t>(walk[1]) << 8) |
                     (static_cast<uint64_t>(walk[2]) << 16) | (static_cast<uint64_t>(walk[3]) << 24);
              walk += 4;
            } else {
              break;
            }
            lastPush.assign(walk, walk + wlen);
            walk += wlen;
          }
          if (lastPush.size() == 32) return lastPush;
          if (!lastPush.empty()) return lastPush;
        }
      }
      rsStart += pushLen;
    }

    p = sigEnd;
    // Skip sequence (4 bytes)
    if (p + 4 > end) return {};
    p += 4;
  }

  return {};
}

// ---- ScriptSig construction for claiming and refunding ----------------------

std::vector<uint8_t> DcrHtlcScript::createClaimScriptSig(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& preimage,
    const std::vector<uint8_t>& redeemScript) {
  //
  // To claim, the stack must contain (bottom to top):
  //   <signature> <preimage> OP_TRUE <serialized redeemScript>
  //
  std::vector<uint8_t> scriptSig;
  scriptSig.reserve(signature.size() + preimage.size() + redeemScript.size() + 10);

  pushData(scriptSig, signature);
  pushData(scriptSig, preimage);
  scriptSig.push_back(OP_TRUE);
  pushData(scriptSig, redeemScript);

  return scriptSig;
}

std::vector<uint8_t> DcrHtlcScript::createRefundScriptSig(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& redeemScript) {
  //
  // To refund, the stack must contain (bottom to top):
  //   <signature> OP_FALSE <serialized redeemScript>
  //
  std::vector<uint8_t> scriptSig;
  scriptSig.reserve(signature.size() + redeemScript.size() + 5);

  pushData(scriptSig, signature);
  scriptSig.push_back(OP_FALSE);
  pushData(scriptSig, redeemScript);

  return scriptSig;
}

// ---- Raw transaction building ------------------------------------------------

std::vector<uint8_t> DcrHtlcScript::buildRawTransaction(
    const std::string& inputTxid,
    uint32_t inputVout,
    uint64_t inputAmount,
    const std::vector<uint8_t>& scriptSig,
    const std::string& outputAddress,
    uint64_t outputAmount,
    uint32_t lockTime) {
  std::vector<uint8_t> tx;

  // Version (4 bytes LE) — DCR uses version 1 for regular transactions
  writeLE32(tx, 1);

  // Input count
  writeCompactSize(tx, 1);

  // Input: prev txid (32 bytes, reversed LE) + vout (4 bytes LE) + scriptSig + sequence (4 bytes)
  auto txidBytes = hexToBytes(inputTxid);
  if (txidBytes.size() != 32) return {};
  std::reverse(txidBytes.begin(), txidBytes.end());
  tx.insert(tx.end(), txidBytes.begin(), txidBytes.end());
  writeLE32(tx, inputVout);
  writeCompactSize(tx, scriptSig.size());
  tx.insert(tx.end(), scriptSig.begin(), scriptSig.end());
  writeLE32(tx, 0xFFFFFFFE);  // sequence (non-final for CLTV)

  // Output count
  writeCompactSize(tx, 1);

  // Output: amount (8 bytes LE) + scriptPubKey length + scriptPubKey
  writeLE64(tx, outputAmount);

  // Decode address to get scriptPubKey
  uint8_t addrVersion = 0;
  std::vector<uint8_t> addrHash;
  if (!base58CheckDecode(outputAddress, addrVersion, addrHash)) return {};

  std::vector<uint8_t> outputScript;
  if (addrHash.size() == 20) {
    if (addrVersion == DCR_P2PKH_VERSION || addrVersion == DCR_P2PKH_TEST) {
      // P2PKH output
      outputScript.push_back(OP_DUP);
      outputScript.push_back(OP_HASH160);
      outputScript.push_back(0x14);
      outputScript.insert(outputScript.end(), addrHash.begin(), addrHash.end());
      outputScript.push_back(OP_EQUALVERIFY);
      outputScript.push_back(OP_CHECKSIG);
    } else if (addrVersion == DCR_P2SH_VERSION || addrVersion == DCR_P2SH_TEST) {
      // P2SH output
      outputScript.push_back(OP_HASH160);
      outputScript.push_back(0x14);
      outputScript.insert(outputScript.end(), addrHash.begin(), addrHash.end());
      outputScript.push_back(OP_EQUAL);
    } else {
      return {};
    }
  } else {
    return {};
  }
  writeCompactSize(tx, outputScript.size());
  tx.insert(tx.end(), outputScript.begin(), outputScript.end());

  // Locktime (4 bytes LE)
  writeLE32(tx, lockTime);

  // Decred expiry (4 bytes LE) — set to 0 (no expiry)
  writeLE32(tx, 0);

  return tx;
}

} // namespace XfgSwap
