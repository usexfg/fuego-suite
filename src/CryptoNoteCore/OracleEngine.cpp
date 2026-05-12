// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "OracleEngine.h"
#include <algorithm>

namespace CryptoNote {

OracleEngine::OracleEngine() : m_xfgPerUsd(0) {}

uint64_t OracleEngine::computeFromPairs(const std::vector<SwapPairPrice>& pairs) {
  if (pairs.empty()) return 0;

  std::vector<uint64_t> rates;
  for (const auto& p : pairs) {
    if (p.xfgPerUnit == 0 || p.cpUsdPrice == 0)
      continue;
    // xfgPerUsd = xfgPerUnit / (cpUsdPrice / 100)
    //           = xfgPerUnit * 100 / cpUsdPrice
    unsigned __int128 num = (unsigned __int128)p.xfgPerUnit * 100;
    uint64_t rate = (uint64_t)(num / p.cpUsdPrice);
    rates.push_back(rate);
  }

  if (rates.empty()) return 0;

  std::sort(rates.begin(), rates.end());
  return rates[rates.size() / 2];  // median
}

void OracleEngine::feedSwapPrices(const std::vector<SwapPairPrice>& pairs) {
  m_xfgPerUsd = computeFromPairs(pairs);
}

} // namespace CryptoNote
