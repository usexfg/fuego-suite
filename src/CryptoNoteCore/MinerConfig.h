// Copyright (c) 2017-2026 Fuego Developers
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

#include <cstdint>
#include <string>

#include <boost/program_options.hpp>

namespace CryptoNote {

class MinerConfig {
public:
  MinerConfig();

  static void initOptions(boost::program_options::options_description& desc);
  void init(const boost::program_options::variables_map& options);

  // ── Core miner settings ──────────────────────────────────────────────────
  std::string extraMessages;
  std::string startMining;
  uint32_t    miningThreads;

  // ── Fuego Cost Index (FCI) basket prices ─────────────────────────────────
  // All stored as integer microUSD (µUSD = 0.000001 USD).
  // FX conversion happens at init() via getCurrencyUsdRate(); consensus only
  // ever sees µUSD integers — no floats cross into the daemon.
  //
  // present_mask bits:
  //   bit 0 = power   bit 1 = milk   bit 2 = bread
  //   bit 3 = eggs    bit 4 = gas
  //
  // A value of 0 in present_mask means no FCI vote is included in coinbase.
  // Phase 1: votes are stored on-chain but no DIGM payout is issued.
  //          Block height + spend-key-hash recorded for auditable back-pay.
  uint8_t  fciPresentMask;   // bitmask of which commodities are configured
  uint32_t fciPower;         // µUSD per kWh   (e.g. $0.12 → 120000)
  uint32_t fciMilk;          // µUSD per gallon whole milk
  uint32_t fciGas;           // µUSD per gallon petrol
  uint32_t fciBread;         // µUSD per kg loaf
  uint32_t fciEggs;          // µUSD per dozen large eggs

  // ── Currency helpers ─────────────────────────────────────────────────────
  // Miner may supply prices in any supported fiat; init() multiplies by
  // fciUsdRate to produce the µUSD integer stored above.
  std::string fciCurrency;   // ISO-4217 code (default: "USD")
  double      fciUsdRate;    // local-currency → USD rate (default: 1.0)

  // ── Auto-fetch config ────────────────────────────────────────────────────
  // Path to optional fci_oracle.json produced by the auto-fetch script.
  // Daemon hot-reloads this file at each epoch boundary.
  // If empty, only CLI args are used.
  std::string fciOraclePath;

  // ── Staleness warning ────────────────────────────────────────────────────
  // Daemon logs a YELLOW warning if fciOraclePath mtime > fciStaleWarnDays old.
  uint32_t    fciStaleWarnDays;  // default: 30

  // ── Future DIGM Paradio vote ─────────────────────────────────────────────
  // Miner-supplied UTF-8 song title (max 128 bytes on wire).
  // Tag 0x39 — independent of FCI; both can coexist in one coinbase.
  // Empty string means no Paradio vote included this block.
  std::string paradioVote;
};

} // namespace CryptoNote
