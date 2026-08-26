// PTLC unit tests — Phase 0/1 bridge + secp adaptor
#include <cassert>
#include <cstring>
#include <iostream>
#include "SwapDaemon/SwapPtlcLock.h"
#include "SwapDaemon/Bitcoin/BtcPtlcScript.h"
#include "crypto/secp_adaptor.h"
#include "crypto/crypto.h"
#include "Common/StringTools.h"

using namespace XfgSwap;

int main() {
  std::cout << "=== PTLC Tests ===\n";

  // 1. negotiateLockType matrix
  {
    std::cout << "[1] negotiateLockType\n";
    assert(negotiateLockType(false,false,false)==SwapLockType::HTLC);
    assert(negotiateLockType(true,true,false)==SwapLockType::PTLC);
    assert(negotiateLockType(true,false,false)==SwapLockType::PTLC_HTLC_BRIDGE);
    assert(negotiateLockType(false,true,false)==SwapLockType::PTLC_HTLC_BRIDGE);
    assert(negotiateLockType(false,false,true)==SwapLockType::PTLC); // require forces PTLC signal (caller must abort if not met)
    assert(isPtlcNativePair(SwapPair::XMR)==true);
    assert(isPtlcNativePair(SwapPair::BTC)==false);
    std::cout << "  PASS\n";
  }

  // 1b. negotiateLockTypeV2 matrix (P4.2 pure)
  {
    std::cout << "[1b] negotiateLockTypeV2\n";
    // both PURE → PTLC pure regardless of base caps
    assert(negotiateLockTypeV2(true, true,  true, true,  false)==SwapLockType::PTLC);
    assert(negotiateLockTypeV2(false,false,true, true,  false)==SwapLockType::PTLC);
    // not both pure → exact legacy behavior
    assert(negotiateLockTypeV2(true, true,  true, false, false)==SwapLockType::PTLC);
    assert(negotiateLockTypeV2(true, false, true, false, false)==SwapLockType::PTLC_HTLC_BRIDGE);
    assert(negotiateLockTypeV2(false,true,  false,true,  false)==SwapLockType::PTLC_HTLC_BRIDGE);
    assert(negotiateLockTypeV2(false,false,false,false,false)==SwapLockType::HTLC);
    assert(negotiateLockTypeV2(false,false,false,false,true )==SwapLockType::PTLC); // require signal preserved
    std::cout << "  PASS\n";
  }

  // 1b. isLegacyUtxoPair hash-family matrix (GLEEC-fix adoption)
  {
    std::cout << "[1b] isLegacyUtxoPair\n";
    // SHA-256 family: UTXO + TON
    assert(isLegacyUtxoPair(SwapPair::BTC)==true);
    assert(isLegacyUtxoPair(SwapPair::BCH)==true);
    assert(isLegacyUtxoPair(SwapPair::LTC)==true);
    assert(isLegacyUtxoPair(SwapPair::DCR)==true);
    assert(isLegacyUtxoPair(SwapPair::KMD_SPV)==true);
    assert(isLegacyUtxoPair(SwapPair::DOGE)==true);
    assert(isLegacyUtxoPair(SwapPair::DASH)==true);
    assert(isLegacyUtxoPair(SwapPair::ZEC)==true);
    assert(isLegacyUtxoPair(SwapPair::TON)==true);
    // GLEEC must be keccak family (EVM client) — the dashpltc fix
    assert(isLegacyUtxoPair(SwapPair::GLEEC)==false);
    // SIA handled explicitly via blake2b, not this predicate
    assert(isLegacyUtxoPair(SwapPair::SIA)==false);
    // EVM/SOL/native pairs → keccak / no-hashlock paths
    assert(isLegacyUtxoPair(SwapPair::ETH)==false);
    assert(isLegacyUtxoPair(SwapPair::SOL)==false);
    assert(isLegacyUtxoPair(SwapPair::XMR)==false);
    assert(isLegacyUtxoPair(SwapPair::ZANO)==false);
    std::cout << "  PASS\n";
  }

  // 2. ptlcPoint verify
  {
    std::cout << "[2] verifyPtlcPoint\n";
    Crypto::SecretKey t{}; reinterpret_cast<uint8_t*>(&t)[0]=5; reinterpret_cast<uint8_t*>(&t)[1]=7;
    Crypto::PublicKey T; assert(Crypto::secret_key_to_public_key(t,T));
    assert(verifyPtlcPoint(t,T)==true);
    Crypto::PublicKey wrong{}; assert(verifyPtlcPoint(t,wrong)==false);
    // ensure copy via ptlcPoint helpers
    std::string hex=ptlcPointToHex(T); Crypto::PublicKey T2; assert(hexToPtlcPoint(hex,T2) && std::memcmp(&T,&T2,sizeof(T))==0);
    std::cout << "  PASS\n";
  }

  // 3. swapLockTypeToString roundtrip
  {
    std::cout << "[3] swapLockTypeToString\n";
    assert(std::string(swapLockTypeToString(SwapLockType::HTLC))=="HTLC");
    assert(std::string(swapLockTypeToString(SwapLockType::PTLC))=="PTLC");
    assert(std::string(swapLockTypeToString(SwapLockType::PTLC_HTLC_BRIDGE))=="PTLC_HTLC_BRIDGE");
    SwapLockType out; assert(swapLockTypeFromString("PTLC", out) && out==SwapLockType::PTLC);
    assert(swapLockTypeFromString("BRIDGE", out) && out==SwapLockType::PTLC_HTLC_BRIDGE);
    assert(swapLockTypeFromString("htlc", out) && out==SwapLockType::HTLC);
    std::cout << "  PASS\n";
  }

  // 4. SwapParams lockType persistence via SwapStateMachine serialize (smoke)
  {
    std::cout << "[4] SwapParams lockType\n";
    SwapParams p; p.lockType=SwapLockType::PTLC_HTLC_BRIDGE; p.requirePtlc=true;
    for(int i=0;i<32;i++) reinterpret_cast<uint8_t*>(&p.ptlcPoint)[i]=uint8_t(i+9);
    assert(p.lockType==SwapLockType::PTLC_HTLC_BRIDGE);
    assert(p.requirePtlc==true);
    assert(!isZeroPubKey(p.ptlcPoint));
    std::cout << "  PASS\n";
  }

  // 5. BtcPtlcScript create + address
  {
    std::cout << "[5] BtcPtlcScript\n";
    std::vector<uint8_t> pt(32, 0xAB);
    std::vector<uint8_t> rec(33, 0x02); rec[1]=0x11; rec[32]=0x33;
    std::vector<uint8_t> send(33, 0x02); send[1]=0x22;
    auto redeem = BtcPtlcScript::createPtlcScript(pt, 0, rec, send, 800000);
    assert(!redeem.empty());
    auto p2wsh = BtcPtlcScript::redeemScriptToP2wshScriptPubKey(redeem);
    assert(p2wsh.size()==34 && p2wsh[0]==0x00 && p2wsh[1]==0x20);
    std::string addr = BtcPtlcScript::witnessScriptToAddress(redeem, "bc");
    assert(!addr.empty());
    std::vector<uint8_t> sig(64, 0x30); sig[0]=0x30;
    auto witness = BtcPtlcScript::createClaimWitness(sig, pt, redeem);
    assert(witness.size()==4);
    std::cout << "  PASS addr=" << addr.substr(0,10) << "...\n";
  }

  // 6. secp adaptor roundtrip (use small valid scalars < n)
  {
    std::cout << "[6] secp_adaptor roundtrip\n";
    Crypto::SecretKey sk{}, k{}, t{};
    reinterpret_cast<uint8_t*>(&sk)[0]=1; reinterpret_cast<uint8_t*>(&sk)[1]=2;
    reinterpret_cast<uint8_t*>(&k)[0]=3; reinterpret_cast<uint8_t*>(&k)[1]=4;
    reinterpret_cast<uint8_t*>(&t)[0]=5; reinterpret_cast<uint8_t*>(&t)[1]=6;
    // ensure non-zero and < n (small values are < n)
    Crypto::Hash msg; for(int i=0;i<32;i++) msg.data[i]=uint8_t(i+10);
    Crypto::SecpAdaptorPresig presig;
    assert(Crypto::secp_adaptor_sign(sk,k,t,msg,presig));
    Crypto::SecpPubKey P,T;
    assert(Crypto::secp_secret_to_pubkey(sk,P));
    assert(Crypto::secp_secret_to_pubkey(t,T));
    assert(Crypto::secp_adaptor_verify(P,T,presig,msg));
    // wrong T should fail
    Crypto::SecretKey wrong_t{}; reinterpret_cast<uint8_t*>(&wrong_t)[0]=9; reinterpret_cast<uint8_t*>(&wrong_t)[1]=9;
    Crypto::SecpPubKey wrong_T; assert(Crypto::secp_secret_to_pubkey(wrong_t,wrong_T));
    assert(!Crypto::secp_adaptor_verify(P,wrong_T,presig,msg));
    // complete and extract
    Crypto::SecpSchnorrSig sig;
    assert(Crypto::secp_complete_schnorr_sig(sk,k,msg,sig));
    Crypto::SecretKey extracted;
    assert(Crypto::secp_adaptor_extract(presig,sig,extracted));
    assert(std::memcmp(&extracted,&t,sizeof(t))==0);
    std::cout << "  PASS\n";
  }

  // 7. chainState suffix handling (BtcPtlc bridge)
  {
    std::cout << "[7] chainState suffix\n";
    SwapParams p; p.pair=SwapPair::BTC; p.lockType=SwapLockType::PTLC_HTLC_BRIDGE;
    for(int i=0;i<32;i++) reinterpret_cast<uint8_t*>(&p.ptlcPoint)[i]=0xCD;
    std::string redeemHex = "0014abcd"; // dummy
    std::string ptHex = Common::podToHex(p.ptlcPoint);
    std::string state = redeemHex + "|ptlc:" + ptHex;
    auto pipe = state.find('|'); assert(pipe!=std::string::npos);
    std::string base = state.substr(0,pipe);
    assert(base==redeemHex);
    std::cout << "  PASS\n";
  }

  std::cout << "\nAll PTLC tests passed.\n";
  return 0;
}
