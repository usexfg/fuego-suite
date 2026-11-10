// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.
//
// With --vectors N, prints N accepted claims as "T htlc recipient amount sig N r_hat"
// so the program's own arithmetic can be run on them.

#include <openssl/rand.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Common/StringTools.h"
#include "SwapDaemon/Solana/SolPointClaim.h"
#include "crypto/cross_dleq.h"

extern "C" {
#include "crypto/crypto-ops.h"
}

using namespace XfgSwap;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL  %s\n", label.c_str());
  }
}

struct Claim {
  Crypto::SecretKey t;
  Crypto::PublicKey T;
  SolEscrowBinding escrow;
  SolPointClaimProof proof;
};

static Claim newClaim() {
  Claim claim;
  Crypto::cross_dleq_generate_scalar(claim.t);
  Crypto::secret_key_to_public_key(claim.t, claim.T);
  RAND_bytes(claim.escrow.htlc.data(), 32);
  RAND_bytes(claim.escrow.recipient.data(), 32);
  claim.escrow.amount = 1000000000;
  proveSolPointClaim(claim.t, claim.T, claim.escrow, claim.proof);
  return claim;
}

static void testAccepted() {
  bool all = true;
  for (int round = 0; round < 64 && all; ++round) {
    const Claim claim = newClaim();
    all = solPointClaimAccepted(claim.T, claim.escrow, claim.proof);
  }
  check(all, "64 claims from the swap's t pass the program's rule");
}

static void testRefusals() {
  const Claim claim = newClaim();
  Claim other = newClaim();
  SolPointClaimProof proof;
  check(!proveSolPointClaim(other.t, claim.T, claim.escrow, proof), "another scalar does not prove the point");

  SolEscrowBinding otherEscrow = claim.escrow;
  otherEscrow.htlc[0] ^= 1;
  check(!solPointClaimAccepted(claim.T, otherEscrow, claim.proof), "a claim is bound to its escrow account");
  otherEscrow = claim.escrow;
  otherEscrow.recipient[7] ^= 1;
  check(!solPointClaimAccepted(claim.T, otherEscrow, claim.proof), "a claim is bound to its recipient");
  otherEscrow = claim.escrow;
  otherEscrow.amount += 1;
  check(!solPointClaimAccepted(claim.T, otherEscrow, claim.proof), "a claim is bound to its amount");
  check(!solPointClaimAccepted(other.T, claim.escrow, claim.proof), "a claim is bound to its point");

  SolPointClaimProof tampered = claim.proof;
  tampered.completedSignature[40] ^= 1;
  check(!solPointClaimAccepted(claim.T, claim.escrow, tampered), "a changed s is refused");
  tampered = claim.proof;
  tampered.nonce[3] ^= 1;
  check(!solPointClaimAccepted(claim.T, claim.escrow, tampered), "a changed nonce is refused");
  tampered = claim.proof;
  tampered.secretOffset[9] ^= 1;
  check(!solPointClaimAccepted(claim.T, claim.escrow, tampered),
        "an offset that does not recover t is refused (the program would store a wrong t)");
}

static void testRecoveredSecret() {
  const Claim claim = newClaim();
  uint8_t s[32];
  std::memcpy(s, claim.proof.completedSignature.data() + 32, 32);
  Crypto::SecretKey recovered;
  sc_sub(reinterpret_cast<uint8_t*>(&recovered), s, claim.proof.secretOffset.data());
  check(std::memcmp(&recovered, &claim.t, 32) == 0, "the secret the program stores, s - r_hat, is the swap's t");
}

static void printVectors(int count) {
  for (int i = 0; i < count; ++i) {
    const Claim claim = newClaim();
    std::printf("%s %s %s %llu %s %s %s\n", Common::podToHex(claim.T).c_str(),
                Common::toHex(claim.escrow.htlc.data(), 32).c_str(),
                Common::toHex(claim.escrow.recipient.data(), 32).c_str(),
                static_cast<unsigned long long>(claim.escrow.amount),
                Common::toHex(claim.proof.completedSignature.data(), 64).c_str(),
                Common::toHex(claim.proof.nonce.data(), 32).c_str(),
                Common::toHex(claim.proof.secretOffset.data(), 32).c_str());
  }
}

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--vectors") {
    printVectors(std::stoi(argv[2]));
    return 0;
  }
  testAccepted();
  testRefusals();
  testRecoveredSecret();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
