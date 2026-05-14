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

#include "MeshtasticIntegration.h"
#include <cstring>
#include <unistd.h>

namespace FuegoMeshtastic {

struct MeshtasticIntegration::Impl {
  bool connected = false;
  int fd = -1;
};

MeshtasticIntegration::MeshtasticIntegration() : pImpl(std::make_unique<Impl>()) {}

MeshtasticIntegration::~MeshtasticIntegration() {
  if (pImpl->fd >= 0) {
    close(pImpl->fd);
  }
}

bool MeshtasticIntegration::initialize(const std::string& devicePath) {
  // For now, just return true to indicate initialization success
  // In a real implementation, this would open the serial device
  pImpl->connected = true;
  return true;
}

bool MeshtasticIntegration::sendMessage(const std::string& message, uint32_t channel) {
  // For now, just return true to indicate success
  // In a real implementation, this would send via Meshtastic
  (void)message;
  (void)channel;
  return pImpl->connected;
}

bool MeshtasticIntegration::receiveMessage(std::string& message, uint32_t& channel) {
  // For now, just return false to indicate no message available
  // In a real implementation, this would receive via Meshtastic
  (void)message;
  (void)channel;
  return false;
}

bool MeshtasticIntegration::isConnected() const {
  return pImpl->connected;
}

} // namespace FuegoMeshtastic
