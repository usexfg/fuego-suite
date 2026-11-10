// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "BchPointLock.h"

#include <algorithm>
#include <cstring>

#include "Common/StringTools.h"
#include "SwapDaemon/Crypto/Bip143Sighash.h"

namespace XfgSwap {
namespace {

constexpr uint8_t OP_PUSHDATA1 = 0x4c;
constexpr uint8_t OP_0 = 0x00;
constexpr uint8_t OP_1 = 0x51;
constexpr uint8_t OP_IF = 0x63;
constexpr uint8_t OP_ELSE = 0x67;
constexpr uint8_t OP_ENDIF = 0x68;
constexpr uint8_t OP_DROP = 0x75;
constexpr uint8_t OP_CHECKSIG = 0xac;
constexpr uint8_t OP_CHECKSIGVERIFY = 0xad;
constexpr uint8_t OP_CHECKLOCKTIMEVERIFY = 0xb1;
constexpr uint32_t BCH_SIGHASH_ALL_FORKID = 0x41;
constexpr uint32_t CLAIM_SEQUENCE = 0xFFFFFFFF;
constexpr uint32_t REFUND_SEQUENCE = 0xFFFFFFFE;
constexpr uint32_t TX_VERSION = 2;
const char STATE_PREFIX[] = "bs1:";

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
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
  } else {
    out.push_back(0xFE);
    appendLE32(out, static_cast<uint32_t>(value));
  }
}

void appendPush(std::vector<uint8_t>& script, const std::vector<uint8_t>& data) {
  if (data.size() < OP_PUSHDATA1) {
    script.push_back(static_cast<uint8_t>(data.size()));
  } else {
    script.push_back(OP_PUSHDATA1);
    script.push_back(static_cast<uint8_t>(data.size()));
  }
  script.insert(script.end(), data.begin(), data.end());
}

void appendPush(std::vector<uint8_t>& script, const BchSchnorr::Point& key) {
  appendPush(script, std::vector<uint8_t>(key.begin(), key.end()));
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

std::array<uint8_t, 32> littleEndianTxid(const std::string& txid) {
  std::vector<uint8_t> bytes;
  Common::fromHex(txid, bytes);
  std::array<uint8_t, 32> out{};
  std::reverse_copy(bytes.begin(), bytes.end(), out.begin());
  return out;
}

struct SpendShape {
  uint32_t sequence;
  uint32_t lockTime;
};

SpendShape claimShape() { return {CLAIM_SEQUENCE, 0}; }
SpendShape refundShape(const BchPointLockTerms& terms) { return {REFUND_SEQUENCE, terms.timeoutBlock}; }

bool spendSighash(const BchPointLockTerms& terms, const PointLockSpend& spend, const SpendShape& shape,
                  BchSchnorr::Message& sighash) {
  if (spend.fee >= spend.lockValue) return false;
  const std::vector<uint8_t> script = bchPointLockScript(terms);
  CryptoNote::SwapDaemon::Crypto::Bip143Sighash bip143;
  sighash = bip143.computeForP2sh(TX_VERSION, shape.lockTime, shape.sequence, littleEndianTxid(spend.lock.txid),
                                  spend.lock.vout, script, spend.lockValue, spend.destinationScript,
                                  spend.lockValue - spend.fee, BCH_SIGHASH_ALL_FORKID);
  return true;
}

std::vector<uint8_t> signatureBytes(const BchSchnorr::Signature& signature) {
  std::vector<uint8_t> bytes(signature.begin(), signature.end());
  bytes.push_back(static_cast<uint8_t>(BCH_SIGHASH_ALL_FORKID));
  return bytes;
}

std::vector<uint8_t> spendTransaction(const PointLockSpend& spend, const SpendShape& shape,
                                      const std::vector<std::vector<uint8_t>>& scriptSigItems) {
  std::vector<uint8_t> scriptSig;
  for (const std::vector<uint8_t>& item : scriptSigItems) {
    if (item.size() == 1 && item[0] == 0x01) scriptSig.push_back(OP_1);
    else if (item.empty()) scriptSig.push_back(OP_0);
    else appendPush(scriptSig, item);
  }
  std::vector<uint8_t> rawTx;
  appendLE32(rawTx, TX_VERSION);
  appendVarInt(rawTx, 1);
  std::vector<uint8_t> txid;
  Common::fromHex(spend.lock.txid, txid);
  rawTx.insert(rawTx.end(), txid.rbegin(), txid.rend());
  appendLE32(rawTx, spend.lock.vout);
  appendVarInt(rawTx, scriptSig.size());
  rawTx.insert(rawTx.end(), scriptSig.begin(), scriptSig.end());
  appendLE32(rawTx, shape.sequence);
  appendVarInt(rawTx, 1);
  appendLE64(rawTx, spend.lockValue - spend.fee);
  appendVarInt(rawTx, spend.destinationScript.size());
  rawTx.insert(rawTx.end(), spend.destinationScript.begin(), spend.destinationScript.end());
  appendLE32(rawTx, shape.lockTime);
  return rawTx;
}

}  // namespace

std::vector<uint8_t> bchPointLockScript(const BchPointLockTerms& terms) {
  std::vector<uint8_t> script{OP_IF};
  appendPush(script, terms.aliceKey);
  script.push_back(OP_CHECKSIGVERIFY);
  appendPush(script, terms.bobKey);
  script.push_back(OP_CHECKSIG);
  script.push_back(OP_ELSE);
  appendPush(script, scriptNumber(terms.timeoutBlock));
  script.push_back(OP_CHECKLOCKTIMEVERIFY);
  script.push_back(OP_DROP);
  appendPush(script, terms.aliceKey);
  script.push_back(OP_CHECKSIG);
  script.push_back(OP_ENDIF);
  return script;
}

std::vector<uint8_t> bchPointLockScriptPubKey(const BchPointLockTerms& terms) {
  return htlcScriptPubKey(UtxoHtlcEncoding::P2SH_FORKID, bchPointLockScript(terms));
}

std::string bchPointLockAddress(const UtxoNetworkParams& network, const BchPointLockTerms& terms) {
  return htlcFundingAddress(UtxoHtlcEncoding::P2SH_FORKID, network, bchPointLockScript(terms));
}

bool bchPointLockClaimSighash(const BchPointLockTerms& terms, const PointLockSpend& spend,
                              BchSchnorr::Message& sighash) {
  return spendSighash(terms, spend, claimShape(), sighash);
}

bool bchPointLockRefundSighash(const BchPointLockTerms& terms, const PointLockSpend& spend,
                               BchSchnorr::Message& sighash) {
  return spendSighash(terms, spend, refundShape(terms), sighash);
}

bool bchPointLockClaimTransaction(const BchPointLockTerms& terms, const PointLockSpend& spend,
                                  const BchSchnorr::Signature& bobSignature,
                                  const BchSchnorr::Signature& aliceSignature, std::vector<uint8_t>& rawTx) {
  if (spend.fee >= spend.lockValue) return false;
  rawTx = spendTransaction(spend, claimShape(),
                           {signatureBytes(bobSignature), signatureBytes(aliceSignature), {0x01},
                            bchPointLockScript(terms)});
  return true;
}

bool bchPointLockRefundTransaction(const BchPointLockTerms& terms, const PointLockSpend& spend,
                                   const BchSchnorr::Signature& aliceSignature, std::vector<uint8_t>& rawTx) {
  if (spend.fee >= spend.lockValue) return false;
  rawTx = spendTransaction(spend, refundShape(terms), {signatureBytes(aliceSignature), {}, bchPointLockScript(terms)});
  return true;
}

uint64_t bchPointLockSpendVirtualSize(bool claimPath) {
  // Schnorr signatures have a fixed size, so these are exact for the largest
  // timeout: 66-byte pushes of signature plus hash type (two for a claim, one
  // for a refund), the branch opcode, the 116-byte redeem script behind
  // OP_PUSHDATA1, and a 25-byte P2PKH output.
  return claimPath ? 336 : 270;
}

bool bchPointLockClaimSignature(const std::vector<uint8_t>& rawTx, const UtxoOutpoint& lock,
                                const BchPointLockTerms& terms, BchSchnorr::Signature& aliceSignature) {
  std::vector<std::vector<uint8_t>> items;
  const std::vector<uint8_t> script = bchPointLockScript(terms);
  if (!spendScriptSig(rawTx, lock, items) || items.size() != 4 || items[2] != std::vector<uint8_t>{0x01} ||
      items[3] != script || items[1].size() != aliceSignature.size() + 1 ||
      items[1].back() != static_cast<uint8_t>(BCH_SIGHASH_ALL_FORKID))
    return false;
  std::copy(items[1].begin(), items[1].begin() + aliceSignature.size(), aliceSignature.begin());
  return true;
}

bool isBchPointLockState(const std::string& chainState) { return chainState.rfind(STATE_PREFIX, 0) == 0; }

namespace {

template <size_t N>
bool hexField(const std::string& text, std::array<uint8_t, N>& out) {
  std::vector<uint8_t> bytes;
  if (text.size() != 2 * N || !Common::fromHex(text, bytes) || bytes.size() != N) return false;
  std::copy(bytes.begin(), bytes.end(), out.begin());
  return true;
}

bool decimalField(const std::string& text, uint64_t maximum, uint64_t& out) {
  if (text.empty() || text.size() > 20 || !std::all_of(text.begin(), text.end(), ::isdigit)) return false;
  out = 0;
  for (const char digit : text) {
    const uint64_t next = out * 10 + static_cast<uint64_t>(digit - '0');
    if (next / 10 != out || next > maximum) return false;
    out = next;
  }
  return true;
}

std::vector<std::string> splitFields(const std::string& text) {
  std::vector<std::string> fields;
  size_t start = 0;
  for (size_t colon = text.find(':'); colon != std::string::npos; colon = text.find(':', start)) {
    fields.push_back(text.substr(start, colon - start));
    start = colon + 1;
  }
  fields.push_back(text.substr(start));
  return fields;
}

}  // namespace

std::string encodeBchPointLockState(const BchPointLockState& state) {
  std::string encoded = std::string(STATE_PREFIX) +
                       Common::toHex(state.terms.aliceKey.data(), state.terms.aliceKey.size()) + ":" +
                       Common::toHex(state.terms.bobKey.data(), state.terms.bobKey.size()) + ":" +
                       std::to_string(state.terms.timeoutBlock);
  for (const BchPointLockClaimOffer& offer : state.claimOffers)
    encoded += ":" + std::to_string(offer.fee) + ":" +
              Common::toHex(offer.presig.R0.data(), offer.presig.R0.size()) + ":" +
              Common::toHex(offer.presig.sPrime.data(), offer.presig.sPrime.size());
  return encoded;
}

bool decodeBchPointLockState(const std::string& chainState, BchPointLockState& state) {
  if (!isBchPointLockState(chainState)) return false;
  const std::vector<std::string> fields = splitFields(chainState.substr(std::strlen(STATE_PREFIX)));
  const size_t offers = fields.size() >= 3 ? (fields.size() - 3) / 3 : 0;
  uint64_t timeout = 0;
  if (offers == 0 || offers > MAX_CLAIM_OFFERS || fields.size() != 3 + 3 * offers ||
      !hexField(fields[0], state.terms.aliceKey) || !hexField(fields[1], state.terms.bobKey) ||
      !decimalField(fields[2], 0xffffffffULL, timeout))
    return false;
  state.terms.timeoutBlock = static_cast<uint32_t>(timeout);
  state.claimOffers.assign(offers, BchPointLockClaimOffer{});
  for (size_t i = 0; i < offers; ++i) {
    BchPointLockClaimOffer& offer = state.claimOffers[i];
    if (!decimalField(fields[3 + 3 * i], UINT64_MAX, offer.fee) || !hexField(fields[4 + 3 * i], offer.presig.R0) ||
        !hexField(fields[5 + 3 * i], offer.presig.sPrime) || (i > 0 && offer.fee <= state.claimOffers[i - 1].fee))
      return false;
  }
  return true;
}

}  // namespace XfgSwap
