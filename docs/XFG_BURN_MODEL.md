# XFG Burn Model

Every HEAT mint requires XFG to be burned. The burn is permanent and irreversible — destroyed XFG never re-enter circulation. But the value they carry doesn't disappear; it is redirected, collateralized, and compounded across two destination pools.

---

## The 50/50 Split

When XFG is burned during a HEAT mint, the amount is split equally:

| Destination | Share | Purpose |
|---|---|---|
| **Eternal Flame** | 50% | Permanent deflation — reduces total XFG supply forever |
| **Sovereign Wealth Fund** | 50% | Collateral reserve backing HEAT issuance and ecosystem liquidity |

---

## Eternal Flame

The Eternal Flame is a one-way vault. XFG sent here are added to a cumulative deflation counter (`ethereal_xfg`) and never move again. Every burn makes the remaining XFG supply scarcer. The counter is publicly verifiable on-chain — anyone can confirm how much XFG has been permanently retired.

## Sovereign Wealth Fund

The SWF acts as the collateral backbone of the HEAT economy. XFG allocated here are not idle — they are actively converted into HEAT on a recurring schedule (every 8 epochs), where they serve as:

- **Collateral backing** for HEAT stablecoin issuance
- **Liquidity reserves** for cross-chain bridge operations
- **Capital reserves** for the broader Fuego ecosystem

The SWF also earns protocol-level yield through its proportional share of Hearth AMM LP fees, which compound back into the reserve over time.

---

## Swap Fee Treasury (20%)

Separately, 20% of all epoch swap fees from the Hearth exchange are routed to the Treasury Reserve. Each epoch:

1. **80%** is converted to HEAT and deposited into the CD APY Pool — funding the yield floor for Certificate of Deposit holders
2. The XFG consumed in this conversion follows the same burn split: half to the Eternal Flame, half to the SWF
3. **20%** is deposited as balanced LP into the Hearth AMM pool, deepening liquidity

This creates a flywheel: swap fees → collateral growth → deeper liquidity → more swap volume.

---

## Summary

```
User burns XFG to mint HEAT
        │
        ├── 50% → Eternal Flame (permanent deflation)
        │
        └── 50% → Sovereign Wealth Fund
                    │
                    ├── Collateral backing for HEAT
                    ├── Cross-chain bridge reserves
                    └── Protocol yield from LP fees
```

Every XFG burned makes the remaining supply scarcer while strengthening the collateral base that supports the entire HEAT ecosystem.
