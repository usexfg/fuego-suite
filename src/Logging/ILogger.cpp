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

#include "ILogger.h"

namespace Logging {

const std::string BLUE = "\x1F""BLUE\x1F";
const std::string GREEN = "\x1F""GREEN\x1F";
const std::string RED = "\x1F""RED\x1F";
const std::string YELLOW = "\x1F""YELLOW\x1F";
const std::string WHITE = "\x1F""WHITE\x1F";
const std::string CYAN = "\x1F""CYAN\x1F";
const std::string MAGENTA = "\x1F""MAGENTA\x1F";
const std::string BRIGHT_BLUE = "\x1F""BRIGHT_BLUE\x1F";
const std::string BRIGHT_GREEN = "\x1F""BRIGHT_GREEN\x1F";
const std::string BRIGHT_RED = "\x1F""BRIGHT_RED\x1F";
const std::string BRIGHT_YELLOW = "\x1F""BRIGHT_YELLOW\x1F";
const std::string BRIGHT_WHITE = "\x1F""BRIGHT_WHITE\x1F";
const std::string BRIGHT_CYAN = "\x1F""BRIGHT_CYAN\x1F";
const std::string BRIGHT_MAGENTA = "\x1F""BRIGHT_MAGENTA\x1F";
const std::string DEFAULT = "\x1F""DEFAULT\x1F";

const char ILogger::COLOR_DELIMETER = '\x1F';

const std::array<std::string, 6> ILogger::LEVEL_NAMES = {
  {"FATAL",
  "ERROR",
  "WARNING",
  "INFO",
  "DEBUG",
  "TRACE"}
};

}
