// Copyright (c) 2017-2026, Fuego Developers
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

#include "PriceFeed.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace XfgSwap {

// Settlement assets, not tickers. HashedTimelock.lock() is external payable with
// no ERC20 parameter and SolRpcClient moves lamports, so every EVM pair settles in
// the chain's native currency. Six pairs therefore share the ETH descriptor.
static const AssetDescriptor kAssets[] = {
  {"SOL",  "coingecko:solana",                  "SOL",   1000000000ULL,   "1e9",   true,  true},
  {"ETH",  "coingecko:ethereum",                "ETH",   1000000000000000000ULL, "1e18", true, true},
  {"XMR",  "coingecko:monero",                  "XMR",   1000000000000ULL, "1e12", true,  true},
  {"BCH",  "coingecko:bitcoin-cash",            "BCH",   100000000ULL,    "1e8",   true,  true},
  {"KMD",  "",                                  "",      100000000ULL,    "1e8",   false, true},
  {"BNB",  "coingecko:binancecoin",             "BNB",   1000000000000000000ULL, "1e18", true, true},
  {"DCR",  "coingecko:decred",                  "",      100000000ULL,    "1e8",   true,  true},
  {"BTC",  "coingecko:bitcoin",                 "BTC",   100000000ULL,    "1e8",   true,  true},
  {"LTC",  "coingecko:litecoin",                "LTC",   100000000ULL,    "1e8",   true,  true},
  {"POL",  "coingecko:polygon-ecosystem-token", "POL",   1000000000000000000ULL, "1e18", true, true},
  {"GLEEC","",                                  "",      1000000000000000000ULL, "1e18", false, true},
  {"AVAX", "coingecko:avalanche-2",             "AVAX",  1000000000000000000ULL, "1e18", true, true},
  {"CRO",  "coingecko:crypto-com-chain",        "CRO",   1000000000000000000ULL, "1e18", true, true},
  {"SC",   "coingecko:siacoin",                 "",      0ULL,             "1e24",  true,  false},
  {"XPL",  "coingecko:plasma",                  "XPL",   1000000000000000000ULL, "1e18", true, true},
  {"DOGE", "coingecko:dogecoin",                "DOGE",  100000000ULL,    "1e8",   true,  true},
  {"DASH", "coingecko:dash",                    "DASH",  100000000ULL,    "1e8",   true,  true},
  {"ZEC",  "coingecko:zcash",                   "ZEC",   100000000ULL,    "1e8",   true,  true},
  {"PLS",  "coingecko:pulsechain",              "",      1000000000000000000ULL, "1e18", true, true},
  {"ZANO", "coingecko:zano",                    "",      1000000000000ULL, "1e12", true, true},
  {"MON",  "coingecko:monad",                   "MON",   1000000000000000000ULL, "1e18", true, true},
  {"GRAM", "coingecko:the-open-network",        "GRAM",  1000000000ULL,   "1e9",   true,  true},
  {"DOT",  "coingecko:polkadot",                "DOT",   10000000000ULL,  "1e10",  true,  true},
  // Native coins of the generic-adapter EVM pairs. No feed id is verified for any of
  // these yet, so each is unpriced and fails closed rather than borrowing a lookalike id.
  {"HYPE", "",                                  "",      1000000000000000000ULL, "1e18", false, true},
  {"RBTC", "",                                  "",      1000000000000000000ULL, "1e18", false, true},
  {"XDAI", "",                                  "",      1000000000000000000ULL, "1e18", false, true},
  {"FLR",  "",                                  "",      1000000000000000000ULL, "1e18", false, true},
  {"KAIA", "",                                  "",      1000000000000000000ULL, "1e18", false, true},
  {"PLUME","",                                  "",      1000000000000000000ULL, "1e18", false, true},
  {"BEAM", "",                                  "",      1000000000000000000ULL, "1e18", false, true},
  {"MOVR", "",                                  "",      1000000000000000000ULL, "1e18", false, true},
  {"PEAQ", "",                                  "",      1000000000000000000ULL, "1e18", false, true},
  {"SEI",  "",                                  "",      1000000000000000000ULL, "1e18", false, true},
};

static const size_t kAssetCount = sizeof(kAssets) / sizeof(kAssets[0]);

struct CatalogIndex {
  std::map<std::string, size_t> bySymbol;
  std::vector<PairAssetBinding> bindings;
  std::vector<std::string> pythSymbols;
  std::vector<std::string> defiLlamaIds;
  CatalogIndex() {
    for (size_t i = 0; i < kAssetCount; ++i) bySymbol[kAssets[i].symbol] = i;

    bindings = {
      {SwapPair::SOL,        &kAssets[0]},   // SOL
      {SwapPair::ETH,        &kAssets[1]},   // ETH
      {SwapPair::XMR,        &kAssets[2]},   // XMR
      {SwapPair::BCH,        &kAssets[3]},   // BCH
      {SwapPair::ARB,        &kAssets[1]},   // ETH
      {SwapPair::BASE,       &kAssets[1]},   // ETH
      {SwapPair::KMD_SPV,    &kAssets[4]},   // KMD
      {SwapPair::BNB,        &kAssets[5]},   // BNB
      {SwapPair::DCR,        &kAssets[6]},   // DCR
      {SwapPair::BTC,        &kAssets[7]},   // BTC
      {SwapPair::LTC,        &kAssets[8]},   // LTC
      {SwapPair::POLYGON,    &kAssets[9]},   // POL
      {SwapPair::GLEEC,      &kAssets[10]},  // GLEEC
      {SwapPair::ROBINHOOD,  &kAssets[1]},   // ETH
      {SwapPair::AVAX,       &kAssets[11]},  // AVAX
      {SwapPair::CRO,        &kAssets[12]},  // CRO
      {SwapPair::BOB,        &kAssets[1]},   // ETH
      {SwapPair::SIA,        &kAssets[13]},  // SC
      {SwapPair::UNICHAIN,   &kAssets[1]},   // ETH
      {SwapPair::PLASMA,     &kAssets[14]},  // XPL
      {SwapPair::DOGE,       &kAssets[15]},  // DOGE
      {SwapPair::DASH,       &kAssets[16]},  // DASH
      {SwapPair::ZEC,        &kAssets[17]},  // ZEC
      {SwapPair::PULSECHAIN, &kAssets[18]},  // PLS
      {SwapPair::ZANO,       &kAssets[19]},  // ZANO
      {SwapPair::MONAD,      &kAssets[20]},  // MON
      {SwapPair::OPTIMISM,   &kAssets[1]},   // ETH
      {SwapPair::TON,        &kAssets[21]},  // GRAM
      {SwapPair::DOT,        &kAssets[22]},  // DOT
      // Generic-adapter EVM pairs (SwapPairCatalog.h ids 29-45). Seven settle in ETH.
      {SwapPair::LINEA,      &kAssets[1]},   // ETH
      {SwapPair::ZKSYNC,     &kAssets[1]},   // ETH
      {SwapPair::HYPEREVM,   &kAssets[23]},  // HYPE
      {SwapPair::INK,        &kAssets[1]},   // ETH
      {SwapPair::RSK,        &kAssets[24]},  // RBTC
      {SwapPair::GNOSIS,     &kAssets[25]},  // XDAI
      {SwapPair::FLARE,      &kAssets[26]},  // FLR
      {SwapPair::KAIA,       &kAssets[27]},  // KAIA
      {SwapPair::SCROLL,     &kAssets[1]},   // ETH
      {SwapPair::ABSTRACT,   &kAssets[1]},   // ETH
      {SwapPair::PLUME,      &kAssets[28]},  // PLUME
      {SwapPair::SONEIUM,    &kAssets[1]},   // ETH
      {SwapPair::DOMA,       &kAssets[1]},   // ETH
      {SwapPair::BEAM,       &kAssets[29]},  // BEAM
      {SwapPair::MOONRIVER,  &kAssets[30]},  // MOVR
      {SwapPair::PEAQ,       &kAssets[31]},  // PEAQ
      {SwapPair::SEI,        &kAssets[32]},  // SEI
    };

    for (size_t i = 0; i < kAssetCount; ++i) {
      if (!kAssets[i].pythSymbol.empty()) pythSymbols.push_back(kAssets[i].pythSymbol);
      if (!kAssets[i].defiLlamaId.empty()) defiLlamaIds.push_back(kAssets[i].defiLlamaId);
    }
    std::sort(pythSymbols.begin(), pythSymbols.end());
    pythSymbols.erase(std::unique(pythSymbols.begin(), pythSymbols.end()), pythSymbols.end());
    std::sort(defiLlamaIds.begin(), defiLlamaIds.end());
    defiLlamaIds.erase(std::unique(defiLlamaIds.begin(), defiLlamaIds.end()), defiLlamaIds.end());
  }
};

static const CatalogIndex& index() {
  static const CatalogIndex idx;
  return idx;
}

const std::vector<AssetDescriptor>& assetCatalog() {
  static const std::vector<AssetDescriptor> all(kAssets, kAssets + kAssetCount);
  return all;
}

const AssetDescriptor* assetForSymbol(const std::string& symbol) {
  const CatalogIndex& idx = index();
  auto it = idx.bySymbol.find(symbol);
  return it == idx.bySymbol.end() ? nullptr : &kAssets[it->second];
}

std::vector<PairAssetBinding> allBindings() {
  return index().bindings;
}

std::vector<std::string> distinctSymbolsForFeed(const std::string& feedName) {
  const CatalogIndex& idx = index();
  if (feedName == "defillama") return idx.defiLlamaIds;
  if (feedName == "pyth")      return idx.pythSymbols;
  return {};
}

std::vector<PairAssetBinding> bindingsForFeed(const std::string& feedName) {
  std::vector<PairAssetBinding> out;
  for (const auto& b : index().bindings) {
    if (b.asset == nullptr) continue;
    if (feedName == "defillama" && b.asset->priced) out.push_back(b);
    else if (feedName == "pyth" && !b.asset->pythSymbol.empty()) out.push_back(b);
  }
  return out;
}

} // namespace XfgSwap