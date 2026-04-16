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
#include <optional>

#include "Logging/ConsoleLogger.h"
#include "Logging/LoggerRef.h"

#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>

namespace {

const uint16_t DEFAULT_MAINNET_PORT = 18180;
const uint16_t DEFAULT_TESTNET_PORT = 28280;
const char* DEFAULT_HOST = "127.0.0.1";

void printUsage() {
  std::cout <<
    "Usage: xfg-swap [options] <command> [args...]\n"
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
    "  --data-dir <dir>        Data directory (default: ~/.xfg-swap)\n"
    "  --swap-config <file>    JSON config with chain RPC + signer keys\n"
    "  --testnet               Use testnet ports (fuegod: 28280)\n"
    "  --help                  Show this help message\n"
    "\n"
    "Pairs: SOL, ETH, XMR, BCH\n"
    "Amounts are in atomic units (1 XFG = 10,000,000 atomic)\n"
    "\n"
    "Examples:\n"
    "  xfg-swap initiate SOL 10000000 1000000000 192.168.1.100:9999\n"
    "  xfg-swap status a1b2c3d4e5f6\n"
    "  xfg-swap list\n"
    "  xfg-swap --testnet list\n"
    << std::endl;
}

std::string getDefaultDataDir() {
  const char* home = std::getenv("HOME");
  if (home) {
    return std::string(home) + "/.xfg-swap";
  }
  return "./.xfg-swap";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
  std::string host = DEFAULT_HOST;
  uint16_t port = DEFAULT_MAINNET_PORT;
  std::string dataDir = getDefaultDataDir();
  std::string swapConfigPath;
  bool testnet = false;

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
    } else if (opt == "--testnet") {
      testnet = true;
      port = DEFAULT_TESTNET_PORT;
    } else {
      std::cerr << "Unknown option: " << opt << std::endl;
      printUsage();
      return 1;
    }
    ++argIdx;
  }

  if (argIdx >= argc) {
    printUsage();
    return 1;
  }

  std::string command = argv[argIdx++];

  // Set up logging
  Logging::ConsoleLogger consoleLogger(Logging::INFO);
  Logging::LoggerRef logger(consoleLogger, "xfg-swap");

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

  // Dispatch command
  if (command == "initiate") {
    if (argIdx + 3 >= argc) {
      std::cerr << "Usage: xfg-swap initiate <pair> <xfg_amount> <ctr_amount> <peer>" << std::endl;
      return 1;
    }

    std::string pairStr = argv[argIdx++];
    std::string xfgAmountStr = argv[argIdx++];
    std::string ctrAmountStr = argv[argIdx++];
    std::string peer = argv[argIdx++];

    XfgSwap::SwapParams params;
    try {
      params.pair = XfgSwap::swapPairFromString(pairStr);
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }

    params.role = XfgSwap::SwapRole::BOB;
    params.xfgAmount = std::strtoull(xfgAmountStr.c_str(), nullptr, 10);
    params.ctrAmount = std::strtoull(ctrAmountStr.c_str(), nullptr, 10);
    params.peerEndpoint = peer;

    // Zero-init crypto fields
    std::memset(&params.aliceXfgPubKey, 0, sizeof(params.aliceXfgPubKey));
    std::memset(&params.bobXfgPubKey, 0, sizeof(params.bobXfgPubKey));
    std::memset(&params.ourSwapSecKey, 0, sizeof(params.ourSwapSecKey));
    std::memset(&params.ourSwapPubKey, 0, sizeof(params.ourSwapPubKey));
    std::memset(&params.peerSwapPubKey, 0, sizeof(params.peerSwapPubKey));
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

    if (!daemon.initiate(params)) {
      return 1;
    }

  } else if (command == "accept") {
    if (argIdx >= argc) {
      std::cerr << "Usage: xfg-swap accept <swap_id>" << std::endl;
      return 1;
    }
    std::string swapId = argv[argIdx++];
    if (!daemon.accept(swapId)) {
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
      std::cerr << "Usage: xfg-swap refund <swap_id>" << std::endl;
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
