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

#include "LtcChainClient.h"
#include "LtcHtlcScript.h"
#include "Bitcoin/BtcHtlcScript.h"
#include "Bitcoin/BtcPtlcScript.h"
#include "Bitcoin/BtcTaprootPtlc.h"
#include "Common/StringTools.h"
#include "../SwapHashLock.h"
#include "../SwapPtlcLock.h"
#include "../Crypto/Secp256k1Signer.h"

#include <array>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include "Komodo/KmdHtlcScript.h"

namespace XfgSwap {

// ── Pure PTLC helpers (P2.3 mirror of BtcChainClient) ────────────────────────
// chainState: "p2tr:<tweakedPub33hex>|ptlc:<ptlcPointHex>[|presig:<R33hex><sPrime32hex>]"

static bool parsePtlcP2trStateLtc(const std::string& chainState,
                                  std::vector<uint8_t>& tweakedPub33,
                                  std::array<uint8_t, 32>& ptlcX,
                                  Crypto::SecpAdaptorPresig& presig,
                                  bool& havePresig) {
  if (chainState.rfind("p2tr:", 0) != 0) return false;
  std::string rest = chainState.substr(5);
  auto pipe = rest.find('|');
  std::string tweakedHex = (pipe == std::string::npos) ? rest : rest.substr(0, pipe);
  std::string ptlcHex, presigHex;
  if (pipe != std::string::npos) {
    std::string tail = rest.substr(pipe + 1);
    size_t start = 0;
    while (start <= tail.size()) {
      size_t end = tail.find('|', start);
      std::string seg = (end == std::string::npos) ? tail.substr(start)
                                                   : tail.substr(start, end - start);
      if (seg.rfind("ptlc:", 0) == 0) ptlcHex = seg.substr(5);
      else if (seg.rfind("presig:", 0) == 0) presigHex = seg.substr(7);
      if (end == std::string::npos) break;
      start = end + 1;
    }
  }
  try {
    tweakedPub33 = BtcTaprootPtlc::hexToBytes(tweakedHex);
    auto ptBytes = BtcTaprootPtlc::hexToBytes(ptlcHex);
    if (tweakedPub33.size() != 33 || ptBytes.size() != 32) return false;
    std::memcpy(ptlcX.data(), ptBytes.data(), 32);
    havePresig = false;
    if (presigHex.size() == 198) {
      auto rBytes = BtcTaprootPtlc::hexToBytes(presigHex.substr(0, 66));
      auto sBytes = BtcTaprootPtlc::hexToBytes(presigHex.substr(66));
      if (rBytes.size() == 33 && sBytes.size() == 32) {
        std::memcpy(presig.R.data.data(), rBytes.data(), 33);
        std::memcpy(presig.s_prime.data(), sBytes.data(), 32);
        havePresig = true;
      }
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

static bool wifToKeyPairLtc(const std::string& wif,
                            std::array<uint8_t, 32>& priv,
                            std::vector<uint8_t>& pub33) {
  if (!LtcHtlcScript::wifToPrivKey(wif, priv)) return false;
  try {
    CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
    pub33 = signer.derivePublicKeyCompressed(priv);
  } catch (const std::exception&) {
    return false;
  }
  return pub33.size() == 33;
}

static bool isZeroSecretLtc(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}

// Inline varint reader (duplicated from LtcHtlcScript.cpp for isolation)
static bool readVarInt(const uint8_t*& p, const uint8_t* end, uint64_t& out) {
  if (p >= end) return false;
  uint8_t first = *p++;
  if (first < 0xFD) {
    out = first;
    return true;
  } else if (first == 0xFD) {
    if (p + 2 > end) return false;
    out = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
    p += 2;
    return true;
  } else if (first == 0xFE) {
    if (p + 4 > end) return false;
    out = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
          (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
    p += 4;
    return true;
  } else {
    if (p + 8 > end) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    p += 8;
    return true;
  }
}

std::string LtcChainClient::getReceiveAddress() const {
  if (m_wif.empty()) return "";
  std::vector<uint8_t> h;
  if (!KmdHtlcScript::wifToPubkeyHash(m_wif, h)) return "";
  return KmdHtlcScript::base58CheckEncode(0x30, h);
}
static bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}

LtcChainClient::LtcChainClient(std::unique_ptr<LtcRpcClient> rpc, const std::string& wif)
  : m_rpc(std::move(rpc)), m_wif(wif) {}

LtcChainClient::LtcChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif)
  : m_spvClient(std::move(spvClient)), m_wif(wif) {}

ChainClientResult LtcChainClient::lock(const SwapParams& params) {
  if (params.lockType == SwapLockType::PTLC) return lockPtlc(params);
  if (!m_rpc)
    return ChainClientResult::fail("LTC lock: RPC client not available (SPV mode does not support lock)");

  std::string hashHex;
  if (!isZeroSecret(params.adaptorSecret)) {
    hashHex = bchHashLockHex(params.adaptorSecret);
  } else {
    bool nonzero = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nonzero = true; break; }
    if (!nonzero)
      return ChainClientResult::fail("LTC lock: no adaptor secret or hashLock (need H(t) from Bob)");
    hashHex = Common::podToHex(params.hashLock);
  }

  std::string recipientKey = params.ctrPubKey;
  if (recipientKey.empty() && params.ctrAddress.size() == 66) {
    try {
      auto bytes = BtcHtlcScript::hexToBytes(params.ctrAddress);
      if (bytes.size() == 33 && (bytes[0] == 0x02 || bytes[0] == 0x03))
        recipientKey = params.ctrAddress;
    } catch (const std::exception&) {}
  }
  if (recipientKey.size() != 66 && !params.ctrAddress.empty()) {
    std::string resolved;
    if (m_rpc->getAddressPubkey(params.ctrAddress, resolved) && resolved.size() == 66)
      recipientKey = resolved;
  }
  if (recipientKey.size() != 66)
    return ChainClientResult::fail(
        "LTC lock: ctrPubKey must be 33-byte compressed pubkey hex (66 chars). "
        "Cannot derive pubkey from address; set ctrPubKey or use a wallet-known address.");

  std::string lockTxId;
  std::string redeemScriptHex;
  bool ok = m_rpc->lockHtlc(
      m_wif,
      recipientKey,
      hashHex,
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAmount,
      lockTxId,
      redeemScriptHex);
  if (!ok) return ChainClientResult::fail("LTC lockHtlc failed");
  return ChainClientResult::okWithState(lockTxId, redeemScriptHex);
}

ChainClientResult LtcChainClient::lockPtlc(const SwapParams& params) {
  // ── Pure PTLC (P2.3): fund a real P2TR output at the tweaked key ──
  if (params.lockType == SwapLockType::PTLC) return lockPtlcPureP2tr(params);
  if (!m_rpc) return ChainClientResult::fail("LTC PtlcLock: RPC not available");
  Crypto::PublicKey pt = params.ptlcPoint; Crypto::PublicKey zero{}; std::memset(&zero,0,sizeof(zero));
  if (std::memcmp(&pt,&zero,sizeof(zero))==0) pt=params.adaptorPoint;
  if (std::memcmp(&pt,&zero,sizeof(zero))==0) return ChainClientResult::fail("LTC PtlcLock: no ptlcPoint");
  std::vector<uint8_t> ptBytes(reinterpret_cast<uint8_t*>(&pt), reinterpret_cast<uint8_t*>(&pt)+32);
  std::string recipientKey=params.ctrPubKey;
  if (recipientKey.empty() && params.ctrAddress.size()==66) { try{auto b=BtcHtlcScript::hexToBytes(params.ctrAddress); if(b.size()==33) recipientKey=params.ctrAddress;}catch (const std::exception&){}}
  if (recipientKey.size()!=66 && !params.ctrAddress.empty()) { std::string r; if(m_rpc->getAddressPubkey(params.ctrAddress,r) && r.size()==66) recipientKey=r; }
  if (recipientKey.size()!=66) return ChainClientResult::fail("LTC PtlcLock: need 33-byte pubkey");
  std::string senderKey; std::string pub; if(m_rpc->getAddressPubkey(getReceiveAddress(),pub) && pub.size()==66) senderKey=pub; if(senderKey.empty()) senderKey=recipientKey;
  auto recBytes=BtcHtlcScript::hexToBytes(recipientKey); auto sendBytes=BtcHtlcScript::hexToBytes(senderKey);
  auto redeem=BtcPtlcScript::createPtlcScript(ptBytes,0,recBytes,sendBytes,static_cast<uint32_t>(params.ctrTimeoutBlock));
  std::string redeemHex=BtcHtlcScript::bytesToHex(redeem); std::string lockTxId; std::string dummy;
  std::string ptHashHex=Common::podToHex(pt);
  bool ok=m_rpc->lockHtlc(m_wif,recipientKey,ptHashHex,static_cast<uint32_t>(params.ctrTimeoutBlock),params.ctrAmount,lockTxId,dummy);
  if(!ok) return ChainClientResult::fail("LTC PtlcLock failed");
  return ChainClientResult::okWithState(lockTxId, redeemHex);
}
ChainClientResult LtcChainClient::lockPtlcPureP2tr(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("LTC PtlcLock pure: RPC not available");

  Crypto::PublicKey pt = params.ptlcPoint;
  Crypto::PublicKey zero{}; std::memset(&zero, 0, sizeof(zero));
  if (std::memcmp(&pt, &zero, sizeof(zero)) == 0) pt = params.adaptorPoint;
  if (std::memcmp(&pt, &zero, sizeof(zero)) == 0)
    return ChainClientResult::fail("LTC PtlcLock pure: no ptlcPoint/adaptorPoint");
  std::vector<uint8_t> ptX32(reinterpret_cast<uint8_t*>(&pt),
                             reinterpret_cast<uint8_t*>(&pt) + 32);

  std::string recipientKey = params.ctrPubKey;
  if (recipientKey.empty() && params.ctrAddress.size() == 66) {
    try {
      auto b = BtcHtlcScript::hexToBytes(params.ctrAddress);
      if (b.size() == 33) recipientKey = params.ctrAddress;
    } catch (const std::exception&) {}
  }
  if (recipientKey.size() != 66 && !params.ctrAddress.empty()) {
    std::string r;
    if (m_rpc->getAddressPubkey(params.ctrAddress, r) && r.size() == 66) recipientKey = r;
  }
  if (recipientKey.size() != 66)
    return ChainClientResult::fail("LTC PtlcLock pure: need 33-byte recipient pubkey");

  std::array<uint8_t, 32> ourPriv{};
  std::vector<uint8_t> ourPub33;
  if (!wifToKeyPairLtc(m_wif, ourPriv, ourPub33))
    return ChainClientResult::fail("LTC PtlcLock pure: invalid WIF");

  try {
    auto out = BtcTaprootPtlc::createTaprootPtlc(
        ourPub33, ptX32,
        static_cast<uint32_t>(params.ctrTimeoutBlock),
        BtcHtlcScript::hexToBytes(recipientKey), ourPub33,
        /*hrp=*/"ltc");

    m_rpc->importAddress(out.p2trAddress, "fuego-ptlc", false);

    std::string lockTxId;
    if (!m_rpc->sendToAddress(out.p2trAddress, params.ctrAmount, lockTxId))
      return ChainClientResult::fail("LTC PtlcLock pure: sendtoaddress to " +
                                     out.p2trAddress + " failed");

    std::string state = "p2tr:" + BtcTaprootPtlc::bytesToHex(out.tweakedPubKey) +
                        "|ptlc:" + BtcTaprootPtlc::bytesToHex(ptX32);
    return ChainClientResult::okWithState(lockTxId, state);
  } catch (const std::exception& e) {
    return ChainClientResult::fail(std::string("LTC PtlcLock pure: ") + e.what());
  }
}

ChainClientResult LtcChainClient::claimOrRefundPtlcP2tr(const SwapParams& params) {
  std::vector<uint8_t> tweaked33;
  std::array<uint8_t, 32> ptlcX{};
  Crypto::SecpAdaptorPresig presig;
  bool havePresig = false;
  if (!parsePtlcP2trStateLtc(params.chainState, tweaked33, ptlcX, presig, havePresig))
    return ChainClientResult::fail("LTC Ptlc spend: chainState is not a valid p2tr state");

  std::array<uint8_t, 32> ourPriv{};
  std::vector<uint8_t> ourPub33;
  if (!wifToKeyPairLtc(m_wif, ourPriv, ourPub33))
    return ChainClientResult::fail("LTC Ptlc spend: invalid WIF");

  std::string recipientKey = params.ctrPubKey;
  if (recipientKey.empty() && params.ctrAddress.size() == 66) {
    try {
      auto b = BtcHtlcScript::hexToBytes(params.ctrAddress);
      if (b.size() == 33) recipientKey = params.ctrAddress;
    } catch (const std::exception&) {}
  }
  if (recipientKey.size() != 66)
    return ChainClientResult::fail("LTC Ptlc spend: need 33-byte recipient pubkey to recompute tweak");

  TaprootPtlcOutput out;
  try {
    out = BtcTaprootPtlc::createTaprootPtlc(
        ourPub33, std::vector<uint8_t>(ptlcX.begin(), ptlcX.end()),
        static_cast<uint32_t>(params.ctrTimeoutBlock),
        BtcHtlcScript::hexToBytes(recipientKey), ourPub33, /*hrp=*/"ltc");
  } catch (const std::exception& e) {
    return ChainClientResult::fail(std::string("LTC Ptlc spend: rebuild failed: ") + e.what());
  }
  if (BtcTaprootPtlc::bytesToHex(out.tweakedPubKey) != BtcTaprootPtlc::bytesToHex(tweaked33))
    return ChainClientResult::fail("LTC Ptlc spend: recomputed tweaked key does not match chainState");

  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  std::vector<uint8_t> destSpk;
  if (BtcHtlcScript::base58CheckDecode(params.ctrAddress, addrVersion, pubKeyHash) &&
      pubKeyHash.size() == 20) {
    destSpk = BtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);
  } else if (params.ctrAddress == out.p2trAddress ||
             (!params.ctrAddress.empty() &&
              params.ctrAddress.rfind("ltc1p", 0) == 0)) {
    destSpk = BtcTaprootPtlc::p2trScriptPubKey(out.tweakedPubKeyXOnly);
  } else {
    return ChainClientResult::fail(
        "LTC Ptlc spend: unsupported destination address " + params.ctrAddress);
  }

  const uint64_t fee = 1000;
  if (params.ctrAmount <= fee)
    return ChainClientResult::fail("LTC Ptlc spend: amount too small for fee");
  const uint64_t outputAmount = params.ctrAmount - fee;

  std::array<uint8_t, 32> sighash{};
  if (!BtcTaprootPtlc::computeTaprootKeyPathSighash(
          params.ctrLockTxId, 0, params.ctrAmount,
          out.tweakedPubKeyXOnly, destSpk, outputAmount,
          /*nVersion=*/2, /*nSequence=*/0xFFFFFFFD, /*nLockTime=*/0, sighash))
    return ChainClientResult::fail("LTC Ptlc spend: sighash computation failed");

  Crypto::SecpSchnorrSig schnorr{};
  bool signedOk = false;
  if (havePresig && !isZeroSecretLtc(params.adaptorSecret)) {
    signedOk = BtcTaprootPtlc::adaptorToSchnorrSig(presig, params.adaptorSecret, schnorr);
  }
  if (!signedOk) {
    std::vector<uint8_t> sig64;
    if (!BtcTaprootPtlc::signTaprootKeyPath(ourPriv, out.tapTweak,
                                            out.tweakedPubKeyXOnly, sighash, sig64))
      return ChainClientResult::fail("LTC Ptlc spend: Schnorr signing failed");
    std::memcpy(schnorr.data.data(), sig64.data(), 64);
  }

  auto witnessStack = BtcTaprootPtlc::createKeyPathClaimWitness(
      std::vector<uint8_t>(schnorr.data.begin(), schnorr.data.end()));

  auto rawTx = BtcTaprootPtlc::buildRawTaprootSpendTx(
      params.ctrLockTxId, 0, witnessStack, destSpk, outputAmount);

  std::string txid;
  if (m_spvClient) {
    if (!m_spvClient->broadcastTx(rawTx, txid))
      return ChainClientResult::fail("LTC Ptlc spend SPV: broadcast failed");
    return ChainClientResult::ok(txid);
  }
  if (!m_rpc)
    return ChainClientResult::fail("LTC Ptlc spend: no RPC or SPV client available");
  if (!m_rpc->sendRawTransaction(BtcTaprootPtlc::bytesToHex(rawTx), txid))
    return ChainClientResult::fail("LTC Ptlc spend: sendrawtransaction failed");
  return ChainClientResult::ok(txid);
}

ChainClientResult LtcChainClient::verifyPtlcLock(const SwapParams& params) {
  // Pure PTLC (chainState "p2tr:..."): verify the P2TR output on-chain.
  std::vector<uint8_t> tweaked33;
  std::array<uint8_t, 32> ptlcX{};
  Crypto::SecpAdaptorPresig presig;
  bool havePresig = false;
  if (!parsePtlcP2trStateLtc(params.chainState, tweaked33, ptlcX, presig, havePresig))
    return verifyLock(params); // bridge state → legacy HTLC verification

  std::string p2trAddress;
  try {
    p2trAddress = BtcTaprootPtlc::tweakedPubToP2trAddress(tweaked33, /*hrp=*/"ltc");
  } catch (const std::exception&) {
    return ChainClientResult::fail("LTC verifyPtlcLock: invalid tweaked pubkey in chainState");
  }

  if (m_rpc) {
    bool ok = m_rpc->verifyLock(p2trAddress, params.ctrAmount);
    if (!ok)
      return ChainClientResult::fail("LTC PTLC lock not verified at " + p2trAddress);
    return ChainClientResult::ok(params.ctrLockTxId);
  }

  if (m_spvClient && !params.ctrLockTxId.empty()) {
    std::vector<uint8_t> rawTx;
    if (!m_spvClient->getRawTx(params.ctrLockTxId, rawTx))
      return ChainClientResult::fail("LTC verifyPtlcLock SPV: getRawTx failed for " +
                                     params.ctrLockTxId);

    auto xOnlyVec = std::vector<uint8_t>(tweaked33.begin() + 1, tweaked33.end());
    bool foundP2tr = false;
    const uint8_t* p = rawTx.data();
    const uint8_t* end = rawTx.data() + rawTx.size();
    auto readVar = [&](uint64_t& out) -> bool {
      if (p >= end) return false;
      uint8_t first = *p++;
      if (first < 0xFD) { out = first; return true; }
      int ext = (first == 0xFD) ? 2 : (first == 0xFE ? 4 : 8);
      if ((first != 0xFD && first != 0xFE && first != 0xFF) || p + ext > end) return false;
      out = 0;
      for (int i = 0; i < ext; ++i) out |= static_cast<uint64_t>(p[i]) << (i * 8);
      p += ext;
      return true;
    };
    p += 4; // version
    uint64_t vinCount = 0, voutCount = 0;
    if (!readVar(vinCount))
      return ChainClientResult::fail("LTC verifyPtlcLock SPV: truncated tx");
    for (uint64_t i = 0; i < vinCount; ++i) {
      if (p + 36 > end) return ChainClientResult::fail("LTC verifyPtlcLock SPV: truncated inputs");
      p += 36;
      uint64_t scriptLen = 0;
      if (!readVar(scriptLen)) return ChainClientResult::fail("LTC verifyPtlcLock SPV: truncated");
      if (p + scriptLen > end) return ChainClientResult::fail("LTC verifyPtlcLock SPV: truncated");
      p += scriptLen;
      if (p + 4 > end) return ChainClientResult::fail("LTC verifyPtlcLock SPV: truncated");
      p += 4;
    }
    if (!readVar(voutCount)) return ChainClientResult::fail("LTC verifyPtlcLock SPV: no vouts");
    for (uint64_t i = 0; i < voutCount; ++i) {
      if (p + 8 > end) return ChainClientResult::fail("LTC verifyPtlcLock SPV: truncated output");
      uint64_t value = 0;
      for (int j = 0; j < 8; ++j) value |= static_cast<uint64_t>(p[j]) << (j * 8);
      p += 8;
      uint64_t spkLen = 0;
      if (!readVar(spkLen)) return ChainClientResult::fail("LTC verifyPtlcLock SPV: truncated");
      if (p + spkLen > end) return ChainClientResult::fail("LTC verifyPtlcLock SPV: truncated");
      if (spkLen == 34 && p[0] == 0x51 && p[1] == 0x20 &&
          std::memcmp(p + 2, xOnlyVec.data(), 32) == 0 &&
          value >= params.ctrAmount) {
        foundP2tr = true;
      }
      p += spkLen;
    }

    if (!foundP2tr)
      return ChainClientResult::fail("LTC verifyPtlcLock SPV: no P2TR output with expected amount " +
                                     std::to_string(params.ctrAmount));

    SpvTxInclusion inclusion;
    if (!m_spvClient->verifyTxInclusion(params.ctrLockTxId, inclusion))
      return ChainClientResult::fail("LTC verifyPtlcLock SPV: verifyTxInclusion failed");

    ChainClientResult result = ChainClientResult::ok(params.ctrLockTxId);
    result.confirmed = inclusion.included;
    result.spvVerified = inclusion.merkleVerified;
    result.blockHeight = inclusion.blockHeight;
    result.confirmations = inclusion.depth;
    return result;
  }

  return ChainClientResult::fail("LTC verifyPtlcLock: no RPC or SPV client available");
}

ChainClientResult LtcChainClient::verifyLock(const SwapParams& params) {
  // Pure PTLC state routes through the P2TR verifier.
  if (params.chainState.rfind("p2tr:", 0) == 0) return verifyPtlcLock(params);
  if (m_spvClient) {
    return verifyLockSpv(params);
  }

  if (!m_rpc)
    return ChainClientResult::fail("LTC verifyLock: no RPC or SPV client available");

  std::string htlcAddress;
  if (!params.chainState.empty()) {
    std::string redeemHexRaw = params.chainState;
    { auto p=redeemHexRaw.find('|'); if(p!=std::string::npos) redeemHexRaw=redeemHexRaw.substr(0,p); auto c=redeemHexRaw.find(':'); if(c!=std::string::npos) redeemHexRaw=redeemHexRaw.substr(0,c); }
    auto redeem = BtcHtlcScript::hexToBytes(redeemHexRaw);
    if (redeem.empty())
      return ChainClientResult::fail("LTC verifyLock: invalid redeem script in chainState");
    htlcAddress = BtcHtlcScript::witnessScriptToAddress(redeem, "ltc");
  } else {
    return ChainClientResult::fail(
        "LTC verifyLock: need chainState (redeem script) — "
        "cannot listunspent by txid alone");
  }

  bool ok = m_rpc->verifyLock(htlcAddress, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("LTC lock not verified at " + htlcAddress);
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult LtcChainClient::verifyLockSpv(const SwapParams& params) {
  // Fetch the raw locking tx
  std::vector<uint8_t> rawTx;
  if (!m_spvClient->getRawTx(params.ctrLockTxId, rawTx)) {
    return ChainClientResult::fail("LTC verifyLock SPV: getRawTx failed for " + params.ctrLockTxId);
  }

  // Parse the raw tx to find outputs paying to a P2WSH or P2SH address.
  // P2WSH scriptPubKey: OP_0 (0x00) PUSH32 (0x20) <32-byte-hash>  — 34 bytes
  // P2SH scriptPubKey:  OP_HASH160 (0xA9) PUSH20 (0x14) <20-byte-hash> OP_EQUAL (0x87) — 23 bytes
  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  // Skip version (4 bytes)
  if (p + 4 > end)
    return ChainClientResult::fail("LTC verifyLock SPV: raw tx too short");
  p += 4;

  // Detect SegWit (marker + flag)
  bool isSegWit = false;
  if (p + 2 <= end && *p == 0x00 && *(p + 1) == 0x01) {
    isSegWit = true;
    p += 2;
  }

  // Read vin count
  uint64_t vinCount = 0;
  if (!readVarInt(p, end, vinCount)) return ChainClientResult::fail("LTC verifyLock SPV: truncated tx");

  // Skip inputs
  for (uint64_t i = 0; i < vinCount; ++i) {
    if (p + 36 > end) return ChainClientResult::fail("LTC verifyLock SPV: truncated tx inputs");
    p += 36;

    uint64_t sigLen = 0;
    if (!readVarInt(p, end, sigLen)) return ChainClientResult::fail("LTC verifyLock SPV: truncated");
    if (p + sigLen > end) return ChainClientResult::fail("LTC verifyLock SPV: truncated");
    p += sigLen;

    if (p + 4 > end) return ChainClientResult::fail("LTC verifyLock SPV: truncated sequence");
    p += 4;
  }

  // Read vout count
  uint64_t voutCount = 0;
  if (!readVarInt(p, end, voutCount)) return ChainClientResult::fail("LTC verifyLock SPV: no vouts");

  bool foundOutput = false;
  for (uint64_t i = 0; i < voutCount; ++i) {
    if (p + 8 > end) return ChainClientResult::fail("LTC verifyLock SPV: truncated output value");
    uint64_t value = 0;
    for (int j = 0; j < 8; ++j) {
      value |= static_cast<uint64_t>(p[j]) << (j * 8);
    }
    p += 8;

    uint64_t spkLen = 0;
    if (!readVarInt(p, end, spkLen)) return ChainClientResult::fail("LTC verifyLock SPV: truncated scriptPubKey length");
    if (p + spkLen > end) return ChainClientResult::fail("LTC verifyLock SPV: truncated scriptPubKey");

    // Check for P2WSH output (34 bytes): OP_0 PUSH32 <32-byte-hash>
    // Require chainState redeem script so we bind to the negotiated HTLC.
    if (spkLen == 34 && p[0] == 0x00 && p[1] == 0x20 && value >= params.ctrAmount) {
      if (!params.chainState.empty()) {
        auto redeemScript = LtcHtlcScript::hexToBytes(params.chainState);
        auto expectedHash = LtcHtlcScript::sha256(redeemScript);
        if (expectedHash.size() == 32 && std::memcmp(p + 2, expectedHash.data(), 32) == 0) {
          foundOutput = true;
        }
      }
      // Fail closed without chainState — never accept any P2WSH of matching amount.
    }

    p += spkLen;
  }

  if (!foundOutput) {
    return ChainClientResult::fail("LTC verifyLock SPV: no matching P2WSH HTLC output (need chainState redeem script)");
  }

  // Verify inclusion via SPV
  SpvTxInclusion inclusion;
  if (!m_spvClient->verifyTxInclusion(params.ctrLockTxId, inclusion)) {
    return ChainClientResult::fail("LTC verifyLock SPV: verifyTxInclusion failed");
  }

  ChainClientResult result = ChainClientResult::ok(params.ctrLockTxId);
  result.confirmed = inclusion.included;
  result.spvVerified = inclusion.merkleVerified;
  result.blockHeight = inclusion.blockHeight;
  result.confirmations = inclusion.depth;
  return result;
}

ChainClientResult LtcChainClient::claim(const SwapParams& params) {
  // Pure PTLC: key-path Schnorr spend of the P2TR output.
  if (params.lockType == SwapLockType::PTLC && params.chainState.rfind("p2tr:", 0) == 0)
    return claimOrRefundPtlcP2tr(params);

  if (!m_rpc) {
    if (m_wif.empty())
      return ChainClientResult::fail("LTC claim: no RPC or WIF available (SPV mode needs WIF for local signing)");

    std::array<uint8_t, 32> privKey{};
    if (!LtcHtlcScript::wifToPrivKey(m_wif, privKey))
      return ChainClientResult::fail("LTC claim: invalid WIF");

    std::string redeemHexRawC = params.chainState;
    { auto p=redeemHexRawC.find('|'); if(p!=std::string::npos) redeemHexRawC=redeemHexRawC.substr(0,p); auto c=redeemHexRawC.find(':'); if(c!=std::string::npos) redeemHexRawC=redeemHexRawC.substr(0,c); }
    auto witnessScript = LtcHtlcScript::hexToBytes(redeemHexRawC);
    auto preimageBytes = LtcHtlcScript::hexToBytes(Common::podToHex(params.adaptorSecret));

    uint8_t addrVersion = 0;
    std::vector<uint8_t> pubKeyHash;
    if (!LtcHtlcScript::base58CheckDecode(params.ctrAddress, addrVersion, pubKeyHash))
      return ChainClientResult::fail("LTC claim: invalid destination address");
    auto outputScript = LtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

    uint64_t fee = 1000;
    if (params.ctrAmount <= fee)
      return ChainClientResult::fail("LTC claim: amount too small for fee");
    uint64_t outputAmount = params.ctrAmount - fee;

    const uint32_t nSequence = 0xFFFFFFFD;

    auto der = LtcHtlcScript::signInput(privKey, 2, 0, nSequence,
        params.ctrLockTxId, 0, witnessScript, params.ctrAmount,
        outputScript, outputAmount);
    if (der.empty())
      return ChainClientResult::fail("LTC claim SPV: signing failed");

    std::vector<uint8_t> emptyScriptSig;
    auto witnessStack = LtcHtlcScript::createClaimWitness(der, preimageBytes, witnessScript);

    auto rawTx = LtcHtlcScript::buildRawSegWitTx(
        params.ctrLockTxId, 0, params.ctrAmount,
        emptyScriptSig, witnessStack, params.ctrAddress, outputAmount, 0);

    std::string txid;
    if (!m_spvClient->broadcastTx(rawTx, txid))
      return ChainClientResult::fail("LTC claim SPV: broadcast failed");
    return ChainClientResult::ok(txid);
  }

  std::string redeemHexRawC2 = params.chainState;
  { auto p=redeemHexRawC2.find('|'); if(p!=std::string::npos) redeemHexRawC2=redeemHexRawC2.substr(0,p); auto c=redeemHexRawC2.find(':'); if(c!=std::string::npos) redeemHexRawC2=redeemHexRawC2.substr(0,c); }
  std::string claimTxId;
  bool ok = m_rpc->claim(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      redeemHexRawC2,
      Common::podToHex(params.adaptorSecret),
      params.ctrAddress,
      claimTxId);
  if (!ok) return ChainClientResult::fail("LTC claim failed");
  return ChainClientResult::ok(claimTxId);
}

ChainClientResult LtcChainClient::refund(const SwapParams& params) {
  // Pure PTLC: the locker's internal key spends the P2TR via key path at any
  // height — timeout-independent for this construction.
  if (params.lockType == SwapLockType::PTLC && params.chainState.rfind("p2tr:", 0) == 0)
    return claimOrRefundPtlcP2tr(params);

  if (!m_rpc) {
    if (m_wif.empty())
      return ChainClientResult::fail("LTC refund: no RPC or WIF available (SPV mode needs WIF for local signing)");

    std::array<uint8_t, 32> privKey{};
    if (!LtcHtlcScript::wifToPrivKey(m_wif, privKey))
      return ChainClientResult::fail("LTC refund: invalid WIF");

    std::string redeemHexRawL0 = params.chainState;
    { auto p=redeemHexRawL0.find('|'); if(p!=std::string::npos) redeemHexRawL0=redeemHexRawL0.substr(0,p); auto c=redeemHexRawL0.find(':'); if(c!=std::string::npos) redeemHexRawL0=redeemHexRawL0.substr(0,c); }
    auto witnessScript = LtcHtlcScript::hexToBytes(redeemHexRawL0);

    uint32_t nLocktime = static_cast<uint32_t>(params.ctrTimeoutBlock);

    uint8_t addrVersion = 0;
    std::vector<uint8_t> pubKeyHash;
    if (!LtcHtlcScript::base58CheckDecode(params.ctrAddress, addrVersion, pubKeyHash))
      return ChainClientResult::fail("LTC refund: invalid destination address");
    auto outputScript = LtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

    uint64_t fee = 1000;
    if (params.ctrAmount <= fee)
      return ChainClientResult::fail("LTC refund: amount too small for fee");
    uint64_t outputAmount = params.ctrAmount - fee;

    auto der = LtcHtlcScript::signInput(privKey, 2, nLocktime,
        0xFFFFFFFE,
        params.ctrLockTxId, 0, witnessScript, params.ctrAmount,
        outputScript, outputAmount);
    if (der.empty())
      return ChainClientResult::fail("LTC refund SPV: signing failed");

    std::vector<uint8_t> emptyScriptSig;
    auto witnessStack = LtcHtlcScript::createRefundWitness(der, witnessScript);

    auto rawTx = LtcHtlcScript::buildRawSegWitTx(
        params.ctrLockTxId, 0, params.ctrAmount,
        emptyScriptSig, witnessStack, params.ctrAddress, outputAmount, nLocktime);

    std::string txid;
    if (!m_spvClient->broadcastTx(rawTx, txid))
      return ChainClientResult::fail("LTC refund SPV: broadcast failed");
    return ChainClientResult::ok(txid);
  }

  std::string redeemHexRawL1 = params.chainState;
  { auto p=redeemHexRawL1.find('|'); if(p!=std::string::npos) redeemHexRawL1=redeemHexRawL1.substr(0,p); auto c=redeemHexRawL1.find(':'); if(c!=std::string::npos) redeemHexRawL1=redeemHexRawL1.substr(0,c); }
  std::string refundTxId;
  bool ok = m_rpc->refundHtlc(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      redeemHexRawL1,
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAddress,
      refundTxId);
  if (!ok) return ChainClientResult::fail("LTC refundHtlc failed");
  return ChainClientResult::ok(refundTxId);
}

ChainClientResult LtcChainClient::verifyReserveProof(const std::string& expectedMessage,
                                                     uint64_t minAmount,
                                                     const std::string& proof) {
  if (!m_rpc)
    return ChainClientResult::fail("LTC verifyReserveProof: RPC client not available");

  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("LTC reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string signature = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  bool sigValid = false;
  if (!m_rpc->verifyMessage(address, signature, message, sigValid))
    return ChainClientResult::fail("LTC reserve proof: verifymessage RPC failed");
  if (!sigValid)
    return ChainClientResult::fail("LTC reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("LTC reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("LTC reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("LTC reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool LtcChainClient::getCurrentHeight(uint64_t& height) {
  if (m_spvClient) {
    return m_spvClient->getTipHeight(height);
  }
  if (m_rpc) {
    return m_rpc->getBlockCount(height);
  }
  return false;
}

ChainClientResult LtcChainClient::getTransactionDetails(const std::string& txId,
                                                        ChainClientResult& result) {
  if (m_spvClient) {
    uint64_t tipHeight = 0;
    if (!m_spvClient->getTipHeight(tipHeight)) {
      result = ChainClientResult::fail("LTC SPV: cannot get tip height");
      return result;
    }

    SpvTxInclusion inclusion;
    if (!m_spvClient->verifyTxInclusion(txId, inclusion)) {
      result = ChainClientResult::fail("LTC SPV: tx not found or not yet included in a block");
      result.confirmed = false;
      result.confirmations = 0;
      return result;
    }

    result.success = true;
    result.confirmed = true;
    result.spvVerified = true;
    result.blockHeight = inclusion.blockHeight;
    result.confirmations = (tipHeight >= inclusion.blockHeight)
        ? (tipHeight - inclusion.blockHeight + 1) : 1;
    return result;
  }

  if (m_rpc) {
    LtcTxInfo txInfo;
    if (!m_rpc->getTransaction(txId, txInfo)) {
      result = ChainClientResult::fail("LTC RPC: gettransaction failed for " + txId);
      return result;
    }

    uint64_t tipHeight = 0;
    m_rpc->getBlockCount(tipHeight);

    result.success = true;
    result.confirmed = txInfo.confirmations > 0;
    result.spvVerified = false;
    result.blockHeight = txInfo.blockHeight;
    result.confirmations = txInfo.confirmations;
    return result;
  }

  result = ChainClientResult::fail("LTC: no RPC or SPV client available");
  return result;
}

std::string LtcChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  std::vector<uint8_t> redeemScript = LtcHtlcScript::hexToBytes(htlcRedeemScriptHex);
  std::vector<uint8_t> p2wshScriptPubKey = LtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);

  if (m_spvClient) {
    return extractSecretSpv(spendingTxid, p2wshScriptPubKey);
  }

  if (!m_rpc) {
    return {};
  }

  std::string rawTxHex;
  if (!m_rpc->getRawTransaction(spendingTxid, rawTxHex)) {
    return {};
  }

  std::vector<uint8_t> rawTx = LtcHtlcScript::hexToBytes(rawTxHex);
  std::vector<uint8_t> preimage = LtcHtlcScript::parseClaimPreimage(rawTx, p2wshScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return LtcHtlcScript::bytesToHex(preimage);
}

std::string LtcChainClient::extractSecretSpv(const std::string& spendingTxid,
                                              const std::vector<uint8_t>& htlcP2wshScriptPubKey) {
  std::vector<uint8_t> rawSpendingTx;
  if (!m_spvClient->getRawTx(spendingTxid, rawSpendingTx)) {
    return {};
  }

  std::vector<uint8_t> preimage = LtcHtlcScript::parseClaimPreimage(rawSpendingTx, htlcP2wshScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return LtcHtlcScript::bytesToHex(preimage);
}

std::string LtcChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  // ── Pure PTLC (P2.3): t = s' − s via BtcTaprootPtlc::parseClaimSecret ──
  if (params.chainState.rfind("p2tr:", 0) == 0) {
    std::vector<uint8_t> tweaked33;
    std::array<uint8_t, 32> ptlcX{};
    Crypto::SecpAdaptorPresig presig;
    bool havePresig = false;
    if (!parsePtlcP2trStateLtc(params.chainState, tweaked33, ptlcX, presig, havePresig))
      return {};
    if (!havePresig)
      return {};  // no stored adaptor presig → s' unknown, t is not recoverable

    std::vector<uint8_t> rawTx;
    if (m_spvClient) {
      for (uint32_t vout = 0; vout < 4 && rawTx.empty(); ++vout) {
        SpvSpend spend;
        if (!m_spvClient->findSpend(params.ctrLockTxId, vout, spend) || !spend.spent)
          continue;
        if (spend.spendingTxid.empty()) continue;
        m_spvClient->getRawTx(spend.spendingTxid, rawTx);
      }
    } else if (m_rpc && !params.ctrClaimTxId.empty()) {
      std::string rawHex;
      if (m_rpc->getRawTransaction(params.ctrClaimTxId, rawHex) && !rawHex.empty())
        rawTx = LtcHtlcScript::hexToBytes(rawHex);
    }
    if (rawTx.empty()) return {};

    Crypto::SecretKey t{};
    if (!BtcTaprootPtlc::parseClaimSecret(rawTx, tweaked33, presig, t))
      return {};
    if (isZeroSecretLtc(t)) return {};
    return Common::podToHex(t);
  }

  if (params.chainState.empty() || params.ctrLockTxId.empty()) return {};

  std::string redeemHex = params.chainState;
  std::string knownClaimTxid;
  auto colon = params.chainState.find(':');
  if (colon != std::string::npos && colon + 1 < params.chainState.size()) {
    std::string left = params.chainState.substr(0, colon);
    std::string right = params.chainState.substr(colon + 1);
    if (!left.empty() && (left.size() % 2) == 0 && right.size() == 64) {
      redeemHex = left;
      knownClaimTxid = right;
    }
  }

  if (m_spvClient) {
    for (uint32_t vout = 0; vout < 4; ++vout) {
      SpvSpend spend;
      if (!m_spvClient->findSpend(params.ctrLockTxId, vout, spend) || !spend.spent)
        continue;
      if (spend.spendingTxid.empty()) continue;
      std::string secret = extractSecret(spend.spendingTxid, redeemHex);
      if (!secret.empty()) return secret;
    }
  }

  if (m_rpc && !knownClaimTxid.empty()) {
    std::string secret = extractSecret(knownClaimTxid, redeemHex);
    if (!secret.empty()) return secret;
  }

  return {};
}

} // namespace XfgSwap
