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

#include "UtxoPointLock.h"

#include <openssl/sha.h>
#include <secp256k1.h>
#include <secp256k1_extrakeys.h>

#include <algorithm>
#include <cstring>

#include "Common/StringTools.h"
#include "SwapDaemon/Bitcoin/BtcTaprootPtlc.h"
#include "UtxoHtlc.h"

namespace XfgSwap {
namespace {

using Hash32 = std::array<uint8_t, 32>;

// BIP341's provably unspendable internal key: lift_x(SHA256(G)).
constexpr Hash32 UNSPENDABLE_INTERNAL_KEY = {0x50, 0x92, 0x9b, 0x74, 0xc1, 0xa0, 0x49, 0x54, 0xb7, 0x8b, 0x4b,
                                             0x60, 0x35, 0xe9, 0x7a, 0x5e, 0x07, 0x8a, 0x5a, 0x0f, 0x28, 0xec,
                                             0x96, 0xd5, 0x47, 0xbf, 0xee, 0x9a, 0xce, 0x80, 0x3a, 0xc0};
constexpr uint8_t TAPSCRIPT_LEAF_VERSION = 0xc0;
constexpr uint8_t OP_CHECKSIG = 0xac;
constexpr uint8_t OP_CHECKSIGVERIFY = 0xad;
constexpr uint8_t OP_CHECKLOCKTIMEVERIFY = 0xb1;
constexpr uint8_t OP_DROP = 0x75;
constexpr uint32_t CLAIM_SEQUENCE = 0xfffffffd;
constexpr uint32_t REFUND_SEQUENCE = 0xfffffffe;
constexpr uint32_t TX_VERSION = 2;
const char STATE_PREFIX[] = "tr1:";

const secp256k1_context* verifyContext() {
  static secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
  return ctx;
}

Hash32 sha256(const std::vector<uint8_t>& data) {
  Hash32 digest;
  SHA256(data.data(), data.size(), digest.data());
  return digest;
}

Hash32 taggedHash(const char* tag, const std::vector<uint8_t>& data) {
  const Hash32 tagHash = sha256(std::vector<uint8_t>(tag, tag + std::strlen(tag)));
  std::vector<uint8_t> preimage(tagHash.begin(), tagHash.end());
  preimage.insert(preimage.end(), tagHash.begin(), tagHash.end());
  preimage.insert(preimage.end(), data.begin(), data.end());
  return sha256(preimage);
}

void appendLE32(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

void appendLE64(std::vector<uint8_t>& out, uint64_t value) {
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

void appendCompactSize(std::vector<uint8_t>& out, uint64_t size) {
  if (size < 0xfd) {
    out.push_back(static_cast<uint8_t>(size));
  } else if (size <= 0xffff) {
    out.push_back(0xfd);
    out.push_back(static_cast<uint8_t>(size));
    out.push_back(static_cast<uint8_t>(size >> 8));
  } else {
    out.push_back(0xfe);
    appendLE32(out, static_cast<uint32_t>(size));
  }
}

void appendWithSize(std::vector<uint8_t>& out, const std::vector<uint8_t>& data) {
  appendCompactSize(out, data.size());
  out.insert(out.end(), data.begin(), data.end());
}

void appendScriptNumber(std::vector<uint8_t>& script, uint32_t value) {
  if (value == 0) {
    script.push_back(0x00);
    return;
  }
  if (value <= 16) {
    script.push_back(static_cast<uint8_t>(0x50 + value));
    return;
  }
  std::vector<uint8_t> bytes;
  for (uint64_t remaining = value; remaining > 0; remaining >>= 8) bytes.push_back(static_cast<uint8_t>(remaining));
  if (bytes.back() & 0x80) bytes.push_back(0x00);
  script.push_back(static_cast<uint8_t>(bytes.size()));
  script.insert(script.end(), bytes.begin(), bytes.end());
}

void appendXOnlyKey(std::vector<uint8_t>& script, const Bip340::XOnlyKey& key) {
  script.push_back(0x20);
  script.insert(script.end(), key.begin(), key.end());
}

Hash32 tapLeafHash(const std::vector<uint8_t>& script) {
  std::vector<uint8_t> data{TAPSCRIPT_LEAF_VERSION};
  appendWithSize(data, script);
  return taggedHash("TapLeaf", data);
}

Hash32 tapBranchHash(const Hash32& a, const Hash32& b) {
  const bool aFirst = std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
  std::vector<uint8_t> data(aFirst ? a.begin() : b.begin(), aFirst ? a.end() : b.end());
  data.insert(data.end(), aFirst ? b.begin() : a.begin(), aFirst ? b.end() : a.end());
  return taggedHash("TapBranch", data);
}

struct TaprootOutput {
  Bip340::XOnlyKey outputKey;
  bool outputKeyOddY = false;
  Hash32 claimLeaf;
  Hash32 refundLeaf;
};

bool taprootOutput(const PointLockTerms& terms, TaprootOutput& output) {
  output.claimLeaf = tapLeafHash(pointLockClaimScript(terms));
  output.refundLeaf = tapLeafHash(pointLockRefundScript(terms));
  std::vector<uint8_t> tweakData(UNSPENDABLE_INTERNAL_KEY.begin(), UNSPENDABLE_INTERNAL_KEY.end());
  const Hash32 merkleRoot = tapBranchHash(output.claimLeaf, output.refundLeaf);
  tweakData.insert(tweakData.end(), merkleRoot.begin(), merkleRoot.end());
  const Hash32 tweak = taggedHash("TapTweak", tweakData);

  secp256k1_xonly_pubkey internalKey, outputXOnly;
  secp256k1_pubkey outputKey;
  int parity = 0;
  if (secp256k1_xonly_pubkey_parse(verifyContext(), &internalKey, UNSPENDABLE_INTERNAL_KEY.data()) != 1 ||
      secp256k1_xonly_pubkey_tweak_add(verifyContext(), &outputKey, &internalKey, tweak.data()) != 1 ||
      secp256k1_xonly_pubkey_from_pubkey(verifyContext(), &outputXOnly, &parity, &outputKey) != 1 ||
      secp256k1_xonly_pubkey_serialize(verifyContext(), output.outputKey.data(), &outputXOnly) != 1)
    return false;
  output.outputKeyOddY = parity != 0;
  return true;
}

std::vector<uint8_t> controlBlock(const TaprootOutput& output, const Hash32& sibling) {
  std::vector<uint8_t> block{static_cast<uint8_t>(TAPSCRIPT_LEAF_VERSION | (output.outputKeyOddY ? 1 : 0))};
  block.insert(block.end(), UNSPENDABLE_INTERNAL_KEY.begin(), UNSPENDABLE_INTERNAL_KEY.end());
  block.insert(block.end(), sibling.begin(), sibling.end());
  return block;
}

std::vector<uint8_t> outpointBytes(const UtxoOutpoint& lock) {
  std::vector<uint8_t> txid;
  if (!Common::fromHex(lock.txid, txid) || txid.size() != 32) return {};
  std::vector<uint8_t> bytes(txid.rbegin(), txid.rend());
  appendLE32(bytes, lock.vout);
  return bytes;
}

struct SpendShape {
  uint32_t sequence;
  uint32_t lockTime;
};

SpendShape claimShape() { return {CLAIM_SEQUENCE, 0}; }
SpendShape refundShape(const PointLockTerms& terms) { return {REFUND_SEQUENCE, terms.timeoutBlock}; }

bool scriptPathSighash(const PointLockTerms& terms, const PointLockSpend& spend, const SpendShape& shape,
                       const std::vector<uint8_t>& leafScript, Bip340::Message& sighash) {
  std::vector<uint8_t> scriptPubKey;
  const std::vector<uint8_t> outpoint = outpointBytes(spend.lock);
  if (outpoint.empty() || spend.fee >= spend.lockValue || !pointLockScriptPubKey(terms, scriptPubKey)) return false;

  std::vector<uint8_t> amounts, scriptPubKeys, sequences, outputs;
  appendLE64(amounts, spend.lockValue);
  appendWithSize(scriptPubKeys, scriptPubKey);
  appendLE32(sequences, shape.sequence);
  appendLE64(outputs, spend.lockValue - spend.fee);
  appendWithSize(outputs, spend.destinationScript);

  std::vector<uint8_t> message{0x00, 0x00};  // epoch, SIGHASH_DEFAULT
  appendLE32(message, TX_VERSION);
  appendLE32(message, shape.lockTime);
  for (const std::vector<uint8_t>& field : {outpoint, amounts, scriptPubKeys, sequences, outputs}) {
    const Hash32 digest = sha256(field);
    message.insert(message.end(), digest.begin(), digest.end());
  }
  message.push_back(0x02);  // script path, no annex
  appendLE32(message, 0);   // input index
  const Hash32 leafHash = tapLeafHash(leafScript);
  message.insert(message.end(), leafHash.begin(), leafHash.end());
  message.push_back(0x00);           // key version
  appendLE32(message, 0xffffffff);   // no OP_CODESEPARATOR
  sighash = taggedHash("TapSighash", message);
  return true;
}

bool spendTransaction(const PointLockSpend& spend, const SpendShape& shape,
                      const std::vector<std::vector<uint8_t>>& witness, std::vector<uint8_t>& rawTx) {
  const std::vector<uint8_t> outpoint = outpointBytes(spend.lock);
  if (outpoint.empty() || spend.fee >= spend.lockValue) return false;
  rawTx.clear();
  appendLE32(rawTx, TX_VERSION);
  rawTx.push_back(0x00);  // segwit marker
  rawTx.push_back(0x01);  // segwit flag
  rawTx.push_back(0x01);  // one input
  rawTx.insert(rawTx.end(), outpoint.begin(), outpoint.end());
  rawTx.push_back(0x00);  // empty scriptSig
  appendLE32(rawTx, shape.sequence);
  rawTx.push_back(0x01);  // one output
  appendLE64(rawTx, spend.lockValue - spend.fee);
  appendWithSize(rawTx, spend.destinationScript);
  appendCompactSize(rawTx, witness.size());
  for (const auto& item : witness) appendWithSize(rawTx, item);
  appendLE32(rawTx, shape.lockTime);
  return true;
}

std::vector<uint8_t> signatureBytes(const Bip340::Signature& signature) {
  return std::vector<uint8_t>(signature.begin(), signature.end());
}

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

std::vector<uint8_t> pointLockClaimScript(const PointLockTerms& terms) {
  std::vector<uint8_t> script;
  appendXOnlyKey(script, terms.aliceKey);
  script.push_back(OP_CHECKSIGVERIFY);
  appendXOnlyKey(script, terms.bobKey);
  script.push_back(OP_CHECKSIG);
  return script;
}

std::vector<uint8_t> pointLockRefundScript(const PointLockTerms& terms) {
  std::vector<uint8_t> script;
  appendScriptNumber(script, terms.timeoutBlock);
  script.push_back(OP_CHECKLOCKTIMEVERIFY);
  script.push_back(OP_DROP);
  appendXOnlyKey(script, terms.aliceKey);
  script.push_back(OP_CHECKSIG);
  return script;
}

bool pointLockScriptPubKey(const PointLockTerms& terms, std::vector<uint8_t>& scriptPubKey) {
  TaprootOutput output;
  if (!taprootOutput(terms, output)) return false;
  scriptPubKey = {0x51, 0x20};
  scriptPubKey.insert(scriptPubKey.end(), output.outputKey.begin(), output.outputKey.end());
  return true;
}

std::string pointLockAddress(const PointLockTerms& terms, const std::string& bech32Hrp) {
  TaprootOutput output;
  if (bech32Hrp.empty() || !taprootOutput(terms, output)) return {};
  return BtcTaprootPtlc::xOnlyToP2trAddress(output.outputKey, bech32Hrp);
}

bool pointLockClaimSighash(const PointLockTerms& terms, const PointLockSpend& spend, Bip340::Message& sighash) {
  return scriptPathSighash(terms, spend, claimShape(), pointLockClaimScript(terms), sighash);
}

bool pointLockRefundSighash(const PointLockTerms& terms, const PointLockSpend& spend, Bip340::Message& sighash) {
  return scriptPathSighash(terms, spend, refundShape(terms), pointLockRefundScript(terms), sighash);
}

bool pointLockClaimTransaction(const PointLockTerms& terms, const PointLockSpend& spend,
                               const Bip340::Signature& bobSignature, const Bip340::Signature& aliceSignature,
                               std::vector<uint8_t>& rawTx) {
  TaprootOutput output;
  if (!taprootOutput(terms, output)) return false;
  const std::vector<std::vector<uint8_t>> witness = {signatureBytes(bobSignature), signatureBytes(aliceSignature),
                                                     pointLockClaimScript(terms),
                                                     controlBlock(output, output.refundLeaf)};
  return spendTransaction(spend, claimShape(), witness, rawTx);
}

bool pointLockRefundTransaction(const PointLockTerms& terms, const PointLockSpend& spend,
                                const Bip340::Signature& aliceSignature, std::vector<uint8_t>& rawTx) {
  TaprootOutput output;
  if (!taprootOutput(terms, output)) return false;
  const std::vector<std::vector<uint8_t>> witness = {signatureBytes(aliceSignature), pointLockRefundScript(terms),
                                                     controlBlock(output, output.claimLeaf)};
  return spendTransaction(spend, refundShape(terms), witness, rawTx);
}

uint64_t pointLockSpendVirtualSize(bool claimPath) {
  // 85 non-witness bytes with a 25-byte destination script; the witness holds
  // the signatures, the leaf script (refund timeout pushed in at most 5 bytes)
  // and a 65-byte control block.
  const uint64_t nonWitnessWeight = 85 * 4;
  const uint64_t witnessWeight = claimPath ? 2 + 1 + 65 + 65 + 69 + 66 : 2 + 1 + 65 + 42 + 66;
  return (nonWitnessWeight + witnessWeight + 3) / 4;
}

bool pointLockClaimSignature(const std::vector<uint8_t>& rawTx, const UtxoOutpoint& lock,
                             const PointLockTerms& terms, Bip340::Signature& aliceSignature) {
  TaprootOutput output;
  std::vector<std::vector<uint8_t>> witness;
  if (!taprootOutput(terms, output) || !spendWitness(rawTx, lock, witness) || witness.size() != 4 ||
      witness[2] != pointLockClaimScript(terms) || witness[3] != controlBlock(output, output.refundLeaf) ||
      witness[1].size() != aliceSignature.size())
    return false;
  std::copy(witness[1].begin(), witness[1].end(), aliceSignature.begin());
  return true;
}

bool isPointLockState(const std::string& chainState) { return chainState.rfind(STATE_PREFIX, 0) == 0; }

std::vector<uint64_t> claimFeeLadder(uint64_t baseFee, uint64_t ceiling) {
  std::vector<uint64_t> fees;
  for (uint64_t fee = std::max<uint64_t>(baseFee, 1); fees.size() < MAX_CLAIM_OFFERS; fee *= CLAIM_FEE_STEP) {
    const uint64_t capped = std::min(fee, ceiling);
    if (!fees.empty() && capped <= fees.back()) break;
    fees.push_back(capped);
    if (capped == ceiling || fee > UINT64_MAX / CLAIM_FEE_STEP) break;
  }
  return fees;
}

std::string encodePointLockState(const PointLockState& state) {
  std::string encoded = std::string(STATE_PREFIX) +
                        Common::toHex(state.terms.aliceKey.data(), state.terms.aliceKey.size()) + ":" +
                        Common::toHex(state.terms.bobKey.data(), state.terms.bobKey.size()) + ":" +
                        std::to_string(state.terms.timeoutBlock);
  for (const PointLockClaimOffer& offer : state.claimOffers)
    encoded += ":" + std::to_string(offer.fee) + ":" +
               Common::toHex(offer.presig.R0.data(), offer.presig.R0.size()) + ":" +
               Common::toHex(offer.presig.sPrime.data(), offer.presig.sPrime.size());
  return encoded;
}

bool decodePointLockState(const std::string& chainState, PointLockState& state) {
  if (!isPointLockState(chainState)) return false;
  const std::vector<std::string> fields = splitFields(chainState.substr(std::strlen(STATE_PREFIX)));
  const size_t offers = fields.size() >= 3 ? (fields.size() - 3) / 3 : 0;
  uint64_t timeout = 0;
  if (offers == 0 || offers > MAX_CLAIM_OFFERS || fields.size() != 3 + 3 * offers ||
      !hexField(fields[0], state.terms.aliceKey) || !hexField(fields[1], state.terms.bobKey) ||
      !decimalField(fields[2], 0xffffffffULL, timeout))
    return false;
  state.terms.timeoutBlock = static_cast<uint32_t>(timeout);
  state.claimOffers.assign(offers, PointLockClaimOffer{});
  for (size_t i = 0; i < offers; ++i) {
    PointLockClaimOffer& offer = state.claimOffers[i];
    if (!decimalField(fields[3 + 3 * i], UINT64_MAX, offer.fee) || !hexField(fields[4 + 3 * i], offer.presig.R0) ||
        !hexField(fields[5 + 3 * i], offer.presig.sPrime) || (i > 0 && offer.fee <= state.claimOffers[i - 1].fee))
      return false;
  }
  return true;
}

}  // namespace XfgSwap
