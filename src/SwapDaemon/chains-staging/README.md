# Chain Integration Staging Area

This directory tracks upcoming chain integrations for Fuego SwapXFG.
Each subdirectory contains a README with the implementation plan for that chain.

## Priority Order

| Priority | Chain | Type | Rationale |
|----------|-------|------|-----------|
| P0 | DOGE | UTXO | Top 10 market cap, same pattern as LTC, massive retail volume |
| P0 | AVAX | EVM | Top 20 market cap, header-only inheritance, DeFi ecosystem access |
| P1 | ZEC | UTXO | Privacy alignment with Fuego, mature atomic swap ecosystem |
| P1 | DASH | UTXO | Established atomic swap ecosystem, privacy features |
| P2 | ZANO | UTXO | Privacy-focused, ASIC-resistant, niche community |
| P2 | TON | Non-standard | Telegram integration, massive user base, custom implementation |
| P2 | SIA | Non-standard | Filecoin ecosystem, placeholder exists, needs full implementation |

## Implementation Sequence

1. **Phase 1 (P0)**: DOGE + AVAX — quickest wins, lowest effort
2. **Phase 2 (P1)**: ZEC + DASH — medium effort, privacy alignment
3. **Phase 3 (P2)**: ZANO + TON + SIA — higher effort, niche value

## Directory Structure

```
chains-staging/
├── README.md          ← this file (overall plan)
├── doge/
│   └── README.md      ← Dogecoin implementation plan
├── avax/
│   └── README.md      ← Avalanche implementation plan
├── zec/
│   └── README.md      ← Zcash implementation plan
├── dash/
│   └── README.md      ← Dash implementation plan
├── zano/
│   └── README.md      ← Zano implementation plan
├── ton/
│   └── README.md      ← TON implementation plan
└── sia/
    └── README.md      ← Sia implementation plan
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
