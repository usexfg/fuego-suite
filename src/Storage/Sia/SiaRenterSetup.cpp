// Copyright (c) 2017-2026 Fuego Developers

#include "SiaRenterSetup.h"

namespace Fuego {
namespace Storage {

SiaRenterReady checkRenterReady(const SiaRenterConfig& cfg) {
  SiaRenterReady r;
  SiaStorageClient client(cfg);
  if (!client.ping(r.status)) {
    r.ok = false;
    return r;
  }
  client.getWalletBalanceHastings(r.balanceHastings);
  if (cfg.minBalanceHastings > 0 && r.balanceHastings < cfg.minBalanceHastings) {
    r.ok = false;
    r.status += "; balance " + std::to_string(r.balanceHastings) +
                " < min " + std::to_string(cfg.minBalanceHastings);
    return r;
  }
  r.ok = true;
  return r;
}

} // namespace Storage
} // namespace Fuego
