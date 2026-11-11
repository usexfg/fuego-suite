# DefiLlama submission pack — Fuego / XFG

Prepared 2026-10-04. This is the infrastructure-independent part of a DefiLlama
listing. Everything here is verified against the running network or the source tree;
nothing is guessed.

## Why this cannot be submitted yet

DefiLlama's `DefiLlama-Chains` requires a working public RPC **and** a block explorer
for every chain. Measured on 2026-10-04 from a host with confirmed outbound access
(`api.llama.fi:443` reachable):

| Host | Declared where | DNS | Connect |
|------|----------------|-----|---------|
| `fuego.seednode1.com:18180` | wallet `network_config.dart` | **no record** | n/a |
| `fuego.seednode2.com:18180` | wallet `network_config.dart` | **no record** | n/a |
| `node1.usexfg.org:18180` | wallet `network_config.dart` | **no record** | n/a |
| `node2.usexfg.org:18180` | wallet `network_config.dart` | **no record** | n/a |
| `fuego.communitynode.net:18180` | wallet `network_config.dart` | **no record** | n/a |
| `207.244.247.64:18180` | wallet `network_config.dart` | — | refused |
| `explorer.usexfg.org` | wallet `explorerUrl` | resolves (GitHub Pages IPs) | **:443 closed** |

So the five named seed nodes have no DNS records at all, the one literal IP refuses
connections, and the explorer domain resolves to GitHub Pages addresses where nothing is
being served. There is currently no public Fuego endpoint for DefiLlama's infrastructure
to call.

## What the code exposes for TVL

Certificates of Deposit are the obvious TVL source. The blockchain snapshot serialises
four CD fields (`src/CryptoNoteCore/Blockchain.cpp`):

    "cd_info"
    "cd_yield_pool"
    "cd_reserve"
    "cd_expiry_by_epoch"

**None of these is a node RPC method.** They are snapshot serialization keys. The public
node RPC surface exposes no aggregate CD read — a search for a `get_*` method touching
CD deposits returns nothing. `cd_reserve` is the protocol reserve, not user principal, so
substituting it for TVL would report a different quantity and must not be done silently.

This is the one genuine API gap, and it is small.

## Required before submission

1. **A public, documented Fuego RPC endpoint** that resolves and accepts connections from
   DefiLlama's infrastructure, with an uptime commitment. A single node will not do —
   DefiLlama expects redundancy.
2. **A working block explorer** on `explorer.usexfg.org` serving `/api/status`, and
   returning `/api/get_tx`, `/api/get_block`, `/api/get_txids_by_block`.
3. **A node RPC method for aggregate CD principal.** Proposed shape, matching the existing
   camelCase convention of the wallet RPC and the snake_case of the snapshot serializer:

       POST /get_cd_summary
       { "jsonrpc": "2.0", "id": 0, "method": "get_cd_summary" }
       -> { "jsonrpc": "2.0", "id": 0, "result": {
              "principal_atomic": "123456789000000000",
              "reserve_atomic":   "...",
              "yield_pool_atomic":"...",
              "epoch": 12345 } }

   `principal_atomic` is the number TVL needs. Returning a decimal **string** rather than a
   JSON number avoids the double-precision loss that a 18-decimal atomic value hits above
   2^53.

4. **A CoinGecko listing for XFG** (or a custom token list entry). DefiLlama sources token
   prices from CoinGecko, and without an id there is no price and no volume.

## Why listing is worth the work

Beyond TVL and DEX volume, there is a correctness argument that came out of this project.

`computeReferenceRate` divides the counterparty's USD price by `xfgUsd`, and `xfgUsd` comes
from a single uncorroborated source: `m_rpc.getFuegoPrice()`, i.e. Hearth. Both price feeds
(DeFiLlama and CoinGecko) corroborate the **counterparty** side of every rate. The **XFG**
side has no second source at all. A DefiLlama or CoinGecko listing for XFG would close that
gap, which is a stronger reason to pursue it than the TVL headline figure.

## Files

- `chains.json` — the `DefiLlama-Chains` entry, with unknown fields explicitly `null`.
- `adapter/index.ts` — TVL adapter skeleton. The call site is marked `TODO` because the RPC
  method in item 3 does not exist yet; inventing a method name here would produce an
  adapter that looks finished and is not.

## Submission order

1. Stand up RPC + explorer, then open the chain PR in `DefiLlama/DefiLlama-Chains`.
2. Once `get_cd_summary` exists and the adapter is un-stubbed, open the TVL adapter PR in
   `DefiLlama/DefiLlama-Adapters` under `projects/fuego-index`.
3. If Hearth Exchange should be listed as a DEX, that is a separate adapter, submitted
   after the chain is live.