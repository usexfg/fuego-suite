// Copyright (c) 2017-2026 Fuego Developers
//
// NeutrinoSpvClient: ISpvClient implementation using BIP-157/158
// compact block filters for privacy-preserving SPV.

#include "NeutrinoSpvClient.h"
#include "SwapDaemon/BitcoinCash/HtlcScript.h"
#include "Common/JsonValue.h"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace XfgSwap {

// =============================================================================
// SipHash-2-4 implementation (PRF for BIP-158 GCS filters)
// Reference: https://github.com/veorq/SipHash
// =============================================================================

static uint64_t u8to64_le(const uint8_t* p) {
  return static_cast<uint64_t>(p[0]) |
         (static_cast<uint64_t>(p[1]) << 8) |
         (static_cast<uint64_t>(p[2]) << 16) |
         (static_cast<uint64_t>(p[3]) << 24) |
         (static_cast<uint64_t>(p[4]) << 32) |
         (static_cast<uint64_t>(p[5]) << 40) |
         (static_cast<uint64_t>(p[6]) << 48) |
         (static_cast<uint64_t>(p[7]) << 56);
}

static uint64_t rotl64(uint64_t x, int b) {
  return (x << b) | (x >> (64 - b));
}

#define SIPROUND \
  do { \
    v0 += v1; v1 = rotl64(v1, 13); v1 ^= v0; v0 = rotl64(v0, 32); \
    v2 += v3; v3 = rotl64(v3, 16); v3 ^= v2; \
    v0 += v3; v3 = rotl64(v3, 21); v3 ^= v0; \
    v2 += v1; v1 = rotl64(v1, 17); v1 ^= v2; v2 = rotl64(v2, 32); \
  } while(0)

uint64_t SipHash(const uint8_t* in, size_t inlen, uint64_t k0, uint64_t k1) {
  uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;
  uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;
  uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;
  uint64_t v3 = k1 ^ 0x7465646279746573ULL;

  const uint8_t* end = in + inlen - (inlen & 7);
  for (; in != end; in += 8) {
    uint64_t m = u8to64_le(in);
    v3 ^= m;
    SIPROUND; SIPROUND;
    v0 ^= m;
  }

  uint64_t b = static_cast<uint64_t>(inlen) << 56;
  switch (inlen & 7) {
    case 7: b |= static_cast<uint64_t>(in[6]) << 48; // fallthrough
    case 6: b |= static_cast<uint64_t>(in[5]) << 40; // fallthrough
    case 5: b |= static_cast<uint64_t>(in[4]) << 32; // fallthrough
    case 4: b |= static_cast<uint64_t>(in[3]) << 24; // fallthrough
    case 3: b |= static_cast<uint64_t>(in[2]) << 16; // fallthrough
    case 2: b |= static_cast<uint64_t>(in[1]) << 8;  // fallthrough
    case 1: b |= static_cast<uint64_t>(in[0]); break;
    case 0: break;
  }

  v3 ^= b;
  SIPROUND; SIPROUND;
  v0 ^= b;

  v2 ^= 0xff;
  SIPROUND; SIPROUND; SIPROUND; SIPROUND;

  return v0 ^ v1 ^ v2 ^ v3;
}

#undef SIPROUND

// =============================================================================
// BIP-158 key derivation
// =============================================================================

GcsFilterParams deriveFilterKey(const std::vector<uint8_t>& blockHash,
                                const GcsFilterParams& base) {
  GcsFilterParams p = base;
  if (blockHash.size() >= 32) {
    auto digest = BchHtlcScript::sha256(blockHash);
    if (digest.size() == 32) {
      std::memcpy(&p.k0, digest.data(), 8);
      std::memcpy(&p.k1, digest.data() + 8, 8);
    }
  }
  // If blockHash empty or sha256 failed, k0/k1 remain 0 (caller uses defaults)
  return p;
}

// =============================================================================
// GCS filter construction and matching (BIP-158)
//
// Filter format:
//   [varint N]           — number of items
//   Golomb-Rice encoded sorted, delta-hashed items
//
// Golomb-Rice: each delta d is split as:
//   quotient  = d >> P   (unary coded: quotient zeros + 1 one)
//   remainder = d & (2^P - 1)  (fixed P-bit width)
// =============================================================================

static void writeBitsLE(std::vector<uint8_t>& out, uint64_t value, int numBits,
                         uint64_t& bitBuf, int& bitCount) {
  for (int i = numBits - 1; i >= 0; --i) {
    bitBuf = (bitBuf << 1) | ((value >> i) & 1);
    ++bitCount;
    if (bitCount == 8) {
      out.push_back(static_cast<uint8_t>(bitBuf));
      bitBuf = 0;
      bitCount = 0;
    }
  }
}

static void flushBits(std::vector<uint8_t>& out, uint64_t& bitBuf, int& bitCount) {
  if (bitCount > 0) {
    bitBuf <<= (8 - bitCount);
    out.push_back(static_cast<uint8_t>(bitBuf));
    bitBuf = 0;
    bitCount = 0;
  }
}

// Write a Bitcoin CompactSize to the bit stream
static void writeCompactSize(std::vector<uint8_t>& out, uint64_t n,
                              uint64_t& bitBuf, int& bitCount) {
  if (n < 0xFD) {
    writeBitsLE(out, n, 8, bitBuf, bitCount);
  } else if (n <= 0xFFFF) {
    writeBitsLE(out, 0xFD, 8, bitBuf, bitCount);
    writeBitsLE(out, n & 0xFF, 8, bitBuf, bitCount);
    writeBitsLE(out, (n >> 8) & 0xFF, 8, bitBuf, bitCount);
  } else if (n <= 0xFFFFFFFF) {
    writeBitsLE(out, 0xFE, 8, bitBuf, bitCount);
    writeBitsLE(out, n & 0xFF, 8, bitBuf, bitCount);
    writeBitsLE(out, (n >> 8) & 0xFF, 8, bitBuf, bitCount);
    writeBitsLE(out, (n >> 16) & 0xFF, 8, bitBuf, bitCount);
    writeBitsLE(out, (n >> 24) & 0xFF, 8, bitBuf, bitCount);
  } else {
    writeBitsLE(out, 0xFF, 8, bitBuf, bitCount);
    for (int i = 0; i < 8; ++i) {
      writeBitsLE(out, (n >> (8 * i)) & 0xFF, 8, bitBuf, bitCount);
    }
  }
}

std::vector<uint8_t> NeutrinoSpvClient::buildFilter(
    const std::vector<std::vector<uint8_t>>& items,
    const GcsFilterParams& params) {
  if (items.empty()) {
    return {};
  }

  // Use explicit k0/k1 if provided (BIP-158 per-block key), else fall back to legacy M/P
  uint64_t k0 = params.k0 ? params.k0 : params.M;
  uint64_t k1 = params.k1 ? params.k1 : params.P;

  // Hash all items with SipHash mod M
  // M is the hash range; with P-bit Golomb-Rice coding, the average
  // delta is M/N and average quotient is ~M/(N * 2^P), which is small
  // when N is reasonable relative to M.
  std::vector<uint64_t> hashes;
  hashes.reserve(items.size());
  for (const auto& item : items) {
    uint64_t h = SipHash(item.data(), item.size(), k0, k1) % params.M;
    hashes.push_back(h);
  }

  // Sort and deduplicate
  std::sort(hashes.begin(), hashes.end());
  hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());

  // Encode with Golomb-Rice
  std::vector<uint8_t> filter;
  uint64_t bitBuf = 0;
  int bitCount = 0;

  // Write item count as CompactSize
  writeCompactSize(filter, hashes.size(), bitBuf, bitCount);

  // Golomb-Rice encode each delta
  uint64_t prev = 0;
  for (uint64_t h : hashes) {
    uint64_t delta = h - prev;
    uint64_t quotient = delta >> params.P;
    uint64_t remainder = delta & ((static_cast<uint64_t>(1) << params.P) - 1);

    // Unary code: quotient zeros followed by a 1
    for (uint64_t q = 0; q < quotient; ++q) {
      writeBitsLE(filter, 0, 1, bitBuf, bitCount);
    }
    writeBitsLE(filter, 1, 1, bitBuf, bitCount);

    // Fixed-width P-bit remainder
    writeBitsLE(filter, remainder, params.P, bitBuf, bitCount);

    prev = h;
  }

  flushBits(filter, bitBuf, bitCount);
  return filter;
}

// Read a single bit from the filter at the given bit position.
// Returns 0 or 1, or 0 if out of bounds.
static inline int readBit(const std::vector<uint8_t>& filter, size_t bitPos) {
  if (bitPos / 8 >= filter.size()) return 0;
  return (filter[bitPos / 8] >> (7 - (bitPos % 8))) & 1;
}

bool NeutrinoSpvClient::matchFilter(
    const std::vector<uint8_t>& filter,
    const std::vector<uint8_t>& data,
    const GcsFilterParams& params) {
  if (filter.empty() || data.empty()) {
    return false;
  }

  // Use explicit k0/k1 if provided (BIP-158 per-block key), else fall back to legacy M/P
  uint64_t k0 = params.k0 ? params.k0 : params.M;
  uint64_t k1 = params.k1 ? params.k1 : params.P;

  uint64_t targetHash = SipHash(data.data(), data.size(), k0, k1) % params.M;

  // Decode CompactSize item count
  size_t pos = 0;
  uint64_t itemCount = 0;
  if (filter.empty()) return false;
  uint8_t first = filter[0];
  pos = 8;  // consumed first byte
  if (first < 0xFD) {
    itemCount = first;
  } else if (first == 0xFD) {
    if (filter.size() < 3) return false;
    itemCount = static_cast<uint64_t>(filter[1]) |
               (static_cast<uint64_t>(filter[2]) << 8);
    pos = 24;
  } else if (first == 0xFE) {
    if (filter.size() < 5) return false;
    itemCount = static_cast<uint64_t>(filter[1]) |
               (static_cast<uint64_t>(filter[2]) << 8) |
               (static_cast<uint64_t>(filter[3]) << 16) |
               (static_cast<uint64_t>(filter[4]) << 24);
    pos = 40;
  } else {
    if (filter.size() < 9) return false;
    itemCount = 0;
    for (int i = 0; i < 8; ++i) {
      itemCount |= static_cast<uint64_t>(filter[1 + i]) << (8 * i);
    }
    pos = 72;
  }

  // Golomb-Rice decode: reconstruct sorted hashes via delta accumulation
  uint64_t prev = 0;
  const size_t totalBits = filter.size() * 8;
  for (uint64_t i = 0; i < itemCount; ++i) {
    // Read unary quotient (count leading 0-bits until a 1-bit)
    uint64_t quotient = 0;
    while (pos < totalBits && readBit(filter, pos) == 0) {
      ++quotient;
      ++pos;
      if (quotient > totalBits) return false;  // bounds: prevent unbounded loop
    }
    if (pos >= totalBits) return false;
    ++pos;  // skip the terminating 1-bit

    // Read P-bit remainder
    if (pos + params.P > totalBits) return false;
    uint64_t remainder = 0;
    for (uint32_t j = 0; j < params.P; ++j) {
      remainder = (remainder << 1) | readBit(filter, pos);
      ++pos;
    }

    // Overflow check: quotient << P must not overflow uint64_t
    if (quotient > (UINT64_MAX >> params.P)) return false;
    uint64_t delta = (quotient << params.P) | remainder;
    uint64_t h = prev + delta;

    if (h == targetHash) return true;
    if (h > targetHash) return false;  // sorted — no match possible

    prev = h;
  }

  return false;
}

// =============================================================================
// Construction / Destruction
// =============================================================================

NeutrinoSpvClient::NeutrinoSpvClient(
    SpvHeaderStore& store,
    const std::vector<SpvHeaderStore::Checkpoint>& checkpoints,
    const GcsFilterParams& params)
    : m_store(store)
    , m_params(params) {
  for (const auto& cp : checkpoints) {
    m_store.anchor(cp.height, cp.hash);
  }
}

// =============================================================================
// ISpvClient interface
// =============================================================================

bool NeutrinoSpvClient::syncHeaders() {
  if (m_connections.empty()) {
    return false;
  }

  // Get tip height from the first connection
  std::string tipResponse;
  if (!m_connections[0]->sendRequest("getheaders", "[]", tipResponse)) {
    return false;
  }

  Common::JsonValue tipJson;
  try {
    tipJson = Common::JsonValue::fromString(tipResponse);
  } catch (...) {
    return false;
  }

  if (!tipJson.contains("height") || !tipJson.contains("hex")) {
    return false;
  }

  uint64_t tipHeight = static_cast<uint64_t>(tipJson("height").getInteger());
  std::string tipHex = tipJson("hex").getString();

  // Determine start height
  uint64_t startHeight = 0;
  uint64_t storeTip;
  std::string storeTipHash;
  if (m_store.bestTip(storeTip, storeTipHash)) {
    if (storeTip >= tipHeight) {
      return true;
    }
    startHeight = storeTip + 1;
  }

  // Fetch headers in batches
  const uint32_t BATCH = 2000;
  uint64_t current = startHeight;

  while (current <= tipHeight) {
    uint32_t count = static_cast<uint32_t>(
        std::min<uint64_t>(BATCH, tipHeight - current + 1));

    std::string params = "[" + std::to_string(current) + ","
        + std::to_string(count) + "]";
    std::string result;
    if (!m_connections[0]->sendRequest("getheaders", params, result)) {
      return false;
    }

    Common::JsonValue json;
    try {
      json = Common::JsonValue::fromString(result);
    } catch (...) {
      return false;
    }

    if (!json.contains("headers")) {
      return false;
    }

    std::string headersHex = json("headers").getString();
    if (headersHex.empty()) {
      return false;
    }

    std::vector<uint8_t> rawBytes = BchHtlcScript::hexToBytes(headersHex);
    if (rawBytes.size() % 80 != 0 || rawBytes.empty()) {
      return false;
    }

    size_t numHeaders = rawBytes.size() / 80;
    for (size_t i = 0; i < numHeaders; ++i) {
      std::vector<uint8_t> headerBytes(
          rawBytes.begin() + i * 80,
          rawBytes.begin() + (i + 1) * 80);
      SpvHeader header = SpvHeader::parse(headerBytes);

      if (!m_store.addHeader(header)) {
        return false;
      }
    }

    current += numHeaders;
    if (numHeaders < count) break;
  }

  return true;
}

bool NeutrinoSpvClient::getTipHeight(uint64_t& height) {
  uint64_t h;
  std::string hash;
  if (!m_store.bestTip(h, hash)) {
    return false;
  }
  height = h;
  return true;
}

bool NeutrinoSpvClient::verifyTxInclusion(
    const std::string& txid, SpvTxInclusion& out) {
  out = SpvTxInclusion();

  if (m_connections.empty()) {
    return false;
  }

  std::string params = "[\"" + txid + "\"]";
  std::string result;
  if (!m_connections[0]->sendRequest("getmerkle", params, result)) {
    return false;
  }

  Common::JsonValue json;
  try {
    json = Common::JsonValue::fromString(result);
  } catch (...) {
    return false;
  }

  if (!json.contains("block_height") || !json.contains("merkle") || !json.contains("pos")) {
    return false;
  }

  uint64_t blockHeight = static_cast<uint64_t>(json("block_height").getInteger());
  uint32_t pos = static_cast<uint32_t>(json("pos").getInteger());

  std::vector<std::string> branch;
  const Common::JsonValue& merkleArr = json("merkle");
  for (size_t i = 0; i < merkleArr.size(); ++i) {
    branch.push_back(merkleArr[i].getString());
  }

  // Verify against header store
  std::vector<uint8_t> rootLE;
  if (!m_store.merkleRootAt(blockHeight, rootLE)) {
    return false;
  }

  std::vector<uint8_t> rootBE(rootLE.rbegin(), rootLE.rend());
  std::string storedRootDisplay = BchHtlcScript::bytesToHex(rootBE);

  // Compute Merkle root from txid + branch + pos
  // Fold: for each branch, if (pos & 1) dsha256(branch || cur) else dsha256(cur || branch)
  std::vector<uint8_t> cur = BchHtlcScript::hexToBytes(txid);
  std::reverse(cur.begin(), cur.end());
  uint32_t posBits = pos;

  for (const auto& bh : branch) {
    std::vector<uint8_t> b = BchHtlcScript::hexToBytes(bh);
    std::reverse(b.begin(), b.end());
    std::vector<uint8_t> cat;
    if (posBits & 1u) {
      cat = b;
      cat.insert(cat.end(), cur.begin(), cur.end());
    } else {
      cat = cur;
      cat.insert(cat.end(), b.begin(), b.end());
    }
    cur = BchHtlcScript::doubleSha256(cat);
    posBits >>= 1;
  }

  std::reverse(cur.begin(), cur.end());
  std::string computedRoot = BchHtlcScript::bytesToHex(cur);

  if (computedRoot != storedRootDisplay) {
    out.included = false;
    out.merkleVerified = false;
    return true;
  }

  out.included = true;
  out.blockHeight = blockHeight;
  out.depth = m_store.depthOf(blockHeight);
  out.merkleVerified = true;
  return true;
}

// Minimal raw Bitcoin transaction parser (non-witness parts).
namespace {

struct NeutrinoParsedTxInput {
  std::vector<uint8_t> prevoutHash;  // 32 bytes
  uint32_t prevoutIndex = 0;
};

struct NeutrinoParsedTxOutput {
  std::vector<uint8_t> scriptPubKey;
};

struct NeutrinoParsedTx {
  bool valid = false;
  std::vector<NeutrinoParsedTxInput> inputs;
  std::vector<NeutrinoParsedTxOutput> outputs;
};

static bool nReadVarInt(const std::vector<uint8_t>& data, size_t& pos, uint64_t& result) {
  if (pos >= data.size()) return false;
  uint8_t first = data[pos++];
  if (first < 0xFD) { result = first; return true; }
  if (first == 0xFD) {
    if (pos + 2 > data.size()) return false;
    result = static_cast<uint64_t>(data[pos]) | (static_cast<uint64_t>(data[pos + 1]) << 8);
    pos += 2; return true;
  }
  if (first == 0xFE) {
    if (pos + 4 > data.size()) return false;
    result = static_cast<uint64_t>(data[pos])
        | (static_cast<uint64_t>(data[pos + 1]) << 8)
        | (static_cast<uint64_t>(data[pos + 2]) << 16)
        | (static_cast<uint64_t>(data[pos + 3]) << 24);
    pos += 4; return true;
  }
  if (pos + 8 > data.size()) return false;
  result = 0;
  for (int i = 0; i < 8; ++i) {
    result |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
  }
  pos += 8; return true;
}

static bool nReadU32LE(const std::vector<uint8_t>& data, size_t& pos, uint32_t& out) {
  if (pos + 4 > data.size()) return false;
  out = static_cast<uint32_t>(data[pos])
      | (static_cast<uint32_t>(data[pos + 1]) << 8)
      | (static_cast<uint32_t>(data[pos + 2]) << 16)
      | (static_cast<uint32_t>(data[pos + 3]) << 24);
  pos += 4; return true;
}

static bool nReadBytes(const std::vector<uint8_t>& data, size_t& pos,
                        size_t count, std::vector<uint8_t>& out) {
  if (pos + count > data.size()) return false;
  out.assign(data.begin() + pos, data.begin() + pos + count);
  pos += count;
  return true;
}

static NeutrinoParsedTx nParseRawTx(const std::vector<uint8_t>& raw) {
  NeutrinoParsedTx tx;
  size_t pos = 0;

  uint32_t version;
  if (!nReadU32LE(raw, pos, version)) return tx;

  bool hasWitness = false;
  if (pos < raw.size() && raw[pos] == 0x00) {
    if (pos + 1 < raw.size() && raw[pos + 1] == 0x01) {
      hasWitness = true;
      pos += 2;
    }
  }

  uint64_t inputCount;
  if (!nReadVarInt(raw, pos, inputCount)) return tx;

  for (uint64_t i = 0; i < inputCount; ++i) {
    NeutrinoParsedTxInput in;
    if (!nReadBytes(raw, pos, 32, in.prevoutHash)) return tx;
    if (!nReadU32LE(raw, pos, in.prevoutIndex)) return tx;
    uint64_t scriptSigLen;
    if (!nReadVarInt(raw, pos, scriptSigLen)) return tx;
    pos += scriptSigLen + 4;  // scriptSig + sequence
    if (pos > raw.size()) return tx;
    tx.inputs.push_back(std::move(in));
  }

  uint64_t outputCount;
  if (!nReadVarInt(raw, pos, outputCount)) return tx;

  for (uint64_t i = 0; i < outputCount; ++i) {
    NeutrinoParsedTxOutput out;
    pos += 8;  // value
    if (pos > raw.size()) return tx;
    uint64_t scriptPubKeyLen;
    if (!nReadVarInt(raw, pos, scriptPubKeyLen)) return tx;
    if (!nReadBytes(raw, pos, scriptPubKeyLen, out.scriptPubKey)) return tx;
    tx.outputs.push_back(std::move(out));
  }

  if (hasWitness) {
    for (uint64_t i = 0; i < inputCount; ++i) {
      uint64_t itemCount;
      if (!nReadVarInt(raw, pos, itemCount)) return tx;
      for (uint64_t j = 0; j < itemCount; ++j) {
        uint64_t itemLen;
        if (!nReadVarInt(raw, pos, itemLen)) return tx;
        pos += itemLen;
        if (pos > raw.size()) return tx;
      }
    }
  }

  tx.valid = true;
  return tx;
}

} // anonymous namespace

bool NeutrinoSpvClient::findSpend(
    const std::string& txid, uint32_t vout, SpvSpend& out) {
  out = SpvSpend();

  // 1. Get the funding transaction to extract the output's scriptPubKey
  std::vector<uint8_t> fundingRaw;
  if (!getRawTx(txid, fundingRaw)) {
    return false;
  }

  NeutrinoParsedTx fundingTx = nParseRawTx(fundingRaw);
  if (!fundingTx.valid || vout >= fundingTx.outputs.size()) {
    return true;  // Output doesn't exist — successfully determined no spend
  }

  const std::vector<uint8_t>& scriptPubKey = fundingTx.outputs[vout].scriptPubKey;

  // 2. Set as watch script
  setWatchScript(scriptPubKey);

  // 3. Get the funding tx's block height via inclusion proof
  SpvTxInclusion fundingInclusion;
  if (!verifyTxInclusion(txid, fundingInclusion) || !fundingInclusion.included) {
    return false;
  }

  uint64_t fundingHeight = fundingInclusion.blockHeight;
  uint64_t tipHeight;
  if (!getTipHeight(tipHeight)) {
    return false;
  }

  // Pre-compute funding txid in internal LE bytes for outpoint comparison
  std::vector<uint8_t> fundingTxidBE = BchHtlcScript::hexToBytes(txid);
  if (fundingTxidBE.size() != 32) return false;
  std::vector<uint8_t> fundingTxidLE(fundingTxidBE.rbegin(), fundingTxidBE.rend());

  // 4. Scan forward from funding block height using filters
  for (uint64_t height = fundingHeight; height <= tipHeight; ++height) {
    std::vector<uint8_t> filter;
    if (!getFilter(height, filter)) {
      continue;
    }

    if (!matchFilter(filter, scriptPubKey, m_params)) {
      continue;
    }

    // Filter matched — download full block tx list
    std::vector<std::string> txids;
    if (!downloadBlockTxs(height, txids)) {
      continue;
    }

    for (const auto& candidateTxid : txids) {
      if (candidateTxid == txid) continue;

      std::vector<uint8_t> candidateRaw;
      if (!getRawTx(candidateTxid, candidateRaw)) {
        continue;
      }

      NeutrinoParsedTx candidateTx = nParseRawTx(candidateRaw);
      if (!candidateTx.valid) continue;

      for (const auto& input : candidateTx.inputs) {
        if (input.prevoutHash.size() != 32) continue;
        if (input.prevoutHash == fundingTxidLE && input.prevoutIndex == vout) {
          out.spent = true;
          out.spendingTxid = candidateTxid;
          out.rawSpendingTx = candidateRaw;
          verifyTxInclusion(candidateTxid, out.inclusion);
          return true;
        }
      }
    }
  }

  return true;
}

bool NeutrinoSpvClient::getRawTx(
    const std::string& txid, std::vector<uint8_t>& rawTx) {
  rawTx.clear();

  if (m_connections.empty()) {
    return false;
  }

  std::string params = "[\"" + txid + "\"]";
  std::string result;
  if (!m_connections[0]->sendRequest("gettx", params, result)) {
    return false;
  }

  Common::JsonValue json;
  try {
    json = Common::JsonValue::fromString(result);
  } catch (...) {
    return false;
  }

  if (!json.isString()) {
    return false;
  }

  std::string hexStr = json.getString();
  if (hexStr.empty()) {
    return false;
  }

  rawTx = BchHtlcScript::hexToBytes(hexStr);
  return !rawTx.empty();
}

// =============================================================================
// Neutrino-specific methods
// =============================================================================

void NeutrinoSpvClient::addConnection(std::unique_ptr<NeutrinoConnection> conn) {
  m_connections.push_back(std::move(conn));
}

void NeutrinoSpvClient::setWatchScript(const std::vector<uint8_t>& scriptPubKey) {
  m_watchScript = scriptPubKey;
}

bool NeutrinoSpvClient::getFilter(uint64_t height, std::vector<uint8_t>& filter) {
  auto it = m_filterCache.find(height);
  if (it != m_filterCache.end()) {
    filter = it->second;
    return true;
  }

  if (!downloadFilter(height, filter)) {
    return false;
  }

  m_filterCache[height] = filter;
  return true;
}

// =============================================================================
// Private helpers
// =============================================================================

bool NeutrinoSpvClient::downloadFilterHeaders(
    uint64_t startHeight, uint64_t count) {
  if (m_connections.empty()) return false;

  std::string params = "[" + std::to_string(startHeight) + ","
      + std::to_string(count) + "]";
  std::string result;
  return m_connections[0]->sendRequest("getcfheaders", params, result);
}

bool NeutrinoSpvClient::downloadFilter(
    uint64_t height, std::vector<uint8_t>& filter) {
  if (m_connections.empty()) return false;

  std::string params = "[" + std::to_string(height) + ",1]";
  std::string result;
  if (!m_connections[0]->sendRequest("getcfilters", params, result)) {
    return false;
  }

  Common::JsonValue json;
  try {
    json = Common::JsonValue::fromString(result);
  } catch (...) {
    return false;
  }

  if (!json.contains("filter")) {
    return false;
  }

  std::string filterHex = json("filter").getString();
  if (filterHex.empty()) {
    return false;
  }

  filter = BchHtlcScript::hexToBytes(filterHex);
  return true;
}

bool NeutrinoSpvClient::downloadBlockTxs(
    uint64_t height, std::vector<std::string>& txids) {
  if (m_connections.empty()) return false;

  std::string params = "[" + std::to_string(height) + "]";
  std::string result;
  if (!m_connections[0]->sendRequest("getblocktxs", params, result)) {
    return false;
  }

  Common::JsonValue json;
  try {
    json = Common::JsonValue::fromString(result);
  } catch (...) {
    return false;
  }

  if (!json.contains("txids")) {
    return false;
  }

  const Common::JsonValue& txArr = json("txids");
  txids.clear();
  for (size_t i = 0; i < txArr.size(); ++i) {
    txids.push_back(txArr[i].getString());
  }
  return true;
}

} // namespace XfgSwap
