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

#include "CryptoTypes.h"

#include <System/ContextGroup.h>
#include <System/Dispatcher.h>
#include <System/Event.h>

#include "Logging/LoggerRef.h"

class BlockchainMonitor {
public:
  BlockchainMonitor(System::Dispatcher& dispatcher, const std::string& daemonHost, uint16_t daemonPort, size_t pollingInterval, Logging::ILogger& logger);

  void waitBlockchainUpdate();
  void stop();
private:
  System::Dispatcher& m_dispatcher;
  std::string m_daemonHost;
  uint16_t m_daemonPort;
  size_t m_pollingInterval;
  bool m_stopped;
  System::Event m_httpEvent;
  System::ContextGroup m_sleepingContext;

  Logging::LoggerRef m_logger;

  Crypto::Hash requestLastBlockHash();
};
