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
#include "SwapPairCatalog.h"

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace XfgSwap {

// Feed metadata only. Pair identity, native asset, decimals and support level
// come from SWAP_PAIR_CATALOG in SwapPairCatalog.h, which is the single
// append-only source of truth; nothing here restates them.
//
// Settlement asset is the asset actually locked, which is not always the chain
// ticker. HashedTimelock.lock() is external payable with no ERC20 parameter and
// SolRpcClient moves lamports, so every EVM pair settles in the chain's native
// currency. RSK settles in RBTC and Gnosis in xDAI, not in BTC and DAI.
static const AssetDescriptor kFeedMeta[] = {
  {"SOL",
   "coingecko:solana",
   "SOL",
   "ef0d8b6fda2ceba41da15d4095d1da392a0d2f8ed0c6c7bc0f4cfac8c280b56d",
   "SOL"},
  {"ETH",
   "coingecko:ethereum",
   "ETH",
   "ff61491a931112ddf1bd8147cd1b641375f79f5825126d665480874634fd0ace",
   "ETH"},
  {"XMR",
   "coingecko:monero",
   "XMR",
   "46b8cc9347f04391764a0361e0b17c3ba394b001e7c304f7650f6376e37c321d",
   "XMR"},
  {"BCH",
   "coingecko:bitcoin-cash",
   "BCH",
   "3dd2b63686a450ec7290df3a1e0b583c0481f651351edfa7636f39aed55cf8a3",
   "BCH"},
  {"KMD",
   "coingecko:komodo",
   "",
   "",
   "KMD"},
  {"BNB",
   "coingecko:binancecoin",
   "BNB",
   "2f95862b045670cd22bee3114c39763a4a08beeb663b145d283c31d7d1101c4f",
   "BNB"},
  {"DCR",
   "coingecko:decred",
   "",
   "",
   "DCR"},
  {"BTC",
   "coingecko:bitcoin",
   "BTC",
   "e62df6c8b4a85fe1a67db44dc12de5db330f7ac66b72dc658afedf0f4a415b43",
   "BTC"},
  {"LTC",
   "coingecko:litecoin",
   "LTC",
   "6e3f3fa8253588df9326580180233eb791e03b443a3ba7a1d892e73874e19a54",
   "LTC"},
  {"POL",
   "coingecko:polygon-ecosystem-token",
   "POL",
   "ffd11c5a1cfd42f80afb2df4d9f264c15f956d68153335374ec10722edd70472",
   "POL"},
  {"AVAX",
   "coingecko:avalanche-2",
   "AVAX",
   "93da3352f9f1d105fdfe4971cfa80e9dd777bfc5d0f683ebb6e1294b92137bb7",
   "AVAX"},
  {"CRO",
   "coingecko:crypto-com-chain",
   "CRO",
   "23199c2bcb1303f667e733b9934db9eca5991e765b45f5ed18bc4b231415f2fe",
   "CRO"},
  {"SC",
   "coingecko:siacoin",
   "",
   "",
   "SC"},
  {"XPL",
   "coingecko:plasma",
   "XPL",
   "9873512f5cb33c77ad7a5af098d74812c62111166be395fd0941c8cedb9b00d4",
   "XPL"},
  {"DOGE",
   "coingecko:dogecoin",
   "DOGE",
   "dcef50dd0a4cd2dcc17e45df1676dcb336a11a61c69df7a0299b0150c672d25c",
   "DOGE"},
  {"DASH",
   "coingecko:dash",
   "DASH",
   "6147ae2020c6ff95f7c961f79660020f36fa72cea06452a866d5788cbedf61f3",
   "DASH"},
  {"ZEC",
   "coingecko:zcash",
   "ZEC",
   "be9b59d178f0d6a97ab4c343bff2aa69caa1eaae3e9048a65788c529b125bb24",
   "ZEC"},
  {"PLS",
   "coingecko:pulsechain",
   "",
   "",
   "PLS"},
  {"ZANO",
   "coingecko:zano",
   "",
   "",
   "ZANO"},
  {"MON",
   "coingecko:monad",
   "MON",
   "31491744e2dbf6df7fcf4ac0820d18a609b49076d45066d3568424e62f686cd1",
   "MON"},
  {"DOT",
   "coingecko:polkadot",
   "DOT",
   "ca3eed9b267293f6595901c734c7525ce8ef49adafe8284606ceb307afa2ca5b",
   "DOT"},
  {"HYPE",
   "coingecko:hyperliquid",
   "HYPE",
   "4279e31cc369bbcc2faf022b382b080e32a8e689ff20fbc530d2a603eb6cd98b",
   "HYPE"},
  {"RBTC",
   "coingecko:rootstock",
   "",
   "",
   "RBTC"},
  {"XDAI",
   "coingecko:xdai",
   "",
   "",
   "XDAI"},
  {"FLR",
   "coingecko:flare-networks",
   "FLR",
   "035aa8d0a2d74e19438f2c1440edff9f3b95f915ca65f681a25ed0bad3dc228d",
   "FLR"},
  {"KAIA",
   "coingecko:kaia",
   "KAIA",
   "452d40e01473f95aa9930911b4392197b3551b37ac92a049e87487b654b4ebbe",
   "KAIA"},
  {"PLUME",
   "coingecko:plume",
   "PLUME",
   "ded84d57dbf810bf86b97936f12e1f01b8d6d01c251a4d6eac592147988d475c",
   "PLUME"},
  {"BEAM",
   "coingecko:beam-2",
   "BEAM",
   "3871d0ef1cf9e26005e4bbf7822f67a8883071a9d8a4e7a0125d2484cca7671f",
   "BEAM"},
  {"MOVR",
   "coingecko:moonriver",
   "",
   "",
   "MOVR"},
  {"PEAQ",
   "",
   "",
   "",
   "PEAQ"},
  {"SEI",
   "coingecko:sei-network",
   "SEI",
   "53614f1cb0c031d4af66c04cb9c756234adad0e1cee85303795091499a4084eb",
   "SEI"},
  {"GLEEC",
   "",
   "",
   "",
   "GLEEC"},
  {"GRAM",
   "coingecko:the-open-network",
   "GRAM",
   "e41cd8a90528974c7b97b506abb694e2cc5750b119f796a7001890c1a93a572d",
   "GRAM"},
};

static const size_t kFeedMetaCount = sizeof(kFeedMeta) / sizeof(kFeedMeta[0]);

// The TON pair's catalog asset is the chain's native TON, but the only
// DeFiLlama and Pyth feed for "the-open-network" is GRAM, which is what this
// project settles the pair in. Pair 27 is STAGED, so it is not executable either
// way; this keeps the displayed asset honest instead of quoting a feed that
// never serves TON. Pyth also publishes a separate TONCOIN feed.
static const char* const kTonPriceableAsset = "GRAM";

// 10^19 is the largest power of ten that fits uint64 (max 1.8446744e19).
static const uint8_t kMaxFittingDecimals = 19;

static uint64_t pow10u64(uint8_t decimals) {
  uint64_t v = 1;
  for (uint8_t i = 0; i < decimals; ++i) v *= 10;
  return v;
}

static std::string decimalsText(uint8_t decimals) {
  return "1e" + std::to_string(decimals);
}

struct ResolvedAsset {
  AssetDescriptor desc;
  bool wired;      // has a registerChain call site
  bool staged;     // STAGED in the protocol catalog
  uint8_t pairId;
};

struct CatalogIndex {
  std::map<std::string, const AssetDescriptor*> bySymbol;
  std::vector<PairAssetBinding> bindings;
  std::deque<AssetDescriptor> resolved;
  std::vector<std::string> pythSymbols;
  std::vector<std::string> defiLlamaIds;
  CatalogIndex() {
    for (size_t i = 0; i < kFeedMetaCount; ++i) {
      AssetDescriptor a;
      a.symbol = kFeedMeta[i].symbol;
      a.defiLlamaId = kFeedMeta[i].defiLlamaId;
      a.pythSymbol = kFeedMeta[i].pythSymbol;
      a.pythId = kFeedMeta[i].pythId;
      a.expectedFeedSymbol = kFeedMeta[i].expectedFeedSymbol;
      a.priced = !kFeedMeta[i].defiLlamaId.empty();
      a.atomicDivisor = 0;
      a.atomicDivisorText = "";
      a.executable = true;
      bySymbol[a.symbol] = &resolved.emplace_back(a);
    }

    for (size_t i = 0; i < SWAP_PAIR_COUNT; ++i) {
      const SwapPairDescriptor& pd = SWAP_PAIR_CATALOG[i];
      std::string asset = pd.assetTicker;
      if (pd.pair == SwapPair::TON) asset = kTonPriceableAsset;
      if (!bySymbol.count(asset)) {
        AssetDescriptor a;
        a.symbol = asset;
        a.priced = false;
        a.executable = false;
        bySymbol[asset] = &resolved.emplace_back(a);
      }
      AssetDescriptor& a = *const_cast<AssetDescriptor*>(bySymbol[asset]);
      a.atomicDivisorText = decimalsText(pd.decimals);
      if (pd.decimals <= kMaxFittingDecimals) {
        a.atomicDivisor = pow10u64(pd.decimals);
      } else {
        a.atomicDivisor = 0;
        a.executable = false;
      }
      bindings.push_back({pd.pair, bySymbol[asset]});
    }

    for (const AssetDescriptor& a : resolved) {
      if (!a.pythSymbol.empty()) pythSymbols.push_back(a.pythSymbol);
      if (!a.defiLlamaId.empty()) defiLlamaIds.push_back(a.defiLlamaId);
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

const std::deque<AssetDescriptor>& assetCatalog() {
  return index().resolved;
}

const AssetDescriptor* assetForSymbol(const std::string& symbol) {
  const CatalogIndex& idx = index();
  auto it = idx.bySymbol.find(symbol);
  return it == idx.bySymbol.end() ? nullptr : it->second;
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

bool pairIsStaged(SwapPair pair) {
  const SwapPairDescriptor* pd = swapPairDescriptor(pair);
  return pd != nullptr && pd->support == SwapPairSupport::STAGED;
}

} // namespace XfgSwap