// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2018-2023 Conceal Network & Conceal Devs
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <fstream>
#include <boost/program_options.hpp>

#include "SimpleWallet/SimpleWallet.h"
#include "TestnetWallet/TestnetWallet.h"
#include "TestnetWallet/Const.h"  // Testnet-specific constants (testnet RPC port 28280)
#include "SimpleWallet/ClientHelper.h"

#include "Common/CommandLine.h"
#include "Common/PathTools.h"
#include "Common/SignalHandler.h"
#include "Wallet/WalletRpcServer.h"

#include "version.h"

#include <Logging/LoggerManager.h>

// Namespace alias for compatibility
namespace cn = CryptoNote;

namespace po = boost::program_options;
using namespace Logging;

int main(int argc, char* argv[])
{
  po::options_description desc_general("General options");
  command_line::add_arg(desc_general, command_line::arg_help);
  command_line::add_arg(desc_general, command_line::arg_version);

  po::options_description desc_params("Testnet Wallet options");
  command_line::add_arg(desc_params, arg_wallet_file);
  command_line::add_arg(desc_params, arg_generate_new_wallet);
  command_line::add_arg(desc_params, arg_password);
  command_line::add_arg(desc_params, arg_daemon_address);
  command_line::add_arg(desc_params, arg_daemon_host);
  command_line::add_arg(desc_params, arg_daemon_port);
  command_line::add_arg(desc_params, arg_command);
  command_line::add_arg(desc_params, arg_log_level);
  command_line::add_arg(desc_params, arg_testnet);
  Tools::wallet_rpc_server::init_options(desc_params);

  po::positional_options_description positional_options;
  positional_options.add(arg_command.name, -1);

  po::options_description desc_all;
  desc_all.add(desc_general).add(desc_params);

  Logging::LoggerManager logManager;
  Logging::LoggerRef logger(logManager, "testnetwallet");
  System::Dispatcher dispatcher;

  po::variables_map vm;

  cn::client_helper m_chelper;

  bool r = command_line::handle_error_helper(desc_all, [&]()
  {
    po::store(command_line::parse_command_line(argc, argv, desc_general, true), vm);

    if (command_line::get_arg(vm, command_line::arg_help))
    {
      std::cout << "Testnet Wallet " << PROJECT_RELEASE_VERSION << std::endl;
      std::cout << desc_all << std::endl;

      return false;
    }
    else if (command_line::get_arg(vm, command_line::arg_version))
    {
      std::cout << "Testnet Wallet " << PROJECT_RELEASE_VERSION << std::endl;
      return false;
    }

    auto parser = po::command_line_parser(argc, argv).options(desc_params).positional(positional_options);
    po::store(parser.run(), vm);
    po::notify(vm);
    return true;
  });

  if (!r)
    return 1;

  //set up logging options
  Level logLevel = INFO;

  if (command_line::has_arg(vm, arg_log_level))
    logLevel = static_cast<Level>(command_line::get_arg(vm, arg_log_level));

  logManager.configure(m_chelper.buildLoggerConfiguration(logLevel, Common::ReplaceExtenstion(argv[0], ".log")));

  logger(INFO, BRIGHT_YELLOW) << "Testnet Wallet " << PROJECT_RELEASE_VERSION;

  // NOTE: Testnet wallet uses testnet=true always (not configurable via --testnet flag)
  bool testnet = true;
  logger(INFO, MAGENTA) << "/!\\ Running in testnet mode /!\\";

  cn::Currency currency = cn::CurrencyBuilder(logManager).
    testnet(testnet).currency();

  if (command_line::has_arg(vm, Tools::wallet_rpc_server::arg_rpc_bind_port))
  {
    //runs wallet with rpc interface
    logger(ERROR, BRIGHT_RED) << "RPC server not supported in testnet wallet CLI";
    return 1;
  }

  // Create testnet wallet (extends simple_wallet with testnet-specific commands)
  cn::testnet_wallet wallet(dispatcher, currency, logManager);

  if (!wallet.init(vm))
    return 1;

  return wallet.run() ? 0 : 1;
}
