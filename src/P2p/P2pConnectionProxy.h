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

#include <queue>

#include "IP2pNodeInternal.h"
#include "LevinProtocol.h"
#include "P2pContextOwner.h"
#include "P2pInterfaces.h"

namespace CryptoNote {

class P2pContext;
class P2pNode;

class P2pConnectionProxy : public IP2pConnection {
public:

  P2pConnectionProxy(P2pContextOwner&& ctx, IP2pNodeInternal& node);
  ~P2pConnectionProxy();

  bool processIncomingHandshake();

  // IP2pConnection
  virtual void read(P2pMessage& message) override;
  virtual void write(const P2pMessage &message) override;
  virtual void ban() override;
  virtual void stop() override;

private:

  void writeHandshake(const P2pMessage &message);
  void handleHandshakeRequest(const LevinProtocol::Command& cmd);
  void handleHandshakeResponse(const LevinProtocol::Command& cmd, P2pMessage& message);
  void handleTimedSync(const LevinProtocol::Command& cmd);

  std::queue<P2pMessage> m_readQueue;
  P2pContextOwner m_contextOwner;
  P2pContext& m_context;
  IP2pNodeInternal& m_node;
};

}
