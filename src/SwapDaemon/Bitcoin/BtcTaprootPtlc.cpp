// Copyright (c) 2017-2026 Fuego Developers
#include "BtcTaprootPtlc.h"
#include "BtcPtlcScript.h" // for reference script structure (optional)
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <cstring>
#include <stdexcept>

namespace XfgSwap {

static const char* SECP_ORDER_HEX = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141";

static BIGNUM* get_order(BN_CTX*) {
  BIGNUM* n = BN_new();
  BN_hex2bn(&n, SECP_ORDER_HEX);
  return n;
}
static EC_GROUP* secp_group() {
  static EC_GROUP* g = nullptr;
  if (!g) {
    g = EC_GROUP_new_by_curve_name(NID_secp256k1);
    EC_GROUP_set_asn1_flag(g, OPENSSL_EC_NAMED_CURVE);
  }
  return g;
}
static std::array<uint8_t,32> bn_to_bytes(const BIGNUM* bn) {
  std::array<uint8_t,32> out{}; out.fill(0);
  int len = BN_num_bytes(bn);
  std::vector<uint8_t> tmp(len);
  BN_bn2bin(bn, tmp.data());
  if (len <= 32) std::memcpy(out.data()+32-len, tmp.data(), len);
  else std::memcpy(out.data(), tmp.data()+len-32, 32);
  return out;
}
static BIGNUM* bytes_to_bn(const std::array<uint8_t,32>& b){ return BN_bin2bn(b.data(),32,nullptr); }
static BIGNUM* bytes_to_bn_vec(const std::vector<uint8_t>& v){ return BN_bin2bn(v.data(), (int)v.size(), nullptr); }

// ── hex ──
std::vector<uint8_t> BtcTaprootPtlc::hexToBytes(const std::string& hex){
  if (hex.size()%2) throw std::runtime_error("hex odd");
  std::vector<uint8_t> out; out.reserve(hex.size()/2);
  for(size_t i=0;i<hex.size();i+=2){
    uint8_t hi,lo;
    char c=hex[i];
    if(c>='0'&&c<='9') hi=c-'0'; else if(c>='a'&&c<='f') hi=c-'a'+10; else if(c>='A'&&c<='F') hi=c-'A'+10; else throw std::runtime_error("hex");
    c=hex[i+1];
    if(c>='0'&&c<='9') lo=c-'0'; else if(c>='a'&&c<='f') lo=c-'a'+10; else if(c>='A'&&c<='F') lo=c-'A'+10; else throw std::runtime_error("hex");
    out.push_back((hi<<4)|lo);
  }
  return out;
}
std::string BtcTaprootPtlc::bytesToHex(const std::vector<uint8_t>& b){
  static const char* H="0123456789abcdef";
  std::string s; s.reserve(b.size()*2);
  for(auto v:b){ s.push_back(H[v>>4]); s.push_back(H[v&0xF]);}
  return s;
}
std::vector<uint8_t> BtcTaprootPtlc::sha256(const std::vector<uint8_t>& data){
  std::vector<uint8_t> d(SHA256_DIGEST_LENGTH);
  SHA256(data.data(), data.size(), d.data());
  return d;
}

// ── TaggedHash ────────────────────────────────────────────────────────────────
// TaggedHash(tag, msg) = SHA256( SHA256(tag) || SHA256(tag) || msg )
// Same EVP pattern as Crypto::secp_adaptor_challenge (secp_adaptor.cpp:70)
std::array<uint8_t,32> BtcTaprootPtlc::taggedHash(const std::string& tag, const std::vector<uint8_t>& data){
  return taggedHash(tag, data.data(), data.size());
}
std::array<uint8_t,32> BtcTaprootPtlc::taggedHash(const std::string& tag, const uint8_t* d, size_t len){
  unsigned char tag_hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(tag.data()), tag.size(), tag_hash);
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
  EVP_DigestUpdate(ctx, tag_hash, 32);
  EVP_DigestUpdate(ctx, tag_hash, 32);
  if (len) EVP_DigestUpdate(ctx, d, len);
  unsigned char out[SHA256_DIGEST_LENGTH];
  unsigned int outlen=0;
  EVP_DigestFinal_ex(ctx, out, &outlen);
  EVP_MD_CTX_free(ctx);
  std::array<uint8_t,32> r{}; std::memcpy(r.data(), out, 32);
  return r;
}

// ── computeTapTweak ─────────────────────────────────────────────────────────
std::array<uint8_t,32> BtcTaprootPtlc::computeTapTweak(
    const std::array<uint8_t,32>& internalPubX,
    const std::vector<uint8_t>& m_swap){
  std::vector<uint8_t> data;
  data.reserve(32 + m_swap.size());
  data.insert(data.end(), internalPubX.begin(), internalPubX.end());
  data.insert(data.end(), m_swap.begin(), m_swap.end());
  return taggedHash("TapTweak", data);
}

std::array<uint8_t,32> BtcTaprootPtlc::computeTapTweak(
    const std::vector<uint8_t>& internalKey33,
    const std::vector<uint8_t>& ptlcPointX32,
    uint32_t timeoutBlocks,
    const std::vector<uint8_t>& recipientPub33,
    const std::vector<uint8_t>& senderPub33){
  if (internalKey33.size()!=33) throw std::runtime_error("internalKey must be 33");
  std::array<uint8_t,32> internalX{};
  std::memcpy(internalX.data(), internalKey33.data()+1, 32);
  std::vector<uint8_t> m_swap;
  m_swap.reserve(32+4+33+33);
  if (ptlcPointX32.size()!=32) throw std::runtime_error("ptlcPointX32 32");
  m_swap.insert(m_swap.end(), ptlcPointX32.begin(), ptlcPointX32.end());
  uint32_t t=timeoutBlocks;
  m_swap.push_back(t & 0xFF); m_swap.push_back((t>>8)&0xFF); m_swap.push_back((t>>16)&0xFF); m_swap.push_back((t>>24)&0xFF);
  if (recipientPub33.size()!=33 || senderPub33.size()!=33) throw std::runtime_error("recipient/sender 33");
  m_swap.insert(m_swap.end(), recipientPub33.begin(), recipientPub33.end());
  m_swap.insert(m_swap.end(), senderPub33.begin(), senderPub33.end());
  return computeTapTweak(internalX, m_swap);
}

// ── bech32m ─────────────────────────────────────────────────────────────────
static const char* BECH32_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
static const uint32_t BECH32_GEN[5] = {0x3b6a57b2,0x26508e6d,0x1ea119fa,0x3d4233dd,0x2a1462b3};

uint32_t BtcTaprootPtlc::bech32Polymod(const std::vector<uint8_t>& values){
  uint32_t chk=1;
  for(uint8_t v: values){
    uint32_t b = chk>>25;
    chk = ((chk & 0x1ffffff)<<5) ^ v;
    for(int i=0;i<5;++i) if((b>>i)&1) chk ^= BECH32_GEN[i];
  }
  return chk;
}
std::vector<uint8_t> BtcTaprootPtlc::hrpExpand(const std::string& hrp){
  std::vector<uint8_t> ret;
  ret.reserve(hrp.size()*2+1);
  for(char c: hrp) ret.push_back((c>>5)&0x07);
  ret.push_back(0);
  for(char c: hrp) ret.push_back(c & 0x1f);
  return ret;
}
std::vector<uint8_t> BtcTaprootPtlc::convertBits(const std::vector<uint8_t>& data, int fromBits, int toBits, bool pad){
  uint32_t acc=0; int bits=0; const uint32_t maxv = (1u<<toBits)-1;
  std::vector<uint8_t> ret;
  for(uint8_t v: data){
    acc = (acc<<fromBits) | v;
    bits += fromBits;
    while(bits>=toBits){
      bits-=toBits;
      ret.push_back((acc>>bits)&maxv);
    }
  }
  if(pad && bits) ret.push_back((acc<<(toBits-bits))&maxv);
  return ret;
}
std::string BtcTaprootPtlc::bech32mEncode(const std::string& hrp, const std::vector<uint8_t>& data5){
  auto hrpExp = hrpExpand(hrp);
  std::vector<uint8_t> combined = hrpExp;
  combined.insert(combined.end(), data5.begin(), data5.end());
  combined.insert(combined.end(), 6, 0);
  uint32_t polymod = bech32Polymod(combined) ^ 0x2bc830a3; // bech32m constant BIP350
  std::string ret = hrp + "1";
  for(uint8_t v: data5) ret += BECH32_CHARSET[v];
  for(int i=0;i<6;++i) ret += BECH32_CHARSET[(polymod >> (5*(5-i))) & 31];
  return ret;
}

std::string BtcTaprootPtlc::xOnlyToP2trAddress(const std::array<uint8_t,32>& xOnly, const std::string& hrp){
  // data5 = [1] + convertBits(xOnly 8->5 pad)
  std::vector<uint8_t> prog(xOnly.begin(), xOnly.end());
  auto prog5 = convertBits(prog, 8, 5, true);
  std::vector<uint8_t> data5;
  data5.reserve(1+prog5.size());
  data5.push_back(1); // witness version 1
  data5.insert(data5.end(), prog5.begin(), prog5.end());
  return bech32mEncode(hrp, data5);
}
std::string BtcTaprootPtlc::tweakedPubToP2trAddress(const std::vector<uint8_t>& tweakedPub33, const std::string& hrp){
  if(tweakedPub33.size()!=33) throw std::runtime_error("tweakedPub33 33");
  std::array<uint8_t,32> xOnly{};
  std::memcpy(xOnly.data(), tweakedPub33.data()+1, 32);
  return xOnlyToP2trAddress(xOnly, hrp);
}

// ── script helpers ──────────────────────────────────────────────────────────
void BtcTaprootPtlc::pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data){
  size_t len=data.size();
  if(len < 0x4c) script.push_back(static_cast<uint8_t>(len));
  else if(len <= 0xff){ script.push_back(0x4c); script.push_back(static_cast<uint8_t>(len)); }
  else if(len <= 0xffff){ script.push_back(0x4d); script.push_back(len&0xff); script.push_back((len>>8)&0xff); }
  else { script.push_back(0x4e); writeLE32(script, static_cast<uint32_t>(len)); }
  script.insert(script.end(), data.begin(), data.end());
}
void BtcTaprootPtlc::writeLE32(std::vector<uint8_t>& out, uint32_t v){
  out.push_back(v&0xff); out.push_back((v>>8)&0xff); out.push_back((v>>16)&0xff); out.push_back((v>>24)&0xff);
}

std::vector<uint8_t> BtcTaprootPtlc::createPtlcTapLeaf(
    const std::vector<uint8_t>& ptlcPointX32,
    uint32_t timeoutBlocks,
    const std::vector<uint8_t>& recipientPub33,
    const std::vector<uint8_t>& senderPub33){
  if(ptlcPointX32.size()!=32) throw std::runtime_error("ptlcPointX32 32");
  if(recipientPub33.size()!=33 || senderPub33.size()!=33) throw std::runtime_error("pub 33");
  std::vector<uint8_t> script;
  script.reserve(100);
  // Same structure as BtcPtlcScript P2WSH for audit parity, but as tap leaf.
  // OP_IF <ptlc> DROP <recipient> CHECKSIG OP_ELSE <timeout> CLTV DROP <sender> CHECKSIG OP_ENDIF
  script.push_back(0x63); // OP_IF
  pushData(script, ptlcPointX32);
  script.push_back(0x75); // OP_DROP
  pushData(script, recipientPub33);
  script.push_back(0xac); // OP_CHECKSIG
  script.push_back(0x67); // OP_ELSE
  // minimal CScriptNum for timeout
  std::vector<uint8_t> timeoutBytes;
  uint32_t t=timeoutBlocks;
  while(t){ timeoutBytes.push_back(t & 0xff); t>>=8; }
  if(timeoutBytes.empty()) timeoutBytes.push_back(0);
  if(timeoutBytes.back() & 0x80) timeoutBytes.push_back(0x00);
  pushData(script, timeoutBytes);
  script.push_back(0xb1); // OP_CLTV
  script.push_back(0x75); // OP_DROP
  pushData(script, senderPub33);
  script.push_back(0xac); // OP_CHECKSIG
  script.push_back(0x68); // OP_ENDIF
  return script;
}

// ── main builder ────────────────────────────────────────────────────────────
TaprootPtlcOutput BtcTaprootPtlc::createTaprootPtlcOutput(
    const std::vector<uint8_t>& internalKey33,
    const std::vector<uint8_t>& ptlcPointX32,
    uint32_t timeoutBlocks,
    const std::vector<uint8_t>& recipientPub33,
    const std::vector<uint8_t>& senderPub33,
    const std::string& hrp){
  if(internalKey33.size()!=33) throw std::runtime_error("internalKey must be 33 compressed");
  if(internalKey33[0]!=0x02 && internalKey33[0]!=0x03) throw std::runtime_error("internalKey bad prefix");
  if(ptlcPointX32.size()!=32) throw std::runtime_error("ptlcPointX32 must be 32 x-only");
  if(recipientPub33.size()!=33 || senderPub33.size()!=33) throw std::runtime_error("recipient/sender must be 33");
  if(recipientPub33[0]!=0x02 && recipientPub33[0]!=0x03) throw std::runtime_error("recipient bad prefix");
  if(senderPub33[0]!=0x02 && senderPub33[0]!=0x03) throw std::runtime_error("sender bad prefix");

  // Build m_swap = T(32) || timeout LE32 || recipient(33) || sender(33)
  std::vector<uint8_t> m_swap;
  m_swap.reserve(102);
  m_swap.insert(m_swap.end(), ptlcPointX32.begin(), ptlcPointX32.end());
  m_swap.push_back(timeoutBlocks & 0xff);
  m_swap.push_back((timeoutBlocks>>8)&0xff);
  m_swap.push_back((timeoutBlocks>>16)&0xff);
  m_swap.push_back((timeoutBlocks>>24)&0xff);
  m_swap.insert(m_swap.end(), recipientPub33.begin(), recipientPub33.end());
  m_swap.insert(m_swap.end(), senderPub33.begin(), senderPub33.end());

  std::array<uint8_t,32> internalX{};
  std::memcpy(internalX.data(), internalKey33.data()+1, 32);
  auto tweak = computeTapTweak(internalX, m_swap);

  EC_GROUP* grp = secp_group();
  BN_CTX* ctx = BN_CTX_new();
  BIGNUM* tweak_bn = BN_bin2bn(tweak.data(), 32, nullptr);
  BIGNUM* order = get_order(ctx);
  TaprootPtlcOutput out;
  out.tapTweak = tweak;

  bool ok=false;
  EC_POINT *P_pt = EC_POINT_new(grp), *tweak_pt = EC_POINT_new(grp), *Q_pt = EC_POINT_new(grp);
  do{
    if(BN_cmp(tweak_bn, order) >= 0){
      // BIP341: tweak interpreted mod n; reduce for EC math
      BIGNUM* tmp = BN_new();
      BN_nnmod(tmp, tweak_bn, order, ctx);
      BN_free(tweak_bn);
      tweak_bn = tmp;
      if(BN_is_zero(tweak_bn)) break; // zero tweak => Q==P
    }
    if(!EC_POINT_oct2point(grp, P_pt, internalKey33.data(), 33, ctx)) break;
    // BIP341: internal key with odd y is negated to even. Normalize P to even y.
    {
      BIGNUM *x=BN_new(), *y=BN_new();
      if(EC_POINT_get_affine_coordinates(grp, P_pt, x, y, ctx)){
        if(BN_is_odd(y)){
          EC_POINT_invert(grp, P_pt, ctx);
        }
      }
      BN_free(x); BN_free(y);
    }
    // tweak*G
    if(!EC_POINT_mul(grp, tweak_pt, tweak_bn, nullptr, nullptr, ctx)) break;
    // Q = P + tweak*G
    if(!EC_POINT_add(grp, Q_pt, P_pt, tweak_pt, ctx)) break;
    if(EC_POINT_is_at_infinity(grp, Q_pt)) break;
    // serialize Q compressed 33
    std::vector<uint8_t> tweaked33(33);
    size_t l = EC_POINT_point2oct(grp, Q_pt, POINT_CONVERSION_COMPRESSED, tweaked33.data(), 33, ctx);
    if(l!=33) break;
    out.tweakedPubKey = tweaked33;
    BIGNUM *qx=BN_new(), *qy=BN_new();
    if(EC_POINT_get_affine_coordinates(grp, Q_pt, qx, qy, ctx)){
      auto qx_b = bn_to_bytes(qx);
      out.tweakedPubKeyXOnly = qx_b;
    }
    BN_free(qx); BN_free(qy);
    // address
    out.p2trAddress = xOnlyToP2trAddress(out.tweakedPubKeyXOnly, hrp);
    // control block for single-leaf script path (0xc0|parity || internalX)
    out.controlBlock = createControlBlock(internalKey33, tweaked33);
    // redeemScript / leaf for script-path refund verification
    out.redeemScript = createPtlcTapLeaf(ptlcPointX32, timeoutBlocks, recipientPub33, senderPub33);
    ok=true;
  }while(false);

  EC_POINT_free(P_pt); EC_POINT_free(tweak_pt); EC_POINT_free(Q_pt);
  BN_free(tweak_bn); BN_free(order); BN_CTX_free(ctx);
  if(!ok) throw std::runtime_error("BtcTaprootPtlc: EC tweak failed (invalid key or tweak)");
  return out;
}

// P2.1 canonical builder — delegates to createTaprootPtlcOutput (which now
// populates tweakedPub/tapTweak/controlBlock/p2trAddress).
TaprootPtlcOutput BtcTaprootPtlc::createTaprootPtlc(
    const std::vector<uint8_t>& internalKey33,
    const std::vector<uint8_t>& ptlcPointX32,
    uint32_t timeoutBlock,
    const std::vector<uint8_t>& recipientPub33,
    const std::vector<uint8_t>& senderPub33,
    const std::string& hrp){
  return createTaprootPtlcOutput(internalKey33, ptlcPointX32, timeoutBlock,
                                 recipientPub33, senderPub33, hrp);
}

// ── adaptor -> Schnorr ───────────────────────────────────────────────────────
bool BtcTaprootPtlc::adaptorToSchnorrSig(
    const Crypto::SecpAdaptorPresig& presig,
    const Crypto::SecretKey& t,
    Crypto::SecpSchnorrSig& outSig){
  // s = s' - t mod n, R_x from presig.R
  EC_GROUP* grp = secp_group();
  BN_CTX* ctx = BN_CTX_new();
  // R_x 32
  EC_POINT* R_pt = EC_POINT_new(grp);
  if(!EC_POINT_oct2point(grp, R_pt, presig.R.data.data(), 33, ctx)){ EC_POINT_free(R_pt); BN_CTX_free(ctx); return false; }
  BIGNUM *rx=BN_new(), *ry=BN_new();
  bool ok=false;
  if(EC_POINT_get_affine_coordinates(grp, R_pt, rx, ry, ctx)){
    auto rx_b = bn_to_bytes(rx);
    std::memcpy(outSig.data.data(), rx_b.data(), 32);
    // s = s' - t
    std::array<uint8_t,32> s_prime = presig.s_prime;
    std::array<uint8_t,32> t_bytes{}; std::memcpy(t_bytes.data(), &t, 32);
    std::array<uint8_t,32> neg_t = Crypto::secp_scalar_neg(t_bytes);
    auto s = Crypto::secp_scalar_add(s_prime, neg_t);
    std::memcpy(outSig.data.data()+32, s.data(), 32);
    ok=true;
  }
  BN_free(rx); BN_free(ry); EC_POINT_free(R_pt); BN_CTX_free(ctx);
  return ok;
}

std::vector<std::vector<uint8_t>> BtcTaprootPtlc::createKeyPathWitness(
    const Crypto::SecpSchnorrSig& sig){
  // Single 64-byte schnorr sig (SIGHASH_DEFAULT). Caller may append 0x01 for SIGHASH_ALL.
  std::vector<std::vector<uint8_t>> w;
  w.reserve(1);
  std::vector<uint8_t> sig64(sig.data.begin(), sig.data.end());
  w.push_back(sig64);
  return w;
}

// P2.1: key-path claim witness from a raw adapted Schnorr sig.
std::vector<std::vector<uint8_t>> BtcTaprootPtlc::createKeyPathClaimWitness(
    const std::vector<uint8_t>& adaptedSig64){
  if(adaptedSig64.size() != 64 && adaptedSig64.size() != 65)
    throw std::runtime_error("adapted Schnorr sig must be 64 bytes (65 with sighash byte)");
  std::vector<std::vector<uint8_t>> w;
  w.reserve(1);
  w.push_back(adaptedSig64);
  return w;
}

// ── SegWit tx witness parsing ────────────────────────────────────────────────
namespace {
bool readVarIntTap(const uint8_t*& p, const uint8_t* end, uint64_t& out){
  if(p >= end) return false;
  uint8_t first = *p++;
  if(first < 0xFD){ out = first; return true; }
  if(first == 0xFD){
    if(p + 2 > end) return false;
    out = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
    p += 2; return true;
  }
  if(first == 0xFE){
    if(p + 4 > end) return false;
    out = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
          (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
    p += 4; return true;
  }
  if(p + 8 > end) return false;
  out = 0;
  for(int i = 0; i < 8; ++i) out |= static_cast<uint64_t>(p[i]) << (i * 8);
  p += 8; return true;
}
} // namespace

bool BtcTaprootPtlc::parseSegWitWitnesses(
    const std::vector<uint8_t>& rawTx,
    std::vector<std::vector<std::vector<uint8_t>>>& witnesses){
  witnesses.clear();
  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();
  if(rawTx.size() < 4 + 2) return false;
  p += 4; // version
  if(!(p[0] == 0x00 && p[1] == 0x01)) return false; // SegWit marker+flag required
  p += 2;
  uint64_t vinCount = 0;
  if(!readVarIntTap(p, end, vinCount)) return false;
  for(uint64_t i = 0; i < vinCount; ++i){
    if(p + 36 > end) return false;
    p += 36; // prevout txid + vout
    uint64_t scriptLen = 0;
    if(!readVarIntTap(p, end, scriptLen)) return false;
    if(p + scriptLen > end) return false;
    p += scriptLen;
    if(p + 4 > end) return false;
    p += 4; // sequence
  }
  uint64_t voutCount = 0;
  if(!readVarIntTap(p, end, voutCount)) return false;
  for(uint64_t i = 0; i < voutCount; ++i){
    if(p + 8 > end) return false;
    p += 8; // value
    uint64_t spkLen = 0;
    if(!readVarIntTap(p, end, spkLen)) return false;
    if(p + spkLen > end) return false;
    p += spkLen;
  }
  witnesses.resize(static_cast<size_t>(vinCount));
  for(uint64_t i = 0; i < vinCount; ++i){
    uint64_t itemCount = 0;
    if(!readVarIntTap(p, end, itemCount)) return false;
    auto& stack = witnesses[static_cast<size_t>(i)];
    stack.reserve(static_cast<size_t>(itemCount));
    for(uint64_t j = 0; j < itemCount; ++j){
      uint64_t itemLen = 0;
      if(!readVarIntTap(p, end, itemLen)) return false;
      if(p + itemLen > end) return false;
      stack.emplace_back(p, p + itemLen);
      p += itemLen;
    }
  }
  return true;
}

// Extract the claim sig: prefer a taproot script-path stack (sig-shaped front item),
// else fall back to a single-item 64/65-byte stack (key-path spend).
// Note: a P2TR output key never appears inside a spending witness, so ownership of a
// bare key-path candidate cannot be proven here — parseClaimSecret() binds the sig to
// our lock via the stored presig R_x.
std::vector<uint8_t> BtcTaprootPtlc::extractClaimSchnorrSig(
    const std::vector<uint8_t>& rawTx,
    const std::vector<uint8_t>& tweakedPub33){
  if(tweakedPub33.size() != 33) throw std::runtime_error("tweakedPub must be 33");
  std::vector<std::vector<std::vector<uint8_t>>> witnesses;
  if(!parseSegWitWitnesses(rawTx, witnesses)) return {};

  std::vector<uint8_t> scriptPathHit;
  std::vector<uint8_t> keyPathFallback;
  for(const auto& stack : witnesses){
    if(stack.empty()) continue;
    const auto& front = stack.front();
    if(front.size() != 64 && front.size() != 65) continue;
    if(stack.size() >= 3){
      // Script path: [<sig> <leafScript> <controlBlock(>=33)>]; sig is item 0.
      const auto& cb = stack.back();
      if(&cb != &front && cb.size() >= 33 && scriptPathHit.empty()){
        scriptPathHit.assign(front.begin(), front.begin() + 64);
      }
    } else if(stack.size() == 1 && keyPathFallback.empty()){
      keyPathFallback.assign(front.begin(), front.begin() + 64);
    }
    if(!scriptPathHit.empty()) break;
  }
  if(!scriptPathHit.empty()) return scriptPathHit;
  return keyPathFallback;
}

// P2.1: t = s' - s against the stored presig. Completing an adaptor preserves R,
// so presig.R_x identifies our claim on both key-path ([sig]) and script-path
// ([sig <script> <control>]) witnesses.
bool BtcTaprootPtlc::parseClaimSecret(
    const std::vector<uint8_t>& rawTx,
    const std::vector<uint8_t>& tweakedPub33,
    const Crypto::SecpAdaptorPresig& storedPresig,
    Crypto::SecretKey& tOut){
  if(tweakedPub33.size() != 33) throw std::runtime_error("tweakedPub must be 33");
  std::vector<std::vector<std::vector<uint8_t>>> witnesses;
  if(!parseSegWitWitnesses(rawTx, witnesses)) return false;

  std::array<uint8_t,32> wantRx{};
  std::memcpy(wantRx.data(), storedPresig.R.data.data() + 1, 32);

  for(const auto& stack : witnesses){
    if(stack.empty()) continue;
    const auto& front = stack.front();
    if(front.size() != 64 && front.size() != 65) continue;
    bool plausibleShape =
        stack.size() == 1 ||
        (stack.size() >= 3 && stack.back().size() >= 33);
    if(!plausibleShape) continue;
    if(std::memcmp(front.data(), wantRx.data(), 32) != 0) continue;

    Crypto::SecpSchnorrSig schnorr{};
    std::memcpy(schnorr.data.data(), front.data(), 64);
    return Crypto::secp_adaptor_extract(storedPresig, schnorr, tOut);
  }
  return false;
}

std::vector<uint8_t> BtcTaprootPtlc::createControlBlock(
    const std::vector<uint8_t>& internalKey33,
    const std::vector<uint8_t>& tweakedPub33){
  if(internalKey33.size()!=33 || tweakedPub33.size()!=33) throw std::runtime_error("control block need 33");
  EC_GROUP* grp = secp_group();
  BN_CTX* ctx = BN_CTX_new();
  EC_POINT* Q_pt = EC_POINT_new(grp);
  uint8_t parity=0;
  if(EC_POINT_oct2point(grp, Q_pt, tweakedPub33.data(), 33, ctx)){
    BIGNUM *x=BN_new(), *y=BN_new();
    if(EC_POINT_get_affine_coordinates(grp, Q_pt, x, y, ctx)){
      parity = BN_is_odd(y) ? 1 : 0;
    }
    BN_free(x); BN_free(y);
  }
  EC_POINT_free(Q_pt); BN_CTX_free(ctx);
  std::vector<uint8_t> cb;
  cb.reserve(33);
  cb.push_back(0xc0 | parity); // leaf version 0xc0 + parity
  cb.insert(cb.end(), internalKey33.begin()+1, internalKey33.begin()+33); // x-only internal
  return cb;
}

std::vector<std::vector<uint8_t>> BtcTaprootPtlc::createScriptPathWitness(
    const std::vector<uint8_t>& sig,
    const std::vector<uint8_t>& leafScript,
    const std::vector<uint8_t>& controlBlock){
  // BIP341 script-path witness: <sig> <script> <control>
  std::vector<std::vector<uint8_t>> w;
  w.reserve(3);
  w.push_back(sig);
  w.push_back(leafScript);
  w.push_back(controlBlock);
  return w;
}

bool BtcTaprootPtlc::verifyAdaptorClaim(
    const Crypto::SecpPubKey& P,
    const Crypto::SecpPubKey& T,
    const Crypto::SecpAdaptorPresig& presig,
    const Crypto::Hash& msg){
  return Crypto::secp_adaptor_verify(P, T, presig, msg);
}

// ── P2.2/P2.3: on-chain key-path spend support ──────────────────────────────

std::vector<uint8_t> BtcTaprootPtlc::p2trScriptPubKey(const std::array<uint8_t,32>& xOnly){
  std::vector<uint8_t> spk;
  spk.reserve(34);
  spk.push_back(0x51); // OP_1 (witness version 1)
  spk.push_back(0x20); // push 32 bytes
  spk.insert(spk.end(), xOnly.begin(), xOnly.end());
  return spk;
}

static void appendLEBytes(std::vector<uint8_t>& v, uint64_t val, int bytes){
  for(int i = 0; i < bytes; ++i) v.push_back(static_cast<uint8_t>((val >> (8*i)) & 0xff));
}

static void appendCompactSize(std::vector<uint8_t>& v, uint64_t n){
  if(n < 0xFD){ v.push_back(static_cast<uint8_t>(n)); }
  else if(n <= 0xFFFF){ v.push_back(0xFD); appendLEBytes(v, n, 2); }
  else if(n <= 0xFFFFFFFF){ v.push_back(0xFE); appendLEBytes(v, n, 4); }
  else { v.push_back(0xFF); appendLEBytes(v, n, 8); }
}

bool BtcTaprootPtlc::computeTaprootKeyPathSighash(
    const std::string& inputTxid, uint32_t inputVout, uint64_t inputValue,
    const std::array<uint8_t,32>& spentXOnly,
    const std::vector<uint8_t>& destScriptPubKey, uint64_t outputValue,
    uint32_t nVersion, uint32_t nSequence, uint32_t nLockTime,
    std::array<uint8_t,32>& sighashOut){
  try{
    // prevout txid: display hex -> wire little-endian
    auto txidBE = hexToBytes(inputTxid);
    if(txidBE.size() != 32) return false;
    std::reverse(txidBE.begin(), txidBE.end());

    // BIP341 §3: all five midstates are SINGLE SHA256 (NOT double SHA256).
    // sha_prevouts = SHA256(outpoints); single outpoint
    std::vector<uint8_t> prevout(txidBE);
    appendLEBytes(prevout, inputVout, 4);
    auto sha_prevouts = sha256(prevout);

    // sha_amounts = SHA256(amounts LE64)
    std::vector<uint8_t> amt;
    appendLEBytes(amt, inputValue, 8);
    auto sha_amounts = sha256(amt);

    // sha_scriptpubkeys = SHA256(scriptPubKeys), each element serialized as
    // script inside CTxOut: CompactSize length prefix || scriptPubKey
    auto spentSpk = p2trScriptPubKey(spentXOnly);
    std::vector<uint8_t> spkList;
    appendCompactSize(spkList, spentSpk.size());
    spkList.insert(spkList.end(), spentSpk.begin(), spentSpk.end());
    auto sha_spks = sha256(spkList);

    // sha_sequences = SHA256(nSequence LE32)
    std::vector<uint8_t> seq;
    appendLEBytes(seq, nSequence, 4);
    auto sha_seqs = sha256(seq);

    // sha_outputs = SHA256(serialized outputs); single output
    std::vector<uint8_t> outs;
    appendLEBytes(outs, outputValue, 8);
    appendCompactSize(outs, destScriptPubKey.size());
    outs.insert(outs.end(), destScriptPubKey.begin(), destScriptPubKey.end());
    auto sha_outputs = sha256(outs);

    // SigMsg (BIP341 §3): hash_type || nVersion || nLockTime ||
    //   sha_prevouts || sha_amounts || sha_scriptpubkeys || sha_sequences ||
    //   sha_outputs || spend_type || input_index
    std::vector<uint8_t> sigmsg;
    sigmsg.reserve(1+4+4+5*32+1+4);
    sigmsg.push_back(0x00);                 // hash_type: SIGHASH_DEFAULT
    appendLEBytes(sigmsg, nVersion, 4);
    appendLEBytes(sigmsg, nLockTime, 4);
    sigmsg.insert(sigmsg.end(), sha_prevouts.begin(), sha_prevouts.end());
    sigmsg.insert(sigmsg.end(), sha_amounts.begin(), sha_amounts.end());
    sigmsg.insert(sigmsg.end(), sha_spks.begin(), sha_spks.end());
    sigmsg.insert(sigmsg.end(), sha_seqs.begin(), sha_seqs.end());
    sigmsg.insert(sigmsg.end(), sha_outputs.begin(), sha_outputs.end());
    sigmsg.push_back(0x00);                 // spend_type: ext_flag 0 | no annex
    appendLEBytes(sigmsg, 0u, 4);           // input_index: 4-byte LE uint32 (0; this builder emits single-input txs)

    // digest = SHA256(hT || 0x00(epoch) || SigMsg), hT = TaggedHash("TapSighash","")
    auto hT = taggedHash("TapSighash", static_cast<const uint8_t*>(nullptr), 0);
    std::vector<uint8_t> msg;
    msg.reserve(32 + 1 + sigmsg.size());
    msg.insert(msg.end(), hT.begin(), hT.end());
    msg.push_back(0x00);
    msg.insert(msg.end(), sigmsg.begin(), sigmsg.end());

    auto digest = sha256(msg);
    std::memcpy(sighashOut.data(), digest.data(), 32);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool BtcTaprootPtlc::signTaprootKeyPath(
    const std::array<uint8_t,32>& skInternal,
    const std::array<uint8_t,32>& tapTweak,
    const std::array<uint8_t,32>& tweakedXOnly,
    const std::array<uint8_t,32>& sighash,
    std::vector<uint8_t>& sig64Out){
  EC_GROUP* grp = secp_group();
  BN_CTX* ctx = BN_CTX_new();
  BIGNUM* order = get_order(ctx);
  bool ok = false;
  BIGNUM* sk_bn = nullptr; BIGNUM* sk_eff = nullptr; BIGNUM* tw_bn = nullptr;
  BIGNUM* sk_q = nullptr; BIGNUM* k_bn = nullptr;
  BIGNUM* e_bn = nullptr; BIGNUM* esk = nullptr; BIGNUM* s_bn = nullptr;
  EC_POINT* R_pt = nullptr;
  do{
    sk_bn = BN_bin2bn(skInternal.data(), 32, nullptr);
    if(!sk_bn || BN_is_zero(sk_bn) || BN_cmp(sk_bn, order) >= 0) break;

    // Mirror createTaprootPtlcOutput: internal key with odd y is negated,
    // so the effective secret is n - sk for odd-y internal keys.
    {
      EC_POINT* P_pt = EC_POINT_new(grp);
      if(!EC_POINT_mul(grp, P_pt, sk_bn, nullptr, nullptr, ctx)){ EC_POINT_free(P_pt); break; }
      BIGNUM *px = BN_new(), *py = BN_new();
      bool oddY = false;
      if(EC_POINT_get_affine_coordinates(grp, P_pt, px, py, ctx)) oddY = BN_is_odd(py) != 0;
      BN_free(px); BN_free(py); EC_POINT_free(P_pt);
      sk_eff = BN_new();
      if(oddY) BN_sub(sk_eff, order, sk_bn); else BN_copy(sk_eff, sk_bn);
    }

    // tweaked secret: sk_q = sk_eff + tweak (mod n)
    tw_bn = BN_bin2bn(tapTweak.data(), 32, nullptr);
    sk_q = BN_new();
    BN_mod_add(sk_q, sk_eff, tw_bn, order, ctx);
    if(BN_is_zero(sk_q)) break;

    // BIP340: verifiers lift the tweaked point Q from its x-only form assuming
    // EVEN y, so the signing secret must correspond to norm_even_y(Q). If
    // Q = sk_q*G has odd y, negate sk_q mod n: (-sk_q)*G = -Q keeps Q's
    // x-coordinate and flips y to even. The tweaked-point parity is thereby
    // even for every downstream verification context.
    {
      EC_POINT* Q_pt = EC_POINT_new(grp);
      if(!EC_POINT_mul(grp, Q_pt, sk_q, nullptr, nullptr, ctx)){ EC_POINT_free(Q_pt); break; }
      BIGNUM *qxq = BN_new(), *qyq = BN_new();
      if(EC_POINT_get_affine_coordinates(grp, Q_pt, qxq, qyq, ctx) && BN_is_odd(qyq)){
        BN_sub(sk_q, order, sk_q);
      }
      BN_free(qxq); BN_free(qyq); EC_POINT_free(Q_pt);
    }

    // Deterministic nonce k = dSHA256(normalized sk_q || sighash || counter), valid range only.
    k_bn = BN_new();
    bool kOk = false;
    for(uint8_t counter = 0; counter < 64 && !kOk; ++counter){
      std::vector<uint8_t> buf;
      buf.reserve(65);
      auto qb = bn_to_bytes(sk_q);
      buf.insert(buf.end(), qb.begin(), qb.end());
      buf.insert(buf.end(), sighash.begin(), sighash.end());
      buf.push_back(counter);
      auto h = sha256(sha256(buf));
      BN_bin2bn(h.data(), 32, k_bn);
      if(!BN_is_zero(k_bn) && BN_cmp(k_bn, order) < 0) kOk = true;
    }
    if(!kOk) break;

    // R = k*G; force even y per BIP340 (negate k if needed).
    R_pt = EC_POINT_new(grp);
    if(!EC_POINT_mul(grp, R_pt, k_bn, nullptr, nullptr, ctx)) break;
    BIGNUM *rx = BN_new(), *ry = BN_new();
    if(!EC_POINT_get_affine_coordinates(grp, R_pt, rx, ry, ctx)){ BN_free(rx); BN_free(ry); break; }
    if(BN_is_odd(ry)){
      BN_sub(k_bn, order, k_bn);
      EC_POINT_invert(grp, R_pt, ctx);
      EC_POINT_get_affine_coordinates(grp, R_pt, rx, ry, ctx);
    }
    auto rxb = bn_to_bytes(rx);
    BN_free(rx); BN_free(ry);

    // BIP340 challenge: e = TaggedHash("BIP0340/challenge", Rx || Qx || m)
    std::vector<uint8_t> chal;
    chal.reserve(96);
    chal.insert(chal.end(), rxb.begin(), rxb.end());
    chal.insert(chal.end(), tweakedXOnly.begin(), tweakedXOnly.end());
    chal.insert(chal.end(), sighash.begin(), sighash.end());
    auto e_arr = taggedHash("BIP0340/challenge", chal);
    e_bn = BN_bin2bn(e_arr.data(), 32, nullptr);

    // s = k + e*sk_q (mod n). BIP340 does NOT flip s > n/2 — its malleability
    // fix is the even-y R above. Official BIP340 vector 8 (negated s) must
    // verify; negating s here produces consensus-invalid signatures.
    esk = BN_new();
    BN_mod_mul(esk, e_bn, sk_q, order, ctx);
    s_bn = BN_new();
    BN_mod_add(s_bn, k_bn, esk, order, ctx);

    auto sb = bn_to_bytes(s_bn);
    sig64Out.assign(rxb.begin(), rxb.end());
    sig64Out.insert(sig64Out.end(), sb.begin(), sb.end());
    ok = true;
  } while(false);

  if(sk_bn) BN_free(sk_bn);
  if(sk_eff) BN_free(sk_eff);
  if(tw_bn) BN_free(tw_bn);
  if(sk_q) BN_free(sk_q);
  if(k_bn) BN_free(k_bn);
  if(e_bn) BN_free(e_bn);
  if(esk) BN_free(esk);
  if(s_bn) BN_free(s_bn);
  if(R_pt) EC_POINT_free(R_pt);
  BN_free(order); BN_CTX_free(ctx);
  return ok && sig64Out.size() == 64;
}

std::vector<uint8_t> BtcTaprootPtlc::buildRawTaprootSpendTx(
    const std::string& inputTxid, uint32_t inputVout,
    const std::vector<std::vector<uint8_t>>& witnessStack,
    const std::vector<uint8_t>& destScriptPubKey, uint64_t outputAmount,
    uint32_t nVersion, uint32_t nSequence, uint32_t nLockTime){
  std::vector<uint8_t> tx;

  writeLE32(tx, nVersion);
  tx.push_back(0x00); // SegWit marker
  tx.push_back(0x01); // SegWit flag

  appendCompactSize(tx, 1); // input count

  auto txidBytes = hexToBytes(inputTxid);
  std::reverse(txidBytes.begin(), txidBytes.end());
  tx.insert(tx.end(), txidBytes.begin(), txidBytes.end());
  appendLEBytes(tx, inputVout, 4);
  appendCompactSize(tx, 0); // empty scriptSig
  appendLEBytes(tx, nSequence, 4);

  appendCompactSize(tx, 1); // output count
  appendLEBytes(tx, outputAmount, 8);
  appendCompactSize(tx, destScriptPubKey.size());
  tx.insert(tx.end(), destScriptPubKey.begin(), destScriptPubKey.end());

  appendCompactSize(tx, witnessStack.size());
  for(const auto& item : witnessStack){
    appendCompactSize(tx, item.size());
    tx.insert(tx.end(), item.begin(), item.end());
  }

  appendLEBytes(tx, nLockTime, 4);
  return tx;
}

} // namespace XfgSwap
