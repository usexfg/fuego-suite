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

#include "MinerConfig.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

#include "Common/CommandLine.h"
#include "Common/StringTools.h"

namespace CryptoNote {

namespace {
// ── Core miner args ──────────────────────────────────────────────────────
const command_line::arg_descriptor<std::string> arg_extra_messages = {
  "extra-messages-file", "Specify file for extra messages to include into coinbase transactions", "", true};
const command_line::arg_descriptor<std::string> arg_start_mining = {
  "start-mining", "Specify wallet address to mining for", "", true};
const command_line::arg_descriptor<uint32_t> arg_mining_threads = {
  "mining-threads", "Specify mining threads count", 0, true};

// ── FCI basket args ───────────────────────────────────────────────────────
// Prices accepted in the miner's local currency (default USD).
// Displayed units make human entry trivial — no scientific notation needed.
// Examples:
//   --fci-power 0.12          (USD per kWh)
//   --fci-gas   3.50          (USD per gallon)
//   --fci-milk  3.90          (USD per gallon)
//   --fci-bread 2.50          (USD per kg loaf)
//   --fci-eggs  3.00          (USD per dozen)
// With non-USD currency:
//   --fci-currency EUR --fci-usd-rate 1.08 --fci-power 0.29
const command_line::arg_descriptor<double>      arg_fci_power = {
  "fci-power",    "Electricity price per kWh in local currency", 0.0, true};
const command_line::arg_descriptor<double>      arg_fci_milk = {
  "fci-milk",     "Whole milk price per GALLON in local currency", 0.0, true};
const command_line::arg_descriptor<double>      arg_fci_gas = {
  "fci-gas",      "Petrol/gasoline price per GALLON in local currency", 0.0, true};
const command_line::arg_descriptor<double>      arg_fci_bread = {
  "fci-bread",    "Bread price per kg loaf in local currency", 0.0, true};
const command_line::arg_descriptor<double>      arg_fci_eggs = {
  "fci-eggs",     "Eggs price per dozen large in local currency", 0.0, true};
const command_line::arg_descriptor<std::string> arg_fci_currency = {
  "fci-currency", "ISO-4217 currency code for FCI prices (default: USD)", "USD", true};
const command_line::arg_descriptor<double>      arg_fci_usd_rate = {
  "fci-usd-rate", "Exchange rate: 1 unit of fci-currency = N USD (default: 1.0)", 1.0, true};
const command_line::arg_descriptor<std::string> arg_fci_oracle_path = {
  "fci-oracle",   "Path to fci_oracle.json produced by the auto-fetch script", "", true};
const command_line::arg_descriptor<uint32_t>    arg_fci_stale_warn = {
  "fci-stale-warn-days", "Days before daemon warns that oracle data may be stale", 30, true};

// ── Paradio vote arg ──────────────────────────────────────────────────────
// Future DIGM feature: block-level song-title vote for the Paradio radio stream.
// Max 128 bytes UTF-8. Independent of FCI — both may coexist in one coinbase.
const command_line::arg_descriptor<std::string> arg_paradio_vote = {
  "paradio-vote", "Song title to vote for on Paradio (DIGM feature, max 128 chars)", "", true};

// ── FX lookup table ───────────────────────────────────────────────────────
// Returns approximate USD per 1 unit of the given ISO-4217 code.
// Values are compile-time reference rates. Miners should always prefer
// --fci-usd-rate for precision; these exist only as a convenience default.
// Never used by consensus — client-side conversion only.
double getCurrencyUsdRate(const std::string& code) {
  std::string c = code;
  std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch){ return std::toupper(ch); });
  if (c == "USD") return 1.0;
  if (c == "EUR") return 1.08;
  if (c == "GBP") return 1.27;
  if (c == "CAD") return 0.74;
  if (c == "AUD") return 0.65;
  if (c == "JPY") return 0.0066;
  if (c == "CHF") return 1.13;
  if (c == "CNY") return 0.14;
  if (c == "INR") return 0.012;
  if (c == "BRL") return 0.20;
  if (c == "MXN") return 0.059;
  if (c == "KRW") return 0.00075;
  if (c == "SGD") return 0.74;
  if (c == "NOK") return 0.095;
  if (c == "SEK") return 0.096;
  if (c == "DKK") return 0.145;
  if (c == "NZD") return 0.61;
  if (c == "ZAR") return 0.055;
  if (c == "HKD") return 0.128;
  if (c == "TRY") return 0.031;
  // Unknown currency: return 0 so caller can warn the miner.
  return 0.0;
}

// Convert local-currency price to integer microUSD (µUSD = 0.000001 USD).
// Returns 0 on invalid input (negative, zero, or unknown currency).
uint32_t toMicroUsd(double localPrice, double usdRate) {
  if (localPrice <= 0.0 || usdRate <= 0.0) return 0;
  double usd = localPrice * usdRate;
  double raw = std::round(usd * 1000000.0);
  if (raw > static_cast<double>(UINT32_MAX)) return UINT32_MAX; // clamp
  return static_cast<uint32_t>(raw);
}

// Try to read FCI prices from a JSON file written by the auto-fetch script.
// Returns true if any fields were loaded. Overwrites only present fields;
// CLI-explicit prices take priority and are NOT overwritten here.
bool tryLoadOracleJson(const std::string& path, double usdRate,
                       uint8_t& mask,
                       uint32_t& power, uint32_t& milk, uint32_t& gas,
                       uint32_t& bread, uint32_t& eggs,
                       uint32_t staleWarnDays)
{
  if (path.empty()) return false;

  // Staleness check — warn if file hasn't been updated recently.
  struct stat st{};
  if (::stat(path.c_str(), &st) == 0) {
    time_t now = std::time(nullptr);
    double ageDays = difftime(now, st.st_mtime) / 86400.0;
    if (ageDays > static_cast<double>(staleWarnDays)) {
      fprintf(stderr,
        "\033[33m[FCI WARNING] %s was last modified %.0f days ago. "
        "Consider running fci-autofetch or updating manually.\033[0m\n",
        path.c_str(), ageDays);
    }
  }

  // Minimal stdlib-only flat JSON parser.
  // Expects: { "key": <number>, ... }  — no nested objects, no arrays.
  // Safe against malformed input: unknown keys are silently skipped.
  // CLI-explicit prices (already in mask) are never overwritten.
  std::ifstream ifs(path);
  if (!ifs.is_open()) return false;

  std::string content((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
  ifs.close();

  bool loaded = false;
  // Walk the string looking for "key": number pairs.
  const char* p = content.c_str();
  while (*p) {
    // Find opening quote of key.
    const char* qs = std::strchr(p, '"');
    if (!qs) break;
    const char* qe = std::strchr(qs + 1, '"');
    if (!qe) break;
    std::string key(qs + 1, qe);

    // Find colon then the numeric value.
    const char* colon = std::strchr(qe + 1, ':');
    if (!colon) break;
    char* end = nullptr;
    double val = std::strtod(colon + 1, &end);
    if (end == colon + 1) { p = qe + 1; continue; } // not a number

    struct { const char* name; uint8_t bit; uint32_t& out; } fields[] = {
      {"power", 1 << 0, power},
      {"milk",  1 << 1, milk},
      {"bread", 1 << 2, bread},
      {"eggs",  1 << 3, eggs},
      {"gas",   1 << 4, gas},
    };
    for (auto& f : fields) {
      if (key == f.name && !(mask & f.bit)) {
        uint32_t u = toMicroUsd(val, usdRate);
        if (u > 0) { f.out = u; mask |= f.bit; loaded = true; }
        break;
      }
    }
    p = end;
  }
  return loaded;

    return false;
  }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────

MinerConfig::MinerConfig()
  : miningThreads(0)
  , fciPresentMask(0)
  , fciPower(0), fciMilk(0), fciGas(0), fciBread(0), fciEggs(0)
  , fciCurrency("USD"), fciUsdRate(1.0)
  , fciStaleWarnDays(30)
{}

void MinerConfig::initOptions(boost::program_options::options_description& desc) {
  command_line::add_arg(desc, arg_extra_messages);
  command_line::add_arg(desc, arg_start_mining);
  command_line::add_arg(desc, arg_mining_threads);
  command_line::add_arg(desc, arg_fci_power);
  command_line::add_arg(desc, arg_fci_milk);
  command_line::add_arg(desc, arg_fci_gas);
  command_line::add_arg(desc, arg_fci_bread);
  command_line::add_arg(desc, arg_fci_eggs);
  command_line::add_arg(desc, arg_fci_currency);
  command_line::add_arg(desc, arg_fci_usd_rate);
  command_line::add_arg(desc, arg_fci_oracle_path);
  command_line::add_arg(desc, arg_fci_stale_warn);
  command_line::add_arg(desc, arg_paradio_vote);
}

void MinerConfig::init(const boost::program_options::variables_map& options) {
  // ── Standard miner options ────────────────────────────────────────────
  if (command_line::has_arg(options, arg_extra_messages))
    extraMessages = command_line::get_arg(options, arg_extra_messages);
  if (command_line::has_arg(options, arg_start_mining))
    startMining   = command_line::get_arg(options, arg_start_mining);
  if (command_line::has_arg(options, arg_mining_threads))
    miningThreads = command_line::get_arg(options, arg_mining_threads);

  // ── FCI currency / FX rate ────────────────────────────────────────────
  if (command_line::has_arg(options, arg_fci_currency))
    fciCurrency = command_line::get_arg(options, arg_fci_currency);
  if (command_line::has_arg(options, arg_fci_usd_rate)) {
    fciUsdRate = command_line::get_arg(options, arg_fci_usd_rate);
  } else {
    // Lookup convenience rate from the static table.
    double r = getCurrencyUsdRate(fciCurrency);
    if (r > 0.0) fciUsdRate = r;
    else {
      fprintf(stderr,
        "[FCI] Unknown currency '%s' — please supply --fci-usd-rate manually.\n",
        fciCurrency.c_str());
    }
  }

  // ── FCI oracle JSON path & staleness ─────────────────────────────────
  if (command_line::has_arg(options, arg_fci_oracle_path))
    fciOraclePath = command_line::get_arg(options, arg_fci_oracle_path);
  if (command_line::has_arg(options, arg_fci_stale_warn))
    fciStaleWarnDays = command_line::get_arg(options, arg_fci_stale_warn);

  // ── Load CLI prices first (highest priority) ──────────────────────────
  // bit 0=power, 1=milk, 2=bread, 3=eggs, 4=gas
  auto parseField = [&](auto arg_desc, uint8_t bit, uint32_t& out) {
    if (command_line::has_arg(options, arg_desc)) {
      double v = command_line::get_arg(options, arg_desc);
      uint32_t u = toMicroUsd(v, fciUsdRate);
      if (u > 0) { out = u; fciPresentMask |= bit; }
    }
  };
  parseField(arg_fci_power, 1 << 0, fciPower);
  parseField(arg_fci_milk,  1 << 1, fciMilk);
  parseField(arg_fci_bread, 1 << 2, fciBread);
  parseField(arg_fci_eggs,  1 << 3, fciEggs);
  parseField(arg_fci_gas,   1 << 4, fciGas);

  // ── Then fill any missing fields from oracle JSON (lower priority) ────
  if (!fciOraclePath.empty()) {
    tryLoadOracleJson(fciOraclePath, fciUsdRate,
                      fciPresentMask,
                      fciPower, fciMilk, fciGas, fciBread, fciEggs,
                      fciStaleWarnDays);
  }

  // ── Paradio vote ──────────────────────────────────────────────────────
  if (command_line::has_arg(options, arg_paradio_vote)) {
    std::string v = command_line::get_arg(options, arg_paradio_vote);
    // Enforce 128-byte wire limit; truncate gracefully.
    if (v.size() > 128) v.resize(128);
    paradioVote = std::move(v);
  }
}

} // namespace CryptoNote
