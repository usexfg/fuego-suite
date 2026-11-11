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

#include "InteractiveWalletDaemon.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/Account.h"
#include "crypto/hash.h"

namespace PaymentService {

InteractiveWalletDaemon::InteractiveWalletDaemon(PaymentGateService& paymentService) : 
  m_paymentService(paymentService),
  m_running(false),
  m_stopEvent(m_dispatcher),
  logger(m_paymentService.getLogger(), "InteractiveWalletDaemon") {
}

InteractiveWalletDaemon::~InteractiveWalletDaemon() {
  stop();
}

bool InteractiveWalletDaemon::init() {
  // Initialization is handled by PaymentGateService
  return true;
}

void InteractiveWalletDaemon::run() {
  logger(Logging::INFO) << "Starting Interactive Wallet Daemon";
  
  // Start the RPC server in a separate thread
  startRpcServer();
  
  m_running = true;
  
  // Start interactive loop
  interactiveLoop();
}

void InteractiveWalletDaemon::stop() {
  if (m_running) {
    m_running = false;
    m_stopEvent.set();
    
    if (m_rpcThread && m_rpcThread->joinable()) {
      m_rpcThread->join();
    }
  }
}

void InteractiveWalletDaemon::printHelp() {
  std::cout << "\n=== Fuego Interactive Wallet Daemon ===" << std::endl;
  std::cout << "Available commands:" << std::endl;
  std::cout << "  help, h          - Show this help message" << std::endl;
  std::cout << "  status, st       - Show wallet synchronization status" << std::endl;
  std::cout << "  balance, bal     - Show wallet balances" << std::endl;
  std::cout << "  address, addr    - Show wallet addresses" << std::endl;
  std::cout << "  transfer, tx     - Send a transfer (not implemented in this example)" << std::endl;
  std::cout << "  deposits, dep    - Show deposit information (Fuego specific)" << std::endl;
  std::cout << "  quit, exit, q    - Stop the daemon and exit" << std::endl;
  std::cout << std::endl;
}

void InteractiveWalletDaemon::printStatus() {
  std::cout << "Wallet Status: " << (m_running ? "RUNNING" : "STOPPED") << std::endl;
  // TODO: Add actual synchronization status when wallet service provides it
  std::cout << "RPC Server: Listening on port " << m_paymentService.getConfig().gateConfiguration.bindPort << std::endl;
}

void InteractiveWalletDaemon::printBalances() {
  // TODO: Connect to wallet service to get actual balances
  std::cout << "=== Wallet Balances ===" << std::endl;
  std::cout << "Note: Balance information not yet implemented in this example" << std::endl;
  std::cout << "Actual balance: [Connect to RPC to get real values]" << std::endl;
  std::cout << "Pending balance: [Connect to RPC to get real values]" << std::endl;
  std::cout << "Locked deposits: [Connect to RPC to get real values]" << std::endl;
  std::cout << "Unlocked deposits: [Connect to RPC to get real values]" << std::endl;
}

void InteractiveWalletDaemon::printAddresses() {
  std::cout << "=== Wallet Addresses ===" << std::endl;
  std::cout << "Note: Address information not yet implemented in this example" << std::endl;
  std::cout << "Wallet file: " << m_paymentService.getConfig().gateConfiguration.containerFile << std::endl;
  // TODO: Connect to wallet service to get actual addresses
}

void InteractiveWalletDaemon::startRpcServer() {
  m_rpcThread = std::make_unique<std::thread>([this]() {
    try {
      // This would run the actual payment service
      // m_paymentService.run(); // This blocks, so we need a different approach
      std::cout << "RPC Server thread started (placeholder)" << std::endl;
    } catch (std::exception& e) {
      logger(Logging::ERROR) << "RPC Server error: " << e.what();
    }
  });
}

void InteractiveWalletDaemon::interactiveLoop() {
  printHelp();
  
  std::string input;
  
  while (m_running) {
    std::cout << "\nwalletd> ";
    std::getline(std::cin, input);
    
    if (input.empty()) {
      continue;
    }
    
    // Trim whitespace
    input.erase(0, input.find_first_not_of(" \t"));
    input.erase(input.find_last_not_of(" \t") + 1);
    
    if (input.empty()) {
      continue;
    }
    
    handleCommand(input);
  }
}

void InteractiveWalletDaemon::handleCommand(const std::string& command) {
  std::string cmd = command;
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
  
  if (cmd == "help" || cmd == "h") {
    printHelp();
  } else if (cmd == "status" || cmd == "st") {
    printStatus();
  } else if (cmd == "balance" || cmd == "bal") {
    printBalances();
  } else if (cmd == "address" || cmd == "addr") {
    printAddresses();
  } else if (cmd == "deposits" || cmd == "dep") {
    std::cout << "=== Fuego Deposit Information ===" << std::endl;
    std::cout << "Fuego supports burn deposits for banking functions." << std::endl;
    std::cout << "Use RPC methods to create and manage deposits." << std::endl;
  } else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
    std::cout << "Stopping Interactive Wallet Daemon..." << std::endl;
    stop();
  } else {
    std::cout << "Unknown command: " << command << std::endl;
    std::cout << "Type 'help' for available commands." << std::endl;
  }
}

} // namespace PaymentService