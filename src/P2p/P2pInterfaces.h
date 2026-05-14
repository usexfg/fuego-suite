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

#pragma once

#include <cstdint>
#include <memory>

#include <CryptoNote.h>

namespace CryptoNote {

struct P2pMessage {
  uint32_t type;
  BinaryArray data;
};

class IP2pConnection {
public:
  virtual ~IP2pConnection();
  virtual void read(P2pMessage &message) = 0;
  virtual void write(const P2pMessage &message) = 0;
  virtual void ban() = 0;
  virtual void stop() = 0;
};

class IP2pNode {
public:
  virtual std::unique_ptr<IP2pConnection> receiveConnection() = 0;
  virtual void stop() = 0;
};

}
