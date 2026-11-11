// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "gtest/gtest.h"

#include "crypto/crypto.h"

using namespace Crypto;

namespace {

// Simulates the sender side of a standard CryptoNote stealth payment: given
// the recipient's public (view, spend) keys, produces a fresh tx keypair and
// the one-time output key for a given output index — exactly what a real
// wallet does when paying that address.
struct SenderOutput {
  PublicKey txPublicKey;
  PublicKey outputKey;
};

SenderOutput payTo(const PublicKey& recipientView, const PublicKey& recipientSpend, size_t outputIndex) {
  PublicKey txPub;
  SecretKey txSec;
  generate_keys(txPub, txSec);

  KeyDerivation derivation;
  EXPECT_TRUE(generate_key_derivation(recipientView, txSec, derivation));

  PublicKey outputKey;
  EXPECT_TRUE(derive_public_key(derivation, outputIndex, recipientSpend, outputKey));

  return {txPub, outputKey};
}

// The exact mechanism Blockchain.cpp's alias-registration fee check uses:
// given the tx's PUBLIC key and the recipient's SECRET view key (no sender
// secret involved), derive the expected output key and compare.
bool recipientRecognizesOutput(const PublicKey& txPublicKey, const SecretKey& recipientViewSecret,
                                const PublicKey& recipientSpend, size_t outputIndex,
                                const PublicKey& actualOutputKey) {
  KeyDerivation derivation;
  if (!generate_key_derivation(txPublicKey, recipientViewSecret, derivation)) {
    return false;
  }
  PublicKey expectedKey;
  if (!derive_public_key(derivation, outputIndex, recipientSpend, expectedKey)) {
    return false;
  }
  return expectedKey == actualOutputKey;
}

TEST(AliasFeeDestination, RecognizesAGenuinePayment) {
  PublicKey devView, devSpend;
  SecretKey devViewSecret, devSpendSecret;
  generate_keys(devView, devViewSecret);
  generate_keys(devSpend, devSpendSecret);

  auto output = payTo(devView, devSpend, /*outputIndex=*/1);

  EXPECT_TRUE(recipientRecognizesOutput(output.txPublicKey, devViewSecret, devSpend,
                                        /*outputIndex=*/1, output.outputKey));
}

// Bug this replaces: the old heuristic accepted ANY output >= the fee amount
// regardless of who it actually paid, so a self-paid change output
// satisfied it. The real derive-and-compare must reject that.
TEST(AliasFeeDestination, RejectsASelfPaidOutput) {
  PublicKey devView, devSpend;
  SecretKey devViewSecret, devSpendSecret;
  generate_keys(devView, devViewSecret);
  generate_keys(devSpend, devSpendSecret);

  PublicKey attackerView, attackerSpend;
  SecretKey attackerViewSecret, attackerSpendSecret;
  generate_keys(attackerView, attackerViewSecret);
  generate_keys(attackerSpend, attackerSpendSecret);

  // Attacker pays themself, not the dev fund, at the same output index.
  auto output = payTo(attackerView, attackerSpend, /*outputIndex=*/1);

  EXPECT_FALSE(recipientRecognizesOutput(output.txPublicKey, devViewSecret, devSpend,
                                         /*outputIndex=*/1, output.outputKey));
}

// The derivation is index-bound: a genuine payment must only match at its
// actual output index, guarding against an off-by-one in the integration.
TEST(AliasFeeDestination, OutputIndexMustMatchExactly) {
  PublicKey devView, devSpend;
  SecretKey devViewSecret, devSpendSecret;
  generate_keys(devView, devViewSecret);
  generate_keys(devSpend, devSpendSecret);

  auto output = payTo(devView, devSpend, /*outputIndex=*/2);

  EXPECT_FALSE(recipientRecognizesOutput(output.txPublicKey, devViewSecret, devSpend,
                                         /*outputIndex=*/0, output.outputKey));
  EXPECT_TRUE(recipientRecognizesOutput(output.txPublicKey, devViewSecret, devSpend,
                                        /*outputIndex=*/2, output.outputKey));
}

}  // namespace
