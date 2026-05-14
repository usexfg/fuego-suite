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

#include "MinerConfig.h"

#include "Common/CommandLine.h"

namespace CryptoNote {

namespace {
const command_line::arg_descriptor<std::string> arg_extra_messages =  {"extra-messages-file", "Specify file for extra messages to include into coinbase transactions", "", true};
const command_line::arg_descriptor<std::string> arg_start_mining =    {"start-mining", "Specify wallet address to mining for", "", true};
const command_line::arg_descriptor<uint32_t>    arg_mining_threads =  {"mining-threads", "Specify mining threads count", 0, true};
}

MinerConfig::MinerConfig() {
  miningThreads = 0;
}

void MinerConfig::initOptions(boost::program_options::options_description& desc) {
  command_line::add_arg(desc, arg_extra_messages);
  command_line::add_arg(desc, arg_start_mining);
  command_line::add_arg(desc, arg_mining_threads);
}

void MinerConfig::init(const boost::program_options::variables_map& options) {
  if(command_line::has_arg(options, arg_extra_messages)) {
    extraMessages = command_line::get_arg(options, arg_extra_messages);
  }

  if (command_line::has_arg(options, arg_start_mining)) {
    startMining = command_line::get_arg(options, arg_start_mining);
  }

  if (command_line::has_arg(options, arg_mining_threads)) {
    miningThreads = command_line::get_arg(options, arg_mining_threads);
  }
}

} //namespace CryptoNote
