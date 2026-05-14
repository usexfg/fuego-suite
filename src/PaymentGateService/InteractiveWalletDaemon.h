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

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <memory>

#include "PaymentGateService.h"
#include "Logging/LoggerRef.h"
#include "System/Dispatcher.h"
#include "System/Event.h"

namespace PaymentService {

class InteractiveWalletDaemon {
public:
  InteractiveWalletDaemon(PaymentGateService& paymentService);
  ~InteractiveWalletDaemon();

  bool init();
  void run();
  void stop();

private:
  void printHelp();
  void printStatus();
  void printBalances();
  void printAddresses();
  void handleCommand(const std::string& command);
  void startRpcServer();
  void interactiveLoop();
  
  PaymentGateService& m_paymentService;
  std::unique_ptr<std::thread> m_rpcThread;
  std::atomic<bool> m_running;
  System::Dispatcher m_dispatcher;
  System::Event m_stopEvent;
  
  Logging::LoggerRef logger;
};

} // namespace PaymentService