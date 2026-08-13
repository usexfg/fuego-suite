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

#include "SwapDaemon.h"
#include "SwapTypes.h"
#include "RpcServer.h"
#include "crypto/crypto.h"
#include "crypto/random.h"
#include "Crypto/Secp256k1Signer.h"
#include "SwapDaemon/Komodo/KmdHtlcScript.h"
#include "SwapDaemon/Decred/DcrHtlcScript.h"
#include <optional>
#include <random>
#include <array>

#include "Logging/ConsoleLogger.h"
#include "Logging/LoggerRef.h"

#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

#include "Common/StringTools.h"

namespace {

const uint16_t DEFAULT_MAINNET_PORT = 18180;
const uint16_t DEFAULT_TESTNET_PORT = 28280;
const char* DEFAULT_HOST = "127.0.0.1";

void printUsage() {
  std::cout <<
    "Usage: xfg-swapd [options] <command> [args...]\n"
    "\n"
    "Commands:\n"
    "  initiate <pair> <xfg_amount> <ctr_amount> <peer>  -- start a swap\n"
    "  accept <swap_id>                                    -- accept incoming swap\n"
    "  status [swap_id]                                    -- show swap(s) status\n"
    "  refund <swap_id>                                    -- force refund (if timeout elapsed)\n"
    "  list                                                -- list all swaps\n"
    "  check-timeouts                                      -- scan and refund expired swaps\n"
    "\n"
    "Options:\n"
    "  --fuegod-host <host>    Fuegod RPC host (default: 127.0.0.1)\n"
    "  --fuegod-port <port>    Fuegod RPC port (default: 18180)\n"
    "  --data-dir <dir>        Data directory (default: ~/.xfg-swapd)\n"
    "  --swap-config <file>    JSON config with chain RPC + signer keys\n"
    "  --offer-config <file>   JSON config for managed auto-pricing offers\n"
    "  --service               Run as background service (no interactive commands)\n"
    "  --auto-complete         Automatically complete swaps when counterparty reveals secret\n"
    "  --socks5-proxy <proxy>  Route swap P2P through SOCKS5 proxy (e.g. Tor: 127.0.0.1:9050)\n"
    "  --status-port <port>    Status endpoint for monitoring (default: 18900)\n"
    "  --rpc-port <port>       JSON-RPC server for wallet integration (default: 18902)\n"
    "  --rpc-token <token>     Require X-Swap-Token header on all JSON-RPC calls\n"
    "  --swap-p2p-port <port>  Swap peer protocol listen port (default: 18901, 0=off)\n"
    "  --swap-p2p-bind <addr>  P2P bind address (default: 127.0.0.1)\n"
    "  --testnet               Use testnet ports (fuegod: 28280)\n"
    "  --generate-spv-config   Generate SPV config template with fresh keys and addresses\n"
    "  --help                  Show this help message\n"
    "\n"
    "Pairs: SOL, ETH, XMR, BCH, ARB, BASE, KMD, BNB, DCR, BTC, LTC, POLYGON\n"
    "Amounts are in atomic units (1 XFG = 10,000,000 atomic)\n"
    "\n"
    "Examples:\n"
    "  xfg-swapd initiate SOL 10000000 1000000000 192.168.1.100:9999\n"
    "  xfg-swapd status a1b2c3d4e5f6\n"
    "  xfg-swapd list\n"
    "  xfg-swapd --testnet list\n"
    "  xfg-swapd --service --offer-config offers.json\n"
    << std::endl;
}

std::string getDefaultDataDir() {
  const char* home = std::getenv("HOME");
  if (home) {
    return std::string(home) + "/.xfg-swapd";
  }
  return "./.xfg-swapd";
}

// Generate a complete SPV config template with fresh keypairs for each chain.
// Prints JSON to stdout and exits.
static void generateSpvConfig() {
  using namespace CryptoNote::SwapDaemon::Crypto;

  auto genPrivKey = []() {
    std::array<uint8_t, 32> key{};
    Crypto::generate_random_bytes(32, key.data());
    return key;
  };

  Secp256k1Signer signer;

  auto pubkeyToAddress = [&](const std::array<uint8_t, 32>& privKey, uint8_t p2pkhVersion) {
    auto pubKey = signer.derivePublicKeyCompressed(privKey);
    auto hash = XfgSwap::KmdHtlcScript::hash160(pubKey);
    return XfgSwap::KmdHtlcScript::base58CheckEncode(p2pkhVersion, hash);
  };

  auto privKeyToWif = [&](const std::array<uint8_t, 32>& privKey, uint8_t wifVersion) {
    std::vector<uint8_t> privKeyVec(privKey.begin(), privKey.end());
    return XfgSwap::KmdHtlcScript::base58CheckEncode(wifVersion, privKeyVec);
  };

  auto btcKey = genPrivKey();
  auto ltcKey = genPrivKey();
  auto kmdKey = genPrivKey();
  auto bchKey = genPrivKey();

  auto btcWif = privKeyToWif(btcKey, 0x80);
  auto btcAddr = pubkeyToAddress(btcKey, 0x00);

  auto ltcWif = privKeyToWif(ltcKey, 0xB0);
  auto ltcAddr = pubkeyToAddress(ltcKey, 0x30);

  auto kmdWif = privKeyToWif(kmdKey, 0xBC);
  auto kmdAddr = pubkeyToAddress(kmdKey, 0x3C);

  auto bchWif = privKeyToWif(bchKey, 0x80);
  auto bchAddr = pubkeyToAddress(bchKey, 0x00);

  // DCR uses hex-encoded private key (not Base58Check WIF) — version prefix + 32 bytes key + compress flag
  auto dcrKey = genPrivKey();
  auto dcrPubKey = signer.derivePublicKeyCompressed(dcrKey);
  auto dcrPubKeyHash = XfgSwap::KmdHtlcScript::hash160(dcrPubKey);
  auto dcrAddr = XfgSwap::DcrHtlcScript::pubkeyHashToAddress(dcrPubKeyHash, false);
  std::vector<uint8_t> dcrWifBytes;
  dcrWifBytes.push_back(0x22);
  dcrWifBytes.insert(dcrWifBytes.end(), dcrKey.begin(), dcrKey.end());
  dcrWifBytes.push_back(0x01);
  auto dcrWif = XfgSwap::KmdHtlcScript::bytesToHex(dcrWifBytes);

  std::string nl = "\n";
  std::string tab = "  ";

  std::cout << R"({)" << nl
    << tab << R"(// ── SPV Configuration — Auto-generated by `xfg-swapd --generate-spv-config` ──)" << nl
    << tab << R"(// 1. Replace each `*_spv_server_0` with a working Electrum server for your region.)" << nl
    << tab << R"(// 2. Keep each `*_wif` as-is — these are freshly generated private keys.)" << nl
    << tab << R"(// 3. Fund each P2PKH address with the chain's native tokens so the daemon can)" << nl
    << tab << R"(//    claim HTLCs and receive refunds.)" << nl
    << tab << R"(// 4. Set `xfg_secret_key` to your XFG wallet's secret key.)" << nl
    << tab << R"(//)" << nl
    << tab << R"(// Find public Electrum servers: https://github.com/cipig/electrum-servers)" << nl
    << tab << R"(// Find public Neutrino servers: https://github.com/dcrlabs/neutrino-servers)" << nl
    << nl
    << tab << R"(// ── BTC ──)" << nl
    << tab << R"("btc_mode": "spv",)" << nl
    << tab << R"("btc_spv_server_0": "electrum.blockstream.info:50002",)" << nl
    << tab << R"("btc_spv_min_servers": 1,)" << nl
    << tab << R"("btc_spv_checkpoint_height": 800000,)" << nl
    << tab << R"("btc_spv_checkpoint_hash": "00000000000000000002a7c4c1e8d3c5d5e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b",)" << nl
    << tab << R"("btc_wif": ")" << btcWif << R"(",  // P2PKH address: )" << btcAddr << nl
    << nl
    << tab << R"(// ── LTC ──)" << nl
    << tab << R"("ltc_mode": "spv",)" << nl
    << tab << R"("ltc_spv_server_0": "ltc.merkle.io:50002",)" << nl
    << tab << R"("ltc_spv_min_servers": 1,)" << nl
    << tab << R"("ltc_wif": ")" << ltcWif << R"(",  // P2PKH address: )" << ltcAddr << nl
    << nl
    << tab << R"(// ── KMD ──)" << nl
    << tab << R"("kmd_mode": "spv",)" << nl
    << tab << R"("kmd_spv_server_0": "electrum1.cipig.net:10001",)" << nl
    << tab << R"("kmd_spv_min_servers": 1,)" << nl
    << tab << R"("kmd_wif": ")" << kmdWif << R"(",  // P2PKH address: )" << kmdAddr << nl
    << nl
    << tab << R"(// ── BCH ──)" << nl
    << tab << R"("bch_mode": "spv",)" << nl
    << tab << R"("bch_spv_server_0": "electroncash.org:50002",)" << nl
    << tab << R"("bch_spv_min_servers": 1,)" << nl
    << tab << R"("bch_wif": ")" << bchWif << R"(",  // P2PKH address: )" << bchAddr << nl
    << nl
    << tab << R"(// ── DCR ── (Neutrino SPV — read-only SPV; claim/refund needs RPC mode))" << nl
    << tab << R"("dcr_mode": "spv",)" << nl
    << tab << R"("dcr_spv_server_0": "localhost:19100",)" << nl
    << tab << R"("dcr_spv_min_servers": 1,)" << nl
    << tab << R"("dcr_wif": ")" << dcrWif << R"(",  // P2PKH address: )" << dcrAddr << nl
    << nl
    << tab << R"(// ── XFG ──)" << nl
    << tab << R"("xfg_secret_key": "<your_xfg_secret_key>",)" << nl
    << nl
    << tab << R"(// ── EVM chains (RPC-only, not SPV) ──)" << nl
    << tab << R"(// These still need RPC endpoints:)" << nl
    << tab << R"("eth_host": "",)" << nl
    << tab << R"("bsc_host": "",)" << nl
    << tab << R"("arb_host": "",)" << nl
    << tab << R"("base_host": "",)" << nl
    << tab << R"("poly_host": "")" << nl
    << R"(})" << std::endl;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
  // Make stdout/stderr line-buffered even when redirected to a file or pipe.
  // Without this, --service mode (long-running, no TTY) holds INFO logs in the
  // 4KB block buffer indefinitely — operators see "the daemon is silent" even
  // when it's running fine.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setvbuf(stderr, nullptr, _IOLBF, 0);

  std::string host = DEFAULT_HOST;
  uint16_t port = DEFAULT_MAINNET_PORT;
  std::string dataDir = getDefaultDataDir();
  std::string swapConfigPath;
  std::string offerConfigPath;
  uint16_t statusPort = 18900;
  uint16_t rpcPort = 18902;
  uint16_t p2pPort = 18901;
  std::string p2pBindAddr = "127.0.0.1";
  std::string rpcToken;
  bool testnet = false;
  bool serviceMode = false;
  bool autoComplete = false;
  std::string socks5Proxy;

  // Parse options (before the command)
  int argIdx = 1;
  while (argIdx < argc && argv[argIdx][0] == '-') {
    std::string opt = argv[argIdx];

    if (opt == "--help" || opt == "-h") {
      printUsage();
      return 0;
    } else if (opt == "--fuegod-host") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --fuegod-host requires an argument" << std::endl;
        return 1;
      }
      host = argv[argIdx];
    } else if (opt == "--fuegod-port") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --fuegod-port requires an argument" << std::endl;
        return 1;
      }
      port = static_cast<uint16_t>(std::atoi(argv[argIdx]));
    } else if (opt == "--data-dir") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --data-dir requires an argument" << std::endl;
        return 1;
      }
      dataDir = argv[argIdx];
    } else if (opt == "--swap-config") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --swap-config requires an argument" << std::endl;
        return 1;
      }
      swapConfigPath = argv[argIdx];
    } else if (opt == "--offer-config") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --offer-config requires an argument" << std::endl;
        return 1;
      }
      offerConfigPath = argv[argIdx];
    } else if (opt == "--service") {
      serviceMode = true;
    } else if (opt == "--auto-complete") {
      autoComplete = true;
    } else if (opt == "--socks5-proxy") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --socks5-proxy requires an argument (host:port)" << std::endl;
        return 1;
      }
      socks5Proxy = argv[argIdx];
    } else if (opt == "--status-port") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --status-port requires an argument" << std::endl;
        return 1;
      }
      statusPort = static_cast<uint16_t>(std::atoi(argv[argIdx]));
    } else if (opt == "--rpc-port") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --rpc-port requires an argument" << std::endl;
        return 1;
      }
      rpcPort = static_cast<uint16_t>(std::atoi(argv[argIdx]));
    } else if (opt == "--rpc-token") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --rpc-token requires an argument" << std::endl;
        return 1;
      }
      rpcToken = argv[argIdx];
    } else if (opt == "--swap-p2p-port") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --swap-p2p-port requires an argument" << std::endl;
        return 1;
      }
      p2pPort = static_cast<uint16_t>(std::atoi(argv[argIdx]));
    } else if (opt == "--swap-p2p-bind") {
      if (++argIdx >= argc) {
        std::cerr << "Error: --swap-p2p-bind requires an argument" << std::endl;
        return 1;
      }
      p2pBindAddr = argv[argIdx];
    } else if (opt == "--testnet") {
      testnet = true;
      port = DEFAULT_TESTNET_PORT;
    } else if (opt == "--generate-spv-config") {
      generateSpvConfig();
      return 0;
    } else {
      std::cerr << "Unknown option: " << opt << std::endl;
      printUsage();
      return 1;
    }
    ++argIdx;
  }

  // Set up logging
  Logging::ConsoleLogger consoleLogger(Logging::INFO);
  Logging::LoggerRef logger(consoleLogger, "xfg-swapd");

  if (testnet) {
    logger(Logging::INFO) << "Using testnet configuration";
  }

  // Create swap daemon (with optional chain client config)
  XfgSwap::SwapDaemon daemon = [&]() -> XfgSwap::SwapDaemon {
    if (!swapConfigPath.empty()) {
      XfgSwap::ChainClientConfig chainCfg;
      std::string errMsg;
      if (!XfgSwap::loadChainClientConfig(swapConfigPath, chainCfg, errMsg)) {
        std::cerr << "Error loading swap config: " << errMsg << std::endl;
        std::exit(1);
      }
      logger(Logging::INFO) << "Loaded chain client config from " << swapConfigPath;
      return XfgSwap::SwapDaemon(host, port, dataDir, consoleLogger, chainCfg);
    }
    return XfgSwap::SwapDaemon(host, port, dataDir, consoleLogger);
  }();

  // Load maker wallet key for signing managed offers
  if (!swapConfigPath.empty()) {
    std::ifstream f(swapConfigPath);
    if (f.is_open()) {
      std::stringstream ss; ss << f.rdbuf();
      std::string json = ss.str();
      auto getStr = [&](const std::string& key) -> std::string {
        std::string needle = "\"" + key + "\"";
        auto pos = json.find(needle);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return "";
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                      json[pos] == '\n' || json[pos] == '\r')) ++pos;
        if (pos >= json.size() || json[pos] != '"') return "";
        ++pos;
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
          if (json[pos] == '\\' && pos + 1 < json.size()) ++pos;
          result += json[pos++];
        }
        return result;
      };
      std::string xfgKeyHex = getStr("xfg_secret_key");
      if (!xfgKeyHex.empty()) {
        Crypto::SecretKey sk;
        Crypto::PublicKey pk;
        if (Common::podFromHex(xfgKeyHex, sk) && Crypto::secret_key_to_public_key(sk, pk)) {
          daemon.setMakerKeys(sk, pk);
          logger(Logging::INFO) << "Loaded XFG wallet key for offer signing";
        } else {
          logger(Logging::ERROR) << "Invalid xfg_secret_key in swap config";
        }
      }
    }
  }

  // Load offer config for auto-pricing
  if (!offerConfigPath.empty()) {
    if (daemon.loadOfferConfig(offerConfigPath)) {
      logger(Logging::INFO) << "Auto-pricing offer manager loaded";
    } else {
      logger(Logging::ERROR) << "Failed to load offer config: " << offerConfigPath;
    }
  }

  // Service mode: run tick loop continuously
  if (serviceMode) {
    logger(Logging::INFO) << "Starting as background service...";
    daemon.start(p2pPort, p2pBindAddr);

    if (!socks5Proxy.empty()) {
      daemon.setSocks5Proxy(socks5Proxy);
    }

    if (daemon.startStatusServer(statusPort)) {
      logger(Logging::INFO) << "Status endpoint: 127.0.0.1:" << statusPort;
    }

    // Start JSON-RPC server for wallet integration.
    // Loopback-only bind. When --rpc-token is set, every POST requires the
    // X-Swap-Token (or Bearer) header; without it, localhost is trusted.
    XfgSwap::RpcServer rpcServer(daemon, consoleLogger, rpcToken);
    if (rpcServer.start(rpcPort)) {
      logger(Logging::INFO) << "JSON-RPC server: 127.0.0.1:" << rpcPort
        << (rpcToken.empty() ? " (loopback trust)" : " (token auth enabled)");
    } else {
      logger(Logging::WARNING) << "Failed to start JSON-RPC server on port " << rpcPort;
    }

    if (p2pPort != 0) {
      logger(Logging::INFO) << "Swap P2P: " << p2pBindAddr << ":" << p2pPort;
    }

    if (autoComplete) {
      logger(Logging::INFO) << "Auto-complete enabled: will process swaps automatically";
    }

    // Service loop — if --auto-complete is set, periodically check for swaps
    // that can be advanced (e.g. counterparty revealed adaptor secret).
    while (true) {
      if (autoComplete) {
        try {
          daemon.checkTimeouts();
          // processSwap internally checks each non-terminal swap and advances it
          // when the counterparty's chain observation is available.
        } catch (const std::exception& e) {
          logger(Logging::WARNING) << "Auto-complete tick error: " << e.what();
        }
      }
      std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    return 0;
  }

  if (argIdx >= argc) {
    printUsage();
    return 1;
  }

  std::string command = argv[argIdx++];

  // Dispatch command
    if (command == "initiate") {
      if (argIdx + 4 >= argc) {
        std::cerr << "Usage: xfg-swapd initiate <pair> <xfg_amount> <ctr_amount> <peer> <expected_peer_pubkey_hex>" << std::endl;
        std::cerr << "  expected_peer_pubkey_hex: 64-char hex Ed25519 swap pubkey of counterparty" << std::endl;
        return 1;
      }

      std::string pairStr = argv[argIdx++];
      std::string xfgAmountStr = argv[argIdx++];
      std::string ctrAmountStr = argv[argIdx++];
      std::string peer = argv[argIdx++];
      std::string expectedPeerHex = argv[argIdx++];

      XfgSwap::SwapParams params;
      try {
        params.pair = XfgSwap::swapPairFromString(pairStr);
      } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
      }

      params.role = XfgSwap::SwapRole::BOB;
      // Validate amounts: strtoull silently returns 0 for "notanumber". Require
      // that endptr advanced past at least one digit AND the value is > 0.
      auto parseAmount = [](const std::string& s, const char* label, uint64_t& out) {
        if (s.empty()) {
          std::cerr << "Error: " << label << " is empty" << std::endl;
          return false;
        }
        char* endp = nullptr;
        unsigned long long v = std::strtoull(s.c_str(), &endp, 10);
        if (endp == s.c_str() || endp == nullptr || *endp != '\0') {
          std::cerr << "Error: " << label << " must be a positive integer (got '" << s << "')" << std::endl;
          return false;
        }
        if (v == 0) {
          std::cerr << "Error: " << label << " must be > 0" << std::endl;
          return false;
        }
        out = static_cast<uint64_t>(v);
        return true;
      };
      if (!parseAmount(xfgAmountStr, "xfg_amount", params.xfgAmount)) return 1;
      if (!parseAmount(ctrAmountStr, "ctr_amount", params.ctrAmount)) return 1;
      params.peerEndpoint = peer;

      // Zero-init crypto fields
      std::memset(&params.aliceXfgPubKey, 0, sizeof(params.aliceXfgPubKey));
      std::memset(&params.bobXfgPubKey, 0, sizeof(params.bobXfgPubKey));
      std::memset(&params.ourSwapSecKey, 0, sizeof(params.ourSwapSecKey));
      std::memset(&params.ourSwapPubKey, 0, sizeof(params.ourSwapPubKey));
      std::memset(&params.peerSwapPubKey, 0, sizeof(params.peerSwapPubKey));
      std::memset(&params.expectedPeerSwapPubKey, 0, sizeof(params.expectedPeerSwapPubKey));
      std::memset(&params.escrowPubKey, 0, sizeof(params.escrowPubKey));
      std::memset(&params.adaptorPoint, 0, sizeof(params.adaptorPoint));
      std::memset(&params.adaptorSecret, 0, sizeof(params.adaptorSecret));
      std::memset(&params.escrowTxHash, 0, sizeof(params.escrowTxHash));
      std::memset(&params.hashLock, 0, sizeof(params.hashLock));
      std::memset(&params.preimage, 0, sizeof(params.preimage));
      params.xfgTimeoutHeight = 0;  // will be set by daemon
      params.ctrTimeoutBlock = 0;
      params.escrowOutputIndex = 0;
      params.htlcOutputIndex = 0;

      if (!Common::podFromHex(expectedPeerHex, params.expectedPeerSwapPubKey)) {
        std::cerr << "Error: expected_peer_pubkey must be 64-char hex" << std::endl;
        return 1;
      }

      if (!daemon.initiate(params)) {
        return 1;
      }
      std::cout << "our_swap_pubkey=" << Common::podToHex(params.ourSwapPubKey) << std::endl;

    } else if (command == "accept") {
      if (argIdx >= argc) {
        std::cerr << "Usage: xfg-swapd accept <swap_id>" << std::endl;
        return 1;
      }
      std::string swapId = argv[argIdx++];
      auto result = daemon.accept(swapId);
      if (!result.success) {
        std::cerr << "Error: " << result.warning << std::endl;
        return 1;
      }

    } else if (command == "status") {
      if (argIdx < argc) {
        // Show specific swap
        std::string swapId = argv[argIdx++];
        daemon.showSwap(swapId);
      } else {
        // Show all swaps
        daemon.listSwaps();
      }

    } else if (command == "refund") {
      if (argIdx >= argc) {
        std::cerr << "Usage: xfg-swapd refund <swap_id>" << std::endl;
        return 1;
      }
      std::string swapId = argv[argIdx++];
      if (!daemon.refund(swapId)) {
        return 1;
      }

    } else if (command == "list") {
      daemon.listSwaps();

    } else if (command == "check-timeouts") {
      if (!daemon.checkTimeouts()) {
        return 1;
      }

    } else {
      std::cerr << "Unknown command: " << command << std::endl;
      printUsage();
      return 1;
    }


  return 0;
}
