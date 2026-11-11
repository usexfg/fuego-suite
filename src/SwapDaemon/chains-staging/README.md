# Chain Integration Staging Area

This directory tracks upcoming chain integrations for Fuego SwapXFG.
Each subdirectory contains a README with the implementation plan for that chain.

## Priority Order

Ground truth is `SwapPairCatalog.h`; this table tracks what is *not yet* live.
DOGE (20), AVAX (14), ZEC (22) and DASH (21) shipped from staging to ACTIVE —
plans below are kept for reference only. LIVE REMAINING: ZANO, TON, SIA, DOT.

| Priority | Chain | Type | Rationale |
|----------|-------|------|-----------|
| P1 | ZANO | Cryptonote | Privacy-focused, ASIC-resistant, dedicated pair catalog (id 24 STAGED), Monero-pattern client |
| P2 | TON | Non-standard | Telegram integration, massive user base, custom implementation (id 27 STAGED) |
| P2 | SIA | Non-standard | Filecoin ecosystem, placeholder exists, needs full implementation (id 17 STAGED) |
| P2 | DOT | Non-standard | Polkadot geonization, custom implementation (id 28 STAGED) |

## Implementation Sequence

1. **DONE (moved to catalog ACTIVE)**: DOGE (20), AVAX (14), ZEC (22), DASH (21)
2. **Phase 1 (P1)**: ZANO — Monero-family cryptonote client
3. **Phase 2 (P2)**: TON + SIA + DOT — higher effort, niche value
4. **Parked**: SPARK (see `spark/README.md`) — direct pair parked behind the open
   SE release-primitive question; Route A (managed rail, no new pair) remains the recommendation.

## Directory Structure

```
chains-staging/
├── README.md          ← this file (overall plan)
├── doge/              ← SHIPPED (catalog id 20, ACTIVE)
├── avax/              ← SHIPPED (catalog id 14, ACTIVE)
├── zec/               ← SHIPPED (catalog id 22, ACTIVE)
├── dash/              ← SHIPPED (catalog id 21, ACTIVE)
├── zano/
│   └── README.md      ← Zano implementation plan (P1)
├── ton/
│   └── README.md      ← TON implementation plan
├── sia/
│   └── README.md      ← Sia implementation plan
└── spark/
    └── README.md      ← Spark (spark.money) Bitcoin L2 plan (parked, see Route A/B)
```

## Template Mapping

Each chain's README references the existing implementation it is based on:

| New Chain | Template Chain | Pattern |
|-----------|---------------|---------|
| DOGE | Litecoin | UTXO full client (copy LTC, change network params) |
| AVAX | Polygon | EVM header-only inheritance |
| ZEC | Bitcoin | UTXO full client (copy BTC, change address prefixes) |
| DASH | Litecoin | UTXO full client (copy LTC, change network params) |
| ZANO | Litecoin | UTXO full client (copy LTC, change network params) |
| TON | Solana | Custom non-standard client |
| SIA | (empty) | Custom non-standard client (placeholder exists) |
| ZANO | Monero | Cryptonote-family client (catalog family CRYPTONOTE, id 24) — the earlier "Litecoin copy" line here was wrong |
| DOT | (none) | Custom non-standard client (Substrate) |

## Testing Strategy

Each chain integration must include:
1. Unit tests in `src/SwapDaemon/<chain>/tests/`
2. Integration test in `src/SwapDaemon/tests/`
3. PriceOracle seed rate entry
4. SwapPair enum entry in `SwapTypes.h`
5. Chain registry entry in `SwapDaemon.cpp`
6. Dashboard chain selector option in `swapxfg.html`
7. Coin icon in `dashboard/static/coin-icons/`

## Adding a New Chain Checklist

- [ ] Create `src/SwapDaemon/<chain>/` directory with client files
- [ ] Add `SwapPair::<CHAIN>` enum value in `SwapTypes.h`
- [ ] Add config fields to `ChainClientConfig` in `SwapDaemon.h`
- [ ] Add registration logic in `SwapDaemon.cpp`
- [ ] Add seed rate in `PriceOracle.cpp`
- [ ] Add timelock block time in `SwapTimelock.cpp`
- [ ] Add HTLC hash function in `AdaptorSwap.cpp` (if UTXO)
- [ ] Add dashboard chain option in `swapxfg.html`
- [ ] Add chain icon to `dashboard/static/coin-icons/`
- [ ] Add icon mapping in `swapxfg.js` CHAIN_INFO
- [ ] Write unit tests
- [ ] Write integration tests
- [ ] Update `ChainRegistry` if needed
- [ ] Update `RpcServer` if needed
- [ ] Update `OfferManager` if needed
