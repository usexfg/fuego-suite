// Copyright (c) 2017-2026 Fuego Developers
#include "BtcPtlcScript.h"
#include "BtcHtlcScript.h"
#include <openssl/sha.h>
#include <stdexcept>

namespace XfgSwap {

std::vector<uint8_t> BtcPtlcScript::createPtlcScript(
    const std::vector<uint8_t>& ptlcPointX32,
    uint32_t /*lockTime*/,
    const std::vector<uint8_t>& recipientPubKey,
    const std::vector<uint8_t>& senderPubKey,
    uint32_t timeoutBlock) {
  if (ptlcPointX32.size() != 32) throw std::runtime_error("ptlcPoint must be 32 bytes x-only");
  if (recipientPubKey.empty() || senderPubKey.empty()) throw std::runtime_error("pubkeys empty");
  std::vector<uint8_t> script;
  script.reserve(2 + 32 + 2 + recipientPubKey.size() + 2 + 10 + senderPubKey.size() + 2);
  // OP_IF
  script.push_back(0x63);
  // <ptlcPoint> OP_DROP (commitment)
  pushData(script, ptlcPointX32);
  script.push_back(0x75); // OP_DROP
  // <recipientPubKey> OP_CHECKSIG
  pushData(script, recipientPubKey);
  script.push_back(0xAC); // OP_CHECKSIG
  // OP_ELSE
  script.push_back(0x67);
  // <timeout> OP_CLTV OP_DROP
  // push timeout as minimal scriptNum
  std::vector<uint8_t> timeoutBytes;
  uint32_t t = timeoutBlock;
  while (t) { timeoutBytes.push_back(t & 0xFF); t >>= 8; }
  if (timeoutBytes.empty()) timeoutBytes.push_back(0);
  // BIP65 minimal encoding: if high bit set, append 0x00
  if (timeoutBytes.back() & 0x80) timeoutBytes.push_back(0x00);
  pushData(script, timeoutBytes);
  script.push_back(0xB1); // OP_CHECKLOCKTIMEVERIFY
  script.push_back(0x75); // OP_DROP
  pushData(script, senderPubKey);
  script.push_back(0xAC); // OP_CHECKSIG
  script.push_back(0x68); // OP_ENDIF
  return script;
}

std::vector<uint8_t> BtcPtlcScript::redeemScriptToP2wshScriptPubKey(const std::vector<uint8_t>& redeemScript) {
  return BtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);
}
std::vector<uint8_t> BtcPtlcScript::witnessScriptHash(const std::vector<uint8_t>& redeemScript) {
  return BtcHtlcScript::witnessScriptHash(redeemScript);
}
std::string BtcPtlcScript::witnessScriptToAddress(const std::vector<uint8_t>& witnessScript, const std::string& hrp) {
  return BtcHtlcScript::witnessScriptToAddress(witnessScript, hrp);
}

std::vector<std::vector<uint8_t>> BtcPtlcScript::createClaimWitness(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& adaptorSecret32,
    const std::vector<uint8_t>& witnessScript) {
  std::vector<std::vector<uint8_t>> witness;
  witness.reserve(4);
  witness.push_back(signature);
  witness.push_back(adaptorSecret32);
  witness.push_back({BtcOpCode::OP_TRUE}); // OP_1
  witness.push_back(witnessScript);
  return witness;
}

std::vector<std::vector<uint8_t>> BtcPtlcScript::createRefundWitness(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& witnessScript) {
  return BtcHtlcScript::createRefundWitness(signature, witnessScript);
}

std::vector<uint8_t> BtcPtlcScript::parseClaimAdaptorSecret(
    const std::vector<uint8_t>& rawTx,
    const std::vector<uint8_t>& p2wshScriptPubKey) {
  return BtcHtlcScript::parseClaimPreimage(rawTx, p2wshScriptPubKey);
}

std::vector<uint8_t> BtcPtlcScript::hexToBytes(const std::string& hex) {
  return BtcHtlcScript::hexToBytes(hex);
}
std::string BtcPtlcScript::bytesToHex(const std::vector<uint8_t>& bytes) {
  return BtcHtlcScript::bytesToHex(bytes);
}
std::vector<uint8_t> BtcPtlcScript::sha256(const std::vector<uint8_t>& data) {
  return BtcHtlcScript::sha256(data);
}

void BtcPtlcScript::pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data) {
  if (data.size() < 0x4C) {
    script.push_back(static_cast<uint8_t>(data.size()));
  } else if (data.size() <= 0xFF) {
    script.push_back(0x4C); script.push_back(static_cast<uint8_t>(data.size()));
  } else if (data.size() <= 0xFFFF) {
    script.push_back(0x4D);
    script.push_back(data.size() & 0xFF); script.push_back((data.size()>>8)&0xFF);
  } else {
    script.push_back(0x4E);
    writeLE32(script, static_cast<uint32_t>(data.size()));
  }
  script.insert(script.end(), data.begin(), data.end());
}
void BtcPtlcScript::writeLE32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(v & 0xFF); out.push_back((v>>8)&0xFF); out.push_back((v>>16)&0xFF); out.push_back((v>>24)&0xFF);
}
void BtcPtlcScript::writeVarInt(std::vector<uint8_t>& out, uint64_t n) {
  if (n < 0xFD) out.push_back(static_cast<uint8_t>(n));
  else if (n <= 0xFFFF) { out.push_back(0xFD); out.push_back(n&0xFF); out.push_back((n>>8)&0xFF); }
  else if (n <= 0xFFFFFFFF) { out.push_back(0xFE); writeLE32(out, static_cast<uint32_t>(n)); }
  else { out.push_back(0xFF); for(int i=0;i<8;++i) out.push_back((n>>(8*i))&0xFF); }
}

} // namespace XfgSwap
