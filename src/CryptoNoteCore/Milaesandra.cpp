// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "Milaesandra.h"
#include "CryptoNoteConfig.h"
#include "Currency.h"

namespace CryptoNote {

Milaesandra::Milaesandra() = default;

bool Milaesandra::isActive(const Currency& currency) const {
  return parameters::MILAESANDRA_SIMULATE_FEES && currency.isTestnet();
}

uint64_t Milaesandra::simulateEpochFees(const Currency& currency) const {
  if (!isActive(currency))
    return 0;

  // Base fee amount: simulates healthy swap activity
  // Scale: ~20K XFG/day × 5 days = 100K XFG × 2% fee = 2,000 XFG/epoch
  // Use atomic units (COIN = 10,000,000)
  uint64_t baseFees = parameters::MILAESANDRA_BASE_FEES_ATOMIC;

  // Add ±20% random variation per epoch
  m_nonce++;
  uint64_t variation = ((m_nonce * 1103515245 + 12345) % 4000) - 2000;
  if (variation > baseFees / 5)
    variation = baseFees / 5;

  // Apply deviation scale factor for stress testing
  uint64_t fees = baseFees + variation;
  fees = (uint64_t)(fees * parameters::MILAESANDRA_FEE_MULTIPLIER / 100);

  return fees;
}

uint64_t Milaesandra::simulateXfgPrice(const Currency& currency,
                                        uint32_t blockHeight) const {
  if (!isActive(currency))
    return 0;

  // XFG price simulation: grows from initial value over time
  // Price in cents (scaled by VALUE_SCALE=100)
  // Starts at MILAESANDRA_INITIAL_XFG_PRICE and can grow to test activation

  uint64_t basePrice = parameters::MILAESANDRA_INITIAL_XFG_PRICE;

  // Activate growth mode if configured
  if (parameters::MILAESANDRA_GROWING_PRICE) {
    // Price grows ~10% per 500 epochs
    uint64_t growthEpochs = blockHeight / 500;
    for (uint64_t i = 0; i < growthEpochs && basePrice < 100000; ++i)
      basePrice = (basePrice * 110) / 100; // +10% each tier
  }

  return basePrice;
}

} // namespace CryptoNote
