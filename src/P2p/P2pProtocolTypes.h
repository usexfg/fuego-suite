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

#include <string.h>
#include <tuple>
#include <boost/uuid/uuid.hpp>
#include "../Common/StringTools.h"

namespace CryptoNote
{
  typedef boost::uuids::uuid uuid;
  typedef boost::uuids::uuid net_connection_id;
  typedef uint64_t PeerIdType;

#pragma pack (push, 1)

  struct NetworkAddress
  {
    uint32_t ip;
    uint32_t port;
  };

  struct PeerlistEntry
  {
    NetworkAddress adr;
    PeerIdType id;
    uint64_t last_seen;
  };

  struct AnchorPeerlistEntry
  {
    NetworkAddress adr;
    PeerIdType id;
    int64_t first_seen;
  };

  struct connection_entry
  {
    NetworkAddress adr;
    PeerIdType id;
    bool is_income;
  };

#pragma pack(pop)

  inline bool operator < (const NetworkAddress& a, const NetworkAddress& b) {
    return std::tie(a.ip, a.port) < std::tie(b.ip, b.port);
  }

  inline bool operator == (const NetworkAddress& a, const NetworkAddress& b) {
    return memcmp(&a, &b, sizeof(a)) == 0;
  }

  inline std::ostream& operator << (std::ostream& s, const NetworkAddress& na) {
    return s << Common::ipAddressToString(na.ip) << ":" << std::to_string(na.port);
  }

  inline uint32_t hostToNetwork(uint32_t n) {
    return (n << 24) | (n & 0xff00) << 8 | (n & 0xff0000) >> 8 | (n >> 24);
  }

  inline uint32_t networkToHost(uint32_t n) {
    return hostToNetwork(n); // the same
  }

}
