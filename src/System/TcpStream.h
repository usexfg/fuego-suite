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

#pragma once

#include <array>
#include <cstdint>
#include <streambuf>

namespace System {

class TcpConnection;

class TcpStreambuf : public std::streambuf {
public:
  explicit TcpStreambuf(TcpConnection& connection);
  TcpStreambuf(const TcpStreambuf&) = delete;
  ~TcpStreambuf();
  TcpStreambuf& operator=(const TcpStreambuf&) = delete;

private:
  TcpConnection& connection;
  std::array<char, 4096> readBuf;
  std::array<uint8_t, 1024> writeBuf;

  std::streambuf::int_type overflow(std::streambuf::int_type ch) override;
  int sync() override;
  std::streambuf::int_type underflow() override;
  bool dumpBuffer(bool finalize);
};

}
