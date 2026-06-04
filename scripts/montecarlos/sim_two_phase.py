#!/usr/bin/env python3
"""
Fuego HEAT — Two-Phase Stability Model
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Phase 1: Bootstrap (fixed 0.2) until XFG ≥ $3 AND oracle data exists
Phase 2: Activated ($1-$3 floating band via swapxfg oracle TWAP)
"""

import numpy as np, time, sys

# ── Constants (matching CryptoNoteConfig.h) ──
BPE       = 900; EPY = 73; LAUNCH = 0.2
KP=0.08; KI=0.015; SWAP_FEE=0.02; CD_SHARE=0.80; TREASURY_SH=0.20

ACTIVATION_PRICE = 3.00  # XFG must be ≥ $3 to activate
N_SIMS    = 200
N_EPOCHS  = EPY * 10  # 10 years

np.random.seed(int(time.time()) % (2**31))

def run_two_phase(spot_manip=0.0, pool_xfg=5000.0, pool_heat=25000.0):
    """Runs the two-phase model once. Returns median metrics."""
    supplies = []; treasuries = []; ratios = []; phases = []; heat_values = []
    
    for s in range(N_SIMS):
        rng = np.random.RandomState(hash(str(s)) % (2**31))
        px = pool_xfg; ph = pool_heat
        redemption = LAUNCH; integral = 0.0
        treasury = 0.0; cd_pool = 0.0; heat_sup = 0.0
        protocol_lp = 0.0; total_lp = 1000.0
        xfg_price = 0.01
        oracle_active = False  # becomes true when bridges start reporting
        oracle_bridge_start = rng.randint(EPY, EPY*2)  # bridges come online 1-2 years in
        oracle_stale = False
        
        heat_yr = [0.0]; treas_yr = [0.0]; ratio_yr = [5.0]
        phase_activated_epochs = 0; rebalance_count = 0
        
        for ep in range(N_EPOCHS):
            t = ep / N_EPOCHS
            # XFG price: privacy coin trajectory (slow start, exponential later)
            xfg_price = 0.01 * np.exp(4 * t**1.5) * (1 + rng.normal(0, 0.25))
            
            # Oracle: becomes active after bridges come online, with intermittent staleness
            if ep >= oracle_bridge_start:
                oracle_active = True
                oracle_stale = rng.random() < 0.05  # 5% chance of stale oracle per epoch
                oracle_price = xfg_price * (1 + rng.normal(0, 0.05))  # accurate ±5%
            else:
                oracle_active = False
                oracle_stale = True
            
            vol_mult = np.clip(xfg_price / 0.01, 0.3, 5.0)
            vol_e = 20000 * (BPE / 180) * vol_mult * np.exp(rng.normal(0, 0.10))
            
            fees = vol_e * SWAP_FEE
            treasury += fees * TREASURY_SH
            cd_pool += fees * CD_SHARE

            # Hearth swaps
            sv = vol_e * 0.05
            if px > 0 and ph > 0:
                for _ in range(3):
                    if rng.random() < 0.5:
                        capped = min(sv/3, px * 0.005)
                        out = ph * capped / (px + capped)
                        px += capped; ph -= out
                    else:
                        capped = min(sv/3, ph * 0.005)
                        out = px * capped / (ph + capped)
                        ph += capped; px -= out

            if ep >= 50 and spot_manip > 0:
                px *= (1.0 + spot_manip * 0.01)
            
            spot = px / max(ph, 0.001)
            
            # ── TARGET RATIO (two-phase) ──
            activated = (oracle_active and not oracle_stale 
                        and oracle_price >= ACTIVATION_PRICE)
            
            if activated:
                phase_activated_epochs += 1
                xfg_price_cents = oracle_price * 100  # convert to cents scale
                heatValue = spot * oracle_price  # HEAT value in dollars
                
                if heatValue < 1.00:
                    targetValue = 1.00
                elif heatValue > 3.00:
                    targetValue = 3.00
                else:
                    targetValue = heatValue
                
                target = targetValue / oracle_price
            else:
                target = LAUNCH  # 0.2 bootstrap
            
            target = max(0.00001, min(10.0, target))
            
            # ── PI Controller ──
            dev = (spot - target) / target if target > 0 else 0
            dt = 1.0 / EPY
            integral = np.clip(integral + dev * dt, -1.0, 1.0)
            red_rate = np.clip(KP * dev + KI * integral, -0.50, 0.50)
            redemption = max(1e-6, target * (1 + red_rate * dt))
            
            # User mint
            mp = max(0, 0.008 - 0.4 * dev)
            heat_sup += vol_e * 0.08 / max(spot, 0.001) * mp
            
            # CD yield: buy from Hearth
            if cd_pool > 0 and ph > 0:
                sr = np.clip(1.0 + red_rate, 0.2, 3.0)
                spend = min(cd_pool, cd_pool * sr)
                hb = ph * spend / (px + spend)
                if 0 < hb < ph * 0.95:
                    px += spend; ph -= hb
                    heat_sup += hb; cd_pool -= spend
            
            # Rebalancer
            if ph > 0 and treasury > 0 and (px / max(ph, 0.001)) > 3:
                reb = min(treasury * 0.10, px * 0.03)
                if redemption > 0:
                    heat_dep = reb / redemption
                    ph += heat_dep; total_lp += heat_dep
                    protocol_lp += heat_dep; treasury -= reb
                    heat_sup += heat_dep; rebalance_count += 1
            
            if ep % EPY == 0 and ep > 0:
                heat_yr.append(heat_sup); treas_yr.append(treasury)
                ratio_yr.append(px / max(ph, 0.001))
        
        supplies.append(heat_yr); treasuries.append(treasury); ratios.append(ratio_yr[-1])
        phases.append(phase_activated_epochs / N_EPOCHS * 100)
        if heat_yr and heat_yr[-1] > 0 and treasuries[-1] > 0:
            heat_values.append(redemption if activated else 0)
    
    mx = min(len(s) for s in supplies)
    return {
        'supply_yr1': np.median([s[1] for s in supplies if len(s) > 1]),
        'supply_yr5': np.median([s[5] for s in supplies if len(s) > 5]),
        'supply_yr10': np.median([s[-1] for s in supplies]),
        'treasury_yr10': np.median(treasuries),
        'pool_ratio': np.median(ratios),
        'activated_pct': np.median(phases),
        'rebalance_avg': np.median([rebalance_count]),
    }


print(f"\n{'#'*85}")
print(f"  FUEGO HEAT — TWO-PHASE STABILITY MODEL ({N_SIMS} sims × {N_EPOCHS} epochs)")
print(f"  Phase 1: fixed 0.2 (XFG < ${ACTIVATION_PRICE} or no oracle)")
print(f"  Phase 2: $1-$3 floating band (XFG ≥ ${ACTIVATION_PRICE} + swapxfg data)")
print(f"  CD buy from Hearth + Treasury rebalance | 80/20 split | 2% swap fee")
print(f"{'#'*85}\n")

for label, sp_man, px, ph in [
    ('BASELINE', 0.0, 5000.0, 25000.0),
    ('SpotManip5%', 5.0, 5000.0, 25000.0),
    ('ThinPool+Manip', 5.0, 500.0, 2500.0),
]:
    t0 = time.time()
    r = run_two_phase(sp_man, px, ph)
    dt = time.time() - t0
    
    cov = r['treasury_yr10'] / max(1e-6, LAUNCH) / max(1, r['supply_yr10']) * 100
    
    print(f"  {label:<18} {dt:3.0f}s  "
          f"Supp: {r['supply_yr1']:>7,.0f}→{r['supply_yr5']:>9,.0f}→{r['supply_yr10']:>10,.0f}  "
          f"Pool: {r['pool_ratio']:.1f}:1  "
          f"Treas: {r['treasury_yr10']:>8,.0f}  "
          f"Cov: {cov:.0f}%  "
          f"Activated: {r['activated_pct']:.0f}%  "
          f"Reb: {r['rebalance_avg']:.0f}×")
    sys.stdout.flush()

print(f"\n── ANALYSIS ──")
print(f"  Phase 1 (bootstrap 0.2): runs until XFG ≥ ${ACTIVATION_PRICE} + oracle data exists")
print(f"  Phase 2 ($1-$3 band): HEAT tracks real market value, PI maintains band")
print(f"  Both phases: CD yield buys from Hearth, treasury rebalances pool")
print(f"  Oracle stale/offline → falls back to Phase 1 (fixed 0.2)")
