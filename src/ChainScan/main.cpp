// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// fuego-chainscan: walks a node's chain over RPC and prints every transaction a
// node built from this tree would refuse for its subaddress extra, every parsed
// 0x06 / 0x09 field, and every commitment output. Exit status: 0 nothing found,
// 1 findings, 2 the scan could not complete.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include "Common/StringTools.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/Currency.h"
#include "Logging/ConsoleLogger.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"
#include "Rpc/HttpClient.h"
#include "Rpc/JsonRpc.h"
#include "System/Dispatcher.h"

#include "ChainScanner.h"

using namespace CryptoNote;

namespace {

struct ScanOptions {
  std::string host = "127.0.0.1";
  uint16_t port = static_cast<uint16_t>(RPC_DEFAULT_PORT);
  bool testnet = false;
  uint32_t fromHeight = 0;
  uint32_t toHeight = UINT32_MAX;
};


void printUsage() {
  std::cerr << "usage: fuego-chainscan [--daemon-address HOST:PORT] [--testnet] [--from HEIGHT] [--to HEIGHT]\n";
}

uint32_t parseHeight(const std::string& text) {
  size_t used = 0;
  const unsigned long long value = std::stoull(text, &used);
  if (used != text.size() || value > UINT32_MAX) {
    throw std::invalid_argument("bad height: " + text);
  }
  return static_cast<uint32_t>(value);
}

void parseAddress(const std::string& text, ScanOptions& options) {
  const size_t colon = text.rfind(':');
  if (colon == std::string::npos || colon == 0) {
    throw std::invalid_argument("expected HOST:PORT, got " + text);
  }
  const uint32_t port = parseHeight(text.substr(colon + 1));
  if (port == 0 || port > UINT16_MAX) {
    throw std::invalid_argument("bad port in " + text);
  }
  options.host = text.substr(0, colon);
  options.port = static_cast<uint16_t>(port);
}

ScanOptions parseOptions(int argc, char** argv) {
  ScanOptions options;
  bool addressGiven = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool hasValue = i + 1 < argc;
    if (arg == "--testnet") {
      options.testnet = true;
    } else if (arg == "--daemon-address" && hasValue) {
      parseAddress(argv[++i], options);
      addressGiven = true;
    } else if (arg == "--from" && hasValue) {
      options.fromHeight = parseHeight(argv[++i]);
    } else if (arg == "--to" && hasValue) {
      options.toHeight = parseHeight(argv[++i]);
    } else {
      throw std::invalid_argument("unknown or incomplete option: " + arg);
    }
  }
  if (options.testnet && !addressGiven) {
    options.port = static_cast<uint16_t>(RPC_DEFAULT_PORT_TESTNET);
  }
  if (options.fromHeight > options.toHeight) {
    throw std::invalid_argument("--from is above --to");
  }
  return options;
}

class RpcBlockSource : public IBlockSource {
public:
  RpcBlockSource(HttpClient& client, const Currency& currency) : m_client(client), m_currency(currency) {}

  BlockBatch blocksFrom(const Crypto::Hash& knownHash) override {
    COMMAND_RPC_GET_BLOCKS_FAST::request request;
    request.block_ids = {knownHash};
    if (knownHash != m_currency.genesisBlockHash()) {
      request.block_ids.push_back(m_currency.genesisBlockHash());
    }
    COMMAND_RPC_GET_BLOCKS_FAST::response response;
    invokeBinaryCommand(m_client, "/getblocks.bin", request, response);
    if (response.status != CORE_RPC_STATUS_OK) {
      throw std::runtime_error("node refused /getblocks.bin (" + response.status +
                               "); is it on the network this scan expects (--testnet)?");
    }
    BlockBatch batch;
    batch.blocks = std::move(response.blocks);
    batch.startHeight = response.start_height;
    batch.chainHeight = response.current_height;
    return batch;
  }

  Crypto::Hash hashAt(uint32_t height) override {
    COMMAND_RPC_GET_BLOCK_HEADER_BY_HEIGHT::request request;
    request.height = height;
    COMMAND_RPC_GET_BLOCK_HEADER_BY_HEIGHT::response response;
    JsonRpc::invokeJsonRpcCommand(m_client, "getblockheaderbyheight", request, response);
    Crypto::Hash hash;
    if (!Common::podFromHex(response.block_header.hash, hash)) {
      throw std::runtime_error("node returned a malformed block hash for height " + std::to_string(height));
    }
    return hash;
  }

private:
  HttpClient& m_client;
  const Currency& m_currency;
};

void printFinding(uint32_t height, const Crypto::Hash& txHash, const ChainScanFinding& finding) {
  std::cout << "height=" << height << " tx=" << Common::podToHex(txHash) << " finding=" << findingName(finding.kind)
            << " detail=\"" << finding.detail << "\"\n";
}

void printProgress(const std::string& message) {
  std::cerr << message << "\n";
}

} // namespace

int main(int argc, char** argv) {
  ScanOptions options;
  try {
    options = parseOptions(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    printUsage();
    return 2;
  }

  try {
    Logging::ConsoleLogger logger(Logging::ERROR);
    const Currency currency = CurrencyBuilder(logger).testnet(options.testnet).currency();
    System::Dispatcher dispatcher;
    HttpClient client(dispatcher, options.host, options.port);
    RpcBlockSource source(client, currency);
    const ScanTotals totals = scanChain(source, currency, ScanRange{options.fromHeight, options.toHeight},
                                        printFinding, printProgress);

    uint64_t findings = 0;
    std::cout << "blocks=" << totals.blocks << " transactions=" << totals.transactions;
    for (const auto& entry : totals.findings) {
      std::cout << " " << findingName(entry.first) << "=" << entry.second;
      findings += entry.second;
    }
    std::cout << "\n";
    return findings == 0 ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "scan failed: " << e.what() << "\n";
    return 2;
  }
}
