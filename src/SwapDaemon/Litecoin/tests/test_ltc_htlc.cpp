// Copyright (c) 2017-2026 Fuego Developers
//
// Tests for LTC HTLC helper methods: P2WSH scriptPubKey generation and
// SegWit witness-based preimage parsing.

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "SwapDaemon/Litecoin/LtcHtlcScript.h"

using namespace XfgSwap;

// =============================================================================
// Helper: build a minimal raw SegWit transaction with a witness stack
// =============================================================================

// Build a SegWit tx with one input whose witness stack is provided.
// SegWit wire format (BIP144):
//   version(4) + marker(0x00) + flag(0x01) + vin + vout + witness + locktime(4)
static std::vector<uint8_t> buildSegWitTxWithWitness(
    const std::vector<std::pair<std::vector<uint8_t>, size_t>>& witnessItems) {

  std::vector<uint8_t> tx;

  // version = 2
  tx.insert(tx.end(), {0x02, 0x00, 0x00, 0x00});

  // SegWit marker + flag
  tx.push_back(0x00);  // marker
  tx.push_back(0x01);  // flag

  // 1 input
  tx.push_back(0x01);

  // prev txid (32 bytes of 0xaa)
  tx.insert(tx.end(), 32, 0xaa);

  // prev vout = 0
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  // scriptSig (empty for SegWit)
  tx.push_back(0x00);

  // sequence
  tx.insert(tx.end(), {0xFF, 0xFF, 0xFF, 0xFF});

  // 1 output
  tx.push_back(0x01);

  // output value = 99000 satoshis (8 bytes LE)
  tx.insert(tx.end(), {0x98, 0x84, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00});

  // output scriptPubKey: P2PKH
  auto outputScript = LtcHtlcScript::buildP2pkhScriptPubKey(std::vector<uint8_t>(20, 0x11));
  tx.push_back(static_cast<uint8_t>(outputScript.size()));
  tx.insert(tx.end(), outputScript.begin(), outputScript.end());

  // Witness data for the single input
  tx.push_back(static_cast<uint8_t>(witnessItems.size()));  // item count
  for (const auto& item : witnessItems) {
    if (item.second < 0xFD) {
      tx.push_back(static_cast<uint8_t>(item.second));
    } else {
      tx.push_back(0xFD);
      tx.push_back(static_cast<uint8_t>(item.second & 0xFF));
      tx.push_back(static_cast<uint8_t>((item.second >> 8) & 0xFF));
    }
    tx.insert(tx.end(), item.first.begin(), item.first.end());
  }

  // locktime = 0
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  return tx;
}

// =============================================================================
// Tests
// =============================================================================

static void test_ltc_redeemScriptToP2wshScriptPubKey() {
  std::cout << "test_ltc_redeemScriptToP2wshScriptPubKey..." << std::endl;

  // Create a dummy redeemScript
  std::vector<uint8_t> redeemScript = {
    0x63,                                           // OP_IF
    0xa8,                                           // OP_SHA256
    0x20,                                           // push 32 bytes
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x88,                                           // OP_EQUALVERIFY
    0x21,                                           // push 33 bytes
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
    0xac,                                           // OP_CHECKSIG
    0x67,                                           // OP_ELSE
    0x03,                                           // push 3 bytes (timeoutBlock=500)
    0xf4, 0x01, 0x00,
    0xb1,                                           // OP_CHECKLOCKTIMEVERIFY
    0x75,                                           // OP_DROP
    0x21,                                           // push 33 bytes
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
    0xac,                                           // OP_CHECKSIG
    0x68                                            // OP_ENDIF
  };

  auto p2wshScriptPubKey = LtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);

  // Verify it matches OP_0 PUSH32 SHA256(redeemScript)
  auto expectedHash = LtcHtlcScript::witnessScriptHash(redeemScript);
  std::vector<uint8_t> expected;
  expected.push_back(0x00);  // OP_0
  expected.push_back(0x20);  // push 32 bytes
  expected.insert(expected.end(), expectedHash.begin(), expectedHash.end());

  assert(p2wshScriptPubKey == expected && "redeemScriptToP2wshScriptPubKey must match expected");

  // Verify structure: OP_0(0x00) PUSH32(0x20) <32 bytes>  — 34 bytes total
  assert(p2wshScriptPubKey.size() == 34 && "P2WSH scriptPubKey must be 34 bytes");
  assert(p2wshScriptPubKey[0] == 0x00 && "must start with OP_0");
  assert(p2wshScriptPubKey[1] == 0x20 && "push must be 32 bytes");

  // Verify SHA256 consistency
  auto hash = LtcHtlcScript::sha256(redeemScript);
  assert(std::equal(p2wshScriptPubKey.begin() + 2, p2wshScriptPubKey.begin() + 34, hash.begin()));

  std::cout << "  PASS" << std::endl;
}

static void test_ltc_parseClaimPreimage_witness() {
  std::cout << "test_ltc_parseClaimPreimage_witness..." << std::endl;

  // Build a minimal HTLC redeem script
  std::vector<uint8_t> hashLock(32, 0xAA);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  recipientPubKey[32] = 0x01;
  std::vector<uint8_t> senderPubKey(33, 0x03);
  senderPubKey[32] = 0x02;
  uint32_t timeoutBlock = 1000;

  auto redeemScript = LtcHtlcScript::createHashTimeLockScript(
      hashLock, recipientPubKey, senderPubKey, timeoutBlock);

  // Compute the P2WSH scriptPubKey
  auto p2wshScriptPubKey = LtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);

  // Known preimage (32 bytes)
  std::vector<uint8_t> preimage = {
    0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28
  };

  // Create a fake DER signature
  std::vector<uint8_t> signature(71, 0xAB);
  signature[0] = 0x30;
  signature[1] = 0x44;
  signature[2] = 0x02;
  signature[3] = 0x20;
  signature[70] = 0x01;  // SIGHASH_ALL

  // Build witness stack for claim: <sig> <preimage> <OP_TRUE> <redeemScript>
  std::vector<std::pair<std::vector<uint8_t>, size_t>> witnessItems;
  witnessItems.push_back({signature, signature.size()});
  witnessItems.push_back({preimage, preimage.size()});
  witnessItems.push_back({{0x01}, 1});  // OP_TRUE
  witnessItems.push_back({redeemScript, redeemScript.size()});

  auto rawTx = buildSegWitTxWithWitness(witnessItems);

  // Parse the claim preimage
  std::vector<uint8_t> extracted = LtcHtlcScript::parseClaimPreimage(rawTx, p2wshScriptPubKey);

  assert(!extracted.empty() && "parseClaimPreimage must find the preimage");
  assert(extracted == preimage && "parseClaimPreimage must return the correct preimage");
  assert(extracted.size() == 32 && "preimage must be 32 bytes");

  std::cout << "  PASS" << std::endl;
}

static void test_ltc_parseClaimPreimage_no_match() {
  std::cout << "test_ltc_parseClaimPreimage_no_match..." << std::endl;

  // Build a SegWit tx with a non-matching witness script
  std::vector<uint8_t> wrongRedeemScript(50, 0xFF);

  std::vector<uint8_t> fakeSig(72, 0xCC);
  fakeSig[0] = 0x30;

  std::vector<std::pair<std::vector<uint8_t>, size_t>> witnessItems;
  witnessItems.push_back({fakeSig, fakeSig.size()});
  witnessItems.push_back({{0xDE, 0xAD}, 2});
  witnessItems.push_back({{0x01}, 1});
  witnessItems.push_back({wrongRedeemScript, wrongRedeemScript.size()});

  auto rawTx = buildSegWitTxWithWitness(witnessItems);

  // Use a P2WSH scriptPubKey whose hash doesn't match the witness script
  std::vector<uint8_t> p2wshScriptPubKey(34, 0x00);
  p2wshScriptPubKey[0] = 0x00;
  p2wshScriptPubKey[1] = 0x20;
  // Hash is all zeros — won't match SHA256(wrongRedeemScript)

  std::vector<uint8_t> result = LtcHtlcScript::parseClaimPreimage(rawTx, p2wshScriptPubKey);
  assert(result.empty() && "parseClaimPreimage must return empty when no claim input found");

  std::cout << "  PASS" << std::endl;
}

int main() {
  std::cout << "=== LTC HTLC Tests ===" << std::endl;

  test_ltc_redeemScriptToP2wshScriptPubKey();
  test_ltc_parseClaimPreimage_witness();
  test_ltc_parseClaimPreimage_no_match();

  std::cout << "\nAll LTC HTLC tests passed." << std::endl;
  return 0;
}
