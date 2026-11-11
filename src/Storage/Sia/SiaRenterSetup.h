// Copyright (c) 2017-2026 Fuego Developers
//
// Infrastructure readiness for XFG ↔ Sia storage (Phase 7E light).
// Does not purchase SC or form contracts — checks renterd + optional balance.

#pragma once

#include "SiaConfig.h"
#include "SiaStorageClient.h"
#include <string>

namespace Fuego {
namespace Storage {

struct SiaRenterReady {
  bool ok = false;
  std::string status;
  uint64_t balanceHastings = 0;
};

// Connect to renterd, ping bus/worker, optional min balance.
SiaRenterReady checkRenterReady(const SiaRenterConfig& cfg);

} // namespace Storage
} // namespace Fuego
