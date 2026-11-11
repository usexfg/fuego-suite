# SwapXFG — Final Nine Chain Recommendations

## Purpose

Consolidates the prior two recommendation docs (`SWAPXFG_CHAIN_RECOMMENDATIONS.md` for HEAT Mode 4 mechanics, `SWAPXFG_CHAIN_REACH_STRATEGY.md` for reach/strategy/privacy/novelty) against the **full Fuego ecosystem stack** — Fuego L1 + HEAT flatcoin + DIGM music platform.

The DIGM layer (per `DIGM_FUEGO_ARCHITECTURE_GUIDE.md`) materially changes priorities:
- **Mobile-first full node** → chains with mobile light-client tech rise (Mina)
- **Telegram-aligned distribution** → TON moves from "useful" to "essential"
- **HEAT premium gate already on Ethereum (ERC-20) + Solana (SPL)** → those need deepening, not new architecture
- **Music creator economy** → Base (Sound.xyz, Farcaster), Polygon (music NFT marketplaces) gain weight
- **Privacy-first network transport (I2P)** → privacy DNA chains stay critical

This doc is the **decision-grade ranked nine** — the chains worth committing engineering capital to.

---

## The Final Nine

### 1. Arbitrum ⭐ (in flight)
**Why**: EVM template that catalyzes the whole EVM family. Once the EIP-1559 upgrade lands, every subsequent EVM chain is ~0.5-1 day each. Deep DeFi (Curve, GMX, Camelot). Cross-chain HEAT pool via Curve HEAT/3pool is the gold standard for mainnet-adjacent peg defense.

**DIGM angle**: Lowest-fee ETH-family chain for wrapped HEAT used in DIGM premium gate (1M HEAT threshold).

**Status**: Worktree `swap-arbitrum-eip1559` — partial. Resume to complete.

**Cost remaining**: ~3-4 days (signAndSend wiring + Workstream B + tests)

---

### 2. Base ⭐
**Why**: The Coinbase ramp = single biggest fiat→crypto on-ramp globally. Aerodrome bribes are the cheapest known liquidity bootstrap. Sub-cent fees make arb economically viable at any spread.

**DIGM angle**: Triple-duty —
- **Coinbase users** (5M+ with on-chain accounts) can hold HEAT premium-gate balance with one-click
- **Sound.xyz** (music NFT platform) lives on Base. Music NFT collectors are DIGM's exact crossover demographic
- **Farcaster** creator economy runs on Base. Creator-economy users overlap with DIGM artists

**Status**: Pending. Direct extension of EthChainClient after Arbitrum.

**Cost**: ~0.5 day after Arbitrum lands (just chainId + RPC + signer config)

---

### 3. Hyperliquid
**Why**: HYPE perp + spot markets give 24/7 deep order-book price discovery — institutional-grade XFG/USD anchor for Mode 4 Tier 1 oracle. Plus: HEAT-USD perp listing creates synthetic stability for holders (delta-neutral USD-value position without selling HEAT). No other chain offers this combination.

**DIGM angle**: Indirect. Artists/labels who want to lock in fiat-equivalent revenue from album sales can hedge via HEAT-USD perp on HL. Producer-grade financial tooling for music industry.

**Status**: Pending.

**Cost**: ~1 day (HyperEVM is EVM-compatible; EthChainClient extension)

---

### 4. TON 📱 (highest DIGM impact)
**Why**: Telegram-native, 100M+ users, deepest USDT-TON stable pool growing fastest in crypto. Asian retail + global Telegram distribution. Mobile-first by design.

**DIGM angle**: **Best-fit single chain for DIGM mobile-first distribution.** Telegram bots already host music players, NFT marketplaces, and crypto wallets natively. Wrapped HEAT on TON = Telegram-distributed yield-bearing stable for DIGM listeners. The DIGM app could integrate Telegram authentication and host artist channels with HEAT-denominated tipping.

**Status**: Pending.

**Cost**: ~4 days (TVM is unique — not EVM-compatible, custom serialization)

---

### 5. Solana (deepen) 🌊
**Why**: Already integrated as atomic swap chain. SPL HEAT is explicitly named in the DIGM premium gate doc. Jupiter aggregator amplification means any HEAT/USDC pool gets routed globally with zero ongoing maintenance. Sub-second finality for cross-chain arb.

**DIGM angle**: SPL HEAT is one of the two premium-gate options in DIGM. Solana-native users (~30M+ wallets) can satisfy the 1M HEAT gate without ever touching Fuego directly. Audius (music-focused chain) has cultural overlap and audio NFT communities are Solana-heavy.

**Status**: Adapter complete. Action items are operational (LP seeding on Orca/Raydium, optional Drift HEAT-USD perp listing).

**Cost**: Engineering ~0. Operational ~2 weeks LP bootstrapping.

---

### 6. Filecoin 📦 (DIGM storage fallback)
**Why**: DIGM's I2P seeding model works for popular content (many buyers = many seeders), but **degrades for niche/zero-buyer albums**. The DIGM doc's fallback ("artist XFG bounty → seeder rewards") pays *people*, but it doesn't replace missing storage. Filecoin is the production-grade decentralized storage layer that fills this gap: cold-store one canonical copy of every album, retrieve on-demand when the I2P swarm thins to zero.

**DIGM angle**: **The storage-fallback play.** Three concrete utilities:
- Under-seeded album cold storage — paid via XFG → FIL atomic swap → retrieval deal
- FIL retrieval markets accept payment in any token via FVM — XFG-denominated storage payments without leaving Fuego's wallet
- Storage providers earning HEAT yield by accepting wrapped HEAT for hot-tier retrieval

**Mode 4 angle**: FIL/USD adds another oracle triangulation pair. Modest mechanics value.

**Why Filecoin over Walrus/0G/Crust**: FVM is EVM-compatible — the adapter is ~1 day via EthChainClient extension. Walrus would require the Sui Move adapter first (~5-7 days). 0G is too new. Crust is too small.

**Status**: Pending.

**Cost**: ~1 day (EthChainClient extension with chainId 314)

---

### 7. Zano 🌒
**Why**: CryptoNote sibling — same crypto family as Fuego (ed25519, ring sigs, stealth addresses). `XmrChainClient` is ~70% reusable. Direct community overlap with Fuego's exact demographic (privacy-coin OGs). Brand cement.

**DIGM angle**: Privacy-aligned listeners. The "anonymous artist" support story (DIGM doc §4) maps naturally to Zano's audit-wallet primitive — artists could prove revenue without revealing identity. Cross-CryptoNote swap is a cultural statement.

**Status**: Pending.

**Cost**: ~1 day (clone XmrChainClient, swap RPC endpoints, registry wire-up)

---

### 8. Polygon PoS 🎨
**Why**: 500M+ wallet addresses, mature consumer-grade NFT/music marketplaces, Aave/Uniswap depth. Polygon is where Web3 mass-market consumer activity actually happens.

**DIGM angle**: **Music NFT / creator-economy bridge.** OpenSea, Catalog, Royal, Songcamp, Coop Records — most music-NFT activity happens on Polygon (or Polygon zkEVM). Adding Polygon brings DIGM into immediate proximity with the existing Web3 music collector demographic. Wrapped HEAT here = mass-market access via the most familiar chain to NFT collectors.

**Status**: Pending.

**Cost**: ~1 day (EthChainClient extension with chainId 137)

---

### 9. Cosmos IBC umbrella 🌌 (the "free chains" unlock)
**Why**: Single Tendermint adapter ⇒ 50+ chains via IBC. The highest-leverage "open up free chains" play available, equivalent to what EthChainClient did for the EVM family.

**What gets unlocked** (atomic swap or HEAT distribution eligible after this one adapter):
- **Penumbra** — IBC shielded DEX (privacy moat)
- **Namada** — multi-asset shielded pool (cross-privacy-tech swap)
- **Secret Network** — TEE-encrypted contracts (compute privacy)
- **Osmosis** — Cosmos's premier DEX (HEAT/ATOM/OSMO liquidity)
- **dYdX v4** — perp DEX (alternative to Hyperliquid for HEAT-USD)
- **Sei** — order-book L1 (better oracle quality than AMM-only chains)
- **Injective** — Cosmos-native financial primitives
- **Saga** — app-chain ecosystem (DIGM as its own chainlet?)
- **Celestia** — modular data availability (could publish Fuego state proofs)
- **Babylon** — BTC staking infrastructure
- **Stride** — liquid staking aggregator
- **Akash** — decentralized compute (DIGM seeder infrastructure?)
- **+ ~40 other production IBC chains**

**DIGM angle**: Akash for decentralized compute could host DIGM artist nodes. Celestia for publishing DIGM merkle anchors (§10) cheaply. Saga for spinning up DIGM as an app-chain if mobile-full-node ever proves infeasible. Penumbra/Namada/Secret as privacy moat distribution.

**Status**: Pending. First Cosmos adapter is a meaningful investment but the ROI is multiplicative.

**Cost**: ~4-5 days for the Tendermint adapter + first IBC chain wire-up. Each subsequent IBC chain: ~1 day to register.

---

## The "Free Chains" Picture

Once these 9 are shipped, the family-clustering effects mean these additional chains become near-free additions later:

### Via EthChainClient (already done)
After Arbitrum + Base + Polygon + Hyperliquid, each subsequent EVM chain is ~0.5-1 day:
- **OP Stack family**: Optimism, Mode, Zora, Worldchain, Soneium (Sony), Unichain, Lisk — 7 chains
- **Other EVM L2s**: BNB Chain, Avalanche, Mantle, Linea, Scroll, zkSync, Berachain — 7 chains
- **Polygon CDK family**: Manta Pacific, Astar zkEVM, Immutable zkEVM — 3 chains

**Total via EthChainClient**: 17+ chains accessible for ~10 dev-days total.

### Via Tendermint adapter (in this 9)
Once Cosmos IBC adapter is built, each subsequent IBC chain is ~1 day:
- 50+ production chains as above

**Total via Tendermint**: 50+ chains for ~5 days adapter + ~1d/chain registration.

### Via XmrChainClient (already done)
Each CryptoNote sibling is ~1 day:
- **Zano** (in the 9), **Wownero**, **Dero**, **Karbo** — 4 chains

**Total via XmrChainClient**: 4+ chains for ~4 dev-days total.

### Architectural insight

**EthChainClient + Tendermint adapter alone could give Fuego presence on ~70 chains within 30 dev-days.** The 9-pick above lays the foundation; family-clustering does the heavy lifting.

---

## Recommended ship order

| Phase | Chains | Days | Why this slot |
|---|---|---|---|
| 1 | Finish Arbitrum (with EIP-1559) | 3-4 | Already in flight; unlocks EVM cascade |
| 2 | Base | 0.5 | Cheapest EVM addition after Arbitrum |
| 3 | Polygon PoS | 1 | Music-NFT ecosystem bridge |
| 4 | Hyperliquid | 1 | Synthetic stability + oracle quality |
| 5 | Filecoin | 1 | DIGM storage fallback (under-seeded audio fallback) |
| 6 | Zano | 1 | XmrChainClient clone, brand cement |
| 7 | TON | 4 | DIGM Telegram distribution (highest DIGM ROI) |
| 8 | Solana deepening | 0 dev / ~2w ops | LP seeding on Orca/Raydium |
| 9 | Cosmos IBC adapter + Penumbra | 5 | "Free chains" multiplier kicks in |

**Engineering total**: ~16-18 dev-days for all 9 (down from ~22-25 with Mina).

**With family clustering aftermath**: ~70 chains accessible within another ~30 days.

---

## What this prioritization explicitly says NO to

To prevent scope creep, here's what stays off the top-9 even though they have merit:

- **BNB Chain** — covered by family clustering after EthChainClient is hot; not worth a top-9 slot
- **Sui / Aptos (Move)** — Pyth integration was strategic under flatcoin design; less critical under Mode 4 hard peg
- **Aztec / Railgun** — privacy moat is real but engineering cost is high; revisit after Cosmos IBC delivers Penumbra/Namada/Secret for similar value
- **Kaspa / Beam / Aleo / Iron Fish / Dero** — high novelty/privacy scores but lower DIGM-distribution overlap; ship via family clustering or one-off later
- **Berachain** — PoL alignment is interesting but speculative; ship via EVM family clustering when convenient
- **Audius** — community overlap with DIGM is real, but AUDIO token is on Solana/ETH (already accessible); no separate Audius chain integration needed

These remain valid targets for the *next* nine (or family-cluster sweeps), but the top-9 prioritizes maximum unlocked value per dev-day.

---

## Cross-doc decision matrix

How each pick scores across the three lenses:

| Chain | Mode 4 mechanics | Reach/Strategy/Privacy/Novelty | DIGM specific | Total weight |
|---|:-:|:-:|:-:|:-:|
| Arbitrum | ★★★★★ | ★★★★ | ★★★ | **highest** |
| Base | ★★★★★ | ★★★★★ | ★★★★ | **highest** |
| Hyperliquid | ★★★★ | ★★★★★ | ★★ | **very high** |
| TON | ★★★ | ★★★★★ | ★★★★★ | **very high** |
| Solana (deepen) | ★★★★★ | ★★★★ | ★★★★ | **very high** |
| Filecoin | ★★ | ★★★ | ★★★★★ | **DIGM storage fallback** |
| Zano | ★★ | ★★★★ | ★★ | **brand-essential** |
| Polygon PoS | ★★★ | ★★★★ | ★★★★ | **high** |
| Cosmos IBC | ★★★ | ★★★★ | ★★★ | **multiplier** |

---

## Sequencing principle

The 9 are ordered so each one **either**:
1. Unlocks family-clustering economics for many free chains (Arbitrum, Cosmos IBC, Zano)
2. Adds a categorically-unique capability nothing else can (Hyperliquid synthetic stability, Mina mobile light-client, TON Telegram distribution)
3. Serves all three Fuego layers simultaneously (Base, Polygon)

No slot in the 9 is "just another EVM chain" or "just another privacy chain" — each pick has multi-axis justification.

---

## Companion documents

- `docs/design/SWAPXFG_CHAIN_RECOMMENDATIONS.md` — full ranked list by Mode 4 mechanics
- `docs/design/SWAPXFG_CHAIN_REACH_STRATEGY.md` — full ranked list by reach/strategy/privacy/novelty
- `docs/implementation/FUEGO_HEAT_FINAL_DESIGN.md` — HEAT v2.0 Mode 4 flatcoin spec
- `docs/DIGM_FUEGO_ARCHITECTURE_GUIDE.md` — DIGM platform context (mobile-first, Telegram-aligned)

This document is the **final synthesis**. If only one chain-recommendation doc gets read before a sprint planning session, this is the one.
