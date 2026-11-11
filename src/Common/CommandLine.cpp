// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "CommandLine.h"

namespace command_line
{
  const arg_descriptor<bool> arg_help = {"help", "Produce help message"};
  const arg_descriptor<bool> arg_version = {"version", "Show version information"};
  const arg_descriptor<std::string> arg_data_dir = {"data-dir", "Specify data directory"};
}
