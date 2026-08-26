// Pure PTLC (P2TR key-path) unit tests — P2.1/P2.2/P2.3
#include <cassert>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <random>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/obj_mac.h>
#include "SwapDaemon/SwapPtlcLock.h"
#include "SwapDaemon/IChainClient.h"
#include "SwapDaemon/Bitcoin/BtcTaprootPtlc.h"
#include "crypto/secp_adaptor.h"
#include "crypto/crypto.h"

using namespace XfgSwap;

// ── helpers ──────────────────────────────────────────────────────────────────
static void setScalar(Crypto::SecretKey& sk, uint8_t lo, uint8_t hi) {
  auto* p = reinterpret_cast<uint8_t*>(&sk);
  std::memset(p, 0, 32);
  p[0] = lo; p[1] = hi;
}

static std::vector<uint8_t> makePub33(uint8_t lo, uint8_t hi) {
  Crypto::SecretKey sk; setScalar(sk, lo, hi);
  Crypto::SecpPubKey pk;
  bool ok = Crypto::secp_secret_to_pubkey(sk, pk);
  assert(ok && "makePub33: secp_secret_to_pubkey failed");
  (void)ok;
  return std::vector<uint8_t>(pk.data.begin(), pk.data.end());
}

static std::array<uint8_t,32> testSha256(const std::vector<uint8_t>& d) {
  std::array<uint8_t,32> out{};
  SHA256(d.data(), d.size(), out.data());
  return out;
}

static std::array<uint8_t,32> testTaggedHash(const std::string& tag, const std::vector<uint8_t>& msg) {
  auto th = testSha256(std::vector<uint8_t>(tag.begin(), tag.end()));
  std::vector<uint8_t> buf(th.begin(), th.end());
  buf.insert(buf.end(), th.begin(), th.end());
  buf.insert(buf.end(), msg.begin(), msg.end());
  return testSha256(buf);
}

// Independent BIP340 verification over (qx, sig[64], msg[32]) using OpenSSL.
static bool bip340Verify(const std::array<uint8_t,32>& qx,
                         const std::vector<uint8_t>& sig,
                         const std::array<uint8_t,32>& msg) {
  if (sig.size() != 64) return false;
  EC_GROUP* grp = EC_GROUP_new_by_curve_name(NID_secp256k1);
  BN_CTX* ctx = BN_CTX_new();
  BIGNUM *p = BN_new(), *n = BN_new();
  EC_GROUP_get_curve(grp, p, nullptr, nullptr, ctx);
  EC_GROUP_get_order(grp, n, ctx);

  bool ok = false;
  EC_POINT* Pq = nullptr; EC_POINT* R = nullptr;
  BIGNUM *x = nullptr, *y = nullptr, *r = nullptr, *s = nullptr, *e = nullptr, *eneg = nullptr;
  do {
    x = BN_bin2bn(qx.data(), 32, nullptr);
    if (BN_is_zero(x) || BN_cmp(x, p) >= 0) break;
    BIGNUM* c = BN_new(); BIGNUM* seven = BN_new();
    BN_mod_sqr(c, x, p, ctx);
    BN_mod_mul(c, c, x, p, ctx);
    BN_set_word(seven, 7);
    BN_mod_add(c, c, seven, p, ctx);
    y = BN_mod_sqrt(nullptr, c, p, ctx);
    BN_free(c); BN_free(seven);
    if (!y) break;                       // x not on curve
    if (BN_is_odd(y)) BN_sub(y, p, y);   // lift_x: even y
    Pq = EC_POINT_new(grp);
    if (!EC_POINT_set_affine_coordinates(grp, Pq, x, y, ctx)) break;

    r = BN_bin2bn(sig.data(), 32, nullptr);
    s = BN_bin2bn(sig.data() + 32, 32, nullptr);
    if (BN_is_zero(r) || BN_cmp(r, n) >= 0) break;
    if (BN_is_zero(s) || BN_cmp(s, n) >= 0) break;

    std::vector<uint8_t> chal(sig.begin(), sig.begin() + 32);
    chal.insert(chal.end(), qx.begin(), qx.end());
    chal.insert(chal.end(), msg.begin(), msg.end());
    auto e_arr = testTaggedHash("BIP0340/challenge", chal);
    e = BN_bin2bn(e_arr.data(), 32, nullptr);
    BN_nnmod(e, e, n, ctx);
    eneg = BN_new();
    if (BN_is_zero(e)) BN_zero(eneg); else BN_sub(eneg, n, e);

    R = EC_POINT_new(grp);
    if (!EC_POINT_mul(grp, R, s, Pq, eneg, ctx)) break;   // s*G - e*Q
    if (EC_POINT_is_at_infinity(grp, R)) break;
    BIGNUM *rx = BN_new(), *ry = BN_new();
    if (!EC_POINT_get_affine_coordinates(grp, R, rx, ry, ctx)) { BN_free(rx); BN_free(ry); break; }
    bool evenY = !BN_is_odd(ry);
    std::array<uint8_t,32> rxb{};
    int len = BN_num_bytes(rx);
    std::vector<uint8_t> tmp(len > 32 ? 32 : static_cast<size_t>(len));
    if (len > 0) BN_bn2bin(rx, tmp.data());
    if (len <= 32) std::memcpy(rxb.data() + 32 - len, tmp.data(), tmp.size());
    else std::memcpy(rxb.data(), tmp.data(), 32);
    BN_free(rx); BN_free(ry);
    ok = evenY && std::memcmp(rxb.data(), sig.data(), 32) == 0;
  } while (false);

  if (x) BN_free(x);
  if (y) BN_free(y);
  if (r) BN_free(r);
  if (s) BN_free(s);
  if (e) BN_free(e);
  if (eneg) BN_free(eneg);
  if (Pq) EC_POINT_free(Pq);
  if (R) EC_POINT_free(R);
  BN_free(p); BN_free(n); BN_CTX_free(ctx); EC_GROUP_free(grp);
  return ok;
}

// Standard BIP340 verifier in spec-equation form: lift_x(r_x) assuming EVEN y,
// e = TaggedHash("BIP0340/challenge", R_x || Q_x || m), require s*G == R + e*Q.
static bool verifySchnorrBip340(const std::array<uint8_t,32>& qx,
                                const std::vector<uint8_t>& sig,
                                const std::array<uint8_t,32>& msg) {
  if (sig.size() != 64) return false;
  EC_GROUP* grp = EC_GROUP_new_by_curve_name(NID_secp256k1);
  BN_CTX* ctx = BN_CTX_new();
  BIGNUM *p = BN_new(), *n = BN_new();
  EC_GROUP_get_curve(grp, p, nullptr, nullptr, ctx);
  EC_GROUP_get_order(grp, n, ctx);

  auto bnTo32 = [&](const BIGNUM* bn) {
    std::array<uint8_t,32> out{}; out.fill(0);
    int len = BN_num_bytes(bn);
    std::vector<uint8_t> tmp(len > 32 ? 32 : static_cast<size_t>(len));
    if (len > 0) BN_bn2bin(bn, tmp.data());
    if (len <= 32) std::memcpy(out.data() + 32 - len, tmp.data(), tmp.size());
    else std::memcpy(out.data(), tmp.data(), 32);
    return out;
  };

  bool ok = false;
  EC_POINT *Q = nullptr, *R = nullptr, *lhs = nullptr, *rhs = nullptr, *eQ = nullptr;
  BIGNUM *x = nullptr, *y = nullptr, *r = nullptr, *s = nullptr, *e = nullptr;
  do {
    // lift_x: x on curve, take the EVEN-y solution (BIP340 spec rule).
    x = BN_bin2bn(qx.data(), 32, nullptr);
    if (BN_is_zero(x) || BN_cmp(x, p) >= 0) break;
    BIGNUM* c = BN_new(); BIGNUM* seven = BN_new();
    BN_mod_sqr(c, x, p, ctx);
    BN_mod_mul(c, c, x, p, ctx);
    BN_set_word(seven, 7);
    BN_mod_add(c, c, seven, p, ctx);
    y = BN_mod_sqrt(nullptr, c, p, ctx);
    BN_free(c); BN_free(seven);
    if (!y) break;                       // x not on curve
    if (BN_is_odd(y)) BN_sub(y, p, y);   // even-y lift
    Q = EC_POINT_new(grp);
    if (!EC_POINT_set_affine_coordinates(grp, Q, x, y, ctx)) break;

    r = BN_bin2bn(sig.data(), 32, nullptr);
    s = BN_bin2bn(sig.data() + 32, 32, nullptr);
    if (BN_is_zero(r) || BN_cmp(r, n) >= 0) break;
    if (BN_is_zero(s) || BN_cmp(s, n) >= 0) break;

    std::vector<uint8_t> chal(sig.begin(), sig.begin() + 32);   // R_x
    chal.insert(chal.end(), qx.begin(), qx.end());              // Q_x
    chal.insert(chal.end(), msg.begin(), msg.end());            // m
    auto e_arr = testTaggedHash("BIP0340/challenge", chal);
    e = BN_bin2bn(e_arr.data(), 32, nullptr);
    BN_nnmod(e, e, n, ctx);

    lhs = EC_POINT_new(grp); rhs = EC_POINT_new(grp);
    eQ = EC_POINT_new(grp);  R = EC_POINT_new(grp);
    if (!EC_POINT_mul(grp, lhs, s, nullptr, nullptr, ctx)) break;         // sG
    // R lifted from r_x with EVEN y.
    if (!EC_POINT_set_compressed_coordinates(grp, R, r, /*y_bit=*/0, ctx)) break;
    if (!EC_POINT_mul(grp, eQ, nullptr, Q, e, ctx)) break;                // eQ
    if (!EC_POINT_add(grp, rhs, R, eQ, ctx)) break;                       // R + eQ
    ok = EC_POINT_cmp(grp, lhs, rhs, ctx) == 0;
  } while (false);

  if (x) BN_free(x);
  if (y) BN_free(y);
  if (r) BN_free(r);
  if (s) BN_free(s);
  if (e) BN_free(e);
  if (Q) EC_POINT_free(Q);
  if (R) EC_POINT_free(R);
  if (lhs) EC_POINT_free(lhs);
  if (rhs) EC_POINT_free(rhs);
  if (eQ) EC_POINT_free(eQ);
  BN_free(p); BN_free(n); BN_CTX_free(ctx); EC_GROUP_free(grp);
  return ok;
}

// Strict-BIP341 sighash recomputation (reference implementation for conformance diag).
static std::array<uint8_t,32> refBip341Sighash(
    const std::string& inputTxid, uint32_t inputVout, uint64_t inputValue,
    const std::vector<uint8_t>& spentSpk, const std::vector<uint8_t>& destSpk,
    uint64_t outputValue, uint32_t nVersion, uint32_t nSequence, uint32_t nLockTime) {
  auto appendLE = [](std::vector<uint8_t>& v, uint64_t val, int bytes) {
    for (int i = 0; i < bytes; ++i) v.push_back(static_cast<uint8_t>((val >> (8*i)) & 0xff));
  };
  auto cs = [](std::vector<uint8_t>& v, uint64_t sz) {
    v.push_back(static_cast<uint8_t>(sz)); // sizes here < 253
  };
  auto txidBE = BtcTaprootPtlc::hexToBytes(inputTxid);
  std::reverse(txidBE.begin(), txidBE.end());

  std::vector<uint8_t> prevouts(txidBE); appendLE(prevouts, inputVout, 4);
  std::vector<uint8_t> amt; appendLE(amt, inputValue, 8);
  std::vector<uint8_t> spks; cs(spks, spentSpk.size()); spks.insert(spks.end(), spentSpk.begin(), spentSpk.end());
  std::vector<uint8_t> seq; appendLE(seq, nSequence, 4);
  std::vector<uint8_t> outs; appendLE(outs, outputValue, 8); cs(outs, destSpk.size()); outs.insert(outs.end(), destSpk.begin(), destSpk.end());

  auto S = [](const std::vector<uint8_t>& d) { return testSha256(d); }; // SINGLE sha256 per BIP341

  std::vector<uint8_t> sigmsg;
  sigmsg.push_back(0x00);
  appendLE(sigmsg, nVersion, 4);
  appendLE(sigmsg, nLockTime, 4);
  auto hp = S(prevouts), ha = S(amt), hs = S(spks), hq = S(seq), ho = S(outs);
  sigmsg.insert(sigmsg.end(), hp.begin(), hp.end());
  sigmsg.insert(sigmsg.end(), ha.begin(), ha.end());
  sigmsg.insert(sigmsg.end(), hs.begin(), hs.end());
  sigmsg.insert(sigmsg.end(), hq.begin(), hq.end());
  sigmsg.insert(sigmsg.end(), ho.begin(), ho.end());
  sigmsg.push_back(0x00);            // spend_type
  appendLE(sigmsg, 0u, 4);           // input_index: 4-byte LE per BIP341

  auto hT = testTaggedHash("TapSighash", {});
  std::vector<uint8_t> msg(hT.begin(), hT.end());
  msg.push_back(0x00);
  msg.insert(msg.end(), sigmsg.begin(), sigmsg.end());
  return testSha256(msg);
}

namespace {
class TestChainClientBase : public IChainClient {
public:
  std::string chainName() const override { return "testbase"; }
  ChainClientResult lock(const SwapParams&) override { return ChainClientResult::fail("stub"); }
  ChainClientResult verifyLock(const SwapParams&) override { return ChainClientResult::fail("stub"); }
  ChainClientResult claim(const SwapParams&) override { return ChainClientResult::fail("stub"); }
  ChainClientResult refund(const SwapParams&) override { return ChainClientResult::fail("stub"); }
  ChainClientResult verifyReserveProof(const std::string&, uint64_t, const std::string&) override {
    return ChainClientResult::fail("stub");
  }
};
class TestChainClientPure : public TestChainClientBase {
public:
  std::string chainName() const override { return "testpure"; }
  bool supportsPurePtlc() const override { return true; }
};
} // namespace

int main() {
  std::cout << "=== Pure PTLC Tests ===\n";

  const std::string kTxid = std::string("deadbeef") + "deadbeef" + "deadbeef" + "deadbeef"
                          + "deadbeef" + "deadbeef" + "deadbeef" + "deadbeef"; // 64 hex chars
  assert(kTxid.size() == 64);

  // 1. createTaprootPtlc: validity, tweak, address, control block, determinism
  {
    std::cout << "[1] createTaprootPtlc\n";
    auto internalKey = makePub33(0x01, 0x02);
    std::vector<uint8_t> ptlcPointX(32); for (int i = 0; i < 32; ++i) ptlcPointX[i] = uint8_t(0xA0 + i);
    auto recipient = makePub33(0x03, 0x04);
    auto sender = makePub33(0x05, 0x06);

    auto out = BtcTaprootPtlc::createTaprootPtlc(internalKey, ptlcPointX, 144, recipient, sender);

    assert(out.tweakedPubKey.size() == 33);
    assert((out.tweakedPubKey[0] == 0x02 || out.tweakedPubKey[0] == 0x03));
    assert(out.tweakedPubKey != internalKey);
    std::array<uint8_t,32> internalX{};
    std::memcpy(internalX.data(), internalKey.data() + 1, 32);
    assert(out.tweakedPubKeyXOnly != internalX);

    bool tweakZero = true;
    for (auto b : out.tapTweak) if (b) { tweakZero = false; break; }
    assert(!tweakZero);

    assert(out.p2trAddress.rfind("bc1p", 0) == 0);
    assert(out.p2trAddress.size() == 62); // bech32m P2TR mainnet length

    assert(out.controlBlock.size() == 33);
    assert(out.controlBlock[0] == 0xc0 || out.controlBlock[0] == 0xc1);
    assert(std::memcmp(out.controlBlock.data() + 1, internalKey.data() + 1, 32) == 0);
    assert(!out.redeemScript.empty());

    auto out2 = BtcTaprootPtlc::createTaprootPtlc(internalKey, ptlcPointX, 144, recipient, sender);
    assert(out2.p2trAddress == out.p2trAddress);
    assert(out2.tapTweak == out.tapTweak);
    assert(out2.tweakedPubKey == out.tweakedPubKey);
    assert(out2.tweakedPubKeyXOnly == out.tweakedPubKeyXOnly);
    assert(out2.controlBlock == out.controlBlock);
    assert(out2.redeemScript == out.redeemScript);

    // different timeout => different tweak/address (commitment binds params)
    auto out3 = BtcTaprootPtlc::createTaprootPtlc(internalKey, ptlcPointX, 200, recipient, sender);
    assert(out3.tapTweak != out.tapTweak);
    assert(out3.p2trAddress != out.p2trAddress);
    std::cout << "  PASS addr=" << out.p2trAddress.substr(0, 12) << "...\n";
  }

  // 2. p2trScriptPubKey encoding
  {
    std::cout << "[2] p2trScriptPubKey\n";
    std::array<uint8_t,32> x{};
    for (int i = 0; i < 32; ++i) x[i] = uint8_t(i * 7 + 1);
    auto spk = BtcTaprootPtlc::p2trScriptPubKey(x);
    assert(spk.size() == 34);
    assert(spk[0] == 0x51); // OP_1
    assert(spk[1] == 0x20); // PUSH32
    assert(std::memcmp(spk.data() + 2, x.data(), 32) == 0);
    std::cout << "  PASS\n";
  }

  // 3. computeTaprootKeyPathSighash + signTaprootKeyPath
  {
    std::cout << "[3] sighash+sign roundtrip\n";
    Crypto::SecretKey skInt; setScalar(skInt, 0x21, 0x43);
    auto internalKey = makePub33(0x21, 0x43);
    std::vector<uint8_t> ptlcPointX(32, 0x5A);
    auto recipient = makePub33(0x07, 0x08);
    auto sender = makePub33(0x09, 0x0A);

    auto out = BtcTaprootPtlc::createTaprootPtlc(internalKey, ptlcPointX, 288, recipient, sender);

    std::array<uint8_t,32> destX{};
    for (int i = 0; i < 32; ++i) destX[i] = uint8_t(0xEE - i);
    auto destSpk = BtcTaprootPtlc::p2trScriptPubKey(destX);

    std::array<uint8_t,32> sighash{};
    assert(BtcTaprootPtlc::computeTaprootKeyPathSighash(
        kTxid, 0, 50000, out.tweakedPubKeyXOnly, destSpk, 49000,
        2, 0xFFFFFFFD, 0, sighash));

    std::array<uint8_t,32> skArr{};
    std::memcpy(skArr.data(), &skInt, 32);

    std::vector<uint8_t> sig1, sig2;
    assert(BtcTaprootPtlc::signTaprootKeyPath(skArr, out.tapTweak, out.tweakedPubKeyXOnly, sighash, sig1));
    assert(BtcTaprootPtlc::signTaprootKeyPath(skArr, out.tapTweak, out.tweakedPubKeyXOnly, sighash, sig2));
    assert(sig1.size() == 64);
    assert(sig1 == sig2); // deterministic nonce

    // Sighash binds transaction data.
    std::array<uint8_t,32> sighash2{};
    assert(BtcTaprootPtlc::computeTaprootKeyPathSighash(
        kTxid, 0, 50000, out.tweakedPubKeyXOnly, destSpk, 48000,
        2, 0xFFFFFFFD, 0, sighash2));
    assert(sighash2 != sighash);
    assert(BtcTaprootPtlc::computeTaprootKeyPathSighash(
        kTxid, 1, 50000, out.tweakedPubKeyXOnly, destSpk, 49000,
        2, 0xFFFFFFFD, 0, sighash2));
    assert(sighash2 != sighash);

    // Bad inputs rejected.
    std::vector<uint8_t> sigBad;
    std::array<uint8_t,32> zeroSk{};
    assert(!BtcTaprootPtlc::signTaprootKeyPath(zeroSk, out.tapTweak, out.tweakedPubKeyXOnly, sighash, sigBad));

    // Independent BIP340 validity measurement (informational — see report).
    {
      int total = 0, valid = 0;
      for (int it = 0; it < 100; ++it) {
        Crypto::SecretKey sk{}; setScalar(sk, uint8_t(it + 1), uint8_t((it * 7) % 250 + 1));
        auto ik = makePub33(uint8_t(it + 1), uint8_t((it * 7) % 250 + 1));
        auto o = BtcTaprootPtlc::createTaprootPtlc(ik, ptlcPointX, uint32_t(100 + it), recipient, sender);
        std::array<uint8_t,32> sh{}, sa{};
        assert(BtcTaprootPtlc::computeTaprootKeyPathSighash(
            kTxid, 0, 50000, o.tweakedPubKeyXOnly, destSpk, 49000, 2, 0xFFFFFFFD, 0, sh));
        std::memcpy(sa.data(), &sk, 32);
        std::vector<uint8_t> sb;
        if (!BtcTaprootPtlc::signTaprootKeyPath(sa, o.tapTweak, o.tweakedPubKeyXOnly, sh, sb)) continue;
        ++total;
        if (bip340Verify(o.tweakedPubKeyXOnly, sb, sh)) ++valid;
      }
      std::cout << "  INFO: BIP340 validity of signTaprootKeyPath: " << valid << "/" << total << "\n";
      assert(total == 100);
      assert(valid == total); // consensus-invalid signatures are now a hard failure
    }
    std::cout << "  PASS\n";
  }

  // 4. parseClaimSecret adaptor roundtrip over a built claim tx
  {
    std::cout << "[4] parseClaimSecret roundtrip\n";
    Crypto::SecretKey sk, k, t;
    setScalar(sk, 0x11, 0x22); setScalar(k, 0x33, 0x44); setScalar(t, 0x55, 0x66);
    Crypto::Hash msg; for (int i = 0; i < 32; ++i) msg.data[i] = uint8_t(i + 3);
    Crypto::SecpAdaptorPresig presig;
    assert(Crypto::secp_adaptor_sign(sk, k, t, msg, presig));

    Crypto::SecpSchnorrSig adapted{};
    assert(BtcTaprootPtlc::adaptorToSchnorrSig(presig, t, adapted));

    std::vector<uint8_t> sig64(adapted.data.begin(), adapted.data.end());
    std::array<uint8_t,32> destX{};
    for (int i = 0; i < 32; ++i) destX[i] = uint8_t(0x80 + i);
    auto destSpk = BtcTaprootPtlc::p2trScriptPubKey(destX);
    auto tweakedPub33 = makePub33(0x77, 0x88);

    auto tx = BtcTaprootPtlc::buildRawTaprootSpendTx(kTxid, 1, {sig64}, destSpk, 123456);
    assert(tx.size() > 60 && tx[4] == 0x00 && tx[5] == 0x01); // segwit marker/flag present

    Crypto::SecretKey extracted{};
    assert(BtcTaprootPtlc::parseClaimSecret(tx, tweakedPub33, presig, extracted));
    assert(std::memcmp(&extracted, &t, sizeof(t)) == 0);

    // Corrupted R_x in the witness -> no matching candidate -> false.
    auto badSig = sig64;
    badSig[0] ^= 0x01;
    auto txCorrupt = BtcTaprootPtlc::buildRawTaprootSpendTx(kTxid, 1, {badSig}, destSpk, 123456);
    Crypto::SecretKey dummy{};
    assert(!BtcTaprootPtlc::parseClaimSecret(txCorrupt, tweakedPub33, presig, dummy));

    // Non-segwit tx -> parser rejects -> false.
    auto txLegacy = tx;
    txLegacy[4] = 0xFF; // break SegWit marker
    assert(!BtcTaprootPtlc::parseClaimSecret(txLegacy, tweakedPub33, presig, dummy));
    std::cout << "  PASS\n";
  }

  // 5. negotiateLockTypeV2 matrix (pure overload)
  {
    std::cout << "[5] negotiateLockTypeV2\n";
    assert(negotiateLockTypeV2(true,  true,  true,  true,  false) == SwapLockType::PTLC);
    assert(negotiateLockTypeV2(false, false, true,  true,  false) == SwapLockType::PTLC);
    assert(negotiateLockTypeV2(true,  false, true,  true,  false) == SwapLockType::PTLC);
    assert(negotiateLockTypeV2(false, true,  true,  true,  true ) == SwapLockType::PTLC);

    assert(negotiateLockTypeV2(true,  true,  true,  false, false) == SwapLockType::PTLC);
    assert(negotiateLockTypeV2(true,  true,  false, true,  false) == SwapLockType::PTLC);
    assert(negotiateLockTypeV2(true,  false, true,  false, false) == SwapLockType::PTLC_HTLC_BRIDGE);
    assert(negotiateLockTypeV2(false, true,  false, true,  false) == SwapLockType::PTLC_HTLC_BRIDGE);
    assert(negotiateLockTypeV2(true,  false, false, false, false) == SwapLockType::PTLC_HTLC_BRIDGE);
    assert(negotiateLockTypeV2(false, true,  false, false, false) == SwapLockType::PTLC_HTLC_BRIDGE);
    assert(negotiateLockTypeV2(true,  true,  false, false, false) == SwapLockType::PTLC);

    assert(negotiateLockTypeV2(false, false, false, false, false) == SwapLockType::HTLC);
    assert(negotiateLockTypeV2(false, false, false, false, true ) == SwapLockType::PTLC); // require sentinel preserved
    std::cout << "  PASS\n";
  }

  // 6. IChainClient::supportsPurePtlc default
  {
    std::cout << "[6] supportsPurePtlc default\n";
    std::unique_ptr<IChainClient> base(new TestChainClientBase());
    std::unique_ptr<IChainClient> pure(new TestChainClientPure());
    assert(base->supportsPurePtlc() == false);
    assert(base->supportsPtlc() == false);
    assert(pure->supportsPurePtlc() == true);
    std::cout << "  PASS\n";
  }

  // 7. BIP341 sighash conformance diagnostics (informational, not asserted)
  {
    std::cout << "[7] BIP341 conformance (informational)\n";
    auto internalKey = makePub33(0xB0, 0xB1);
    std::vector<uint8_t> ptlcPointX(32, 0xC7);
    auto recipient = makePub33(0xB2, 0xB3);
    auto sender = makePub33(0xB4, 0xB5);
    auto out = BtcTaprootPtlc::createTaprootPtlc(internalKey, ptlcPointX, 100, recipient, sender);
    auto destSpk = BtcTaprootPtlc::p2trScriptPubKey(out.tweakedPubKeyXOnly);

    std::array<uint8_t,32> prod{}, ref{};
    bool okProd = BtcTaprootPtlc::computeTaprootKeyPathSighash(
        kTxid, 0, 9000, out.tweakedPubKeyXOnly, destSpk, 8000, 2, 0xFFFFFFFD, 0, prod);
    ref = refBip341Sighash(kTxid, 0, 9000, BtcTaprootPtlc::p2trScriptPubKey(out.tweakedPubKeyXOnly),
                           destSpk, 8000, 2, 0xFFFFFFFD, 0);
    assert(okProd);
    assert(prod == ref); // strict BIP341 conformance required post-fix
    std::cout << "  PASS (production sighash matches strict BIP341 reference)\n";
  }

  // 8. signTaprootKeyPath must produce standard-BIP340-valid signatures against
  //    the TWEAKED public key: even-y normalization of the tweaked secret and
  //    NO low-s flip (official BIP340 vector 8 semantics).
  {
    std::cout << "[8] signTaprootKeyPath BIP340 validity vs tweaked pubkey\n";
    std::mt19937_64 rng(0xF00D5EEDULL); // deterministic run
    auto recipient = makePub33(0x07, 0x08);
    auto sender = makePub33(0x09, 0x0A);

    const int kIters = 20;
    int validCount = 0;
    std::vector<uint8_t> lastSig;
    std::array<uint8_t,32> lastQx{}, lastMsg{};
    for (int it = 0; it < kIters; ++it) {
      // Random small scalar (< 2^224): four leading zero bytes, rest random.
      Crypto::SecretKey sk{};
      {
        auto* p = reinterpret_cast<uint8_t*>(&sk);
        std::memset(p, 0, 32);
        bool allZero = true;
        for (int i = 4; i < 32; ++i) {
          p[i] = static_cast<uint8_t>(rng() & 0xFF);
          if (p[i]) allZero = false;
        }
        if (allZero) p[31] = 1;
      }

      Crypto::SecpPubKey pk;
      assert(Crypto::secp_secret_to_pubkey(sk, pk));
      auto internalKey = std::vector<uint8_t>(pk.data.begin(), pk.data.end());
      std::vector<uint8_t> ptlcPointX(32);
      for (int i = 0; i < 32; ++i) ptlcPointX[i] = static_cast<uint8_t>(rng() & 0xFF);

      auto o = BtcTaprootPtlc::createTaprootPtlc(
          internalKey, ptlcPointX, uint32_t(100 + it), recipient, sender);
      assert(o.tweakedPubKey.size() == 33);

      std::array<uint8_t,32> sh{};
      assert(BtcTaprootPtlc::computeTaprootKeyPathSighash(
          kTxid, uint32_t(it), 50000, o.tweakedPubKeyXOnly,
          BtcTaprootPtlc::p2trScriptPubKey(o.tweakedPubKeyXOnly), 49000,
          2, 0xFFFFFFFD, 0, sh));

      std::array<uint8_t,32> skArr{};
      std::memcpy(skArr.data(), &sk, 32);
      std::vector<uint8_t> sig;
      assert(BtcTaprootPtlc::signTaprootKeyPath(skArr, o.tapTweak, o.tweakedPubKeyXOnly, sh, sig));
      assert(sig.size() == 64);

      if (verifySchnorrBip340(o.tweakedPubKeyXOnly, sig, sh)) ++validCount;
      lastSig = sig;
      lastQx = o.tweakedPubKeyXOnly;
      lastMsg = sh;
    }
    std::cout << "  INFO: standard-BIP340 verifier accepted " << validCount << "/" << kIters << "\n";
    assert(validCount == kIters); // require 20/20

    // BIP340 vector-8 semantics: negating s (low-S style flip) MUST FAIL.
    {
      EC_GROUP* grp = EC_GROUP_new_by_curve_name(NID_secp256k1);
      BN_CTX* ctx = BN_CTX_new();
      BIGNUM* n = BN_new();
      EC_GROUP_get_order(grp, n, ctx);
      BIGNUM* s = BN_bin2bn(lastSig.data() + 32, 32, nullptr);
      BIGNUM* sNeg = BN_new();
      BN_sub(sNeg, n, s);
      int len = BN_num_bytes(sNeg);
      std::vector<uint8_t> badSig(lastSig.begin(), lastSig.begin() + 32);
      badSig.resize(64, 0);
      std::vector<uint8_t> tmp(len > 32 ? 32 : static_cast<size_t>(len));
      if (len > 0) BN_bn2bin(sNeg, tmp.data());
      std::memcpy(badSig.data() + 32 + (32 - tmp.size()), tmp.data(), tmp.size());
      assert(!verifySchnorrBip340(lastQx, badSig, lastMsg));
      BN_free(s); BN_free(sNeg); BN_free(n); BN_CTX_free(ctx); EC_GROUP_free(grp);
      std::cout << "  INFO: negated-s (BIP340 vector 8 form) correctly rejected\n";
    }
    std::cout << "  PASS (" << kIters << "/" << kIters << " valid)\n";
  }

  // 9. Sighash midstate spot-check: production must feed SINGLE-SHA256
  //    midstates (sha_prevouts et al.), CompactSize-prefixed scriptPubKeys,
  //    and a 4-byte LE input_index into the TapSighash engine.
  {
    std::cout << "[9] sighash midstate spot-check\n";
    auto internalKey = makePub33(0xC1, 0xC2);
    std::vector<uint8_t> ptlcPointX(32, 0xD1);
    auto recipient = makePub33(0xC3, 0xC4);
    auto sender = makePub33(0xC5, 0xC6);
    auto out = BtcTaprootPtlc::createTaprootPtlc(internalKey, ptlcPointX, 500, recipient, sender);

    const uint32_t vout = 3, ver = 2, seq = 0xFFFFFFFD, locktime = 777;
    const uint64_t amt = 123456789ULL, outAmt = 123450000ULL;
    auto spentSpk = BtcTaprootPtlc::p2trScriptPubKey(out.tweakedPubKeyXOnly);
    auto destSpk  = BtcTaprootPtlc::p2trScriptPubKey(out.tweakedPubKeyXOnly);

    std::array<uint8_t,32> prod{};
    assert(BtcTaprootPtlc::computeTaprootKeyPathSighash(
        kTxid, vout, amt, out.tweakedPubKeyXOnly, destSpk, outAmt, ver, seq, locktime, prod));

    // Replicate the serialized inputs exactly as production assembles them.
    auto txidBE = BtcTaprootPtlc::hexToBytes(kTxid);
    std::reverse(txidBE.begin(), txidBE.end());
    std::vector<uint8_t> prevout(txidBE);
    for (int i = 0; i < 4; ++i) prevout.push_back(uint8_t((vout >> (8*i)) & 0xFF));
    auto leBytes = [](uint64_t v, int n) {
      std::vector<uint8_t> r;
      for (int i = 0; i < n; ++i) r.push_back(uint8_t((v >> (8*i)) & 0xFF));
      return r;
    };
    std::vector<uint8_t> amt_b = leBytes(amt, 8);
    std::vector<uint8_t> spks_prefixed; spks_prefixed.push_back(uint8_t(spentSpk.size()));
    spks_prefixed.insert(spks_prefixed.end(), spentSpk.begin(), spentSpk.end());
    std::vector<uint8_t> spks_bare(spentSpk.begin(), spentSpk.end());
    std::vector<uint8_t> seq_b = leBytes(seq, 4);
    std::vector<uint8_t> outs_b = leBytes(outAmt, 8);
    outs_b.push_back(uint8_t(destSpk.size()));
    outs_b.insert(outs_b.end(), destSpk.begin(), destSpk.end());

    auto digestWith = [&](bool dblPrevouts, bool spkPrefix, bool idxLE4) {
      std::array<uint8_t,32> hp = testSha256(prevout);
      if (dblPrevouts) hp = testSha256({hp.begin(), hp.end()});
      auto ha = testSha256(amt_b);
      auto hs = testSha256(spkPrefix ? spks_prefixed : spks_bare);
      auto hq = testSha256(seq_b);
      auto ho = testSha256(outs_b);

      std::vector<uint8_t> sigmsg;
      sigmsg.push_back(0x00);                       // hash_type SIGHASH_DEFAULT
      for (auto b : leBytes(ver, 4)) sigmsg.push_back(b);
      for (auto b : leBytes(locktime, 4)) sigmsg.push_back(b);
      for (const auto* h : {&hp, &ha, &hs, &hq, &ho})
        sigmsg.insert(sigmsg.end(), h->begin(), h->end());
      sigmsg.push_back(0x00);                       // spend_type
      if (idxLE4) for (auto b : leBytes(0u, 4)) sigmsg.push_back(b);
      else sigmsg.push_back(0x00);                  // legacy 1-byte compactSize bug

      auto hT = testTaggedHash("TapSighash", {});
      std::vector<uint8_t> msg(hT.begin(), hT.end());
      msg.push_back(0x00);
      msg.insert(msg.end(), sigmsg.begin(), sigmsg.end());
      return testSha256(msg);
    };

    // sha_prevouts fed by production is SINGLE SHA256 of the outpoint.
    auto sha_prevouts_single = testSha256(prevout);
    auto sha_prevouts_double = testSha256({sha_prevouts_single.begin(),
                                           sha_prevouts_single.end()});
    assert(sha_prevouts_single != sha_prevouts_double);

    assert(digestWith(false, true,  true ) == prod);  // exact match with fixed engine
    assert(digestWith(true,  true,  true ) != prod);  // double-hashed prevouts rejected
    assert(digestWith(false, false, true ) != prod);  // bare spk (no prefix) rejected
    assert(digestWith(false, true,  false) != prod);  // 1-byte input_index rejected

    std::cout << "  PASS (single-sha midstates, spk prefix, 4-byte input_index confirmed)\n";
  }

  std::cout << "\nAll Pure PTLC tests passed.\n";
  return 0;
}
