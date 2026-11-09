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

#include "UtxoHtlc.h"

#include "SwapDaemon/Bitcoin/BtcHtlcScript.h"
#include "SwapDaemon/Crypto/Base58Std.h"
#include "SwapDaemon/Crypto/Bip143Sighash.h"
#include "SwapDaemon/Crypto/LegacySighash.h"
#include "SwapDaemon/Crypto/Secp256k1Signer.h"
#include "SwapDaemon/Crypto/ripemd160.h"

#include <openssl/sha.h>

#include <algorithm>
#include <stdexcept>

namespace XfgSwap {

namespace {

constexpr uint8_t OP_0 = 0x00;
constexpr uint8_t OP_PUSHDATA1 = 0x4c;
constexpr uint8_t OP_PUSHDATA2 = 0x4d;
constexpr uint8_t OP_1 = 0x51;
constexpr uint8_t OP_IF = 0x63;
constexpr uint8_t OP_ELSE = 0x67;
constexpr uint8_t OP_ENDIF = 0x68;
constexpr uint8_t OP_DROP = 0x75;
constexpr uint8_t OP_DUP = 0x76;
constexpr uint8_t OP_EQUAL = 0x87;
constexpr uint8_t OP_EQUALVERIFY = 0x88;
constexpr uint8_t OP_SHA256 = 0xa8;
constexpr uint8_t OP_HASH160 = 0xa9;
constexpr uint8_t OP_CHECKSIG = 0xac;
constexpr uint8_t OP_CHECKLOCKTIMEVERIFY = 0xb1;

constexpr uint32_t TX_VERSION = 2;
constexpr uint32_t CLAIM_SEQUENCE = 0xFFFFFFFD;
constexpr uint32_t REFUND_SEQUENCE = 0xFFFFFFFE;
constexpr uint32_t SIGHASH_ALL = 0x01;
constexpr uint32_t SIGHASH_ALL_FORKID = 0x41;
constexpr uint32_t MIN_NON_SMALL_INT_TIMEOUT = 17;  // 1-16 would need OP_n to stay minimal

void appendLE32(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

void appendLE64(std::vector<uint8_t>& out, uint64_t value) {
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

void appendVarInt(std::vector<uint8_t>& out, uint64_t value) {
  if (value < 0xFD) {
    out.push_back(static_cast<uint8_t>(value));
  } else if (value <= 0xFFFF) {
    out.push_back(0xFD);
    for (int i = 0; i < 2; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
  } else if (value <= 0xFFFFFFFFULL) {
    out.push_back(0xFE);
    appendLE32(out, static_cast<uint32_t>(value));
  } else {
    out.push_back(0xFF);
    appendLE64(out, value);
  }
}

void appendBytes(std::vector<uint8_t>& out, const std::vector<uint8_t>& bytes) {
  out.insert(out.end(), bytes.begin(), bytes.end());
}

void appendPush(std::vector<uint8_t>& script, const std::vector<uint8_t>& data) {
  if (data.size() < OP_PUSHDATA1) {
    script.push_back(static_cast<uint8_t>(data.size()));
  } else if (data.size() <= 0xFF) {
    script.push_back(OP_PUSHDATA1);
    script.push_back(static_cast<uint8_t>(data.size()));
  } else {
    script.push_back(OP_PUSHDATA2);
    script.push_back(static_cast<uint8_t>(data.size()));
    script.push_back(static_cast<uint8_t>(data.size() >> 8));
  }
  appendBytes(script, data);
}

std::vector<uint8_t> scriptNumber(uint32_t value) {
  std::vector<uint8_t> out;
  while (value > 0) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    value >>= 8;
  }
  if (!out.empty() && (out.back() & 0x80)) out.push_back(0x00);
  return out;
}

bool readScriptNumber(const std::vector<uint8_t>& bytes, uint32_t& value) {
  if (bytes.empty() || bytes.size() > 5) return false;
  if (bytes.back() & 0x80) return false;  // negative
  uint64_t v = 0;
  for (size_t i = 0; i < bytes.size(); ++i) v |= static_cast<uint64_t>(bytes[i]) << (8 * i);
  if (v > 0xFFFFFFFFULL) return false;
  value = static_cast<uint32_t>(v);
  return scriptNumber(value) == bytes;  // minimal encoding only
}

std::vector<uint8_t> hash160(const std::vector<uint8_t>& data) {
  return ripemd160(sha256Digest(data));
}

std::vector<uint8_t> base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> data;
  data.push_back(version);
  appendBytes(data, payload);
  const std::vector<uint8_t> checksum = sha256Digest(sha256Digest(data));
  data.insert(data.end(), checksum.begin(), checksum.begin() + 4);
  return data;
}

bool base58CheckDecode(const std::string& encoded, uint8_t& version, std::vector<uint8_t>& payload) {
  std::vector<uint8_t> data;
  try {
    data = Base58Std::decode(encoded);
  } catch (const std::exception&) {
    return false;
  }
  if (data.size() < 5) return false;
  const std::vector<uint8_t> body(data.begin(), data.end() - 4);
  const std::vector<uint8_t> checksum = sha256Digest(sha256Digest(body));
  if (!std::equal(checksum.begin(), checksum.begin() + 4, data.end() - 4)) return false;
  version = body[0];
  payload.assign(body.begin() + 1, body.end());
  return true;
}

std::array<uint8_t, 32> littleEndianTxid(const std::string& txid) {
  std::vector<uint8_t> bytes;
  decodeHex(txid, bytes);
  std::array<uint8_t, 32> out{};
  std::reverse_copy(bytes.begin(), bytes.end(), out.begin());
  return out;
}

std::vector<uint8_t> derSignature(const CryptoNote::SwapDaemon::Crypto::RecoverableSignature& sig, uint32_t sighashType) {
  auto integer = [](const std::array<uint8_t, 32>& be) {
    size_t start = 0;
    while (start < 31 && be[start] == 0) ++start;
    std::vector<uint8_t> out(be.begin() + start, be.end());
    if (out[0] & 0x80) out.insert(out.begin(), 0x00);
    return out;
  };
  const std::vector<uint8_t> r = integer(sig.r);
  const std::vector<uint8_t> s = integer(sig.s);
  std::vector<uint8_t> der = {0x30, static_cast<uint8_t>(4 + r.size() + s.size()), 0x02,
                              static_cast<uint8_t>(r.size())};
  appendBytes(der, r);
  der.push_back(0x02);
  der.push_back(static_cast<uint8_t>(s.size()));
  appendBytes(der, s);
  der.push_back(static_cast<uint8_t>(sighashType));
  return der;
}

struct SpendShape {
  uint32_t lockTime;
  uint32_t sequence;
};

bool validSpend(const UtxoHtlcSpend& spend, std::string& error) {
  UtxoHtlcTerms terms;
  if (!isTxidHex(spend.lock.txid)) error = "malformed lock txid";
  else if (!parseHtlcScript(spend.htlcScript, terms)) error = "lock script is not the HTLC template";
  else if (spend.destinationScript.empty()) error = "no destination script";
  else if (spend.fee >= spend.lockValue) error = "lock value does not cover the fee";
  else return true;
  return false;
}

std::vector<uint8_t> signSpend(UtxoHtlcEncoding encoding, const UtxoHtlcSpend& spend,
                               const SpendShape& shape) {
  const uint32_t sighashType =
      encoding == UtxoHtlcEncoding::P2WSH ? SIGHASH_ALL : SIGHASH_ALL_FORKID;
  CryptoNote::SwapDaemon::Crypto::Bip143Sighash bip143;
  const std::array<uint8_t, 32> digest = bip143.computeForP2sh(
      TX_VERSION, shape.lockTime, shape.sequence, littleEndianTxid(spend.lock.txid),
      spend.lock.vout, spend.htlcScript, spend.lockValue, spend.destinationScript,
      spend.lockValue - spend.fee, sighashType);
  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  return derSignature(signer.signRecoverable(digest, spend.signingKey), sighashType);
}

std::vector<uint8_t> serializeSpend(UtxoHtlcEncoding encoding, const UtxoHtlcSpend& spend,
                                    const SpendShape& shape,
                                    const std::vector<std::vector<uint8_t>>& unlockItems) {
  const bool witness = encoding == UtxoHtlcEncoding::P2WSH;
  std::vector<uint8_t> tx;
  appendLE32(tx, TX_VERSION);
  if (witness) {
    tx.push_back(0x00);
    tx.push_back(0x01);
  }
  appendVarInt(tx, 1);
  const std::array<uint8_t, 32> prevTxid = littleEndianTxid(spend.lock.txid);
  tx.insert(tx.end(), prevTxid.begin(), prevTxid.end());
  appendLE32(tx, spend.lock.vout);
  if (witness) {
    appendVarInt(tx, 0);
  } else {
    std::vector<uint8_t> scriptSig;
    for (const auto& item : unlockItems) {
      if (item.size() == 1 && (item[0] == OP_1 || item[0] == OP_0)) scriptSig.push_back(item[0]);
      else appendPush(scriptSig, item);
    }
    appendVarInt(tx, scriptSig.size());
    appendBytes(tx, scriptSig);
  }
  appendLE32(tx, shape.sequence);
  appendVarInt(tx, 1);
  appendLE64(tx, spend.lockValue - spend.fee);
  appendVarInt(tx, spend.destinationScript.size());
  appendBytes(tx, spend.destinationScript);
  if (witness) {
    appendVarInt(tx, unlockItems.size());
    for (const auto& item : unlockItems) {
      appendVarInt(tx, item.size());
      appendBytes(tx, item);
    }
  }
  appendLE32(tx, shape.lockTime);
  return tx;
}

// Witness v0 MINIMALIF: the OP_IF argument must be exactly 0x01 or empty.
// A scriptSig selects the branch with the OP_1 / OP_0 opcodes instead.
std::vector<uint8_t> branchSelector(UtxoHtlcEncoding encoding, bool claimBranch) {
  if (encoding == UtxoHtlcEncoding::P2WSH) return claimBranch ? std::vector<uint8_t>{0x01}
                                                               : std::vector<uint8_t>{};
  return {claimBranch ? OP_1 : OP_0};
}

struct ParsedInput {
  std::array<uint8_t, 32> prevTxid{};
  uint32_t prevVout = 0;
  std::vector<uint8_t> scriptSig;
  std::vector<std::vector<uint8_t>> witness;
};

struct ParsedOutput {
  uint64_t value = 0;
  std::vector<uint8_t> scriptPubKey;
};

bool readVarInt(const uint8_t*& p, const uint8_t* end, uint64_t& value) {
  if (p >= end) return false;
  const uint8_t prefix = *p++;
  if (prefix < 0xFD) {
    value = prefix;
    return true;
  }
  const size_t width = prefix == 0xFD ? 2 : (prefix == 0xFE ? 4 : 8);
  if (static_cast<size_t>(end - p) < width) return false;
  value = 0;
  for (size_t i = 0; i < width; ++i) value |= static_cast<uint64_t>(p[i]) << (8 * i);
  p += width;
  return true;
}

bool readSlice(const uint8_t*& p, const uint8_t* end, std::vector<uint8_t>& out) {
  uint64_t size = 0;
  if (!readVarInt(p, end, size) || static_cast<uint64_t>(end - p) < size) return false;
  out.assign(p, p + size);
  p += size;
  return true;
}

bool parseTransaction(const std::vector<uint8_t>& rawTx, std::vector<ParsedInput>& inputs,
                      std::vector<ParsedOutput>& outputs) {
  const uint8_t* p = rawTx.data();
  const uint8_t* end = p + rawTx.size();
  if (end - p < 4) return false;
  p += 4;
  bool hasWitness = false;
  if (end - p >= 2 && p[0] == 0x00) {
    if (p[1] != 0x01) return false;  // only plain segwit serialization is spendable here
    hasWitness = true;
    p += 2;
  }
  uint64_t inputCount = 0;
  if (!readVarInt(p, end, inputCount) || inputCount == 0 || inputCount > 10000) return false;
  std::vector<ParsedInput> parsed(static_cast<size_t>(inputCount));
  for (auto& input : parsed) {
    if (end - p < 36) return false;
    std::copy(p, p + 32, input.prevTxid.begin());
    p += 32;
    input.prevVout = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                     (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    p += 4;
    if (!readSlice(p, end, input.scriptSig) || end - p < 4) return false;
    p += 4;
  }
  uint64_t outputCount = 0;
  if (!readVarInt(p, end, outputCount) || outputCount > static_cast<uint64_t>(end - p) / 9) return false;
  std::vector<ParsedOutput> parsedOutputs(static_cast<size_t>(outputCount));
  for (auto& output : parsedOutputs) {
    if (end - p < 8) return false;
    for (size_t i = 0; i < 8; ++i) output.value |= static_cast<uint64_t>(p[i]) << (8 * i);
    p += 8;
    if (!readSlice(p, end, output.scriptPubKey)) return false;
  }
  if (hasWitness) {
    for (auto& input : parsed) {
      uint64_t items = 0;
      if (!readVarInt(p, end, items) || items > 1000) return false;
      input.witness.resize(static_cast<size_t>(items));
      for (auto& item : input.witness)
        if (!readSlice(p, end, item)) return false;
    }
  }
  if (end - p != 4) return false;
  inputs = std::move(parsed);
  outputs = std::move(parsedOutputs);
  return true;
}

bool parsePushes(const std::vector<uint8_t>& script, std::vector<std::vector<uint8_t>>& pushes) {
  size_t i = 0;
  while (i < script.size()) {
    const uint8_t op = script[i++];
    size_t size = 0;
    if (op == OP_0) {
      pushes.emplace_back();
      continue;
    }
    if (op == OP_1) {
      pushes.push_back({0x01});
      continue;
    }
    if (op < OP_PUSHDATA1) {
      size = op;
    } else if (op == OP_PUSHDATA1 && i < script.size()) {
      size = script[i++];
    } else if (op == OP_PUSHDATA2 && i + 1 < script.size()) {
      size = static_cast<size_t>(script[i]) | (static_cast<size_t>(script[i + 1]) << 8);
      i += 2;
    } else {
      return false;
    }
    if (script.size() - i < size) return false;
    pushes.emplace_back(script.begin() + i, script.begin() + i + size);
    i += size;
  }
  return true;
}

}  // namespace

std::vector<uint8_t> sha256Digest(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  SHA256(data.data(), data.size(), digest.data());
  return digest;
}

std::vector<uint8_t> buildHtlcScript(const UtxoHtlcTerms& terms) {
  if (terms.hashLock.size() != 32 || !isCompressedPublicKey(terms.recipientPubKey) ||
      !isCompressedPublicKey(terms.senderPubKey) ||
      terms.timeoutBlock < MIN_NON_SMALL_INT_TIMEOUT)
    return {};
  std::vector<uint8_t> script = {OP_IF, OP_SHA256};
  appendPush(script, terms.hashLock);
  script.push_back(OP_EQUALVERIFY);
  appendPush(script, terms.recipientPubKey);
  script.push_back(OP_CHECKSIG);
  script.push_back(OP_ELSE);
  appendPush(script, scriptNumber(terms.timeoutBlock));
  script.push_back(OP_CHECKLOCKTIMEVERIFY);
  script.push_back(OP_DROP);
  appendPush(script, terms.senderPubKey);
  script.push_back(OP_CHECKSIG);
  script.push_back(OP_ENDIF);
  return script;
}

bool parseHtlcScript(const std::vector<uint8_t>& script, UtxoHtlcTerms& terms) {
  // 2 + 33 + 1 + 34 + 2 = 72 bytes precede the timeout push; 1 + 1 + 34 + 2 follow it.
  constexpr size_t PREFIX = 72;
  constexpr size_t SUFFIX = 38;
  if (script.size() <= PREFIX + SUFFIX + 1) return false;
  UtxoHtlcTerms parsed;
  parsed.hashLock.assign(script.begin() + 3, script.begin() + 35);
  parsed.recipientPubKey.assign(script.begin() + 37, script.begin() + 70);
  const size_t timeoutPushSize = script[PREFIX];
  if (script.size() != PREFIX + 1 + timeoutPushSize + SUFFIX) return false;
  const std::vector<uint8_t> timeoutBytes(script.begin() + PREFIX + 1,
                                          script.begin() + PREFIX + 1 + timeoutPushSize);
  if (!readScriptNumber(timeoutBytes, parsed.timeoutBlock)) return false;
  parsed.senderPubKey.assign(script.end() - 35, script.end() - 2);
  if (buildHtlcScript(parsed) != script) return false;
  terms = parsed;
  return true;
}

std::vector<uint8_t> htlcScriptPubKey(UtxoHtlcEncoding encoding, const std::vector<uint8_t>& script) {
  if (encoding == UtxoHtlcEncoding::P2WSH) {
    std::vector<uint8_t> spk = {OP_0, 0x20};
    appendBytes(spk, sha256Digest(script));
    return spk;
  }
  std::vector<uint8_t> spk = {OP_HASH160, 0x14};
  appendBytes(spk, hash160(script));
  spk.push_back(OP_EQUAL);
  return spk;
}

std::string htlcFundingAddress(UtxoHtlcEncoding encoding, const UtxoNetworkParams& network,
                               const std::vector<uint8_t>& script) {
  if (encoding == UtxoHtlcEncoding::P2WSH)
    return BtcHtlcScript::witnessScriptToAddress(script, network.bech32Hrp);
  return Base58Std::encode(base58CheckEncode(network.p2shVersion, hash160(script)));
}

bool decodeWif(const UtxoNetworkParams& network, const std::string& wif,
               std::array<uint8_t, 32>& key) {
  uint8_t version = 0;
  std::vector<uint8_t> payload;
  if (!base58CheckDecode(wif, version, payload) || version != network.wifVersion) return false;
  // Only compressed-key WIFs: every script here commits to a compressed pubkey.
  if (payload.size() != 33 || payload.back() != 0x01) return false;
  std::copy(payload.begin(), payload.begin() + 32, key.begin());
  std::vector<uint8_t> pub;
  return compressedPublicKey(key, pub);
}

bool compressedPublicKey(const std::array<uint8_t, 32>& key, std::vector<uint8_t>& pubKey) {
  try {
    CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
    pubKey = signer.derivePublicKeyCompressed(key);
  } catch (const std::exception&) {
    return false;
  }
  return isCompressedPublicKey(pubKey);
}

bool isCompressedPublicKey(const std::vector<uint8_t>& pubKey) {
  return pubKey.size() == 33 && (pubKey[0] == 0x02 || pubKey[0] == 0x03);
}

std::string p2pkhAddress(const UtxoNetworkParams& network, const std::vector<uint8_t>& pubKey) {
  return Base58Std::encode(base58CheckEncode(network.p2pkhVersion, hash160(pubKey)));
}

std::vector<uint8_t> p2pkhScriptForPubKey(const std::vector<uint8_t>& pubKey) {
  std::vector<uint8_t> spk = {OP_DUP, OP_HASH160, 0x14};
  appendBytes(spk, hash160(pubKey));
  spk.push_back(OP_EQUALVERIFY);
  spk.push_back(OP_CHECKSIG);
  return spk;
}

bool p2pkhScriptForAddress(const UtxoNetworkParams& network, const std::string& address,
                           std::vector<uint8_t>& script) {
  uint8_t version = 0;
  std::vector<uint8_t> hash;
  if (!base58CheckDecode(address, version, hash) || version != network.p2pkhVersion ||
      hash.size() != 20)
    return false;
  script = {OP_DUP, OP_HASH160, 0x14};
  appendBytes(script, hash);
  script.push_back(OP_EQUALVERIFY);
  script.push_back(OP_CHECKSIG);
  return true;
}

bool buildHtlcClaim(UtxoHtlcEncoding encoding, const UtxoHtlcSpend& spend,
                    const std::vector<uint8_t>& preimage, std::vector<uint8_t>& rawTx,
                    std::string& error) {
  if (!validSpend(spend, error)) return false;
  UtxoHtlcTerms terms;
  parseHtlcScript(spend.htlcScript, terms);
  if (preimage.size() != 32 || sha256Digest(preimage) != terms.hashLock) {
    error = "preimage does not open the hash lock";
    return false;
  }
  std::vector<uint8_t> signerPub;
  if (!compressedPublicKey(spend.signingKey, signerPub) || signerPub != terms.recipientPubKey) {
    error = "signing key is not the lock's recipient";
    return false;
  }
  const SpendShape shape{0, CLAIM_SEQUENCE};
  const std::vector<uint8_t> signature = signSpend(encoding, spend, shape);
  rawTx = serializeSpend(encoding, spend, shape,
                         {signature, preimage, branchSelector(encoding, true), spend.htlcScript});
  return true;
}

bool buildHtlcRefund(UtxoHtlcEncoding encoding, const UtxoHtlcSpend& spend,
                     std::vector<uint8_t>& rawTx, std::string& error) {
  if (!validSpend(spend, error)) return false;
  UtxoHtlcTerms terms;
  parseHtlcScript(spend.htlcScript, terms);
  std::vector<uint8_t> signerPub;
  if (!compressedPublicKey(spend.signingKey, signerPub) || signerPub != terms.senderPubKey) {
    error = "signing key is not the lock's sender";
    return false;
  }
  const SpendShape shape{terms.timeoutBlock, REFUND_SEQUENCE};
  const std::vector<uint8_t> signature = signSpend(encoding, spend, shape);
  rawTx = serializeSpend(encoding, spend, shape,
                         {signature, branchSelector(encoding, false), spend.htlcScript});
  return true;
}

uint64_t htlcSpendVirtualSize(UtxoHtlcEncoding encoding, bool claimPath) {
  // Measured from the serializations above with a 72-byte signature, a
  // 115-byte script and a P2PKH output, rounded up.
  if (encoding == UtxoHtlcEncoding::P2WSH) return claimPath ? 150 : 140;
  return claimPath ? 320 : 285;
}

bool readTransactionOutput(const std::vector<uint8_t>& rawTx, uint32_t vout, uint64_t& value,
                           std::vector<uint8_t>& scriptPubKey) {
  std::vector<ParsedInput> inputs;
  std::vector<ParsedOutput> outputs;
  if (!parseTransaction(rawTx, inputs, outputs) || vout >= outputs.size()) return false;
  value = outputs[vout].value;
  scriptPubKey = outputs[vout].scriptPubKey;
  return true;
}

bool buildP2pkhSelfSpend(UtxoHtlcEncoding encoding, const P2pkhSelfSpend& spend, std::vector<uint8_t>& rawTx) {
  if (!isTxidHex(spend.input.txid) || !isCompressedPublicKey(spend.publicKey) || spend.fee >= spend.inputValue)
    return false;
  const std::vector<uint8_t> script = p2pkhScriptForPubKey(spend.publicKey);
  const uint64_t outputValue = spend.inputValue - spend.fee;
  const std::array<uint8_t, 32> prevTxid = littleEndianTxid(spend.input.txid);
  const bool forkId = encoding == UtxoHtlcEncoding::P2SH_FORKID;
  const uint32_t sighashType = forkId ? SIGHASH_ALL_FORKID : SIGHASH_ALL;
  const std::array<uint8_t, 32> digest =
      forkId ? CryptoNote::SwapDaemon::Crypto::Bip143Sighash().computeForP2sh(
                   TX_VERSION, 0, CLAIM_SEQUENCE, prevTxid, spend.input.vout, script, spend.inputValue, script,
                   outputValue, sighashType)
             : CryptoNote::SwapDaemon::Crypto::LegacySighash().computeForP2sh(
                   TX_VERSION, 0, CLAIM_SEQUENCE, prevTxid, spend.input.vout, script, script, outputValue,
                   sighashType);
  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  std::vector<uint8_t> scriptSig;
  appendPush(scriptSig, derSignature(signer.signRecoverable(digest, spend.signingKey), sighashType));
  appendPush(scriptSig, spend.publicKey);

  rawTx.clear();
  appendLE32(rawTx, TX_VERSION);
  appendVarInt(rawTx, 1);
  rawTx.insert(rawTx.end(), prevTxid.begin(), prevTxid.end());
  appendLE32(rawTx, spend.input.vout);
  appendVarInt(rawTx, scriptSig.size());
  appendBytes(rawTx, scriptSig);
  appendLE32(rawTx, CLAIM_SEQUENCE);
  appendVarInt(rawTx, 1);
  appendLE64(rawTx, outputValue);
  appendVarInt(rawTx, script.size());
  appendBytes(rawTx, script);
  appendLE32(rawTx, 0);
  return true;
}

bool spendsOutpoint(const std::vector<uint8_t>& rawTx, const UtxoOutpoint& outpoint) {
  std::vector<std::vector<uint8_t>> witness;
  return spendWitness(rawTx, outpoint, witness);
}

bool spendWitness(const std::vector<uint8_t>& rawTx, const UtxoOutpoint& lock,
                  std::vector<std::vector<uint8_t>>& witness) {
  std::vector<ParsedInput> inputs;
  std::vector<ParsedOutput> outputs;
  if (!isTxidHex(lock.txid) || !parseTransaction(rawTx, inputs, outputs)) return false;
  const std::array<uint8_t, 32> lockTxid = littleEndianTxid(lock.txid);
  for (const auto& input : inputs) {
    if (input.prevTxid != lockTxid || input.prevVout != lock.vout) continue;
    witness = input.witness;
    return true;
  }
  return false;
}

bool extractHtlcPreimage(UtxoHtlcEncoding encoding, const std::vector<uint8_t>& rawTx,
                         const UtxoOutpoint& lock, const std::vector<uint8_t>& htlcScript,
                         std::vector<uint8_t>& preimage) {
  UtxoHtlcTerms terms;
  std::vector<ParsedInput> inputs;
  std::vector<ParsedOutput> outputs;
  if (!isTxidHex(lock.txid) || !parseHtlcScript(htlcScript, terms) || !parseTransaction(rawTx, inputs, outputs))
    return false;
  const std::array<uint8_t, 32> lockTxid = littleEndianTxid(lock.txid);
  for (const auto& input : inputs) {
    if (input.prevTxid != lockTxid || input.prevVout != lock.vout) continue;
    std::vector<std::vector<uint8_t>> items;
    if (encoding == UtxoHtlcEncoding::P2WSH) items = input.witness;
    else if (!parsePushes(input.scriptSig, items)) return false;
    if (items.size() != 4 || items[3] != htlcScript || items[2] != std::vector<uint8_t>{0x01})
      return false;
    if (items[1].size() != 32 || sha256Digest(items[1]) != terms.hashLock) return false;
    preimage = items[1];
    return true;
  }
  return false;
}

}  // namespace XfgSwap
