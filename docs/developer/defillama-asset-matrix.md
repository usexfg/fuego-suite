# DeFiLlama counterparty asset matrix (Phase 1 audit)

Companion machine-readable data: `defillama-asset-matrix.json`
Acceptance test: `scripts/verify-defillama-assets.py` (live; exit 0 = consistent)

Status: audited 2026-10-01 against `https://coins.llama.fi/prices/current/<coingecko ids>`.

## Why the settlement asset is not the ticker

The asset that must be priced is whatever the chain client **actually locks**, which is
not always the chain's governance token. Two pieces of evidence fix this:

- EVM: `src/SwapDaemon/Ethereum/HashedTimelock.sol` declares
  `function lock(address payable recipient, bytes32 hashLock, uint256 timeoutBlock)
  external payable`. There is no `IERC20` parameter and no token address anywhere in
  `EthChainClient` / `ContractAbi`, and `deployHtlc`/`lockHtlc` take `valueWei`. Every EVM
  pair therefore locks the **chain's native currency**.
- Solana: `SolRpcClient.cpp` moves the vault's `lamports` through `system_program`; no SPL
  token account is involved.

Consequence: ARB(4), BASE(5), ROBINHOOD(13), BOB(16), UNICHAIN(18) and OPTIMISM(26) settle in
**ETH**, not ARB/OP. `coingecko:arbitrum` resolves to the ARB governance token at ~$0.20 and
is the wrong input for pair 4.

## Matrix

`price` = DeFiLlama USD. `div` = atomic units per whole coin.

| ID | Enum | Locks | div | DeFiLlama id | Sym | Status |
|----|------|-------|-----|--------------|-----|--------|
| 0 | SOL | SOL (lamports) | 1e9 | `coingecko:solana` | SOL | priced |
| 1 | ETH | ETH | 1e18 | `coingecko:ethereum` | ETH | priced |
| 2 | XMR | XMR | 1e12 | `coingecko:monero` | XMR | priced |
| 3 | BCH | BCH | 1e8 | `coingecko:bitcoin-cash` | BCH | priced |
| 4 | ARB | **ETH** | 1e18 | `coingecko:ethereum` | ETH | priced |
| 5 | BASE | **ETH** | 1e18 | `coingecko:ethereum` | ETH | priced |
| 6 | KMD_SPV | KMD | 1e8 | — | KMD | **unpriced** |
| 7 | BNB | BNB | 1e18 | `coingecko:binancecoin` | BNB | priced |
| 8 | DCR | DCR | 1e8 | `coingecko:decred` | DCR | priced |
| 9 | BTC | BTC | 1e8 | `coingecko:bitcoin` | BTC | priced |
| 10 | LTC | LTC | 1e8 | `coingecko:litecoin` | LTC | priced |
| 11 | POLYGON | POL | 1e18 | `coingecko:polygon-ecosystem-token` | POL | priced |
| 12 | GLEEC | GLEEC | 1e18 | — | GLEEC | **unpriced** |
| 13 | ROBINHOOD | **ETH** | 1e18 | `coingecko:ethereum` | ETH | priced |
| 14 | AVAX | AVAX | 1e18 | `coingecko:avalanche-2` | AVAX | priced |
| 15 | CRO | CRO | 1e18 | `coingecko:crypto-com-chain` | CRO | priced |
| 16 | BOB | **ETH** | 1e18 | `coingecko:ethereum` | ETH | priced |
| 17 | SIA | SC | **1e24** | `coingecko:siacoin` | SC | **unexecutable** |
| 18 | UNICHAIN | **ETH** | 1e18 | `coingecko:ethereum` | ETH | priced |
| 19 | PLASMA | XPL | 1e18 | `coingecko:plasma` | XPL | priced |
| 20 | DOGE | DOGE | 1e8 | `coingecko:dogecoin` | DOGE | priced |
| 21 | DASH | DASH | 1e8 | `coingecko:dash` | DASH | priced |
| 22 | ZEC | ZEC | 1e8 | `coingecko:zcash` | ZEC | priced |
| 23 | PULSECHAIN | PLS | 1e18 | `coingecko:pulsechain` | PLS | priced |
| 24 | ZANO | ZANO | 1e12 | `coingecko:zano` | ZANO | priced |
| 25 | MONAD | MON | 1e18 | `coingecko:monad` | MON | priced |
| 26 | OPTIMISM | **ETH** | 1e18 | `coingecko:ethereum` | ETH | priced |
| 27 | TON | GRAM | 1e9 | `coingecko:the-open-network` | GRAM | priced, **client unwired** |
| 28 | DOT | DOT | 1e10 | `coingecko:polkadot` | DOT | priced |

Totals: 29 pairs, **21 distinct priced assets** (7 pairs share ETH: Ethereum itself plus ARB,
BASE, ROBINHOOD, BOB, UNICHAIN and OPTIMISM), 27 pairs priced live, 2 unpriced (KMD, GLEEC).
**All 29 pairs have an implemented chain client**; 25 are registered in `SwapDaemon.cpp` and 4
(SIA, ZANO, TON, DOT) are not. Nothing is missing code.

Two independent axes, deliberately not conflated: `status` is the **amount model** (SC alone, at
1e24, is unexecutable), and `chainClient` is the **wiring state** (SIA, ZANO, TON, DOT are
implemented but never registered).

## The feed is DeFiLlama, not CoinGecko

The oracle is **DeFiLlama** (`coins.llama.fi`). The `coingecko:` prefix on every id is
DeFiLlama's own coin-id namespace, used for assets whose reference price DeFiLlama sources
from CoinGecko. The daemon must not call CoinGecko directly, and must not treat a `coingecko:`
id as permission to bypass DeFiLlama — DeFiLlama is the feed of record, including its
`confidence` and batch `timestamp` semantics.

### Why not call CoinGecko directly — measured, not assumed

Both work keyless. Measured on this machine:

| | CoinGecko keyless | DeFiLlama |
|---|---|---|
| keyless request | 200 | 200 |
| 6 rapid hits | 200, 200, 200, **429, 429, 429** | 200 x6 |
| 12 rapid hits | already limited | 200 x12 |
| provider timestamp | `last_updated_at`, opt-in flag | `timestamp`, always present |
| price confidence | **absent** | `confidence` (0.99 observed) |

Three concrete reasons DeFiLlama is primary:

1. **Rate limit breaks the polling design.** CoinGecko keyless tolerates roughly 3 requests
   per minute. The design already polls every 30 s — 2/min — from xfg-swapd alone, and the
   guide additionally has Valise fetch its own batch, taking it to 4/min. That is a hard
   `429`, and a 429 is indistinguishable from "oracle unavailable", which per policy means
   cancelling managed offers and pausing new swaps. The system would flail by design.
2. **No `confidence`.** DeFiLlama supplies it, and the "reject confidence below 0.8" gate is
   written against it. CoinGecko's free endpoint has no price-quality equivalent; its
   `trust_score` is an asset-trustworthiness rating, not a confidence in this price.
3. **Venue aggregation.** DeFiLlama blends CEX and DEX books. For the thin and unusual assets
   here — GLEEC, MON, PLS, XPL — that coverage is the difference between a price and no price.

Caveat: this probed the **keyless** tier only. A CoinGecko API key raises the ceiling
substantially and would remove reason 1, so it is worth re-testing before treating CoinGecko
as permanently excluded.

### Recommended shape: one interface, two sources

Depending on a single third party is a real cost, and it is the same residual risk the guide
flags. Put the feed behind a `PriceFeed` interface
(`fetch(distinctAssets) -> map<assetId, {price, timestamp, confidence}>`) with two
implementations, and:

- DeFiLlama primary, CoinGecko-keyed as secondary.
- When both answer the same asset inside the staleness window, require agreement within a
  tolerance band; beyond it mark the pair `disputed` and refuse to quote or fund.
- Assets only one feed answers are labelled single-source in the oracle snapshot rather than
  presented as corroborated.

That turns "one unsigned third-party feed" into "two independent feeds that must agree",
which is the only cheap mitigation available for a price that gates real funds.

### Provider landscape — measured 2026-10-01

Requirements for this use: free or near-free, no paid tier as a hard dependency, broad coverage
of obscure assets, and a freshness signal so a stale feed is detectable.

| Provider | Key needed | Rate limit | Freshness signal | Covers our 24 symbols | CoinGecko-derived |
|---|---|---|---|---|---|
| DeFiLlama `coins.llama.fi` | no | 12/12 rapid hits OK | `timestamp` + `confidence` | **19 / 21 needed** | partly (id namespace) |
| Pyth Hermes | no | not hit | on-chain `publish_time` | 17 / 24 | **no** |
| CoinGecko keyless | no | **429 after 3/min** | opt-in `last_updated_at` | 19 / 21 needed | is the source |
| CoinPaprika / CryptoCompare | no | not tested | varies | not tested | no (own universe) |

**Pyth is free.** The public Hermes instance answered keyless with HTTP 200, and reading a
Pyth price feed on-chain costs nothing — only a transaction would cost gas. Pyth sells paid
data products and higher-rate endpoints, but nothing in the public price feed is paid. That
makes it the cheapest genuinely independent source.

Pyth's coverage is `Crypto.<SYM>/USD`, 435 feeds, verified against our asset set. It covers
SOL, ETH, XMR, BCH, BNB, BTC, LTC, POL, AVAX, CRO, XPL, DOGE, DASH, ZEC, MON, GRAM and TON.
It does **not** cover KMD, GLEEC, DCR, SC, PLS or ZANO.

Two consequences:

1. **Pyth does not fix the unpriced pairs.** KMD and GLEEC are missing there too, and Pyth is
   *narrower* than DeFiLlama for this portfolio — it also lacks DCR, SC, PLS and ZANO. So
   swapping DeFiLlama out for Pyth would *lose* four assets.
2. **Pyth independently confirms the GRAM correction.** `Crypto.GRAM/USD` is a first-class
   Pyth feed with the description "GRAM / US DOLLAR", corroborating pair 27's settlement
   asset from a source that has nothing to do with CoinGecko.

Recommended split, unchanged but now evidenced: **DeFiLlama primary for coverage, Pyth as the
mandatory independent cross-check.** 17 of our 21 priced assets get two-source corroboration;
DCR, SC, PLS and ZANO remain single-source and must be labelled that way in the oracle
snapshot rather than presented as confirmed.

Operational note: this Hermes deployment served the listing routes
(`/v2/price_feeds`, `/v2/price_feeds/{id}`) but returned 404 on `/candles` and rejected the
batched `/v2/price_feeds/latest?ids=` form, so the exact batch route needs pinning down
during implementation. Do not assume the batch shape without testing it.

## TON settles in GRAM

`coingecko:the-open-network` is the correct DeFiLlama id for pair 27 and returns
`symbol: "GRAM"`. That is the pair's settlement asset, not a mismatch. `coingecko:toncoin`,
`coingecko:ton` and `coingecko:GRAM` all return nothing.

GRAM's `1e9` divisor fits the amount model, so pair 27 is **priced** and not asset-blocked.
`TonChainClient` and `TonRpcClient` are implemented but there is no
`registerChain(SwapPair::TON)` call, so `getClient()` returns nullptr and `newSwapAllowed`
stays false. Two distinct axes: `status` is the amount model, `chainClient` is the wiring.

## Only two pairs are genuinely unpriced

- **KMD_SPV (6)** — neither `coingecko:komodo` nor `coingecko:kmd` is served. Unpriced.
- **GLEEC (12)** — no identifier serves GLEEC. Unpriced.

Both have wired chain clients, so they can execute today but have no USD reference to display
or guard against. The verifier reports this explicitly.

## Priced but not executable

- **SIA (17)** — `coingecko:sia` is not served; `coingecko:siacoin` resolves to SC (~$0.00101).
  1 SC = 10^24 hastings and 10^24 > UINT64_MAX (1.8e19), so the `uint64` amount model cannot
  represent a single SIA even once wired. Amount-model blocker, not a pricing one.
  `SiaChainClient.cpp` *is* in CMake, but unwired.
- **TON (27)** — priced via GRAM; client implemented but unregistered. Amount model is fine
  here (1e9), so the block is purely the missing `registerChain`.

**ZANO (24)** and **DOT (28)** are also implemented-but-unregistered. They are priced and
buildable, and would become executable by adding one `registerChain` call each plus config.
DOT additionally needs its Substrate HTLC story settled; SIA is capped by the amount model
regardless.

`newSwapAllowed` must be false for both regardless of feed health.

## Why the EVM chains are not "active" — it is config, not code

All 14 EVM pairs are already code-complete and already wired in `SwapDaemon.cpp`:
ETH(1), ARB(4), BASE(5), BNB(7), POLYGON(11), GLEEC(12), ROBINHOOD(13), AVAX(14), CRO(15),
BOB(16), UNICHAIN(18), PLASMA(19), PULSECHAIN(23), MONAD(25), OPTIMISM(26).

The per-chain subclasses are pure naming wrappers, e.g. the whole of
`src/SwapDaemon/Optimism/OptimismChainClient.h`:

```cpp
class OptimismChainClient : public EthChainClient {
public:
  OptimismChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address)
    : EthChainClient(std::move(rpc), address, "OPTIMISM") {}
};
```

Activation is therefore gated on configuration, not on missing code — each block is
`if (!chainCfg.<chain>Host.empty())` followed by `registerChain(...)`. A pair becomes live by
supplying `<chain>_host`, `<chain>_port`, `<chain>_chain_id`, the signer key/address, and the
HTLC registry address. So adding further EVM chains is a drop-in: a config stanza plus a
3-line wrapper, and the `SwapPair` enum value.

Two real prerequisites that are not code:

1. **`HashedTimelock` must be deployed on that chain.** `EthRpcClient::deployHtlc` refuses to
   fall back to a per-swap constructor deploy and throws if the registry address is unset, and
   `applyHtlcConfig` warns "lock/claim will fail until configured". The registry is a single
   shared registry per chain, not per swap.
2. **The registry address is chain-specific.** `ethHtlcRegistry` is reused across chains only
   where CREATE2 put the contract at the same address. GLEEC is called out in the code: an
   Evmos fork "does not guarantee" that, and so has its own `gleec_htlc_registry`. Any new
   chain must be verified rather than assumed to share the ETH address.

The verifier cross-checks `chainClient` against the `registerChain(SwapPair::…)` call sites in
`SwapDaemon.cpp`, so adding a pair to one place and not the other fails the audit.

## Feed cadence contradicts the proposed 120 s staleness gate

The guide proposes "a provider timestamp no more than 120 seconds old". Sampling
`coingecko:ethereum` four times at 70 s intervals:

```
local 1790912015  provider 1790911720  age 295s
local 1790912086  provider 1790911720  age 366s
local 1790912156  provider 1790912020  age 136s
local 1790912226  provider 1790912020  age 206s
```

The provider stamp advances in exact **300 s buckets** and is shared across all coins in a
response. Observed age therefore ramps 0 → 300 s and exceeds 120 s for ~80 % of every cycle.
A 120 s provider-age gate combined with a 30 s poll would reject most polls and make the
oracle flap between "priced" and "unavailable".

This needs a decision before the feed is coded. Recommended split:

- **fetch freshness** — gate on our own receipt time (4 s timeout), not the provider stamp;
- **provider freshness** — allow up to 600 s (two missed buckets), and treat a jump beyond
  one bucket as a provider stall to be surfaced rather than silently accepted.

## Rate contract

`referenceRate = counterpartyUsd / xfgUsd`, in **whole XFG per one whole counterparty coin**.
`rateNum = round(referenceRate * 10_000_000)` with an explicit overflow check (1 BTC at the
current XFG denominator is far past `UINT64_MAX / 1e7`, so the multiplication must be
checked, not merely cast). `xfgUsd` comes from Hearth's raw price plus the fixed HEAT peg —
never from DeFiLlama and never from the `SEED_XFG_USD` constant in `PriceOracle.cpp`.

Two-sided guard, rechecked immediately before funding and in the AFK maker path:
`referenceRate / 1.2 <= proposedRate <= referenceRate / 0.8`.

## Plan amendments (added 2026-10-01)

### A. ERC20 token support — widen from "chain asset only" to chain asset + ETH + stables

Today every EVM pair can only lock the chain's native currency, because
`HashedTimelock.lock()` takes no token address. That caps the catalog at one asset per chain
and makes stablecoin pairs impossible. Adding the ERC20 parameter unlocks, per EVM chain:
wrapped chain asset (WETH/WAVAX/WBNB/WPOL/WCRO) and the majors (USDT, USDC, DAI).

Scope of the change:

1. **Contract** — `HashedTimelock.sol` gains an `IERC20 token` field per contract, with
   `lock(token, recipient, hashLock, timeoutBlock)` pulling via `safeTransferFrom`, and
   `claim`/`refund` paying out via `safeTransfer`. Native and ERC20 both supported; the zero
   address means native. Existing deployments stay valid because the struct gains a field only
   at the end and the selector is versioned.
2. **Client** — `EthRpcClient::deployHtlc`/`lockHtlc`/`verifyLock`/`ContractInfo` carry the
   token address, and the ABI encoder gains the token parameter. `ContractInfo` must decode
   which asset a lock actually holds, so the chain client can refuse to treat an ERC20 lock as
   native.
3. **Approval** — before `lock`, the signer must `approve(registry, amount)`. Use an exact
   allowance per swap to avoid the approve-front-running race, and verify the allowance was
   not spent by a third party first.
4. **Divisor becomes per-asset, not per-chain** — this is the load-bearing change. Today
   `PriceOracle::ctrDivisor(SwapPair)` is one number per pair. USDC and USDT are 6 decimals,
   most ERC20s 18, WBTC 8. The divisor table must key on (chain, token). This also fixes the
   `default: return 1e8` silent-wrong-divisor defect by making an unknown asset an error.
5. **Matrix becomes per-asset rows** — a pair gains a list of priceable assets rather than
   one. Each needs its own DeFiLlama id and expected symbol, and `unpriced` is per-asset.
6. **Token screening** — reject fee-on-transfer (measure received vs sent), rebasing, and
   tokens with a transfer hook that can revert or reenter, before a pair may be offered.
7. **Oracle snapshot** — report per-asset, and keep `newSwapAllowed` per (pair, asset).

Sequencing: the multi-feed `PriceFeed` interface (section above) and the per-asset divisor
table are prerequisites, because ERC20 makes the asset dimension real rather than implied.

### B. Multi-feed cross-check

`PriceFeed` interface with DeFiLlama primary and CoinGecko-keyed secondary; disagreement
beyond a tolerance band marks the pair `disputed` and blocks quoting and funding.

### C. Activate the wired EVM pairs (config, not code)

All 14 EVM pairs already have code and a `registerChain` call site. Bringing them up needs, per
chain: RPC endpoint, chain id, signer key and address, and an HTLC registry address **verified
for that chain** (not assumed equal to ETH's). Until then they report as wired but inactive.

1. `PriceOracle.cpp` seeds OPTIMISM from `SEED_OP_USD`. Optimism locks native ETH, so this
   is a wrong-asset price in the current effective-rate path.
2. `PriceOracle::ctrDivisor` falls through to `1e8` in `default:` for unknown pairs, which
   silently produces a wrong divisor instead of failing.
3. `OfferManager::compositeToRateNum` falls back to the static `getSeedRate()` and never
   consults `getEffectiveRate()`, so managed offers never see the live Hearth price.
4. `PriceOracle` constructor sets `m_floorThreshold = 0.80` while its header comment and
   `validateRate` prose describe 50%; the effective band is `ref/0.8` up and `ref*0.20`
   down, which is neither the documented nor the intended symmetric ±20 % guard.
5. Valise's `SwapPairSdk` exposes **22** pairs, not the 12 stated in `AGENTS.md` and the
   dev guide. Seven C++ pairs are absent: SIA(17), DOGE(20), DASH(21), ZEC(22), ZANO(24),
   TON(27), DOT(28).
6. `scripts/verify-defillama-assets.py` parsing `SwapTypes.h` initially missed `DOT = 28`
   because the last enumerator has no trailing comma — the cross-language enum guard is
   what surfaced it.

## Residual risk (explicit, not fixed by this work)

DeFiLlama is a third-party unsigned feed. The XFG denominator comes from the Hearth pool,
which at the current thin seed depth (~1,000 HEAT, roughly $1,580 on the HEAT side) is
manipulable: a sustained attack can move the denominator while DeFiLlama's counterparty
numerator stays correct, and a two-sided guard against the moved denominator does not
remove that exposure. With no pool-based offer cap in force, the oracle bounds *display and
rate consistency*; it does not make uncapped automated liquidity economically safe.
