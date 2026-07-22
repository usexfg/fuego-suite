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

// Sia HTLC script test — known-answer tests for redeem script creation,
// address computation, and claim/refund conditions.

#include <cassert>
#include <string>
#include <vector>
#include <iostream>
#include "SwapDaemon/Sia/SiaHtlcScript.h"

using namespace XfgSwap;

int main() {
  // Test 1: SHA256 of known input
  {
    std::vector<uint8_t> input = {0x48, 0x65, 0x6c, 0x6c, 0x6f};  // "Hello"
    std::vector<uint8_t> hash = SiaHtlcScript::sha256(input);

    // SHA256("Hello") = 185f8db32271fe25f561a6fc938b2e264306ec304eda518007d1764826381969
    std::string expectedHex = "185f8db32271fe25f561a6fc938b2e264306ec304eda518007d1764826381969";
    std::string actualHex = SiaHtlcScript::bytesToHex(hash);
    assert(actualHex == expectedHex);
    std::cout << "Test 1 PASSED: SHA256 of known input" << std::endl;
  }

  // Test 2: Address computation from ed25519 public key
  {
    // Use a known ed25519 public key (32 bytes)
    std::vector<uint8_t> pubKey(32);
    for (int i = 0; i < 32; ++i) {
      pubKey[i] = static_cast<uint8_t>(i);
    }

    std::string address = SiaHtlcScript::computeAddress(pubKey);
    assert(!address.empty());
    assert(address.size() == 76);  // Sia addresses are 76 hex characters
    assert(address.substr(0, 2) == "00");  // Mainnet prefix (0x00)
    std::cout << "Test 2 PASSED: Address computation" << std::endl;
  }

  // Test 3: Address decode
  {
    std::vector<uint8_t> pubKey(32);
    for (int i = 0; i < 32; ++i) {
      pubKey[i] = static_cast<uint8_t>(i);
    }

    std::string address = SiaHtlcScript::computeAddress(pubKey);
    std::vector<uint8_t> unlockHash;
    bool ok = SiaHtlcScript::decodeAddress(address, unlockHash);
    assert(ok);
    assert(unlockHash.size() == 32);

    // Verify that the unlock hash matches SHA256(pubKey)
    std::vector<uint8_t> expectedHash = SiaHtlcScript::sha256(pubKey);
    assert(unlockHash == expectedHash);
    std::cout << "Test 3 PASSED: Address decode" << std::endl;
  }

  // Test 4: HTLC redeem script creation
  {
    std::vector<uint8_t> hashLockSha256(32, 0xAA);
    std::vector<uint8_t> recipientPubKey(32, 0xBB);
    std::vector<uint8_t> senderPubKey(32, 0xCC);
    uint32_t timeoutBlock = 1000;

    std::vector<uint8_t> redeemScript = SiaHtlcScript::createRedeemScript(
        hashLockSha256, recipientPubKey, senderPubKey, timeoutBlock);

    assert(!redeemScript.empty());
    assert(redeemScript[0] == SiaConstants::OP_IF);
    assert(redeemScript[1] == SiaConstants::OP_SHA256);

    std::cout << "Test 4 PASSED: HTLC redeem script creation" << std::endl;
  }

  // Test 5: Claim condition creation
  {
    std::vector<uint8_t> preimage(32, 0xDD);
    uint32_t currentBlockHeight = 1001;

    std::vector<uint8_t> condition = SiaHtlcScript::createClaimCondition(
        preimage, currentBlockHeight);

    assert(!condition.empty());
    std::cout << "Test 5 PASSED: Claim condition creation" << std::endl;
  }

  // Test 6: Refund condition creation
  {
    uint32_t timeoutBlock = 1000;

    std::vector<uint8_t> condition = SiaHtlcScript::createRefundCondition(timeoutBlock);

    assert(!condition.empty());
    std::cout << "Test 6 PASSED: Refund condition creation" << std::endl;
  }

  // Test 7: Hex conversion roundtrip
  {
    std::vector<uint8_t> original = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                     0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    std::string hex = SiaHtlcScript::bytesToHex(original);
    std::vector<uint8_t> decoded = SiaHtlcScript::hexToBytes(hex);
    assert(decoded == original);
    std::cout << "Test 7 PASSED: Hex conversion roundtrip" << std::endl;
  }

  // Test 8: Base64 encode/decode roundtrip
  {
    std::vector<uint8_t> original = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f,
                                     0x72, 0x6c, 0x64, 0x21};  // "Hello World!"
    std::string encoded = SiaHtlcScript::base64Encode(original);
    std::vector<uint8_t> decoded = SiaHtlcScript::base64Decode(encoded);
    assert(decoded == original);
    std::cout << "Test 8 PASSED: Base64 encode/decode roundtrip" << std::endl;
  }

  // Test 9: Address decode with invalid address
  {
    std::vector<uint8_t> unlockHash;
    bool ok = SiaHtlcScript::decodeAddress("invalid_address", unlockHash);
    assert(!ok);
    std::cout << "Test 9 PASSED: Address decode with invalid address" << std::endl;
  }

  // Test 10: Address decode with wrong prefix
  {
    std::vector<uint8_t> pubKey(32, 0x01);
    std::string address = SiaHtlcScript::computeAddress(pubKey);

    // Modify the address to have wrong prefix (change version byte from 00 to 01)
    if (address.size() >= 2) {
      address[0] = '0';
      address[1] = '1';
    }

    std::vector<uint8_t> unlockHash;
    bool ok = SiaHtlcScript::decodeAddress(address, unlockHash);
    assert(!ok);
    std::cout << "Test 10 PASSED: Address decode with wrong prefix" << std::endl;
  }

  std::cout << "\nAll tests PASSED!" << std::endl;
  return 0;
}
