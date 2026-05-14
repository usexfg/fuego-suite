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

#include "TcpStream.h"
#include "TcpConnection.h"

namespace System {

TcpStreambuf::TcpStreambuf(TcpConnection& connection) : connection(connection) {
  setg(&readBuf.front(), &readBuf.front(), &readBuf.front());
  setp(reinterpret_cast<char*>(&writeBuf.front()), reinterpret_cast<char *>(&writeBuf.front() + writeBuf.max_size()));
}

TcpStreambuf::~TcpStreambuf() {
  dumpBuffer(true);
}

std::streambuf::int_type TcpStreambuf::overflow(std::streambuf::int_type ch) {
  if (ch == traits_type::eof()) {
    return traits_type::eof();
  }

  if (pptr() == epptr()) {
    if (!dumpBuffer(false)) {
      return traits_type::eof();
    }
  }

  *pptr() = static_cast<char>(ch);
  pbump(1);
  return ch;
}

int TcpStreambuf::sync() {
  return dumpBuffer(true) ? 0 : -1;
}

std::streambuf::int_type TcpStreambuf::underflow() {
  if (gptr() < egptr()) {
    return traits_type::to_int_type(*gptr());
  }

  size_t bytesRead;
  try {
    bytesRead = connection.read(reinterpret_cast<uint8_t*>(&readBuf.front()), readBuf.max_size());
  } catch (std::exception&) {
    return traits_type::eof();
  }

  if (bytesRead == 0) {
    return traits_type::eof();
  }

  setg(&readBuf.front(), &readBuf.front(), &readBuf.front() + bytesRead);
  return traits_type::to_int_type(*gptr());
}

bool TcpStreambuf::dumpBuffer(bool finalize) {
  try {
    size_t count = pptr() - pbase();
    if(count == 0) {
      return true;
    }

    size_t transferred = connection.write(&writeBuf.front(), count);
    if(transferred == count) {
      pbump(-static_cast<int>(count));
    } else {
      if(!finalize) {
        size_t front = 0;
        for (size_t pos = transferred; pos < count; ++pos, ++front) {
          writeBuf[front] = writeBuf[pos];
        }

        pbump(-static_cast<int>(transferred));
      } else {
        size_t offset = transferred;
        while( offset != count) {
          offset += connection.write(&writeBuf.front() + offset, count - offset);
        }

        pbump(-static_cast<int>(count));
      }
    }
  } catch (std::exception&) {
    return false;
  }

  return true;
}

}
