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
#include "CryptoNoteCore/TransactionApi.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolHandler.h"
#include "NodeRpcProxy/NodeRpcProxy.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"
#include "Rpc/HttpClient.h"
#include "System/Timer.h"
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
#include "SwapDaemon/AdaptorSwap.h"
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
const command_line::arg_descriptor<uint16_t> arg_daemon_port = { "daemon-port", "Use daemon instance at port <arg> instead of default", 28280 };
const command_line::arg_descriptor<uint32_t> arg_log_level = { "set_log", "", INFO, true };
const command_line::arg_descriptor<bool> arg_testnet = { "testnet", "Used to deploy test nets. The daemon must be launched with --testnet flag", false };
const command_line::arg_descriptor< std::vector<std::string> > arg_command = { "command", "" };
const command_line::arg_descriptor<uint16_t> arg_wallet_rpc_port = { "wallet-rpc-port", "Wallet RPC port for swapxfg (enables balance + swap signing in TUI)", 8070 };

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
  m_consoleHandler.setHandler("get_swapKey", boost::bind(&simple_wallet::get_swapKey, this, boost::arg<1>()), "Show the public spend key for atomic swaps");
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
  m_consoleHandler.setHandler("deposit", boost::bind(&simple_wallet::deposit, this, boost::arg<1>()), "deposit <amount> <epochs> - Create CD / Certificate of Deposit (0.8, 8, 80, 800 XFG, 1-72 epochs where 1 epoch=900 blocks or about 5 days and 72 is about 1yr).");
  m_consoleHandler.setHandler("rollover", boost::bind(&simple_wallet::rollover, this, boost::arg<1>()), "rollover <id> <new_epochs> - Rollover a matured CD with compound interest (principal + interest reinvested).");
  m_consoleHandler.setHandler("list_cds", boost::bind(&simple_wallet::list_cds, this, boost::arg<1>()), "list_cds - List all CD (Certificate of Deposit) yield accounts");
  m_consoleHandler.setHandler("cd_info", boost::bind(&simple_wallet::cd_info, this, boost::arg<1>()), "cd_info <id> - Get detailed info on CD by ID");
  m_consoleHandler.setHandler("withdraw", boost::bind(&simple_wallet::withdraw, this, boost::arg<1>()), "withdraw <id> - Withdraw a matured CD");
  // Cold/off-chain STARK commands - hidden
  // m_consoleHandler.setHandler("list_burns", boost::bind(&simple_wallet::list_burns, this, boost::arg<1>()), "list_burns - List all XFG burn transactions (HEAT)");
  m_consoleHandler.setHandler("burn_info", boost::bind(&simple_wallet::burn_info, this, boost::arg<1>()), "burn_info <id> - Get detailed info of burn by ID");
  m_consoleHandler.setHandler("migrate_legacy_deposit", boost::bind(&simple_wallet::migrate_legacy_deposit, this, boost::arg<1>()), "migrate_legacy_deposit <id> - Migrate a pre-v3 legacy deposit to v3 format");
  // m_consoleHandler.setHandler("create_cold_secret", boost::bind(&simple_wallet::create_cold_secret, this, boost::arg<1>()), "create_cold_secret <amount> <term_blocks> <chain_code> <metadata> - Create COLD commitment");
  m_consoleHandler.setHandler("gen_proof", boost::bind(&simple_wallet::gen_proof, this, boost::arg<1>()), "gen_proof <tx_hash> - Data needed to generate STARK proof for deposit transaction (for L2 claims)");

  // @ Alias system commands
  m_consoleHandler.setHandler("register_alias", boost::bind(&simple_wallet::register_alias, this, boost::arg<1>()), "register_alias <alias> - Register an @ alias (8 chars [a-z0-9&], costs 1 XFG).");
  m_consoleHandler.setHandler("lookup_alias", boost::bind(&simple_wallet::lookup_alias, this, boost::arg<1>()), "lookup_alias <alias_or_address> - Look up an @ alias by name or wallet address");
  m_consoleHandler.setHandler("list_aliases", boost::bind(&simple_wallet::list_aliases, this, boost::arg<1>()), "list_aliases - List all registered @ aliases on the network");
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
  m_wallet_rpc_port = command_line::get_arg(vm, arg_wallet_rpc_port);
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

    // Validate amount is one of the allowed CD tiers
    std::vector<uint64_t> valid_amounts = {
      CryptoNote::parameters::AMOUNT_TIER_0,                       // 0.8 XFG
      CryptoNote::parameters::AMOUNT_TIER_1,                       // 8 XFG
      CryptoNote::parameters::AMOUNT_TIER_2,                       // 80 XFG
      CryptoNote::parameters::AMOUNT_TIER_3                        // 800 XFG
    };

    std::vector<std::string> amount_labels = {
      "0.8 XFG",
      "8 XFG",
      "80 XFG",
      "800 XFG"
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

    // Parse term in epochs
    uint32_t term_epochs = boost::lexical_cast<uint32_t>(args[1]);
    uint32_t deposit_term = 0;
    std::string term_label = "";

    // Get min/max epochs from CD config
    uint32_t min_epochs = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_CD_MIN_EPOCHS
                                                  : CryptoNote::parameters::CD_MIN_EPOCHS;
    uint32_t max_epochs = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_CD_MAX_EPOCHS
                                                  : CryptoNote::parameters::CD_MAX_EPOCHS;

    // Validate epoch range based on actual term limits
    if (term_epochs < min_epochs || term_epochs > max_epochs) {
      fail_msg_writer() << "Invalid term. Must be " << min_epochs << "-" << max_epochs
                        << " epochs (1 epoch = 900 blocks ≈ 5 days).";
      fail_msg_writer() << "  Min: " << min_epochs << " epoch(s)";
      fail_msg_writer() << "  Max: " << max_epochs << " epoch(s)";
      return true;
    }

    // Calculate term in blocks using correct epoch duration
    uint32_t epoch_duration = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_EPOCH_DURATION_BLOCKS : CryptoNote::parameters::EPOCH_DURATION_BLOCKS;
    deposit_term = term_epochs * epoch_duration;
    uint32_t days_per_epoch = m_currency.isTestnet() ? 1 : 5;
    term_label = std::to_string(term_epochs) + " epoch(s) (~" + std::to_string(term_epochs * days_per_epoch) + " days)";

    // Confirm with user
    success_msg_writer() << "Creating new Fuego CD:";
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

    // Generate CD commitment - simple hash for on-chain interest tracking
    Crypto::Hash commitment;
    Crypto::generate_random_bytes(32, commitment.data);

    // Create the transaction extra with CD/YIELD commitment
    std::string cia_id = "";  // No CIA ID for standard CD deposits
    uint8_t claim_chain_code = 1;  // Default chain code
    std::vector<uint8_t> gift_secret;  // Empty - not gifting
    std::vector<uint8_t> metadata;  // Empty metadata

    if (!CryptoNote::createTxExtraWithYieldCommitment(commitment, deposit_amount, deposit_term, cia_id, metadata, claim_chain_code, gift_secret, extra)) {
      fail_msg_writer() << "Failed to create CD commitment data";
      return true;
    }

    success_msg_writer() << "CD commitment generated: " << Common::podToHex(commitment);
    success_msg_writer() << "";
    success_msg_writer() << "This CD earns on-chain interest from the fee pool.";
    success_msg_writer() << "Interest accrues automatically each epoch (900 blocks ≈ 5 days).";

    // Convert extra vector to string
    extraString = std::string(extra.begin(), extra.end());

    // Use IWalletLegacy deposit method with extra data
    uint64_t fee = m_currency.minimumFee();
    CryptoNote::TransactionId txId = m_wallet->deposit(deposit_term, deposit_amount, fee, extraString, 0);

    if (txId == CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      fail_msg_writer() << "Failed to create deposit transaction";
      return true;
    }

success_msg_writer(true) << "CD deposit transaction created successfully!";
    success_msg_writer() << "Transaction ID: " << txId;
    success_msg_writer() << "Amount: " << m_currency.formatAmount(deposit_amount);
    success_msg_writer() << "Term: " << term_label << " (" << deposit_term << " blocks)";
    success_msg_writer() << "";
    success_msg_writer() << "Your CD will earn interest from the swap fee pool.";
    success_msg_writer() << "Use 'rollover <id> <epochs>' to compound interest when mature.";
    success_msg_writer() << "Use 'withdraw <id>' to withdraw principal + interest when mature.";
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
bool simple_wallet::list_cds(const std::vector<std::string> &)
{
  size_t deposit_count = m_wallet->getDepositCount();

  if (deposit_count == 0)
  {
    success_msg_writer() << "No deposits found";
    return true;
  }

   success_msg_writer() << "Deposits (" << deposit_count << "):";
   success_msg_writer() << "ID    | Amount             | Term          | Unlock Height | Status | Key Image";
   success_msg_writer() << "------|--------------------|---------------|---------------|--------|---------";

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

     // Extract commitment (Key Image) from transaction extra
     std::string key_image_str = "";
     if (!deposit.extra.empty()) {
       std::vector<TransactionExtraField> extraFields;
       std::vector<uint8_t> extraBytes(deposit.extra.begin(), deposit.extra.end());
       if (parseTransactionExtra(extraBytes, extraFields)) {
         for (const auto& field : extraFields) {
           if (field.type() == typeid(TransactionExtraHeatCommitment)) {
             const auto& heatCommit = boost::get<TransactionExtraHeatCommitment>(field);
             key_image_str = Common::podToHex(heatCommit.commitment);
             break;
           } else if (field.type() == typeid(CryptoNote::TransactionExtraSimpleCD)) {
             const auto& coldCommit = boost::get<CryptoNote::TransactionExtraSimpleCD>(field);
             key_image_str = Common::podToHex(coldCommit.commitment);
             break;
           }
         }
       }
     }

     // Format amount
     std::string amount_str = m_currency.formatAmount(deposit.amount);

     // Format term (CD deposits)
     uint32_t cdMin = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_DEPOSIT_MIN_TERM
                                                 : CryptoNote::parameters::DEPOSIT_MIN_TERM;
     uint32_t cdMax = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_DEPOSIT_MAX_TERM
                                                 : CryptoNote::parameters::DEPOSIT_MAX_TERM;

     std::string term_str;
     // Convert blocks to epochs (1 epoch = 900 blocks ≈ 5 days)
     if (deposit.term >= cdMin && deposit.term <= cdMax) {
       uint32_t epochs = deposit.term / CryptoNote::parameters::EPOCH_DURATION_BLOCKS;
       uint32_t days_approx = epochs * 5;
       term_str = std::to_string(epochs) + " epoch(s) (~" + std::to_string(days_approx) + " days)";
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
       std::setw(8) << status_str << " | " <<
       key_image_str;
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
    success_msg_writer() << "  Banking Fee: " << m_currency.formatAmount(banking_fee) << " XFG (0.1% of amount)";
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
    uint32_t min_term = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_DEPOSIT_MIN_TERM : CryptoNote::parameters::DEPOSIT_MIN_TERM;
    uint32_t max_term = m_currency.isTestnet() ? CryptoNote::parameters::TESTNET_DEPOSIT_MAX_TERM : CryptoNote::parameters::DEPOSIT_MAX_TERM;

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
    success_msg_writer() << "  Banking Fee: " << m_currency.formatAmount(banking_fee) << " XFG (0.1% of amount)";
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

    /*
    std::vector<uint8_t> extra;
    CryptoNote::TransactionExtraColdCommitment coldCommitment;
    coldCommitment.commitment = starkResult.commitment;
    coldCommitment.amount = cold_amount;
    coldCommitment.term = cold_term;
    coldCommitment.claimChainCode = 1;  // Default to ETH chain

    CryptoNote::addColdCommitmentToExtra(extra, coldCommitment);

    // Encrypt STARK secret into tx extra (0xD5) so cd_info can retrieve it
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
    */
  }
  catch (const std::exception& e)
  {
    fail_msg_writer() << "Error: " << e.what();
    return true;
  }
}

//----------------------------------------------------------------------------------------------------


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
 //----------------------------------------------------------------------------------------------------
// Rollover matured CD with compound interest (principal + interest reinvested)
bool simple_wallet::rollover(const std::vector<std::string> &args)
{
  if (args.size() != 2)
  {
    fail_msg_writer() << "Usage: rollover <id> <new_epochs>";
    fail_msg_writer() << "  <id>: Deposit ID to rollover";
    fail_msg_writer() << "  <new_epochs>: New term in epochs (1-72, where 1 epoch = 900 blocks ≈ 5 days)";
    fail_msg_writer() << "";
    fail_msg_writer() << "Rollover reinvests principal + accrued interest into a new CD.";
    fail_msg_writer() << "NOTE: This command shows rollover info. To execute, manually:";
    fail_msg_writer() << "  1. withdraw <id>  (withdraw matured CD)";
    fail_msg_writer() << "  2. deposit <amount> <epochs>  (create new CD with principal+interest)";
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

    // Check if deposit is mature
    uint32_t current_height = m_node->getLastLocalBlockHeight();
    if (deposit.unlockHeight > current_height) {
      fail_msg_writer() << "Deposit is not yet mature. Unlock height: " << deposit.unlockHeight
                        << ", Current height: " << current_height;
      fail_msg_writer() << "Blocks remaining: " << (deposit.unlockHeight - current_height);
      return true;
    }

    // Parse new term in epochs
    uint32_t new_epochs = boost::lexical_cast<uint32_t>(args[1]);
    if (new_epochs < 1 || new_epochs > 72) {
      fail_msg_writer() << "Invalid term. Must be 1-72 epochs (1 epoch = 900 blocks ≈ 5 days).";
      return true;
    }

    uint32_t new_term_blocks = new_epochs * CryptoNote::parameters::EPOCH_DURATION_BLOCKS;
    uint32_t new_unlock_height = current_height + new_term_blocks;

    // TODO: Calculate actual interest from fee pool (requires CommitmentIndex access)
    // For now, show principal only
    uint64_t total_reinvest = deposit.amount;  // + interest (TODO)

    success_msg_writer() << "=== CD Rollover Information ===";
    success_msg_writer() << "Deposit ID: " << deposit_id;
    success_msg_writer() << "Principal: " << m_currency.formatAmount(deposit.amount);
    success_msg_writer() << "Accrued interest: (calculated from fee pool - TODO)";
    success_msg_writer() << "Total to reinvest: " << m_currency.formatAmount(total_reinvest);
    success_msg_writer() << "";
    success_msg_writer() << "New CD terms:";
    success_msg_writer() << "  Term: " << new_epochs << " epoch(s) (" << new_term_blocks << " blocks, ~" << (new_epochs * 5) << " days)";
    success_msg_writer() << "  New unlock height: " << new_unlock_height;
    success_msg_writer() << "";
    success_msg_writer() << "To execute this rollover:";
    success_msg_writer() << "  1. withdraw " << deposit_id << "  (withdraw matured CD with interest)";
    success_msg_writer() << "  2. deposit " << total_reinvest << " " << new_epochs << "  (create new CD)";
  }
  catch (std::exception &e)
  {
    fail_msg_writer() << "Rollover info failed: " << e.what();
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
          // Check for CD deposit commitment (0xCD = 205 tag in tx_extra)
          else if (field.type() == typeid(CryptoNote::TransactionExtraSimpleCD)) {
            const auto& coldDeposit = boost::get<CryptoNote::TransactionExtraSimpleCD>(field);

            success_msg_writer() << "Found CD transaction: " << tx_hash;
            success_msg_writer() << "Amount: " << m_currency.formatAmount(coldDeposit.amount);
            success_msg_writer() << "Term: " << coldDeposit.term << " blocks";

            std::cout << "\n=== XFG CERTIFICATE OF LEDGER DEPOSIT PROOF ===" << std::endl;
            std::cout << "Transaction Hash: " << tx_hash << std::endl;
            std::cout << "Commitment: " << Common::podToHex(coldDeposit.commitment) << std::endl;
            std::cout << "Amount: " << coldDeposit.amount << " heat (atomic XFG)" << std::endl;
            std::cout << "Term: " << coldDeposit.term << " blocks" << std::endl;
            std::cout << "=================================" << std::endl;

            logger(INFO, BRIGHT_GREEN) << "Proof data generated for CD transaction " << tx_hash;
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
bool simple_wallet::cd_info(const std::vector<std::string> &args)
{
  if (args.size() != 1)
  {
    fail_msg_writer() << "Usage: cd_info <id>";
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
        depositType = "CD (Certificate of Deposit)";
        typeDescription = "On-chain yield deposit - locked for your specified term, earns interest from fee pool";
        break;
      default:
        depositType = "Unknown";
        typeDescription = "Unknown deposit type";
    }

    // Display term (user-defined unlock time, independent of deposit type)
    if (deposit.term == CryptoNote::parameters::DEPOSIT_TERM_FOREVER) {
      success_msg_writer() << "Term:          FOREVER";
    } else if (deposit.term == CryptoNote::parameters::DEPOSIT_MIN_TERM) {
      success_msg_writer() << "Term:          3 months (16,440 blocks)";
    } else if (deposit.term == CryptoNote::parameters::DEPOSIT_MAX_TERM) {
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
               success_msg_writer() << "Key Image:     " << Common::podToHex(heatCommit.commitment);
             } else if (field.type() == typeid(CryptoNote::TransactionExtraSimpleCD)) {
               const auto& coldCommit = boost::get<CryptoNote::TransactionExtraSimpleCD>(field);
               success_msg_writer() << "Key Image:     " << Common::podToHex(coldCommit.commitment);
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
      fail_msg_writer() << "Deposit " << deposit_id << " is not a burn transaction (use 'cd_info' instead).";
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
bool simple_wallet::migrate_legacy_deposit(const std::vector<std::string> &args) {
  if (args.size() != 1) {
    fail_msg_writer() << "Usage: migrate_legacy_deposit <deposit_id>";
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

    // Must be a COLD deposit (not HEAT burn or legacy staking)
    if (deposit.depositType == CryptoNote::Deposit::Type::HEAT) {
      fail_msg_writer() << "Deposit " << depositId << " is a HEAT burn. Use burn_info to view it.";
      fail_msg_writer() << "HEAT burns already have v3 commitments if created after the v3 upgrade.";
      return true;
    }

    // Check if already has a 0xD5 secret (already v3)
    std::vector<uint8_t> extraBytes(deposit.extra.begin(), deposit.extra.end());
    CryptoNote::TransactionExtraDepositSecret existingSecret;
    if (CryptoNote::getDepositSecretFromExtra(extraBytes, existingSecret)) {
      fail_msg_writer() << "Deposit " << depositId << " already has a STARK secret (v3 format).";
      fail_msg_writer() << "Use 'cd_info " << depositId << "' to view it.";
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
    migration.targetChainId = 1; // ETH
    CryptoNote::addColdMigrationToExtra(migrationExtra, migration);

    // 0xD5 encrypted secret — so cd_info can decrypt it later
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
// swapxfg TUI launcher
//----------------------------------------------------------------------------------------------------

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <climits>
#include <cerrno>
#include <cstring>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

static std::string findSwapxfg() {
#ifdef _WIN32
  // Try same directory as current executable
  char self[MAX_PATH];
  if (GetModuleFileNameA(nullptr, self, MAX_PATH)) {
    std::string dir(self);
    auto pos = dir.rfind('\\');
    if (pos != std::string::npos) {
      std::string candidate = dir.substr(0, pos + 1) + "xfg-swap.exe";
      if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
        return candidate;
    }
  }
  return "";
#else
  // 1. Same directory as current executable
  std::string self;
#ifdef __APPLE__
  char path[PATH_MAX];
  uint32_t len = PATH_MAX;
  if (_NSGetExecutablePath(path, &len) == 0) {
    self = path;
  }
#else
  char self_buf[PATH_MAX];
  ssize_t len_read = readlink("/proc/self/exe", self_buf, sizeof(self_buf) - 1);
  if (len_read > 0) {
    self_buf[len_read] = '\0';
    self = self_buf;
  }
#endif

  if (!self.empty()) {
    std::string dir = self;
    dir = dir.substr(0, dir.rfind('/'));
    std::string candidate = dir + "/xfg-swap";
    if (access(candidate.c_str(), X_OK) == 0) return candidate;
  }

  // 1.5. Current working directory fallback
  if (access("./xfg-swap", X_OK) == 0) return "./xfg-swap";

  // 2. PATH fallback
  if (system("which xfg-swap > /dev/null 2>&1") == 0)
    return "xfg-swap";
  return "";
#endif
}

void simple_wallet::launchSwapxfg(bool testnet) {
  std::string swapxfgPath = findSwapxfg();
  if (swapxfgPath.empty()) {
    logger(Logging::WARNING) << "xfg-swap not found. To use the swap terminal, install xfg-swap:";
    logger(Logging::WARNING) << "  - Download from https://github.com/usexfg/fuego-suite/releases";
    logger(Logging::WARNING) << "  - Place xfg-swap in the same directory as fire_wallet";
    logger(Logging::INFO)    << "Alternatively, run manually:";
    logger(Logging::INFO)    << "  xfg-swap --wallet http://127.0.0.1:18182 --daemon http://127.0.0.1:" << m_daemon_port;
    return;
  }

  std::string daemonEndpoint = "http://" + (m_daemon_host.empty() ? "127.0.0.1" : m_daemon_host)
                               + ":" + std::to_string(m_daemon_port);

#ifdef _WIN32
  std::string cmd = "\"" + swapxfgPath + "\" --daemon \"" + daemonEndpoint + "\"";
  if (m_wallet_rpc_port) {
    cmd += " --wallet \"http://127.0.0.1:" + std::to_string(m_wallet_rpc_port) + "\"";
  }
  if (testnet) cmd += " --testnet";
  system(cmd.c_str());
#else
  pid_t pid = fork();
  if (pid == 0) {
    // child
    if (m_wallet_rpc_port) {
      std::string walletEndpoint = "http://127.0.0.1:" + std::to_string(m_wallet_rpc_port);
      if (testnet) {
        execlp(swapxfgPath.c_str(), "xfg-swap",
               "--wallet", walletEndpoint.c_str(),
               "--daemon", daemonEndpoint.c_str(),
               "--testnet", nullptr);
      } else {
        execlp(swapxfgPath.c_str(), "xfg-swap",
               "--wallet", walletEndpoint.c_str(),
               "--daemon", daemonEndpoint.c_str(), nullptr);
      }
    } else {
      if (testnet) {
        execlp(swapxfgPath.c_str(), "xfg-swap",
               "--daemon", daemonEndpoint.c_str(),
               "--testnet", nullptr);
      } else {
        execlp(swapxfgPath.c_str(), "xfg-swap",
               "--daemon", daemonEndpoint.c_str(), nullptr);
      }
    }
    _exit(1);
  } else if (pid > 0) {
    int status;
    waitpid(pid, &status, 0);
  } else {
    logger(Logging::ERROR) << "fork() failed: " << strerror(errno);
  }
#endif
}

bool simple_wallet::swap_tui(const std::vector<std::string>& /*args*/) {
  launchSwapxfg(false);
  return true;
}

//----------------------------------------------------------------------------------------------------
// Adaptor signature swap commands
//----------------------------------------------------------------------------------------------------
bool simple_wallet::initiate_swap(const std::vector<std::string> &args) {
  if (args.size() < 3 || args.size() > 4) {
    fail_msg_writer() << "Usage: initiate_swap <amount> <peer_pubkey_hex> <pair> [role]";
    fail_msg_writer() << "  amount:        XFG to lock in Musig2 escrow";
    fail_msg_writer() << "  peer_pubkey:   64-char hex Ed25519 public key of swap counterparty";
    fail_msg_writer() << "  pair:          XMR, ETH, or BCH";
    fail_msg_writer() << "  role:          alice (default) or bob";
    return true;
  }

  try {
    // Parse amount
    uint64_t amount;
    if (!m_currency.parseAmount(args[0], amount)) {
      fail_msg_writer() << "Invalid amount: " << args[0];
      return true;
    }

    // Parse peer public key
    Crypto::PublicKey peerPub;
    if (!Common::podFromHex(args[1], peerPub)) {
      fail_msg_writer() << "Invalid peer public key hex: " << args[1];
      return true;
    }

    // Parse pair
    std::string pairStr = args[2];
    std::transform(pairStr.begin(), pairStr.end(), pairStr.begin(), ::toupper);
    XfgSwap::SwapPair pair;
    if (pairStr == "XMR") pair = XfgSwap::SwapPair::XMR;
    else if (pairStr == "ETH") pair = XfgSwap::SwapPair::ETH;
    else if (pairStr == "BCH") pair = XfgSwap::SwapPair::BCH;
    else {
      fail_msg_writer() << "Invalid pair: " << args[2] << ". Use XMR, ETH, or BCH.";
      return true;
    }

    // Parse role (default: Alice)
    XfgSwap::SwapRole role = XfgSwap::SwapRole::ALICE;
    if (args.size() == 4) {
      std::string roleStr = args[3];
      std::transform(roleStr.begin(), roleStr.end(), roleStr.begin(), ::tolower);
      if (roleStr == "bob") role = XfgSwap::SwapRole::BOB;
      else if (roleStr != "alice") {
        fail_msg_writer() << "Invalid role: " << args[3] << ". Use alice or bob.";
        return true;
      }
    }

    // Initialize swap params
    XfgSwap::SwapParams params;
    params.pair = pair;
    params.role = role;
    params.xfgAmount = amount;
    params.peerSwapPubKey = peerPub;

    // Step 1: Generate our swap keypair
    XfgSwap::adaptor_generate_keys(params);

    // Step 2: Musig2 key aggregation → escrow address
    if (!XfgSwap::adaptor_key_aggregate(params)) {
      fail_msg_writer() << "Failed to aggregate Musig2 keys (invalid peer pubkey?)";
      return true;
    }

    // Step 3: Generate nonces for signing session
    XfgSwap::adaptor_nonce_generate(params);

    // Step 4: If Bob, generate adaptor secret + DLEQ proof
    if (role == XfgSwap::SwapRole::BOB) {
      // Use a deterministic base point for DLEQ (hash of escrow key)
      Crypto::PublicKey dleqBase;
      Crypto::hash_data_to_ec(
          reinterpret_cast<const uint8_t*>(&params.escrowPubKey), 32, dleqBase);

      if (!XfgSwap::adaptor_generate_adaptor(params, dleqBase)) {
        fail_msg_writer() << "Failed to generate adaptor point + DLEQ proof";
        return true;
      }
    }

    // Generate swap ID from escrow key hash
    Crypto::Hash swapIdHash;
    Crypto::cn_fast_hash(&params.escrowPubKey, sizeof(params.escrowPubKey), swapIdHash);
    params.swapId = Common::podToHex(swapIdHash).substr(0, 16);

    // Display swap info
    success_msg_writer() << "Swap initiated:";
    success_msg_writer() << "  Swap ID:        " << params.swapId;
    success_msg_writer() << "  Pair:           XFG/" << (pairStr);
    success_msg_writer() << "  Role:           " << (role == XfgSwap::SwapRole::ALICE ? "Alice (XFG seller)" : "Bob (XFG buyer)");
    success_msg_writer() << "  Amount:         " << m_currency.formatAmount(amount) << " XFG";
    success_msg_writer() << "  Our pubkey:     " << Common::podToHex(params.ourSwapPubKey);
    success_msg_writer() << "  Peer pubkey:    " << Common::podToHex(peerPub);
    success_msg_writer() << "  Escrow key:     " << Common::podToHex(params.escrowPubKey);
    success_msg_writer() << "  Our pub nonce0: " << Common::podToHex(params.musig2.ourPubNonce.R[0]);
    success_msg_writer() << "  Our pub nonce1: " << Common::podToHex(params.musig2.ourPubNonce.R[1]);

    if (role == XfgSwap::SwapRole::BOB) {
      success_msg_writer() << "  Adaptor point:  " << Common::podToHex(params.adaptorPoint);
      success_msg_writer() << "  DLEQ challenge: " << Common::podToHex(params.adaptorDleqProof.challenge);
      success_msg_writer() << "  DLEQ response:  " << Common::podToHex(params.adaptorDleqProof.response);
    }

    success_msg_writer() << "";
    success_msg_writer() << "Share with counterparty:";
    success_msg_writer() << "  pubkey=" << Common::podToHex(params.ourSwapPubKey);
    success_msg_writer() << "  nonce0=" << Common::podToHex(params.musig2.ourPubNonce.R[0]);
    success_msg_writer() << "  nonce1=" << Common::podToHex(params.musig2.ourPubNonce.R[1]);
    if (role == XfgSwap::SwapRole::BOB) {
      success_msg_writer() << "  adaptor=" << Common::podToHex(params.adaptorPoint);
    }
    success_msg_writer() << "";
    success_msg_writer() << "Next: wait for counterparty's nonces + adaptor point,";
    success_msg_writer() << "then use 'complete_swap' to finalize.";

  } catch (const std::exception& e) {
    fail_msg_writer() << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::complete_swap(const std::vector<std::string> &args) {
  if (args.size() != 3) {
    fail_msg_writer() << "Usage: complete_swap <adaptor_secret_hex> <peer_partial_sig_hex> <tx_prefix_hash_hex>";
    fail_msg_writer() << "  adaptor_secret:  64-char hex scalar t (learned from counterparty chain)";
    fail_msg_writer() << "  peer_partial_sig: 64-char hex Musig2 partial signature from peer";
    fail_msg_writer() << "  tx_prefix_hash:  64-char hex prefix hash of the claim transaction";
    fail_msg_writer() << "";
    fail_msg_writer() << "This command completes a Musig2 adaptor swap by adapting the peer's";
    fail_msg_writer() << "partial signature with the revealed adaptor secret, producing a valid";
    fail_msg_writer() << "Schnorr signature verifiable against the escrow Musig2 public key.";
    return true;
  }

  try {
    // Parse adaptor secret
    Crypto::EllipticCurveScalar adaptorSecret;
    if (!Common::podFromHex(args[0], adaptorSecret)) {
      fail_msg_writer() << "Invalid adaptor secret hex: " << args[0];
      return true;
    }

    // Parse peer partial sig
    Crypto::Musig2PartialSig peerSig;
    if (!Common::podFromHex(args[1], peerSig.s)) {
      fail_msg_writer() << "Invalid peer partial sig hex: " << args[1];
      return true;
    }

    // Parse tx prefix hash
    Crypto::Hash txPrefixHash;
    if (!Common::podFromHex(args[2], txPrefixHash)) {
      fail_msg_writer() << "Invalid tx prefix hash hex: " << args[2];
      return true;
    }

    // Adapt peer's partial sig: s_adapted = s_peer + t
    Crypto::Musig2PartialSig adaptedSig;
    sc_add(reinterpret_cast<unsigned char*>(&adaptedSig.s),
           reinterpret_cast<const unsigned char*>(&peerSig.s),
           reinterpret_cast<const unsigned char*>(&adaptorSecret));

    success_msg_writer() << "Adapted partial signature:";
    success_msg_writer() << "  Original:  " << Common::podToHex(peerSig.s);
    success_msg_writer() << "  Secret t:  " << Common::podToHex(adaptorSecret);
    success_msg_writer() << "  Adapted:   " << Common::podToHex(adaptedSig.s);
    success_msg_writer() << "";
    success_msg_writer() << "Use the adapted partial sig in Musig2 aggregation";
    success_msg_writer() << "to produce the final claim transaction signature.";

  } catch (const std::exception& e) {
    fail_msg_writer() << e.what();
  }

  return true;
}

//----------------------------------------------------------------------------------------------------
bool simple_wallet::refund_swap(const std::vector<std::string> &args) {
  if (args.size() != 2) {
    fail_msg_writer() << "Usage: refund_swap <peer_partial_sig_hex> <tx_prefix_hash_hex>";
    fail_msg_writer() << "  peer_partial_sig: 64-char hex Musig2 partial signature from peer";
    fail_msg_writer() << "  tx_prefix_hash:  64-char hex prefix hash of the refund transaction";
    fail_msg_writer() << "";
    fail_msg_writer() << "Cooperative refund: both parties sign a refund tx (no adaptor).";
    fail_msg_writer() << "The refund tx returns escrowed XFG to the original sender.";
    return true;
  }

  try {
    // Parse peer partial sig
    Crypto::Musig2PartialSig peerSig;
    if (!Common::podFromHex(args[0], peerSig.s)) {
      fail_msg_writer() << "Invalid peer partial sig hex: " << args[0];
      return true;
    }

    // Parse tx prefix hash
    Crypto::Hash txPrefixHash;
    if (!Common::podFromHex(args[1], txPrefixHash)) {
      fail_msg_writer() << "Invalid tx prefix hash hex: " << args[1];
      return true;
    }

    success_msg_writer() << "Cooperative refund signing:";
    success_msg_writer() << "  Peer partial sig: " << Common::podToHex(peerSig.s);
    success_msg_writer() << "  Tx prefix hash:   " << Common::podToHex(txPrefixHash);
    success_msg_writer() << "";
    success_msg_writer() << "Aggregate both partial sigs (no adaptor) to produce";
    success_msg_writer() << "the refund transaction signature, then broadcast.";

  } catch (const std::exception& e) {
    fail_msg_writer() << e.what();
  }

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

bool simple_wallet::get_swapKey(const std::vector<std::string>& args) {
  AccountKeys keys;
  m_wallet->getAccountKeys(keys);
  success_msg_writer() << "Public Spend Key: " << Common::podToHex(keys.address.spendPublicKey);
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
  std::cout << "Public spend key: " << Common::podToHex(keys.address.spendPublicKey) << std::endl;
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
    fail_msg_writer() << "  Alias must use lowercase [a-z0-9&] characters";
    return true;
  }

  std::string alias = args[0];

  // Validate length
  if (alias.length() != 8) {
    fail_msg_writer() << "Alias must be exactly 8 characters. Got " << alias.length() << ".";
    return true;
  }

  // Block uppercase — aliases must be lowercase
  for (char c : alias) {
    if (c >= 'A' && c <= 'Z') {
      fail_msg_writer() << "Uppercase characters are not allowed in aliases.";
      fail_msg_writer() << "Use lowercase [a-z0-9&] only.";
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

  uint8_t aliasType = 1;  // Regular user alias

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

  // Check for pending alias registration in unconfirmed transactions
  // This prevents registering multiple aliases before the first one confirms
  bool hasPendingAlias = false;
  size_t txCount = m_wallet->getTransactionCount();
  for (size_t i = 0; i < txCount; ++i) {
    CryptoNote::WalletLegacyTransaction txInfo;
    if (m_wallet->getTransaction(i, txInfo)) {
      // Check if transaction is unconfirmed and has alias registration extra
      if (txInfo.blockHeight == WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT) {
        // Check if transaction has alias registration tag (0xEA)
        if (!txInfo.extra.empty()) {
          if (txInfo.extra[0] == 0xEA) {  // TX_EXTRA_ALIAS_REGISTRATION
            hasPendingAlias = true;
            break;
          }
        }
      }
    }
  }

  if (hasPendingAlias) {
    fail_msg_writer() << "You already have a pending alias registration.";
    fail_msg_writer() << "Please wait for the transaction to confirm before registering another alias.";
    fail_msg_writer() << "Unconfirmed transactions will confirm in ~3-5 minutes (1-2 blocks).";
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
    // v2 addressHash: cn_fast_hash(spendKey||viewKey) — rainbow-table resistant.
    // Hashing the raw 64-byte key preimage (not the base58 string) prevents
    // precomputed base58 rainbow-table attacks on the on-chain hash.
    {
      CryptoNote::AccountPublicAddress addr;
      if (!m_currency.parseAccountAddressString(walletAddress, addr)) {
        fail_msg_writer() << "Failed to parse wallet address for addressHash computation.";
        return true;
      }
      uint8_t preimage[64];
      memcpy(preimage,      &addr.spendPublicKey, 32);
      memcpy(preimage + 32, &addr.viewPublicKey,  32);
      Crypto::cn_fast_hash(preimage, 64, aliasReg.addressHash);
    }
    aliasReg.ownerAddress = "";  // Not stored on-chain for privacy — addressHash is sufficient
    aliasReg.aliasType = aliasType;
    aliasReg.networkId = static_cast<uint32_t>(m_currency.getFuegoNetworkId());

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
        success_msg_writer() << "  Type:     Regular";
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
        success_msg_writer() << "  Type:     Regular";
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
      std::string typeStr = (entry.alias_type == 0) ? "Sys  " : "User ";
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
