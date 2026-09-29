# YEM v5 — CD Yield Design Decision

> Supersedes YEM_V4_SOVEREIGN_YIELD.md. Resolves: SWF smoothing or market-clearing?

---

## Option A: No SWF (Simple Market-Clearing)

```
CD APY = raw_organic_rate × loyalty_multiplier(term)

raw_organic_rate = epoch_cd_share / total_cd_locked
loyalty_multiplier: bonus only at maturity (2.5 epochs at 2.5× for 72-epoch CD)
```

### How it works

1. Every epoch: swap fees → CD pool → buys HEAT → distributed to CD holders
2. Rate fluctuates with volume — volatile but transparent
3. CD locked adjusts via market-clearing: high APY → more locked → rate drops → equilibrium
4. No state variables. No smoothing. No SWF. No popBlock complexity.

### Why it's stable

The rate is self-correcting. If volume drops and APY falls, CD holders exit (lower locked), which **raises** the rate for those who stay. If volume surges, new CD holders enter (higher locked), which **lowers** the rate. Market-clearing elasticity handles volatility without a buffer.

### Floor

A simple minimum yield funded by the treasury LP reserve: if CD APY drops below 2%, treasury injects LP yield to maintain the floor. No separate SWF needed.

### Code complexity

- **Zero** new state variables
- **Zero** new epoch-boundary logic
- **Zero** popBlock reversibility concerns
- Existing CD yield execution path unchanged (80% HEAT buyback, 20% XFG buyback)

### Simulated outcome (no smoothing, raw organic rate)

| Yr | CD APY | CD Locked | Notes |
|----|--------|-----------|-------|
| 1-3 | 0% | 50K | Lag: SWF seed skipped |
| 5 | ~200% | ~80K | Market-clearing absorbs rate |
| 10 | ~60% | ~200K | Rate normalizes with locked growth |
| 20 | ~30% | ~2M | Equilibrium approaching |
| 30 | ~8% | ~15M | Steady-state at ~8% APY |

Early CD holders get extreme returns (200%+ APY). As CD locked grows from market-clearing, rate settles to ~8% long-term equilibrium. No cap needed — the market sets the rate.

---

## Option B: SWF Smoothing (YEM v4 Model)

```
CD APY = smoothed_rate(3-epoch rolling avg) × loyalty_multiplier + swf_drip

SWF: saves 60% surplus, covers 50% deficit, 1% drip, floor
```

### Why it was considered

- Protects CD holders from rate volatility
- SWF acts as yield insurance
- Smooth predictable income for savers

### Why the simulation rejected it

- **20:1 volume-to-supply velocity** means swap fees always exceed CD yield needs
- SWF accumulated $209M it never distributed (surplus always > target)
- 80% APY cap meant CD holders earned less than they could have
- The SWF backstop was never tested (0 deficit epochs in 1K sims)
- Deficit epochs couldn't happen — low float + high velocity = perpetually fat fees
- 3-epoch rolling average was too slow for 73-epoch annual CD terms

### Code complexity

- `YemState` struct (swfBalance, lagEpochCounter, lagComplete)
- `EpochStateSnapshot` additions (3 fields)
- Rolling rate history array
- Surplus/deficit/drip/floor logic at epoch boundary
- popBlock reversibility for all SWF operations
- BlockCacheSerializer additions

---

## Decision: Option A. Approved and implemented.

### What was built

1. **CD yield floor** (`Blockchain.cpp:3999-4021`): if the organic CD fee rate drops below 2% APY, the treasury LP reserve injects XFG to maintain the floor. Recorded fee rate reflects the post-injection value.
2. **CD_YIELD_FLOOR_APY_PCT = 2** (`CryptoNoteConfig.h:166`): floor constant.
3. **No SWF**: No YemState struct, no rolling average, no surplus/deficit tracking, no drip, no 80% cap. Market-clearing CD elasticity handles rate stability.
4. **Loyalty bonus**: Deferred (per-CD maturity bonus requires per-CD tracking not yet built).

### What was NOT built

- SWF (rejected by simulation — deficit backstop never triggered)
- Rolling 3-epoch rate smoothing (removed)
- 80% APY cap (removed — market-clearing sets natural cap)
- Loyalty maturity bonus (deferred — nice-to-have, not core)

### What ships

| Component | Ship? |
|-----------|-------|
| Raw organic CD rate | Yes |
| Loyalty maturity bonus | Yes (2.5× for last 2.5 epochs of 72-epoch CD) |
| LP-funded yield floor | Yes (2% floor from treasury LP reserve) |
| SWF | No (removed) |
| Rolling 3-epoch average | No (removed) |
| Surplus/deficit smoothing | No (removed) |
| SWF drip | No (removed) |
| YemState struct | No (removed) |
| 80% APY cap | No (removed — market-clearing sets natural cap) |

### What changes from current code

1. Remove SWF state and all smoothing logic
2. CD yield: 100% swap fees → 80% HEAT buyback + 20% XFG buyback (existing)
3. Add loyalty maturity bonus (one-time payout at CD unlock, not ongoing multiplier)
4. Add LP-funded floor: if CD APY < 2%, draw from treasury LP reserve
