// Copyright (c) 2017-2026 Fuego Developers
//
// Tests for BTC HTLC helper methods: redeemScriptToP2wshScriptPubKey and parseClaimPreimage.

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "SwapDaemon/Bitcoin/BtcHtlcScript.h"

using namespace XfgSwap;

// =============================================================================
// Helper: build a minimal raw SegWit transaction with P2WSH outputs
// =============================================================================

static std::vector<uint8_t> buildMinimalSegwitTx(
    const std::vector<uint8_t>& p2wshScriptPubKey,
    uint64_t outputValue) {

  std::vector<uint8_t> tx;

  // Version = 2
  tx.insert(tx.end(), {0x02, 0x00, 0x00, 0x00});

  // SegWit marker (0x00) + flag (0x01)
  tx.insert(tx.end(), {0x00, 0x01});

  // 1 input
  tx.push_back(0x01);

  // prev txid (32 bytes zeros)
  tx.insert(tx.end(), 32, 0x00);

  // prev vout = 0
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  // scriptSig (empty for SegWit)
  tx.push_back(0x00);

  // sequence
  tx.insert(tx.end(), {0xFF, 0xFF, 0xFF, 0xFF});

  // 1 output
  tx.push_back(0x01);

  // value (8 bytes LE)
  for (int i = 0; i < 8; ++i) {
    tx.push_back(static_cast<uint8_t>((outputValue >> (i * 8)) & 0xFF));
  }

  // scriptPubKey
  tx.push_back(static_cast<uint8_t>(p2wshScriptPubKey.size()));
  tx.insert(tx.end(), p2wshScriptPubKey.begin(), p2wshScriptPubKey.end());

  // Witness for input 0: 1 item, empty witness (just a dummy for the structure)
  tx.push_back(0x01);  // 1 witness item
  tx.push_back(0x00);  // item length = 0

  // locktime
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  return tx;
}

// Build a SegWit spending tx with a witness stack for a P2WSH HTLC claim.
// witness stack = [<signature>, <preimage>, <redeemScript>]
static std::vector<uint8_t> buildClaimSpendingSegwitTx(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& preimage,
    const std::vector<uint8_t>& redeemScript) {

  std::vector<uint8_t> tx;

  // Version = 2
  tx.insert(tx.end(), {0x02, 0x00, 0x00, 0x00});

  // SegWit marker + flag
  tx.insert(tx.end(), {0x00, 0x01});

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

  // 0 outputs (we don't care about outputs for this test)
  tx.push_back(0x00);

  // Witness for input 0: [<signature>, <preimage>, <redeemScript>]
  tx.push_back(0x03);  // 3 witness items

  // Item 0: signature
  if (signature.size() < 0xFD) {
    tx.push_back(static_cast<uint8_t>(signature.size()));
  } else {
    tx.push_back(0xFD);
    tx.push_back(static_cast<uint8_t>(signature.size() & 0xFF));
    tx.push_back(static_cast<uint8_t>((signature.size() >> 8) & 0xFF));
  }
  tx.insert(tx.end(), signature.begin(), signature.end());

  // Push preimage
  if (preimage.size() < 0xFD) {
    tx.push_back(static_cast<uint8_t>(preimage.size()));
  } else {
    tx.push_back(0xFD);
    tx.push_back(static_cast<uint8_t>(preimage.size() & 0xFF));
    tx.push_back(static_cast<uint8_t>((preimage.size() >> 8) & 0xFF));
  }
  tx.insert(tx.end(), preimage.begin(), preimage.end());

  // Push redeemScript (witnessScript — last item)
  if (redeemScript.size() < 0xFD) {
    tx.push_back(static_cast<uint8_t>(redeemScript.size()));
  } else {
    tx.push_back(0xFD);
    tx.push_back(static_cast<uint8_t>(redeemScript.size() & 0xFF));
    tx.push_back(static_cast<uint8_t>((redeemScript.size() >> 8) & 0xFF));
  }
  tx.insert(tx.end(), redeemScript.begin(), redeemScript.end());

  // locktime
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  return tx;
}

// Build a SegWit spending tx with a witness stack for refund (no preimage)
// witness stack = [<signature>, <redeemScript>]
static std::vector<uint8_t> buildRefundSpendingSegwitTx(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& redeemScript) {

  std::vector<uint8_t> tx;

  // Version = 2
  tx.insert(tx.end(), {0x02, 0x00, 0x00, 0x00});

  // SegWit marker + flag
  tx.insert(tx.end(), {0x00, 0x01});

  // 1 input
  tx.push_back(0x01);

  // prev txid (32 bytes of 0xbb)
  tx.insert(tx.end(), 32, 0xbb);

  // prev vout = 0
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  // scriptSig (empty)
  tx.push_back(0x00);

  // sequence
  tx.insert(tx.end(), {0xFF, 0xFF, 0xFF, 0xFF});

  // 0 outputs
  tx.push_back(0x00);

  // Witness for input 0: [<signature>, <redeemScript>]
  tx.push_back(0x02);  // 2 witness items

  // Push signature
  if (signature.size() < 0xFD) {
    tx.push_back(static_cast<uint8_t>(signature.size()));
  } else {
    tx.push_back(0xFD);
    tx.push_back(static_cast<uint8_t>(signature.size() & 0xFF));
    tx.push_back(static_cast<uint8_t>((signature.size() >> 8) & 0xFF));
  }
  tx.insert(tx.end(), signature.begin(), signature.end());

  // Push redeemScript
  if (redeemScript.size() < 0xFD) {
    tx.push_back(static_cast<uint8_t>(redeemScript.size()));
  } else {
    tx.push_back(0xFD);
    tx.push_back(static_cast<uint8_t>(redeemScript.size() & 0xFF));
    tx.push_back(static_cast<uint8_t>((redeemScript.size() >> 8) & 0xFF));
  }
  tx.insert(tx.end(), redeemScript.begin(), redeemScript.end());

  // locktime
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  return tx;
}

// =============================================================================
// Tests
// =============================================================================

static void test_btc_redeemScriptToP2wshScriptPubKey() {
  std::cout << "test_btc_redeemScriptToP2wshScriptPubKey..." << std::endl;

  // Create a dummy redeem script
  std::vector<uint8_t> redeemScript = {
    0x63,                                           // OP_IF
    0xa8,                                           // OP_SHA256
    0x20,                                           // push 32 bytes
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,  // hashLock
    0x88,                                           // OP_EQUALVERIFY
    0x21,                                           // push 33 bytes
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

  auto p2wshScriptPubKey = BtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);

  // Verify P2WSH structure: 34 bytes, OP_0, OP_20, SHA256 hash
  assert(p2wshScriptPubKey.size() == 34 && "P2WSH scriptPubKey must be 34 bytes");
  assert(p2wshScriptPubKey[0] == 0x00 && "must start with OP_0");
  assert(p2wshScriptPubKey[1] == 0x20 && "push must be 32 bytes");

  // Verify the hash matches SHA256(redeemScript)
  auto expectedHash = BtcHtlcScript::sha256(redeemScript);
  assert(std::equal(p2wshScriptPubKey.begin() + 2, p2wshScriptPubKey.begin() + 34,
                     expectedHash.begin()) &&
         "P2WSH hash must be SHA256(redeemScript)");

  std::cout << "  PASS" << std::endl;
}

static void test_btc_parseClaimPreimage_witness() {
  std::cout << "test_btc_parseClaimPreimage_witness..." << std::endl;

  // Build a minimal HTLC redeem script
  std::vector<uint8_t> hashLock(32, 0xAA);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  recipientPubKey[32] = 0x01;
  std::vector<uint8_t> senderPubKey(33, 0x03);
  senderPubKey[32] = 0x02;
  uint32_t timeoutBlock = 1000;

  auto redeemScript = BtcHtlcScript::createHashTimeLockScript(
      hashLock, 0, recipientPubKey, senderPubKey, timeoutBlock);

  // Compute the P2WSH scriptPubKey
  auto p2wshScriptPubKey = BtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);

  // Known preimage (32 bytes)
  std::vector<uint8_t> preimage = {
    0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28
  };

  // Fake DER signature (71 bytes + sighash byte = 72 bytes)
  std::vector<uint8_t> signature(72, 0xAB);
  signature[0] = 0x30;
  signature[1] = 0x44;
  signature[2] = 0x02;
  signature[3] = 0x20;
  signature[71] = 0x01;  // SIGHASH_ALL (no FORKID for BTC)

  // Build a SegWit spending tx with witness: [<sig>, <preimage>, <redeemScript>]
  auto rawTx = buildClaimSpendingSegwitTx(signature, preimage, redeemScript);

  // Parse the claim preimage
  std::vector<uint8_t> extracted = BtcHtlcScript::parseClaimPreimage(rawTx, p2wshScriptPubKey);

  assert(!extracted.empty() && "parseClaimPreimage must find the preimage");
  assert(extracted == preimage && "parseClaimPreimage must return the correct preimage");
  assert(extracted.size() == 32 && "preimage must be 32 bytes");

  std::cout << "  PASS" << std::endl;
}

static void test_btc_parseClaimPreimage_no_match() {
  std::cout << "test_btc_parseClaimPreimage_no_match..." << std::endl;

  // Build a minimal HTLC redeem script
  std::vector<uint8_t> hashLock(32, 0xBB);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  std::vector<uint8_t> senderPubKey(33, 0x03);
  auto redeemScript = BtcHtlcScript::createHashTimeLockScript(
      hashLock, 0, recipientPubKey, senderPubKey, 500);

  std::vector<uint8_t> signature(72, 0xCC);
  signature[0] = 0x30;
  signature[71] = 0x01;
  std::vector<uint8_t> preimage(32, 0xDD);

  // Build a spending tx using the real redeemScript
  auto rawTx = buildClaimSpendingSegwitTx(signature, preimage, redeemScript);

  // Now try to parse with a DIFFERENT P2WSH hash (wrong script)
  std::vector<uint8_t> wrongRedeemScript(20, 0xFF);
  auto wrongP2wsh = BtcHtlcScript::redeemScriptToP2wshScriptPubKey(wrongRedeemScript);

  std::vector<uint8_t> result = BtcHtlcScript::parseClaimPreimage(rawTx, wrongP2wsh);
  assert(result.empty() && "parseClaimPreimage must return empty when no witness stack matches");

  std::cout << "  PASS" << std::endl;
}

static void test_btc_parseClaimPreimage_refund_no_preimage() {
  std::cout << "test_btc_parseClaimPreimage_refund_no_preimage..." << std::endl;

  // A refund witness stack is [<sig>, <redeemScript>] — only 2 items.
  // parseClaimPreimage should NOT find a preimage (needs >= 3 items).
  std::vector<uint8_t> hashLock(32, 0xCC);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  std::vector<uint8_t> senderPubKey(33, 0x03);
  auto redeemScript = BtcHtlcScript::createHashTimeLockScript(
      hashLock, 0, recipientPubKey, senderPubKey, 500);
  auto p2wshScriptPubKey = BtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);

  std::vector<uint8_t> signature(72, 0xEE);
  auto rawTx = buildRefundSpendingSegwitTx(signature, redeemScript);

  std::vector<uint8_t> result = BtcHtlcScript::parseClaimPreimage(rawTx, p2wshScriptPubKey);
  assert(result.empty() && "parseClaimPreimage must not find preimage in refund witness stack");

  std::cout << "  PASS" << std::endl;
}

static void test_btc_parseClaimPreimage_not_segwit() {
  std::cout << "test_btc_parseClaimPreimage_not_segwit..." << std::endl;

  // Build a non-SegWit tx (no marker+flag) — should return empty
  std::vector<uint8_t> tx;

  // version = 2
  tx.insert(tx.end(), {0x02, 0x00, 0x00, 0x00});

  // No marker+flag — regular tx

  // 1 input
  tx.push_back(0x01);

  // prev txid
  tx.insert(tx.end(), 32, 0x00);

  // prev vout
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  // scriptSig (non-empty)
  tx.push_back(0x04);
  tx.insert(tx.end(), {0x01, 0x02, 0x03, 0x04});

  // sequence
  tx.insert(tx.end(), {0xFF, 0xFF, 0xFF, 0xFF});

  // 0 outputs
  tx.push_back(0x00);

  // locktime
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  // Random P2WSH scriptPubKey
  std::vector<uint8_t> p2wsh(34, 0x00);
  p2wsh[0] = 0x00;
  p2wsh[1] = 0x20;

  auto result = BtcHtlcScript::parseClaimPreimage(tx, p2wsh);
  assert(result.empty() && "parseClaimPreimage must return empty for non-SegWit tx");

  std::cout << "  PASS" << std::endl;
}

static void test_btc_createHashTimeLockScript() {
  std::cout << "test_btc_createHashTimeLockScript..." << std::endl;

  std::vector<uint8_t> hashLock(32, 0xAA);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  recipientPubKey[32] = 0x01;
  std::vector<uint8_t> senderPubKey(33, 0x03);
  senderPubKey[32] = 0x02;
  uint32_t timeoutBlock = 500000;

  auto script = BtcHtlcScript::createHashTimeLockScript(
      hashLock, 0, recipientPubKey, senderPubKey, timeoutBlock);

  // Verify script structure
  assert(script.size() > 0 && "script must not be empty");
  assert(script[0] == BtcOpCode::OP_IF && "must start with OP_IF");
  assert(script.back() == BtcOpCode::OP_ENDIF && "must end with OP_ENDIF");

  // Verify it's the same structure as BCH HTLC
  // (OP_IF OP_SHA256 <32> OP_EQUALVERIFY <33> OP_CHECKSIG OP_ELSE <timeout> OP_CLTV OP_DROP <33> OP_CHECKSIG OP_ENDIF)
  assert(script[1] == BtcOpCode::OP_SHA256 && "second byte must be OP_SHA256");

  // Verify P2WSH wrapping works
  auto p2wsh = BtcHtlcScript::redeemScriptToP2wshScriptPubKey(script);
  assert(p2wsh.size() == 34 && "P2WSH scriptPubKey must be 34 bytes");
  assert(p2wsh[0] == 0x00 && "P2WSH must start with OP_0");
  assert(p2wsh[1] == 0x20 && "P2WSH push must be 32 bytes");

  std::cout << "  PASS" << std::endl;
}

// =============================================================================
// Main
// =============================================================================

int main() {
  std::cout << "=== BTC HTLC Tests ===" << std::endl;

  test_btc_redeemScriptToP2wshScriptPubKey();
  test_btc_parseClaimPreimage_witness();
  test_btc_parseClaimPreimage_no_match();
  test_btc_parseClaimPreimage_refund_no_preimage();
  test_btc_parseClaimPreimage_not_segwit();
  test_btc_createHashTimeLockScript();

  std::cout << "\nAll BTC HTLC tests passed." << std::endl;
  return 0;
}
