// Copyright (c) 2017-2025 Elderfire Privacy Council
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

#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "BlockingQueue.h"
#include "ConsoleTools.h"

namespace Common {

class AsyncConsoleReader {

public:

  AsyncConsoleReader();
  ~AsyncConsoleReader();

  void start();
  bool getline(std::string& line);
  void stop();
  bool stopped() const;

private:

  void consoleThread();
  bool waitInput();

  std::atomic<bool> m_stop;
  std::thread m_thread;
  BlockingQueue<std::string> m_queue;
};


class ConsoleHandler {
public:

  ~ConsoleHandler();

  typedef std::function<bool(const std::vector<std::string> &)> ConsoleCommandHandler;

  std::string getUsage() const;
  void setHandler(const std::string& command, const ConsoleCommandHandler& handler, const std::string& usage = "");
  void requestStop();
  bool runCommand(const std::vector<std::string>& cmdAndArgs);

  void start(bool startThread = true, const std::string& prompt = "", Console::Color promptColor = Console::Color::Default);
  void stop();
  void wait(); 

private:

  typedef std::map<std::string, std::pair<ConsoleCommandHandler, std::string>> CommandHandlersMap;

  virtual void handleCommand(const std::string& cmd);

  void handlerThread();

  std::thread m_thread;
  std::string m_prompt;
  Console::Color m_promptColor = Console::Color::Default;
  CommandHandlersMap m_handlers;
  AsyncConsoleReader m_consoleReader;
};

}
