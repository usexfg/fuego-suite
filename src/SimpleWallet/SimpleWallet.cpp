// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
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

#include "SimpleWallet.h"
#include "crypto/subaddress.h"
#include "Common/ConsoleTools.h"

#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <thread>
#include <set>
#include <sstream>
#include <regex>
#include <array>
#include <limits>

#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include "Common/Base58.h"
#include "Common/CommandLine.h"
#include "Common/SignalHandler.h"
#include "Common/StringTools.h"
#include "Common/PathTools.h"
#include "Common/Util.h"
#include "Common/DnsTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolHandler.h"
#include "NodeRpcProxy/NodeRpcProxy.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"
#include "Rpc/HttpClient.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "CryptoNoteCore/DepositCommitment.h"
#include "CryptoNoteCore/AliasIndex.h"
#include "crypto/crypto.h"
#include "crypto/keccak.h"
#include "CryptoNoteCore/BurnProofDataFileGenerator.h"

#include "Wallet/WalletRpcServer.h"
#include "WalletLegacy/WalletLegacy.h"
#include "Wallet/LegacyKeysImporter.h"
#include "WalletLegacy/WalletHelper.h"
#include "Mnemonics/electrum-words.cpp"

#include "Common/CommandLine.h"
#include "Common/SignalHandler.h"
#include "Common/StringTools.h"
#include "Common/PathTools.h"
#include "Common/Util.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolHandler.h"
#include "NodeRpcProxy/NodeRpcProxy.h"

namespace CryptoNote {
  std::string remote_fee_address;
}
#include "Rpc/CoreRpcServerCommandsDefinitions.h"
#include "Rpc/HttpClient.h"

#include "Wallet/WalletRpcServer.h"
#include "WalletLegacy/WalletHelper.h"

#include <Logging/LoggerManager.h>

#include "Common/CommandLine.h"
#include "Common/SignalHandler.h"
#include "Common/StringTools.h"
#include "Common/PathTools.h"
#include "Common/Util.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolHandler.h"
#include "NodeRpcProxy/NodeRpcProxy.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"
#include "Rpc/HttpClient.h"

#include "version.h"

#if defined(WIN32)
#include <crtdbg.h>
#endif

using namespace CryptoNote;
using namespace Logging;
using Common::JsonValue;

namespace po = boost::program_options;

#define EXTENDED_LOGS_FILE "wallet_details.log"
#undef ERROR

const command_line::arg_descriptor<std::string> arg_wallet_file = { "wallet-file", "Use wallet <arg>", "" };
const command_line::arg_descriptor<std::string> arg_generate_new_wallet = { "generate-new-wallet", "Generate new wallet and save it to <arg>", "" };
const command_line::arg_descriptor<std::string> arg_daemon_address = { "daemon-address", "Use daemon instance at <host>:<port>", "" };
const command_line::arg_descriptor<std::string> arg_daemon_host = { "daemon-host", "Use daemon instance at host <arg> instead of localhost", "" };
const command_line::arg_descriptor<std::string> arg_password = { "password", "Wallet password", "", true };
const command_line::arg_descriptor<uint16_t> arg_daemon_port = { "daemon-port", "Use daemon instance at port <arg> instead of default", 0 };
const command_line::arg_descriptor<uint32_t> arg_log_level = { "set_log", "", INFO, true };
const command_line::arg_descriptor<bool> arg_testnet = { "testnet", "Used to deploy test nets. The daemon must be launched with --testnet flag", false };
const command_line::arg_descriptor< std::vector<std::string> > arg_command = { "command", "" };

bool parseUrlAddress(const std::string& url, std::string& address, uint16_t& port) {
  auto pos = url.find("://");
  size_t addrStart = 0;

  if (pos != std::string::npos)
    addrStart = pos + 3;

  auto addrEnd = url.find(':', addrStart);

  if (addrEnd != std::string::npos) {
    auto portEnd = url.find('/', addrEnd);
    port = Common::fromString<uint16_t>(url.substr(
      addrEnd + 1, portEnd == std::string::npos ? std::string::npos : portEnd - addrEnd - 1));
  } else {
    addrEnd = url.find('/');
    port = 80;
  }

  address = url.substr(addrStart, addrEnd - addrStart);
  return true;
}

std::string interpret_rpc_response(bool ok, const std::string& status) {
  std::string err;
  if (ok) {
    if (status == CORE_RPC_STATUS_BUSY) {
      err = "daemon is busy. Please try later";
    } else if (status != CORE_RPC_STATUS_OK) {
      err = status;
    }
  } else {
    err = "possible lost connection to daemon";
  }
  return err;
}

template <typename IterT, typename ValueT = typename IterT::value_type>
class ArgumentReader {
public:

  ArgumentReader(IterT begin, IterT end) :
    m_begin(begin), m_end(end), m_cur(begin) {
  }

  bool eof() const {
    return m_cur == m_end;
  }

  ValueT next() {
    if (eof()) {
      throw std::runtime_error("unexpected end of arguments");
    }

    return *m_cur++;
  }

private:

  IterT m_cur;
  IterT m_begin;
  IterT m_end;
};

struct TransferCommand {
  const CryptoNote::Currency& m_currency;
  size_t fake_outs_count;
  std::vector<CryptoNote::WalletLegacyTransfer> dsts;
  std::vector<uint8_t> extra;
  uint64_t fee;
  std::map<std::string, std::vector<WalletLegacyTransfer>> aliases;
  std::vector<std::string> messages;
  uint64_t ttl;

  TransferCommand(const CryptoNote::Currency& currency) :
    m_currency(currency), fake_outs_count(0), fee(currency.minimumFee()), ttl(0) {
  }

  bool parseArguments(LoggerRef& logger, const std::vector<std::string> &args) {

    ArgumentReader<std::vector<std::string>::const_iterator> ar(args.begin(), args.end());

    try {

      auto arg = ar.next();

      if (arg.size() && arg[0] == '-') {

        const auto& value = ar.next();

        if (arg == "-p") {
          if (!createTxExtraWithPaymentId(value, extra)) {
            logger(ERROR, BRIGHT_RED) << "payment ID has invalid format: \"" << value << "\", expected 64-character string";
            return false;
          }
        } else if (arg == "-m") {
          messages.emplace_back(value);
        } else if (arg == "-ttl") {
          try {
            ttl = boost::lexical_cast<uint64_t>(value);
          } catch (const boost::bad_lexical_cast &) {
            logger(ERROR, BRIGHT_RED) << "TTL has invalid format: \"" << value << "\", expected integer";
            return false;
          }
        }
      } else {
        WalletLegacyTransfer destination;
        CryptoNote::TransactionDestinationEntry de;
        std::string aliasUrl;

        if (!m_currency.parseAccountAddressString(arg, de.addr)) {
          aliasUrl = arg;
        }

        auto value = ar.next();
        bool ok = m_currency.parseAmount(value, de.amount);

        if (!ok || 0 == de.amount) {
          logger(ERROR, BRIGHT_RED) << "amount is wrong: " << arg << ' ' << value <<
            ", expected number from 0 to " << m_currency.formatAmount(std::numeric_limits<uint64_t>::max());
          return false;
        }

        if (aliasUrl.empty()) {
          destination.address = arg;
          destination.amount = de.amount;
          dsts.push_back(destination);
        } else {
          aliases[aliasUrl].emplace_back(WalletLegacyTransfer{"", static_cast<int64_t>(de.amount)});
        }

        if (de.amount < m_currency.minimumFee()) {
          logger(ERROR, BRIGHT_RED) << "Amount must be at least " << m_currency.formatAmount(m_currency.minimumFee());
          return false;
        }
      }

      if (dsts.empty() && aliases.empty()) {
        logger(ERROR, BRIGHT_RED) << "At least one destination address is required";
        return false;
      }
    } catch (const std::exception& e) {
      logger(ERROR, BRIGHT_RED) << e.what();
      return false;
    }

    return true;
  }
};

const size_t TIMESTAMP_MAX_WIDTH = 19;
const size_t HASH_MAX_WIDTH = 64;
const size_t TOTAL_AMOUNT_MAX_WIDTH = 20;
const size_t FEE_MAX_WIDTH = 14;
const size_t BLOCK_MAX_WIDTH = 7;
const size_t UNLOCK_TIME_MAX_WIDTH = 11;

void printListTransfersHeader(LoggerRef& logger) {
  std::string header = Common::makeCenteredString(TIMESTAMP_MAX_WIDTH, "timestamp (UTC)") + "  ";
  header += Common::makeCenteredString(HASH_MAX_WIDTH, "hash") + "  ";
  header += Common::makeCenteredString(TOTAL_AMOUNT_MAX_WIDTH, "total amount") + "  ";
  header += Common::makeCenteredString(FEE_MAX_WIDTH, "fee") + "  ";
  header += Common::makeCenteredString(BLOCK_MAX_WIDTH, "block") + "  ";
  header += Common::makeCenteredString(UNLOCK_TIME_MAX_WIDTH, "unlock time");

  logger(INFO) << header;
  logger(INFO) << std::string(header.size(), '-');
}

void printListTransfersItem(LoggerRef& logger, const WalletLegacyTransaction& txInfo, IWalletLegacy& wallet, const Currency& currency) {
  std::vector<uint8_t> extraVec = Common::asBinaryArray(txInfo.extra);

  Crypto::Hash paymentId;
  std::string paymentIdStr = (getPaymentIdFromTxExtra(extraVec, paymentId) && paymentId != NULL_HASH ? Common::podToHex(paymentId) : "");

  char timeString[TIMESTAMP_MAX_WIDTH + 1];
  time_t timestamp = static_cast<time_t>(txInfo.timestamp);
  if (std::strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", std::gmtime(&timestamp)) == 0) {
    throw std::runtime_error("time buffer is too small");
  }

  std::string rowColor = txInfo.totalAmount < 0 ? MAGENTA : GREEN;
  logger(INFO, rowColor)
    << std::setw(TIMESTAMP_MAX_WIDTH) << timeString
    << "  " << std::setw(HASH_MAX_WIDTH) << Common::podToHex(txInfo.hash)
    << "  " << std::setw(TOTAL_AMOUNT_MAX_WIDTH) << currency.formatAmount(txInfo.totalAmount)
    << "  " << std::setw(FEE_MAX_WIDTH) << currency.formatAmount(txInfo.fee)
    << "  " << std::setw(BLOCK_MAX_WIDTH) << txInfo.blockHeight
    << "  " << std::setw(UNLOCK_TIME_MAX_WIDTH) << txInfo.unlockTime;

  if (!paymentIdStr.empty()) {
    logger(INFO, rowColor) << "payment ID: " << paymentIdStr;
  }

  if (txInfo.totalAmount < 0) {
    if (txInfo.transferCount > 0) {
      logger(INFO, rowColor) << "transfers:";
      for (TransferId id = txInfo.firstTransferId; id < txInfo.firstTransferId + txInfo.transferCount; ++id) {
        WalletLegacyTransfer tr;
        wallet.getTransfer(id, tr);
        logger(INFO, rowColor) << tr.address << "  " << std::setw(TOTAL_AMOUNT_MAX_WIDTH) << currency.formatAmount(tr.amount);
      }
    }
  }

  logger(INFO, rowColor) << " ";
}

std::string prepareWalletAddressFilename(const std::string& walletBaseName) {
  return walletBaseName + ".address";
}

bool writeAddressFile(const std::string& addressFilename, const std::string& address) {
  std::ofstream addressFile(addressFilename, std::ios::out | std::ios::trunc | std::ios::binary);
  if (!addressFile.good()) {
    return false;
  }
  addressFile << address;
  return true;
}

bool processServerAliasResponse(const std::string& s, std::string& address) {
  try {
    auto pos = s.find("oa1:xfg");
    if (pos == std::string::npos)
      return false;
    pos = s.find("recipient_address=", pos);
    if (pos == std::string::npos)
      return false;
    pos += 18;
    auto pos2 = s.find(";", pos);
    if (pos2 != std::string::npos) {
      if (pos2 - pos == 100) {
        address = s.substr(pos, 100);
      } else {
        return false;
      }
    }
  } catch (std::exception&) {
    return false;
  }
  return true;
}

bool splitUrlToHostAndUri(const std::string& aliasUrl, std::string& host, std::string& uri) {
  size_t protoBegin = aliasUrl.find("http://");
  if (protoBegin != 0 && protoBegin != std::string::npos) {
    return false;
  }

  size_t hostBegin = protoBegin == std::string::npos ? 0 : 7;
  size_t hostEnd = aliasUrl.find('/', hostBegin);

  if (hostEnd == std::string::npos) {
    uri = "/";
    host = aliasUrl.substr(hostBegin);
  } else {
    uri = aliasUrl.substr(hostEnd);
    host = aliasUrl.substr(hostBegin, hostEnd - hostBegin);
  }

  return true;
}

bool askAliasesTransfersConfirmation(const std::map<std::string, std::vector<WalletLegacyTransfer>>& aliases, const Currency& currency) {
  std::cout << "Would you like to send money to the following addresses?" << std::endl;

  for (const auto& kv: aliases) {
    for (const auto& transfer: kv.second) {
      std::cout << transfer.address << " " << std::setw(21) << currency.formatAmount(transfer.amount) << "  " << kv.first << std::endl;
    }
  }

  std::string answer;
  do {
    std::cout << "y/n: ";
    std::getline(std::cin, answer);
  } while (answer != "y" && answer != "Y" && answer != "n" && answer != "N");

  return answer == "y" || answer == "Y";
}

bool processServerFeeAddressResponse(const std::string& response, std::string& fee_address) {
  try {
    std::stringstream stream(response);
    JsonValue json;
    stream >> json;

    auto rootIt = json.getObject().find("fee_address");
    if (rootIt == json.getObject().end()) {
      return false;
    }

    fee_address = rootIt->second.getString();
  } catch (std::exception&) {
    return false;
  }
  return true;
}

JsonValue buildLoggerConfiguration(Level level, const std::string& logfile) {
  JsonValue loggerConfiguration(JsonValue::OBJECT);
  loggerConfiguration.insert("globalLevel", static_cast<int64_t>(level));

  JsonValue& cfgLoggers = loggerConfiguration.insert("loggers", JsonValue::ARRAY);

  JsonValue& consoleLogger = cfgLoggers.pushBack(JsonValue::OBJECT);
  consoleLogger.insert("type", "console");
  consoleLogger.insert("level", static_cast<int64_t>(Logging::TRACE));
  consoleLogger.insert("pattern", "");

  JsonValue& fileLogger = cfgLoggers.pushBack(JsonValue::OBJECT);
  fileLogger.insert("type", "file");
  fileLogger.insert("filename", logfile);
  fileLogger.insert("level", static_cast<int64_t>(Logging::TRACE));

  return loggerConfiguration;
}

std::error_code initAndLoadWallet(IWalletLegacy& wallet, std::istream& walletFile, const std::string& password) {
  WalletHelper::InitWalletResultObserver initObserver;
  std::future<std::error_code> f_initError = initObserver.initResult.get_future();

  WalletHelper::IWalletRemoveObserverGuard removeGuard(wallet, initObserver);
  wallet.initAndLoad(walletFile, password);
  auto initError = f_initError.get();

  return initError;
}

std::string tryToOpenWalletOrLoadKeysOrThrow(LoggerRef& logger, std::unique_ptr<IWalletLegacy>& wallet, const std::string& walletFile, const std::string& password) {
  std::string walletFileName;
  boost::system::error_code ignored_ec;

  bool walletFileExists = boost::filesystem::exists(walletFile, ignored_ec);

  if (!walletFileExists) {
    throw std::runtime_error("Wallet file does not exist: " + walletFile);
  }

  walletFileName = walletFile;

  logger(INFO) << "Loading wallet...";

  std::ifstream walletFileStream(walletFileName, std::ios::binary);
  if (!walletFileStream) {
    throw std::runtime_error("Failed to open wallet file: " + walletFileName);
  }

  auto initError = initAndLoadWallet(*wallet, walletFileStream, password);
  if (initError) {
    throw std::runtime_error("Failed to load wallet: " + initError.message());
  }

  return walletFileName;
}

std::string simple_wallet::get_commands_str() {
  std::stringstream ss;
  ss << "Commands: " << ENDL;
  std::string usage = m_consoleHandler.getUsage();
  boost::replace_all(usage, "\n", "\n  ");
  usage.insert(0, "  ");
  ss << usage << ENDL;
  return ss.str();
}

bool simple_wallet::help(const std::vector<std::string> &args) {
  success_msg_writer() << get_commands_str();
  return true;
}

bool simple_wallet::exit(const std::vector<std::string> &args) {
  m_consoleHandler.requestStop();
  return true;
}

simple_wallet::simple_wallet(System::Dispatcher& dispatcher, const CryptoNote::Currency& currency, Logging::LoggerManager& log) :
  m_dispatcher(dispatcher),
  m_daemon_port(0),
  m_currency(currency),
  logManager(log),
  logger(log, "firewallet"),
  m_refresh_progress_reporter(*this),
  m_walletSynchronized(false) {
  m_consoleHandler.setHandler("create_integrated", boost::bind(&simple_wallet::create_integrated, this, boost::arg<1>()), "create_integrated <payment_id> - Create an integrated address with a payment ID");
  m_consoleHandler.setHandler("export_keys", boost::bind(&simple_wallet::export_keys, this, boost::arg<1>()), "Show the secret keys of the current wallet");
  m_consoleHandler.setHandler("balance", boost::bind(&simple_wallet::show_balance, this, boost::arg<1>()), "Show current wallet balance");
  m_consoleHandler.setHandler("sign_message", boost::bind(&simple_wallet::sign_message, this, boost::arg<1>()), "Sign a message with your wallet keys");
  m_consoleHandler.setHandler("verify_signature", boost::bind(&simple_wallet::verify_signature, this, boost::arg<1>()), "Verify a signed message");
  m_consoleHandler.setHandler("incoming_transfers", boost::bind(&simple_wallet::show_incoming_transfers, this, boost::arg<1>()), "Show incoming transfers");
  m_consoleHandler.setHandler("list_transfers", boost::bind(&simple_wallet::listTransfers, this, boost::arg<1>()), "list_transfers <block_height> - Show all known transfers, optionally from a certain block height");
  m_consoleHandler.setHandler("payments", boost::bind(&simple_wallet::show_payments, this, boost::arg<1>()), "payments <payment_id_1> [<payment_id_2> ... <payment_id_N>] - Show payments <payment_id_1>, ... <payment_id_N>");
  m_consoleHandler.setHandler("get_tx_proof", boost::bind(&simple_wallet::get_tx_proof, this, boost::arg<1>()), "Generate a signature to prove payment: <txid> <address> [<txkey>]");
  m_consoleHandler.setHandler("get_reserve_proof", boost::bind(&simple_wallet::get_reserve_proof, this, boost::arg<1>()), "all|<amount> [<message>] - Generate a signature proving that you own at least <amount>, optionally with a challenge string <message>. ");
  m_consoleHandler.setHandler("height", boost::bind(&simple_wallet::show_blockchain_height, this, boost::arg<1>()), "Show blockchain height");
  m_consoleHandler.setHandler("show_dust", boost::bind(&simple_wallet::show_dust, this, boost::arg<1>()), "Show the number of unmixable dust outputs");
  m_consoleHandler.setHandler("outputs", boost::bind(&simple_wallet::show_num_unlocked_outputs, this, boost::arg<1>()), "Show the number of unlocked outputs available for a transaction");
  m_consoleHandler.setHandler("optimize", boost::bind(&simple_wallet::optimize_outputs, this, boost::arg<1>()), "Combine many available outputs into a few by sending a transaction to self");
  m_consoleHandler.setHandler("optimize_all", boost::bind(&simple_wallet::optimize_all_outputs, this, boost::arg<1>()), "Optimize your wallet several times so you can send large transactions");
  m_consoleHandler.setHandler("transfer", boost::bind(&simple_wallet::transfer, this, boost::arg<1>()), "transfer <addr_1> <amount_1> [<addr_2> <amount_2> ... <addr_N> <amount_N>] [-p payment_id] [-m message] - Transfer <amount_1>,... <amount_N> to <address_1>,... <address_N>, respectively. ");
  m_consoleHandler.setHandler("set_log", boost::bind(&simple_wallet::set_log, this, boost::arg<1>()), "set_log <level> - Change current log level, <level> is a number 0-4");
  m_consoleHandler.setHandler("address", boost::bind(&simple_wallet::print_address, this, boost::arg<1>()), "Show current wallet public address");
  m_consoleHandler.setHandler("save", boost::bind(&simple_wallet::save, this, boost::arg<1>()), "Save wallet synchronized data");
  m_consoleHandler.setHandler("reset", boost::bind(&simple_wallet::reset, this, boost::arg<1>()), "Discard cache data and start synchronizing from the start");
  m_consoleHandler.setHandler("help", boost::bind(&simple_wallet::help, this, boost::arg<1>()), "Show this help");
  m_consoleHandler.setHandler("exit", boost::bind(&simple_wallet::exit, this, boost::arg<1>()), "Close wallet");
  m_consoleHandler.setHandler("payment_id", boost::bind(&simple_wallet::payment_id, this, _1), "Generate random Payment ID");
  m_consoleHandler.setHandler("start_mining", boost::bind(&simple_wallet::start_mining, this, boost::arg<1>()), "start_mining [<threads>] - Start mining to your wallet");
  m_consoleHandler.setHandler("stop_mining", boost::bind(&simple_wallet::stop_mining, this, boost::arg<1>()), "stop_mining - Stop mining");

  // Deposit commands
  // TODO: May re-enable 'deposit' command later for backward compatibility
  // m_consoleHandler.setHandler("deposit", boost::bind(&simple_wallet::deposit, this, boost::arg<1>()), "deposit <amount> <term_code> - Create a COLD deposit (0.8, 8, 80, 800 XFG with terms 3=3mo, 12=1yr). ETH address provided at claim time for privacy.");
  // HEAT/COLD deposits temporarily disabled on mainnet - focus on elderfier registration first
  // These will be re-enabled in the next release after elderfiers are registered
  // TODO: Re-enable burn and cold commands in next release
  // m_consoleHandler.setHandler("burn", boost::bind(&simple_wallet::burn, this, boost::arg<1>()), "burn <amount> - Create a HEAT burn deposit (0.8, 8, 80, 800 XFG). Term automatically set to FOREVER.");
  // m_consoleHandler.setHandler("cold", boost::bind(&simple_wallet::cold, this, boost::arg<1>()), "cold <amount> <term_code> - Create a COLD deposit (0.8, 8, 80, 800 XFG with terms 3=3mo, 12=1yr).");
  m_consoleHandler.setHandler("elderking_ceremony", boost::bind(&simple_wallet::elderking_ceremony, this, boost::arg<1>()), "elderking_ceremony - Begins Ælderfire StayKing Ceremony. Details on what is, & how to become, an Ξlderfier (interactive, 20 deposits across all tiers, 4,444 XFG req'd).");
  m_consoleHandler.setHandler("withdraw", boost::bind(&simple_wallet::withdraw, this, boost::arg<1>()), "withdraw <id> - Withdraw a deposit");
  m_consoleHandler.setHandler("unstake", boost::bind(&simple_wallet::unstake, this, boost::arg<1>()), "unstake - Batch-withdraw all Elderfier staking deposits (single tx)");
  m_consoleHandler.setHandler("list_cold", boost::bind(&simple_wallet::list_cold, this, boost::arg<1>()), "list_cold - List all COLD yield deposits");
  m_consoleHandler.setHandler("cold_info", boost::bind(&simple_wallet::cold_info, this, boost::arg<1>()), "cold_info <id> - Get detailed info on your Certificate of Ledger Deposits");
  m_consoleHandler.setHandler("list_burns", boost::bind(&simple_wallet::list_burns, this, boost::arg<1>()), "list_burns - List all XFG burn transactions (HEAT)");
  m_consoleHandler.setHandler("burn_info", boost::bind(&simple_wallet::burn_info, this, boost::arg<1>()), "burn_info <id> - Get detailed info of burn by ID");

  m_consoleHandler.setHandler("migrate_cold", boost::bind(&simple_wallet::migrate_cold, this, boost::arg<1>()), "migrate_cold <id> - Migrate a pre-v3 legacy deposit to v3 format (register commitment for L2 claims)");
  // Hidden — only surfaced inside the elder_council panel. Direct use requires knowing it exists.
  m_consoleHandler.setHandler("propose_slash", boost::bind(&simple_wallet::propose_slash, this, boost::arg<1>()), "");
  m_consoleHandler.setHandler("get_report", boost::bind(&simple_wallet::get_report, this, boost::arg<1>()), "");

  // NOTE: create_cold_secret and gen_proof might be better off as INTERNAL commands
  // Users should NOT manually create commitments (auto-embedded in tx_extra)
  // But users MUST generate STARK proofs from deposits for L2 claims
  m_consoleHandler.setHandler("create_cold_secret", boost::bind(&simple_wallet::create_cold_secret, this, boost::arg<1>()), "create_cold_secret <amount> <term_blocks> <chain_code> <metadata> - Create COLD commitment");
  m_consoleHandler.setHandler("gen_proof", boost::bind(&simple_wallet::gen_proof, this, boost::arg<1>()), "gen_proof <tx_hash> - Data needed to generate STARK proof for deposit transaction (for L2 claims)");

  // @ Alias system commands
  m_consoleHandler.setHandler("register_alias", boost::bind(&simple_wallet::register_alias, this, boost::arg<1>()), "register_alias <alias> - Register an @ alias (8 chars [a-z0-9&], costs 1 XFG). EFier aliases [A-Z0-9&] via elderking_ceremony.");
  m_consoleHandler.setHandler("lookup_alias", boost::bind(&simple_wallet::lookup_alias, this, boost::arg<1>()), "lookup_alias <alias_or_address> - Look up an @ alias by name or wallet address");
  m_consoleHandler.setHandler("list_aliases", boost::bind(&simple_wallet::list_aliases, this, boost::arg<1>()), "list_aliases - List all registered @ aliases on the network");
  // Hidden command — empty usage string keeps it out of help output.
  // Only shows content to registered Elderfiers.
  m_consoleHandler.setHandler("elder_council", boost::bind(&simple_wallet::elder_council, this, boost::arg<1>()), "");
  m_consoleHandler.setHandler("gen_new_sub", boost::bind(&simple_wallet::gen_new_sub, this, boost::arg<1>()), "gen_new_sub [major] [minor] - Generate a sub-address at index (major, minor). Omit args for auto-increment (0, N).");
  m_consoleHandler.setHandler("list_subs", boost::bind(&simple_wallet::list_subs, this, boost::arg<1>()), "list_subs - List all sub-addresses for this wallet");
}

bool simple_wallet::show_dust(const std::vector<std::string>& args) {
  logger(INFO, BRIGHT_WHITE) << "Dust outputs: " << m_wallet->dustBalance() << std::endl;
  return true;
}

bool simple_wallet::set_log(const std::vector<std::string> &args) {
  if (args.size() != 1) {
    fail_msg_writer() << "use: set_log <log_level_number_0-4>";
    return true;
  }

  uint16_t l = 0;
  if (!Common::fromString(args[0], l)) {
    fail_msg_writer() << "wrong number format, use: set_log <log_level_number_0-4>";
    return true;
  }

  if (l > 4) {
    if (l > Logging::TRACE) {
      fail_msg_writer() << "wrong number range, use: set_log <log_level_number_0-4>";
      return true;
    }
  }

  logManager.setMaxLevel(static_cast<Level>(l));
  return true;
}

bool key_import = false;
bool simple_wallet::payment_id(const std::vector<std::string> &args) {
  Crypto::Hash result;
  Crypto::generate_random_bytes(32, result.data);
  std::string pid_str = Common::podToHex(result);
  success_msg_writer() << "Payment ID: " << pid_str;
  return true;
}

bool simple_wallet::init(const boost::program_options::variables_map& vm) {
  handle_command_line(vm);

  if (!m_daemon_address.empty() && (!m_daemon_host.empty() || 0 != m_daemon_port)) {
    fail_msg_writer() << "you can't specify daemon host or port several times";
    return false;
  }

  if (m_daemon_host.empty())
    m_daemon_host = "localhost";
  if (!m_daemon_port) {
    // Use testnet port when in testnet mode, otherwise use mainnet port
    m_daemon_port = m_currency.isTestnet() ? RPC_DEFAULT_PORT_TESTNET : RPC_DEFAULT_PORT;
  }

  if (!m_daemon_address.empty()) {
    if (!parseUrlAddress(m_daemon_address, m_daemon_host, m_daemon_port)) {
      fail_msg_writer() << "failed to parse daemon address: " << m_daemon_address;
      return false;
    }
    remote_fee_address = getFeeAddress();
    logger(INFO, BRIGHT_WHITE) << "Connected to remote node: " << m_daemon_host;
    if (!remote_fee_address.empty()) {
      logger(INFO, BRIGHT_WHITE) << "Fee address: " << remote_fee_address;
    }
  } else {
    if (!m_daemon_host.empty())
      remote_fee_address = getFeeAddress();
    m_daemon_address = std::string("http://") + m_daemon_host + ":" + std::to_string(m_daemon_port);
    logger(INFO, BRIGHT_WHITE) << "Connected to remote node: " << m_daemon_host;
    if (!remote_fee_address.empty()) {
      logger(INFO, BRIGHT_WHITE) << "Fee address: " << remote_fee_address;
    }
  }

  if (m_generate_new.empty() && m_wallet_file_arg.empty()) {
    std::cout << "\n";
    std::cout << "\n";
    std::cout << "       ░░░░░░░ ░░    ░░ ░░░░░░░  ░░░░░░   ░░░░░░        " << "\n";
    std::cout << "       ▒▒      ▒▒    ▒▒ ▒▒      ▒▒       ▒▒    ▒▒       " << "\n";
    std::cout << "       ▒▒▒▒▒   ▒▒    ▒▒ ▒▒▒▒▒   ▒▒   ▒▒▒ ▒▒    ▒▒       " << "\n";
    std::cout << "       ▓▓      ▓▓    ▓▓ ▓▓      ▓▓    ▓▓ ▓▓    ▓▓       " << "\n";
    std::cout << "       ██       ██████  ███████  ██████   ██████        " << "\n";
    std::cout << "\n";
    std::cout << "\n";
    std::cout << "\n";
    std::cout << "Welcome to Fuego command-line wallet." << "\n";
    std::cout << "Please choose from the following options what you would like to do:\n";
    std::cout << "O - Open wallet\n";
    std::cout << "₲ - Generate new wallet\n";
    std::cout << "R - Restore from backup/paperwallet\n";
    std::cout << "I - Import wallet from private keys\n";
    std::cout << "M - Mnemonic seed (25-words) import\n";
    std::cout << "E - Exit\n";
    char c;
    do {
      std::string answer;
      std::getline(std::cin, answer);
      c = answer[0];
      if (!(c == 'O' || c == 'G' || c == 'E' || c == 'I' || c == 'o' || c == 'g' || c == 'e' || c == 'i' || c == 'm' || c == 'M')) {
        std::cout << "Unknown command: " << c << std::endl;
      } else {
        break;
      }
    } while (true);

    if (c == 'E' || c == 'e') {
      return false;
    }

    std::cout << "Specify wallet file name (e.g., name.wallet).\n";
    std::string userInput;
    do {
      std::cout << "Wallet file name: ";
      std::getline(std::cin, userInput);
      boost::algorithm::trim(userInput);
    } while (userInput.empty());
    if (c == 'i' || c == 'I') {
      key_import = true;
      m_import_new = userInput;
    } else if (c == 'm' || c == 'M') {
      key_import = false;
      m_import_new = userInput;
    } else if (c == 'g' || c == 'G') {
      m_generate_new = userInput;
    } else {
      m_wallet_file_arg = userInput;
    }
  }

  if (!m_generate_new.empty() && !m_wallet_file_arg.empty() && !m_import_new.empty()) {
    fail_msg_writer() << "you can't specify 'generate-new-wallet' and 'wallet-file' arguments simultaneously";
    return false;
  }

  std::string walletFileName;
  if (!m_generate_new.empty() || !m_import_new.empty()) {
    std::string ignoredString;
    if (!m_generate_new.empty()) {
      WalletHelper::prepareFileNames(m_generate_new, ignoredString, walletFileName);
    } else if (!m_import_new.empty()) {
      WalletHelper::prepareFileNames(m_import_new, ignoredString, walletFileName);
    }
    boost::system::error_code ignore;
    if (boost::filesystem::exists(walletFileName, ignore)) {
      fail_msg_writer() << walletFileName << " already exists";
      return false;
    }
  }

  Tools::PasswordContainer pwd_container;
  if (command_line::has_arg(vm, arg_password)) {
    pwd_container.password(command_line::get_arg(vm, arg_password));
  } else if (!pwd_container.read_password()) {
    fail_msg_writer() << "failed to read wallet password";
    return false;
  }

  this->m_node.reset(new NodeRpcProxy(m_daemon_host, m_daemon_port));

  std::promise<std::error_code> errorPromise;
  std::future<std::error_code> f_error = errorPromise.get_future();
  auto callback = [&errorPromise](std::error_code e) { errorPromise.set_value(e); };

  m_node->addObserver(static_cast<INodeRpcProxyObserver*>(this));
  m_node->init(callback);
  auto error = f_error.get();
  if (error) {
    fail_msg_writer() << "failed to init NodeRPCProxy: " << error.message();
    return false;
  }

  if (!m_generate_new.empty()) {
    std::string walletAddressFile = prepareWalletAddressFilename(m_generate_new);
    boost::system::error_code ignore;
    if (boost::filesystem::exists(walletAddressFile, ignore)) {
      logger(ERROR, BRIGHT_RED) << "Address file already exists: " + walletAddressFile;
      return false;
    }

    if (!new_wallet(walletFileName, pwd_container.password())) {
      logger(ERROR, BRIGHT_RED) << "account creation failed";
      return false;
    }

    if (!writeAddressFile(walletAddressFile, m_wallet->getAddress())) {
      logger(WARNING, BRIGHT_RED) << "Couldn't write wallet address file: " + walletAddressFile;
    }
  } else if (!m_import_new.empty()) {
    std::string walletAddressFile = prepareWalletAddressFilename(m_import_new);
    boost::system::error_code ignore;
    if (boost::filesystem::exists(walletAddressFile, ignore)) {
      logger(ERROR, BRIGHT_RED) << "Address file already exists: " + walletAddressFile;
      return false;
    }

    std::string private_spend_key_string;
    std::string private_view_key_string;

    Crypto::SecretKey private_spend_key;
    Crypto::SecretKey private_view_key;

    if (key_import) {
      do {
        std::cout << "Private Spend Key: ";
        std::getline(std::cin, private_spend_key_string);
        boost::algorithm::trim(private_spend_key_string);
      } while (private_spend_key_string.empty());
      do {
        std::cout << "Private View Key: ";
        std::getline(std::cin, private_view_key_string);
        boost::algorithm::trim(private_view_key_string);
      } while (private_view_key_string.empty());
    } else {
      std::string mnemonic_phrase;

      do {
        std::cout << "Mnemonics Phrase (25 words): ";
        std::getline(std::cin, mnemonic_phrase);
        boost::algorithm::trim(mnemonic_phrase);
        boost::algorithm::to_lower(mnemonic_phrase);
      } while (!is_valid_mnemonic(mnemonic_phrase, private_spend_key));

      /* This is not used, but is needed to be passed to the function, not sure how we can avoid this */
      Crypto::PublicKey unused_dummy_variable;

      AccountBase::generateViewFromSpend(private_spend_key, private_view_key, unused_dummy_variable);
    }

    /* We already have our keys if we import via mnemonic seed */
    if (key_import) {
      Crypto::Hash private_spend_key_hash;
      Crypto::Hash private_view_key_hash;
      size_t size;
      if (!Common::fromHex(private_spend_key_string, &private_spend_key_hash, sizeof(private_spend_key_hash), size) || size != sizeof(private_spend_key_hash)) {
        return false;
      }
      if (!Common::fromHex(private_view_key_string, &private_view_key_hash, sizeof(private_view_key_hash), size) || size != sizeof(private_spend_key_hash)) {
        return false;
      }
      private_spend_key = *(struct Crypto::SecretKey *)&private_spend_key_hash;
      private_view_key = *(struct Crypto::SecretKey *)&private_view_key_hash;
    }

    if (!new_wallet(private_spend_key, private_view_key, walletFileName, pwd_container.password())) {
      logger(ERROR, BRIGHT_RED) << "account creation failed";
      return false;
    }

    if (!writeAddressFile(walletAddressFile, m_wallet->getAddress())) {
      logger(WARNING, BRIGHT_RED) << "Couldn't write wallet address file: " + walletAddressFile;
    }
  } else {
    m_wallet.reset(new WalletLegacy(m_currency, *m_node, logManager));

    try {
      client_helper ch;
      m_wallet_file = ch.tryToOpenWalletOrLoadKeysOrThrow(logger, m_wallet, m_wallet_file_arg, pwd_container.password());
    } catch (const std::exception& e) {
      fail_msg_writer() << "failed to load wallet: " << e.what();
      return false;
    }

    m_wallet->addObserver(this);
    m_node->addObserver(static_cast<INodeObserver*>(this));

    logger(INFO, BRIGHT_WHITE) << "Opened wallet: " << m_wallet->getAddress();
    loadSubAddresses();

    success_msg_writer() <<
      "**********************************************************************\n" <<
      "Use \"help\" command to see the list of available commands.\n" <<
      "**********************************************************************";
  }

  return true;
}

bool simple_wallet::deinit() {
  m_wallet->removeObserver(this);
  m_node->removeObserver(static_cast<INodeObserver*>(this));
  m_node->removeObserver(static_cast<INodeRpcProxyObserver*>(this));

  if (!m_wallet.get())
    return true;

  return close_wallet();
}

void simple_wallet::handle_command_line(const boost::program_options::variables_map& vm) {
  m_wallet_file_arg = command_line::get_arg(vm, arg_wallet_file);
  m_generate_new = command_line::get_arg(vm, arg_generate_new_wallet);
  m_daemon_address = command_line::get_arg(vm, arg_daemon_address);
  m_daemon_host = command_line::get_arg(vm, arg_daemon_host);
  m_daemon_port = command_line::get_arg(vm, arg_daemon_port);
}

bool simple_wallet::new_wallet(const std::string &wallet_file, const std::string& password) {
  m_wallet_file = wallet_file;

  m_wallet.reset(new WalletLegacy(m_currency, *m_node, logManager));
  m_node->addObserver(static_cast<INodeObserver*>(this));
  m_wallet->addObserver(this);
  try {
    m_initResultPromise.reset(new std::promise<std::error_code>());
    std::future<std::error_code> f_initError = m_initResultPromise->get_future();
    m_wallet->initAndGenerate(password);
    auto initError = f_initError.get();
    m_initResultPromise.reset(nullptr);
    if (initError) {
      fail_msg_writer() << "failed to generate new wallet: " << initError.message();
      return false;
    }

    try {
      CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file);
    } catch (std::exception& e) {
      fail_msg_writer() << "failed to save new wallet: " << e.what();
      throw;
    }

    AccountKeys keys;
    m_wallet->getAccountKeys(keys);

    std::string secretKeysData = std::string(reinterpret_cast<char*>(&keys.spendSecretKey), sizeof(keys.spendSecretKey)) + std::string(reinterpret_cast<char*>(&keys.viewSecretKey), sizeof(keys.viewSecretKey));
    std::string guiKeys = Tools::Base58::encode_addr(CryptoNote::parameters::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX, secretKeysData);

    logger(INFO, BRIGHT_GREEN) << "fire_wallet is an open-source, client-side, free wallet which allows you to send & receive Fuego instantly on the blockchain. Only YOU are in control of your funds & your private keys. When you generate a new wallet, send, receive or deposit XFG - everything happens locally. Your seed is never transmitted, received or stored. IT IS IMPERATIVE that you write down, print, or save your seed phrase somewhere safe. The backup of keys is your responsibility only. If you lose your seed, your account CANNOT be recovered. Freedom isn't free - the cost is responsibility. Schofield's 2nd Law of Computing states that data doesn't really exist unless you have at least two copies of it-- then keep each somewhere safe & secure.   " << std::endl << std::endl;

    std::cout << "Wallet Address: " << m_wallet->getAddress() << std::endl;
    std::cout << "Private spend key: " << Common::podToHex(keys.spendSecretKey) << std::endl;
    std::cout << "Private view key: " << Common::podToHex(keys.viewSecretKey) << std::endl;
    std::cout << "Mnemonic Seed: " << generate_mnemonic(keys.spendSecretKey) << std::endl;

  }
  catch (const std::exception& e) {
    fail_msg_writer() << "failed to generate new wallet: " << e.what();
    return false;
  }

  success_msg_writer() <<
    "**********************************************************************\n" <<
    "Your wallet has been generated.\n" <<
    "Use \"help\" command to see the list of available commands.\n" <<
    "Always use \"exit\" command when closing fire_wallet to save\n" <<
    "current session's state. Otherwise, you will possibly need to synchronize \n" <<
    "your wallet again. Your wallet keys are not under risk in doing so.\n" <<
    "**********************************************************************";
  return true;
}

bool simple_wallet::new_wallet(Crypto::SecretKey &secret_key, Crypto::SecretKey &view_key, const std::string &wallet_file, const std::string& password) {
  m_wallet_file = wallet_file;

  m_wallet.reset(new WalletLegacy(m_currency, *m_node.get(), logManager));
  m_node->addObserver(static_cast<INodeObserver*>(this));
  m_wallet->addObserver(this);
  try {
    m_initResultPromise.reset(new std::promise<std::error_code>());
    std::future<std::error_code> f_initError = m_initResultPromise->get_future();

    AccountKeys wallet_keys;
    wallet_keys.spendSecretKey = secret_key;
    wallet_keys.viewSecretKey = view_key;
    Crypto::secret_key_to_public_key(wallet_keys.spendSecretKey, wallet_keys.address.spendPublicKey);
    Crypto::secret_key_to_public_key(wallet_keys.viewSecretKey, wallet_keys.address.viewPublicKey);

    m_wallet->initWithKeys(wallet_keys, password);
    auto initError = f_initError.get();
    m_initResultPromise.reset(nullptr);
    if (initError) {
      fail_msg_writer() << "failed to generate new wallet: " << initError.message();
      return false;
    }

    try {
      CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file);
    } catch (std::exception& e) {
      fail_msg_writer() << "failed to save new wallet: " << e.what();
      throw;
    }

    AccountKeys keys;
    m_wallet->getAccountKeys(keys);

    logger(INFO, BRIGHT_WHITE) <<
      "Imported wallet: " << m_wallet->getAddress() << std::endl;
  }
  catch (const std::exception& e) {
    fail_msg_writer() << "failed to import wallet: " << e.what();
    return false;
  }

  success_msg_writer() <<
    "**********************************************************************\n" <<
    "Your wallet has been imported.\n" <<
    "Use \"help\" command to see the list of available commands.\n" <<
    "Always use \"exit\" command when closing fire_wallet to save\n" <<
    "current session's state. Otherwise, you will possibly need to synchronize \n" <<
    "your wallet again. Your wallet key is not under risk in doing so.\n" <<
    "**********************************************************************";
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::deposit(const std::vector<std::string> &args)
{
  // No ETH address required at deposit time
  // Recipient binding happens at STARK proof generation (xfg-stark-cli)
  // This prevents linking Fuego deposits to ETH addresses on-chain
  if (args.size() != 2)
  {
    fail_msg_writer() << "Usage: deposit <amount> <term_code>";
    fail_msg_writer() << "Amount tiers: 0.8, 8, 80, 800 XFG";
    fail_msg_writer() << "Term codes: 3 (3 months), 12 (1 year)";
    fail_msg_writer() << "";
    fail_msg_writer() << "For XFG burns (HEAT), use: burn <amount>";
    fail_msg_writer() << "";
    fail_msg_writer() << "ETH address is provided later when generating STARK proof.";
    fail_msg_writer() << "         This prevents linking your Fuego wallet to your ETH address on-chain.";
    return true;
  }

  try
  {
    // Parse and validate amount
    uint64_t deposit_amount = 0;
    bool ok = m_currency.parseAmount(args[0], deposit_amount);

    if (!ok || 0 == deposit_amount)
    {
      fail_msg_writer() << "Invalid amount format: " << args[0];
      return true;
    }

    // Validate amount is one of the allowed tiers (same tiers for HEAT burns and COLD deposits)
    std::vector<uint64_t> valid_amounts = {
      CryptoNote::parameters::AMOUNT_TIER_0,                       // 0.8 XFG
      CryptoNote::parameters::AMOUNT_TIER_1,                       // 8 XFG
      CryptoNote::parameters::AMOUNT_TIER_2,                       // 80 XFG
      CryptoNote::parameters::AMOUNT_TIER_3                        // 800 XFG
    };

    std::vector<std::string> amount_labels = {
      "0.8 XFG (HEAT/COLD)",
      "8 XFG (HEAT/COLD)",
      "80 XFG (HEAT/COLD)",
      "800 XFG (HEAT/COLD)"
    };

    auto it = std::find(valid_amounts.begin(), valid_amounts.end(), deposit_amount);
    if (it == valid_amounts.end()) {
      fail_msg_writer() << "Invalid amount. Valid tiers:";
      for (const auto& label : amount_labels) {
        fail_msg_writer() << "  " << label;
      }
      return true;
    }

    size_t amount_index = std::distance(valid_amounts.begin(), it);
    std::string amount_label = amount_labels[amount_index];

    // Helper function to check if amount is HEAT burn (0.8 XFG tier)
    // auto is_heat_burn_amount = [](uint64_t amount) -> bool {
    //  return amount == CryptoNote::parameters::AMOUNT_TIER_0;
    // };

    // Parse term code
    uint32_t term_code = boost::lexical_cast<uint32_t>(args[1]);
    uint32_t deposit_term = 0;
    std::string term_label = "";

    // Define valid terms based on network
    uint32_t min_term = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_COLD_MIN_TERM : CryptoNote::parameters::COLD_MIN_TERM;
    uint32_t max_term = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_COLD_MAX_TERM : CryptoNote::parameters::COLD_MAX_TERM;

    // Validate term codes - only COLD deposits (3 or 12), no HEAT (0)
    if (term_code == 3) {
      deposit_term = min_term;
      term_label = "3 months";
    } else if (term_code == 12) {
      deposit_term = max_term;
      term_label = "1 year";
    } else {
      fail_msg_writer() << "Invalid term code. Valid terms for deposit:";
      fail_msg_writer() << "  (3) for 3-month COLD deposit";
      fail_msg_writer() << "  (12) for 1-year COLD deposit";
      fail_msg_writer() << "";
      fail_msg_writer() << "For XFG burns (HEAT), use: burn <amount>";
      return true;
    }

    // Confirm with user
    success_msg_writer() << "Creating deposit:";
    success_msg_writer() << "  Amount: " << m_currency.formatAmount(deposit_amount) << " (" << amount_label << ")";
    success_msg_writer() << "  Term: " << term_label << " (" << deposit_term << " blocks)";

    std::string confirm;
    success_msg_writer() << "Confirm? (y/n): ";
    std::getline(std::cin, confirm);
    if (confirm != "y" && confirm != "Y") {
      success_msg_writer() << "Deposit cancelled";
      return true;
    }

    success_msg_writer() << "Creating deposit with commitment...";

    // Create transaction extra with commitment
    std::vector<uint8_t> extra;
    std::string extraString;

    // No ETH address in commitment
    // Recipient binding happens at STARK proof generation time (xfg-stark-cli)
    // This prevents Fuego deposits from linking to ETH addresses on-chain

    // Generate a random 32-byte secret
    Crypto::PublicKey public_key;
    Crypto::SecretKey secret_key;
    Crypto::generate_keys(public_key, secret_key);

    // Convert secret to array for commitment computation
    std::array<uint8_t, 32> secret_array;
    std::copy(secret_key.data, secret_key.data + 32, secret_array.begin());

    // Create a tx_prefix_hash placeholder (hash of secret for binding)
    Crypto::Hash placeholder_tx_hash;
    keccak(secret_key.data, sizeof(secret_key.data), placeholder_tx_hash.data, sizeof(placeholder_tx_hash.data));

    // Network parameters
    uint32_t network_id = 1; // Fuego mainnet
    uint32_t target_chain_id = 1; // Ethereum mainnet
    uint32_t commitment_version = 1;

    // Empty metadata - no ETH address stored (privacy)
    std::vector<uint8_t> metadata;

    if (deposit_term == CryptoNote::parameters::DEPOSIT_TERM_FOREVER) {
      // Create HEAT commitment for burn deposit (no term, FOREVER)
      Crypto::Hash commitment = CryptoNote::computeHeatCommitment(
        secret_array,
        deposit_amount,
        placeholder_tx_hash,
        network_id,
        target_chain_id,
        commitment_version
      );

      if (commitment == Crypto::Hash{}) {
        fail_msg_writer() << "Failed to compute HEAT commitment";
        return true;
      }

      // Create the transaction extra with the HEAT commitment
      if (!CryptoNote::createTxExtraWithHeatCommitment(commitment, deposit_amount, metadata, extra)) {
        fail_msg_writer() << "Failed to create HEAT commitment";
        return true;
      }

      success_msg_writer() << "HEAT commitment generated: " << Common::podToHex(commitment);
      success_msg_writer() << "Secret key (STORE SECURELY): " << Common::podToHex(secret_key);
      success_msg_writer() << "";
      success_msg_writer() << "IMPORTANT: Save the secret key! You will need it + your ETH address";
      success_msg_writer() << "           when generating the STARK proof with xfg-stark-cli.";
    } else {
      // Create COLD deposit commitment for yield deposit (with term)
      uint8_t chain_code = 1; // Ethereum mainnet chain code
      std::vector<uint8_t> gift_secret; // Empty - not gifting

      Crypto::Hash commitment = CryptoNote::computeColdCommitment(
        secret_array,
        deposit_amount,
        placeholder_tx_hash,
        network_id,
        target_chain_id,
        commitment_version,
        deposit_term
      );

      if (commitment == Crypto::Hash{}) {
        fail_msg_writer() << "Failed to compute COLD commitment";
        return true;
      }

      // Create the transaction extra with COLD commitment
      if (!CryptoNote::createTxExtraWithColdCommitment(commitment, deposit_amount, deposit_term, chain_code, metadata, gift_secret, extra)) {
        fail_msg_writer() << "Failed to create COLD commitment data";
        return true;
      }

      success_msg_writer() << "COLD commitment generated: " << Common::podToHex(commitment);
      success_msg_writer() << "Secret key (STORE SECURELY): " << Common::podToHex(secret_key);
      success_msg_writer() << "";
      success_msg_writer() << "IMPORTANT: Save the secret key! You will need it + your ETH address";
      success_msg_writer() << "           when generating the STARK proof with xfg-stark-cli.";
    }

    // Convert extra vector to string
    extraString = std::string(extra.begin(), extra.end());

    // Use IWalletLegacy deposit method with extra data
    uint64_t fee = m_currency.minimumFee();
    CryptoNote::TransactionId txId = m_wallet->deposit(deposit_term, deposit_amount, fee, extraString, 0);

    if (txId == CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      fail_msg_writer() << "Failed to create deposit transaction";
      return true;
    }

    if (deposit_amount == CryptoNote::parameters::AMOUNT_TIER_0) {
      success_msg_writer(true) << "HEAT burn transaction created successfully!";
      success_msg_writer() << "Transaction ID: " << txId;
      success_msg_writer() << "Amount burned: " << m_currency.formatAmount(deposit_amount);
      success_msg_writer() << "";
      success_msg_writer() << "For privacy, your ETH address is never part of on-chain transactions.";
      success_msg_writer() << "Use xfg-stark-cli to generate a STARK proof and claim HEAT (Fuego Embers) tokens.";
      success_msg_writer() << "You will provide your ETH address when generating the STARK proof.";
    } else {
      success_msg_writer(true) << "COLD transaction created successfully!";
      success_msg_writer() << "Transaction ID: " << txId;
      success_msg_writer() << "Amount to be locked: " << m_currency.formatAmount(deposit_amount);
      success_msg_writer() << "Term: " << term_label;
      success_msg_writer() << "";
      success_msg_writer() << "For privacy, your ETH address is never included in commitment.";
      success_msg_writer() << "Use xfg-stark-cli to generate a STARK proof for claiming.";
      success_msg_writer() << "You will provide your ETH address when generating the STARK proof.";
    }
  }
  catch (const std::system_error& e)
  {
    fail_msg_writer() << "System error: " << e.what();
  }
  catch (const std::exception& e)
  {
    fail_msg_writer() << "Error: " << e.what();
  }
  catch (...)
  {
    fail_msg_writer() << "unknown error";
  }

 return true;
}

//----------------------------------------------------------------------------------------------------
// DISABLED: Internal command - users should not create their own commitments

bool simple_wallet::create_cold_secret(const std::vector<std::string> &args) {
 // ETH address recipient binding at STARK proof generation time for privacy
 if (args.size() != 3) {
   fail_msg_writer() << "usage: create_cold_secret <amount> <term_blocks> <chain_code>";
   fail_msg_writer() << "  amount: amount in atomic XFG (e.g., 80000000 for 8 XFG)";
   fail_msg_writer() << "  term_blocks: deposit term in blocks (e.g., 16440 for 3 months)";
   fail_msg_writer() << "  chain_code: target claim chain (1=ETH, 2=ARB)";
   fail_msg_writer() << "";
   fail_msg_writer() << "PRIVACY NOTE: ETH address NOT required for XFG deposits.";
   fail_msg_writer() << "         You will provide your ETH address when generating the STARK proof.";
   fail_msg_writer() << "";
   fail_msg_writer() << "Example: create_cold_secret 80000000 16440 1";
   return true;
 }

 try {
   uint64_t amount = boost::lexical_cast<uint64_t>(args[0]);
   uint32_t term_blocks = boost::lexical_cast<uint32_t>(args[1]);
   uint8_t chain_code = boost::lexical_cast<uint8_t>(args[2]);

   // Validate amount is a valid tier
   if (!CryptoNote::BurnProofDataFileGenerator::isValidXfgAmount(amount)) {
     fail_msg_writer() << "Invalid amount. Must be one of: 8000000 (0.8 XFG), 80000000 (8 XFG), 800000000 (80 XFG), 8000000000 (800 XFG)";
     return true;
   }

   // Generate a random secret key
   Crypto::PublicKey public_key;
   Crypto::SecretKey secret_key;
   Crypto::generate_keys(public_key, secret_key);

   // Convert secret to array for commitment computation
   std::array<uint8_t, 32> secret_array;
   std::copy(secret_key.data, secret_key.data + 32, secret_array.begin());

   // Placeholder tx_hash for standalone commitment generation
   // Use secret-derived hash since we don't have actual tx yet
   Crypto::Hash placeholder_tx_hash;
   {
     std::vector<uint8_t> binding_data;
     binding_data.insert(binding_data.end(), secret_key.data, secret_key.data + 32);
     // Add term to binding for uniqueness
     binding_data.insert(binding_data.end(),
       reinterpret_cast<const uint8_t*>(&term_blocks),
       reinterpret_cast<const uint8_t*>(&term_blocks) + sizeof(term_blocks));
     keccak(binding_data.data(), binding_data.size(), placeholder_tx_hash.data, sizeof(placeholder_tx_hash.data));
   }

   // PRIVACY MODEL: Compute COLD commitment WITHOUT recipient (ETH address)
   // Recipient binding happens at STARK proof generation time
   uint32_t network_id = 1;
   uint32_t target_chain_id = chain_code;  // Use chain_code as target chain
   uint32_t commitment_version = 1;

   Crypto::Hash commitment = CryptoNote::computeColdCommitment(
     secret_array, amount, placeholder_tx_hash,
     network_id, target_chain_id, commitment_version, term_blocks
   );

   if (commitment == Crypto::Hash{}) {
     fail_msg_writer() << "Failed to compute COLD commitment";
     return true;
   }

   // Empty metadata - no ETH address stored on-chain for privacy
   std::vector<uint8_t> metadata;
   std::vector<uint8_t> gift_secret; // Empty - not gifting

   // Create the transaction extra with COLD commitment
   std::vector<uint8_t> extra;
   if (!CryptoNote::createTxExtraWithColdCommitment(commitment, amount, term_blocks, chain_code, metadata, gift_secret, extra)) {
     fail_msg_writer() << "Failed to create COLD commitment data";
     return true;
   }

   success_msg_writer() << "COLD commitment created successfully:";
   success_msg_writer() << "Commitment: " << Common::podToHex(commitment);
   success_msg_writer() << "Secret Key (STORE SECURELY): " << Common::podToHex(secret_key);
   success_msg_writer() << "Amount: " << amount << " atomic XFG (" << (amount / 10000000.0) << " XFG)";
   success_msg_writer() << "Term: " << term_blocks << " blocks";
   success_msg_writer() << "Chain Code: " << static_cast<int>(chain_code);
   success_msg_writer() << "";
   success_msg_writer() << "For privacy, your ETH address is added during zkSTARK generation & never in Fuego blockchain.";
   success_msg_writer() << "IMPORTANT: Save the secret key! You will need it when generating";
   success_msg_writer() << "           your STARK proof for interest redemption using xfg-stark-cli.";

 } catch (const std::exception& e) {
   fail_msg_writer() << "Failed to parse arguments: " << e.what();
   return true;
 }

 return true;
}


//----------------------------------------------------------------------------------------------------
bool simple_wallet::close_wallet()
{
  try {
    CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file);
  } catch (const std::exception& e) {
    fail_msg_writer() << e.what();
    return false;
  }

  m_wallet->removeObserver(this);
  m_wallet->shutdown();

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::save(const std::vector<std::string> &args)
{
  try {
    CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file);
    success_msg_writer() << "Wallet data saved";
  } catch (const std::exception& e) {
    fail_msg_writer() << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::list_cold(const std::vector<std::string> &)
{
  size_t deposit_count = m_wallet->getDepositCount();

  if (deposit_count == 0)
  {
    success_msg_writer() << "No deposits found";
    return true;
  }

  success_msg_writer() << "Deposits (" << deposit_count << "):";
  success_msg_writer() << "ID    | Amount             | Term          | Unlock Height | Status";
  success_msg_writer() << "------|--------------------|---------------|---------------|--------";

  // go through deposits ids for the amount of deposits in wallet
  for (CryptoNote::DepositId id = 0; id < deposit_count; ++id)
  {
    // get deposit info from id and store it to deposit
    CryptoNote::Deposit deposit;
    if (!m_wallet->getDeposit(id, deposit)) {
      continue; // Skip invalid deposits
    }

    // Skip burns / HEAT txns — those belong in list_burns only
    if (deposit.term == CryptoNote::parameters::DEPOSIT_TERM_FOREVER) {
      continue;
    }

    // Skip EFier stakes — those belong in elder_council only
    uint32_t efTerm = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_DEPOSIT_TERM_ELDERFIER_STAKING
                                              : CryptoNote::parameters::DEPOSIT_TERM_ELDERFIER_STAKING;
    if (deposit.term == efTerm) {
      continue;
    }

    // Format amount (interest handled off-chain via L2)
    std::string amount_str = m_currency.formatAmount(deposit.amount);

    // Format term (COLD deposits only — HEAT and EFier already filtered)
    uint32_t coldMin = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_COLD_MIN_TERM
                                               : CryptoNote::parameters::COLD_MIN_TERM;
    uint32_t coldMax = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_COLD_MAX_TERM
                                               : CryptoNote::parameters::COLD_MAX_TERM;

    std::string term_str;
    if (deposit.term == coldMin) {
      term_str = "3 months";
    } else if (deposit.term == coldMax) {
      term_str = "1 year";
    } else {
      term_str = std::to_string(deposit.term) + " blocks";
    }

    // Format unlock height
    std::string unlock_str = "";
    if (deposit.locked) {
      unlock_str = (deposit.unlockHeight == 0) ? "Pending" : std::to_string(deposit.unlockHeight);
    } else if (deposit.spendingTransactionId != CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      unlock_str = "Withdrawn";
    } else {
      unlock_str = "Unlocked";
    }

    // Format status
    std::string status_str = "";
    if (deposit.locked) {
      status_str = "Locked";
    } else if (deposit.spendingTransactionId == CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      status_str = "Unlocked";
    } else {
      status_str = "Withdrawn";
    }

    success_msg_writer() << std::left <<
      std::setw(5)  << std::to_string(id) << " | " <<
      std::setw(18) << amount_str << " | " <<
      std::setw(13) << term_str << " | " <<
      std::setw(13) << unlock_str << " | " <<
      std::setw(8) << status_str;
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::burn(const std::vector<std::string> &args)
{
  // Simplified burn command - just takes amount, term is always FOREVER
  if (args.size() != 1)
  {
    fail_msg_writer() << "Usage: burn <amount>";
    fail_msg_writer() << "Valid amounts: 0.8, 8, 80, 800 XFG";
    fail_msg_writer() << "";
    fail_msg_writer() << "Creates an XFG burn (PERMANENT!) for minting an equivalent amount of HEAT.";
    fail_msg_writer() << "ETH address is provided later when generating STARK proof.";
    return true;
  }

  try
  {
    // Parse and validate amount
    uint64_t burn_amount = 0;
    bool ok = m_currency.parseAmount(args[0], burn_amount);

    if (!ok || 0 == burn_amount)
    {
      fail_msg_writer() << "Invalid amount format: " << args[0];
      return true;
    }

    // Validate amount is one of the allowed tiers
    std::vector<uint64_t> valid_amounts = {
      CryptoNote::parameters::AMOUNT_TIER_0,  // 0.8 XFG
      CryptoNote::parameters::AMOUNT_TIER_1,  // 8 XFG
      CryptoNote::parameters::AMOUNT_TIER_2,  // 80 XFG
      CryptoNote::parameters::AMOUNT_TIER_3   // 800 XFG
    };

    std::vector<std::string> amount_labels = {
      "0.8 XFG",
      "8 XFG",
      "80 XFG",
      "800 XFG"
    };

    auto it = std::find(valid_amounts.begin(), valid_amounts.end(), burn_amount);
    if (it == valid_amounts.end()) {
      fail_msg_writer() << "Invalid amount. Valid tiers:";
      for (const auto& label : amount_labels) {
        fail_msg_writer() << "  " << label;
      }
      return true;
    }

    size_t amount_index = std::distance(valid_amounts.begin(), it);
    std::string amount_label = amount_labels[amount_index];

    // HEAT burn deposits always use DEPOSIT_TERM_FOREVER
    uint32_t burn_term = CryptoNote::parameters::DEPOSIT_TERM_FOREVER;
    std::string term_label = "XFG burn (forever)";

    // Determine banking fee based on amount tier
    uint64_t banking_fee = 0;
    if (burn_amount == CryptoNote::parameters::AMOUNT_TIER_0) {
      banking_fee = CryptoNote::parameters::BANK_FEE_TIER_0;
    } else if (burn_amount == CryptoNote::parameters::AMOUNT_TIER_1) {
      banking_fee = CryptoNote::parameters::BANK_FEE_TIER_1;
    } else if (burn_amount == CryptoNote::parameters::AMOUNT_TIER_2) {
      banking_fee = CryptoNote::parameters::BANK_FEE_TIER_2;
    } else if (burn_amount == CryptoNote::parameters::AMOUNT_TIER_3) {
      banking_fee = CryptoNote::parameters::BANK_FEE_TIER_3;
    }
    uint64_t fee = m_currency.minimumFee();

    // Confirmation
    success_msg_writer() << "";
    success_msg_writer() << "Burn XFG Transaction Summary:";
    success_msg_writer() << "  Amount: " << m_currency.formatAmount(burn_amount) << " XFG (PERMANENT)";
    success_msg_writer() << "  Banking Fee: " << m_currency.formatAmount(banking_fee) << " XFG (0.1% of amount to Elderfiers)";
    success_msg_writer() << "  Network Fee: " << m_currency.formatAmount(m_currency.minimumFee()) << " XFG (minimum txn fee to miners)";
    success_msg_writer() << "  Commitment Type: 〘HEAT〙 These funds will be BURNED (enabling HEAT minting rights)";
    success_msg_writer() << "";
    success_msg_writer() << "Confirm? (1) OK  (2) No ";

    std::string confirm;
    m_consoleHandler.readLine(confirm);

    if (confirm != "1" && confirm != "OK" && confirm != "Ok" && confirm != "ok") {
      success_msg_writer() << "Cancelled.";
      return true;
    }

    // Generate unified STARK commitment (v3) for burn
    auto starkResult = CryptoNote::StarkCommitmentGenerator::generate(
        burn_amount,
        CryptoNote::parameters::DEPOSIT_TERM_FOREVER,
        CryptoNote::parameters::STARK_NETWORK_ID_MAINNET,
        CryptoNote::parameters::STARK_TARGET_CHAIN_ETH,
        CryptoNote::parameters::STARK_COMMITMENT_VERSION);

    // Display secret — user's claim ticket for xfg-stark-cli proof generation
    success_msg_writer() << "";
    success_msg_writer() << "STARK Commitment Data (SAVE THIS — needed to claim HEAT):";
    success_msg_writer() << "  Secret:     " << Common::podToHex(starkResult.secret);
    success_msg_writer() << "  Commitment: " << Common::podToHex(starkResult.commitment);
    success_msg_writer() << "  Nullifier:  " << Common::podToHex(starkResult.nullifier);
    success_msg_writer() << "";

    std::vector<uint8_t> extra;
    CryptoNote::TransactionExtraHeatCommitment heatCommitment;
    heatCommitment.commitment = starkResult.commitment;
    heatCommitment.amount = burn_amount;
    heatCommitment.metadata = {0x08};

    CryptoNote::addHeatCommitmentToExtra(extra, heatCommitment);

    // Encrypt STARK secret into tx extra (0xD5) so burn_info can retrieve it
    CryptoNote::AccountKeys walletKeys;
    m_wallet->getAccountKeys(walletKeys);
    CryptoNote::DepositSecretPayload secretPayload;
    secretPayload.depositType = 0x08; // HEAT
    secretPayload.amount = burn_amount;
    secretPayload.term = CryptoNote::parameters::DEPOSIT_TERM_FOREVER;
    memcpy(secretPayload.depositSecret, &starkResult.secret, 32);
    CryptoNote::TransactionExtraDepositSecret encSecret;
    if (CryptoNote::encryptDepositSecret(secretPayload, walletKeys.address.viewPublicKey, encSecret)) {
      CryptoNote::addDepositSecretToExtra(extra, encSecret);
    }

    std::string extraString(extra.begin(), extra.end());

    // Send the burn deposit transaction
    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

    CryptoNote::TransactionId txId = m_wallet->deposit(burn_term, burn_amount, fee + banking_fee, extraString, 0);

    if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == txId) {
      fail_msg_writer() << "Sending burn transaction failed";
      return true;
    }

    std::error_code sendError = sent.wait(txId);
    removeGuard.removeObserver();

    if (sendError) {
      fail_msg_writer() << "Burn transaction failed: " << sendError.message();
      return true;
    }

    success_msg_writer() << "XFG burn transaction sent! ID: " << txId;
    return true;
  }
  catch (const std::exception& e)
  {
    fail_msg_writer() << "Error: " << e.what();
    return true;
  }
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::cold(const std::vector<std::string> &args)
{
  // Simplified COLD deposit command - amount + term code (3 or 12)
  if (args.size() != 2)
  {
    fail_msg_writer() << "Usage: cold <amount> <term_code>";
    fail_msg_writer() << "Valid amounts: 0.8, 8, 80, 800 XFG";
    fail_msg_writer() << "Valid term codes: 3 (3 months) or 12 (1 year)";
    fail_msg_writer() << "";
    return true;
  }

  try
  {
    // Parse and validate amount
    uint64_t cold_amount = 0;
    bool ok = m_currency.parseAmount(args[0], cold_amount);

    if (!ok || 0 == cold_amount)
    {
      fail_msg_writer() << "Invalid amount format: " << args[0];
      return true;
    }

    // Validate amount is one of the allowed tiers
    std::vector<uint64_t> valid_amounts = {
      CryptoNote::parameters::AMOUNT_TIER_0,  // 0.8 XFG
      CryptoNote::parameters::AMOUNT_TIER_1,  // 8 XFG
      CryptoNote::parameters::AMOUNT_TIER_2,  // 80 XFG
      CryptoNote::parameters::AMOUNT_TIER_3   // 800 XFG
    };

    std::vector<std::string> amount_labels = {
      "0.8 XFG",
      "8 XFG",
      "80 XFG",
      "800 XFG"
    };

    auto it = std::find(valid_amounts.begin(), valid_amounts.end(), cold_amount);
    if (it == valid_amounts.end()) {
      fail_msg_writer() << "Invalid amount. Valid tiers:";
      for (const auto& label : amount_labels) {
        fail_msg_writer() << "  " << label;
      }
      return true;
    }

    size_t amount_index = std::distance(valid_amounts.begin(), it);
    std::string amount_label = amount_labels[amount_index];

    // Parse term code
    uint32_t term_code = boost::lexical_cast<uint32_t>(args[1]);
    uint32_t cold_term = 0;
    std::string term_label = "";

    // Define valid terms based on network
    uint32_t min_term = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_COLD_MIN_TERM : CryptoNote::parameters::COLD_MIN_TERM;
    uint32_t max_term = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_COLD_MAX_TERM : CryptoNote::parameters::COLD_MAX_TERM;

    // Validate term codes - only 3 or 12
    if (term_code == 3) {
      cold_term = min_term;
      term_label = "3 months";
    } else if (term_code == 12) {
      cold_term = max_term;
      term_label = "1 year";
    } else {
      fail_msg_writer() << "Invalid term code. Valid terms:";
      fail_msg_writer() << "  (3) for 3-month COLD term";
      fail_msg_writer() << "  (12) for 1-year COLD term";
      return true;
    }

    // Determine banking fee based on amount tier
    uint64_t banking_fee = 0;
    if (cold_amount == CryptoNote::parameters::AMOUNT_TIER_0) {
      banking_fee = CryptoNote::parameters::BANK_FEE_TIER_0;
    } else if (cold_amount == CryptoNote::parameters::AMOUNT_TIER_1) {
      banking_fee = CryptoNote::parameters::BANK_FEE_TIER_1;
    } else if (cold_amount == CryptoNote::parameters::AMOUNT_TIER_2) {
      banking_fee = CryptoNote::parameters::BANK_FEE_TIER_2;
    } else if (cold_amount == CryptoNote::parameters::AMOUNT_TIER_3) {
      banking_fee = CryptoNote::parameters::BANK_FEE_TIER_3;
    }
    uint64_t fee = m_currency.minimumFee();

    // Confirmation
    success_msg_writer() << "";
    success_msg_writer() << "Your XFG Certificate Of Ledger Deposit Summary:";
    success_msg_writer() << "  Amount: " << m_currency.formatAmount(cold_amount) << " XFG";
    success_msg_writer() << "  Term: " << term_label << " (" << cold_term << " blocks)";
    success_msg_writer() << "  Banking Fee: " << m_currency.formatAmount(banking_fee) << " XFG (0.1% of amount to Elderfiers)";
    success_msg_writer() << "  Network Fee: " << m_currency.formatAmount(m_currency.minimumFee()) << " XFG (minimum txn fee to miners)";
    success_msg_writer() << "  Commitment Type: 【COLD】 ▋ Off-chain (CD) interest yield";
    success_msg_writer() << "";

    success_msg_writer() << "Confirm? (1) OK  (2) NO  ";

    std::string confirm;
    m_consoleHandler.readLine(confirm);

    if (confirm != "1" && confirm != "OK" && confirm != "Ok" && confirm != "ok") {
      success_msg_writer() << "Cancelled.";
      return true;
    }

    // Generate unified STARK commitment (v3) for COLD deposit
    auto starkResult = CryptoNote::StarkCommitmentGenerator::generate(
        cold_amount,
        cold_term,
        CryptoNote::parameters::STARK_NETWORK_ID_MAINNET,
        CryptoNote::parameters::STARK_TARGET_CHAIN_ETH,
        CryptoNote::parameters::STARK_COMMITMENT_VERSION);

    // Display secret — user's claim ticket for xfg-stark-cli proof generation
    success_msg_writer() << "";
    success_msg_writer() << "STARK Commitment Data (SAVE THIS — needed to claim CD interest):";
    success_msg_writer() << "  Secret:     " << Common::podToHex(starkResult.secret);
    success_msg_writer() << "  Commitment: " << Common::podToHex(starkResult.commitment);
    success_msg_writer() << "  Nullifier:  " << Common::podToHex(starkResult.nullifier);
    success_msg_writer() << "";

    std::vector<uint8_t> extra;
    CryptoNote::TransactionExtraColdCommitment coldCommitment;
    coldCommitment.commitment = starkResult.commitment;
    coldCommitment.amount = cold_amount;
    coldCommitment.term = cold_term;
    coldCommitment.claimChainCode = 1;  // Default to ETH chain

    CryptoNote::addColdCommitmentToExtra(extra, coldCommitment);

    // Encrypt STARK secret into tx extra (0xD5) so cold_info can retrieve it
    CryptoNote::AccountKeys walletKeys;
    m_wallet->getAccountKeys(walletKeys);
    CryptoNote::DepositSecretPayload secretPayload;
    secretPayload.depositType = 0xCD; // COLD
    secretPayload.amount = cold_amount;
    secretPayload.term = cold_term;
    memcpy(secretPayload.depositSecret, &starkResult.secret, 32);
    CryptoNote::TransactionExtraDepositSecret encSecret;
    if (CryptoNote::encryptDepositSecret(secretPayload, walletKeys.address.viewPublicKey, encSecret)) {
      CryptoNote::addDepositSecretToExtra(extra, encSecret);
    }

    std::string extraString(extra.begin(), extra.end());

    // Send the COLD deposit transaction
    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

    CryptoNote::TransactionId txId = m_wallet->deposit(cold_term, cold_amount, fee + banking_fee, extraString, 0);

    if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == txId) {
      fail_msg_writer() << "Sending deposit transaction failed";
      return true;
    }

    std::error_code sendError = sent.wait(txId);
    removeGuard.removeObserver();

    if (sendError) {
      fail_msg_writer() << "COLD transaction failed: " << sendError.message();
      return true;
    }

    success_msg_writer() << "COLD transaction sent! ID: " << txId;
    return true;
  }
  catch (const std::exception& e)
  {
    fail_msg_writer() << "Error: " << e.what();
    return true;
  }
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::elderking_ceremony(const std::vector<std::string> &args)
{
  // Interactive Elderfire StayKing Ceremony.
  // Alias is chosen interactively — no command-line arg needed.

  // ── PRE-CHECK: detect existing ceremony deposits for resume ────────────
  uint32_t efTerm = CryptoNote::parameters::DEPOSIT_TERM_ELDERFIER_STAKING;
  const uint32_t totalDeposits = CryptoNote::parameters::ELDERKING_TOTAL_DEPOSITS;
  size_t existingStakes = 0;
  {
    size_t depCount = m_wallet->getDepositCount();
    for (CryptoNote::DepositId di = 0; di < depCount; ++di) {
      CryptoNote::Deposit d;
      if (!m_wallet->getDeposit(di, d)) continue;
      if (d.term == efTerm) ++existingStakes;
    }
  }

  if (existingStakes >= totalDeposits) {
    success_msg_writer() << "";
    success_msg_writer() << "  You already have " << totalDeposits << " Elderfier stakes. Use elder_council.";
    success_msg_writer() << "";
    return true;
  }

  std::string alias;
  bool resuming = (existingStakes > 0);

  if (resuming) {
    // ── RESUME FLOW ──────────────────────────────────────────────────────
    success_msg_writer() << "";
    success_msg_writer() << "  ── CEREMONY RESUME ──────────────────────────────────────";
    success_msg_writer() << "";
    success_msg_writer() << "  " << existingStakes << "/" << totalDeposits << " Elderfier stakes detected.";
    success_msg_writer() << "  " << (totalDeposits - existingStakes) << " remaining stake(s) to complete the ceremony.";
    success_msg_writer() << "";

    bool aliasFound = false;
    try {
      HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
      COMMAND_RPC_GET_ALIAS_BY_ADDRESS::request addrReq;
      COMMAND_RPC_GET_ALIAS_BY_ADDRESS::response addrRes;
      addrReq.address = m_wallet->getAddress();
      invokeJsonCommand(httpClient, "/get_alias_by_address", addrReq, addrRes);
      if (addrRes.found && addrRes.alias_type == 0) {
        alias = addrRes.alias;
        aliasFound = true;
        success_msg_writer() << "  Registered alias: @" << alias;
      }
    } catch (...) {}

    if (!aliasFound) {
      success_msg_writer() << "  Alias not yet confirmed on-chain.";
      success_msg_writer() << "  Enter your Ælder King name to resume: ";
      m_consoleHandler.readLine(alias);
      while (!alias.empty() && std::isspace((unsigned char)alias.front())) alias.erase(alias.begin());
      while (!alias.empty() && std::isspace((unsigned char)alias.back()))  alias.pop_back();
      if (alias.empty() || !CryptoNote::AliasIndex::isValidElderfierAlias(alias)) {
        fail_msg_writer() << "  Invalid or empty alias. Resume aborted.";
        return true;
      }
    }

    success_msg_writer() << "";
    success_msg_writer() << "  Resuming ceremony for Ælder King " << alias << "...";
    success_msg_writer() << "";

  } else {

  // ── PART I: THE CALLING ──────────────────────────────────────────────────
  success_msg_writer() << "";
  success_msg_writer() << "╔════════════════════════════════════════════════════════════╗";
  success_msg_writer() << "║                                                            ║";
  success_msg_writer() << "║         THE ΞLDERFIRE STAYKING CEREMONY                    ║";
  success_msg_writer() << "║                                                            ║";
  success_msg_writer() << "╚════════════════════════════════════════════════════════════╝";
  success_msg_writer() << "";
  success_msg_writer() << "  You stand at the threshold of the Ælder Kings Council.";
  success_msg_writer() << "  Before you lies a path of honour, sacrifice, and power.";
  success_msg_writer() << "";
  success_msg_writer() << "  ── WHAT IS AN ΞLDERFIER? ────────────────────────────────";
  success_msg_writer() << "";
  success_msg_writer() << "  Ξlderfiers are the guardians of the Fuego realm — keepers";
  success_msg_writer() << "  of cross-chain truth. They validate deposit commitments,";
  success_msg_writer() << "  sign merkle roots, and ensure that XFG burned on Fuego is";
  success_msg_writer() << "  faithfully reborn on Ethereum. Without Elderfiers, the";
  success_msg_writer() << "  bridge between worlds cannot hold.";
  success_msg_writer() << "";
  success_msg_writer() << "  ── WHAT IT TAKES TO BE AN ÆLDER KING ───────────────────";
  success_msg_writer() << "";
  success_msg_writer() << "  COURAGE   A King must be brave in the face of adversity.";
  success_msg_writer() << "            When the network is under attack, when nodes";
  success_msg_writer() << "            fall, when pressure mounts — you do not falter.";
  success_msg_writer() << "";
  success_msg_writer() << "  JUSTICE   A King must be just in all their judgments.";
  success_msg_writer() << "            Signing only merkle roots believed to be from the one true Fuego chain. Never";
  success_msg_writer() << "            colluding or deceiving; and always placing strength of network over personal gain.";
  success_msg_writer() << "";
  success_msg_writer() << "  PROTECTION  A King must protect the weak and innocent.";
  success_msg_writer() << "            The small holders, newcomers, the silent burners";
  success_msg_writer() << "            who trust the bridge — their safety is yours.";
  success_msg_writer() << "";
  success_msg_writer() << "  VIGILANCE   A King must guard the Realm of Privacy without rest.";
  success_msg_writer() << "            Run your Elderfier node at all times. A Guardian";
  success_msg_writer() << "            of the Flame must protect the Realm of Privacy.";
  success_msg_writer() << "";
  success_msg_writer() << "  HONOUR    Your 4,444 XFG stake is your bond to the Realm.";
  success_msg_writer() << "            Betrayal will result in SLASHING — your stake";
  success_msg_writer() << "            burned and your name struck from the registry.";
  success_msg_writer() << "";
  success_msg_writer() << "  ── WHAT THE CEREMONY REQUIRES ───────────────────────────";
  success_msg_writer() << "";
  success_msg_writer() << "    20 deposits across all 4 tiers (4,444 XFG total stake)";
  success_msg_writer() << "    5x 0.8 XFG + 5x 8 XFG + 5x 80 XFG + 5x 800 XFG";
  success_msg_writer() << "    Tagged 0xEF — the Ælderfier mark — slashable stake";
  success_msg_writer() << "    A unique 8-character Ælder King name (your on-chain ID)";
  success_msg_writer() << "    You MUST run an Elderfier node to sign roots & earn fees";
  success_msg_writer() << "";
  success_msg_writer() << "  Will you head the calling of the Ælder Kings Council?";
  success_msg_writer() << "  Pledge to be brave, be just, protect the innocent,";
  success_msg_writer() << "  and guard the Realm with all your might and honour..??";
  success_msg_writer() << "";
  success_msg_writer() << "  Type 'aye' to step forward, or Enter to walk away: ";

  std::string acceptance;
  m_consoleHandler.readLine(acceptance);
  // Abort if not aye/y/yes; proceed when user says aye
  if (acceptance.empty() || !(acceptance[0] == 'a' || acceptance[0] == 'A' || acceptance[0] == 'y' || acceptance[0] == 'Y')) {
    success_msg_writer() << "";
    success_msg_writer() << "  The Ælder Council watches. Return when you are ready.";
    success_msg_writer() << "  The Realm awaits those worthy of guarding the flame.";
    success_msg_writer() << "";
    return true;
  }

  success_msg_writer() << "";
  success_msg_writer() << "  The Ælder Council nods. You have answered the call.";
  success_msg_writer() << "";

  // ── PART II: CHOOSE YOUR ELDER KING NAME ────────────────────────────────
  success_msg_writer() << "  ── CHOOSE YOUR ELDER KING NAME ─────────────────────────";
  success_msg_writer() << "";
  success_msg_writer() << "  Your Ælder King name is your eternal identity on the Fuego";
  success_msg_writer() << "  network. It will be embedded in all of your stakes and";
  success_msg_writer() << "  registered on-chain the moment your deposits confirm.";
  success_msg_writer() << "";
  success_msg_writer() << "  Rules:  Exactly 8 characters  |  A-Z  0-9  & only";
  success_msg_writer() << "  No two Kings may share a name.";
  success_msg_writer() << "";
  success_msg_writer() << "  Examples:";
  success_msg_writer() << "    FIRENODE  |  IGNITE88  |  ETHGUARD  |  BLAZE&KG";
  success_msg_writer() << "    REALM001  |  VAULT888  |  DRAGON&1  |  XFG&KING";
  success_msg_writer() << "";

  std::string alias;
  while (true) {
    success_msg_writer() << "  Enter your Ælder King name: ";
    m_consoleHandler.readLine(alias);
    // Trim whitespace
    while (!alias.empty() && std::isspace((unsigned char)alias.front())) alias.erase(alias.begin());
    while (!alias.empty() && std::isspace((unsigned char)alias.back()))  alias.pop_back();

    if (alias.empty()) {
      success_msg_writer() << "";
      success_msg_writer() << "  The Ælder Council watches. Return when you are ready.";
      return true;
    }
    if (!CryptoNote::AliasIndex::isValidElderfierAlias(alias)) {
      fail_msg_writer() << "  Invalid name '" << alias << "'.";
      fail_msg_writer() << "  Use exactly 8 characters [A-Z 0-9 &]";
      fail_msg_writer() << "  (Press Enter with no input to abort.)";
      continue;
    }
    break;
  }

  // ── PART III: THE OATH ───────────────────────────────────────────────────
  success_msg_writer() << "";
  success_msg_writer() << "  Before the Eternal Flame and the eyes of the Ælder Council,";
  success_msg_writer() << "  hear the oath of Ælder King " << alias << ":";
  success_msg_writer() << "";
  success_msg_writer() << "  ════════════════════════════════════════════════════════";
  success_msg_writer() << "";
  success_msg_writer() << "  Ælder King " << alias << ", do you vow:";
  success_msg_writer() << "";
  success_msg_writer() << "    to be BRAVE in the face of all adversity — to stand";
  success_msg_writer() << "    firm when the network is tested, threatened, or besieged?";
  success_msg_writer() << "";
  success_msg_writer() << "    to be JUST in all your judgments — to sign only the";
  success_msg_writer() << "    roots you believe true, and never act for dishonest gain?";
  success_msg_writer() << "";
  success_msg_writer() << "    to PROTECT the weak and innocent — to safeguard those";
  success_msg_writer() << "    who burn their XFG in trust that the bridge will hold?";
  success_msg_writer() << "";
  success_msg_writer() << "    to GUARD the Realm of Fuego with all your might and";
  success_msg_writer() << "    honour — running your node with vigilance, not deceit?";
  success_msg_writer() << "";
  success_msg_writer() << "    to honour the ETERNAL FLAME — knowing that betrayal";
  success_msg_writer() << "    brings slashing, and that your stake is your sacred bond";
  success_msg_writer() << "    to this network, now and for all time?";
  success_msg_writer() << "";
  success_msg_writer() << "  ════════════════════════════════════════════════════════";
  success_msg_writer() << "";
  success_msg_writer() << "  To seal these vows, enter your Ælder King name again: ";

  std::string confirmAlias;
  m_consoleHandler.readLine(confirmAlias);
  while (!confirmAlias.empty() && std::isspace((unsigned char)confirmAlias.front())) confirmAlias.erase(confirmAlias.begin());
  while (!confirmAlias.empty() && std::isspace((unsigned char)confirmAlias.back()))  confirmAlias.pop_back();

  if (confirmAlias != alias) {
    fail_msg_writer() << "";
    fail_msg_writer() << "  The names do not match.";
    fail_msg_writer() << "  Entered '" << confirmAlias << "' — your chosen name was '" << alias << "'.";
    fail_msg_writer() << "  The Realm demands certainty. Return when your resolve is firm.";
    return true;
  }

  success_msg_writer() << "";
  success_msg_writer() << "  So be it.  Arise, King " << alias << "...";
  success_msg_writer() << " Protector of the Realm And Ξlderfier of the Fuego P2P Network";

  // ── RPC: check alias availability ───────────────────────────────────────
  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
    COMMAND_RPC_GET_ALIAS::request checkReq;
    COMMAND_RPC_GET_ALIAS::response checkRes;
    checkReq.alias = alias;
    invokeJsonCommand(httpClient, "/get_alias", checkReq, checkRes);
    if (checkRes.found) {
      fail_msg_writer() << "  The name @" << alias << " is already claimed by another Elder King.";
      fail_msg_writer() << "  Ceremony aborted. Run elderking_ceremony again to choose a new name.";
      return true;
    }
  } catch (const ConnectException&) {
    printConnectionError();
    return true;
  } catch (const std::exception& e) {
    fail_msg_writer() << "Failed to check alias availability: " << e.what();
    return true;
  }

  // ── RPC: check address not already registered ────────────────────────────
  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
    COMMAND_RPC_GET_ALIAS_BY_ADDRESS::request addrReq;
    COMMAND_RPC_GET_ALIAS_BY_ADDRESS::response addrRes;
    addrReq.address = m_wallet->getAddress();
    invokeJsonCommand(httpClient, "/get_alias_by_address", addrReq, addrRes);
    if (addrRes.found) {
      fail_msg_writer() << "  Your address already bears the name @" << addrRes.alias;
      fail_msg_writer() << "  An Ælder King may not be crowned twice. Ceremony aborted.";
      return true;
    }
  } catch (const ConnectException&) {
    printConnectionError();
    return true;
  } catch (const std::exception& e) {
    fail_msg_writer() << "Failed to check address alias: " << e.what();
    return true;
  }

  } // end of new-ceremony interactive prompts (else block)

  // ══════════════════════════════════════════════════════════════════════
  // COMMON PATH: balance check, dry run, ceremony loop
  // Both new-ceremony and resume paths reach here with 'alias' set.
  // ══════════════════════════════════════════════════════════════════════

  try
  {
    // ── Balance check ────────────────────────────────────────────────────
    uint64_t balance = m_wallet->actualBalance();
    const uint64_t tierAmounts[] = {
      CryptoNote::parameters::AMOUNT_TIER_0,  // 0.8 XFG
      CryptoNote::parameters::AMOUNT_TIER_1,  // 8 XFG
      CryptoNote::parameters::AMOUNT_TIER_2,  // 80 XFG
      CryptoNote::parameters::AMOUNT_TIER_3   // 800 XFG
    };
    const uint32_t depositsPerTier = CryptoNote::parameters::ELDERKING_DEPOSITS_PER_TIER;
    uint64_t fee = m_currency.minimumFee();

    // Calculate remaining deposits and amount needed
    uint64_t remainingRequired = 0;
    uint32_t remainingCount = 0;
    {
      uint32_t cnt = 0;
      for (uint32_t tier = 0; tier < 4; ++tier) {
        for (uint32_t d = 0; d < depositsPerTier; ++d) {
          if (cnt >= existingStakes) {
            remainingRequired += tierAmounts[tier];
            remainingCount++;
          }
          cnt++;
        }
      }
    }

    success_msg_writer() << "  ── PREPARING THE RITUAL ─────────────────────────────────";
    success_msg_writer() << "";
    success_msg_writer() << "  Wallet balance:      " << m_currency.formatAmount(balance) << " XFG";
    if (resuming) {
      success_msg_writer() << "  Remaining stakes:    " << remainingCount << " deposits ("
                           << m_currency.formatAmount(remainingRequired) << " XFG)";
    } else {
      success_msg_writer() << "  20 stakes (5/tier):  " << m_currency.formatAmount(remainingRequired) << " XFG";
      success_msg_writer() << "    5x 0.8 + 5x 8 + 5x 80 + 5x 800 XFG";
    }
    success_msg_writer() << "  Network fees (x" << remainingCount << "): " << m_currency.formatAmount(remainingCount * fee) << " XFG";
    success_msg_writer() << "  Total required:      " << m_currency.formatAmount(remainingRequired + (remainingCount * fee)) << " XFG";
    success_msg_writer() << "";

    if (balance < remainingRequired + (remainingCount * fee)) {
      fail_msg_writer() << "  The flame requires more ħeat.";
      fail_msg_writer() << "  You need " << m_currency.formatAmount(remainingRequired + (remainingCount * fee) - balance) << " more XFG.";
      fail_msg_writer() << "  Ceremony aborted. Return when your coffers are ready.";
      return true;
    }

    // ── DRY RUN: verify wallet has enough separate outputs ──────────────
    // After each deposit TX, change outputs are locked (unconfirmed) and
    // cannot be spent by subsequent deposits. We simulate the same bucket-
    // based selection that selectTransfersToSend uses: group outputs into
    // power-of-10 buckets, take one from each bucket per round (smallest
    // bucket first) until neededMoney is covered. Change is NOT added back.
    {
      auto unspent = m_wallet->getUnspentOutputs();
      std::vector<uint64_t> available;
      for (const auto& out : unspent) {
        if (out.amount > m_currency.defaultDustThreshold())
          available.push_back(out.amount);
      }

      // Build deposit requirements in ceremony order (tier 0→3), skipping done deposits
      struct DepositReq { uint64_t amount; uint32_t count; };
      std::vector<DepositReq> reqs;
      {
        uint32_t cnt = 0;
        for (uint32_t tier = 0; tier < 4; ++tier) {
          uint32_t tierRemaining = 0;
          for (uint32_t d = 0; d < depositsPerTier; ++d) {
            if (cnt >= existingStakes) tierRemaining++;
            cnt++;
          }
          if (tierRemaining > 0)
            reqs.push_back({tierAmounts[tier], tierRemaining});
        }
      }

      // Simulate selectTransfersToSend bucket algorithm for each deposit
      bool dryRunOk = true;
      for (const auto& req : reqs) {
        for (uint32_t d = 0; d < req.count; ++d) {
          uint64_t needed = req.amount + fee;

          // Group remaining outputs into base-10 digit buckets
          std::map<int, std::vector<size_t>> buckets;
          for (size_t i = 0; i < available.size(); ++i) {
            if (available[i] > 0) {
              int digits = static_cast<int>(std::floor(std::log10(static_cast<double>(available[i])))) + 1;
              buckets[digits].push_back(i);
            }
          }

          // Take one output from each bucket per round (smallest bucket first)
          uint64_t found = 0;
          std::vector<size_t> consumed;
          while (found < needed && !buckets.empty()) {
            for (auto it = buckets.begin(); it != buckets.end(); ) {
              if (it->second.empty()) {
                it = buckets.erase(it);
              } else {
                if (found < needed) {
                  size_t idx = it->second.back();
                  it->second.pop_back();
                  found += available[idx];
                  consumed.push_back(idx);
                }
                ++it;
              }
            }
          }

          if (found < needed) {
            dryRunOk = false;
            break;
          }

          // Remove consumed outputs (mark as 0 so indices stay stable)
          for (size_t idx : consumed) {
            available[idx] = 0;
          }
        }
        if (!dryRunOk) break;
      }

      // Clean up zeroed entries
      available.erase(std::remove(available.begin(), available.end(), uint64_t(0)), available.end());

      if (!dryRunOk) {
        fail_msg_writer() << "";
        fail_msg_writer() << "  Dry run failed: not enough separate outputs for " << remainingCount << " deposits.";
        fail_msg_writer() << "  Each deposit consumes wallet outputs, and change from pending";
        fail_msg_writer() << "  transactions is not immediately spendable.";
        fail_msg_writer() << "";
        fail_msg_writer() << "  Type 'prepare' to auto-split your funds for the ceremony,";
        fail_msg_writer() << "  or press Enter to abort: ";
        std::string choice;
        m_consoleHandler.readLine(choice);
        while (!choice.empty() && std::isspace((unsigned char)choice.front())) choice.erase(choice.begin());
        while (!choice.empty() && std::isspace((unsigned char)choice.back()))  choice.pop_back();

        if (choice == "prepare" || choice == "PREPARE" || choice == "Prepare") {
          success_msg_writer() << "";
          success_msg_writer() << "  Preparing " << remainingCount << " outputs for the ceremony...";

          std::string selfAddr = m_wallet->getAddress();
          std::vector<CryptoNote::WalletLegacyTransfer> prepTransfers;
          uint32_t cnt = 0;
          for (uint32_t tier = 0; tier < 4; ++tier) {
            for (uint32_t d = 0; d < depositsPerTier; ++d) {
              if (cnt >= existingStakes) {
                CryptoNote::WalletLegacyTransfer t;
                t.address = selfAddr;
                t.amount = tierAmounts[tier] + fee;
                prepTransfers.push_back(t);
              }
              cnt++;
            }
          }

          Crypto::SecretKey prepTxSK;
          CryptoNote::WalletHelper::SendCompleteResultObserver prepSent;
          WalletHelper::IWalletRemoveObserverGuard prepGuard(*m_wallet, prepSent);

          CryptoNote::TransactionId prepTxId = m_wallet->sendTransaction(
            prepTxSK, prepTransfers, fee, std::string(), 0, 0, {}, 0);

          if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == prepTxId) {
            prepGuard.removeObserver();
            fail_msg_writer() << "  Failed to create preparation transaction.";
            return true;
          }

          std::error_code prepErr = prepSent.wait(prepTxId);
          prepGuard.removeObserver();
          if (prepErr) {
            fail_msg_writer() << "  Preparation failed: " << prepErr.message();
            return true;
          }

          try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
          catch (...) {}

          success_msg_writer() << "  Preparation TX broadcast (TX: " << prepTxId << ").";
          success_msg_writer() << "  Wait for 1 block confirmation, then run elderking_ceremony again.";
          success_msg_writer() << "";
          return true;
        }

        fail_msg_writer() << "  Ceremony aborted. Consolidate your outputs and try again.";
        return true;
      }
      success_msg_writer() << "  Dry run passed: outputs can cover all " << remainingCount << " deposits.";
    }

    success_msg_writer() << "  The balance holds. The Ritual of Five Flames begins.";
    success_msg_writer() << "";
    success_msg_writer() << "╔════════════════════════════════════════════════════════════╗";
    success_msg_writer() << "║            THE RITUAL OF FIVE FLAMES                       ║";
    success_msg_writer() << "╚════════════════════════════════════════════════════════════╝";
    success_msg_writer() << "";

    static const char* const tierNames[] = { "(0.8 XFG)", "(8 XFG)", "(80 XFG)", "(800 XFG)" };

    // Fetch spend public key once — used to build per-deposit commitment
    CryptoNote::AccountKeys walletKeys;
    m_wallet->getAccountKeys(walletKeys);

    // Derive a deterministic signing keypair from the wallet's spend secret key.
    Crypto::PublicKey signingPubKey;
    Crypto::SecretKey signingSecKey;
    {
      static const char label[] = "fuego_ef_sign___";  // 16 bytes
      uint8_t preimage[48];
      std::memcpy(preimage,      label, 16);
      std::memcpy(preimage + 16, walletKeys.spendSecretKey.data, 32);
      Crypto::hash_to_scalar(preimage, sizeof(preimage),
        reinterpret_cast<Crypto::EllipticCurveScalar&>(signingSecKey));
      Crypto::secret_key_to_public_key(signingSecKey, signingPubKey);
    }

    uint32_t flameCount = 0;
    bool firstDeposit = (existingStakes == 0);  // alias already registered if resuming
    for (uint32_t tier = 0; tier < 4; ++tier) {
      // Skip entire tier if all its deposits are already done
      uint32_t tierEnd = (tier + 1) * depositsPerTier;
      if (tierEnd <= existingStakes) {
        flameCount += depositsPerTier;
        continue;
      }

      success_msg_writer() << "";
      success_msg_writer() << "  ── Tier " << tier << ": " << tierNames[tier] << " ──";

      for (uint32_t d = 0; d < depositsPerTier; ++d) {
        ++flameCount;
        if (flameCount <= (uint32_t)existingStakes) continue;  // skip already-done deposits

        uint64_t depositAmount = tierAmounts[tier];
        success_msg_writer() << "    Flame " << flameCount << "/" << totalDeposits
                             << " — " << m_currency.formatAmount(depositAmount) << " XFG...";

        std::vector<uint8_t> extra;
        Crypto::PublicKey public_key;
        Crypto::SecretKey secret_key;
        Crypto::generate_keys(public_key, secret_key);
        Crypto::Hash commitment_hash = Crypto::cn_fast_hash(public_key.data, sizeof(public_key.data));

        // Build one-way commitment: H(spendPublicKey || ephemeralPublicKey)
        uint8_t commit_preimage[64];
        std::memcpy(commit_preimage,      walletKeys.address.spendPublicKey.data, 32);
        std::memcpy(commit_preimage + 32, public_key.data,                         32);
        Crypto::Hash elderfier_commitment = Crypto::cn_fast_hash(commit_preimage, sizeof(commit_preimage));

        CryptoNote::TransactionExtraElderfierDeposit elderfierDeposit;
        elderfierDeposit.depositHash        = commitment_hash;
        elderfierDeposit.depositAmount      = depositAmount;
        elderfierDeposit.elderfierCommitment = elderfier_commitment;
        elderfierDeposit.securityWindow     = 28800;
        elderfierDeposit.metadata.clear();
        elderfierDeposit.metadata.push_back(0xEA);
        elderfierDeposit.metadata.insert(elderfierDeposit.metadata.end(), alias.begin(), alias.end());
        elderfierDeposit.metadata.insert(elderfierDeposit.metadata.end(),
          signingPubKey.data, signingPubKey.data + 32);
        // v2: append payout address keys (spend + view = 64 bytes) for coinbase reward delivery
        elderfierDeposit.metadata.insert(elderfierDeposit.metadata.end(),
          walletKeys.address.spendPublicKey.data, walletKeys.address.spendPublicKey.data + 32);
        elderfierDeposit.metadata.insert(elderfierDeposit.metadata.end(),
          walletKeys.address.viewPublicKey.data, walletKeys.address.viewPublicKey.data + 32);
        elderfierDeposit.signature.clear();
        elderfierDeposit.isSlashable        = true;

        if (firstDeposit) {
          CryptoNote::TransactionExtraAliasRegistration aliasReg;
          aliasReg.alias = alias;
          aliasReg.aliasHash = Crypto::cn_fast_hash(alias.data(), alias.size());
          std::string walletAddr = m_wallet->getAddress();
          aliasReg.addressHash = Crypto::cn_fast_hash(walletAddr.data(), walletAddr.size());
          aliasReg.ownerAddress = "";
          aliasReg.aliasType = 0;     // Elderfier
          CryptoNote::addAliasToExtra(extra, aliasReg);
          firstDeposit = false;
        }

        CryptoNote::addElderfierDepositToExtra(extra, elderfierDeposit);
        std::string extraString = std::string(extra.begin(), extra.end());

        CryptoNote::WalletHelper::SendCompleteResultObserver sent;
        WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

        CryptoNote::TransactionId txId = m_wallet->deposit(
          CryptoNote::parameters::DEPOSIT_TERM_ELDERFIER_STAKING,
          depositAmount,
          fee,
          extraString,
          0
        );

        if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == txId) {
          removeGuard.removeObserver();
          fail_msg_writer() << "";
          fail_msg_writer() << "  The ritual faltered at flame " << flameCount << " of " << totalDeposits << ".";
          fail_msg_writer() << "  " << (flameCount - 1) << " stake(s) were forged before it broke.";
          fail_msg_writer() << "  Check your balance and connection, then try again.";
          try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
          catch (...) {}
          return true;
        }

        std::error_code sendError = sent.wait(txId);
        removeGuard.removeObserver();
        if (sendError) {
          fail_msg_writer() << "";
          fail_msg_writer() << "  Flame " << flameCount << " failed: " << sendError.message();
          fail_msg_writer() << "  " << (flameCount - 1) << " stake(s) were forged before this flame broke.";
          try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
          catch (...) {}
          return true;
        }

        success_msg_writer() << "      Sealed.  TX: " << txId;
      }

      // Auto-save after each completed tier
      try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
      catch (...) {}
    }

    // ── Completion ───────────────────────────────────────────────────────
    try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
    catch (...) {}

    success_msg_writer() << "";
    success_msg_writer() << "╔════════════════════════════════════════════════════════════╗";
    success_msg_writer() << "║           CEREMONY COMPLETE — ΞLDERFIER CREATED            ║";
    success_msg_writer() << "╚════════════════════════════════════════════════════════════╝";
    success_msg_writer() << "";
    success_msg_writer() << "  Ælder King " << alias << " — all 20 Elderfire stakes have been";
    success_msg_writer() << "  broadcast to the network. Your name is embedded in each.";
    success_msg_writer() << "";
    success_msg_writer() << "  When all 20 deposits confirm on-chain, the network will";
    success_msg_writer() << "  register you as Ælder King @" << alias;
    success_msg_writer() << "  and add you to the active Ξlderfiers registry.";
    success_msg_writer() << "";
    success_msg_writer() << "  ── YOUR ELDERFIER SIGNING KEY ───────────────────────────";
    success_msg_writer() << "";
    success_msg_writer() << "  Signing key:  " << Common::podToHex(signingSecKey);
    success_msg_writer() << "";
    success_msg_writer() << "  To run your Elderfier signing node:";
    success_msg_writer() << "";
    success_msg_writer() << "    fuegod --elderfier-key " << Common::podToHex(signingSecKey);
    success_msg_writer() << "";
    success_msg_writer() << "  This key is derived from your wallet and can always be";
    success_msg_writer() << "  viewed again via  elder_council";
    success_msg_writer() << "";
    success_msg_writer() << "  Your Elderfire burns bright.";
    success_msg_writer() << "  Guard the Realm well, King " << alias << ".";
    success_msg_writer() << "";
    success_msg_writer() << "  Commands:  list_cold  |  lookup_alias " << alias;
    success_msg_writer() << "";

    return true;
  }
  catch (const std::exception& e)
  {
    try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
    catch (...) {}
    fail_msg_writer() << "Error during Elderfire ceremony: " << e.what();
    return true;
  }
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::elder_council(const std::vector<std::string> &)
{
  // ── Derive signing keypair from wallet spend key (needed for both gate and panel) ─
  CryptoNote::AccountKeys walletKeys;
  m_wallet->getAccountKeys(walletKeys);
  Crypto::PublicKey signingPubKey;
  Crypto::SecretKey signingSecKey;
  {
    static const char label[] = "fuego_ef_sign___";  // 16 bytes
    uint8_t preimage[48];
    std::memcpy(preimage,      label, 16);
    std::memcpy(preimage + 16, walletKeys.spendSecretKey.data, 32);
    Crypto::hash_to_scalar(preimage, sizeof(preimage),
      reinterpret_cast<Crypto::EllipticCurveScalar&>(signingSecKey));
    Crypto::secret_key_to_public_key(signingSecKey, signingPubKey);
  }

  // ── Gate: must be a registered Elderfier — identified by signing pubkey ──
  std::string registeredAlias;
  uint32_t registeredBlock = 0;

  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
    COMMAND_RPC_GET_ELDERFIER_BY_PUBKEY::request efReq;
    COMMAND_RPC_GET_ELDERFIER_BY_PUBKEY::response efRes;
    efReq.signing_pubkey_hex = Common::toHex(signingPubKey.data, sizeof(signingPubKey.data));
    invokeJsonCommand(httpClient, "/get_elderfier_by_pubkey", efReq, efRes);

    if (!efRes.found || efRes.status == "void") {
      // Not yet registered — check if ceremony is pending
      uint32_t efTerm = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_DEPOSIT_TERM_ELDERFIER_STAKING
                                                : CryptoNote::parameters::DEPOSIT_TERM_ELDERFIER_STAKING;
      size_t pendingStakes = 0;
      size_t depositCount = m_wallet->getDepositCount();
      for (CryptoNote::DepositId i = 0; i < depositCount; ++i) {
        CryptoNote::Deposit d;
        if (!m_wallet->getDeposit(i, d)) continue;
        if (d.term == efTerm) ++pendingStakes;
      }
      if (pendingStakes > 0) {
        const uint32_t TOTAL = CryptoNote::parameters::ELDERKING_TOTAL_DEPOSITS;
        success_msg_writer() << "";
        success_msg_writer() << "  Elderfier ceremony pending (" << pendingStakes << "/" << TOTAL << " stakes tracked).";
        success_msg_writer() << "  elder_council command will be available once all " << TOTAL << " confirm and your alias is registered.";
        success_msg_writer() << "  Check: list_cold  |  lookup_alias <your_alias>";
        success_msg_writer() << "";
      } else {
        fail_msg_writer() << "Command not found.";
      }
      return true;
    }
    registeredAlias = efRes.ceremony_alias;
    registeredBlock = 0;  // Not tracked in this endpoint
  } catch (const ConnectException&) {
    printConnectionError();
    return true;
  } catch (const std::exception& e) {
    fail_msg_writer() << "Error: " << e.what();
    return true;
  }

  // ── Header ───────────────────────────────────────────────────────────────
  success_msg_writer() << "";
  Common::Console::setTextColor(Common::Console::Color::BrightYellow);
  std::cout << "╔════════════════════════════════════════════════════════════╗\n";
  std::cout << "║   ";
  Common::Console::setTextColor(Common::Console::Color::BrightWhite);
  std::cout << "ΞLDERFIER PANEL  ·  KING " << std::left << std::setw(32) << registeredAlias;
  Common::Console::setTextColor(Common::Console::Color::BrightYellow);
  std::cout << "║\n";
  std::cout << "╚════════════════════════════════════════════════════════════╝\n";
  Common::Console::setTextColor(Common::Console::Color::Default);
  success_msg_writer() << "";
  success_msg_writer() << "  EFID:           " << registeredAlias;
  success_msg_writer() << "  Registered:     Block " << registeredBlock;
  std::cout << "  Signing key:    ";
  Common::Console::setTextColor(Common::Console::Color::BrightCyan);
  std::cout << Common::podToHex(signingSecKey) << "\n";
  Common::Console::setTextColor(Common::Console::Color::Default);
  std::cout << "  Signing pubkey: ";
  Common::Console::setTextColor(Common::Console::Color::BrightCyan);
  std::cout << Common::podToHex(signingPubKey) << "\n";
  Common::Console::setTextColor(Common::Console::Color::Default);
  success_msg_writer() << "";
  success_msg_writer() << "  Run daemon:     fuegod --elderfier-key " << Common::podToHex(signingSecKey);
  success_msg_writer() << "";

  // ── Stakes ───────────────────────────────────────────────────────────────
  Common::Console::setTextColor(Common::Console::Color::BrightYellow);
  if (m_currency.isTestnet()) {
    std::cout << "  ── ΞLDERFIRE STAKES (444.4 TEST) ────────────────────────────\n";
  } else {
    std::cout << "  ── ΞLDERFIRE STAKES (4444 XFG) ──────────────────────────────\n";
  }
  Common::Console::setTextColor(Common::Console::Color::Default);
  success_msg_writer() << "";

  size_t depositCount = m_wallet->getDepositCount();
  int stakeCount = 0;
  uint64_t totalStaked = 0;

  for (CryptoNote::DepositId id = 0; id < depositCount; ++id) {
    CryptoNote::Deposit deposit;
    if (!m_wallet->getDeposit(id, deposit)) continue;
    uint32_t efStakeTerm = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_DEPOSIT_TERM_ELDERFIER_STAKING
                                                   : CryptoNote::parameters::DEPOSIT_TERM_ELDERFIER_STAKING;
    if (deposit.term != efStakeTerm) continue;

    ++stakeCount;
    totalStaked += deposit.amount;

    std::string statusStr = deposit.locked ? "Locked" :
      (deposit.spendingTransactionId == CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID
        ? "Unlocked" : "Withdrawn");
    std::string heightStr = (deposit.unlockHeight == 0) ? "Pending" : std::to_string(deposit.unlockHeight);

    Common::Console::setTextColor(Common::Console::Color::BrightYellow);
    std::cout << "  Stake #" << stakeCount;
    Common::Console::setTextColor(Common::Console::Color::Default);
    std::cout << ":  "
      << m_currency.formatAmount(deposit.amount) << " XFG"
      << "  |  " << statusStr
      << "  |  Block: " << (deposit.height > 0 ? std::to_string(deposit.height) : "Pending")
      << "  |  " << Common::podToHex(deposit.transactionHash).substr(0, 16) << "...\n";
  }

  if (stakeCount == 0) {
    fail_msg_writer() << "  No ΞLDERFIER stakes found in this wallet.";
  } else {
    success_msg_writer() << "";
    success_msg_writer() << "  Stakes:         " << stakeCount << " / 20";
    success_msg_writer() << "  Total staked:   " << m_currency.formatAmount(totalStaked) << " XFG";
  }
  success_msg_writer() << "";

  // ── Consensus + network (optional, requires daemon) ──────────────────────
  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);

    COMMAND_RPC_GET_ELDERFIER_CONSENSUS_STATUS::request consReq;
    COMMAND_RPC_GET_ELDERFIER_CONSENSUS_STATUS::response consRes;
    invokeJsonCommand(httpClient, "/elderfier_consensus_status", consReq, consRes);

    Common::Console::setTextColor(Common::Console::Color::BrightYellow);
    std::cout << "  ── CURRENT CONSENSUS ROUND ─────────────────────────────\n";
    Common::Console::setTextColor(Common::Console::Color::Default);
    success_msg_writer() << "";
    success_msg_writer() << "  Merkle root:    " << consRes.current_merkle_root.substr(0, 32) << "...";
    success_msg_writer() << "  Block height:   " << consRes.current_block_height;
    success_msg_writer() << "  Signatures:     " << consRes.elderfiers_signed
                         << " / " << consRes.total_registered_elderfiers
                         << "  (" << static_cast<int>(consRes.consensus_percentage) << "%)";
    success_msg_writer() << "  Threshold met:  " << (consRes.meets_69_percent ? "YES" : "NO — awaiting signatures");
    success_msg_writer() << "  Claims ready:   " << (consRes.ready_for_user_claim ? "YES" : "NO")
                         << "  (" << consRes.blocks_until_next_flush << " blocks to next flush)";
    if (!consRes.signed_by.empty()) {
      std::string ids;
      for (auto eid : consRes.signed_by) ids += std::to_string(eid) + " ";
      success_msg_writer() << "  Signed by IDs:  [" << ids << "]";
    }
    success_msg_writer() << "";

    COMMAND_RPC_GET_ELDERFIER_NETWORK_STATS::request netReq;
    COMMAND_RPC_GET_ELDERFIER_NETWORK_STATS::response netRes;
    invokeJsonCommand(httpClient, "/elderfier_network_stats", netReq, netRes);

    Common::Console::setTextColor(Common::Console::Color::BrightYellow);
    std::cout << "  ── NETWORK ─────────────────────────────────────────────\n";
    Common::Console::setTextColor(Common::Console::Color::Default);
    success_msg_writer() << "";
    success_msg_writer() << "  Active Ξlderfiers:  " << netRes.total_registered_elderfiers;
    success_msg_writer() << "  Fees in escrow: " << m_currency.formatAmount(netRes.total_fees_pending_in_escrow) << " XFG";
    success_msg_writer() << "  Fees paid out:  " << m_currency.formatAmount(netRes.total_fees_distributed_all_time) << " XFG";
    success_msg_writer() << "";

  } catch (const ConnectException&) {
    fail_msg_writer() << "  (daemon offline — consensus data unavailable)";
    success_msg_writer() << "";
  } catch (const std::exception& e) {
    fail_msg_writer() << "  (consensus error: " << e.what() << ")";
    success_msg_writer() << "";
  }

  // ── Council actions ──────────────────────────────────────────────────────
  Common::Console::setTextColor(Common::Console::Color::BrightYellow);
  std::cout << "  ── COUNCIL ACTIONS ─────────────────────────────────────\n";
  Common::Console::setTextColor(Common::Console::Color::Default);
  success_msg_writer() << "";
  Common::Console::setTextColor(Common::Console::Color::BrightCyan);
  std::cout << "  get_report [epoch]\n";
  Common::Console::setTextColor(Common::Console::Color::Default);
  success_msg_writer() << "      Fetch the epoch report for a given epoch (default: latest).";
  success_msg_writer() << "      Shows EFier activity, double-sign events, and slash advisories.";
  success_msg_writer() << "";
  Common::Console::setTextColor(Common::Console::Color::BrightCyan);
  std::cout << "  propose_slash <deposit_txhash> <reason>\n";
  Common::Console::setTextColor(Common::Console::Color::Default);
  success_msg_writer() << "      Submit a slash proposal for elder_council review.";
  success_msg_writer() << "      Reasons: double_sign | inactive_<N>_epochs";
  success_msg_writer() << "      Requires explicit confirmation before broadcasting.";
  success_msg_writer() << "";
  Common::Console::setTextColor(Common::Console::Color::BrightCyan);
  std::cout << "  unstake\n";
  Common::Console::setTextColor(Common::Console::Color::Default);
  success_msg_writer() << "      Batch-withdraw all (20) Elderfier staking deposits (1000-block review).";
  success_msg_writer() << "";
  success_msg_writer() << "  ────────────────────────────────────────────────────────";
  success_msg_writer() << "";
  success_msg_writer() << "  Guard the Realm well, King " << registeredAlias << ".";
  success_msg_writer() << "";
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::get_report(const std::vector<std::string> &args)
{
  uint64_t epoch = 0;
  if (!args.empty()) {
    try { epoch = std::stoull(args[0]); }
    catch (...) { fail_msg_writer() << "Usage: get_report [epoch]"; return true; }
  }

  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
    COMMAND_RPC_GET_EPOCH_REPORT::request req;
    COMMAND_RPC_GET_EPOCH_REPORT::response res;
    req.epoch = epoch;
    invokeJsonCommand(httpClient, "/get_epoch_report", req, res);

    if (!res.found) {
      fail_msg_writer() << "No epoch report found" << (epoch > 0 ? " for epoch " + std::to_string(epoch) : "") << ".";
      return true;
    }

    success_msg_writer() << "";
    Common::Console::setTextColor(Common::Console::Color::BrightYellow);
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║   ";
    Common::Console::setTextColor(Common::Console::Color::BrightWhite);
    std::cout << "EPOCH " << std::left << std::setw(6) << res.epoch_number << " REPORT";
    std::cout << std::setw(33) << " ";
    Common::Console::setTextColor(Common::Console::Color::BrightYellow);
    std::cout << "║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    Common::Console::setTextColor(Common::Console::Color::Default);
    success_msg_writer() << "";
    success_msg_writer() << "  Blocks:           " << res.epoch_start_block << " — " << res.epoch_end_block;
    success_msg_writer() << "  Generated at:     Block " << res.generated_at_block;
    success_msg_writer() << "  Active EFiers:    " << res.active_efer_count;
    success_msg_writer() << "  Participating:    " << res.participating_efer_count;
    success_msg_writer() << "  Fees distributed: " << m_currency.formatAmount(res.total_fees_distributed) << " XFG";
    success_msg_writer() << "";

    // Signing / Missing
    if (!res.signing_efier_ids.empty()) {
      std::string ids;
      for (auto id : res.signing_efier_ids) ids += std::to_string(id) + " ";
      success_msg_writer() << "  Signed:           [" << ids << "]";
    }
    if (!res.missing_efier_ids.empty()) {
      std::string ids;
      for (auto id : res.missing_efier_ids) ids += std::to_string(id) + " ";
      Common::Console::setTextColor(Common::Console::Color::BrightRed);
      std::cout << "  Missing:          [" << ids << "]\n";
      Common::Console::setTextColor(Common::Console::Color::Default);
    }

    // EFier activity table
    if (!res.efier_activity.empty()) {
      success_msg_writer() << "";
      Common::Console::setTextColor(Common::Console::Color::BrightYellow);
      std::cout << "  ── EFIER ACTIVITY ──────────────────────────────────────\n";
      Common::Console::setTextColor(Common::Console::Color::Default);
      for (const auto& ef : res.efier_activity) {
        std::string flags;
        if (ef.is_slashed) flags += " [SLASHED]";
        if (ef.is_unstaking) flags += " [UNSTAKING]";
        if (ef.consecutive_missed_epochs > 0)
          flags += " [MISSED:" + std::to_string(ef.consecutive_missed_epochs) + "]";

        Common::Console::setTextColor(ef.signed_this_epoch ? Common::Console::Color::BrightGreen : Common::Console::Color::BrightRed);
        std::cout << "  EFiD " << std::setw(3) << static_cast<int>(ef.elderfier_id);
        Common::Console::setTextColor(Common::Console::Color::BrightCyan);
        std::cout << "  @" << std::left << std::setw(9) << ef.ceremony_alias;
        Common::Console::setTextColor(Common::Console::Color::Default);
        std::cout << "  sigs=" << std::setw(3) << ef.signatures_submitted
                  << "  fees=" << std::setw(12) << m_currency.formatAmount(ef.fees_earned)
                  << (ef.signed_this_epoch ? "  OK" : "  MISS")
                  << flags << "\n";
      }
    }

    // Double-sign events
    if (!res.double_sign_events.empty()) {
      success_msg_writer() << "";
      Common::Console::setTextColor(Common::Console::Color::BrightRed);
      std::cout << "  ── DOUBLE-SIGN EVENTS ──────────────────────────────────\n";
      Common::Console::setTextColor(Common::Console::Color::Default);
      for (const auto& ds : res.double_sign_events) {
        success_msg_writer() << "  EFiD " << static_cast<int>(ds.elderfier_id)
                             << "  root_a=" << ds.root_a.substr(0, 16) << "..."
                             << "  root_b=" << ds.root_b.substr(0, 16) << "..."
                             << "  block=" << ds.block_height
                             << "  detected=" << ds.detected_at_block;
      }
    }

    // Slash advisories
    if (!res.slash_advisory.empty()) {
      success_msg_writer() << "";
      Common::Console::setTextColor(Common::Console::Color::BrightRed);
      std::cout << "  ── SLASH ADVISORIES ────────────────────────────────────\n";
      Common::Console::setTextColor(Common::Console::Color::Default);
      for (const auto& adv : res.slash_advisory) {
        success_msg_writer() << "  " << adv;
      }
    }

    success_msg_writer() << "";
  } catch (const ConnectException&) {
    printConnectionError();
  } catch (const std::exception& e) {
    fail_msg_writer() << "Error: " << e.what();
  }
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::withdraw(const std::vector<std::string> &args)
{
  if (args.size() != 1)
  {
    fail_msg_writer() << "Usage: withdraw <id>";
    return true;
  }

  try
  {
    size_t deposit_count = m_wallet->getDepositCount();
    if (deposit_count == 0)
    {
      fail_msg_writer() << "No deposits have been made in this wallet.";
      return true;
    }

    uint64_t deposit_id = boost::lexical_cast<uint64_t>(args[0]);

    // Check if deposit exists
    if (deposit_id >= deposit_count) {
      fail_msg_writer() << "Invalid deposit ID.";
      return true;
    }

    CryptoNote::Deposit deposit;
    if (!m_wallet->getDeposit(deposit_id, deposit)) {
      fail_msg_writer() << "Failed to retrieve deposit information.";
      return true;
    }

    if (deposit.locked) {
      fail_msg_writer() << "Deposit is still locked. Unlock height: " << deposit.unlockHeight;
      return true;
    }

    std::vector<CryptoNote::DepositId> depositIds = {deposit_id};
    uint64_t fee = m_currency.minimumFee();

    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

    CryptoNote::TransactionId txId = m_wallet->withdrawDeposits(depositIds, fee);
    if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == txId) {
      removeGuard.removeObserver();
      fail_msg_writer() << "Failed to create withdrawal transaction.";
      return true;
    }

    std::error_code sendError = sent.wait(txId);
    removeGuard.removeObserver();
    if (sendError) {
      fail_msg_writer() << "Withdrawal failed: " << sendError.message();
      return true;
    }

    success_msg_writer(true) << "Deposit withdrawal transaction sent!";
    success_msg_writer() << "Transaction ID: " << txId;
    success_msg_writer() << "Withdrawn amount: " << m_currency.formatAmount(deposit.amount);
    //if (deposit.amount != CryptoNote::parameters::AMOUNT_TIER_0) {
     // success_msg_writer() << "Interest earned: " << m_currency.formatAmount(deposit.interest);
     // }
  }
  catch (std::exception &e)
  {
    fail_msg_writer() << "Failed to withdraw deposit: " << e.what();
  }

  return true;
 }

 //----------------------------------------------------------------------------------------------------
bool simple_wallet::unstake(const std::vector<std::string> &args)
{
  try
  {
    size_t deposit_count = m_wallet->getDepositCount();
    if (deposit_count == 0) {
      fail_msg_writer() << "No deposits found in this wallet.";
      return true;
    }

    // Collect all EFier staking deposits that are unlocked and unspent
    std::vector<CryptoNote::DepositId> efierIds;
    uint64_t totalAmount = 0;

    for (size_t i = 0; i < deposit_count; ++i) {
      CryptoNote::Deposit deposit;
      if (!m_wallet->getDeposit(i, deposit)) continue;

      // Match EFier deposits by term (DEPOSIT_TERM_ELDERFIER_STAKING)
      bool isEfier = (deposit.term == CryptoNote::parameters::DEPOSIT_TERM_ELDERFIER_STAKING)
                  || (deposit.depositType == CryptoNote::Deposit::Type::ELDERFIER);

      if (!isEfier) continue;

      // Skip already spent
      if (deposit.spendingTransactionId != CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID) continue;

      if (deposit.locked) {
        success_msg_writer() << "  Deposit #" << i << " (" << m_currency.formatAmount(deposit.amount)
                             << " XFG) still locked until block " << deposit.unlockHeight;
        continue;
      }

      efierIds.push_back(i);
      totalAmount += deposit.amount;
    }

    if (efierIds.empty()) {
      fail_msg_writer() << "No withdrawable Elderfier staking deposits found.";
      fail_msg_writer() << "  (Deposits may still be locked or already withdrawn.)";
      return true;
    }

    success_msg_writer() << "";
    success_msg_writer() << "  ── ΞLDERFIER UNSTAKING ──────────────────────────────────";
    success_msg_writer() << "";
    success_msg_writer() << "  Found " << efierIds.size() << " Elderfier staking deposits";
    success_msg_writer() << "  Total stake: " << m_currency.formatAmount(totalAmount) << " XFG";
    success_msg_writer() << "";
    success_msg_writer() << "  WARNING: Unstaking will remove you from the Elderfier";
    success_msg_writer() << "  registry- permanently. You will no longer sign merkle roots or";
    success_msg_writer() << "  earn fees. This address cannot be used as an Elderfier and";
    success_msg_writer() << "  your current Elder King name shall be voided forevermore.";
    success_msg_writer() << "  This action can not be undone.";
    success_msg_writer() << "";
    success_msg_writer() << "  Type 'confirm' to proceed, or Enter to cancel: ";

    std::string confirmation;
    m_consoleHandler.readLine(confirmation);
    while (!confirmation.empty() && std::isspace((unsigned char)confirmation.front())) confirmation.erase(confirmation.begin());
    while (!confirmation.empty() && std::isspace((unsigned char)confirmation.back()))  confirmation.pop_back();

    if (confirmation != "confirm") {
      success_msg_writer() << "  Unstaking cancelled. Your stake remains.";
      return true;
    }

    uint64_t fee = m_currency.minimumFee();

    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

    CryptoNote::TransactionId txId = m_wallet->withdrawDeposits(efierIds, fee);
    if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == txId) {
      removeGuard.removeObserver();
      fail_msg_writer() << "  Failed to create batch withdrawal transaction.";
      return true;
    }

    std::error_code sendError = sent.wait(txId);
    removeGuard.removeObserver();
    if (sendError) {
      fail_msg_writer() << "  Unstaking failed: " << sendError.message();
      return true;
    }

    success_msg_writer() << "";
    success_msg_writer() << "  Unstaking transaction sent!";
    success_msg_writer() << "  Transaction ID: " << txId;
    success_msg_writer() << "  Deposits withdrawn: " << efierIds.size();
    success_msg_writer() << "  Total returned: " << m_currency.formatAmount(totalAmount) << " XFG";
    success_msg_writer() << "";
  }
  catch (std::exception &e)
  {
    fail_msg_writer() << "Failed to unstake: " << e.what();
  }

  return true;
}

 //----------------------------------------------------------------------------------------------------
// USER-FACING: Users MUST generate STARK proofs from deposits for L2 claims
bool simple_wallet::gen_proof(const std::vector<std::string> &args) {
   if (args.size() != 1) {
     fail_msg_writer() << "Usage: gen_proof <tx_hash>";
     return true;
   }

   const std::string& tx_hash = args[0];

   try {
     // Parse transaction hash
     Crypto::Hash hash;
     if (!parse_hash256(tx_hash, hash)) {
       fail_msg_writer() << "Failed to parse transaction hash";
       return true;
     }

     // Fetch transaction from daemon via /gettransactions RPC
     CryptoNote::COMMAND_RPC_GET_TRANSACTIONS::request req;
     CryptoNote::COMMAND_RPC_GET_TRANSACTIONS::response res;
     req.txs_hashes.push_back(tx_hash);

     try {
       HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
       invokeJsonCommand(httpClient, "/gettransactions", req, res);
     } catch (const ConnectException&) {
       fail_msg_writer() << "Failed to connect to daemon. Is the node running?";
       return true;
     } catch (const std::exception& e) {
       fail_msg_writer() << "Error querying daemon: " << e.what();
       return true;
     }

     if (!res.missed_tx.empty() || res.txs_as_hex.empty()) {
       fail_msg_writer() << "Transaction not found on blockchain: " << tx_hash;
       return true;
     }

     // Decode raw transaction from hex blob
     BinaryArray txBlob;
     if (!Common::fromHex(res.txs_as_hex[0], txBlob)) {
       fail_msg_writer() << "Failed to decode transaction data";
       return true;
     }

     CryptoNote::Transaction tx;
     if (!fromBinaryArray(tx, txBlob)) {
       fail_msg_writer() << "Failed to parse transaction";
       return true;
     }

     // Check for HEAT commitment (0x08 tag in tx_extra)
     std::vector<CryptoNote::TransactionExtraField> extraFields;
     if (CryptoNote::parseTransactionExtra(tx.extra, extraFields)) {
       for (const auto& field : extraFields) {
         if (field.type() == typeid(CryptoNote::TransactionExtraHeatCommitment)) {
           const auto& heatCommitment = boost::get<CryptoNote::TransactionExtraHeatCommitment>(field);

           success_msg_writer() << "Found XFG burn transaction: " << tx_hash;
           success_msg_writer() << "Amount: " << m_currency.formatAmount(heatCommitment.amount);

           std::cout << "\n=== PROOF DATA NEEDED FOR STARK GENERATION ===" << std::endl;
           std::cout << "Transaction Hash: " << tx_hash << std::endl;
           std::cout << "Commitment: " << Common::podToHex(heatCommitment.commitment) << std::endl;
           std::cout << "Amount: " << heatCommitment.amount << " heat / atomic XFG" << std::endl;
           std::cout << "=====================================" << std::endl;

           logger(INFO, BRIGHT_GREEN) << "Proof data generated for XFG burn (HEAT) transaction " << tx_hash;
           return true;
         }
         // Check for COLD deposit commitment (0xCD = 205 tag in tx_extra)
         else if (field.type() == typeid(CryptoNote::TransactionExtraColdCommitment)) {
           const auto& coldDeposit = boost::get<CryptoNote::TransactionExtraColdCommitment>(field);

           success_msg_writer() << "Found COLD transaction: " << tx_hash;
           success_msg_writer() << "Amount: " << m_currency.formatAmount(coldDeposit.amount);
           success_msg_writer() << "Term: " << coldDeposit.term << " blocks";

           std::cout << "\n=== XFG CERTIFICATE OF LEDGER DEPOSIT PROOF ===" << std::endl;
           std::cout << "Transaction Hash: " << tx_hash << std::endl;
           std::cout << "Commitment: " << Common::podToHex(coldDeposit.commitment) << std::endl;
           std::cout << "Amount: " << coldDeposit.amount << " heat (atomic XFG)" << std::endl;
           std::cout << "Term: " << coldDeposit.term << " blocks" << std::endl;
           std::cout << "Chain Code: " << static_cast<int>(coldDeposit.claimChainCode) << std::endl;
           std::cout << "=================================" << std::endl;

           logger(INFO, BRIGHT_GREEN) << "Proof data generated for COLD transaction " << tx_hash;
           return true;
         }
       }
     }

     fail_msg_writer() << "No HEAT (burn) or COLD commitment found in transaction: " << tx_hash;
     return true;
   } catch (const std::exception& e) {
     fail_msg_writer() << "Error processing transaction: " << e.what();
   }

   return true;
 }



//----------------------------------------------------------------------------------------------------
bool simple_wallet::cold_info(const std::vector<std::string> &args)
{
  if (args.size() != 1)
  {
    fail_msg_writer() << "Usage: cold_info <id>";
    return true;
  }

  try {
    uint64_t deposit_id = boost::lexical_cast<uint64_t>(args[0]);

    if (deposit_id >= m_wallet->getDepositCount()) {
      fail_msg_writer() << "Invalid deposit ID.";
      return true;
    }

    CryptoNote::Deposit deposit;
    if (!m_wallet->getDeposit(deposit_id, deposit)) {
      fail_msg_writer() << "Failed to retrieve deposit information.";
      return true;
    }

    success_msg_writer() << "Deposit Information:";
    success_msg_writer() << "ID:            " << deposit_id;
    success_msg_writer() << "Amount:        " << m_currency.formatAmount(deposit.amount);
    // NOTE: No interest display on Fuego - interest handled off-chain via L2

    // Display deposit type (from transaction extra field)
    std::string depositType = "Unknown";
    std::string typeDescription = "";

    switch (deposit.depositType) {
      case CryptoNote::Deposit::Type::HEAT:
        depositType = "XFG Burn (HEAT/0x08)";
        typeDescription = "Permanent 'forever' deposit - removed from circulation";
        break;
      case CryptoNote::Deposit::Type::COLD:
        depositType = "COLD Yield (0xCD)";
        typeDescription = "Off-chain (CD) interest deposit - locked for your specified term";
        break;
      case CryptoNote::Deposit::Type::ELDERFIER:
        depositType = "ΞLDERFIER Stake (0xEF)";
        typeDescription = "Staking deposit for Ξlderfier - unstakeable with 8-day review window";
        break;
      default:
        depositType = "Unknown";
        typeDescription = "Unknown deposit type";
    }

    // Display term (user-defined unlock time, independent of deposit type)
    if (deposit.term == CryptoNote::parameters::DEPOSIT_TERM_FOREVER) {
      success_msg_writer() << "Term:          FOREVER";
    } else if (deposit.term == CryptoNote::parameters::DEPOSIT_TERM_ELDERFIER_STAKING) {
      success_msg_writer() << "Term:          Ξlderfier Staking (unstakeable on demand, " << CryptoNote::parameters::ELDERFIER_STAKING_REVIEW_WINDOW << "-block review)";
    } else if (deposit.term == CryptoNote::parameters::COLD_MIN_TERM) {
      success_msg_writer() << "Term:          3 months (16,000 blocks)";
    } else if (deposit.term == CryptoNote::parameters::COLD_MAX_TERM) {
      success_msg_writer() << "Term:          1 year (65,000 blocks)";
    } else {
      success_msg_writer() << "Term:          " << deposit.term << " blocks";
    }

    success_msg_writer() << "Height:        " << deposit.height;

    // Show unlock height for non-HEAT deposits; 0 means pending (not yet confirmed)
    if (deposit.depositType != CryptoNote::Deposit::Type::HEAT) {
      if (deposit.unlockHeight == 0) {
        success_msg_writer() << "Unlock Height: Pending (not yet confirmed)";
      } else {
        success_msg_writer() << "Unlock Height: " << deposit.unlockHeight;
      }
    }

    // Show status
    if (deposit.locked) {
      success_msg_writer() << "Status:        Locked";
    } else if (deposit.spendingTransactionId == CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      success_msg_writer() << "Status:        Unlocked";
    } else {
      success_msg_writer() << "Status:        Withdrawn";
    }

    success_msg_writer() << "Transaction:   " << Common::podToHex(deposit.transactionHash);

    // Show deposit type information
    success_msg_writer() << "Type:          " << depositType;
    success_msg_writer() << "Description:   " << typeDescription;

    // Parse and display commitment from transaction extra
    if (!deposit.extra.empty()) {
      std::vector<TransactionExtraField> extraFields;
      std::vector<uint8_t> extraBytes(deposit.extra.begin(), deposit.extra.end());

      if (parseTransactionExtra(extraBytes, extraFields)) {
        for (const auto& field : extraFields) {
          if (field.type() == typeid(TransactionExtraHeatCommitment)) {
            const auto& heatCommit = boost::get<TransactionExtraHeatCommitment>(field);
            success_msg_writer() << "Commitment:    " << Common::podToHex(heatCommit.commitment);
          } else if (field.type() == typeid(TransactionExtraColdCommitment)) {
            const auto& coldCommit = boost::get<TransactionExtraColdCommitment>(field);
            success_msg_writer() << "Commitment:    " << Common::podToHex(coldCommit.commitment);
          } else if (field.type() == typeid(TransactionExtraElderfierDeposit)) {
            const auto& elfDeposit = boost::get<TransactionExtraElderfierDeposit>(field);
            success_msg_writer() << "Commitment:    " << Common::podToHex(elfDeposit.depositHash);
          }
        }
      }

      // Also show raw extra hex for debugging
      success_msg_writer() << "Extra (hex):   " << Common::toHex(extraBytes);

      // Decrypt and display STARK secret if present (0xD5 tag)
      CryptoNote::TransactionExtraDepositSecret encSecret;
      if (CryptoNote::getDepositSecretFromExtra(extraBytes, encSecret)) {
        CryptoNote::AccountKeys walletKeys;
        m_wallet->getAccountKeys(walletKeys);
        CryptoNote::DepositSecretPayload decrypted;
        if (CryptoNote::decryptDepositSecret(encSecret, walletKeys.viewSecretKey, decrypted)) {
          Crypto::SecretKey secret;
          memcpy(&secret, decrypted.depositSecret, 32);
          success_msg_writer() << "";
          success_msg_writer() << "STARK Secret:  " << Common::podToHex(secret);
        }
      }
    }

  } catch (const std::exception &e) {
    fail_msg_writer() << "Error: " << e.what();
    return false;
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::list_burns(const std::vector<std::string> &)
{
  size_t deposit_count = m_wallet->getDepositCount();

  if (deposit_count == 0) {
    success_msg_writer() << "No deposits found";
    return true;
  }

  success_msg_writer() << "";
  success_msg_writer() << "=== XFG (HEAT) Burn Transactions ===";
  success_msg_writer() << "";
  success_msg_writer() << "ID    | Amount             | Height        | TX Hash                          | Status";
  success_msg_writer() << "------|--------------------|---------------|----------------------------------|--------";

  size_t burnCount = 0;
  for (CryptoNote::DepositId id = 0; id < deposit_count; ++id) {
    CryptoNote::Deposit deposit;
    if (!m_wallet->getDeposit(id, deposit)) {
      continue;
    }

    // Only show HEAT/burn deposits (FOREVER term)
    if (deposit.term != CryptoNote::parameters::DEPOSIT_TERM_FOREVER) {
      continue;
    }

    burnCount++;

    std::string amount_str = m_currency.formatAmount(deposit.amount);
    std::string height_str = std::to_string(deposit.height);
    std::string tx_str = Common::podToHex(deposit.transactionHash).substr(0, 32) + "...";

    std::string status_str;
    if (deposit.locked) {
      status_str = "Burned";
    } else {
      status_str = "Burned";
    }

    success_msg_writer() << std::left
      << std::setw(5)  << std::to_string(id) << " | "
      << std::setw(18) << amount_str << " | "
      << std::setw(13) << height_str << " | "
      << std::setw(32) << tx_str << " | "
      << std::setw(8)  << status_str;
  }

  if (burnCount == 0) {
    success_msg_writer() << "  No burn transactions found.";
  } else {
    success_msg_writer() << "";
    success_msg_writer() << "Total burns: " << burnCount;
    success_msg_writer() << "Use 'burn_info <id>' for detailed information about a specific burn.";
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::burn_info(const std::vector<std::string> &args)
{
  if (args.size() != 1) {
    fail_msg_writer() << "Usage: burn_info <id>";
    return true;
  }

  try {
    uint64_t deposit_id = boost::lexical_cast<uint64_t>(args[0]);

    if (deposit_id >= m_wallet->getDepositCount()) {
      fail_msg_writer() << "Invalid deposit ID.";
      return true;
    }

    CryptoNote::Deposit deposit;
    if (!m_wallet->getDeposit(deposit_id, deposit)) {
      fail_msg_writer() << "Failed to retrieve deposit information.";
      return true;
    }

    // Verify this is actually a burn (FOREVER term)
    if (deposit.term != CryptoNote::parameters::DEPOSIT_TERM_FOREVER) {
      fail_msg_writer() << "Deposit " << deposit_id << " is not a burn transaction (use 'cold_info' instead).";
      return true;
    }

    success_msg_writer() << "";
    success_msg_writer() << "=== XFG Burn Info ===";
    success_msg_writer() << "ID:            " << deposit_id;
    success_msg_writer() << "Amount:        " << m_currency.formatAmount(deposit.amount);
    success_msg_writer() << "Type:          XFG Burn (HEAT/0x08)";
    success_msg_writer() << "Term:          FOREVER (permanently removed from circulation)";
    success_msg_writer() << "Height:        " << deposit.height;
    success_msg_writer() << "Status:        Burned";
    success_msg_writer() << "Transaction:   " << Common::podToHex(deposit.transactionHash);

    // Parse and display HEAT commitment from transaction extra
    if (!deposit.extra.empty()) {
      std::vector<TransactionExtraField> extraFields;
      std::vector<uint8_t> extraBytes(deposit.extra.begin(), deposit.extra.end());

      if (parseTransactionExtra(extraBytes, extraFields)) {
        for (const auto& field : extraFields) {
          if (field.type() == typeid(TransactionExtraHeatCommitment)) {
            const auto& heatCommit = boost::get<TransactionExtraHeatCommitment>(field);
            success_msg_writer() << "Commitment:    " << Common::podToHex(heatCommit.commitment);
            success_msg_writer() << "Burn Amount:   " << heatCommit.amount << " heat (atomic)";
            if (!heatCommit.metadata.empty()) {
              success_msg_writer() << "Metadata:      " << Common::toHex(heatCommit.metadata);
            }
          }
        }
      }

      success_msg_writer() << "Extra (hex):   " << Common::toHex(extraBytes);

      // Decrypt and display STARK secret if present (0xD5 tag)
      CryptoNote::TransactionExtraDepositSecret encSecret;
      if (CryptoNote::getDepositSecretFromExtra(extraBytes, encSecret)) {
        CryptoNote::AccountKeys walletKeys;
        m_wallet->getAccountKeys(walletKeys);
        CryptoNote::DepositSecretPayload decrypted;
        if (CryptoNote::decryptDepositSecret(encSecret, walletKeys.viewSecretKey, decrypted)) {
          Crypto::SecretKey secret;
          memcpy(&secret, decrypted.depositSecret, 32);
          success_msg_writer() << "";
          success_msg_writer() << "STARK Secret:  " << Common::podToHex(secret);
        }
      }
    }

    success_msg_writer() << "";
    success_msg_writer() << "Use 'gen_proof " << Common::podToHex(deposit.transactionHash) << "' to generate STARK proof data.";

  } catch (const std::exception &e) {
    fail_msg_writer() << "Error: " << e.what();
    return false;
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::migrate_cold(const std::vector<std::string> &args) {
  if (args.size() != 1) {
    fail_msg_writer() << "Usage: migrate_cold <deposit_id>";
    fail_msg_writer() << "Migrates a pre-v3 legacy deposit to v3 format, registering a commitment for L2 claims.";
    return true;
  }

  try {
    size_t depositId = std::stoull(args[0]);
    size_t depositCount = m_wallet->getDepositCount();

    if (depositId >= depositCount) {
      fail_msg_writer() << "Invalid deposit ID: " << depositId << ". You have " << depositCount << " deposits.";
      return true;
    }

    CryptoNote::Deposit deposit;
    if (!m_wallet->getDeposit(depositId, deposit)) {
      fail_msg_writer() << "Failed to retrieve deposit " << depositId;
      return true;
    }

    // Must be a COLD deposit (not HEAT burn or EFier)
    if (deposit.depositType == CryptoNote::Deposit::Type::HEAT) {
      fail_msg_writer() << "Deposit " << depositId << " is a HEAT burn. Use burn_info to view it.";
      fail_msg_writer() << "HEAT burns already have v3 commitments if created after the v3 upgrade.";
      return true;
    }
    if (deposit.depositType == CryptoNote::Deposit::Type::ELDERFIER) {
      fail_msg_writer() << "Deposit " << depositId << " is an Elderfier stake. Migration not applicable.";
      return true;
    }

    // Check if already has a 0xD5 secret (already v3)
    std::vector<uint8_t> extraBytes(deposit.extra.begin(), deposit.extra.end());
    CryptoNote::TransactionExtraDepositSecret existingSecret;
    if (CryptoNote::getDepositSecretFromExtra(extraBytes, existingSecret)) {
      fail_msg_writer() << "Deposit " << depositId << " already has a STARK secret (v3 format).";
      fail_msg_writer() << "Use 'cold_info " << depositId << "' to view it.";
      return true;
    }

    // Check if already has a 0xCE migration tag
    std::vector<CryptoNote::TransactionExtraField> extraFields;
    if (CryptoNote::parseTransactionExtra(extraBytes, extraFields)) {
      for (const auto& field : extraFields) {
        if (field.type() == typeid(CryptoNote::TransactionExtraColdMigration)) {
          fail_msg_writer() << "Deposit " << depositId << " already has a migration tag.";
          return true;
        }
      }
    }

    // Display deposit info and confirm
    success_msg_writer() << "";
    success_msg_writer() << "=== Legacy COLD Deposit Migration ===";
    success_msg_writer() << "";
    success_msg_writer() << "Deposit ID:    " << depositId;
    success_msg_writer() << "Amount:        " << m_currency.formatAmount(deposit.amount) << " XFG";
    if (deposit.term == CryptoNote::parameters::DEPOSIT_TERM_FOREVER) {
      success_msg_writer() << "Term:          FOREVER";
    } else {
      success_msg_writer() << "Term:          " << deposit.term << " blocks";
    }
    success_msg_writer() << "TX Hash:       " << Common::podToHex(deposit.transactionHash);
    success_msg_writer() << "";
    success_msg_writer() << "This will create a migration transaction that registers a v3";
    success_msg_writer() << "commitment for this legacy deposit, enabling L2 claims via xfg-stark-cli.";
    success_msg_writer() << "Cost: network fee only (" << m_currency.formatAmount(m_currency.minimumFee()) << " XFG)";
    success_msg_writer() << "";
    success_msg_writer() << "Confirm? (1) OK  (2) No ";

    std::string confirm;
    m_consoleHandler.readLine(confirm);

    if (confirm != "1" && confirm != "OK" && confirm != "Ok" && confirm != "ok") {
      success_msg_writer() << "Cancelled.";
      return true;
    }

    // Generate v3 STARK commitment for this legacy deposit
    uint32_t networkId = m_currency.isTestnet()
        ? CryptoNote::parameters::STARK_NETWORK_ID_TESTNET
        : CryptoNote::parameters::STARK_NETWORK_ID_MAINNET;
    auto starkResult = CryptoNote::StarkCommitmentGenerator::generate(
        deposit.amount,
        deposit.term,
        networkId,
        CryptoNote::parameters::STARK_TARGET_CHAIN_ETH,
        CryptoNote::parameters::STARK_COMMITMENT_VERSION);

    // Display secret
    success_msg_writer() << "";
    success_msg_writer() << "STARK Commitment Data (SAVE THIS — needed to claim CD interest):";
    success_msg_writer() << "  Secret:     " << Common::podToHex(starkResult.secret);
    success_msg_writer() << "  Commitment: " << Common::podToHex(starkResult.commitment);
    success_msg_writer() << "  Nullifier:  " << Common::podToHex(starkResult.nullifier);
    success_msg_writer() << "";

    // Build migration extra data
    std::vector<uint8_t> migrationExtra;

    // 0xCE migration tag — references original deposit
    CryptoNote::TransactionExtraColdMigration migration;
    migration.originalTxHash = deposit.transactionHash;
    migration.commitment = starkResult.commitment;
    migration.amount = deposit.amount;
    migration.term = deposit.term;
    migration.claimChainCode = 1; // ETH
    CryptoNote::addColdMigrationToExtra(migrationExtra, migration);

    // 0xD5 encrypted secret — so cold_info can decrypt it later
    CryptoNote::AccountKeys walletKeys;
    m_wallet->getAccountKeys(walletKeys);
    CryptoNote::DepositSecretPayload secretPayload;
    secretPayload.depositType = 0xCD; // COLD
    secretPayload.amount = deposit.amount;
    secretPayload.term = deposit.term;
    memcpy(secretPayload.depositSecret, &starkResult.secret, 32);
    CryptoNote::TransactionExtraDepositSecret encSecret;
    if (CryptoNote::encryptDepositSecret(secretPayload, walletKeys.address.viewPublicKey, encSecret)) {
      CryptoNote::addDepositSecretToExtra(migrationExtra, encSecret);
    }

    std::string extraString(migrationExtra.begin(), migrationExtra.end());

    // Send as self-transfer (just carries the extra data, costs only network fee)
    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

    std::vector<CryptoNote::WalletLegacyTransfer> transfers;
    CryptoNote::WalletLegacyTransfer selfTransfer;
    selfTransfer.address = m_wallet->getAddress();
    selfTransfer.amount = m_currency.minimumFee(); // minimum self-transfer
    transfers.push_back(selfTransfer);

    std::vector<CryptoNote::TransactionMessage> messages;
    uint64_t fee = m_currency.minimumFee();
    uint64_t mixIn = 0;
    uint64_t unlockTimestamp = 0;
    uint64_t ttl = 0;
    Crypto::SecretKey transactionSK;

    CryptoNote::TransactionId tx = m_wallet->sendTransaction(transactionSK, transfers, fee, extraString, mixIn, unlockTimestamp, messages, ttl);

    if (tx == CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      fail_msg_writer() << "Failed to create migration transaction.";
      return true;
    }

    std::error_code sendError = sent.wait(tx);
    removeGuard.removeObserver();

    if (sendError) {
      fail_msg_writer() << "Migration transaction failed: " << sendError.message();
      return true;
    }

    CryptoNote::WalletLegacyTransaction txInfo;
    m_wallet->getTransaction(tx, txInfo);

    success_msg_writer(true) << "";
    success_msg_writer(true) << "Legacy deposit migrated successfully!";
    success_msg_writer(true) << "  Migration TX: " << Common::podToHex(txInfo.hash);
    success_msg_writer(true) << "  Original TX:  " << Common::podToHex(deposit.transactionHash);
    success_msg_writer(true) << "";
    success_msg_writer(true) << "Once confirmed, use 'gen_proof " << Common::podToHex(deposit.transactionHash) << "' to generate STARK proof data.";

    try {
      CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file);
    } catch (const std::exception& e) {
      fail_msg_writer() << e.what();
      return true;
    }

  } catch (const std::exception &e) {
    fail_msg_writer() << "Migration error: " << e.what();
  }

  return true;
}

bool simple_wallet::propose_slash(const std::vector<std::string> &args) {
  if (args.size() < 2) {
    fail_msg_writer() << "Usage: propose_slash <deposit_txhash> <reason>";
    fail_msg_writer() << "  <deposit_txhash>  64-char hex hash of the 0xEF deposit tx to slash";
    fail_msg_writer() << "  <reason>          double_sign | inactive_<N>_epochs";
    fail_msg_writer() << "";
    fail_msg_writer() << "This submits a 0xEC QUORUM slash proposal on-chain for elder_council review.";
    fail_msg_writer() << "Requires your Elderfier signing key (secret, 64 hex chars).";
    fail_msg_writer() << "Slash proposals are advisory — they do not auto-execute.";
    return true;
  }

  try {
    // Parse deposit txhash
    std::string depositHashHex = args[0];
    if (depositHashHex.size() != 64) {
      fail_msg_writer() << "Invalid deposit txhash: must be 64 hex characters.";
      return true;
    }
    Crypto::Hash depositTxHash;
    if (!Common::podFromHex(depositHashHex, depositTxHash)) {
      fail_msg_writer() << "Invalid deposit txhash: bad hex encoding.";
      return true;
    }

    std::string reason = args[1];
    if (reason != "double_sign" && reason.find("inactive") == std::string::npos) {
      fail_msg_writer() << "Invalid reason. Must be 'double_sign' or 'inactive_<N>_epochs'.";
      return true;
    }

    // Derive EFier signing keypair from wallet spend key — same derivation as elder_council.
    // No manual key entry: the signing key is deterministic from the wallet.
    // This command is only reachable from the elder_council panel, which already verified
    // that this wallet is a registered Elderfier.
    CryptoNote::AccountKeys walletKeys;
    m_wallet->getAccountKeys(walletKeys);
    Crypto::PublicKey sigPubKey;
    Crypto::SecretKey sigSecKey;
    {
      static const char label[] = "fuego_ef_sign___";  // 16 bytes
      uint8_t preimage[48];
      std::memcpy(preimage,      label, 16);
      std::memcpy(preimage + 16, walletKeys.spendSecretKey.data, 32);
      Crypto::hash_to_scalar(preimage, sizeof(preimage),
        reinterpret_cast<Crypto::EllipticCurveScalar&>(sigSecKey));
      Crypto::secret_key_to_public_key(sigSecKey, sigPubKey);
    }

    success_msg_writer() << "";
    success_msg_writer() << "=== Ξlderfier Slash Proposal ===";
    success_msg_writer() << "";
    success_msg_writer() << "Deposit TX:  " << depositHashHex;
    success_msg_writer() << "Reason:      " << reason;
    success_msg_writer() << "Signing as:  " << Common::podToHex(sigPubKey).substr(0, 16) << "...";
    success_msg_writer() << "";

    // Build messageData: [slashing_percentage=100 (1 byte)] [slashed_amount=0 (8 bytes)]
    // Amount of 0 lets the node compute slashed amount from the deposit itself
    uint8_t slashPct = 100;
    uint64_t slashAmt = 0;  // node derives exact amount from deposit record
    std::vector<uint8_t> messageData(9, 0);
    messageData[0] = slashPct;
    memcpy(&messageData[1], &slashAmt, 8);

    // Append reason string (null-terminated) so node logs can show it
    messageData.insert(messageData.end(), reason.begin(), reason.end());
    messageData.push_back(0x00);

    // Compute signature: sign H(targetDepositHash || messageData) with EFier secret key
    std::vector<uint8_t> sigPreimage;
    sigPreimage.insert(sigPreimage.end(), depositTxHash.data,
                       depositTxHash.data + sizeof(depositTxHash));
    sigPreimage.insert(sigPreimage.end(), messageData.begin(), messageData.end());
    Crypto::Hash sigHash;
    Crypto::cn_fast_hash(sigPreimage.data(), sigPreimage.size(), sigHash);

    Crypto::Signature msgSig;
    Crypto::generate_signature(sigHash, sigPubKey, sigSecKey, msgSig);

    // Build 0xEC QUORUM message
    CryptoNote::TransactionExtraElderfierMessage slashMsg;
    slashMsg.senderKey = sigPubKey;
    slashMsg.recipientKey = Crypto::PublicKey{};  // broadcast to all
    slashMsg.messageType = 0x01;  // slashing message type
    slashMsg.timestamp = static_cast<uint64_t>(std::time(nullptr));
    slashMsg.messageData = messageData;
    slashMsg.signature.assign(reinterpret_cast<uint8_t*>(&msgSig),
                              reinterpret_cast<uint8_t*>(&msgSig) + sizeof(msgSig));
    slashMsg.consensusRequired = true;
    slashMsg.consensusType = CryptoNote::ElderfierConsensusType::QUORUM;
    slashMsg.requiredThreshold = 80;
    slashMsg.targetDepositHash = depositTxHash;

    // Final explicit confirmation — user must type the deposit hash to confirm
    success_msg_writer() << "";
    success_msg_writer() << "Signing pubkey: " << Common::podToHex(sigPubKey);
    success_msg_writer() << "";
    success_msg_writer() << "WARNING: This will permanently lock the target deposit.";
    success_msg_writer() << "To confirm, type the first 8 characters of the deposit txhash:";
    success_msg_writer() << "  Expected: " << depositHashHex.substr(0, 8);

    std::string confirmHex;
    m_consoleHandler.readLine(confirmHex);
    confirmHex.erase(std::remove_if(confirmHex.begin(), confirmHex.end(), ::isspace), confirmHex.end());

    if (confirmHex != depositHashHex.substr(0, 8)) {
      success_msg_writer() << "Confirmation mismatch — slash proposal cancelled.";
      return true;
    }

    // Serialize the 0xEC message into tx_extra
    std::vector<uint8_t> extra;
    CryptoNote::addElderfierMessageToExtra(extra, slashMsg);
    std::string extraStr(extra.begin(), extra.end());

    // Self-transfer carrying the slash proposal (fee-only tx)
    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

    std::vector<CryptoNote::WalletLegacyTransfer> transfers;
    CryptoNote::WalletLegacyTransfer selfTransfer;
    selfTransfer.address = m_wallet->getAddress();
    selfTransfer.amount = m_currency.minimumFee();
    transfers.push_back(selfTransfer);

    std::vector<CryptoNote::TransactionMessage> messages;
    uint64_t fee = m_currency.minimumFee();
    Crypto::SecretKey txSK;

    CryptoNote::TransactionId tx = m_wallet->sendTransaction(
        txSK, transfers, fee, extraStr, 0, 0, messages, 0);

    if (tx == CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      fail_msg_writer() << "Failed to create slash proposal transaction.";
      return true;
    }

    std::error_code sendError = sent.wait(tx);
    removeGuard.removeObserver();

    if (sendError) {
      fail_msg_writer() << "Slash proposal transaction failed: " << sendError.message();
      return true;
    }

    CryptoNote::WalletLegacyTransaction txInfo;
    m_wallet->getTransaction(tx, txInfo);

    success_msg_writer(true) << "";
    success_msg_writer(true) << "Slash proposal submitted.";
    success_msg_writer(true) << "  Proposal TX:  " << Common::podToHex(txInfo.hash);
    success_msg_writer(true) << "  Target:       " << depositHashHex;
    success_msg_writer(true) << "  Reason:       " << reason;
    success_msg_writer(true) << "";
    success_msg_writer(true) << "The node will verify your EFier registration and execute the slash";
    success_msg_writer(true) << "if the deposit is not already slashed and your signature is valid.";

    try {
      CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file);
    } catch (const std::exception& e) {
      fail_msg_writer() << e.what();
    }

  } catch (const std::exception& e) {
    fail_msg_writer() << "propose_slash error: " << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
std::string simple_wallet::generate_mnemonic(Crypto::SecretKey &private_spend_key) {
  std::string mnemonic_str;
  crypto::ElectrumWords::bytes_to_words(private_spend_key, mnemonic_str, "English");
  return mnemonic_str;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::is_valid_mnemonic(std::string &mnemonic_phrase, Crypto::SecretKey &private_spend_key) {
  static std::string languages[] = {"English"};
  static const int num_of_languages = 1;
  static const int mnemonic_phrase_length = 25;

  std::vector<std::string> words;
  words = boost::split(words, mnemonic_phrase, ::isspace);

  if (words.size() != mnemonic_phrase_length) {
    logger(ERROR, BRIGHT_RED) << "Invalid mnemonic phrase!";
    logger(ERROR, BRIGHT_RED) << "Seed phrase is not 25 words! Please try again.";
    return false;
  }

  for (int i = 0; i < num_of_languages; i++) {
    if (crypto::ElectrumWords::words_to_bytes(mnemonic_phrase, private_spend_key, languages[i])) {
      return true;
    }
  }

  logger(ERROR, BRIGHT_RED) << "Invalid mnemonic phrase!";
  return false;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::reset(const std::vector<std::string> &args) {
  {
    std::unique_lock<std::mutex> lock(m_walletSynchronizedMutex);
    m_walletSynchronized = false;
  }

  m_wallet->reset();
  success_msg_writer(true) << "Reset completed successfully.";

  std::unique_lock<std::mutex> lock(m_walletSynchronizedMutex);
  while (!m_walletSynchronized) {
    m_walletSynchronizedCV.wait(lock);
  }

  std::cout << std::endl;
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::start_mining(const std::vector<std::string>& args) {
  COMMAND_RPC_START_MINING::request req;
  req.miner_address = m_wallet->getAddress();

  bool ok = true;
  size_t max_mining_threads_count = (std::max)(std::thread::hardware_concurrency(), static_cast<unsigned>(2));
  if (0 == args.size()) {
    req.threads_count = 1;
  } else if (1 == args.size()) {
    uint16_t num = 1;
    ok = Common::fromString(args[0], num);
    ok = ok && (1 <= num && num <= max_mining_threads_count);
    req.threads_count = num;
  } else {
    ok = false;
  }

  if (!ok) {
    fail_msg_writer() << "invalid arguments. Please use start_mining [<number_of_threads>], " <<
      "<number_of_threads> should be from 1 to " << max_mining_threads_count;
    return true;
  }

  COMMAND_RPC_START_MINING::response res;

  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
    invokeJsonCommand(httpClient, "/start_mining", req, res);

    std::string err = interpret_rpc_response(true, res.status);
    if (err.empty())
      success_msg_writer() << "Mining started in daemon";
    else
      fail_msg_writer() << "mining has NOT been started: " << err;

  } catch (const ConnectException&) {
    printConnectionError();
  } catch (const std::exception& e) {
    fail_msg_writer() << "Failed to invoke rpc method: " << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::stop_mining(const std::vector<std::string>& args) {
  COMMAND_RPC_STOP_MINING::request req;
  COMMAND_RPC_STOP_MINING::response res;

  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
    invokeJsonCommand(httpClient, "/stop_mining", req, res);
    std::string err = interpret_rpc_response(true, res.status);
    if (err.empty())
      success_msg_writer() << "Mining stopped in daemon";
    else
      fail_msg_writer() << "mining has NOT been stopped: " << err;
  } catch (const ConnectException&) {
    printConnectionError();
  } catch (const std::exception& e) {
    fail_msg_writer() << "Failed to invoke rpc method: " << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::get_reserve_proof(const std::vector<std::string> &args) {
  if (args.size() != 1 && args.size() != 2) {
    fail_msg_writer() << "Usage: get_reserve_proof (all|<amount>) [<message>]";
    return true;
  }

  uint64_t reserve = 0;
  if (args[0] != "all") {
    if (!m_currency.parseAmount(args[0], reserve)) {
      fail_msg_writer() << "amount is wrong: " << args[0];
      return true;
    }
  } else {
    reserve = m_wallet->actualBalance();
  }

  try {
    const std::string sig_str = m_wallet->getReserveProof(reserve, args.size() == 2 ? args[1] : "");

    const std::string filename = "reserve_proof_" + args[0] + "_XFG.txt";
    boost::system::error_code ec;
    if (boost::filesystem::exists(filename, ec)) {
      boost::filesystem::remove(filename, ec);
    }

    std::ofstream proofFile(filename, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!proofFile.good()) {
      return false;
    }
    proofFile << sig_str;

    success_msg_writer() << "signature file saved to: " << filename;

  } catch (const std::exception &e) {
    fail_msg_writer() << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::get_tx_proof(const std::vector<std::string> &args) {
  if(args.size() != 2 && args.size() != 3) {
    fail_msg_writer() << "Usage: get_tx_proof <txid> <dest_address> [<txkey>]";
    return true;
  }

  const std::string &str_hash = args[0];
  Crypto::Hash txid;
  if (!parse_hash256(str_hash, txid)) {
    fail_msg_writer() << "Failed to parse txid";
    return true;
  }

  const std::string address_string = args[1];
  CryptoNote::AccountPublicAddress address;
  if (!m_currency.parseAccountAddressString(address_string, address)) {
     fail_msg_writer() << "Failed to parse address " << address_string;
     return true;
  }

  std::string sig_str;
  Crypto::SecretKey tx_key, tx_key2;
  bool r = m_wallet->get_tx_key(txid, tx_key);

  if (args.size() == 3) {
    Crypto::Hash tx_key_hash;
    size_t size;
    if (!Common::fromHex(args[2], &tx_key_hash, sizeof(tx_key_hash), size) || size != sizeof(tx_key_hash)) {
      fail_msg_writer() << "failed to parse tx_key";
      return true;
    }
    tx_key2 = *(struct Crypto::SecretKey *) &tx_key_hash;

    if (r) {
      if (args.size() == 3 && tx_key != tx_key2) {
        fail_msg_writer() << "Txn secret key was found for the given txid, but you've also provided another txn secret key which doesn't match the found one.";
        return true;
      }
    }
    tx_key = tx_key2;
  } else {
    if (!r) {
      fail_msg_writer() << "Txn secret key wasn't found in the wallet file. Provide it as the optional third parameter if you have it elsewhere.";
      return true;
    }
  }

  if (m_wallet->getTxProof(txid, address, tx_key, sig_str)) {
    success_msg_writer() << "Signature: " << sig_str << std::endl;
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
void simple_wallet::initCompleted(std::error_code result) {
  if (m_initResultPromise.get() != nullptr) {
    m_initResultPromise->set_value(result);
  }
}

//----------------------------------------------------------------------------------------------------
void simple_wallet::connectionStatusUpdated(bool connected) {
  if (connected) {
    logger(INFO, GREEN) << "Wallet connected to daemon.";
  } else {
    printConnectionError();
  }
}

//----------------------------------------------------------------------------------------------------
void simple_wallet::externalTransactionCreated(CryptoNote::TransactionId transactionId) {
  WalletLegacyTransaction txInfo;
  m_wallet->getTransaction(transactionId, txInfo);

  std::stringstream logPrefix;
  if (txInfo.blockHeight == WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT) {
    logPrefix << "Unconfirmed";
  } else {
    logPrefix << "Height " << txInfo.blockHeight << ',';
  }

  if (txInfo.totalAmount >= 0) {
    logger(INFO, GREEN) <<
      logPrefix.str() << " transaction " << Common::podToHex(txInfo.hash) <<
      ", received " << m_currency.formatAmount(txInfo.totalAmount);
  } else {
    logger(INFO, MAGENTA) <<
      logPrefix.str() << " transaction " << Common::podToHex(txInfo.hash) <<
      ", spent " << m_currency.formatAmount(static_cast<uint64_t>(-txInfo.totalAmount));
  }

  if (txInfo.blockHeight == WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT) {
    m_refresh_progress_reporter.update(m_node->getLastLocalBlockHeight(), true);
  } else {
    m_refresh_progress_reporter.update(txInfo.blockHeight, true);
  }
}

//----------------------------------------------------------------------------------------------------
void simple_wallet::synchronizationCompleted(std::error_code result) {
  std::unique_lock<std::mutex> lock(m_walletSynchronizedMutex);
  m_walletSynchronized = true;
  m_walletSynchronizedCV.notify_one();
}

//----------------------------------------------------------------------------------------------------
void simple_wallet::synchronizationProgressUpdated(uint32_t current, uint32_t total) {
  std::unique_lock<std::mutex> lock(m_walletSynchronizedMutex);
  if (!m_walletSynchronized) {
    m_refresh_progress_reporter.update(current, false);
  }
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::show_balance(const std::vector<std::string>& args) {
  success_msg_writer() << "available balance: " << m_currency.formatAmount(m_wallet->actualBalance()) <<
    ", locked amount: " << m_currency.formatAmount(m_wallet->pendingBalance());
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::sign_message(const std::vector<std::string>& args) {
  if(args.size() < 1) {
    fail_msg_writer() << "Use: sign_message <message>";
    return true;
  }

  AccountKeys keys;
  m_wallet->getAccountKeys(keys);

  Crypto::Hash message_hash;
  Crypto::Signature sig;
  Crypto::cn_fast_hash(args[0].data(), args[0].size(), message_hash);
  Crypto::generate_signature(message_hash, keys.address.spendPublicKey, keys.spendSecretKey, sig);

  success_msg_writer() << "Sig" << Tools::Base58::encode(std::string(reinterpret_cast<char*>(&sig)));
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::verify_signature(const std::vector<std::string>& args) {
  if (args.size() != 3) {
    fail_msg_writer() << "Use: verify_signature <message> <address> <signature>";
    return true;
  }

  const std::string& encodedSig = args[2];
  const char* prefix_literal = "Sig";
  const size_t prefix_size = strlen(prefix_literal);
  if (encodedSig.size() <= prefix_size || encodedSig.substr(0, prefix_size) != prefix_literal) {
    fail_msg_writer() << "Invalid signature prefix";
    return true;
  }

  Crypto::Hash message_hash;
  Crypto::cn_fast_hash(args[0].data(), args[0].size(), message_hash);

  std::string decodedSig;
  if (!Tools::Base58::decode(encodedSig.substr(prefix_size), decodedSig)) {
    fail_msg_writer() << "Failed to decode signature";
    return true;
  }
  Crypto::Signature sig;
  std::memcpy(&sig, decodedSig.data(), sizeof(sig));

  uint64_t prefix = 0;
  CryptoNote::AccountPublicAddress addr;
  if (!CryptoNote::parseAccountAddressString(prefix, addr, args[1])) {
    fail_msg_writer() << "Failed to parse address";
    return true;
  }

  if (Crypto::check_signature(message_hash, addr.spendPublicKey, sig))
    success_msg_writer() << "Valid";
  else
    success_msg_writer() << "Invalid";
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::create_integrated(const std::vector<std::string>& args) {
  if (args.empty()) {
    fail_msg_writer() << "Please enter a payment ID";
    return true;
  }

  std::string paymentID = args[0];
  std::regex hexChars("^[0-9a-f]+$");
  if(paymentID.size() != 64 || !regex_match(paymentID, hexChars)) {
    fail_msg_writer() << "Invalid payment ID";
    return true;
  }

  std::string address = m_wallet->getAddress();
  uint64_t prefix;
  CryptoNote::AccountPublicAddress addr;

  if(!CryptoNote::parseAccountAddressString(prefix, addr, address)) {
    logger(ERROR, BRIGHT_RED) << "Failed to parse account address from string";
    return true;
  }

  CryptoNote::BinaryArray ba;
  CryptoNote::toBinaryArray(addr, ba);
  std::string keys = Common::asString(ba);

  std::string integratedAddress = Tools::Base58::encode_addr(
    m_currency.isTestnet() ? CryptoNote::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX_TESTNET : CryptoNote::parameters::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX,
    paymentID + keys
  );

  std::cout << std::endl << "Integrated address: " << integratedAddress << std::endl << std::endl;
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::export_keys(const std::vector<std::string>& args) {
  AccountKeys keys;
  m_wallet->getAccountKeys(keys);

  std::string secretKeysData = std::string(reinterpret_cast<char*>(&keys.spendSecretKey), sizeof(keys.spendSecretKey)) + std::string(reinterpret_cast<char*>(&keys.viewSecretKey), sizeof(keys.viewSecretKey));
  std::string guiKeys = Tools::Base58::encode_addr(
    m_currency.isTestnet() ? CryptoNote::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX_TESTNET : CryptoNote::parameters::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX,
    secretKeysData
  );

  logger(INFO, BRIGHT_GREEN) << std::endl << "fire_wallet is an open-source, client-side, free wallet which allows you to send & receive Fuego instantly on the blockchain. You are in control of your funds & your private keys. When you generate a new wallet, login, send, receive or deposit XFG - everything happens locally. Your seed is never transmitted, received or stored. That's why IT IS IMPERATIVE to write down, print or save your seed somewhere safe. The backup of keys is your responsibility only. If you lose your seed, your account can not be recovered. Freedom isn't free - the cost is responsibility. Protect your keys." << std::endl << std::endl;

  std::cout << "Private spend key: " << Common::podToHex(keys.spendSecretKey) << std::endl;
  std::cout << "Private view key: " <<  Common::podToHex(keys.viewSecretKey) << std::endl;

  Crypto::PublicKey unused_dummy_variable;
  Crypto::SecretKey deterministic_private_view_key;

  AccountBase::generateViewFromSpend(keys.spendSecretKey, deterministic_private_view_key, unused_dummy_variable);

  bool deterministic_private_keys = deterministic_private_view_key == keys.viewSecretKey;

  if (deterministic_private_keys) {
    std::cout << "Mnemonic seed: " << generate_mnemonic(keys.spendSecretKey) << std::endl << std::endl;
  }
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::show_incoming_transfers(const std::vector<std::string>& args) {
  bool hasTransfers = false;
  size_t transactionsCount = m_wallet->getTransactionCount();
  for (size_t trantransactionNumber = 0; trantransactionNumber < transactionsCount; ++trantransactionNumber) {
    WalletLegacyTransaction txInfo;
    m_wallet->getTransaction(trantransactionNumber, txInfo);
    if (txInfo.totalAmount < 0) continue;
    hasTransfers = true;
    logger(INFO) << "        amount       \t                              tx id";
    logger(INFO, GREEN) <<
      std::setw(21) << m_currency.formatAmount(txInfo.totalAmount) << '\t' << Common::podToHex(txInfo.hash);
  }

  if (!hasTransfers) success_msg_writer() << "No incoming transfers";
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::listTransfers(const std::vector<std::string>& args) {
  bool haveTransfers = false;
  bool haveBlockHeight = false;
  std::string blockHeightString = "";
  uint32_t blockHeight = 0;
  WalletLegacyTransaction txInfo;

  if (args.empty()) {
    haveBlockHeight = false;
  } else {
    blockHeightString = args[0];
    haveBlockHeight = true;
    blockHeight = atoi(blockHeightString.c_str());
  }

  size_t transactionsCount = m_wallet->getTransactionCount();
  for (size_t trantransactionNumber = 0; trantransactionNumber < transactionsCount; ++trantransactionNumber) {
    m_wallet->getTransaction(trantransactionNumber, txInfo);
    if (txInfo.state != WalletLegacyTransactionState::Active || txInfo.blockHeight == WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT) {
      continue;
    }

    if (!haveTransfers) {
      printListTransfersHeader(logger);
      haveTransfers = true;
    }

    if (haveBlockHeight == false) {
      printListTransfersItem(logger, txInfo, *m_wallet, m_currency);
    } else {
      if (txInfo.blockHeight >= blockHeight) {
        printListTransfersItem(logger, txInfo, *m_wallet, m_currency);
      }
    }
  }

  if (!haveTransfers) {
    success_msg_writer() << "No transfers";
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::show_payments(const std::vector<std::string> &args) {
  if (args.empty()) {
    fail_msg_writer() << "expected at least one payment ID";
    return true;
  }

  try {
    auto hashes = args;
    std::sort(std::begin(hashes), std::end(hashes));
    hashes.erase(std::unique(std::begin(hashes), std::end(hashes)), std::end(hashes));
    std::vector<PaymentId> paymentIds;
    paymentIds.reserve(hashes.size());
    std::transform(std::begin(hashes), std::end(hashes), std::back_inserter(paymentIds), [](const std::string& arg) {
      PaymentId paymentId;
      if (!CryptoNote::parsePaymentId(arg, paymentId)) {
        throw std::runtime_error("payment ID has invalid format: \"" + arg + "\", expected 64-character string");
      }
      return paymentId;
    });

    logger(INFO) << "                            payment                             \t" <<
      "                          transaction                           \t" <<
      "  height\t       amount        ";

    auto payments = m_wallet->getTransactionsByPaymentIds(paymentIds);

    for (auto& payment : payments) {
      for (auto& transaction : payment.transactions) {
        success_msg_writer(true) <<
          Common::podToHex(payment.paymentId) << '\t' <<
          Common::podToHex(transaction.hash) << '\t' <<
          std::setw(8) << transaction.blockHeight << '\t' <<
          std::setw(21) << m_currency.formatAmount(transaction.totalAmount);
      }

      if (payment.transactions.empty()) {
        success_msg_writer() << "No payments with id " << Common::podToHex(payment.paymentId);
      }
    }
  } catch (std::exception& e) {
    fail_msg_writer() << "show_payments exception: " << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::show_blockchain_height(const std::vector<std::string>& args) {
  try {
    uint64_t height = m_node->getLastLocalBlockHeight();
    success_msg_writer() << height;
  } catch (std::exception &e) {
    fail_msg_writer() << "failed to get blockchain height: " << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::show_num_unlocked_outputs(const std::vector<std::string>& args) {
  try {
    std::vector<TransactionOutputInformation> unlocked_outputs = m_wallet->getUnspentOutputs();
    success_msg_writer() << "Count: " << unlocked_outputs.size();
    for (const auto& out : unlocked_outputs) {
      success_msg_writer() << "Key: " << out.transactionPublicKey << " amount: " << m_currency.formatAmount(out.amount);
    }
  } catch (std::exception &e) {
    fail_msg_writer() << "failed to get outputs: " << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::optimize_outputs(const std::vector<std::string>& args) {
  try {
    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

    std::vector<CryptoNote::WalletLegacyTransfer> transfers;
    std::vector<CryptoNote::TransactionMessage> messages;
    std::string extraString;
    uint64_t fee = m_currency.minimumFee();
    uint64_t mixIn = 0;
    uint64_t unlockTimestamp = 0;
    uint64_t ttl = 0;
    Crypto::SecretKey transactionSK;
    CryptoNote::TransactionId tx = m_wallet->sendTransaction(transactionSK, transfers, fee, extraString, mixIn, unlockTimestamp, messages, ttl);
    if (tx == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      fail_msg_writer() << "Can't send money";
      return true;
    }

    std::error_code sendError = sent.wait(tx);
    removeGuard.removeObserver();

    if (sendError) {
      fail_msg_writer() << sendError.message();
      return true;
    }

    CryptoNote::WalletLegacyTransaction txInfo;
    m_wallet->getTransaction(tx, txInfo);
    success_msg_writer(true) << "Money successfully sent, transaction " << Common::podToHex(txInfo.hash);
    success_msg_writer(true) << "Transaction secret key " << Common::podToHex(transactionSK);

    try {
      CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file);
    } catch (const std::exception& e) {
      fail_msg_writer() << e.what();
      return true;
    }
  } catch (const std::system_error& e) {
    fail_msg_writer() << e.what();
  } catch (const std::exception& e) {
    fail_msg_writer() << e.what();
  } catch (...) {
    fail_msg_writer() << "unknown error";
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::optimize_all_outputs(const std::vector<std::string>& args) {
  uint64_t num_unlocked_outputs = 0;

  try {
    num_unlocked_outputs = m_wallet->getNumUnlockedOutputs();
    success_msg_writer() << "Total outputs: " << num_unlocked_outputs;
  } catch (std::exception &e) {
    fail_msg_writer() << "failed to get outputs: " << e.what();
  }

  uint64_t remainder = num_unlocked_outputs % 100;
  uint64_t rounds = (num_unlocked_outputs - remainder) / 100;
  success_msg_writer() << "Total optimization rounds: " << rounds;

  for(uint64_t a = 1; a < rounds; a = a + 1) {
    try {
      CryptoNote::WalletHelper::SendCompleteResultObserver sent;
      WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

      std::vector<CryptoNote::WalletLegacyTransfer> transfers;
      std::vector<CryptoNote::TransactionMessage> messages;
      std::string extraString;
      uint64_t fee = m_currency.minimumFee();
      uint64_t mixIn = 0;
      uint64_t unlockTimestamp = 0;
      uint64_t ttl = 0;
      Crypto::SecretKey transactionSK;
      CryptoNote::TransactionId tx = m_wallet->sendTransaction(transactionSK, transfers, fee, extraString, mixIn, unlockTimestamp, messages, ttl);
      if (tx == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
        fail_msg_writer() << "Can't send money";
        return true;
      }

      std::error_code sendError = sent.wait(tx);
      removeGuard.removeObserver();

      if (sendError) {
        fail_msg_writer() << sendError.message();
        return true;
      }

      CryptoNote::WalletLegacyTransaction txInfo;
      m_wallet->getTransaction(tx, txInfo);
      success_msg_writer(true) << a << ". Optimization transaction successfully sent, transaction " << Common::podToHex(txInfo.hash);

      try {
        CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file);
      } catch (const std::exception& e) {
        fail_msg_writer() << e.what();
        return true;
      }
    } catch (const std::system_error& e) {
      fail_msg_writer() << e.what();
    } catch (const std::exception& e) {
      fail_msg_writer() << e.what();
    } catch (...) {
      fail_msg_writer() << "unknown error";
    }
  }
  return true;
}

//----------------------------------------------------------------------------------------------------
std::string simple_wallet::resolveAlias(const std::string& aliasUrl) {
  std::string host;
  std::string uri;
  std::vector<std::string>records;
  std::string address;

  if (!Common::fetch_dns_txt(aliasUrl, records)) {
    #ifdef _WIN32
    throw std::runtime_error("Failed to lookup DNS record for: " + aliasUrl);
    #else
    // DNS TXT resolution not available on this platform (macOS/Linux)
    // Users can still use standard Fuego wallet addresses directly
    throw std::runtime_error("OpenAlias (oa1:xfg) not supported on this platform. Please use a standard Fuego wallet address directly, or use Windows/a system with DNS resolver support.");
    #endif
  }

  for (const auto& record : records) {
    if (processServerAliasResponse(record, address)) {
      return address;
    }
  }
  throw std::runtime_error("Failed to parse OpenAlias response for: " + aliasUrl);
}

//----------------------------------------------------------------------------------------------------
std::string simple_wallet::getFeeAddress() {
  HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);

  HttpRequest req;
  HttpResponse res;

  req.setUrl("/feeaddress");
  try {
    httpClient.request(req, res);
  } catch (const std::exception& e) {
    fail_msg_writer() << "Error connecting to the remote node: " << e.what();
  }

  if (res.getStatus() != HttpResponse::STATUS_200) {
    fail_msg_writer() << "Remote node returned code " + std::to_string(res.getStatus());
  }

  std::string address;
  if (!processServerFeeAddressResponse(res.getBody(), address)) {
    fail_msg_writer() << "Failed to parse remote node response";
  }

  return address;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::transfer(const std::vector<std::string> &args) {
  try {
    TransferCommand cmd(m_currency);

    if (!cmd.parseArguments(logger, args))
      return true;

    for (auto& kv: cmd.aliases) {
      std::string address;

      try {
        address = resolveAlias(kv.first);

        AccountPublicAddress ignore;
        if (!m_currency.parseAccountAddressString(address, ignore)) {
          throw std::runtime_error("Address \"" + address + "\" is invalid");
        }
      } catch (std::exception& e) {
        fail_msg_writer() << "Couldn't resolve alias: " << e.what() << ", alias: " << kv.first;
        return true;
      }

      for (auto& transfer: kv.second) {
        transfer.address = address;
      }
    }

    if (!cmd.aliases.empty()) {
      if (!askAliasesTransfersConfirmation(cmd.aliases, m_currency)) {
        return true;
      }

      for (auto& kv: cmd.aliases) {
        std::copy(std::move_iterator<std::vector<WalletLegacyTransfer>::iterator>(kv.second.begin()),
                  std::move_iterator<std::vector<WalletLegacyTransfer>::iterator>(kv.second.end()),
                  std::back_inserter(cmd.dsts));
      }
    }

    std::vector<TransactionMessage> messages;
    for (auto dst : cmd.dsts) {
      for (auto msg : cmd.messages) {
        messages.emplace_back(TransactionMessage{ msg, dst.address });
      }
    }

    uint64_t ttl = 0;
    if (cmd.ttl != 0) {
      ttl = static_cast<uint64_t>(time(nullptr)) + cmd.ttl;
    }

    CryptoNote::WalletHelper::SendCompleteResultObserver sent;

    std::string extraString;
    std::copy(cmd.extra.begin(), cmd.extra.end(), std::back_inserter(extraString));

    WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

    cmd.fake_outs_count = CryptoNote::parameters::MIN_TX_MIXIN_SIZE;

    if (cmd.fee < CryptoNote::parameters::MINIMUM_FEE_8KH) {
      cmd.fee = CryptoNote::parameters::MINIMUM_FEE_8KH;
    }

    Crypto::SecretKey transactionSK;
    CryptoNote::TransactionId tx = m_wallet->sendTransaction(transactionSK, cmd.dsts, cmd.fee, extraString, cmd.fake_outs_count, 0, messages, ttl);
    if (tx == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      fail_msg_writer() << "Can't send money";
      return true;
    }

    std::error_code sendError = sent.wait(tx);
    removeGuard.removeObserver();

    if (sendError) {
      fail_msg_writer() << sendError.message();
      return true;
    }

    CryptoNote::WalletLegacyTransaction txInfo;
    m_wallet->getTransaction(tx, txInfo);
    success_msg_writer(true) << "Money successfully sent, transaction hash: " << Common::podToHex(txInfo.hash);
    success_msg_writer(true) << "Transaction secret key " << Common::podToHex(transactionSK);

    try {
      CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file);
    } catch (const std::exception& e) {
      fail_msg_writer() << e.what();
      return true;
    }
  } catch (const std::system_error& e) {
    fail_msg_writer() << e.what();
  } catch (const std::exception& e) {
    fail_msg_writer() << e.what();
  } catch (...) {
    fail_msg_writer() << "unknown error";
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::run() {
  {
    std::unique_lock<std::mutex> lock(m_walletSynchronizedMutex);
    while (!m_walletSynchronized) {
      m_walletSynchronizedCV.wait(lock);
    }
  }

  std::cout << std::endl;

  std::string addr_start = m_wallet->getAddress().substr(0, 6);
  m_consoleHandler.start(false, "[wallet " + addr_start + "]: ", Common::Console::Color::BrightYellow);
  return true;
}

//----------------------------------------------------------------------------------------------------
void simple_wallet::stop() {
  m_consoleHandler.requestStop();
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::print_address(const std::vector<std::string> &args) {
  success_msg_writer() << m_wallet->getAddress();
  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::process_command(const std::vector<std::string> &args) {
  return m_consoleHandler.runCommand(args);
}

//----------------------------------------------------------------------------------------------------
// @ ALIAS SYSTEM COMMANDS
//----------------------------------------------------------------------------------------------------
bool simple_wallet::register_alias(const std::vector<std::string> &args) {
  if (args.size() != 1) {
    fail_msg_writer() << "Usage: register_alias <alias>";
    fail_msg_writer() << "  Alias must be exactly 8 characters [a-z0-9&] (e.g., firenode)";
    fail_msg_writer() << "  UPPERCASE [A-Z0-9&] aliases are assigned via elderking_ceremony";
    return true;
  }

  std::string alias = args[0];

  // Validate length
  if (alias.length() != 8) {
    fail_msg_writer() << "Alias must be exactly 8 characters. Got " << alias.length() << ".";
    return true;
  }

  // Check if any uppercase letters present — block them, EFier aliases come from ceremony
  for (char c : alias) {
    if (c >= 'A' && c <= 'Z') {
      fail_msg_writer() << "UPPERCASE aliases are reserved for Elderfiers.";
      fail_msg_writer() << "Elderfier aliases are assigned during the ceremony:";
      fail_msg_writer() << "  elderking_ceremony  (alias chosen interactively)";
      fail_msg_writer() << "Use lowercase [a-z0-9&] for regular user aliases.";
      return true;
    }
  }

  // Validate all chars are [a-z0-9&]
  for (char c : alias) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '&');
    if (!ok) {
      fail_msg_writer() << "Invalid character '" << c << "'. Use [a-z0-9&] only.";
      return true;
    }
  }

  uint8_t aliasType = 1;  // Regular user alias only (EFier aliases come from ceremony)

  // Check if alias is already taken
  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
    COMMAND_RPC_GET_ALIAS::request checkReq;
    COMMAND_RPC_GET_ALIAS::response checkRes;
    checkReq.alias = alias;
    invokeJsonCommand(httpClient, "/get_alias", checkReq, checkRes);

    if (checkRes.found) {
      fail_msg_writer() << "Alias @" << alias << " is already registered.";
      return true;
    }
  } catch (const ConnectException&) {
    printConnectionError();
    return true;
  } catch (const std::exception& e) {
    fail_msg_writer() << "Failed to check alias availability: " << e.what();
    return true;
  }

  // Check if address already has an alias
  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
    COMMAND_RPC_GET_ALIAS_BY_ADDRESS::request addrReq;
    COMMAND_RPC_GET_ALIAS_BY_ADDRESS::response addrRes;
    addrReq.address = m_wallet->getAddress();
    invokeJsonCommand(httpClient, "/get_alias_by_address", addrReq, addrRes);

    if (addrRes.found) {
      fail_msg_writer() << "Your address already has alias @" << addrRes.alias;
      fail_msg_writer() << "Each address can only have one alias.";
      return true;
    }
  } catch (const ConnectException&) {
    printConnectionError();
    return true;
  } catch (const std::exception& e) {
    fail_msg_writer() << "Failed to check address alias: " << e.what();
    return true;
  }

  std::string walletAddress = m_wallet->getAddress();

  // Show confirmation summary
  success_msg_writer() << "";
  {
    success_msg_writer() << "Registering alias @" << alias;
    success_msg_writer() << "  Type: Regular [a-z0-9&]";
    if (m_currency.isTestnet()) {
      success_msg_writer() << "  Fee: self-transfer (testnet)";
    } else {
      success_msg_writer() << "  Fee: 1 XFG sent to Fuego Developer Fund";
    }
  }
  success_msg_writer() << "  Address: " << walletAddress;
  success_msg_writer() << "";
  success_msg_writer() << "Confirm? (1) OK  (2) No ";

  std::string confirm;
  std::getline(std::cin, confirm);

  if (confirm != "1" && confirm != "OK" && confirm != "Ok" && confirm != "ok") {
    success_msg_writer() << "Cancelled.";
    return true;
  }

  try {
    // Build the 0xEA alias registration extra
    CryptoNote::TransactionExtraAliasRegistration aliasReg;
    aliasReg.version = 1;
    aliasReg.alias = alias;
    aliasReg.aliasHash = Crypto::cn_fast_hash(alias.data(), alias.size());
    aliasReg.addressHash = Crypto::cn_fast_hash(walletAddress.data(), walletAddress.size());
    aliasReg.ownerAddress = "";  // Not stored on-chain for privacy — addressHash is sufficient
    aliasReg.aliasType = aliasType;

    if (!aliasReg.isValid()) {
      fail_msg_writer() << "Invalid alias registration data.";
      return true;
    }

    // Serialize alias registration into transaction extra
    std::vector<uint8_t> extra;
    if (!CryptoNote::addAliasToExtra(extra, aliasReg)) {
      fail_msg_writer() << "Failed to build alias transaction extra.";
      return true;
    }

    std::string extraString(extra.begin(), extra.end());

    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

    std::vector<CryptoNote::WalletLegacyTransfer> transfers;

    if (m_currency.isTestnet()) {
      // Testnet: send minimum amount to self
      CryptoNote::WalletLegacyTransfer selfTransfer;
      selfTransfer.address = walletAddress;
      selfTransfer.amount = m_currency.minimumFee();
      transfers.push_back(selfTransfer);
    } else {
      // Regular alias on mainnet: 1 XFG fee to Fuego Developer Fund
      // (EFier aliases are free and registered through elderking_ceremony)
      CryptoNote::WalletLegacyTransfer devFundTransfer;
      devFundTransfer.address = CryptoNote::FUEGO_DEV_FUND_ADDRESS;
      devFundTransfer.amount = CryptoNote::parameters::ALIAS_REGISTRATION_FEE;
      transfers.push_back(devFundTransfer);
    }

    std::vector<CryptoNote::TransactionMessage> messages;
    uint64_t fee = m_currency.minimumFee();
    uint64_t mixIn = 0;
    uint64_t unlockTimestamp = 0;
    uint64_t ttl = 0;
    Crypto::SecretKey transactionSK;

    CryptoNote::TransactionId tx = m_wallet->sendTransaction(transactionSK, transfers, fee, extraString, mixIn, unlockTimestamp, messages, ttl);

    if (tx == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      fail_msg_writer() << "Failed to create alias registration transaction.";
      return true;
    }

    std::error_code sendError = sent.wait(tx);
    removeGuard.removeObserver();

    if (sendError) {
      fail_msg_writer() << "Alias registration failed: " << sendError.message();
      return true;
    }

    CryptoNote::WalletLegacyTransaction txInfo;
    m_wallet->getTransaction(tx, txInfo);
    success_msg_writer(true) << "";
    success_msg_writer(true) << " XFG Alias registered successfully!";
    success_msg_writer(true) << "  Alias: @" << alias;
    success_msg_writer(true) << "  TX Hash: " << Common::podToHex(txInfo.hash);
    success_msg_writer(true) << "  The alias will be active after the transaction is confirmed.";

    try {
      CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file);
    } catch (const std::exception& e) {
      fail_msg_writer() << e.what();
      return true;
    }
  } catch (const std::exception& e) {
    fail_msg_writer() << "Alias registration error: " << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::lookup_alias(const std::vector<std::string> &args) {
  if (args.size() != 1) {
    fail_msg_writer() << "Usage: lookup_alias <alias_or_address>";
    return true;
  }

  std::string query = args[0];

  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);

    // If query looks like an address (long string), look up by address
    if (query.length() > 20) {
      COMMAND_RPC_GET_ALIAS_BY_ADDRESS::request req;
      COMMAND_RPC_GET_ALIAS_BY_ADDRESS::response res;
      req.address = query;
      invokeJsonCommand(httpClient, "/get_alias_by_address", req, res);

      if (res.found) {
        success_msg_writer() << "XFG Alias found:";
        success_msg_writer() << "  Alias:    @" << res.alias;
        success_msg_writer() << "  Address:  " << res.address;
        success_msg_writer() << "  Type:     " << (res.alias_type == 0 ? "Elderfier" : "Regular");
        success_msg_writer() << "  Block:    " << res.registered_block;
      } else {
        fail_msg_writer() << "No alias registered for that address.";
      }
    } else {
      // Look up by alias name
      COMMAND_RPC_GET_ALIAS::request req;
      COMMAND_RPC_GET_ALIAS::response res;
      req.alias = query;
      invokeJsonCommand(httpClient, "/get_alias", req, res);

      if (res.found) {
        success_msg_writer() << "XFG Alias found:";
        success_msg_writer() << "  Alias:    @" << res.alias;
        success_msg_writer() << "  Address:  " << res.address;
        success_msg_writer() << "  Type:     " << (res.alias_type == 0 ? "Elderfier" : "Regular");
        success_msg_writer() << "  Block:    " << res.registered_block;
      } else {
        fail_msg_writer() << "Alias @" << query << " not found.";
      }
    }
  } catch (const ConnectException&) {
    printConnectionError();
  } catch (const std::exception& e) {
    fail_msg_writer() << "Failed to look up alias: " << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::list_aliases(const std::vector<std::string> &args) {
  try {
    HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);

    COMMAND_RPC_GET_ALL_ALIASES::request req;
    COMMAND_RPC_GET_ALL_ALIASES::response res;
    invokeJsonCommand(httpClient, "/get_all_aliases", req, res);

    if (res.aliases.empty()) {
      success_msg_writer() << "No aliases registered on the network yet.";
      return true;
    }

    success_msg_writer() << "";
    success_msg_writer() << "Registered XFG Aliases (" << res.total << " total):";
    success_msg_writer() << "────────────────────────────────────────────────";

    for (const auto& entry : res.aliases) {
      std::string typeStr = (entry.alias_type == 0) ? "EFier" : "User ";
      success_msg_writer() << "  @" << entry.alias
                           << "  [" << typeStr << "]"
                           << "  Block: " << entry.registered_block
                           << "  Addr: " << entry.address.substr(0, 12) << "...";
    }

    success_msg_writer() << "────────────────────────────────────────────────";
    success_msg_writer() << "Total: " << res.total << " aliases";

  } catch (const ConnectException&) {
    printConnectionError();
  } catch (const std::exception& e) {
    fail_msg_writer() << "Failed to list aliases: " << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
void simple_wallet::printConnectionError() const {
  fail_msg_writer() << "wallet failed to connect to daemon (" << m_daemon_address << ").";
}

//----------------------------------------------------------------------------------------------------
// Sub-address sidecar persistence: <wallet_file>.subaddresses
// Format: one line per sub-address — "major minor address"
//----------------------------------------------------------------------------------------------------
void simple_wallet::loadSubAddresses() {
  m_subAddresses.clear();
  std::string path = m_wallet_file + ".subaddresses";
  std::ifstream f(path);
  if (!f.is_open()) return;
  uint32_t major, minor;
  std::string addr;
  while (f >> major >> minor >> addr) {
    m_subAddresses.emplace_back(major, minor, addr);
    // Re-subscribe each sub-address to the chain scanner so incoming outputs are detected.
    try {
      m_wallet->registerSubAddress(major, minor);
    } catch (...) {
      // If registration fails (e.g. wallet not yet fully initialized), skip silently.
      // The user can re-open the wallet to retry.
    }
  }
}

void simple_wallet::saveSubAddresses() const {
  std::string path = m_wallet_file + ".subaddresses";
  std::ofstream f(path);
  for (const auto& entry : m_subAddresses) {
    f << std::get<0>(entry) << " " << std::get<1>(entry) << " " << std::get<2>(entry) << "\n";
  }
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::gen_new_sub(const std::vector<std::string>& args) {
  // Determine index.
  uint32_t major = 0;
  uint32_t minor = 0;

  if (args.size() >= 2) {
    try {
      major = static_cast<uint32_t>(std::stoul(args[0]));
      minor = static_cast<uint32_t>(std::stoul(args[1]));
    } catch (...) {
      fail_msg_writer() << "Usage: gen_new_sub [major] [minor]";
      return true;
    }
  } else if (args.size() == 1) {
    fail_msg_writer() << "Usage: gen_new_sub [major] [minor]  (provide both or neither)";
    return true;
  } else {
    // Auto-increment: major=0, minor = next unused
    minor = 1;
    for (const auto& e : m_subAddresses) {
      if (std::get<0>(e) == 0) {
        minor = std::max(minor, std::get<1>(e) + 1);
      }
    }
  }

  if (major == 0 && minor == 0) {
    fail_msg_writer() << "Index (0,0) is the primary address. Use minor >= 1.";
    return true;
  }

  // Check for duplicate.
  for (const auto& e : m_subAddresses) {
    if (std::get<0>(e) == major && std::get<1>(e) == minor) {
      success_msg_writer() << "Sub-address [" << major << "," << minor << "] already exists:";
      success_msg_writer() << std::get<2>(e);
      return true;
    }
  }

  // Register with the wallet (subscribes the chain scanner and enables balance detection).
  std::string addrStr;
  try {
    addrStr = m_wallet->registerSubAddress(major, minor);
  } catch (const std::exception& e) {
    fail_msg_writer() << "Failed to register sub-address [" << major << "," << minor << "]: " << e.what();
    return true;
  } catch (...) {
    fail_msg_writer() << "Failed to register sub-address [" << major << "," << minor << "]: Unknown error";
    return true;
  }

  // Validate the returned address string
  if (addrStr.empty()) {
    fail_msg_writer() << "Sub-address registration returned empty string for [" << major << "," << minor << "]";
    return true;
  }

  // Basic sanity check - addresses should be reasonably long (base58 encoded)
  if (addrStr.size() < 10) {
    fail_msg_writer() << "Sub-address returned suspiciously short string for [" << major << "," << minor << "]: " << addrStr;
    return true;
  }

  m_subAddresses.emplace_back(major, minor, addrStr);
  saveSubAddresses();

  success_msg_writer() << "Sub-address [" << major << "," << minor << "]:";
  success_msg_writer() << addrStr;
  success_msg_writer() << "";
  success_msg_writer() << "Share this address with payers. Incoming funds will appear in your balance.";
  success_msg_writer() << "Each payer gets a unique address so payments cannot be correlated on-chain.";

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::list_subs(const std::vector<std::string>& args) {
  if (m_subAddresses.empty()) {
    success_msg_writer() << "No sub-addresses generated yet. Use: gen_new_sub";
    return true;
  }
  success_msg_writer() << "Sub-addresses for this wallet:";
  success_msg_writer() << "";
  for (const auto& e : m_subAddresses) {
    success_msg_writer() << "  [" << std::get<0>(e) << "," << std::get<1>(e) << "]  " << std::get<2>(e);
  }
  return true;
}
