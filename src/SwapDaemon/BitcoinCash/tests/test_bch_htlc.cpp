// Copyright (c) 2017-2026 Fuego Developers
//
// Tests for BCH HTLC helper methods: parseClaimPreimage and redeemScriptToP2shScriptPubKey.

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "SwapDaemon/BitcoinCash/HtlcScript.h"

using namespace XfgSwap;

static void test_redeemScriptToP2shScriptPubKey() {
  std::cout << "test_redeemScriptToP2shScriptPubKey..." << std::endl;

  // Create a dummy redeemScript (doesn't need to be a valid HTLC script for this test)
  std::vector<uint8_t> redeemScript = {
    0x63,                                           // OP_IF
    0xa8,                                           // OP_SHA256
    0x20,                                           // push 32 bytes
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,  // hashLock (32 bytes)
    0x88,                                           // OP_EQUALVERIFY
    0x21,                                           // push 33 bytes (compressed pubkey)
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,                                           // recipientPubKey
    0xac,                                           // OP_CHECKSIG
    0x67,                                           // OP_ELSE
    0x03,                                           // push 3 bytes (timeoutBlock=500)
    0xf4, 0x01, 0x00,                              // 500 in LE CScriptNum
    0xb1,                                           // OP_CHECKLOCKTIMEVERIFY
    0x75,                                           // OP_DROP
    0x21,                                           // push 33 bytes
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,                                           // senderPubKey
    0xac,                                           // OP_CHECKSIG
    0x68                                            // OP_ENDIF
  };

  std::vector<uint8_t> p2shScriptPubKey = BchHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  // Verify it matches buildP2shScriptPubKey(hash160(redeemScript))
  std::vector<uint8_t> expected = BchHtlcScript::buildP2shScriptPubKey(
      BchHtlcScript::hash160(redeemScript));

  assert(p2shScriptPubKey == expected && "redeemScriptToP2shScriptPubKey must match buildP2shScriptPubKey(hash160(...))");

  // Verify structure: OP_HASH160(0xA9) <push(0x14)> <20 bytes> OP_EQUAL(0x87)
  assert(p2shScriptPubKey.size() == 23 && "P2SH scriptPubKey must be 23 bytes");
  assert(p2shScriptPubKey[0]  == 0xA9 && "must start with OP_HASH160");
  assert(p2shScriptPubKey[1]  == 0x14 && "push must be 20 bytes");
  assert(p2shScriptPubKey[22] == 0x87 && "must end with OP_EQUAL");

  std::cout << "  PASS" << std::endl;
}

static void test_parseClaimPreimage() {
  std::cout << "test_parseClaimPreimage..." << std::endl;

  // Build a minimal HTLC redeem script
  std::vector<uint8_t> hashLock(32, 0xAA);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  recipientPubKey[32] = 0x01;
  std::vector<uint8_t> senderPubKey(33, 0x03);
  senderPubKey[32] = 0x02;
  uint32_t timeoutBlock = 1000;

  auto redeemScript = BchHtlcScript::createRedeemScript(
      hashLock, recipientPubKey, senderPubKey, timeoutBlock);

  // Compute the P2SH scriptPubKey that this redeemScript hashes to
  auto p2shScriptPubKey = BchHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  // Known preimage (32 bytes)
  std::vector<uint8_t> preimage = {
    0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28
  };

  // Create a fake DER signature (71 bytes + sighash byte = 72 bytes)
  std::vector<uint8_t> signature = {
    0x30, 0x44, 0x02, 0x20  // DER header
  };
  signature.resize(71, 0xAB);
  signature[0] = 0x30;
  signature[1] = 0x44;
  signature[2] = 0x02;
  signature[3] = 0x20;
  signature[70] = 0x41;  // SIGHASH_ALL | SIGHASH_FORKID

  // Build claim scriptSig: <signature> <preimage> OP_TRUE <redeemScript>
  auto scriptSig = BchHtlcScript::createClaimScriptSig(signature, preimage, redeemScript);

  // Build a raw transaction using the existing builder
  // Use a fake txid for the input
  std::string fakeTxid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  uint32_t inputVout = 0;
  uint64_t inputAmount = 100000;  // 0.001 BCH
  std::string destAddress = "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa";  // well-known P2PKH
  uint64_t outputAmount = inputAmount - 1000;  // minus fee
  uint32_t nLockTime = 0;

  // Build raw tx — this creates a tx spending from the HTLC with the claim scriptSig.
  // We don't have a valid address for the output, but buildRawTransaction expects one.
  // Let's construct the raw tx manually instead, since the dest address is hard-coded
  // and we need to ensure the structure is correct for parsing.

  // Manual construction of a minimal raw BCH transaction:
  // version(4) + vin_count(1) + prev_txid(32) + prev_vout(4) + scriptSig_len(varint) + scriptSig + sequence(4)
  // + vout_count(1) + output_value(8) + output_scriptPubKey_len(varint) + output_scriptPubKey + locktime(4)
  std::vector<uint8_t> rawTx;

  // version = 2
  rawTx.push_back(0x02);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);

  // vin count = 1
  rawTx.push_back(0x01);

  // prev txid (32 bytes of 0xaa)
  rawTx.insert(rawTx.end(), 32, 0xaa);

  // prev vout = 0
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);

  // scriptSig length (varint)
  if (scriptSig.size() < 0xFD) {
    rawTx.push_back(static_cast<uint8_t>(scriptSig.size()));
  } else {
    rawTx.push_back(0xFD);
    rawTx.push_back(static_cast<uint8_t>(scriptSig.size() & 0xFF));
    rawTx.push_back(static_cast<uint8_t>((scriptSig.size() >> 8) & 0xFF));
  }

  // scriptSig
  rawTx.insert(rawTx.end(), scriptSig.begin(), scriptSig.end());

  // sequence = 0xFFFFFFFF
  rawTx.push_back(0xFF);
  rawTx.push_back(0xFF);
  rawTx.push_back(0xFF);
  rawTx.push_back(0xFF);

  // vout count = 1
  rawTx.push_back(0x01);

  // output value = 99000 satoshis (8 bytes LE)
  rawTx.push_back(0x98);
  rawTx.push_back(0x84);
  rawTx.push_back(0x01);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);

  // output scriptPubKey: simple OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG (P2PKH)
  std::vector<uint8_t> outputScriptPubKey = BchHtlcScript::buildP2pkhScriptPubKey(
      std::vector<uint8_t>(20, 0x11));
  rawTx.push_back(static_cast<uint8_t>(outputScriptPubKey.size()));
  rawTx.insert(rawTx.end(), outputScriptPubKey.begin(), outputScriptPubKey.end());

  // locktime = 0
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);

  // Parse the claim preimage
  std::vector<uint8_t> extracted = BchHtlcScript::parseClaimPreimage(rawTx, p2shScriptPubKey);

  assert(extracted == preimage && "parseClaimPreimage must return the correct preimage");
  assert(extracted.size() == 32 && "preimage must be 32 bytes");

  std::cout << "  PASS" << std::endl;
}

static void test_parseClaimPreimage_no_match() {
  std::cout << "test_parseClaimPreimage_no_match..." << std::endl;

  // Build a raw tx with a non-HTLC scriptSig (e.g., simple P2PKH spend)
  std::vector<uint8_t> rawTx;

  // version = 2
  rawTx.push_back(0x02);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);

  // vin count = 1
  rawTx.push_back(0x01);

  // prev txid (32 bytes of 0xbb)
  rawTx.insert(rawTx.end(), 32, 0xbb);

  // prev vout = 1
  rawTx.push_back(0x01);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);

  // A simple P2PKH scriptSig: <sig> <pubkey>
  std::vector<uint8_t> p2pkhScriptSig;
  std::vector<uint8_t> fakeSig(72, 0xCC);
  fakeSig[0] = 0x30;
  fakeSig[71] = 0x41;
  p2pkhScriptSig.push_back(0x48);  // push 72 bytes
  p2pkhScriptSig.insert(p2pkhScriptSig.end(), fakeSig.begin(), fakeSig.end());
  std::vector<uint8_t> fakePubkey(33, 0x02);
  p2pkhScriptSig.push_back(0x21);  // push 33 bytes
  p2pkhScriptSig.insert(p2pkhScriptSig.end(), fakePubkey.begin(), fakePubkey.end());

  rawTx.push_back(static_cast<uint8_t>(p2pkhScriptSig.size()));
  rawTx.insert(rawTx.end(), p2pkhScriptSig.begin(), p2pkhScriptSig.end());

  // sequence
  rawTx.push_back(0xFF);
  rawTx.push_back(0xFF);
  rawTx.push_back(0xFF);
  rawTx.push_back(0xFF);

  // vout count = 1
  rawTx.push_back(0x01);

  // output value
  rawTx.push_back(0x00);
  rawTx.push_back(0x10);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);

  // output scriptPubKey (P2PKH)
  auto outputScript = BchHtlcScript::buildP2pkhScriptPubKey(std::vector<uint8_t>(20, 0x11));
  rawTx.push_back(static_cast<uint8_t>(outputScript.size()));
  rawTx.insert(rawTx.end(), outputScript.begin(), outputScript.end());

  // locktime
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);
  rawTx.push_back(0x00);

  // A random P2SH scriptPubKey (not matching the input)
  std::vector<uint8_t> fakeP2sh(23, 0x00);
  fakeP2sh[0] = 0xA9;
  fakeP2sh[1] = 0x14;
  fakeP2sh[22] = 0x87;

  std::vector<uint8_t> result = BchHtlcScript::parseClaimPreimage(rawTx, fakeP2sh);
  assert(result.empty() && "parseClaimPreimage must return empty when no claim input found");

  std::cout << "  PASS" << std::endl;
}

int main() {
  test_redeemScriptToP2shScriptPubKey();
  test_parseClaimPreimage();
  test_parseClaimPreimage_no_match();

  std::cout << "All BCH HTLC tests passed." << std::endl;
  return 0;
}
