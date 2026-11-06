# DeXFG Swap Chains

XFG is one side of every DeXFG swap. The counterparty chains come from one table:
`src/SwapDaemon/SwapPairCatalog.h`, an append-only X-macro catalog of 46 wire ids
(25 ACTIVE, 17 ADAPTER, 4 STAGED). Ids are serialized in swap records and on the wire;
they are never renumbered or reused.

## When a pair can start a swap

All four gates must pass:

1. **Catalog support is not STAGED** (`isProtocolSwapPair`).
2. **The relay admits it** (`SwapOfferRelay::isExecutablePair`). It excludes the UTXO and
   Monero pairs: their SPV clients cannot submit locks, and their full-node modes cannot
   discover claims independently (`isReadyForNewSwap()` returns false).
3. **The chain client is ready**: a signer, an RPC whose chain id matches the catalog, and
   a contract deployed on that chain — `HashedTimelock` for EVM (readiness probes its
   on-chain code), the HTLC program for Solana.
4. **A live price quote allows it** (`validateNewSwapRate`). A settlement asset with no
   verified feed id is `UNPRICED` and fails closed.

Of the 46 ids, **31 pass all four once configured**: 30 EVM chains and Solana.

| Gate that blocks | Pairs |
|---|---|
| none (configure + deploy) | 30 EVM pairs and SOL |
| price feed | GLEEC (12), PEAQ (44) |
| relay + client | XMR, BCH, KMD, DCR, BTC, LTC, DOGE, DASH, ZEC |
| STAGED | SIA, ZANO, TON, DOT |

## Catalog

| Id | Symbol | Config key | Settles in | Family | Chain id | Support | Can start a swap |
|---:|---|---|---|---|---:|---|---|
| 0 | SOL | `sol` | SOL | SOLANA | — | ACTIVE | Yes — local RPC + deployed program |
| 1 | ETH | `eth` | ETH | EVM | 1 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 2 | XMR | `xmr` | XMR | CRYPTONOTE | — | ACTIVE | No — no executable client |
| 3 | BCH | `bch` | BCH | UTXO | — | ACTIVE | No — no executable client |
| 4 | ARB | `arb` | ETH | EVM | 42161 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 5 | BASE | `base` | ETH | EVM | 8453 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 6 | KMD_SPV | `kmd` | KMD | UTXO | — | ACTIVE | No — no executable client |
| 7 | BNB | `bsc` | BNB | EVM | 56 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 8 | DCR | `dcr` | DCR | UTXO | — | ACTIVE | No — no executable client |
| 9 | BTC | `btc` | BTC | UTXO | — | ACTIVE | No — no executable client |
| 10 | LTC | `ltc` | LTC | UTXO | — | ACTIVE | No — no executable client |
| 11 | POLYGON | `poly` | POL | EVM | 137 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 12 | GLEEC | `gleec` | GLEEC | EVM | 11169 | ACTIVE | No — no verified price feed |
| 13 | ROBINHOOD | `rh` | ETH | EVM | 4663 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 14 | AVAX | `avax` | AVAX | EVM | 43114 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 15 | CRO | `cro` | CRO | EVM | 25 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 16 | BOB | `bob` | ETH | EVM | 60808 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 17 | SIA | `sia` | SC | SIA | — | STAGED | No — STAGED |
| 18 | UNICHAIN | `uni` | ETH | EVM | 130 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 19 | PLASMA | `xpl` | XPL | EVM | 9745 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 20 | DOGE | `doge` | DOGE | UTXO | — | ACTIVE | No — no executable client |
| 21 | DASH | `dash` | DASH | UTXO | — | ACTIVE | No — no executable client |
| 22 | ZCASH | `zec` | ZEC | UTXO | — | ACTIVE | No — no executable client |
| 23 | PULSECHAIN | `pls` | PLS | EVM | 369 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 24 | ZANO | `zano` | ZANO | CRYPTONOTE | — | STAGED | No — STAGED |
| 25 | MONAD | `monad` | MON | EVM | 143 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 26 | OPTIMISM | `op` | ETH | EVM | 10 | ACTIVE | Yes — signer + HashedTimelock on this chain |
| 27 | TON | `ton` | GRAM | TON | — | STAGED | No — STAGED |
| 28 | DOT | `dot` | DOT | SUBSTRATE | — | STAGED | No — STAGED |
| 29 | LINEA | `linea` | ETH | EVM | 59144 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 30 | ZKSYNC | `zksync` | ETH | EVM | 324 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 31 | HYPEREVM | `hyperevm` | HYPE | EVM | 999 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 32 | INK | `ink` | ETH | EVM | 57073 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 33 | ROOTSTOCK | `rsk` | RBTC | EVM | 30 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 34 | GNOSIS | `gnosis` | XDAI | EVM | 100 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 35 | FLARE | `flare` | FLR | EVM | 14 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 36 | KAIA | `kaia` | KAIA | EVM | 8217 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 37 | SCROLL | `scroll` | ETH | EVM | 534352 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 38 | ABSTRACT | `abstract` | ETH | EVM | 2741 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 39 | PLUME | `plume` | PLUME | EVM | 98866 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 40 | SONEIUM | `soneium` | ETH | EVM | 1868 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 41 | DOMA | `doma` | ETH | EVM | 97477 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 42 | BEAM | `beam` | BEAM | EVM | 4337 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 43 | MOONRIVER | `moonriver` | MOVR | EVM | 1285 | ADAPTER | Yes — signer + HashedTimelock on this chain |
| 44 | PEAQ | `peaq` | PEAQ | EVM | 3338 | ADAPTER | No — no verified price feed |
| 45 | SEI | `sei` | SEI | EVM | 1329 | ADAPTER | Yes — signer + HashedTimelock on this chain |

ACTIVE means a dedicated client class; ADAPTER means the generic `EthChainClient`
registered from an `evm_chains` config entry. Both are executable. Every EVM pair settles
in its chain's native coin: `HashedTimelock.lock()` is `payable` and takes no token, so
Rootstock settles in RBTC and Gnosis in xDAI.

## Configuring an EVM pair

ADAPTER pairs have no flat `*_host` fields; `evm_chains` is their only config path.
`swap_config.example.json` carries an entry for each, and
`test_example_evm_chains_cover_every_adapter_pair` fails if that block drifts from the
catalog. An entry needs `rpc_url`, a funded `private_key` with its `address`, and the
`htlc_registry` deployed **on that chain**. On mainnet `chain_id` must equal the catalog
value.

## Price feeds

`src/SwapDaemon/AssetCatalog.cpp` maps each settlement asset to a `coingecko:*` id.
xfg-swapd's price worker reads that id from DeFiLlama (primary) and CoinGecko (secondary).

- **PEAQ**: `coingecko:peaq` and `coingecko:peaq-network` were tested against DeFiLlama and
  neither resolves. `coingecko:peaq-2` appears on third-party sites but has not been tested.
  Until a verified id lands, PEAQ quotes are `UNPRICED` and every new PEAQ swap is refused.
- **GLEEC**: no feed id, same outcome.

## Not in the catalog: Tempo

Tempo (chain id 4217) has no native coin; `eth_getBalance` returns a constant. With nothing
native to escrow, `HashedTimelock` cannot lock value there. Supporting Tempo needs a
token-escrow HTLC contract, a token lock/claim/refund path in `EthRpcClient`, a chosen
settlement token with a verified price feed, and a new catalog id. Fuego Valise lists Tempo
in its ERC20 wallet layer only.

## Fuego Valise mirror

The wallet mirrors the catalog in `SwapPairSdk` (39 ids: every id except SIA, DOGE, DASH,
ZEC, ZANO, TON and DOT) and in `chains.yaml`, where all 32 catalog EVM chains are
`tier: swap` and Tempo is `tier: wallet`. The Cross-Chain tab lists chains from xfg-swapd's
own readiness report, so a pair appears there only when the daemon carries it.
