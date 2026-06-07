#!/usr/bin/env python3
# ⚠️ SUPERSEDED by sim_final_v18.py — this sim hallucinates a HEAT→XFG burn path
# that does not exist. HEAT is mint-only. The AMM pool is the sole exit.
"""HEAT — Mint Premium: 0% vs 5% (8:1 Full Float, 150 sims × 20yr)"""

import numpy as np, time, sys

EPY = 73; N_SIMS = 150; N_EPOCHS = EPY * 21; N_YEARS = 21
KP = 0.08; KI = 0.015; SWAP_FEE = 0.02; CD_SHARE = 0.80; TREASURY_SH = 0.20
SEED_BASE = 20260520

def xfg_price_model(t, rng):
    base = 2.0 + 1.0 * np.exp(0.15 * t)
    return base * float(1.0 + rng.normal(0.0, 0.25))

def sim_core(rng, premium):
    launch = 0.125; px = 5000.0; ph = 25000.0
    redemption = launch; integral = 0.0
    treasury = 0.0; cd_pool = 0.0; heat_sup = 0.0
    total_mint = 0.0; total_premium_burned = 0.0
    oracle_bridge_start = int(rng.randint(EPY//2, EPY*2))
    launch_twap = None

    supply_y = np.zeros(N_YEARS); treas_y = np.zeros(N_YEARS)
    mint_y   = np.zeros(N_YEARS); premium_y = np.zeros(N_YEARS)
    ratio_y  = np.zeros(N_YEARS); price_y = np.zeros(N_YEARS)

    for ep in range(N_EPOCHS):
        yr = ep // EPY
        if yr >= N_YEARS: break

        xfg_price = xfg_price_model(ep/EPY, rng)
        oracle_active = ep >= oracle_bridge_start
        oracle_stale = rng.random() < 0.05 if oracle_active else True

        vol_mult = np.clip(xfg_price / 2.0, 0.3, 5.0)
        vol_e = 20000.0 * vol_mult * float(np.exp(rng.normal(0.0, 0.10)))
        fees = vol_e * SWAP_FEE
        treasury += fees * TREASURY_SH
        cd_pool  += fees * CD_SHARE

        sv = vol_e * 0.05
        if px > 0 and ph > 0:
            for _ in range(3):
                if rng.random() < 0.5:
                    cap = min(sv/3.0, px * 0.005)
                    ph -= ph * cap / (px + cap); px += cap
                else:
                    cap = min(sv/3.0, ph * 0.005)
                    px -= px * cap / (ph + cap); ph += cap

        spot = px / max(ph, 0.001)
        if launch_twap is None and oracle_active and not oracle_stale and spot > 0:
            launch_twap = spot

        if launch_twap is not None and spot > 0.0001:
            target = launch * launch_twap / spot
        else:
            target = launch

        target = np.clip(target, 0.00001, 10.0)
        dev = (spot - target) / max(target, 0.00001)
        dt = 1.0 / EPY
        integral = np.clip(integral + dev * dt, -1.0, 1.0)
        red_rate = np.clip(KP * dev + KI * integral, -0.50, 0.50)
        redemption = max(1e-6, target * (1.0 + red_rate * dt))

        # User mint
        mp = max(0.0, 0.008 - 0.4 * dev)
        mint_vol = vol_e * 0.08 / max(spot, 0.001) * mp
        premium_burned = mint_vol * premium
        heat_sup += mint_vol * (1.0 - premium)
        total_mint += mint_vol * (1.0 - premium)
        total_premium_burned += premium_burned
        treasury += premium_burned  # premium goes to treasury
        mint_y[yr] += mint_vol * (1.0 - premium)
        premium_y[yr] += premium_burned

        # CD yield
        if cd_pool > 0 and ph > 0:
            sr = np.clip(1.0 + red_rate, 0.2, 3.0)
            spend = min(cd_pool, cd_pool * sr)
            hb = ph * spend / (px + spend)
            if 0 < hb < ph * 0.95:
                px += spend; ph -= hb; heat_sup += hb; cd_pool -= spend

        # Rebalancer
        if ph > 0 and treasury > 0 and (px / max(ph, 0.001)) > 3.0:
            reb = min(treasury * 0.10, px * 0.03)
            if redemption > 0:
                ph += reb / redemption; treasury -= reb; heat_sup += reb / redemption

        supply_y[yr]  = heat_sup
        treas_y[yr]   = treasury
        ratio_y[yr]   = spot
        price_y[yr]   = redemption

    returns = []
    for i in range(2, min(11, N_YEARS)):
        if price_y[i-1] > 0:
            returns.append((price_y[i] - price_y[i-1]) / price_y[i-1])
    volatility = np.std(returns) if returns else 0.0

    return {
        'supply': supply_y, 'treasury': treas_y,
        'ratio': ratio_y, 'price': price_y,
        'mint': mint_y, 'premium_burned': premium_y,
        'volatility': volatility,
    }


print(f"\n  MINT PREMIUM COMPARISON: 0% vs 5% (8:1 Full Float, {N_SIMS} sims)")
print(f"  {'='*70}")

for premium, label in [(0.0, '0%'), (0.05, '5%')]:
    print(f"\n  Premium: {label}")
    print(f"  {'─'*60}")
    t0 = time.time()
    batch = []
    for s in range(N_SIMS):
        rng = np.random.RandomState(SEED_BASE + s)
        batch.append(sim_core(rng, premium))
    dt = time.time() - t0

    med = {}
    for key in ['supply','treasury','ratio','price','mint','premium_burned']:
        med[key] = [np.median([b[key][y] for b in batch]) for y in range(N_YEARS)]
    med['volatility'] = np.median([b['volatility'] for b in batch])

    def fmt(v):
        if v >= 1e6: return f"{v/1e6:6.2f}M"
        return f"{v/1e3:6.1f}K"

    for y in [5, 10, 20]:
        s = med['supply'][y]; t = med['treasury'][y]
        m = med['mint'][y]; p = med['premium_burned'][y]
        apy = t / max(s, 1) * 100
        print(f"  Yr{y:>2}: supply={fmt(s)}  treasury={fmt(t)}  APY={apy:5.2f}%  "
              f"minted={fmt(m)}  premium_burned={fmt(p)}  "
              f"pool={med['ratio'][y]:.1f}:1  vol={med['volatility']:.3f}  "
              f"({dt:3.0f}s)")

print(f"\n  ── EFFECTS ──")
print(f"  Removing premium: minting = pool rate. No funnel to AMM.")
print(f"  More minting, less swap volume, lower fees, lower CD APY.")
print(f"  Premium burns ~2-5% of mint XFG → treasury, funding CD yield.")
print()
