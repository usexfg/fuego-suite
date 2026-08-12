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

#include <HTTP/httplib.h>
#include "Common/Int128.h"
#include "SwapDaemon.h"
#include "AdaptorSwap.h"
#include "SwapHashLock.h"
#include "SwapTimelock.h"
#include "SwapTxBuilder.h"
#include "../Treasury/VaultKeys.h"
#include "SwapPeerProtocol.h"
#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/SwapOfferRelay.h"
#include "crypto/hash.h"
#include "crypto/crypto.h"
#include "BitcoinCash/BchChainClient.h"
#include "Bitcoin/BtcRpcClient.h"
#include "Bitcoin/BtcChainClient.h"
#include "Litecoin/LtcRpcClient.h"
#include "Litecoin/LtcChainClient.h"
#include "Komodo/KmdRpcClient.h"
#include "Komodo/KmdChainClient.h"
#include "Ethereum/EthChainClient.h"
#include "Solana/SolChainClient.h"
#include "Monero/XmrChainClient.h"
#include "BSC/BscChainClient.h"
#include "Polygon/PolygonChainClient.h"
#include "Decred/DcrChainClient.h"
#include "Gleec/GleecChainClient.h"
// Extra EVM/UTXO chain clients (Robinhood, Plasma, Bob, etc.) are staged
// under SwapDaemon/*/ and chains-staging/; they are not registered until
// ChainClientConfig fields and SwapDaemonLib sources are complete.
#include "Spv/ElectrumSpvClient.h"
#include "Spv/Neutrino/NeutrinoSpvClient.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <thread>
#include <chrono>
#include "../Logging/ILogger.h"
#include "Ethereum/EthRpcClient.h"

namespace {
// Load HTLC deploy bytecode from a .bin path (hex text). Returns empty on failure.
std::string loadHtlcBytecode(const std::string& path) {
  if (path.empty()) return {};
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) return {};
  std::ostringstream ss;
  ss << ifs.rdbuf();
  std::string hex = ss.str();
  // Strip whitespace/newlines
  hex.erase(std::remove_if(hex.begin(), hex.end(),
                           [](unsigned char c) { return std::isspace(c) != 0; }),
            hex.end());
  // Drop optional 0x prefix
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
    hex = hex.substr(2);
  }
  return hex;
}

// Wire an EthRpcClient with optional HTLC bytecode + registry address.
void applyHtlcConfig(XfgSwap::EthRpcClient& rpc, const std::string& binPath,
                     const std::string& registry,
                     Logging::LoggerRef& logger, const char* chainLabel) {
  if (!registry.empty()) {
    rpc.setHtlcRegistry(registry);
    logger(Logging::INFO) << chainLabel << " HTLC registry set: " << registry;
  } else {
    logger(Logging::WARNING) << chainLabel
      << " eth_htlc_registry not set — lock/claim will fail until configured";
  }
  if (!binPath.empty()) {
    std::string bytecode = loadHtlcBytecode(binPath);
    if (!bytecode.empty()) {
      rpc.setHtlcBytecode(bytecode);
      logger(Logging::INFO) << chainLabel << " HTLC bytecode loaded ("
        << (bytecode.size() / 2) << " bytes) from " << binPath;
    }
  }
}
} // namespace

namespace XfgSwap {

// ── Base58 encode (Bitcoin alphabet, matches SolRpcClient) ───────────────
namespace {
static const char kBase58Alpha[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static std::string base58Encode(const std::vector<uint8_t>& data) {
  if (data.empty()) return "";
  size_t leadingZeros = 0;
  while (leadingZeros < data.size() && data[leadingZeros] == 0) ++leadingZeros;
  size_t maxChars = data.size() * 138 / 100 + 1;
  std::vector<uint8_t> buf(maxChars, 0);
  for (size_t i = 0; i < data.size(); ++i) {
    int carry = data[i];
    for (int j = static_cast<int>(maxChars) - 1; j >= 0; --j) {
      carry += 256 * buf[static_cast<size_t>(j)];
      buf[static_cast<size_t>(j)] = static_cast<uint8_t>(carry % 58);
      carry /= 58;
    }
  }
  size_t skip = 0;
  while (skip < maxChars && buf[skip] == 0) ++skip;
  std::string result(leadingZeros, '1');
  for (size_t i = skip; i < maxChars; ++i) result += kBase58Alpha[buf[i]];
  return result;
}

// Load Solana keypair JSON file (array of 64 integers [0..255]) → base58.
static std::string loadSolKeypairBase58(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) return "";
  std::string json((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
  size_t pos = json.find('[');
  if (pos == std::string::npos) return "";
  ++pos;
  std::vector<uint8_t> bytes;
  while (pos < json.size()) {
    if (json[pos] == ']') break;
    if (json[pos] == ',' || json[pos] == ' ' ||
        json[pos] == '\n' || json[pos] == '\t' || json[pos] == '\r') {
      ++pos; continue;
    }
    int val = 0;
    while (pos < json.size() &&
           std::isdigit(static_cast<unsigned char>(json[pos])))
      val = val * 10 + (json[pos++] - '0');
    bytes.push_back(static_cast<uint8_t>(val));
  }
  if (bytes.size() != 64) return "";
  return base58Encode(bytes);
}
} // anonymous namespace

SwapDaemon::SwapDaemon(const std::string& fuegodHost, uint16_t fuegodPort,
                        const std::string& dataDir, Logging::ILogger& logger)
  : m_rpc(fuegodHost, fuegodPort)
  , m_db(dataDir)
  , m_logger(logger, "SwapDaemon") {
  // Chain clients not configured — processSwap() will warn if needed.
}

SwapDaemon::SwapDaemon(const std::string& fuegodHost, uint16_t fuegodPort,
                        const std::string& dataDir, Logging::ILogger& logger,
                        const ChainClientConfig& chainCfg)
  : m_rpc(fuegodHost, fuegodPort)
  , m_db(dataDir)
  , m_logger(logger, "SwapDaemon") {
  if (!chainCfg.bchHost.empty() || chainCfg.bchMode == "spv") {
    if (chainCfg.bchMode == "spv" && !chainCfg.bchSpvServers.empty()) {
      // SPV mode: create ElectrumSpvClient with BCH checkpoints
      auto spvClient = std::make_shared<ElectrumSpvClient>(
          chainCfg.bchSpvServers,
          chainCfg.bchSpvMinServers,
          chainCfg.bchSpvCheckpointHeight,
          chainCfg.bchSpvCheckpointHash);
      m_chainRegistry.registerChain(SwapPair::BCH,
          std::make_unique<BchChainClient>(spvClient, chainCfg.bchWif));
      m_logger(Logging::INFO) << "BCH chain client registered: SPV mode ("
        << chainCfg.bchSpvServers.size() << " server(s))";
    } else if (!chainCfg.bchHost.empty()) {
      // Full-node RPC mode
      auto rpc = std::make_unique<BchRpcClient>(
          chainCfg.bchHost, chainCfg.bchPort,
          chainCfg.bchRpcUser, chainCfg.bchRpcPass);
      m_chainRegistry.registerChain(SwapPair::BCH,
          std::make_unique<BchChainClient>(std::move(rpc), chainCfg.bchWif));
      m_logger(Logging::INFO) << "BCH chain client registered: "
        << chainCfg.bchHost << ":" << chainCfg.bchPort;
    }
  }
  if (!chainCfg.ethHost.empty()) {
    std::unique_ptr<EthRpcClient> rpc;
    if (!chainCfg.ethPrivKeyHex.empty() && !chainCfg.ethAddress.empty()) {
      rpc = std::make_unique<EthRpcClient>(
          chainCfg.ethHost, chainCfg.ethPort,
          chainCfg.ethPrivKeyHex, chainCfg.ethAddress, chainCfg.ethChainId);
    } else {
      rpc = std::make_unique<EthRpcClient>(
          chainCfg.ethHost, chainCfg.ethPort);
    }
    applyHtlcConfig(*rpc, chainCfg.ethHtlcBinPath, chainCfg.ethHtlcRegistry, m_logger, "ETH");
    m_chainRegistry.registerChain(SwapPair::ETH,
        std::make_unique<EthChainClient>(std::move(rpc), chainCfg.ethAddress));
    m_logger(Logging::INFO) << "ETH chain client registered: "
      << chainCfg.ethHost << ":" << chainCfg.ethPort;
  }
  if (!chainCfg.arbHost.empty()) {
    std::unique_ptr<EthRpcClient> rpc;
    if (!chainCfg.arbPrivKeyHex.empty() && !chainCfg.arbAddress.empty()) {
      rpc = std::make_unique<EthRpcClient>(
          chainCfg.arbHost, chainCfg.arbPort,
          chainCfg.arbPrivKeyHex, chainCfg.arbAddress, chainCfg.arbChainId,
          EthTxType::Eip1559);
    } else {
      rpc = std::make_unique<EthRpcClient>(chainCfg.arbHost, chainCfg.arbPort);
    }
    applyHtlcConfig(*rpc,
                    chainCfg.arbHtlcBinPath.empty() ? chainCfg.ethHtlcBinPath : chainCfg.arbHtlcBinPath,
                    chainCfg.ethHtlcRegistry, m_logger, "ARB");
    m_chainRegistry.registerChain(SwapPair::ARB,
        std::make_unique<EthChainClient>(std::move(rpc), chainCfg.arbAddress, "ARB"));
    m_logger(Logging::INFO) << "ARB chain client registered: "
      << chainCfg.arbHost << ":" << chainCfg.arbPort
      << " (chainId=" << chainCfg.arbChainId << ")";
  }
  if (!chainCfg.baseHost.empty()) {
    std::unique_ptr<EthRpcClient> rpc;
    if (!chainCfg.basePrivKeyHex.empty() && !chainCfg.baseAddress.empty()) {
      rpc = std::make_unique<EthRpcClient>(
          chainCfg.baseHost, chainCfg.basePort,
          chainCfg.basePrivKeyHex, chainCfg.baseAddress, chainCfg.baseChainId,
          EthTxType::Eip1559);
    } else {
      rpc = std::make_unique<EthRpcClient>(chainCfg.baseHost, chainCfg.basePort);
    }
    applyHtlcConfig(*rpc,
                    chainCfg.baseHtlcBinPath.empty() ? chainCfg.ethHtlcBinPath : chainCfg.baseHtlcBinPath,
                    chainCfg.ethHtlcRegistry, m_logger, "BASE");
    m_chainRegistry.registerChain(SwapPair::BASE,
        std::make_unique<EthChainClient>(std::move(rpc), chainCfg.baseAddress, "BASE"));
    m_logger(Logging::INFO) << "BASE chain client registered: "
      << chainCfg.baseHost << ":" << chainCfg.basePort
      << " (chainId=" << chainCfg.baseChainId << ")";
  }
  if (!chainCfg.solHost.empty()) {
    auto rpc = std::make_unique<SolRpcClient>(
        chainCfg.solHost, chainCfg.solPort, chainCfg.solProgramId);
    std::string keypairBase58;
    if (!chainCfg.solKeypairPath.empty()) {
      keypairBase58 = loadSolKeypairBase58(chainCfg.solKeypairPath);
      if (!keypairBase58.empty()) {
        m_logger(Logging::INFO) << "Loaded SOL keypair from " << chainCfg.solKeypairPath;
      } else {
        m_logger(Logging::WARNING) << "Failed to load SOL keypair from " << chainCfg.solKeypairPath;
      }
    }
    m_chainRegistry.registerChain(SwapPair::SOL,
        std::make_unique<SolChainClient>(std::move(rpc), keypairBase58));
    m_logger(Logging::INFO) << "SOL chain client registered: "
      << chainCfg.solHost << ":" << chainCfg.solPort;
  }
  if (!chainCfg.xmrDaemonHost.empty()) {
    auto rpc = std::make_unique<MoneroRpcClient>(
        chainCfg.xmrDaemonHost, chainCfg.xmrDaemonPort,
        chainCfg.xmrWalletHost, chainCfg.xmrWalletPort);
    m_chainRegistry.registerChain(SwapPair::XMR,
        std::make_unique<XmrChainClient>(std::move(rpc),
            chainCfg.xmrSpendKeyHex, chainCfg.xmrViewKeyHex));
    m_logger(Logging::INFO) << "XMR chain client registered: "
      << chainCfg.xmrDaemonHost << ":" << chainCfg.xmrDaemonPort;
  }
  if (!chainCfg.bscHost.empty()) {
    std::unique_ptr<EthRpcClient> rpc;
    if (!chainCfg.bscPrivKeyHex.empty() && !chainCfg.bscAddress.empty()) {
      rpc = std::make_unique<EthRpcClient>(
          chainCfg.bscHost, chainCfg.bscPort,
          chainCfg.bscPrivKeyHex, chainCfg.bscAddress, chainCfg.bscChainId,
          EthTxType::Eip1559);
    } else {
      rpc = std::make_unique<EthRpcClient>(chainCfg.bscHost, chainCfg.bscPort);
    }
    applyHtlcConfig(*rpc,
                    chainCfg.bscHtlcBinPath.empty() ? chainCfg.ethHtlcBinPath : chainCfg.bscHtlcBinPath,
                    chainCfg.ethHtlcRegistry, m_logger, "BSC");
    m_chainRegistry.registerChain(SwapPair::BNB,
        std::make_unique<BscChainClient>(std::move(rpc), chainCfg.bscAddress));
    m_logger(Logging::INFO) << "BSC (BNB) chain client registered: "
      << chainCfg.bscHost << ":" << chainCfg.bscPort
      << " (chainId=" << chainCfg.bscChainId << ")";
  }
  if (!chainCfg.polyHost.empty()) {
    std::unique_ptr<EthRpcClient> rpc;
    if (!chainCfg.polyPrivKeyHex.empty() && !chainCfg.polyAddress.empty()) {
      rpc = std::make_unique<EthRpcClient>(
          chainCfg.polyHost, chainCfg.polyPort,
          chainCfg.polyPrivKeyHex, chainCfg.polyAddress, chainCfg.polyChainId,
          EthTxType::Eip1559);
    } else {
      rpc = std::make_unique<EthRpcClient>(chainCfg.polyHost, chainCfg.polyPort);
    }
    applyHtlcConfig(*rpc,
                    chainCfg.polyHtlcBinPath.empty() ? chainCfg.ethHtlcBinPath : chainCfg.polyHtlcBinPath,
                    chainCfg.ethHtlcRegistry, m_logger, "POLYGON");
    m_chainRegistry.registerChain(SwapPair::POLYGON,
        std::make_unique<PolygonChainClient>(std::move(rpc), chainCfg.polyAddress));
    m_logger(Logging::INFO) << "Polygon chain client registered: "
      << chainCfg.polyHost << ":" << chainCfg.polyPort
      << " (chainId=" << chainCfg.polyChainId << ")";
  }

  // GLEEC (Evmos fork) — EVM-compatible
  if (!chainCfg.gleecHost.empty()) {
    auto rpc = std::make_unique<EthRpcClient>(chainCfg.gleecHost, chainCfg.gleecPort,
        chainCfg.gleecPrivKeyHex, chainCfg.gleecAddress,
        chainCfg.gleecChainId);
    applyHtlcConfig(*rpc, chainCfg.gleecHtlcBinPath, chainCfg.ethHtlcRegistry, m_logger, "GLEEC");
    m_chainRegistry.registerChain(SwapPair::GLEEC,
        std::make_unique<GleecChainClient>(std::move(rpc), chainCfg.gleecAddress));
    m_logger(Logging::INFO) << "GLEEC chain client registered: "
      << chainCfg.gleecHost << ":" << chainCfg.gleecPort
      << " (chainId=" << chainCfg.gleecChainId << ")";
  }
  if (!chainCfg.dcrHost.empty() || chainCfg.dcrMode == "spv") {
    if (chainCfg.dcrMode == "spv" && !chainCfg.dcrSpvServers.empty()) {
      // SPV mode: create NeutrinoSpvClient with DCR checkpoints
      auto headerStore = std::make_shared<SpvHeaderStore>();
      if (chainCfg.dcrSpvCheckpointHeight > 0 && !chainCfg.dcrSpvCheckpointHash.empty()) {
        headerStore->anchor(chainCfg.dcrSpvCheckpointHeight, chainCfg.dcrSpvCheckpointHash);
      }
      headerStore->setMaxHeightDelta(2000);
      m_spvHeaderStores[SwapPair::DCR] = headerStore;
      auto spvClient = std::make_shared<NeutrinoSpvClient>(
          *headerStore,
          std::vector<SpvHeaderStore::Checkpoint>(),
          GcsFilterParams());
      auto rpc = std::make_unique<DcrRpcClient>(
          chainCfg.dcrHost, chainCfg.dcrPort,
          chainCfg.dcrRpcUser, chainCfg.dcrRpcPass);
      m_chainRegistry.registerChain(SwapPair::DCR,
          std::make_unique<DcrChainClient>(spvClient, std::move(rpc), chainCfg.dcrWif));
      m_logger(Logging::INFO) << "DCR chain client registered: SPV mode ("
        << chainCfg.dcrSpvServers.size() << " server(s))";
    } else if (!chainCfg.dcrHost.empty()) {
      // Full-node RPC mode
      auto rpc = std::make_unique<DcrRpcClient>(
          chainCfg.dcrHost, chainCfg.dcrPort,
          chainCfg.dcrRpcUser, chainCfg.dcrRpcPass);
      m_chainRegistry.registerChain(SwapPair::DCR,
          std::make_unique<DcrChainClient>(std::move(rpc), chainCfg.dcrWif));
      m_logger(Logging::INFO) << "DCR chain client registered: "
        << chainCfg.dcrHost << ":" << chainCfg.dcrPort;
    }
  }
  // BTC (Bitcoin — P2WSH SegWit)
  if (!chainCfg.btcHost.empty() || chainCfg.btcMode == "spv") {
    if (chainCfg.btcMode == "spv" && !chainCfg.btcSpvServers.empty()) {
      // SPV mode: create ElectrumSpvClient with BTC checkpoints
      auto spvClient = std::make_shared<ElectrumSpvClient>(
          chainCfg.btcSpvServers,
          chainCfg.btcSpvMinServers,
          chainCfg.btcSpvCheckpointHeight,
          chainCfg.btcSpvCheckpointHash);
      m_chainRegistry.registerChain(SwapPair::BTC,
          std::make_unique<BtcChainClient>(spvClient, chainCfg.btcWif));
      m_logger(Logging::INFO) << "BTC chain client registered: SPV mode ("
        << chainCfg.btcSpvServers.size() << " server(s))";
    } else if (!chainCfg.btcHost.empty()) {
      // Full-node RPC mode
      auto rpc = std::make_unique<BtcRpcClient>(
          chainCfg.btcHost, chainCfg.btcPort,
          chainCfg.btcRpcUser, chainCfg.btcRpcPass);
      m_chainRegistry.registerChain(SwapPair::BTC,
          std::make_unique<BtcChainClient>(std::move(rpc), chainCfg.btcWif));
      m_logger(Logging::INFO) << "BTC chain client registered: "
        << chainCfg.btcHost << ":" << chainCfg.btcPort;
    }
  }

  // LTC (Litecoin — P2WSH SegWit)
  if (!chainCfg.ltcHost.empty() || chainCfg.ltcMode == "spv") {
    if (chainCfg.ltcMode == "spv" && !chainCfg.ltcSpvServers.empty()) {
      // SPV mode: create ElectrumSpvClient with LTC checkpoints
      auto spvClient = std::make_shared<ElectrumSpvClient>(
          chainCfg.ltcSpvServers,
          chainCfg.ltcSpvMinServers,
          chainCfg.ltcSpvCheckpointHeight,
          chainCfg.ltcSpvCheckpointHash);
      m_chainRegistry.registerChain(SwapPair::LTC,
          std::make_unique<LtcChainClient>(spvClient, chainCfg.ltcWif));
      m_logger(Logging::INFO) << "LTC chain client registered: SPV mode ("
        << chainCfg.ltcSpvServers.size() << " server(s))";
    } else if (!chainCfg.ltcHost.empty()) {
      // Full-node RPC mode
      auto rpc = std::make_unique<LtcRpcClient>(
          chainCfg.ltcHost, chainCfg.ltcPort,
          chainCfg.ltcRpcUser, chainCfg.ltcRpcPass);
      m_chainRegistry.registerChain(SwapPair::LTC,
          std::make_unique<LtcChainClient>(std::move(rpc), chainCfg.ltcWif));
      m_logger(Logging::INFO) << "LTC chain client registered: "
        << chainCfg.ltcHost << ":" << chainCfg.ltcPort;
    }
  }

  // KMD (Komodo — P2SH, Bitcoin-like RPC)
  if (!chainCfg.kmdHost.empty() || chainCfg.kmdMode == "spv") {
    if (chainCfg.kmdMode == "spv" && !chainCfg.kmdSpvServers.empty()) {
      // SPV mode: create ElectrumSpvClient with KMD checkpoints
      auto spvClient = std::make_shared<ElectrumSpvClient>(
          chainCfg.kmdSpvServers,
          chainCfg.kmdSpvMinServers,
          chainCfg.kmdSpvCheckpointHeight,
          chainCfg.kmdSpvCheckpointHash);
      m_chainRegistry.registerChain(SwapPair::KMD_SPV,
          std::make_unique<KmdChainClient>(spvClient, chainCfg.kmdWif));
      m_logger(Logging::INFO) << "KMD chain client registered: SPV mode ("
        << chainCfg.kmdSpvServers.size() << " server(s))";
    } else if (!chainCfg.kmdHost.empty()) {
      // Full-node RPC mode
      auto rpc = std::make_unique<KmdRpcClient>(
          chainCfg.kmdHost, chainCfg.kmdPort,
          chainCfg.kmdRpcUser, chainCfg.kmdRpcPass);
      m_chainRegistry.registerChain(SwapPair::KMD_SPV,
          std::make_unique<KmdChainClient>(std::move(rpc), chainCfg.kmdWif));
      m_logger(Logging::INFO) << "KMD chain client registered: "
        << chainCfg.kmdHost << ":" << chainCfg.kmdPort;
    }
  }

  m_xfgWalletRpcHost = chainCfg.xfgWalletRpcHost;
  m_xfgWalletRpcPort = chainCfg.xfgWalletRpcPort;
  m_xfgWalletRpcUser = chainCfg.xfgWalletRpcUser;
  m_xfgWalletRpcPass = chainCfg.xfgWalletRpcPass;

  // Store view key for escrow funding derivation
  if (!chainCfg.xfgViewKeyHex.empty()) {
    Common::podFromHex(chainCfg.xfgViewKeyHex, m_makerViewSecretKey);
  }
}

SwapDaemon::~SwapDaemon() {
  stop();
  if (m_offerManager) {
    m_offerManager->shutdown();
  }
}

void SwapDaemon::start(uint16_t p2pPort, const std::string& p2pBind) {
  uint32_t currentHeight = 0;
  m_rpc.getHeight(currentHeight);

  // Derive escrow encryption key from maker wallet key (memory-hard KDF).
  // Never persisted — held only in m_db for this process lifetime.
  if (m_makerKeysSet) {
    std::string keyInput(Common::podToHex(m_makerSecretKey));
    keyInput += "::swap-escrow-enc-key";
    Crypto::cn_context ctx;
    Crypto::Hash derived;
    Crypto::cn_slow_hash(ctx, keyInput.data(), keyInput.size(), derived, 0, 0, 0);
    m_db.setEncryptionKey(std::string(reinterpret_cast<const char*>(derived.data), sizeof(derived.data)));
  }
  // NOTE: XFG_SWAP_ENC_KEY env var fallback removed for security.
  // The maker key is the only valid encryption key source.

  // Migrate terminal swaps to archive so the tick loop never loads them.
  m_db.migrateTerminalSwaps();

  std::vector<std::string> swapIds = m_db.listSwaps();
  int recovered = 0;
  int autoRefunded = 0;

  for (const auto& id : swapIds) {
    SwapStateMachine sm;
    if (m_db.loadSwap(id, sm) && !sm.isTerminal()) {
      SwapState state = sm.currentState();
      const auto& params = sm.params();

      if ((state == SwapState::AFK_OFFER_LOCKED || state == SwapState::AFK_OFFER_ACCEPTED) &&
          params.xfgTimeoutHeight > 0 && currentHeight >= params.xfgTimeoutHeight) {
        m_logger(Logging::WARNING) << "AFK swap " << id << " timed out during offline — auto-refunding";
        refund(id);
        autoRefunded++;
        continue;
      }

      m_logger(Logging::INFO) << "Recovered in-progress swap " << id
        << " state=" << swapStateToString(sm.currentState());
      recovered++;
    }
  }

  if (recovered > 0) {
    m_logger(Logging::INFO) << "Recovered " << recovered << " in-progress swap(s)";
  }
  if (autoRefunded > 0) {
    m_logger(Logging::INFO) << "Auto-refunded " << autoRefunded << " expired AFK swap(s)";
  }

  // Resubmit managed offers via swap relay
  if (m_offerManager && m_swapRelay) {
    m_offerManager->tick(currentHeight);
  }

  // Handle any swaps that expired while daemon was offline
  checkTimeouts();

  // Start swap peer P2P (signed PeerMessage transport)
  if (p2pPort != 0) {
    m_p2p = std::make_unique<SwapP2P>(p2pPort, p2pBind, m_logger);
    m_p2p->setMessageCallback([this](const SwapMessage& msg) {
      onP2pMessage(msg);
    });
    if (m_p2p->start()) {
      m_logger(Logging::INFO) << "SwapP2P listening on " << p2pBind << ":" << p2pPort;
    } else {
      m_logger(Logging::ERROR) << "SwapP2P failed to start on " << p2pBind << ":" << p2pPort;
      m_p2p.reset();
    }
  }

  m_running.store(true);
  m_tickThread = std::thread(&SwapDaemon::tickLoop, this);
  m_logger(Logging::INFO) << "SwapDaemon tick thread started (interval=" << TICK_INTERVAL_SECS << "s)";
}

void SwapDaemon::stop() {
  if (!m_running.exchange(false)) return;
  m_tickCv.notify_all();
  if (m_tickThread.joinable()) {
    m_tickThread.join();
  }
  if (m_p2p) {
    m_p2p->stop();
    m_p2p.reset();
  }
  m_logger(Logging::INFO) << "SwapDaemon tick thread stopped";
}

bool SwapDaemon::deliverPeerMessage(const PeerMessage& msg) {
  if (!m_p2p) {
    m_logger(Logging::WARNING) << "deliverPeerMessage: SwapP2P not running";
    return false;
  }
  SwapStateMachine sm;
  if (!m_db.loadSwap(msg.swapId, sm)) {
    m_logger(Logging::ERROR) << "deliverPeerMessage: unknown swap " << msg.swapId;
    return false;
  }
  const std::string& endpoint = sm.params().peerEndpoint;
  if (endpoint.empty()) {
    m_logger(Logging::WARNING) << "deliverPeerMessage: empty peerEndpoint for " << msg.swapId;
    return false;
  }
  SwapMessage wire;
  wire.type = SwapMsgType::PEER_PROTOCOL;
  wire.swapId = msg.swapId;
  wire.payload = serializePeerMessage(msg);
  if (!m_p2p->sendMessage(endpoint, wire)) {
    m_logger(Logging::ERROR) << "deliverPeerMessage: send failed to " << endpoint
      << " type=" << static_cast<int>(msg.type);
    return false;
  }
  m_logger(Logging::DEBUGGING) << "deliverPeerMessage: sent type="
    << static_cast<int>(msg.type) << " to " << endpoint;
  return true;
}

void SwapDaemon::onP2pMessage(const SwapMessage& msg) {
  // Accept PEER_PROTOCOL (full JSON) or legacy typed frames with JSON payload.
  if (msg.payload.empty()) return;
  PeerMessage peerMsg;
  if (!deserializePeerMessage(msg.payload, peerMsg)) {
    // Legacy: some frames may only carry payload fields; require full JSON.
    m_logger(Logging::WARNING) << "SwapP2P: dropped non-PeerMessage frame swapId="
      << msg.swapId << " type=" << static_cast<int>(msg.type);
    return;
  }
  if (peerMsg.swapId.empty())
    peerMsg.swapId = msg.swapId;
  if (!handlePeerMessage(peerMsg)) {
    m_logger(Logging::WARNING) << "SwapP2P: handlePeerMessage rejected type="
      << static_cast<int>(peerMsg.type) << " swapId=" << peerMsg.swapId;
  }
}

void SwapDaemon::recordCompletedTrade(const SwapStateMachine& sm) {
  const auto& p = sm.params();
  if (p.xfgAmount == 0 || p.ctrAmount == 0) return;

  double rate = 0.0;
  double div = PriceOracle::ctrDivisor(p.pair);
  if (div > 0.0 && p.ctrAmount > 0) {
    double wholeCtr = static_cast<double>(p.ctrAmount) / div;
    if (wholeCtr > 0.0)
      rate = (static_cast<double>(p.xfgAmount) / 1e7) / wholeCtr;
  }

  CompletedSwapTrade local;
  local.pair = p.pair;
  local.xfgAmount = p.xfgAmount;
  local.ctrAmount = p.ctrAmount;
  local.rate = rate;
  local.blockHeight = 0;
  local.timestamp = std::time(nullptr);
  m_oracle.recordCompletedSwap(local);

  // Node offer-relay TWAP only when attached (authenticated local path)
  if (m_swapRelay) {
    CryptoNote::SwapTradeRecord trade;
    trade.pair = static_cast<uint8_t>(p.pair);
    trade.xfgAmount = p.xfgAmount;
    trade.ctrAmount = p.ctrAmount;
    trade.rate = rate;
    trade.timestamp = static_cast<uint64_t>(local.timestamp);
    trade.blockHeight = 0;
    m_swapRelay->recordLocalTrade(trade);
  }
}

void SwapDaemon::tickLoop() {
  while (m_running.load()) {
    // Wait for TICK_INTERVAL_SECS or until stop() wakes us
    std::unique_lock<std::mutex> lock(m_tickMutex);
    m_tickCv.wait_for(lock, std::chrono::seconds(TICK_INTERVAL_SECS),
                      [this]{ return !m_running.load(); });
    if (!m_running.load()) break;

    // checkTimeouts handles refunds for all expired swaps
    checkTimeouts();

    // Process pending soft order requests from SwapOfferRelay
    if (m_swapRelay) {
      auto pendingRequests = m_swapRelay->getPendingSwapRequests();
      for (const auto& req : pendingRequests) {
        handleSwapRequest(req.offerId, req.amount, req.takerPubKey, req.proofOfFunds);
      }
    }

    // Auto-reprice managed offers
    if (m_offerManager) {
      uint32_t currentHeight = 0;
      m_rpc.getHeight(currentHeight);
      m_offerManager->tick(currentHeight);
    }

    // Fetch live XFG/USD from Hearth pool via fuegod RPC
    // Feed it back to fuegod (PI controller removed — value is ignored)
    {
      FuegoPrice livePrice;
      if (m_rpc.getFuegoPrice(livePrice) && livePrice.xfgSpotUsd > 0.0) {
        m_oracle.setLiveXfgUsd(livePrice.xfgSpotUsd);
        uint64_t marketValueCents = static_cast<uint64_t>(livePrice.xfgSpotUsd * 100.0);
        if (marketValueCents > 0)
          m_rpc.setXfgMarketValue(marketValueCents);
      }
    }

    // Prune stale taker history
    pruneTakerHistory();

    // Advance every non-terminal swap one step
    auto swapIds = m_db.listSwaps();
    for (const auto& id : swapIds) {
      SwapStateMachine sm;
      if (!m_db.loadSwap(id, sm)) continue;
      if (sm.isTerminal()) continue;
      processSwap(sm);
    }
  }
}

std::string SwapDaemon::generateSwapId() {
  struct {
    time_t timestamp;
    uint8_t random[32];
  } seed;

  seed.timestamp = std::time(nullptr);
  Crypto::generate_random_bytes(sizeof(seed.random), seed.random);

  Crypto::Hash hash;
  Crypto::cn_fast_hash(&seed, sizeof(seed), hash);

  return Common::toHex(hash.data, 16);
}

std::string SwapDaemon::resolveAddressOrAlias(const std::string& input) {
  if (input.empty()) return "";
  // XFG addresses are 98 chars and start with lowercase 'f' (e.g. fireVHx...)
  // Anything shorter or not starting with 'f' is treated as an alias candidate
  const bool looksLikeAlias = input.length() < 98 || input[0] != 'f';
  if (looksLikeAlias) {
    std::string candidate = input;
    if (!candidate.empty() && candidate[0] == '@') candidate = candidate.substr(1);
    std::string resolved;
    if (m_rpc.resolveAlias(candidate, resolved)) {
      return resolved;
    }
  }
  return input; // treat as raw address
}

bool SwapDaemon::initiate(SwapParams& params) {
  uint32_t currentHeight = 0;
  if (!m_rpc.getHeight(currentHeight)) {
    m_logger(Logging::ERROR) << "Cannot connect to fuegod";
    return false;
  }

  m_logger(Logging::INFO) << "Connected to fuegod at height " << currentHeight;

  params.ctrAddress = resolveAddressOrAlias(params.ctrAddress);

  if (params.swapId.empty()) {
    params.swapId = generateSwapId();
  }

  // Set default XFG timeout (cooperative refund window: ~1 day)
  if (params.xfgTimeoutHeight == 0) {
    params.xfgTimeoutHeight = currentHeight + 180;
  }

  // Timelock ordering: wall-clock comparison with per-chain block times.
  // XFG refund window must outlast the counterparty timeout by >= safety margin.
  {
    IChainClient* client = m_chainRegistry.getClient(params.pair);
    uint64_t ctrCurrentHeight = 0;
    bool ctrHeightOk = client && client->getCurrentHeight(ctrCurrentHeight) && ctrCurrentHeight > 0;

    if (params.ctrTimeoutBlock == 0) {
      // Caller didn't specify a CTR timeout — auto-derive a safe one.
      // CTR window = 50% of XFG remaining window (in wall-clock ms). The
      // subsequent timelockOrderingOk check verifies the margin holds.
      if (!ctrHeightOk) {
        m_logger(Logging::ERROR)
          << "ctrTimeoutBlock not set and counterparty chain height unavailable — cannot auto-derive";
        return false;
      }
      uint64_t xfgRemainingMs = (params.xfgTimeoutHeight - currentHeight) * 480000ULL;
      uint64_t ctrWindowMs = xfgRemainingMs / 2;
      uint64_t ctrBlocks = ctrWindowMs / msPerBlock(params.pair);
      if (ctrBlocks == 0) ctrBlocks = 1;
      params.ctrTimeoutBlock = ctrCurrentHeight + ctrBlocks;
      m_logger(Logging::INFO)
        << "Auto-derived ctrTimeoutBlock=" << params.ctrTimeoutBlock
        << " (" << ctrBlocks << " " << swapPairToString(params.pair)
        << " blocks ≈ " << (ctrWindowMs / 60000ULL) << " min wall-clock)";
    }

    if (!ctrHeightOk) {
      // Fail closed: never fund escrow without a verified wall-clock ordering.
      m_logger(Logging::ERROR)
        << "Cannot query counterparty chain height — refusing initiate (timelock check required)";
      return false;
    }
    if (!timelockOrderingOk(params.pair, currentHeight, params.xfgTimeoutHeight,
                             ctrCurrentHeight, params.ctrTimeoutBlock)) {
      m_logger(Logging::ERROR)
        << "Timelock ordering violation: XFG window ("
        << (params.xfgTimeoutHeight - currentHeight) << " blocks) must outlast "
        << swapPairToString(params.pair) << " timeout by >= "
        << (DEFAULT_SAFETY_MARGIN_SEC / 3600) << "h in wall-clock";
      return false;
    }
  }

  // Validate price against TWAP
  RateCheck rc = m_oracle.validateSwapAmounts(params.pair, params.xfgAmount, params.ctrAmount);
  if (rc == RateCheck::BELOW_FLOOR) {
    m_logger(Logging::ERROR)
      << "Swap rate rejected: XFG priced >= 50% below TWAP floor. "
      << PriceOracle::rateCheckToString(rc);
    return false;
  }
  if (rc == RateCheck::ABOVE_MARKET) {
    m_logger(Logging::WARNING)
      << "Swap rate is significantly above market TWAP. Proceeding.";
  }
  if (rc == RateCheck::RATE_NO_DATA) {
    m_logger(Logging::INFO)
      << "No TWAP data yet (bootstrap mode). Seed rate: "
      << PriceOracle::getSeedRate(params.pair) << " XFG per 1 "
      << swapPairToString(params.pair);
  }

  // ── Adaptor sig step 1: generate swap keypair ──
  adaptor_generate_keys(params);

  m_logger(Logging::DEBUGGING) << "Generated swap keypair: "
    << Common::podToHex(params.ourSwapPubKey);

  // expectedPeerSwapPubKey: if caller pre-set it, KEY_EXCHANGE must match.
  // If peerSwapPubKey was already known (offer), promote it to expected.
  {
    static const Crypto::PublicKey ZERO{};
    if (std::memcmp(&params.expectedPeerSwapPubKey, &ZERO, sizeof(ZERO)) == 0 &&
        std::memcmp(&params.peerSwapPubKey, &ZERO, sizeof(ZERO)) != 0) {
      params.expectedPeerSwapPubKey = params.peerSwapPubKey;
    }
    if (std::memcmp(&params.expectedPeerSwapPubKey, &ZERO, sizeof(ZERO)) == 0) {
      m_logger(Logging::WARNING)
        << "No expectedPeerSwapPubKey set — first KEY_EXCHANGE will bind any valid signer "
           "(set expected_peer_pubkey on initiate for anti-griefing)";
    } else {
      m_logger(Logging::INFO) << "  Expected peer pubkey bound: "
        << Common::podToHex(params.expectedPeerSwapPubKey);
    }
  }

  SwapStateMachine sm(params);

  if (!m_db.saveSwap(sm)) {
    m_logger(Logging::ERROR) << "Failed to save swap to database";
    return false;
  }

  // Deliver our KEY_EXCHANGE so peer can bind us (they should set us as expected).
  if (!params.peerEndpoint.empty()) {
    PeerMessage kx;
    kx.type = PeerMessageType::KEY_EXCHANGE;
    kx.swapId = params.swapId;
    kx.keyExchange.swapPubKey = params.ourSwapPubKey;
    if (signPeerMessage(kx, params.ourSwapPubKey, params.ourSwapSecKey)) {
      if (deliverPeerMessage(kx)) {
        m_logger(Logging::INFO) << "Delivered KEY_EXCHANGE to peer";
      } else {
        m_logger(Logging::WARNING) << "KEY_EXCHANGE delivery failed — peer may connect later";
      }
    }
  }

  m_logger(Logging::INFO) << "Swap initiated: " << params.swapId;
  m_logger(Logging::INFO) << "  Pair: XFG/" << swapPairToString(params.pair);
  m_logger(Logging::INFO) << "  Role: " << (params.role == SwapRole::BOB ? "BOB (selling XFG)" : "ALICE (buying XFG)");
  m_logger(Logging::DEBUGGING) << "  XFG amount: " << params.xfgAmount << " atomic";
  m_logger(Logging::DEBUGGING) << "  CTR amount: " << params.ctrAmount << " atomic";
  m_logger(Logging::INFO) << "  Timeout height: " << params.xfgTimeoutHeight;
  m_logger(Logging::INFO) << "  Our swap pubkey (give to peer as expected_peer_pubkey): "
    << Common::podToHex(params.ourSwapPubKey);
  m_logger(Logging::INFO) << "  Share this swap ID with your counterparty: " << params.swapId;

  return true;
}

SwapDaemon::AcceptResult SwapDaemon::accept(const std::string& swapId) {
  SwapStateMachine sm;
  if (!m_db.loadSwap(swapId, sm)) {
    const std::string msg = "Swap not found: " + swapId;
    m_logger(Logging::ERROR) << msg;
    return {false, msg};
  }

  if (sm.currentState() != SwapState::INITIATED &&
      sm.currentState() != SwapState::AFK_OFFER_LOCKED) {
    const std::string msg = std::string("Swap is not in a state that can be accepted (current: ")
                            + swapStateToString(sm.currentState()) + ")";
    m_logger(Logging::ERROR) << msg;
    return {false, msg};
  }
  
  auto& params = sm.params();
  std::string warning = "";

  // Peer identity: if already bound, lock expected to that key.
  // If expected is set and peer is set, they must match.
  {
    static const Crypto::PublicKey ZERO{};
    const bool hasPeer = std::memcmp(&params.peerSwapPubKey, &ZERO, sizeof(ZERO)) != 0;
    const bool hasExpected = std::memcmp(&params.expectedPeerSwapPubKey, &ZERO, sizeof(ZERO)) != 0;
    if (hasPeer && !hasExpected) {
      params.expectedPeerSwapPubKey = params.peerSwapPubKey;
      m_logger(Logging::INFO) << "  Locked expectedPeerSwapPubKey from bound peer";
    } else if (hasPeer && hasExpected &&
               std::memcmp(&params.peerSwapPubKey, &params.expectedPeerSwapPubKey,
                           sizeof(Crypto::PublicKey)) != 0) {
      const std::string msg = "peerSwapPubKey does not match expectedPeerSwapPubKey — refusing accept";
      m_logger(Logging::ERROR) << msg;
      return {false, msg};
    }
  }

  uint32_t currentHeight = 0;
  m_rpc.getHeight(currentHeight);
  
    // AFK Safety Check: Check remaining time of Maker's lock
    if (sm.currentState() == SwapState::AFK_OFFER_LOCKED) {
      uint32_t currentHeight = 0;
      if (!m_rpc.getHeight(currentHeight)) {
        m_logger(Logging::ERROR) << "Cannot query fuegod height";
        return {false, ""};
      }
      
      // Fuego block time = 480s (8 min). 1 hour = 7.5 blocks. Use 8 blocks as safe minimum.
      int32_t remainingBlocks = static_cast<int32_t>(params.xfgTimeoutHeight) - currentHeight;
      
      if (remainingBlocks < 8) {
        m_logger(Logging::ERROR) << "AFK offer expired or too close to expiry (" 
                                 << remainingBlocks << " blocks left). Acceptance rejected.";
        return {false, ""};
      }
      
      if (remainingBlocks < 64) { // 8 hours = 64 blocks (8 * 8)
        double remainingHrs = remainingBlocks / 7.5;
        warning = "This offer is under 8 hours from expiry. Please be aware you only have " 
                  + std::to_string(remainingHrs) + " hours to claim your funds before maker has access to funds again to initiate a refund.";
      }
    }


  // Timelock ordering check
  {
    if (params.ctrTimeoutBlock == 0) {
      const std::string msg = "ctrTimeoutBlock not set in offer — cannot verify timelock ordering";
      m_logger(Logging::ERROR) << msg;
      return {false, msg};
    }
    IChainClient* client = m_chainRegistry.getClient(params.pair);
    uint64_t ctrCurrentHeight = 0;
    if (!client || !client->getCurrentHeight(ctrCurrentHeight) || ctrCurrentHeight == 0) {
      const std::string msg = "Cannot query counterparty chain height — refusing accept (timelock check required)";
      m_logger(Logging::ERROR) << msg;
      return {false, msg};
    }
    if (!timelockOrderingOk(params.pair, currentHeight, params.xfgTimeoutHeight,
                             ctrCurrentHeight, params.ctrTimeoutBlock)) {
      const std::string msg = "Timelock ordering violation: XFG window must outlast counterparty timeout";
      m_logger(Logging::ERROR) << msg;
      return {false, msg};
    }
  }
  
  // ── Adaptor sig step 2: key aggregation ──
  if (!adaptor_key_aggregate(params)) {
    m_logger(Logging::ERROR) << "Musig2 key aggregation failed";
    return {false, ""};
  }
  
  m_logger(Logging::DEBUGGING) << "Musig2 escrow key: "
    << Common::podToHex(params.escrowPubKey);

  if (params.role == SwapRole::BOB) {
    if (!adaptor_generate_adaptor(params, params.escrowPubKey)) {
      m_logger(Logging::ERROR) << "Adaptor point generation failed";
      return {false, ""};
    }
    m_logger(Logging::DEBUGGING) << "Adaptor point T: "
      << Common::podToHex(params.adaptorPoint);
  }
  
  SwapState newState = (sm.currentState() == SwapState::AFK_OFFER_LOCKED) 
                       ? SwapState::AFK_OFFER_ACCEPTED 
                       : SwapState::ADAPTOR_KEYS_EXCHANGED;

  if (!sm.transition(newState)) {
    m_logger(Logging::ERROR) << "State transition failed to " << swapStateToString(newState);
    return {false, ""};
  }
  
  if (!m_db.saveSwap(sm)) {
    m_logger(Logging::ERROR) << "Failed to save swap state";
    return {false, ""};
  }

  // Bob delivers T + DLEQ + H(t) so Alice can lock CTR without learning t.
  if (params.role == SwapRole::BOB && !params.peerEndpoint.empty()) {
    PeerMessage ax;
    ax.type = PeerMessageType::ADAPTOR_EXCHANGE;
    ax.swapId = params.swapId;
    ax.adaptorExchange.adaptorPoint = params.adaptorPoint;
    ax.adaptorExchange.adaptorDleqQ = params.adaptorDleqQ;
    ax.adaptorExchange.dleqProof = params.adaptorDleqProof;
    ax.adaptorExchange.htlcHashLock = params.hashLock;
    signPeerMessage(ax, params.ourSwapPubKey, params.ourSwapSecKey);
    if (deliverPeerMessage(ax)) {
      m_logger(Logging::INFO) << "Delivered ADAPTOR_EXCHANGE (T + H(t)) to peer";
    } else {
      m_logger(Logging::WARNING) << "ADAPTOR_EXCHANGE delivery failed — peer may retry later";
    }
  }
  
  m_logger(Logging::INFO) << "Swap " << swapId << " -> " << swapStateToString(sm.currentState());
  return {true, warning};
}


void SwapDaemon::checkStuckSwaps() {
  time_t now = std::time(nullptr);
  for (const auto& id : m_db.listSwaps()) {
    SwapStateMachine sm;
    if (!m_db.loadSwap(id, sm)) continue;
    if (sm.isTerminal()) continue;

    time_t age = now - sm.updatedAt();
    int threshold = (sm.currentState() >= SwapState::ADAPTOR_ESCROW_FUNDED)
      ? SWAP_STUCK_THRESHOLD_ESCROW_SECS
      : SWAP_STUCK_THRESHOLD_KEYS_SECS;

    if (age > threshold) {
      m_logger(Logging::WARNING) << "Swap " << id
        << " stuck in state " << swapStateToString(sm.currentState())
        << " for " << age << "s — consider cooperative refund";
    }
  }
}

bool SwapDaemon::checkTimeouts() {
  uint32_t currentHeight = 0;
  if (!m_rpc.getHeight(currentHeight)) {
    m_logger(Logging::ERROR) << "Cannot query fuegod height";
    return false;
  }

  auto swapIds = m_db.listSwaps();
  bool anyExpired = false;

  for (const auto& swapId : swapIds) {
    SwapStateMachine sm;
    if (!m_db.loadSwap(swapId, sm)) continue;
    if (sm.isTerminal()) continue;

    const auto& params = sm.params();

    if (params.xfgTimeoutHeight > 0 && currentHeight >= params.xfgTimeoutHeight) {
      SwapState current = sm.currentState();

      // Cooperative refund possible from escrow-funded or pre-sigs-ready states.
      // Do NOT transition to REFUNDED here — a real cooperative refund requires
      // exchanging a Musig2 partial with the peer and broadcasting the refund
      // tx. checkTimeouts() only flags the opportunity; the user (or a future
      // peer-protocol layer) must call refund() to actually execute it.
      if ((current == SwapState::ADAPTOR_ESCROW_FUNDED ||
           current == SwapState::ADAPTOR_PRESIGS_READY) &&
          params.role == SwapRole::BOB) {
        m_logger(Logging::WARNING) << "Swap " << swapId
          << " XFG timeout reached at height " << currentHeight
          << " — call 'refund " << swapId << "' to initiate cooperative refund.";
        anyExpired = true;
      }

      if (current == SwapState::AFK_OFFER_LOCKED ||
          current == SwapState::AFK_OFFER_ACCEPTED) {
        uint32_t threshold = (params.xfgTimeoutHeight > currentHeight)
                             ? (params.xfgTimeoutHeight - currentHeight) : 0;
        if (threshold < 2) {
          m_logger(Logging::WARNING) << "AFK swap " << swapId
            << " timeout imminent at height " << currentHeight
            << " (deadline: " << params.xfgTimeoutHeight << ") — attempting auto-refund";
          refund(swapId);
        } else {
          m_logger(Logging::INFO) << "AFK swap " << swapId
            << " state=" << swapStateToString(current)
            << " remaining=" << threshold << " blocks";
        }
      }
    }
  }

  if (!anyExpired) {
    m_logger(Logging::DEBUGGING) << "No swaps timed out at height " << currentHeight;
  }

  checkStuckSwaps();

  return true;
}

bool SwapDaemon::processSwap(const std::string& swapId) {
  SwapStateMachine sm;
  if (!m_db.loadSwap(swapId, sm)) {
    m_logger(Logging::ERROR) << "Swap not found: " << swapId;
    return false;
  }
  return processSwap(sm);
}

// ── Escrow funding ─────────────────────────────────────────────────────────

bool SwapDaemon::fundEscrow(SwapParams& params) {
  m_logger(Logging::INFO) << "  Funding escrow";
  m_logger(Logging::DEBUGGING) << "  Escrow amount: " << params.xfgAmount
    << " atomic -> " << Common::podToHex(params.escrowPubKey);

  if (!m_makerKeysSet) {
    m_logger(Logging::ERROR) << "  Maker keys not set";
    return false;
  }

  // Configure wallet RPC from config
  if (!m_xfgWalletRpcHost.empty() && m_xfgWalletRpcPort != 0) {
    m_rpc.setWalletRpc(m_xfgWalletRpcHost, m_xfgWalletRpcPort);
    if (!m_xfgWalletRpcUser.empty())
      m_rpc.setWalletAuth(m_xfgWalletRpcUser, m_xfgWalletRpcPass);
  } else {
    m_logger(Logging::ERROR) << "  Wallet RPC not configured";
    return false;
  }

  // 1. Create a known output via optimize RPC
  TransferResult opt;
  if (!m_rpc.optimizeWallet(params.xfgAmount, opt)) {
    m_logger(Logging::ERROR) << "  optimize RPC failed";
    return false;
  }
  m_logger(Logging::INFO) << "  Optimize tx: " << opt.txHash;

  // 2. Derive txPubKey and one-time secret
  Crypto::SecretKey txSec;
  if (!Common::podFromHex(opt.txSecretKey, txSec)) return false;
  Crypto::PublicKey txPubKey;
  if (!Crypto::secret_key_to_public_key(txSec, txPubKey)) return false;

  Crypto::KeyDerivation derivation;
  if (!Crypto::generate_key_derivation(txPubKey, m_makerViewSecretKey, derivation))
    return false;

  // 3. Poll for the optimize output global index.
  // The optimize tx creates outputs at various amounts. We try all output
  // indices (0..9) in the derivation and match against on-chain outputs.
  Crypto::PublicKey derivedKey;
  Crypto::SecretKey outputSecret;
  Crypto::KeyImage keyImage;
  uint32_t realGI = 0;
  bool found = false;
  size_t foundOutIdx = 0;

  for (int retry = 0; retry < 200 && !found; ++retry) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::vector<TxOutputInfo> optOuts;
    if (m_rpc.getTransactionOutputs(opt.txHash, optOuts) && !optOuts.empty()) {
      for (size_t outIdx = 0; outIdx < 10 && !found; ++outIdx) {
        Crypto::derive_public_key(derivation, outIdx, m_makerPublicKey, derivedKey);
        for (size_t i = 0; i < optOuts.size(); ++i) {
          if (std::memcmp(&optOuts[i].targetKey, &derivedKey,
                          sizeof(Crypto::PublicKey)) == 0) {
            foundOutIdx = outIdx;
            uint64_t realAmt = optOuts[i].amount;
            std::vector<RandomOutputEntry> outs;
            if (m_rpc.getRandomOutputs(realAmt, 100, outs)) {
              for (auto& o : outs) {
                if (std::memcmp(&o.outKey, &derivedKey,
                                sizeof(Crypto::PublicKey)) == 0) {
                  realGI = static_cast<uint32_t>(o.globalIndex);
                  found = true;
                  m_logger(Logging::INFO) << "  Optimize output at gi=" << realGI
                    << " amt=" << realAmt << " outIdx=" << outIdx;
                  // Re-derive secret and key image with the correct index
                  Crypto::derive_secret_key(derivation, outIdx, m_makerSecretKey, outputSecret);
                  Crypto::generate_key_image(derivedKey, outputSecret, keyImage);
                  break;
                }
              }
            }
            break;
          }
        }
      }
    }
    if (!found && retry % 10 == 0)
      m_logger(Logging::INFO) << "  Waiting for optimize tx confirmation... (" << retry << ")";
  }
  if (!found) {
    m_logger(Logging::ERROR) << "  Optimize output not found after polling";
    return false;
  }
  // realGI is the optimize *input* global index — NOT the escrow output.
  // escrowOutputIndex is resolved after the escrow funding tx confirms.
  m_logger(Logging::INFO) << "  Optimize input at gi=" << realGI
    << " (local outIdx=" << foundOutIdx << ")";

  // 4. Get decoy outputs
  std::vector<RandomOutputEntry> decoys;
  if (!m_rpc.getRandomOutputs(params.xfgAmount, 9, decoys) || decoys.size() < 9) {
    m_logger(Logging::ERROR) << "  Insufficient decoys: " << decoys.size();
    return false;
  }
  decoys.erase(std::remove_if(decoys.begin(), decoys.end(),
      [realGI](const RandomOutputEntry& e) { return e.globalIndex == realGI; }),
      decoys.end());
  if (decoys.size() < 8) {
    m_logger(Logging::ERROR) << "  Not enough decoys after filtering";
    return false;
  }
  decoys.resize(8);

  // 5. Build ring
  struct RM { uint32_t gi; Crypto::PublicKey pk; };
  std::vector<RM> ring;
  ring.push_back({realGI, derivedKey});
  for (auto& d : decoys) ring.push_back({(uint32_t)d.globalIndex, d.outKey});
  std::sort(ring.begin(), ring.end(),
            [](const RM& a, const RM& b) { return a.gi < b.gi; });
  size_t realIdx = 0;
  for (size_t i = 0; i < ring.size(); ++i)
    if (ring[i].gi == realGI) { realIdx = i; break; }

  std::vector<uint32_t> abs;
  std::vector<Crypto::PublicKey> ringKeys;
  for (auto& r : ring) { abs.push_back(r.gi); ringKeys.push_back(r.pk); }
  auto rel = CryptoNote::absolute_output_offsets_to_relative(abs);

  // 6. Build unsigned escrow funding tx
  CryptoNote::Transaction tx;
  tx.version = CryptoNote::TRANSACTION_VERSION_1;
  tx.unlockTime = 0;
  CryptoNote::KeyPair txKey;
  Crypto::generate_keys(txKey.publicKey, txKey.secretKey);
  CryptoNote::addTransactionPublicKeyToExtra(tx.extra, txKey.publicKey);

  CryptoNote::KeyInput input;
  input.amount = params.xfgAmount;
  input.outputIndexes = rel;
  input.keyImage = keyImage;
  tx.inputs.push_back(input);

  CryptoNote::KeyOutput ko;
  ko.key = params.escrowPubKey;
  CryptoNote::TransactionOutput escrowOut;
  escrowOut.amount = params.xfgAmount;
  escrowOut.target = ko;
  tx.outputs.push_back(escrowOut);
  tx.signatures.push_back(std::vector<Crypto::Signature>(ring.size()));

  Crypto::Hash prefixHash;
  if (!CryptoNote::getObjectHash(
          static_cast<CryptoNote::TransactionPrefix&>(tx), prefixHash))
    return false;

  // 7. Ring signature
  {
    std::vector<const Crypto::PublicKey*> ptrs;
    for (auto& k : ringKeys) ptrs.push_back(&k);
    Crypto::generate_ring_signature(prefixHash, keyImage, ptrs,
        outputSecret, realIdx,
        const_cast<Crypto::Signature*>(tx.signatures[0].data()));
  }

  // 8. Broadcast
  std::string txHex = SwapTxBuilder::serializeToHex(tx);
  m_logger(Logging::INFO) << "  Broadcasting escrow tx (" << txHex.size() << " hex)";
  if (!m_rpc.sendRawTransaction(txHex)) {
    m_logger(Logging::ERROR) << "  sendRawTransaction failed";
    return false;
  }

  Crypto::Hash txHash;
  CryptoNote::getObjectHash(
      static_cast<CryptoNote::TransactionPrefix&>(tx), txHash);
  params.escrowTxHash = txHash;
  m_logger(Logging::INFO) << "  Escrow funded: " << Common::podToHex(txHash);

  // Resolve the escrow output's *global* index (required for spend ring).
  // Poll getTransactionOutputs + getRandomOutputs until the escrow key appears.
  uint32_t escrowGi = 0;
  bool giFound = false;
  for (int retry = 0; retry < 200 && !giFound; ++retry) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::vector<TxOutputInfo> outs;
    if (!m_rpc.getTransactionOutputs(Common::podToHex(txHash), outs)) continue;
    for (const auto& o : outs) {
      if (o.amount != params.xfgAmount) continue;
      if (std::memcmp(&o.targetKey, &params.escrowPubKey, sizeof(Crypto::PublicKey)) != 0)
        continue;
      std::vector<RandomOutputEntry> candidates;
      if (!m_rpc.getRandomOutputs(params.xfgAmount, 200, candidates)) continue;
      for (const auto& c : candidates) {
        if (std::memcmp(&c.outKey, &params.escrowPubKey, sizeof(Crypto::PublicKey)) == 0) {
          escrowGi = static_cast<uint32_t>(c.globalIndex);
          giFound = true;
          break;
        }
      }
      break;
    }
    if (!giFound && retry % 10 == 0)
      m_logger(Logging::INFO) << "  Waiting for escrow output global index... (" << retry << ")";
  }
  if (!giFound) {
    m_logger(Logging::ERROR) << "  Escrow output global index not found after funding";
    return false;
  }
  params.escrowOutputIndex = escrowGi;
  m_logger(Logging::INFO) << "  Escrow output global index=" << escrowGi;
  return true;
}

bool SwapDaemon::verifyEscrowFunding(const SwapParams& params) {
  std::vector<TxOutputInfo> outputs;
  if (!m_rpc.getTransactionOutputs(Common::podToHex(params.escrowTxHash), outputs))
    return false;
  for (size_t i = 0; i < outputs.size(); ++i) {
    if (outputs[i].amount == params.xfgAmount &&
        std::memcmp(&outputs[i].targetKey, &params.escrowPubKey,
                    sizeof(Crypto::PublicKey)) == 0) {
      // Best-effort: if escrowOutputIndex is still unset (Alice side), resolve GI.
      // Callers that need a durable GI must pass a non-const path — fundEscrow
      // already sets it for Bob. Alice resolves on first successful spend/refund
      // attempt via resolveEscrowGlobalIndex if still zero.
      return true;
    }
  }
  return false;
}

// Resolve escrow output global index into params.escrowOutputIndex (mutable).
// Returns true if already set or successfully resolved.
bool SwapDaemon::resolveEscrowGlobalIndex(SwapParams& params) {
  if (params.escrowOutputIndex != 0) return true;
  std::vector<TxOutputInfo> outs;
  if (!m_rpc.getTransactionOutputs(Common::podToHex(params.escrowTxHash), outs))
    return false;
  for (const auto& o : outs) {
    if (o.amount != params.xfgAmount) continue;
    if (std::memcmp(&o.targetKey, &params.escrowPubKey, sizeof(Crypto::PublicKey)) != 0)
      continue;
    std::vector<RandomOutputEntry> candidates;
    if (!m_rpc.getRandomOutputs(params.xfgAmount, 200, candidates)) return false;
    for (const auto& c : candidates) {
      if (std::memcmp(&c.outKey, &params.escrowPubKey, sizeof(Crypto::PublicKey)) == 0) {
        params.escrowOutputIndex = static_cast<uint32_t>(c.globalIndex);
        return true;
      }
    }
  }
  return false;
}

// ── Per-state handlers (extracted from processSwap in commit b6d82cad's refactor) ──

bool SwapDaemon::handleEscrowFunded(SwapStateMachine& sm, uint32_t currentHeight) {
  (void)currentHeight;
  SwapParams& params = sm.params();
  m_logger(Logging::INFO) << "  Escrow funded (tx: "
    << Common::podToHex(params.escrowTxHash) << ").";
  // Do NOT mutate xfgAmount after funding. The on-chain escrow output amount is fixed
  // at fund time; post-hoc surcharge broke amount matching for decoy selection and
  // verifyEscrowFunding. Apply any protocol fees before fundEscrow if needed.
  m_logger(Logging::INFO) << "  Next: exchange Musig2 nonces and create adaptor pre-sigs.";
  return true;
}

bool SwapDaemon::handlePreSigsReady(SwapStateMachine& sm) {
  SwapParams& params = sm.params();

  // Alice-locks model (standard HTLC atomic swap with adaptor secret on Bob):
  //   Alice (has CTR) locks CTR with H(t); Bob (has t) claims CTR revealing t.
  //   Alice extracts t on-chain and Bob spends XFG to Alice.
  if (params.role == SwapRole::ALICE) {
    m_logger(Logging::INFO) << "  Pre-sigs ready. Alice locking counterparty ("
      << swapPairToString(params.pair) << ") with H(t)...";

    // Hard-gate: must have verified adaptor point + H(t) before locking value.
    {
      static const Crypto::PublicKey ZERO_PK{};
      static const Crypto::Hash ZERO_H{};
      if (std::memcmp(&params.adaptorPoint, &ZERO_PK, sizeof(ZERO_PK)) == 0 ||
          std::memcmp(&params.hashLock, &ZERO_H, sizeof(ZERO_H)) == 0) {
        m_logger(Logging::ERROR)
          << "  Refusing CTR lock: adaptor point / hashLock not set (DLEQ exchange incomplete)";
        return false;
      }
    }

    auto* client = m_chainRegistry.getClient(params.pair);
    if (!client) {
      m_logger(Logging::ERROR) << "  " << swapPairToString(params.pair)
        << " client not configured — cannot lock";
      return false;
    }
    auto result = client->lock(params);
    if (result.success) {
      m_logger(Logging::INFO) << "  " << client->chainName()
        << " locked, ref: " << result.txId;
      params.ctrLockTxId = result.txId;
      if (!result.chainState.empty()) {
        params.chainState = result.chainState;
      }
      sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
      m_db.saveSwap(sm);
      return true;
    }
    m_logger(Logging::ERROR) << "  " << client->chainName()
      << " lock failed: " << result.error;
    if (result.fatal) {
      sm.transition(SwapState::FAILED);
      m_db.saveSwap(sm);
    }
    return false;
  }

  // Bob: verify Alice has locked on the counterparty chain
  m_logger(Logging::INFO) << "  Pre-sigs ready. Bob verifying counterparty ("
    << swapPairToString(params.pair) << ") lock...";

  auto* client = m_chainRegistry.getClient(params.pair);
  if (!client) {
    m_logger(Logging::WARNING) << "  " << swapPairToString(params.pair)
      << " client not configured — cannot verify lock";
    return false;
  }
  auto result = client->verifyLock(params);
  if (result.success) {
    m_logger(Logging::INFO) << "  Counterparty lock verified. Transitioning to CTR_LOCKED.";
    sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
    m_db.saveSwap(sm);
    return true;
  }
  m_logger(Logging::INFO) << "  Counterparty lock not yet verified — will retry next tick.";
  return false;
}

bool SwapDaemon::handleCtrLocked(SwapStateMachine& sm) {
  SwapParams& params = sm.params();

  auto isZeroSecret = [](const Crypto::SecretKey& s) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
    for (size_t i = 0; i < sizeof(s); ++i) if (p[i]) return false;
    return true;
  };

  // Alice-locks model: Bob claims CTR with t (reveals preimage on-chain).
  if (params.role == SwapRole::BOB) {
    if (isZeroSecret(params.adaptorSecret)) {
      m_logger(Logging::ERROR) << "  Bob missing adaptor secret — cannot claim CTR";
      return false;
    }

    if (params.useSpvVerification) {
      m_logger(Logging::INFO) << "  " << swapPairToString(params.pair)
        << " locked. SPV mode — Bob will claim then wait for confirmations";
      sm.transition(SwapState::ADAPTOR_WAITING_SPV);
      m_db.saveSwap(sm);
      return true;
    }

    m_logger(Logging::INFO) << "  " << swapPairToString(params.pair)
      << " locked. Bob claiming CTR with adaptor secret (reveals t on-chain)...";

    auto* client = m_chainRegistry.getClient(params.pair);
    if (!client) {
      m_logger(Logging::ERROR) << "  " << swapPairToString(params.pair)
        << " client not configured — cannot claim";
      return false;
    }
    auto result = client->claim(params);
    if (result.success) {
      m_logger(Logging::INFO) << "  " << client->chainName()
        << " claimed, txid: " << result.txId;
      params.adaptorSecretRevealedToPeer = true;  // on-chain reveal
      params.ctrClaimTxId = result.txId;
      // Append claim txid to chainState so peers that share state can extract.
      if (!params.chainState.empty() && params.chainState.find(':') == std::string::npos
          && !result.txId.empty()) {
        params.chainState = params.chainState + ":" + result.txId;
      }
      // Push SECRET_REVEAL so Alice learns t even without chain indexer.
      {
        PeerMessage rev;
        rev.type = PeerMessageType::SECRET_REVEAL;
        rev.swapId = params.swapId;
        rev.secretReveal.adaptorSecret = params.adaptorSecret;
        rev.secretReveal.claimTxId = result.txId;
        if (signPeerMessage(rev, params.ourSwapPubKey, params.ourSwapSecKey)) {
          deliverPeerMessage(rev);
        }
      }
      sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
      m_db.saveSwap(sm);
      return true;
    }
    m_logger(Logging::ERROR) << "  " << client->chainName()
      << " claim failed: " << result.error;
    if (result.fatal) {
      sm.transition(SwapState::FAILED);
      m_db.saveSwap(sm);
    }
    return false;
  }

  // Alice: extract t from Bob's on-chain claim (no SECRET_REVEAL required).
  auto* client = m_chainRegistry.getClient(params.pair);
  if (!client) {
    m_logger(Logging::ERROR) << "  " << swapPairToString(params.pair)
      << " client not configured";
    return false;
  }

  if (params.useSpvVerification) {
    m_logger(Logging::INFO) << "  Waiting for Bob's claim + SPV confirmation...";
    sm.transition(SwapState::ADAPTOR_WAITING_SPV);
    m_db.saveSwap(sm);
    return true;
  }

  // Prefer known claim txid (from SECRET_REVEAL) for extract paths that need it.
  if (!params.ctrClaimTxId.empty() && !params.chainState.empty()
      && params.chainState.find(':') == std::string::npos) {
    params.chainState = params.chainState + ":" + params.ctrClaimTxId;
  }

  std::string claimedHex = client->tryExtractClaimedSecret(params);
  if (claimedHex.empty() || claimedHex.size() != 64) {
    m_logger(Logging::INFO) << "  " << swapPairToString(params.pair)
      << " locked. Waiting for Bob to claim (on-chain preimage)...";
    return false;
  }

  Crypto::SecretKey claimed;
  if (!Common::podFromHex(claimedHex, claimed)) {
    m_logger(Logging::ERROR) << "  Invalid extracted preimage hex";
    return false;
  }

  // Bind extracted preimage to adaptor point T and published hashLock.
  Crypto::PublicKey derivedT;
  if (!Crypto::secret_key_to_public_key(claimed, derivedT) ||
      std::memcmp(&derivedT, &params.adaptorPoint, sizeof(derivedT)) != 0) {
    m_logger(Logging::ERROR) << "  Extracted preimage does not match adaptorPoint T — rejecting";
    return false;
  }
  {
    Crypto::Hash expectedH = params.hashLock;
    bool hashSet = false;
    for (size_t i = 0; i < sizeof(expectedH); ++i)
      if (reinterpret_cast<const uint8_t*>(&expectedH)[i]) { hashSet = true; break; }
    if (hashSet) {
      Crypto::Hash computed{};
      switch (params.pair) {
        case SwapPair::BCH: case SwapPair::BTC: case SwapPair::LTC:
        case SwapPair::DCR: case SwapPair::KMD_SPV:
        case SwapPair::DOGE: case SwapPair::DASH: case SwapPair::ZEC:
        case SwapPair::TON: {
          std::string hex = bchHashLockHex(claimed);
          Common::podFromHex(hex, computed);
          break;
        }
        case SwapPair::SIA: {
          auto md = SiaHtlcScript::blake2b256(
              reinterpret_cast<const uint8_t*>(&claimed), 32);
          std::memcpy(&computed, md.data(), 32);
          break;
        }
        default: {
          std::string hex = solHashLockHex(claimed);
          Common::podFromHex(hex, computed);
          break;
        }
      }
      if (std::memcmp(&computed, &expectedH, sizeof(expectedH)) != 0) {
        m_logger(Logging::ERROR) << "  Extracted preimage H(t) does not match hashLock — rejecting";
        return false;
      }
    }
  }

  params.adaptorSecret = claimed;
  params.adaptorSecretReceived = true;
  m_logger(Logging::INFO) << "  Extracted adaptor secret from Bob's CTR claim (bound to T and H(t))";
  sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
  m_db.saveSwap(sm);
  return true;
}

bool SwapDaemon::handleSecretRevealed(SwapStateMachine& sm) {
  return finalizeEscrowSpend(sm, "Adaptor secret learned");
}

bool SwapDaemon::handleSecretConfirmedSpv(SwapStateMachine& sm) {
  return finalizeEscrowSpend(sm, "SPV confirmed");
}

bool SwapDaemon::finalizeEscrowSpend(SwapStateMachine& sm, const std::string& logContext) {
  SwapParams& params = sm.params();
  const std::string& swapId = params.swapId;

  if (params.role != SwapRole::BOB) {
    m_logger(Logging::INFO) << "  " << logContext << ". Waiting for Bob to broadcast escrow spend.";
    return true;
  }

  // Protocol fee: 1% initiation + 1% claim = 2% of escrow amount → treasury
  Crypto::PublicKey treasuryKey;
  if (!getTreasuryPubKey(treasuryKey)) {
    m_logger(Logging::ERROR) << "  Cannot derive treasury key for protocol fee";
    return false;
  }
  params.protocolFee = (params.xfgAmount * CryptoNote::parameters::SWAP_FEE_RATE_BPS)
                     / CryptoNote::parameters::SWAP_FEE_RATE_DIVISOR;  // 1% initiation
  params.protocolFee += (params.xfgAmount * CryptoNote::parameters::SWAP_FEE_RATE_BPS)
                      / CryptoNote::parameters::SWAP_FEE_RATE_DIVISOR;  // 1% claim
  params.treasuryPubKey = treasuryKey;

  m_logger(Logging::INFO) << "  " << logContext << ". Building adapted escrow spend tx...";

  // If tx already broadcast, transition to terminal state.
  if (params.ringTxBroadcast) {
    sm.transition(SwapState::ADAPTOR_XFG_SPENT);
    m_db.saveSwap(sm);
    m_logger(Logging::INFO) << "  Escrow spend confirmed. Swap " << swapId << " completed.";
    recordCompletedTrade(sm);
    return true;
  }

  // If peer has sent both Round 1 and Round 2 data, finalize and broadcast.
  if (params.ringPeerRound1Received && params.ringPeerRound2Received) {
    Crypto::PublicKey alicePub = params.peerSwapPubKey;
    if (buildAndBroadcastEscrowTx(params, alicePub, "spend")) {
      params.ringTxBroadcast = true;
      sm.transition(SwapState::ADAPTOR_XFG_SPENT);
      m_db.saveSwap(sm);
      m_logger(Logging::INFO) << "  Escrow spend broadcast. Swap " << swapId << " completed.";
      recordCompletedTrade(sm);
      return true;
    }
    m_logger(Logging::ERROR) << "  Failed to build/broadcast escrow spend tx";
    return false;
  }

  // If peer has sent Round 1 but not Round 2, send our Round 2.
  if (params.ringPeerRound1Received && !params.ringOurRound2Sent) {
    if (!resolveEscrowGlobalIndex(params)) {
      m_logger(Logging::ERROR) << "  Escrow GI unresolved — cannot Round 2";
      return false;
    }
    SwapParams working = params;
    CollaborativeRingState ringState;
    CryptoNote::Transaction spendTx;
    Crypto::Hash spendPrefixHash;
    Crypto::PublicKey alicePub = params.peerSwapPubKey;

    if (!SwapTxBuilder::buildUnsignedEscrowSpend(
            m_rpc, working, alicePub, params.protocolFee, params.treasuryPubKey,
            SwapTxBuilder::MIN_FEE, spendTx, spendPrefixHash, ringState)) {
      m_logger(Logging::ERROR) << "  Failed to build escrow spend tx for Round 2";
      return false;
    }
    // Restore our Round 1 (must not regenerate)
    if (!params.ringOurRound1MaterialValid) {
      m_logger(Logging::ERROR) << "  Missing our Round 1 material for Round 2";
      return false;
    }
    ringState.ourPartialKeyImage = params.ringOurPartialKeyImage;
    ringState.ourRingNoncePub    = params.ringOurRingNoncePub;
    ringState.ourRingNonceHp     = params.ringOurRingNonceHp;
    ringState.ourRingNonceSec    = params.ringOurRingNonceSec;
    ringState.peerPartialKeyImage = params.ringPeerPartialKeyImage;
    ringState.peerRingNoncePub    = params.ringPeerRingNoncePub;
    ringState.peerRingNonceHp     = params.ringPeerRingNonceHp;

    if (!SwapTxBuilder::writeAggregateKeyImageToTx(spendTx, ringState)) {
      m_logger(Logging::ERROR) << "  Failed to write aggregate KI";
      return false;
    }
    if (!CryptoNote::getObjectHash(
        static_cast<CryptoNote::TransactionPrefix&>(spendTx), spendPrefixHash)) {
      m_logger(Logging::ERROR) << "  Failed to recompute prefix hash";
      return false;
    }
    if (!SwapTxBuilder::ringRound1Finalize(spendPrefixHash, ringState)) {
      m_logger(Logging::ERROR) << "  Ring Round 1 finalize failed";
      return false;
    }
    SwapTxBuilder::ringRound2Sign(working, ringState);
    params.ringOurPartialResponse = ringState.ourPartialResponse;
    params.ringOurRound2Sent = true;
    m_db.saveSwap(sm);

    PeerMessage r2msg;
    r2msg.type = PeerMessageType::RING_ROUND2;
    r2msg.swapId = params.swapId;
    r2msg.ringRound2.partialResponse = ringState.ourPartialResponse;
    signPeerMessage(r2msg, params.ourSwapPubKey, params.ourSwapSecKey);
    deliverPeerMessage(r2msg);
    m_logger(Logging::INFO) << "  Sending Ring Round 2 to peer...";
    return true;
  }

  // If we haven't sent Round 1 yet, build tx and send Round 1.
  if (!params.ringOurRound1Sent) {
    if (!resolveEscrowGlobalIndex(params)) {
      m_logger(Logging::ERROR) << "  Escrow GI unresolved — cannot Round 1";
      return false;
    }
    SwapParams working = params;
    CryptoNote::Transaction spendTx;
    Crypto::Hash spendPrefixHash;
    CollaborativeRingState spendRingState;
    Crypto::PublicKey alicePub = params.peerSwapPubKey;

    if (!SwapTxBuilder::buildUnsignedEscrowSpend(
            m_rpc, working, alicePub, params.protocolFee, params.treasuryPubKey,
            SwapTxBuilder::MIN_FEE, spendTx, spendPrefixHash, spendRingState)) {
      m_logger(Logging::ERROR) << "  Failed to build escrow spend tx";
      return false;
    }
    SwapTxBuilder::ringRound1Generate(working, spendRingState);
    params.ringOurPartialKeyImage = spendRingState.ourPartialKeyImage;
    params.ringOurRingNoncePub    = spendRingState.ourRingNoncePub;
    params.ringOurRingNonceHp     = spendRingState.ourRingNonceHp;
    params.ringOurRingNonceSec    = spendRingState.ourRingNonceSec;
    params.ringOurRound1MaterialValid = true;
    params.ringOurRound1Sent = true;
    m_db.saveSwap(sm);

    PeerMessage r1msg;
    r1msg.type = PeerMessageType::RING_ROUND1;
    r1msg.swapId = params.swapId;
    r1msg.ringRound1.partialKeyImage = spendRingState.ourPartialKeyImage;
    r1msg.ringRound1.ringNoncePub = spendRingState.ourRingNoncePub;
    r1msg.ringRound1.ringNonceHp = spendRingState.ourRingNonceHp;
    signPeerMessage(r1msg, params.ourSwapPubKey, params.ourSwapSecKey);
    deliverPeerMessage(r1msg);

    m_logger(Logging::INFO) << "  Escrow spend tx built. Sending Ring Round 1 to peer...";
    return true;
  }

  m_logger(Logging::INFO) << "  Awaiting peer ring data (Round 1 sent, waiting for response).";
  return true;
}

bool SwapDaemon::handleWaitingSpv(SwapStateMachine& sm) {
  SwapParams& params = sm.params();
  const std::string& swapId = params.swapId;

  auto* client = m_chainRegistry.getClient(params.pair);
  if (!client) {
    m_logger(Logging::ERROR) << "  " << swapPairToString(params.pair)
      << " client not configured — cannot verify SPV";
    return false;
  }

  uint32_t required = params.requiredConfirmations;
  if (required == 0) required = 6;

  auto isZeroSecret = [](const Crypto::SecretKey& s) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
    for (size_t i = 0; i < sizeof(s); ++i) if (p[i]) return false;
    return true;
  };

  // Alice-locks + SPV: Bob claims with t; Alice waits for on-chain preimage.
  if (params.role == SwapRole::BOB && !isZeroSecret(params.adaptorSecret)) {
    auto claimResult = client->claim(params);
    if (claimResult.success) {
      m_logger(Logging::INFO) << "  SPV path: Bob CTR claim submitted " << claimResult.txId;
    } else if (claimResult.fatal) {
      m_logger(Logging::ERROR) << "  SPV path: claim fatal — " << claimResult.error;
      sm.transition(SwapState::FAILED);
      m_db.saveSwap(sm);
      return true;
    } else {
      m_logger(Logging::INFO) << "  SPV path: claim pending — " << claimResult.error;
    }
  }

  if (params.role == SwapRole::ALICE) {
    std::string claimed = client->tryExtractClaimedSecret(params);
    if (claimed.empty() || claimed.size() != 64) {
      m_logger(Logging::INFO) << "  SPV: Alice waiting for Bob's claim preimage";
      return false;
    }
    Crypto::SecretKey sec;
    if (Common::podFromHex(claimed, sec)) {
      params.adaptorSecret = sec;
      params.adaptorSecretReceived = true;
    }
  }

  // Prefer verifying the *lock* has enough depth before advancing.
  ChainClientResult spvResult;
  client->getTransactionDetails(params.ctrLockTxId, spvResult);

  if (spvResult.fatal) {
    m_logger(Logging::ERROR) << "  SPV verification failed fatally: " << spvResult.error;
    sm.transition(SwapState::FAILED);
    m_db.saveSwap(sm);
    return true;
  }

  if (!spvResult.success) {
    m_logger(Logging::INFO) << "  SPV lookup failed (will retry): " << spvResult.error;
    return false;
  }

  if (spvResult.confirmed && spvResult.confirmations >= required) {
    sm.transition(SwapState::ADAPTOR_SECRET_CONFIRMED_SPV);
    m_db.saveSwap(sm);
    m_logger(Logging::INFO) << "  SPV verified (" << spvResult.confirmations
      << " confirmations). Swap " << swapId << " -> ADAPTOR_SECRET_CONFIRMED_SPV";
    return true;
  }

  m_logger(Logging::INFO) << "  SPV not yet confirmed (confirmations="
    << spvResult.confirmations << "/" << required << "). Will retry next tick.";
  return false;
}

bool SwapDaemon::processSwap(SwapStateMachine& sm) {
  const std::string& swapId = sm.params().swapId;

  if (sm.isTerminal()) {
    m_logger(Logging::INFO) << "Swap " << swapId
      << " is in terminal state: " << swapStateToString(sm.currentState());
    return true;
  }

  uint32_t currentHeight = 0;
  if (!m_rpc.getHeight(currentHeight)) {
    m_logger(Logging::ERROR) << "Cannot query fuegod height";
    return false;
  }

  // Use a mutable reference so that chain dispatch can update ctrLockTxId etc.
  // before saving.
  SwapParams& params = sm.params();
  SwapState current = sm.currentState();

  m_logger(Logging::INFO) << "Processing swap " << swapId
    << " state=" << swapStateToString(current)
    << " height=" << currentHeight;

  switch (current) {
    case SwapState::INITIATED:
      m_logger(Logging::INFO) << "  Waiting for peer pubkey. Use 'accept' after exchanging keys.";
      break;

    case SwapState::ADAPTOR_KEYS_EXCHANGED:
      m_logger(Logging::INFO) << "  Keys aggregated. Escrow key: "
        << Common::podToHex(params.escrowPubKey);
      {
        Crypto::Hash zeroHash;
        std::memset(&zeroHash, 0, sizeof(zeroHash));
        bool escrowTxKnown = (std::memcmp(&params.escrowTxHash, &zeroHash,
                                          sizeof(Crypto::Hash)) != 0);

        if (params.role == SwapRole::BOB) {
          if (!escrowTxKnown) {
            if (!fundEscrow(params)) {
              m_logger(Logging::ERROR) << "Failed to fund escrow for swap " << swapId;
              return false;
            }
            m_db.saveSwap(sm);
          } else if (verifyEscrowFunding(params)) {
            sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
            m_db.saveSwap(sm);
            m_logger(Logging::INFO) << "Swap " << swapId << " -> ADAPTOR_ESCROW_FUNDED";
          }
        } else if (escrowTxKnown && verifyEscrowFunding(params)) {
          sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
          m_db.saveSwap(sm);
          m_logger(Logging::INFO) << "Swap " << swapId << " -> ADAPTOR_ESCROW_FUNDED";
        }
      }
      break;

    case SwapState::ADAPTOR_ESCROW_FUNDED:
      if (!handleEscrowFunded(sm, currentHeight)) {
        m_logger(Logging::INFO) << "  handleEscrowFunded not yet complete";
      }
      break;

    case SwapState::ADAPTOR_PRESIGS_READY:
      if (!handlePreSigsReady(sm)) {
        m_logger(Logging::INFO) << "  handlePreSigsReady not yet complete";
      }
      break;

    case SwapState::ADAPTOR_CTR_LOCKED:
      if (!handleCtrLocked(sm)) {
        m_logger(Logging::INFO) << "  handleCtrLocked not yet complete";
      }
      break;

    case SwapState::ADAPTOR_SECRET_REVEALED:
      if (!handleSecretRevealed(sm)) {
        m_logger(Logging::INFO) << "  handleSecretRevealed not yet complete";
      }
      break;

    case SwapState::ADAPTOR_WAITING_SPV:
      if (!handleWaitingSpv(sm)) {
        m_logger(Logging::INFO) << "  handleWaitingSpv not yet complete";
      }
      break;

    case SwapState::ADAPTOR_SECRET_CONFIRMED_SPV:
      if (!handleSecretConfirmedSpv(sm)) {
        m_logger(Logging::INFO) << "  handleSecretConfirmedSpv not yet complete";
      }
      break;

    default:
      break;
  }

  // Advance in-progress cooperative ring sig (refund or spend) if peer
  // data has arrived since the last tick.  This handles the async round-
  // trip for swaps that are waiting on peer Ring Round 1 or 2 responses.
  if (!sm.isTerminal() && params.ringOurRound1Sent &&
      params.ringPeerRound1Received && params.ringPeerRound2Received &&
      !params.ringTxBroadcast) {
    // Both peer rounds received — finalize and broadcast.
    bool isSpendPath = (current == SwapState::ADAPTOR_SECRET_REVEALED ||
                        current == SwapState::ADAPTOR_SECRET_CONFIRMED_SPV);
    Crypto::PublicKey destKey =
        (isSpendPath && params.role == SwapRole::BOB)
            ? params.peerSwapPubKey   // spend: Alice gets XFG
            : params.ourSwapPubKey;    // refund: Bob gets XFG back
    std::string txType = isSpendPath ? "spend" : "refund";

    if (buildAndBroadcastEscrowTx(params, destKey, txType)) {
      params.ringTxBroadcast = true;
      if (isSpendPath) {
        sm.transition(SwapState::ADAPTOR_XFG_SPENT);
        m_db.saveSwap(sm);
        m_logger(Logging::INFO) << "  Escrow " << txType << " tx broadcast. Swap " << swapId << " completed.";
        recordCompletedTrade(sm);
      } else {
        sm.transition(SwapState::ADAPTOR_REFUNDED, currentHeight);
        m_db.saveSwap(sm);
        m_logger(Logging::INFO) << "  Escrow " << txType << " tx broadcast. Swap " << swapId << " completed.";
      }
    }
  }

  return true;
}

void SwapDaemon::listSwaps() {
  auto swapIds = m_db.listSwaps();

  if (swapIds.empty()) {
    std::cout << "No swaps found." << std::endl;
    return;
  }

  std::cout << std::left
            << std::setw(34) << "SWAP ID"
            << std::setw(22) << "STATE"
            << std::setw(6)  << "PAIR"
            << std::setw(6)  << "ROLE"
            << std::setw(18) << "XFG AMOUNT"
            << std::endl;
  std::cout << std::string(86, '-') << std::endl;

  for (const auto& swapId : swapIds) {
    SwapStateMachine sm;
    if (!m_db.loadSwap(swapId, sm)) {
      std::cout << swapId << "  [ERROR: cannot load]" << std::endl;
      continue;
    }

    const auto& p = sm.params();
    std::cout << std::left
              << std::setw(34) << p.swapId
              << std::setw(22) << swapStateToString(sm.currentState())
              << std::setw(6)  << swapPairToString(p.pair)
              << std::setw(6)  << (p.role == SwapRole::BOB ? "BOB" : "ALICE")
              << std::setw(18) << p.xfgAmount
              << std::endl;
  }
}

void SwapDaemon::showSwap(const std::string& swapId) {
  SwapStateMachine sm;
  if (!m_db.loadSwap(swapId, sm)) {
    std::cout << "Swap not found: " << swapId << std::endl;
    return;
  }

  const auto& p = sm.params();

  std::cout << "=== Swap Details ===" << std::endl;
  std::cout << "  Swap ID:          " << p.swapId << std::endl;
  std::cout << "  State:            " << swapStateToString(sm.currentState()) << std::endl;
  std::cout << "  Pair:             XFG/" << swapPairToString(p.pair) << std::endl;
  std::cout << "  Role:             " << (p.role == SwapRole::BOB ? "BOB (selling XFG)" : "ALICE (buying XFG)") << std::endl;
  std::cout << "  XFG amount:       " << p.xfgAmount << " atomic ("
            << (static_cast<double>(p.xfgAmount) / 10000000.0) << " XFG)" << std::endl;
  std::cout << "  CTR amount:       " << p.ctrAmount << " atomic" << std::endl;

  // Adaptor sig fields
  std::cout << "  Our swap pubkey:  " << Common::podToHex(p.ourSwapPubKey) << std::endl;
  std::cout << "  Peer swap pubkey: " << Common::podToHex(p.peerSwapPubKey) << std::endl;
  std::cout << "  Escrow key:       " << Common::podToHex(p.escrowPubKey) << std::endl;

  Crypto::PublicKey zeroPk;
  std::memset(&zeroPk, 0, sizeof(zeroPk));
  if (std::memcmp(&p.adaptorPoint, &zeroPk, sizeof(zeroPk)) != 0) {
    std::cout << "  Adaptor point T:  " << Common::podToHex(p.adaptorPoint) << std::endl;
  }

  Crypto::Hash zeroHash;
  std::memset(&zeroHash, 0, sizeof(zeroHash));
  if (std::memcmp(&p.escrowTxHash, &zeroHash, sizeof(zeroHash)) != 0) {
    std::cout << "  Escrow tx:        " << Common::podToHex(p.escrowTxHash) << std::endl;
    std::cout << "  Escrow out idx:   " << p.escrowOutputIndex << std::endl;
  }

  std::cout << "  XFG timeout:      height " << p.xfgTimeoutHeight << std::endl;
  std::cout << "  CTR timeout:      slot/block " << p.ctrTimeoutBlock << std::endl;

  if (!p.ctrLockTxId.empty()) {
    std::cout << "  CTR lock tx:      " << p.ctrLockTxId << std::endl;
  }
  if (!p.ctrAddress.empty()) {
    std::cout << "  CTR address:      " << p.ctrAddress << std::endl;
  }
  if (!p.peerEndpoint.empty()) {
    std::cout << "  Peer endpoint:    " << p.peerEndpoint << std::endl;
  }

  // Timestamps
  char timeBuf[64];
  struct tm* tm;

  time_t created = sm.createdAt();
  tm = std::localtime(&created);
  std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm);
  std::cout << "  Created:          " << timeBuf << std::endl;

  time_t updated = sm.updatedAt();
  tm = std::localtime(&updated);
  std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm);
  std::cout << "  Updated:          " << timeBuf << std::endl;

  std::cout << "  Terminal:         " << (sm.isTerminal() ? "yes" : "no") << std::endl;
}

bool SwapDaemon::refund(const std::string& swapId) {
  SwapStateMachine sm;
  if (!m_db.loadSwap(swapId, sm)) {
    m_logger(Logging::ERROR) << "Swap not found: " << swapId;
    return false;
  }

  uint32_t currentHeight = 0;
  if (!m_rpc.getHeight(currentHeight)) {
    m_logger(Logging::ERROR) << "Cannot query fuegod height";
    return false;
  }

  auto& params = sm.params();
  SwapState current = sm.currentState();

  // AFK soft-order locks: funds are locked via wallet unlockTimestamp.
  // After the height/time deadline the outputs are spendable by the maker
  // again with no special on-chain refund tx — mark AFK_REFUNDED.
  if (current == SwapState::AFK_OFFER_LOCKED ||
      current == SwapState::AFK_OFFER_ACCEPTED) {
    if (params.xfgTimeoutHeight > 0 && currentHeight < params.xfgTimeoutHeight) {
      m_logger(Logging::ERROR) << "AFK refund not yet available. Current height: "
        << currentHeight << ", timeout: " << params.xfgTimeoutHeight;
      return false;
    }
    sm.transition(SwapState::AFK_REFUNDED, currentHeight);
    m_db.saveSwap(sm);
    m_logger(Logging::INFO) << "AFK swap " << swapId
      << " marked AFK_REFUNDED (time-lock elapsed; outputs spendable by maker).";
    return true;
  }

  // Cooperative refund: both parties sign a non-adaptor Musig2 sig
  // spending escrow back to Bob. Available from ESCROW_FUNDED or PRESIGS_READY.
  if (current == SwapState::ADAPTOR_ESCROW_FUNDED ||
      current == SwapState::ADAPTOR_PRESIGS_READY) {
    if (currentHeight < params.xfgTimeoutHeight) {
      m_logger(Logging::ERROR) << "Cannot refund yet. Current height: " << currentHeight
        << ", timeout: " << params.xfgTimeoutHeight
        << " (" << (params.xfgTimeoutHeight - currentHeight) << " blocks remaining)";
      return false;
    }

    m_logger(Logging::INFO) << "Timeout elapsed. Building cooperative refund tx...";

    // If both peer rounds received, finalize and broadcast the refund tx.
    if (params.ringPeerRound1Received && params.ringPeerRound2Received) {
      Crypto::PublicKey destKey = (params.role == SwapRole::BOB)
          ? params.ourSwapPubKey : params.peerSwapPubKey;
      if (buildAndBroadcastEscrowTx(params, destKey, "refund")) {
        params.ringTxBroadcast = true;
        sm.transition(SwapState::ADAPTOR_REFUNDED, currentHeight);
        m_db.saveSwap(sm);
        m_logger(Logging::INFO) << "  Cooperative refund tx broadcast. Swap marked ADAPTOR_REFUNDED.";
        return true;
      }
      m_logger(Logging::ERROR) << "  Failed to broadcast cooperative refund tx";
      return false;
    }

    // If peer sent Round 1 but not Round 2, send our Round 2.
    if (params.ringPeerRound1Received && !params.ringOurRound2Sent) {
      if (!resolveEscrowGlobalIndex(params) || !params.ringOurRound1MaterialValid) {
        m_logger(Logging::ERROR) << "Failed refund Round 2: missing GI or Round 1 material";
        return false;
      }
      SwapParams working = params;
      CryptoNote::Transaction refundTx;
      Crypto::Hash prefixHash;
      CollaborativeRingState ringState;
      Crypto::PublicKey destKey = (params.role == SwapRole::BOB)
          ? params.ourSwapPubKey : params.peerSwapPubKey;

      // Protocol fee: 1% initiation → treasury
      Crypto::PublicKey treasuryKey;
      if (!getTreasuryPubKey(treasuryKey)) {
        m_logger(Logging::ERROR) << "Cannot derive treasury key for refund";
        return false;
      }
      uint64_t refundFee = (params.xfgAmount * CryptoNote::parameters::SWAP_FEE_RATE_BPS)
                         / CryptoNote::parameters::SWAP_FEE_RATE_DIVISOR;

      if (!SwapTxBuilder::buildUnsignedEscrowSpend(
              m_rpc, working, destKey, refundFee, treasuryKey,
              SwapTxBuilder::MIN_FEE, refundTx, prefixHash, ringState)) {
        m_logger(Logging::ERROR) << "Failed to build refund tx for Round 2";
        return false;
      }
      ringState.ourPartialKeyImage = params.ringOurPartialKeyImage;
      ringState.ourRingNoncePub    = params.ringOurRingNoncePub;
      ringState.ourRingNonceHp     = params.ringOurRingNonceHp;
      ringState.ourRingNonceSec    = params.ringOurRingNonceSec;
      ringState.peerPartialKeyImage = params.ringPeerPartialKeyImage;
      ringState.peerRingNoncePub    = params.ringPeerRingNoncePub;
      ringState.peerRingNonceHp     = params.ringPeerRingNonceHp;
      if (!SwapTxBuilder::writeAggregateKeyImageToTx(refundTx, ringState) ||
          !CryptoNote::getObjectHash(
              static_cast<CryptoNote::TransactionPrefix&>(refundTx), prefixHash) ||
          !SwapTxBuilder::ringRound1Finalize(prefixHash, ringState)) {
        m_logger(Logging::ERROR) << "Ring Round 1 finalize failed (refund)";
        return false;
      }
      SwapTxBuilder::ringRound2Sign(working, ringState);
      params.ringOurPartialResponse = ringState.ourPartialResponse;
      params.ringOurRound2Sent = true;
      m_db.updateSwap(swapId, [&](SwapStateMachine& s) {
        s.params().ringOurRound2Sent = true;
        s.params().ringOurPartialResponse = ringState.ourPartialResponse;
        return true;
      });

      PeerMessage r2msg;
      r2msg.type = PeerMessageType::RING_ROUND2;
      r2msg.swapId = params.swapId;
      r2msg.ringRound2.partialResponse = ringState.ourPartialResponse;
      signPeerMessage(r2msg, params.ourSwapPubKey, params.ourSwapSecKey);
      deliverPeerMessage(r2msg);
      m_logger(Logging::INFO) << "  Sending Ring Round 2 to peer...";
      m_logger(Logging::INFO) << "  Awaiting peer Ring Round 2 response.";
      return true;
    }

    // If we haven't sent Round 1 yet, build tx and send Round 1.
    if (!params.ringOurRound1Sent) {
      if (!resolveEscrowGlobalIndex(params)) {
        m_logger(Logging::ERROR) << "Failed refund Round 1: escrow GI unresolved";
        return false;
      }
      SwapParams working = params;
      CryptoNote::Transaction refundTx;
      Crypto::Hash prefixHash;
      CollaborativeRingState ringState;
      Crypto::PublicKey destKey = (params.role == SwapRole::BOB)
          ? params.ourSwapPubKey : params.peerSwapPubKey;

      // Protocol fee: 1% initiation → treasury
      Crypto::PublicKey treasuryKey;
      if (!getTreasuryPubKey(treasuryKey)) {
        m_logger(Logging::ERROR) << "Cannot derive treasury key for refund";
        return false;
      }
      uint64_t refundFee = (params.xfgAmount * CryptoNote::parameters::SWAP_FEE_RATE_BPS)
                         / CryptoNote::parameters::SWAP_FEE_RATE_DIVISOR;

      if (!SwapTxBuilder::buildUnsignedEscrowSpend(
              m_rpc, working, destKey, refundFee, treasuryKey,
              SwapTxBuilder::MIN_FEE, refundTx, prefixHash, ringState)) {
        m_logger(Logging::ERROR) << "Failed to build refund tx (decoy fetch or params)";
        return false;
      }
      SwapTxBuilder::ringRound1Generate(working, ringState);
      params.ringOurPartialKeyImage = ringState.ourPartialKeyImage;
      params.ringOurRingNoncePub    = ringState.ourRingNoncePub;
      params.ringOurRingNonceHp     = ringState.ourRingNonceHp;
      params.ringOurRingNonceSec    = ringState.ourRingNonceSec;
      params.ringOurRound1MaterialValid = true;
      params.ringOurRound1Sent = true;
      m_db.updateSwap(swapId, [&](SwapStateMachine& s) {
        s.params().ringOurRound1Sent = true;
        s.params().ringOurRound1MaterialValid = true;
        s.params().ringOurPartialKeyImage = ringState.ourPartialKeyImage;
        s.params().ringOurRingNoncePub = ringState.ourRingNoncePub;
        s.params().ringOurRingNonceHp = ringState.ourRingNonceHp;
        s.params().ringOurRingNonceSec = ringState.ourRingNonceSec;
        return true;
      });

      PeerMessage r1msg;
      r1msg.type = PeerMessageType::RING_ROUND1;
      r1msg.swapId = params.swapId;
      r1msg.ringRound1.partialKeyImage = ringState.ourPartialKeyImage;
      r1msg.ringRound1.ringNoncePub = ringState.ourRingNoncePub;
      r1msg.ringRound1.ringNonceHp = ringState.ourRingNonceHp;
      signPeerMessage(r1msg, params.ourSwapPubKey, params.ourSwapSecKey);
      deliverPeerMessage(r1msg);

      m_logger(Logging::INFO) << "  Refund tx built. Sending Ring Round 1 to peer...";
      m_logger(Logging::INFO) << "  Awaiting peer Ring Round 1 response. "
        << "Call 'refund " << swapId << "' again when peer has responded.";
      return true;
    }

    m_logger(Logging::INFO) << "  Awaiting peer ring data for cooperative refund.";
    return true;
  }

  // SPV waiting states: Alice locked on counterparty chain but SPV verification
  // hasn't confirmed yet. If timeout elapsed, Bob can refund the counterparty HTLC.
  if ((current == SwapState::ADAPTOR_WAITING_SPV ||
       current == SwapState::ADAPTOR_SECRET_CONFIRMED_SPV) &&
      params.role == SwapRole::BOB) {
    if (currentHeight < params.xfgTimeoutHeight) {
      m_logger(Logging::ERROR) << "Cannot refund yet. Current height: " << currentHeight
        << ", timeout: " << params.xfgTimeoutHeight
        << " (" << (params.xfgTimeoutHeight - currentHeight) << " blocks remaining)";
      return false;
    }

    m_logger(Logging::INFO) << "Timeout elapsed (SPV state). Refunding counterparty ("
      << swapPairToString(params.pair) << ") HTLC...";

    auto* client = m_chainRegistry.getClient(params.pair);
    if (!client) {
      m_logger(Logging::ERROR) << "  " << swapPairToString(params.pair)
        << " client not configured — cannot refund";
      return false;
    }

    auto result = client->refund(params);
    if (result.success) {
      m_logger(Logging::INFO) << "  " << client->chainName()
        << " refunded, txid: " << result.txId;
      sm.transition(SwapState::ADAPTOR_REFUNDED, currentHeight);
      m_db.saveSwap(sm);
      m_logger(Logging::INFO) << "  Counterparty HTLC refunded. Swap marked ADAPTOR_REFUNDED.";
      return true;
    }

    m_logger(Logging::ERROR) << "  " << client->chainName()
      << " refund failed: " << result.error;
    if (result.fatal) {
      sm.transition(SwapState::FAILED);
      m_db.saveSwap(sm);
    }
    return false;
  }

  // Counterparty chain refund: Bob locked on the counterparty chain but the
  // swap timed out before Alice claimed.  Bob must also refund the CTR HTLC.
  if (current == SwapState::ADAPTOR_CTR_LOCKED && params.role == SwapRole::BOB) {
    if (currentHeight < params.xfgTimeoutHeight) {
      m_logger(Logging::ERROR) << "Cannot refund yet. Current height: " << currentHeight
        << ", timeout: " << params.xfgTimeoutHeight
        << " (" << (params.xfgTimeoutHeight - currentHeight) << " blocks remaining)";
      return false;
    }

    m_logger(Logging::INFO) << "Timeout elapsed. Refunding counterparty ("
      << swapPairToString(params.pair) << ") HTLC...";

    bool ctrRefundOk = false;
    auto* client = m_chainRegistry.getClient(params.pair);
    if (!client) {
      m_logger(Logging::ERROR) << "  " << swapPairToString(params.pair)
        << " client not configured — cannot refund";
    } else {
      auto result = client->refund(params);
      if (result.success) {
        m_logger(Logging::INFO) << "  " << client->chainName()
          << " refunded, txid: " << result.txId;
        ctrRefundOk = true;
      } else {
        m_logger(Logging::ERROR) << "  " << client->chainName()
          << " refund failed: " << result.error;
        if (result.fatal) {
          sm.transition(SwapState::FAILED);
          m_db.saveSwap(sm);
          return false;
        }
      }
    }

    if (ctrRefundOk) {
      sm.transition(SwapState::ADAPTOR_REFUNDED, currentHeight);
      m_db.saveSwap(sm);
      m_logger(Logging::INFO) << "  Counterparty HTLC refunded. Swap marked ADAPTOR_REFUNDED.";
    } else {
      m_logger(Logging::WARNING) << "  Counterparty refund failed — will retry next tick.";
    }
    return ctrRefundOk;
  }

  m_logger(Logging::ERROR) << "Cannot refund swap in state: "
    << swapStateToString(current);
  return false;
}

bool SwapDaemon::buildAndBroadcastEscrowTx(SwapParams& params,
                                           const Crypto::PublicKey& destinationKey,
                                           const std::string& txType) {
  // Full pipeline: build unsigned tx → collaborative ring sig → broadcast.
  // This is the synchronous version that assumes peer Round 1 + Round 2
  // data has already been populated in the ring state.

  if (!resolveEscrowGlobalIndex(params)) {
    m_logger(Logging::ERROR) << "Escrow global index unresolved — cannot build " << txType;
    return false;
  }

  CryptoNote::Transaction tx;
  Crypto::Hash prefixHash;
  CollaborativeRingState ringState;

  if (!SwapTxBuilder::buildUnsignedEscrowSpend(
          m_rpc, params, destinationKey, params.protocolFee, params.treasuryPubKey,
          SwapTxBuilder::MIN_FEE, tx, prefixHash, ringState)) {
    m_logger(Logging::ERROR) << "Failed to build " << txType << " tx";
    return false;
  }

  // Restore or generate our Round 1 material. Never regenerate after first send.
  if (params.ringOurRound1MaterialValid) {
    ringState.ourPartialKeyImage = params.ringOurPartialKeyImage;
    ringState.ourRingNoncePub    = params.ringOurRingNoncePub;
    ringState.ourRingNonceHp     = params.ringOurRingNonceHp;
    ringState.ourRingNonceSec    = params.ringOurRingNonceSec;
  } else {
    SwapTxBuilder::ringRound1Generate(params, ringState);
    params.ringOurPartialKeyImage = ringState.ourPartialKeyImage;
    params.ringOurRingNoncePub    = ringState.ourRingNoncePub;
    params.ringOurRingNonceHp     = ringState.ourRingNonceHp;
    params.ringOurRingNonceSec    = ringState.ourRingNonceSec;
    params.ringOurRound1MaterialValid = true;
    params.ringOurRound1Sent = true;
  }

  // Populate peer Round 1 data from persisted params
  if (params.ringPeerRound1Received) {
    ringState.peerPartialKeyImage = params.ringPeerPartialKeyImage;
    ringState.peerRingNoncePub    = params.ringPeerRingNoncePub;
    ringState.peerRingNonceHp     = params.ringPeerRingNonceHp;
  }

  Crypto::KeyImage zeroKI;
  std::memset(&zeroKI, 0, sizeof(zeroKI));
  if (std::memcmp(&ringState.peerPartialKeyImage, &zeroKI, sizeof(zeroKI)) == 0) {
    m_logger(Logging::WARNING) << "  Peer Ring Round 1 data not yet received. "
      << "Broadcast deferred until peer responds.";
    return false;
  }

  // KI → prefix hash → challenge (correct CryptoNote ring order)
  if (!SwapTxBuilder::writeAggregateKeyImageToTx(tx, ringState)) {
    m_logger(Logging::ERROR) << "Failed to aggregate key image";
    return false;
  }
  if (!CryptoNote::getObjectHash(
      static_cast<CryptoNote::TransactionPrefix&>(tx), prefixHash)) {
    m_logger(Logging::ERROR) << "Failed to recompute prefix hash after KI";
    return false;
  }
  if (!SwapTxBuilder::ringRound1Finalize(prefixHash, ringState)) {
    m_logger(Logging::ERROR) << "Ring Round 1 finalize failed";
    return false;
  }

  // Round 2: sign locally (only if not already done)
  if (!params.ringOurRound2Sent) {
    SwapTxBuilder::ringRound2Sign(params, ringState);
    params.ringOurPartialResponse = ringState.ourPartialResponse;
    params.ringOurRound2Sent = true;
  } else {
    ringState.ourPartialResponse = params.ringOurPartialResponse;
  }

  // Populate peer Round 2 data from persisted params.
  if (params.ringPeerRound2Received) {
    ringState.peerPartialResponse = params.ringPeerPartialResponse;
  }

  // Need peer Round 2 data
  Crypto::EllipticCurveScalar zeroScalar;
  std::memset(&zeroScalar, 0, sizeof(zeroScalar));
  if (std::memcmp(&ringState.peerPartialResponse, &zeroScalar, sizeof(zeroScalar)) == 0) {
    m_logger(Logging::WARNING) << "  Peer Ring Round 2 data not yet received.";
    return false;
  }

  // Finalize Round 2 (assemble complete ring sig)
  if (!SwapTxBuilder::ringRound2Finalize(ringState, tx)) {
    m_logger(Logging::ERROR) << "Ring Round 2 finalize failed";
    return false;
  }

  // Serialize and broadcast
  std::string txHex = SwapTxBuilder::serializeToHex(tx);
  m_logger(Logging::INFO) << "Broadcasting " << txType << " tx (" << txHex.size() / 2 << " bytes)...";

  if (!m_rpc.sendRawTransaction(txHex)) {
    m_logger(Logging::ERROR) << "sendRawTransaction failed for " << txType;
    return false;
  }

  m_logger(Logging::INFO) << "  " << txType << " tx broadcast successfully!";

  // Protocol fee accounting: 2% total (1% initiation + 1% claim) → treasury
  // Both fees are routed to the treasury vault as counter XFG.
  if (params.xfgAmount > 0) {
    if (params.xfgAmount > UINT64_MAX / CryptoNote::parameters::SWAP_FEE_RATE_BPS) {
      m_logger(Logging::ERROR) << "Claim fee multiplication overflow on swap";
      return false;
    }
    uint64_t claimFee = (params.xfgAmount * CryptoNote::parameters::SWAP_FEE_RATE_BPS)
                      / CryptoNote::parameters::SWAP_FEE_RATE_DIVISOR;  // 1% claim
    uint64_t totalProtocolFee = params.protocolFee + claimFee;  // 2% total
    if (totalProtocolFee > 0) {
      m_rpc.addSwapFee(totalProtocolFee);
      m_logger(Logging::INFO) << "  Swap protocol fee (2%): " << totalProtocolFee
          << " → treasury vault";
    }
  }

  if (txType == "spend") {
    m_logger(Logging::INFO) << "  Fuego's Protocol-owned Treasury has covered your network transaction fee (0.0008 XFG / 8k fire) to say thank you for using SwapXFG.";
  }

  return true;
}

bool SwapDaemon::handlePeerMessage(const PeerMessage& msg) {
  // Dispatch incoming peer messages to the appropriate swap and phase.
  m_logger(Logging::INFO) << "Peer message for swap " << msg.swapId
    << " type=" << static_cast<int>(msg.type);

  return m_db.updateSwap(msg.swapId, [&](SwapStateMachine& sm) -> bool {
    SwapParams& params = const_cast<SwapParams&>(sm.params());

    // Peer authentication gate:
    // KEY_EXCHANGE: signature is self-attested by the key carried in the body
    //   (sender proves they own the key they're announcing). Bind it once.
    // All other messages: signature must verify under the bound peerSwapPubKey.
    static const Crypto::PublicKey ZERO_KEY{};
    bool keyExchanged = (std::memcmp(&params.peerSwapPubKey, &ZERO_KEY, sizeof(ZERO_KEY)) != 0);

    switch (msg.type) {
      case PeerMessageType::KEY_EXCHANGE:
        if (keyExchanged) {
          m_logger(Logging::WARNING) << "Duplicate KEY_EXCHANGE rejected for swap " << msg.swapId;
          return false;
        }
        if (std::memcmp(&msg.keyExchange.swapPubKey, &ZERO_KEY, sizeof(ZERO_KEY)) == 0) {
          m_logger(Logging::WARNING) << "KEY_EXCHANGE with zero pubkey rejected";
          return false;
        }
        // When an expected peer key is pre-bound (offer/handshake), require match.
        if (std::memcmp(&params.expectedPeerSwapPubKey, &ZERO_KEY, sizeof(ZERO_KEY)) != 0) {
          if (std::memcmp(&msg.keyExchange.swapPubKey, &params.expectedPeerSwapPubKey,
                          sizeof(Crypto::PublicKey)) != 0) {
            m_logger(Logging::WARNING)
              << "KEY_EXCHANGE rejected: pubkey does not match expectedPeerSwapPubKey for "
              << msg.swapId;
            return false;
          }
        }
        if (!verifyPeerMessage(msg, msg.keyExchange.swapPubKey)) {
          m_logger(Logging::WARNING) << "KEY_EXCHANGE signature invalid for swap " << msg.swapId;
          return false;
        }
        params.peerSwapPubKey = msg.keyExchange.swapPubKey;
        // Once bound, freeze expected so later identity changes are impossible.
        params.expectedPeerSwapPubKey = msg.keyExchange.swapPubKey;
        if (!adaptor_key_aggregate(params)) return false;
        sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
        m_logger(Logging::INFO) << "KEY_EXCHANGE bound peer "
          << Common::podToHex(params.peerSwapPubKey);
        return true;

      default:
        break;  // fall through to post-KEY_EXCHANGE auth gate below.
    }

    // Non-KEY_EXCHANGE messages: peer key must be bound + signature must verify.
    if (!keyExchanged) {
      m_logger(Logging::WARNING) << "Peer message before KEY_EXCHANGE rejected: type="
        << static_cast<int>(msg.type) << " swap=" << msg.swapId;
      return false;
    }
    if (!verifyPeerMessage(msg, params.peerSwapPubKey)) {
      m_logger(Logging::WARNING) << "Peer message signature invalid: type="
        << static_cast<int>(msg.type) << " swap=" << msg.swapId;
      return false;
    }

    switch (msg.type) {
      case PeerMessageType::ADAPTOR_EXCHANGE:
        params.adaptorPoint = msg.adaptorExchange.adaptorPoint;
        params.adaptorDleqQ = msg.adaptorExchange.adaptorDleqQ;
        params.adaptorDleqProof = msg.adaptorExchange.dleqProof;
        params.hashLock = msg.adaptorExchange.htlcHashLock;
        if (!adaptor_verify_adaptor(params, params.escrowPubKey, params.adaptorDleqQ)) {
          m_logger(Logging::ERROR) << "DLEQ proof verification failed!";
          return false;
        }
        return true;

      case PeerMessageType::NONCE_EXCHANGE:
        params.musig2.peerPubNonce = msg.nonceExchange.pubNonce;
        return true;

      case PeerMessageType::PARTIAL_SIG:
        params.musig2.peerPartialSig = msg.partialSig.partialSig;
        if (!adaptor_partial_verify(params)) {
          m_logger(Logging::ERROR) << "Peer partial sig verification failed!";
          return false;
        }
        sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
        return true;

      case PeerMessageType::RING_ROUND1:
        params.ringPeerPartialKeyImage = msg.ringRound1.partialKeyImage;
        params.ringPeerRingNoncePub    = msg.ringRound1.ringNoncePub;
        params.ringPeerRingNonceHp     = msg.ringRound1.ringNonceHp;
        params.ringPeerRound1Received  = true;
        m_logger(Logging::INFO) << "  Received peer Ring Round 1 data (persisted)";
        return true;

      case PeerMessageType::RING_ROUND2:
        params.ringPeerPartialResponse = msg.ringRound2.partialResponse;
        params.ringPeerRound2Received  = true;
        m_logger(Logging::INFO) << "  Received peer Ring Round 2 data (persisted)";
        return true;

      case PeerMessageType::SECRET_REVEAL: {
        // Alice receives adaptor preimage t from Bob.
        // Verify T = t*G matches the negotiated adaptorPoint before accepting.
        if (params.role != SwapRole::ALICE) {
          m_logger(Logging::WARNING) << "SECRET_REVEAL ignored (not Alice)";
          return false;
        }
        Crypto::PublicKey derivedT;
        if (!Crypto::secret_key_to_public_key(msg.secretReveal.adaptorSecret, derivedT)) {
          m_logger(Logging::ERROR) << "SECRET_REVEAL: invalid secret";
          return false;
        }
        if (std::memcmp(&derivedT, &params.adaptorPoint, sizeof(Crypto::PublicKey)) != 0) {
          m_logger(Logging::ERROR) << "SECRET_REVEAL: secret does not match adaptorPoint";
          return false;
        }
        params.adaptorSecret = msg.secretReveal.adaptorSecret;
        params.adaptorSecretReceived = true;
        if (!msg.secretReveal.claimTxId.empty()) {
          params.ctrClaimTxId = msg.secretReveal.claimTxId;
          if (!params.chainState.empty() && params.chainState.find(':') == std::string::npos) {
            params.chainState = params.chainState + ":" + msg.secretReveal.claimTxId;
          }
        }
        m_logger(Logging::INFO) << "  Received adaptor secret from peer (verified against T)";
        if (sm.currentState() == SwapState::ADAPTOR_CTR_LOCKED) {
          sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
        }
        return true;
      }

      case PeerMessageType::ABORT:
        m_logger(Logging::WARNING) << "Peer aborted swap " << msg.swapId;
        return true;

      case PeerMessageType::KEY_EXCHANGE:
        // Unreachable — handled in the first switch.
        return false;

      default:
        m_logger(Logging::ERROR) << "Unknown peer message type: "
          << static_cast<int>(msg.type);
        return false;
    }
  });
}

PriceOracle& SwapDaemon::priceOracle() {
   return m_oracle;
 }

  std::vector<SwapStateMachine> SwapDaemon::getActiveAfkOffers() {
    std::vector<SwapStateMachine> activeOffers;
    uint32_t currentHeight = 0;
    if (!m_rpc.getHeight(currentHeight)) {
      m_logger(Logging::ERROR) << "Cannot query fuegod height for orderbook filtering";
      return activeOffers;
    }

    auto swapIds = m_db.listSwaps();
    for (const auto& id : swapIds) {
      SwapStateMachine sm;
      if (!m_db.loadSwap(id, sm)) continue;
      
    if (sm.currentState() == SwapState::AFK_OFFER_LOCKED) {
      // Remove offers with < 1 hour remaining (~8 blocks)
      if (static_cast<int32_t>(sm.params().xfgTimeoutHeight) - currentHeight >= 8) {
        activeOffers.push_back(sm);
      }
    }

    }
    return activeOffers;
  }
bool SwapDaemon::handleSwapRequest(const std::string& offerId, uint64_t amount,
                         const std::string& takerPubKey, const std::string& proofOfFunds) {
  m_logger(Logging::INFO) << "Received swap request for offer " << offerId << " amount " << amount;

  if (isTakerRateLimited(takerPubKey)) {
    m_logger(Logging::WARNING) << "Taker " << takerPubKey.substr(0, 16) << "... is rate-limited — rejecting";
    return false;
  }

  if (!m_swapRelay) {
    m_logger(Logging::ERROR) << "Swap relay not configured, cannot handle swap request";
    return false;
  }

  CryptoNote::SwapOfferMsg targetOffer;
  bool found = false;
  for (int pair = 0; pair <= static_cast<int>(SwapPair::ZANO); ++pair) {
    auto pairOffers = m_swapRelay->getOffers(pair);
    for (const auto& offer : pairOffers) {
      if (offer.offerId == offerId) {
        targetOffer = offer;
        found = true;
        break;
      }
    }
    if (found) break;
  }

  if (!found) {
    m_logger(Logging::ERROR) << "Offer " << offerId << " not found";
    return false;
  }

  if (!targetOffer.isSoftOrder) {
    m_logger(Logging::ERROR) << "Offer " << offerId << " is not a soft order";
    return false;
  }

  // Only the local maker may lock AFK funds for a soft order. Without this check
  // any gossiped soft offer would cause this node to lock its own XFG.
  if (!m_makerKeysSet ||
      std::memcmp(&targetOffer.makerPubKey, &m_makerPublicKey, sizeof(Crypto::PublicKey)) != 0) {
    m_logger(Logging::WARNING) << "Ignoring soft-order request for non-local maker offer "
                               << offerId;
    return false;
  }

  uint64_t fillAmount = amount;
  uint64_t remaining = targetOffer.xfgAmount - targetOffer.filledAmount;
  if (fillAmount == 0 || fillAmount > remaining) {
    fillAmount = remaining;
  }
  if (fillAmount == 0) {
    m_logger(Logging::ERROR) << "Offer " << offerId << " has no remaining amount";
    return false;
  }

  SwapPair pair = static_cast<SwapPair>(targetOffer.pair);
  IChainClient* client = m_chainRegistry.getClient(pair);
  if (!client) {
    m_logger(Logging::ERROR) << "No chain client for pair " << (int)targetOffer.pair;
    return false;
  }

  uint64_t requiredCtrAmount = 0;
  {
    if (targetOffer.rateNum == 0) {
      m_logger(Logging::ERROR) << "Invalid (zero) rate for offer " << offerId;
      return false;
    }
    // Integer math — avoid double precision loss on large 1e18-divisor chains.
    //   requiredCtr = fillAmount(atomic XFG) * ctrDivisor / rateNum
    // The 1e7 scaling on rateNum and (implicitly) on fillAmount cancels out.
    uint64_t ctrDiv = static_cast<uint64_t>(m_oracle.ctrDivisor(pair));
    uint128_t num = static_cast<uint128_t>(fillAmount) * ctrDiv;
    uint128_t result = num / static_cast<uint128_t>(targetOffer.rateNum);
    if (result > static_cast<uint128_t>(UINT64_MAX)) {
      m_logger(Logging::ERROR) << "CTR amount overflow for offer " << offerId;
      return false;
    }
    requiredCtrAmount = static_cast<uint64_t>(result);
  }

  // Bind the reserve proof to this offer (proof message must equal offerId).
  ChainClientResult proofResult = client->verifyReserveProof(offerId, requiredCtrAmount, proofOfFunds);
  if (!proofResult.success) {
    m_logger(Logging::ERROR) << "Reserve proof failed for offer " << offerId << ": " << proofResult.error;
    recordTakerFailure(takerPubKey);
    return false;
  }

  // Bind expected taker swap pubkey for later KEY_EXCHANGE (anti first-wins).
  Crypto::PublicKey expectedTaker{};
  if (!takerPubKey.empty() && !Common::podFromHex(takerPubKey, expectedTaker)) {
    m_logger(Logging::WARNING) << "takerPubKey is not valid 32-byte hex — KEY_EXCHANGE will be open";
    std::memset(&expectedTaker, 0, sizeof(expectedTaker));
  }

  std::string lockId;
  std::string adaptorPoint;
  std::string preSig;

  if (!m_rpc.createAfkLock(fillAmount, 1, targetOffer.pair, lockId, adaptorPoint, preSig)) {
    m_logger(Logging::ERROR) << "Failed to create AFK lock for offer " << offerId;
    return false;
  }

  // Persist AFK swap record with expected peer identity when possible.
  {
    static const Crypto::PublicKey ZERO{};
    if (std::memcmp(&expectedTaker, &ZERO, sizeof(ZERO)) != 0) {
      SwapParams afkParams;
      afkParams.swapId = lockId;
      afkParams.pair = pair;
      afkParams.role = SwapRole::BOB;
      afkParams.xfgAmount = fillAmount;
      afkParams.ctrAmount = requiredCtrAmount;
      afkParams.expectedPeerSwapPubKey = expectedTaker;
      afkParams.peerSwapPubKey = expectedTaker; // pre-bind offer identity
      SwapStateMachine afkSm(afkParams);
      afkSm.transition(SwapState::AFK_OFFER_LOCKED);
      if (m_db.saveSwap(afkSm)) {
        m_logger(Logging::INFO) << "AFK swap " << lockId
          << " saved with expectedPeerSwapPubKey="
          << Common::podToHex(expectedTaker);
      }
    }
  }

  m_logger(Logging::INFO) << "AFK lock " << lockId << " created for " << fillAmount
                          << " of " << remaining << " XFG on offer " << offerId;

  uint64_t newRemaining = remaining - fillAmount;
  if (!m_swapRelay->updateOfferAmount(offerId, newRemaining)) {
    m_logger(Logging::WARNING) << "Failed to update offer fill state for " << offerId;
  }

  return true;
}

bool SwapDaemon::isTakerRateLimited(const std::string& takerPubKey) {
  std::lock_guard<std::mutex> lock(m_takerMutex);
  auto it = m_takerHistory.find(takerPubKey);
  if (it == m_takerHistory.end()) return false;

  time_t now = time(nullptr);
  time_t oneHourAgo = now - 3600;
  it->second.requestTimes.erase(
    std::remove_if(it->second.requestTimes.begin(),
                   it->second.requestTimes.end(),
                   [oneHourAgo](time_t t) { return t < oneHourAgo; }),
    it->second.requestTimes.end()
  );

  if (it->second.failedSwaps >= TAKER_BAN_THRESHOLD) return true;
  if (it->second.requestTimes.size() >= MAX_TAKER_REQUESTS_PER_HOUR) return true;
  return false;
}

void SwapDaemon::recordTakerFailure(const std::string& takerPubKey) {
  std::lock_guard<std::mutex> lock(m_takerMutex);
  auto& record = m_takerHistory[takerPubKey];
  record.requestTimes.push_back(time(nullptr));
  record.failedSwaps++;
}

void SwapDaemon::pruneTakerHistory() {
  std::lock_guard<std::mutex> lock(m_takerMutex);
  time_t now = std::time(nullptr);
  for (auto it = m_takerHistory.begin(); it != m_takerHistory.end(); ) {
    // Drop entries with no failed swaps and no recent requests (older than 2 hours)
    it->second.requestTimes.erase(
        std::remove_if(it->second.requestTimes.begin(), it->second.requestTimes.end(),
                       [now](time_t t) { return (now - t) > 7200; }),
        it->second.requestTimes.end());
    if (it->second.requestTimes.empty() && it->second.failedSwaps == 0) {
      it = m_takerHistory.erase(it);
    } else {
      ++it;
    }
  }
}

void SwapDaemon::setSocks5Proxy(const std::string& proxy) {
  if (m_p2p) {
    m_p2p->setSocks5Proxy(proxy);
    m_logger(Logging::INFO) << "SOCKS5 proxy set: " << proxy;
  }
}

void SwapDaemon::setMakerKeys(const Crypto::SecretKey& sk, const Crypto::PublicKey& pk) {
  m_makerSecretKey = sk;
  m_makerPublicKey = pk;
  m_makerKeysSet = true;
  // Wire escrow secret encryption immediately so CLI paths (initiate without
  // start()) can persist adaptorSecret / ourSwapSecKey safely.
  std::string keyInput(Common::podToHex(m_makerSecretKey));
  keyInput += "::swap-escrow-enc-key";
  Crypto::cn_context ctx;
  Crypto::Hash derived;
  Crypto::cn_slow_hash(ctx, keyInput.data(), keyInput.size(), derived, 0, 0, 0);
  m_db.setEncryptionKey(std::string(reinterpret_cast<const char*>(derived.data), sizeof(derived.data)));
}

bool SwapDaemon::loadOfferConfig(const std::string& jsonPath) {
  if (!m_swapRelay) {
    m_logger(Logging::ERROR) << "Swap relay not set, cannot initialize OfferManager";
    return false;
  }
  if (!m_makerKeysSet) {
    m_logger(Logging::ERROR) << "Maker keys not set, cannot initialize OfferManager";
    return false;
  }
  m_offerManager.reset(new OfferManager(
    *m_swapRelay, m_makerSecretKey, m_makerPublicKey, m_logger.getLogger()));
  if (!m_offerManager->loadConfig(jsonPath)) {
    m_offerManager.reset();
    return false;
  }
  m_logger(Logging::INFO) << "OfferManager initialized with "
                          << m_offerManager->activeOfferCount() << " offers";
  return true;
}

std::string SwapDaemon::buildStatusJson() {
  std::ostringstream json;
  json << "{";

  uint32_t height = 0;
  m_rpc.getHeight(height);
  json << "\"height\":" << height << ",";

  json << "\"offers\":[";
  if (m_swapRelay) {
    auto offers = m_swapRelay->getAllOffers();
    for (size_t i = 0; i < offers.size(); ++i) {
      if (i > 0) json << ",";
      json << "{\"offerId\":\"" << offers[i].offerId << "\""
           << ",\"pair\":" << (int)offers[i].pair
           << ",\"xfgAmount\":" << offers[i].xfgAmount
           << ",\"filledAmount\":" << offers[i].filledAmount
           << ",\"rateNum\":" << offers[i].rateNum
           << ",\"postedHeight\":" << offers[i].postedHeight
           << ",\"ttlBlocks\":" << offers[i].ttlBlocks
           << ",\"timestamp\":" << offers[i].timestamp
           << ",\"isSell\":" << (offers[i].isSell ? "true" : "false")
           << ",\"isSoftOrder\":" << (offers[i].isSoftOrder ? "true" : "false")
           << "}";
    }
  }
  json << "],";

  json << "\"swaps\":[";
  {
    auto swapIds = m_db.listSwaps();
    bool first = true;
    for (const auto& id : swapIds) {
      SwapStateMachine sm;
      if (!m_db.loadSwap(id, sm)) continue;
      if (sm.isTerminal()) continue;
      if (!first) json << ",";
      first = false;
      const auto& p = sm.params();
      json << "{\"swapId\":\"" << id << "\""
           << ",\"state\":\"" << swapStateToString(sm.currentState()) << "\""
           << ",\"pair\":" << (int)p.pair
           << ",\"timeoutHeight\":" << p.xfgTimeoutHeight
           << "}";
    }
  }
  json << "]}";
  return json.str();
}

bool SwapDaemon::startStatusServer(uint16_t port) {
  m_statusServer.reset(new StatusServer(port,
    [this]() { return this->buildStatusJson(); },
    m_logger.getLogger()));
  if (!m_statusServer->start()) {
    m_statusServer.reset();
    return false;
  }
  return true;
}

bool SwapDaemon::getTreasuryPubKey(Crypto::PublicKey& key) {
  if (m_treasuryPubKeyCached) {
    key = m_treasuryPubKey;
    return true;
  }

  std::string genesisHashHex;
  if (!m_rpc.getCurrencyId(genesisHashHex)) {
    m_logger(Logging::ERROR) << "Failed to get currency ID for treasury key derivation";
    return false;
  }

  // Parse genesis hash bytes from hex
  if (genesisHashHex.size() != 64) {
    m_logger(Logging::ERROR) << "Invalid genesis hash length: " << genesisHashHex.size();
    return false;
  }

  Crypto::Hash genesisHash;
  std::vector<uint8_t> hashBytes = Common::fromHex(genesisHashHex);
  if (hashBytes.size() != sizeof(Crypto::Hash)) {
    m_logger(Logging::ERROR) << "Failed to parse genesis hash hex";
    return false;
  }
  std::memcpy(&genesisHash, hashBytes.data(), sizeof(Crypto::Hash));

  // Derive vault keys: spendKey = SHA256(genesisHash), viewKey = SHA256(spendKey)
  auto vaultKeys = CryptoNote::deriveVaultKeys(genesisHash);
  key = vaultKeys.spendPub;
  m_treasuryPubKey = key;
  m_treasuryPubKeyCached = true;

  m_logger(Logging::INFO) << "Treasury vault key derived from genesis hash";
  return true;
}


} // namespace XfgSwap
