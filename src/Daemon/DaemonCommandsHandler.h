// Copyright (c) 2017-2025 Elderfire Privacy Council
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

#pragma once

#include <boost/format.hpp>
#include "Common/ConsoleHandler.h"
#include "CryptoNoteProtocol/ICryptoNoteProtocolQuery.h"
#include <Logging/LoggerRef.h>
#include <Logging/LoggerManager.h>

namespace CryptoNote {
class core;
class Currency;
class NodeServer;
class ICryptoNoteProtocolQuery;
}

class DaemonCommandsHandler
{
public:
  DaemonCommandsHandler(CryptoNote::core& core, CryptoNote::NodeServer& srv, Logging::LoggerManager& log, const CryptoNote::ICryptoNoteProtocolQuery& protocol);

  bool start_handling() {
    m_consoleHandler.start();
    return true;
  }

  void stop_handling() {
    m_consoleHandler.stop();
  }

private:

  Common::ConsoleHandler m_consoleHandler;
  CryptoNote::core& m_core;
  CryptoNote::NodeServer& m_srv;
  Logging::LoggerRef logger;
  Logging::LoggerManager& m_logManager;
  const CryptoNote::ICryptoNoteProtocolQuery& protocolQuery;
  std::string get_commands_str();
  std::string get_mining_speed(uint32_t hr);
  float get_sync_percentage(uint64_t height, uint64_t target_height);
  bool print_block_by_height(uint32_t height);
  bool print_block_by_hash(const std::string& arg);
  uint64_t calculatePercent(const CryptoNote::Currency& currency, uint64_t value, uint64_t total);

  bool exit(const std::vector<std::string>& args);
  bool help(const std::vector<std::string>& args);
  bool print_pl(const std::vector<std::string>& args);
  bool show_hr(const std::vector<std::string>& args);
  bool hide_hr(const std::vector<std::string>& args);
  bool rollbackchainto(uint32_t height);  
  bool rollback_chain(const std::vector<std::string>& args);  
  bool print_bc_outs(const std::vector<std::string>& args);
  bool print_cn(const std::vector<std::string>& args);
  bool print_bc(const std::vector<std::string>& args);
  bool print_bci(const std::vector<std::string>& args);
  bool print_height(const std::vector<std::string>& args);
  bool set_log(const std::vector<std::string>& args);
  bool print_block(const std::vector<std::string>& args);
  bool print_tx(const std::vector<std::string>& args);
  bool print_pool(const std::vector<std::string>& args);
  bool print_pool_sh(const std::vector<std::string>& args);
  bool status(const std::vector<std::string>& args);
  bool save(const std::vector<std::string> &args);

  bool start_mining(const std::vector<std::string>& args);
  bool stop_mining(const std::vector<std::string>& args);
  bool print_ban(const std::vector<std::string>& args);
  bool ban(const std::vector<std::string>& args);
  bool unban(const std::vector<std::string>& args);
};
