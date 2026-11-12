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

// F32: the AFK fill result carries the maker's UTXO claim key and is signed
// by the offer's maker over every field the taker locks against.

#include "Common/JsonValue.h"
#include "Common/StringTools.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/SwapOfferRelay.h"
#include "Logging/ConsoleLogger.h"
#include "SwapDaemon/RpcServer.h"
#include "SwapDaemon/SwapDaemon.h"
#include "crypto/crypto.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace XfgSwap;

namespace XfgSwap {
struct RpcServerTestAccess {
  static std::string dispatch(RpcServer& server, const std::string& request) { return server.dispatch(request); }
};
}  // namespace XfgSwap

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL: %s\n", label.c_str());
  }
}

static CryptoNote::SwapRequestResult sampleResult() {
  CryptoNote::SwapRequestResult result;
  result.offerId = "offer-1";
  result.lockId = "lock-1";
  result.makerEndpoint = "203.0.113.7:18888";
  result.adaptorPoint = std::string(64, 'a');
  result.hashLock = std::string(64, 'b');
  result.preSig = std::string(128, 'c');
  result.ctrAddress = "1BoatSLRHtKNngkdXEeobR76b53LETtpyT";
  result.ctrClaimPubKey = "02" + std::string(64, 'd');
  result.createdAt = 1700000000;
  return result;
}

static bool signedBy(const Crypto::PublicKey& maker, const std::string& taker,
                     const CryptoNote::SwapRequestResult& result, const Crypto::Signature& signature) {
  return Crypto::check_signature(CryptoNote::swapRequestResultHash(taker, result), maker, signature);
}

static void testSignatureBindsEveryField() {
  Crypto::PublicKey makerPub;
  Crypto::SecretKey makerSec;
  Crypto::generate_keys(makerPub, makerSec);
  const std::string taker(64, 'e');
  const CryptoNote::SwapRequestResult result = sampleResult();
  Crypto::Signature signature;
  Crypto::generate_signature(CryptoNote::swapRequestResultHash(taker, result), makerPub, makerSec, signature);
  check(signedBy(makerPub, taker, result, signature), "the maker's signature verifies");
  check(!signedBy(makerPub, std::string(64, 'f'), result, signature), "a result signed for one taker fails for another");

  using Mutation = std::function<void(CryptoNote::SwapRequestResult&)>;
  const std::vector<std::pair<std::string, Mutation>> mutations = {
      {"offer id", [](CryptoNote::SwapRequestResult& r) { r.offerId += "x"; }},
      {"lock id", [](CryptoNote::SwapRequestResult& r) { r.lockId += "x"; }},
      {"maker endpoint", [](CryptoNote::SwapRequestResult& r) { r.makerEndpoint = "198.51.100.1:18888"; }},
      {"adaptor point", [](CryptoNote::SwapRequestResult& r) { r.adaptorPoint[0] = '0'; }},
      {"hash lock", [](CryptoNote::SwapRequestResult& r) { r.hashLock[0] = '0'; }},
      {"pre-signature", [](CryptoNote::SwapRequestResult& r) { r.preSig[0] = '0'; }},
      {"counterparty address", [](CryptoNote::SwapRequestResult& r) { r.ctrAddress += "x"; }},
      {"claim key", [](CryptoNote::SwapRequestResult& r) { r.ctrClaimPubKey[2] = '0'; }},
      {"creation time", [](CryptoNote::SwapRequestResult& r) { r.createdAt += 1; }},
  };
  for (const auto& mutation : mutations) {
    CryptoNote::SwapRequestResult changed = result;
    mutation.second(changed);
    check(!signedBy(makerPub, taker, changed, signature), "a changed " + mutation.first + " breaks the signature");
  }

  CryptoNote::SwapRequestResult left = result;
  CryptoNote::SwapRequestResult right = result;
  left.offerId = "ab";
  left.lockId = "c";
  right.offerId = "a";
  right.lockId = "bc";
  check(CryptoNote::swapRequestResultHash(taker, left) != CryptoNote::swapRequestResultHash(taker, right),
        "field boundaries are part of the digest");

  Crypto::PublicKey otherPub;
  Crypto::SecretKey otherSec;
  Crypto::generate_keys(otherPub, otherSec);
  check(!signedBy(otherPub, taker, result, signature), "another key does not verify the maker's signature");
}

static void testAfkInitiateRefusedWhileDisabled() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() /
                       ("xfg-afk-fill-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Logging::ConsoleLogger logger(Logging::ERROR);
  {
    SwapDaemon daemon("127.0.0.1", 1, dir.string(), logger);
    RpcServer server(daemon, logger, std::string(64, 'a'));
    Crypto::PublicKey takerPub;
    Crypto::SecretKey takerSec;
    Crypto::generate_keys(takerPub, takerSec);
    Crypto::PublicKey makerPub;
    Crypto::SecretKey makerSec;
    Crypto::generate_keys(makerPub, makerSec);
    const std::string request =
        R"({"jsonrpc":"2.0","id":5,"method":"initiate_swap","params":{"pair":"btc","xfg_amount":"10000000",)"
        R"("ctr_amount":"100000","peer":"203.0.113.7:18888","role":"alice","afk":true,"swap_id":"lock-1",)"
        R"("expected_peer_pubkey":")" + Common::podToHex(makerPub) +
        R"(","our_swap_secret_key":")" + Common::podToHex(takerSec) + R"("}})";
    const Common::JsonValue response = Common::JsonValue::fromString(RpcServerTestAccess::dispatch(server, request));
    const std::string message =
        response.contains("error") ? response("error")("message").getString() : std::string("no error");
    const std::string expected = CryptoNote::parameters::AFK_SOFT_ORDER_EXECUTION_ENABLED
                                     ? "AFK fill result is missing"
                                     : "AFK soft-order execution is disabled";
    check(message.find(expected) != std::string::npos,
          "AFK initiate without a maker-signed fill result is refused (" + message + ")");
  }
  std::error_code ignored;
  fs::remove_all(dir, ignored);
}

int main() {
  testSignatureBindsEveryField();
  testAfkInitiateRefusedWhileDisabled();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
