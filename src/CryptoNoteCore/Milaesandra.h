// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#pragma once

#include <cstdint>

namespace CryptoNote {

class Currency;
class Blockchain;

// Milaesandra — testnet atomic swap fee simulator
// ==============================================
// Generates simulated swap fees so the full protocol machinery
// (CD yield, treasury, rebalancer, PI controller, oracle activation)
// can be tested on testnet without real cross-chain activity.
//
// Three levers of the system:
//   1. CD yield routing — 40% → treasury when pool lopsided
//   2. PI controller — negative feedback on mint rate
//   3. Rebalancer — single-sided LP for pool defense
//
// Testnet only. Does nothing on mainnet.

class Milaesandra {
public:
  Milaesandra();

  // Returns simulated swap fees (XFG atomic units) for this epoch.
  // Returns 0 if simulation is disabled or not on testnet.
  uint64_t simulateEpochFees(const Currency& currency) const;

  // Returns simulated XFG price for oracle activation testing.
  // Returns 0 if simulation is disabled or not on testnet.
  // Simulates a price trajectory: starts low, grows over time.
  uint64_t simulateXfgPrice(const Currency& currency, uint32_t blockHeight) const;

  // Returns true if fee simulation is active at current block
  bool isActive(const Currency& currency, uint32_t blockHeight = 0) const;

private:
  // Internal randomness for fee variation
  mutable uint64_t m_nonce = 0;
};

} // namespace CryptoNote
