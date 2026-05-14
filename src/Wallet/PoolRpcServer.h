// Copyright (c) 2017-2022 Fuego Developers
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

#pragma  once

#include <future>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include "WalletRpcServerCommandsDefinitions.h"
#include "WalletLegacy/WalletLegacy.h"
#include "Common/CommandLine.h"
#include "Rpc/HttpServer.h"

#include <Logging/LoggerRef.h>

namespace Tools
{
  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  class pool_rpc_server : CryptoNote::HttpServer
  {
  public:

    pool_rpc_server(
      System::Dispatcher& dispatcher,
      Logging::ILogger& log,
      CryptoNote::IWalletLegacy &w,
      CryptoNote::INode &n,
      CryptoNote::Currency& currency,
      const std::string& walletFilename);


    static void init_options(boost::program_options::options_description& desc);
    bool init(const boost::program_options::variables_map& vm);

    bool run();
    void send_stop_signal();

    static const command_line::arg_descriptor<uint16_t> arg_rpc_bind_port;
    static const command_line::arg_descriptor<std::string> arg_rpc_bind_ip;
    static const command_line::arg_descriptor<std::string> arg_rpc_user;
    static const command_line::arg_descriptor<std::string> arg_rpc_password;

  private:

    virtual void processRequest(const CryptoNote::HttpRequest& request, CryptoNote::HttpResponse& response) override;

    //json_rpc
    bool on_getbalance(const wallet_rpc::COMMAND_RPC_GET_BALANCE::request& req, wallet_rpc::COMMAND_RPC_GET_BALANCE::response& res);
    bool on_create_integrated(const wallet_rpc::COMMAND_RPC_CREATE_INTEGRATED::request& req, wallet_rpc::COMMAND_RPC_CREATE_INTEGRATED::response& res);
    bool on_transfer(const wallet_rpc::COMMAND_RPC_TRANSFER::request& req, wallet_rpc::COMMAND_RPC_TRANSFER::response& res);
    bool on_store(const wallet_rpc::COMMAND_RPC_STORE::request& req, wallet_rpc::COMMAND_RPC_STORE::response& res);
    bool on_get_messages(const wallet_rpc::COMMAND_RPC_GET_MESSAGES::request& req, wallet_rpc::COMMAND_RPC_GET_MESSAGES::response& res);
    bool on_get_payments(const wallet_rpc::COMMAND_RPC_GET_PAYMENTS::request& req, wallet_rpc::COMMAND_RPC_GET_PAYMENTS::response& res);
    bool on_get_transfers(const wallet_rpc::COMMAND_RPC_GET_TRANSFERS::request& req, wallet_rpc::COMMAND_RPC_GET_TRANSFERS::response& res);
    bool on_get_height(const wallet_rpc::COMMAND_RPC_GET_HEIGHT::request& req, wallet_rpc::COMMAND_RPC_GET_HEIGHT::response& res);
    bool on_get_outputs(const wallet_rpc::COMMAND_RPC_GET_OUTPUTS::request& req, wallet_rpc::COMMAND_RPC_GET_OUTPUTS::response& res);
    bool on_optimize(const wallet_rpc::COMMAND_RPC_OPTIMIZE::request& req, wallet_rpc::COMMAND_RPC_OPTIMIZE::response& res);
    bool on_reset(const wallet_rpc::COMMAND_RPC_RESET::request& req, wallet_rpc::COMMAND_RPC_RESET::response& res);

    bool handle_command_line(const boost::program_options::variables_map& vm);

    Logging::LoggerRef logger;
    CryptoNote::IWalletLegacy& m_wallet;
    CryptoNote::INode& m_node;
    uint16_t m_port;
    std::string m_bind_ip;
    std::string m_rpcUser;
    std::string m_rpcPassword;
    CryptoNote::Currency& m_currency;
    const std::string m_walletFilename;

    System::Dispatcher& m_dispatcher;
    System::Event m_stopComplete;
  };
}
