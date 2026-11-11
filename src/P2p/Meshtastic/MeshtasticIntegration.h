// Copyright (c) 2017-2026 Fuego Developers
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

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace FuegoMeshtastic {

// Simple interface for Meshtastic integration
class MeshtasticIntegration {
public:
  MeshtasticIntegration();
  ~MeshtasticIntegration();

  // Initialize Meshtastic connection
  bool initialize(const std::string& devicePath);

  // Send a message via Meshtastic
  bool sendMessage(const std::string& message, uint32_t channel = 0);

  // Receive a message from Meshtastic (non-blocking)
  bool receiveMessage(std::string& message, uint32_t& channel);

  // Check if Meshtastic is connected
  bool isConnected() const;

private:
  // Implementation details
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};

} // namespace FuegoMeshtastic
