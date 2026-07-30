# SwapXFG Atomic Swap Chain Recommendations

## Purpose

This document ranks chains for atomic swap integration via Fuego's `IChainClient` interface, given:

- **HEAT (Mode 4 flatcoin)** — $1.58 starting peg adjusting monthly with CPI; XFG/USD price discovery via multi-pair atomic swap TWAP is now the Tier 1 oracle
- **XFG-as-overcollateral** — XFG price discovery quality directly affects HEAT mint/burn correctness
- **Bridge-yield distribution** — wrapped HEAT on external chains auto-accrues CD yield, making HEAT distribution a strategic priority
- **Per-block cross-chain arb** — every chain HEAT lives on contributes to peg-tightening pressure

Chains are scored across four axes:

1. **Oracle quality** — does this pair improve XFG/USD price discovery?
2. **HEAT distribution** — does bridged HEAT here unlock new yield-earning markets?
3. **Arb venue depth** — can cross-chain arb actually function here at scale?
4. **Adapter cost** — how much engineering work to ship?

---

## Tier S — Ship first (high impact × low cost)

### 1. Arbitrum ⭐ (already in flight)
- **Oracle**: ETH-on-L2 swaps add another ETH/USD triangulation source. Bonus: lower fees mean more swap volume → tighter TWAP.
- **HEAT distribution**: Wrapped HEAT/USDC on Camelot or Curve = cheap-arb venue for Eastern hemisphere ETH users.
- **Arb venue**: Curve HEAT/3pool would be the gold standard. Sub-cent arb spreads feasible.
- **Adapter cost**: **~half day** — EthChainClient with chainId 42161 + RPC URL. Worktree work already underway.
- **Strategic value**: ★★★★★

### 2. Base ⭐ (next after Arbitrum)
- **Oracle**: Same as Arbitrum — ETH/USD with deep on-chain price discovery.
- **HEAT distribution**: USDC native, Coinbase ramps = easiest fiat ↔ HEAT path globally. Wrapped HEAT/USDC on Aerodrome would route through 1Inch/Matcha aggregators.
- **Arb venue**: Aerodrome veAERO bribes are the cheapest way to bootstrap deep liquidity. Sub-cent arb economics.
- **Adapter cost**: **~half day** — EthChainClient pattern with chainId 8453 + RPC.
- **Strategic value**: ★★★★★

### 3. Tron 🐉
- **Oracle**: TRX/USD adds a high-volume, Asian-market-anchored XFG/USD triangulation. Different timezone profile than Western chains — diversifies TWAP.
- **HEAT distribution**: ~$60B+ USDT depth (the deepest stable pool in all of crypto). HEAT/USDT on SunSwap = massive new market for Asian retail.
- **Arb venue**: Sub-cent transaction fees, deep stable pairs.
- **Adapter cost**: **~2-3 days** — Tron has its own TVM (mostly EVM-compatible but signing format differs). Custom RPC client needed.
- **Strategic value**: ★★★★★

### 4. Solana deepening
- **Oracle**: Already integrated. Action: incentivize HEAT/USDC LP on Orca/Raydium for Jupiter aggregator routing.
- **HEAT distribution**: SPL-wrapped HEAT already mentioned in HEAT design doc. Highest-priority wrap target.
- **Arb venue**: Sub-second finality, Drift HEAT-USD perp listing for synthetic hedging.
- **Adapter cost**: **Already done** (Solana adapter exists). Distribution work is operational, not engineering.
- **Strategic value**: ★★★★★

---

## Tier A — Strong strategic fit, moderate cost

### 5. Hyperliquid
- **Oracle**: HYPE perp and spot markets provide 24/7 deep XFG/USD anchoring via HYPE/USD as CTR reference. Best-quality on-chain order-book data in crypto.
- **HEAT distribution**: HEAT-USD perp listing on HL = synthetic-stable instrument for holders. Combined with wrapped HEAT, makes HEAT leverageable.
- **Arb venue**: HL's matching engine + cross-margin would enable institutional-grade arb against bridged HEAT.
- **Adapter cost**: **~1 day** — HyperEVM is EVM-compatible; EthChainClient extends with chainId + RPC.
- **Strategic value**: ★★★★★

### 6. Zano 🌒
- **Oracle**: CryptoNote DNA — XmrChainClient pattern is ~70% reusable. New XFG/USD anchor from privacy-aligned community.
- **HEAT distribution**: Zano holders are Fuego's exact demographic (privacy-first, ed25519, ring sigs). Natural HEAT custodians.
- **Arb venue**: Limited (Zano has shallow DEX liquidity), but oracle contribution is the value.
- **Adapter cost**: **~1 day** — clone XmrChainClient, swap RPC endpoints.
- **Strategic value**: ★★★★

### 7. TON
- **Oracle**: TON/USDT deep on every CEX, 24/7 retail volume = manipulation-resistant USD anchor for triangulation.
- **HEAT distribution**: 100M+ Telegram users. Wrapped HEAT as USDT-alternative paying yield to Telegram users = unique market.
- **Arb venue**: Native USDT depth on TON DEXes (DeDust, STON.fi).
- **Adapter cost**: **~3-4 days** — TVM is unique architecture (not EVM-compatible), TL-B serialization, BoC encoding.
- **Strategic value**: ★★★★

### 8. Kaspa
- **Oracle**: BlockDAG with deep PoW community + 24/7 CEX coverage = different anchor profile than EVM/Asian chains.
- **HEAT distribution**: PoW maximalist community is culturally aligned with Fuego's CryptoNote roots.
- **Arb venue**: Limited DEX activity but growing.
- **Adapter cost**: **~2 days** — UTXO model similar to BCH, but kHeavyHash signing differs.
- **Strategic value**: ★★★

---

## Tier B — Strategic moats / unique offerings

### 9. Curve on Ethereum (protocol integration, not chain adapter)
- HEAT/3pool on Curve = deepest possible stable-stable AMM for HEAT/USD-stable arb on mainnet
- **Single biggest peg-defense mechanism** on Ethereum
- No new chain adapter — pure protocol/governance work to get listed
- **Adapter cost**: zero engineering; operational governance work
- **Strategic value**: ★★★★★ (priority for ETH ecosystem)

### 10. Aztec Network
- **HEAT distribution**: Shielded HEAT/USDC pool = *the only private yield-bearing stablecoin* in existence
- Fuego's natural privacy DNA extends to the largest privacy DeFi venue on Ethereum
- **Adapter cost**: **~5-7 days** — Aztec has its own zk circuits + Noir contract language
- **Strategic value**: ★★★★

### 11. Railgun (protocol integration on ETH/Polygon/Arb/BNB)
- Wrapped HEAT routes through Railgun's shielded pools for private swaps to/from any DEX
- Already-deployed protocol — integration work, not adapter work
- **Adapter cost**: **~3 days** (smart contract integration)
- **Strategic value**: ★★★★

### 12. Beam (MimbleWimble)
- **Oracle**: Only other MimbleWimble L1 at scale. Diversifies privacy-coin price discovery vs XMR.
- **HEAT distribution**: BEAM community wants private DeFi liquidity
- **Adapter cost**: **~4 days** — MW transactions have unique kernel + cut-through model
- **Strategic value**: ★★★

---

## Tier C — Niche / asymmetric upside

### 13. Penumbra (IBC shielded DEX)
- Native shielded liquidity pools + IBC bridging
- HEAT could pair with shielded USDC bridged via IBC
- **Adapter cost**: **~4-5 days** — Tendermint RPC + IBC packets + Cosmos SDK quirks
- **Strategic value**: ★★★

### 14. Sui or Aptos
- **Oracle**: Pyth Network home chains. Adding either unlocks Pyth feeds as a sanity oracle (less critical under Mode 4 hard peg, but still useful for XFG/USD reference).
- **Adapter cost**: **~5-7 days** — Move-based execution, resource model, custom serialization
- **Strategic value**: ★★★ (was ★★★★★ under flatcoin-CPI design — Mode 4 reduces criticality)

### 15. Dero
- **HEAT distribution**: Tiny chain, but homomorphic encryption smart contracts on CryptoNote-style base = ideologically very aligned
- **Adapter cost**: **~3 days** — CryptoNote variant, similar to XMR
- **Strategic value**: ★★

### 16. Mina
- **Oracle**: 22KB recursive zk L1 — enables a *trust-minimized* light-client bridge
- Technically elegant but small community
- **Adapter cost**: **~7+ days** — recursive SNARK circuits required
- **Strategic value**: ★★

---

## Tier D — De-prioritized (was higher in earlier designs)

These were higher-priority under the flatcoin-CPI-oracle design. Mode 4 with fixed-peg-plus-monthly-BLS removes much of their strategic value:

- **Truflation integration** — Mode 4 uses BLS-only monthly. No need for daily Truflation.
- **Pyth commodity basket fetcher** — was for CPI sanity. Mode 4 doesn't need it.
- **FPI (Frax Price Index)** — was for CPI consensus. Mode 4 doesn't need it.

These can be **removed from the CpiOracleService implementation**. Only BLS monthly fetcher is needed.

---

## Recommended ship order

Sequence chosen for maximum impact per unit of engineering time:

| Order | Chain | Why this slot | Cost |
|---|---|---|---|
| 1 | **Arbitrum** | Already in flight; cheapest EVM extension; Curve+3pool path | ~3-4 days (with EIP-1559 upgrade) |
| 2 | **Base** | USDC native + Coinbase ramps; Aerodrome bribes cheap | ~half day after Arbitrum |
| 3 | **Curve ETH integration** | Pure protocol work; biggest mainnet impact | 0 dev (governance) |
| 4 | **Solana HEAT/USDC pool** | Operational; Jupiter routing amplification | 0 dev (LP seeding) |
| 5 | **Hyperliquid** | HEAT-USD perp + HYPE oracle anchor | ~1 day |
| 6 | **Tron** | USDT depth + Asian distribution | ~2-3 days |
| 7 | **Zano** | Privacy community + cheap adapter clone | ~1 day |
| 8 | **TON** | Telegram distribution scale | ~3-4 days |
| 9 | **Aztec or Railgun** | Private yield-stable moat | ~3-7 days |
| 10 | **Kaspa** | PoW community + diversification | ~2 days |

This sequence ships 7 of the 10 in roughly 2-3 weeks of engineering, gives HEAT presence on every major liquidity zone, and establishes the multi-pair oracle quality needed for the Mode 4 Tier 1.

---

## Cross-cutting recommendations

### For oracle quality (Tier 1 strength):
- Add ≥6 chains within 6 months — gives ≥6 atomic swap pairs feeding multi-pair median
- Mix chain types: EVM, UTXO, Move, CryptoNote = no architectural manipulation surface
- Mix timezone profiles: Western (Base, Arb), Asian (Tron, TON), neutral (Zano, Hyperliquid)
- Mix transparency: public (Arb, Base) + private (XMR, Zano, Beam) = diverse market segments

### For HEAT distribution (bridge-yield strategy):
- Prioritize chains with deep stablecoin liquidity (Base, Tron, Solana, BNB)
- Wrapped HEAT must be on chains where users actually hold stables today
- Each wrapped HEAT pool becomes additional arb venue → tighter global peg

### For arb depth:
- Need at minimum 3 chains with HEAT pools >$1M depth to make cross-chain arb economically viable
- Below $1M depth, arb gas costs eat the spread
- Curve, Aerodrome, Orca are the three minimum-viable depth targets

### For adapter engineering efficiency:
- EVM-compatible chains share EthChainClient → marginal cost is ~1 day each
- CryptoNote-family chains (Zano, Dero, Wownero) share XmrChainClient → ~1 day each
- UTXO chains (Kaspa, LTC, DOGE) share BchChainClient pattern → ~2 days each
- Move chains (Sui, Aptos) require fresh adapter → ~5-7 days each
- Cosmos chains (Penumbra, ATOM) share Tendermint RPC → first one is ~4 days, subsequent ~2 days

**Implication**: Cluster integrations by chain family. After Arbitrum's EIP-1559 upgrade is shipped, Base + Hyperliquid + Optimism + BNB are nearly-free additions. After Zano lands, Dero/Wownero are nearly-free.

---

## What this is NOT

This doc is not:
- A roadmap commitment (chains may shift based on partnership opportunities)
- A vote on Fuego's HEAT design (Mode 4 is the decided design — these chains support it)
- A complete list (other chains may merit later consideration based on market evolution)
- An order-book vs AMM debate (Fuego is AMM-native via Hearth; external listing on order-book DEXes is a separate question)

This doc IS:
- The cost/benefit analysis to inform sequencing
- The mapping from strategic axes to specific chains
- The reference for any partner conversations about HEAT distribution targets
