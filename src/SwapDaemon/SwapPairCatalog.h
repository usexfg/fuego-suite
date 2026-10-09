// Copyright (c) 2017-2026 Fuego Developers
//
// Append-only protocol catalog for cross-chain swap pairs.
//
// IMPORTANT: numeric ids are serialized in swap records and sent on the wire.
// Never renumber or reuse an existing id.  A chain whose implementation is not
// ready remains in this table with STAGED support so old records stay readable.

#pragma once

#include <cstddef>
#include <cstdint>

namespace XfgSwap {

enum class SwapChainFamily : uint8_t {
  SOLANA,
  EVM,
  CRYPTONOTE,
  UTXO,
  SIA,
  TON,
  SUBSTRATE
};

// ACTIVE: dedicated client/registration path exists.
// ADAPTER: supported by the generic family adapter when runtime preflight
//          succeeds (RPC, signer, chain id, and HTLC registry).
// STAGED: id is reserved but new offers/swaps must be rejected.
enum class SwapPairSupport : uint8_t { ACTIVE, ADAPTER, STAGED };

enum class EvmTxEnvelope : uint8_t { NONE, LEGACY, EIP1559, SPECIAL };

// name, id, config key, native asset, display name, family, chain id,
// conservative minimum/maximum milliseconds per block, decimals, support,
// transaction envelope, icon filename, brand color.  Timelock verification
// uses the minimum (enough blocks for the claim runway); ordering uses the
// maximum (the counterparty deadline must fit inside the XFG deadline).
#define XFG_SWAP_PAIR_CATALOG(X) \
  X(SOL,          0,  "sol",        "SOL",   "Solana",          SOLANA,     0,      400,      400,  9,  ACTIVE,  NONE,    "sol.png",       "#9945ff") \
  X(ETH,          1,  "eth",        "ETH",   "Ethereum",        EVM,        1,    12000,    12000, 18,  ACTIVE,  EIP1559, "eth.png",       "#627eea") \
  X(XMR,          2,  "xmr",        "XMR",   "Monero",          CRYPTONOTE, 0,   120000,   120000, 12,  ACTIVE,  NONE,    "monero.png",    "#ff6600") \
  X(BCH,          3,  "bch",        "BCH",   "Bitcoin Cash",    UTXO,       0,   600000,   600000,  8,  ACTIVE,  NONE,    "bch.png",       "#8dc351") \
  X(ARB,          4,  "arb",        "ETH",   "Arbitrum",        EVM,    42161,      250,      250, 18,  ACTIVE,  EIP1559, "arb.png",       "#28a0f0") \
  X(BASE,         5,  "base",       "ETH",   "Base",            EVM,     8453,     2000,     2000, 18,  ACTIVE,  EIP1559, "base.png",      "#0052ff") \
  X(KMD_SPV,      6,  "kmd",        "KMD",   "Komodo",          UTXO,       0,    60000,    60000,  8,  ACTIVE,  NONE,    "kmd.png",       "#2b6def") \
  X(BNB,          7,  "bsc",        "BNB",   "BNB Chain",       EVM,       56,     3000,     3000, 18,  ACTIVE,  EIP1559, "bnb.png",       "#f3ba2f") \
  X(DCR,          8,  "dcr",        "DCR",   "Decred",          UTXO,       0,   300000,   300000,  8,  ACTIVE,  NONE,    "dcr.png",       "#2970ff") \
  X(BTC,          9,  "btc",        "BTC",   "Bitcoin",         UTXO,       0,   600000,   600000,  8,  ACTIVE,  NONE,    "btc.png",       "#f7931a") \
  X(LTC,         10,  "ltc",        "LTC",   "Litecoin",        UTXO,       0,   150000,   150000,  8,  ACTIVE,  NONE,    "ltc.png",       "#bfbbbb") \
  X(POLYGON,     11,  "poly",       "POL",   "Polygon",         EVM,      137,     2000,     2000, 18,  ACTIVE,  EIP1559, "matic.png",     "#8247e5") \
  X(GLEEC,       12,  "gleec",      "GLEEC", "Gleec",           EVM,    11169,     5000,     5000, 18,  ACTIVE,  LEGACY,  "gleec.png",     "#4caf50") \
  X(ROBINHOOD,   13,  "rh",         "ETH",   "Robinhood Chain", EVM,     4663,     3000,     3000, 18,  ACTIVE,  EIP1559, "rhc.png",       "#3a2e8c") \
  X(AVAX,        14,  "avax",       "AVAX",  "Avalanche",       EVM,    43114,     2000,     2000, 18,  ACTIVE,  EIP1559, "avax.png",      "#e84142") \
  X(CRO,         15,  "cro",        "CRO",   "Cronos",          EVM,       25,     6000,     6000, 18,  ACTIVE,  LEGACY,  "cronos.png",    "#002d74") \
  X(BOB,         16,  "bob",        "ETH",   "BOB",             EVM,    60808,     2000,     2000, 18,  ACTIVE,  EIP1559, "bob.png",       "#2d9cdb") \
  X(SIA,         17,  "sia",        "SC",    "Sia",             SIA,        0,    15000,    15000, 24,  STAGED,  NONE,    "sc.png",        "#00b8d4") \
  X(UNICHAIN,    18,  "uni",        "ETH",   "Unichain",        EVM,      130,     1000,     1000, 18,  ACTIVE,  EIP1559, "uni.png",       "#fc72ff") \
  X(PLASMA,      19,  "xpl",        "XPL",   "Plasma",          EVM,     9745,     2000,     2000, 18,  ACTIVE,  EIP1559, "xpl.png",       "#7b2ff2") \
  X(DOGE,        20,  "doge",       "DOGE",  "Dogecoin",        UTXO,       0,    60000,    60000,  8,  ACTIVE,  NONE,    "doge.png",      "#c2a633") \
  X(DASH,        21,  "dash",       "DASH",  "Dash",            UTXO,       0,   260000,   260000,  8,  ACTIVE,  NONE,    "dash.png",      "#008ce7") \
  X(ZEC,         22,  "zec",        "ZEC",   "Zcash",           UTXO,       0,   150000,   150000,  8,  ACTIVE,  NONE,    "zec.png",       "#f4b728") \
  X(PULSECHAIN,  23,  "pls",        "PLS",   "PulseChain",      EVM,      369,     1000,     1000, 18,  ACTIVE,  LEGACY,  "plsx.png",      "#ff7b00") \
  X(ZANO,        24,  "zano",       "ZANO",  "Zano",            CRYPTONOTE, 0,   120000,   120000, 12,  STAGED,  NONE,    "zano.png",      "#8a2be2") \
  X(MONAD,       25,  "monad",      "MON",   "Monad",           EVM,      143,      500,      500, 18,  ACTIVE,  EIP1559, "monad.png",     "#836ef9") \
  X(OPTIMISM,    26,  "op",         "ETH",   "OP Mainnet",      EVM,       10,     2000,     2000, 18,  ACTIVE,  EIP1559, "op.jpg",        "#ff0420") \
  X(TON,         27,  "ton",        "TON",   "TON",             TON,        0,     5000,     5000,  9,  STAGED,  NONE,    "ton.png",       "#0098ea") \
  X(DOT,         28,  "dot",        "DOT",   "Polkadot",        SUBSTRATE,  0,     6000,     6000, 10,  STAGED,  NONE,    "dot.png",       "#e6007a") \
  X(LINEA,       29,  "linea",      "ETH",   "Linea",           EVM,    59144,     1000,     3000, 18,  ADAPTER, EIP1559, "linea.png",     "#61dfff") \
  X(ZKSYNC,      30,  "zksync",     "ETH",   "ZKsync Era",      EVM,      324,      500,     2000, 18,  ADAPTER, EIP1559, "zksync.png",    "#8c8dfc") \
  X(HYPEREVM,    31,  "hyperevm",   "HYPE",  "HyperEVM",        EVM,      999,     1000,     2000, 18,  ADAPTER, EIP1559, "hyperevm.png",  "#97fce4") \
  X(INK,         32,  "ink",        "ETH",   "Ink",             EVM,    57073,      250,     2000, 18,  ADAPTER, EIP1559, "ink.png",       "#7132f5") \
  X(RSK,         33,  "rsk",        "RBTC",  "Rootstock",       EVM,       30,    15000,    60000, 18,  ADAPTER, LEGACY,  "rsk.png",       "#e9b64e") \
  X(GNOSIS,      34,  "gnosis",     "XDAI",  "Gnosis",          EVM,      100,     5000,    10000, 18,  ADAPTER, EIP1559, "gnosis.png",    "#1d6c4e") \
  X(FLARE,       35,  "flare",      "FLR",   "Flare",           EVM,       14,     1000,     3000, 18,  ADAPTER, EIP1559, "flare.png",     "#e6413e") \
  X(KAIA,        36,  "kaia",       "KAIA",  "Kaia",            EVM,     8217,      500,     2000, 18,  ADAPTER, LEGACY,  "kaia.png",      "#ff1d01") \
  X(SCROLL,      37,  "scroll",     "ETH",   "Scroll",          EVM,   534352,      250,     6000, 18,  ADAPTER, EIP1559, "scroll.png",    "#ebc28e") \
  X(ABSTRACT,    38,  "abstract",   "ETH",   "Abstract",        EVM,     2741,      250,     2000, 18,  ADAPTER, EIP1559, "abstract.png",  "#202020") \
  X(PLUME,       39,  "plume",      "PLUME", "Plume",           EVM,    98866,      200,     1000, 18,  ADAPTER, EIP1559, "plume.png",     "#ff3d00") \
  X(SONEIUM,     40,  "soneium",    "ETH",   "Soneium",         EVM,     1868,     1000,     4000, 18,  ADAPTER, EIP1559, "soneium.png",   "#937dff") \
  X(DOMA,        41,  "doma",       "ETH",   "Doma",            EVM,    97477,      250,     4000, 18,  ADAPTER, EIP1559, "doma.png",      "#4f46e5") \
  X(BEAM,        42,  "beam",       "BEAM",  "Beam",            EVM,     4337,     1000,     4000, 18,  ADAPTER, EIP1559, "beam.png",      "#0bdbb5") \
  X(MOONRIVER,   43,  "moonriver",  "MOVR",  "Moonriver",       EVM,     1285,     3000,    12000, 18,  ADAPTER, LEGACY,  "moonriver.png", "#f5b700") \
  X(PEAQ,        44,  "peaq",       "PEAQ",  "peaq",            EVM,     3338,     3000,    18000, 18,  ADAPTER, LEGACY,  "peaq.png",      "#7a2bf5") \
  X(SEI,         45,  "sei",        "SEI",   "Sei",             EVM,     1329,      200,     1000, 18,  ADAPTER, EIP1559, "sei.png",       "#9e1f19")

enum class SwapPair : uint8_t {
#define XFG_SWAP_ENUM(name, id, key, asset, display, family, chainId, minBlockMs, maxBlockMs, decimals, support, tx, icon, color) name = id,
  XFG_SWAP_PAIR_CATALOG(XFG_SWAP_ENUM)
#undef XFG_SWAP_ENUM
};

struct SwapPairDescriptor {
  SwapPair pair;
  uint8_t id;
  const char* key;
  const char* symbol;
  const char* assetTicker;
  const char* displayName;
  SwapChainFamily family;
  uint64_t chainId;
  uint64_t minBlockTimeMs;
  uint64_t maxBlockTimeMs;
  uint8_t decimals;
  SwapPairSupport support;
  EvmTxEnvelope txEnvelope;
  const char* icon;
  const char* color;
};

constexpr SwapPairDescriptor SWAP_PAIR_CATALOG[] = {
#define XFG_SWAP_DESC(name, id, key, asset, display, family, chainId, minBlockMs, maxBlockMs, decimals, support, tx, icon, color) \
  {SwapPair::name, id, key, #name, asset, display, SwapChainFamily::family, chainId, minBlockMs, maxBlockMs, decimals, SwapPairSupport::support, EvmTxEnvelope::tx, icon, color},
  XFG_SWAP_PAIR_CATALOG(XFG_SWAP_DESC)
#undef XFG_SWAP_DESC
};

constexpr size_t SWAP_PAIR_COUNT = sizeof(SWAP_PAIR_CATALOG) / sizeof(SWAP_PAIR_CATALOG[0]);
constexpr uint8_t MAX_SWAP_PAIR_INDEX = SWAP_PAIR_CATALOG[SWAP_PAIR_COUNT - 1].id;

constexpr const SwapPairDescriptor* swapPairDescriptor(SwapPair pair) {
  const auto id = static_cast<uint8_t>(pair);
  return id < SWAP_PAIR_COUNT && SWAP_PAIR_CATALOG[id].id == id
      ? &SWAP_PAIR_CATALOG[id]
      : nullptr;
}

constexpr bool isKnownSwapPair(uint8_t id) {
  return id < SWAP_PAIR_COUNT && SWAP_PAIR_CATALOG[id].id == id;
}

constexpr bool isProtocolSwapPair(uint8_t id) {
  return isKnownSwapPair(id) &&
      SWAP_PAIR_CATALOG[id].support != SwapPairSupport::STAGED;
}

constexpr const char* swapChainFamilyToString(SwapChainFamily family) {
  switch (family) {
    case SwapChainFamily::SOLANA: return "solana";
    case SwapChainFamily::EVM: return "evm";
    case SwapChainFamily::CRYPTONOTE: return "cryptonote";
    case SwapChainFamily::UTXO: return "utxo";
    case SwapChainFamily::SIA: return "sia";
    case SwapChainFamily::TON: return "ton";
    case SwapChainFamily::SUBSTRATE: return "substrate";
  }
  return "unknown";
}

constexpr const char* swapPairSupportToString(SwapPairSupport support) {
  switch (support) {
    case SwapPairSupport::ACTIVE: return "active";
    case SwapPairSupport::ADAPTER: return "adapter";
    case SwapPairSupport::STAGED: return "staged";
  }
  return "staged";
}

constexpr const char* evmTxEnvelopeToString(EvmTxEnvelope envelope) {
  switch (envelope) {
    case EvmTxEnvelope::NONE: return "none";
    case EvmTxEnvelope::LEGACY: return "legacy";
    case EvmTxEnvelope::EIP1559: return "eip1559";
    case EvmTxEnvelope::SPECIAL: return "special";
  }
  return "special";
}

static_assert(MAX_SWAP_PAIR_INDEX == 45, "pair ids are append-only; update the protocol bound deliberately");
static_assert(SWAP_PAIR_COUNT == static_cast<size_t>(MAX_SWAP_PAIR_INDEX) + 1,
              "pair catalog ids must remain contiguous");

#undef XFG_SWAP_PAIR_CATALOG

} // namespace XfgSwap
