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

#include <list>
#include "P2pProtocolDefinitions.h"

namespace CryptoNote {

class P2pContext;

class IP2pNodeInternal {
public:
  virtual const CORE_SYNC_DATA& getGenesisPayload() const = 0;
  virtual std::list<PeerlistEntry> getLocalPeerList() const = 0;
  virtual basic_node_data getNodeData() const = 0;
  virtual PeerIdType getPeerId() const = 0;

  virtual void handleNodeData(const basic_node_data& node, P2pContext& ctx) = 0;
  virtual bool handleRemotePeerList(const std::list<PeerlistEntry>& peerlist, time_t local_time) = 0;
  virtual void tryPing(P2pContext& ctx) = 0;
};

}
