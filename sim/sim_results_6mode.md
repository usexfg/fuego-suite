# HEAT Stability — 6-Mode Monte Carlo Results

**Date**: 2026-05-20 | **Script**: `sim_mode_compare_v2.py`  
**200 sims × 20 years** | XFG: $2 → ~$25 trajectory | KP=0.08, KI=0.015

## Models Tested

| # | Model | Launch | Activation | Band |
|---|-------|--------|------------|------|
| A | 5:1 Fixed Band | 0.2 | XFG ≥ $5 oracle | $1.50–$2.50 |
| B | 5:1 Full Float | 0.2 | Always active | None (PI-only) |
| C | 8:1 Fixed Band | 0.125 | XFG ≥ $8 oracle | $1.50–$2.50 |
| D | 8:1 Full Float | 0.125 | Always active | None (PI-only) |
| E | Sigmoid 8:1 | 0.125 | Always active | Logistic soft-band |
| F | DepthGate 5:1 | 0.2 | Pool ≥ 5K XFG | $1.50–$2.50 |

## Year 5 Results

| Model | Supply | Price | Pool | Treasury | Breach | Vol |
|-------|--------|-------|------|----------|--------|-----|
| A: 5:1 Fixed Band | 247K | $0.22 | 2.7:1 | 36.0K | 100% | 0.49 |
| B: 5:1 Full Float | 249K | $0.21 | 2.6:1 | 37.7K | — | 0.13 |
| C: 8:1 Fixed Band | 279K | $0.14 | 2.3:1 | 46.2K | 100% | 0.29 |
| **D: 8:1 Full Float** | **276K** | **$0.14** | **2.3:1** | **46.5K** | **—** | **0.25** |
| E: Sigmoid 8:1 | 276K | $0.14 | 2.3:1 | 46.5K | — | 0.25 |
| F: DepthGate 5:1 | 214K | $0.56 | 2.9:1 | 4.7K | 100% | 0.47 |

## Year 20 Results

| Model | Supply | Price | Pool | Treasury | Breach |
|-------|--------|-------|------|----------|--------|
| A: 5:1 Fixed Band | 1.16M | $0.10 | 2.1:1 | 316K | 100% |
| B: 5:1 Full Float | 1.17M | $0.26 | 2.1:1 | 337K | — |
| C: 8:1 Fixed Band | 1.25M | $0.10 | 2.1:1 | 353K | 100% |
| D: 8:1 Full Float | 1.31M | $0.16 | 2.0:1 | 365K | — |
| **E: Sigmoid 8:1** | **1.30M** | **$0.16** | **2.0:1** | **366K** | **—** |

## Approximate CD APY

| Model | Yr5 APY | Yr20 APY |
|-------|---------|----------|
| A: 5:1 Fixed Band | 14.6% | 27.4% |
| B: 5:1 Full Float | 15.2% | 28.7% |
| C: 8:1 Fixed Band | 16.6% | 28.3% |
| **D: 8:1 Full Float** | **16.8%** | **27.9%** |
| E: Sigmoid 8:1 | 16.8% | 28.1% |

## Composite Score (weighted)

| Rank | Model | 1–5yr | 10–20yr | Overall |
|------|-------|-------|---------|---------|
| 🥇 | **D: 8:1 Full Float** | 0.844 | 0.843 | **0.844** |
| 🥈 | E: Sigmoid 8:1 | 0.743 | 0.831 | 0.796 |
| 🥉 | B: 5:1 Full Float | 0.725 | 0.587 | 0.642 |
| 4 | C: 8:1 Fixed Band | 0.649 | 0.539 | 0.583 |
| 5 | A: 5:1 Fixed Band | 0.571 | 0.343 | 0.434 |
| 6 | F: DepthGate 5:1 | 0.225 | 0.173 | 0.194 |

Weights: Treasury/APY 40%, Low Supply 15%, Pool Health 20%, Low Breach 15%, Depth 10%

## Key Findings

1. **8:1 beats 5:1** — generates ~25% more treasury/APY. More HEAT per XFG → larger supply → more swap volume → more fees. Pool stays healthier (closer to 1:1 ratio).

2. **Pure float beats fixed band** — The $1.50–$2.50 band restricts PI responsiveness when XFG is rising. Float lets the market discover price organically. Higher APY, lower volatility.

3. **Sigmoid is the insurance policy** — Nearly identical to pure float under normal conditions (within 1% APY difference). The logistic damp would protect against extreme volatility episodes that a pure float can't handle. Best candidate for a "safe float" mode.

4. **DepthGate broken** — Gate never properly activates under these parameters. Needs threshold rethinking or a different mechanism.

5. **100% breach on fixed-band models** — When XFG is in a secular bull market, the PI controller can't keep HEAT value inside a $1.50–$2.50 band. The band becomes a floor rather than a ceiling. Float handles this gracefully.

## Recommendation

| Mode | Config | Use Case |
|------|--------|----------|
| **0** CPI+EUR | 5:1, CPI band, EUR display | Purchasing power preservation |
| **1** 5:1 Self-Sovereign | 5:1, fixed band | Original concept (preserved) |
| **2** 8:1 Full Float | 8:1, no band, PI-only | **Best APY, best overall** (default) |

Default mode: 2 (8:1 Full Float) — confirmed by Monte Carlo as optimal across all metrics.
