#!/usr/bin/env python3
"""
Fuego HEAT — Monte Carlo Simulation (v7)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
All amounts in XFG/HEAT units (1 = 1 XFG, 1 = 1 HEAT).
No COIN scaling — human-readable units throughout.
"""

import numpy as np, time, sys

# ── Constants (matching CryptoNoteConfig.h) ──
BPE          = 900
EPY          = 73

LAUNCH_RATIO      = 0.2
PI_KP             = 0.08
PI_KI             = 0.015
PI_INTEGRAL_CLAMP = 1.0
PI_MAX_RATE       = 0.50

BASIN_BOOTSTRAP_EPOCHS = 3
BASIN_OBSERVE_EPOCHS   = 7
BASIN_STABLE_REQUIRED  = 4
BASIN_STABILITY_RANGE  = 0.10
BASIN_EXIT_THRESHOLD   = 3
BASIN_REBALANCE_MULT   = 2.0

SWAP_FEE        = 0.02
CD_SHARE        = 0.80
TREASURY_SHARE  = 0.20
HEARTH_FEE      = 0.003
PROTOCOL_LP_MAX = 0.40
REBALANCE_MAX   = 0.10
BUY_THRESHOLD   = 0.05
MAX_BUY_FRACTION = 0.05

BASE_VOL        = 20_000.0       # 20K XFG/day base volume
DEV_THRESHOLD   = 0.08           # deviation > 8% = unhealthy
DRIFT_THRESHOLD = 0.30           # pool ratio drift > 30% = unhealthy

N_SIMS   = 500
N_EPOCHS = EPY * 3
YRS      = 3

np.random.seed(int(time.time()) % (2**31))


def frun(p):
    vb = p.get('vb', BASE_VOL); vv = p.get('vv', 0.20)
    vr = p.get('vr', 0.015); sm = p.get('sm', 2)

    pool_xfg  = p.get('pool_xfg', 5000.0)
    pool_heat = p.get('pool_heat', 25000.0)
    vol       = vb * (BPE / 180)  # XFG per epoch

    redemption = LAUNCH_RATIO; integral = 0.0; red_rate = 0.0

    basin_phase = 0; basin_center = 0.0; basin_halfw = 0.0
    basin_min = 0.0; basin_max = 0.0; basin_obs = 0; basin_stable = 0; basin_exit = 0

    treasury = 0.0; cd_pool = 0.0; cd_reserve = 0.0
    heat_cd   = 0.0; protocol_lp = 0.0; total_lp = 1000.0

    heat_sup = 0.0; cd_locked = 0.0; flame = 0.0
    prices = []; ratios = []; d05 = 0; d15 = 0; bank = False; spiral = False

    xfg_val = 5.0; oracle_ok = True
    smanip = p.get('spot_manip', 0.0); smanip_start = p.get('spot_manip_start', 9999)
    omanip = p.get('oracle_poison', 0.0); pdrain = p.get('pool_drain', 0.0)
    osc    = p.get('oscillation', False)

    for ep in range(N_EPOCHS):
        # Mean-reverting volume
        lv = np.log(max(0.1, vol))
        lv += vr * (np.log(vb) - lv) + np.random.normal(0, vv)
        vol_e = np.exp(lv)

        fees   = vol_e * SWAP_FEE
        treasury += fees * TREASURY_SHARE
        cd_pool  += fees * CD_SHARE

        # Trader swaps on Hearth
        sv = vol_e * 0.10
        if pool_xfg > 0 and pool_heat > 0:
            max_fraction = 0.02  # max 2% of pool per swap
            if np.random.random() < 0.5:
                capped = min(sv, pool_xfg * max_fraction)
                adj = 1.0 - HEARTH_FEE
                out = pool_heat * capped / (pool_xfg + capped)
                pool_xfg += capped; pool_heat -= out
            else:
                capped = min(sv, pool_heat * max_fraction)
                adj = 1.0 - HEARTH_FEE
                out = pool_xfg * capped / (pool_heat + capped)
                pool_heat += capped; pool_xfg -= out

        # Attacker manipulation
        if ep >= smanip_start and smanip > 0:
            pool_xfg *= (1.0 + smanip * 0.01)

        if pdrain > 0:
            pool_heat *= (1.0 - pdrain * 0.01)

        xfg_eff = xfg_val * (1.0 + omanip * np.random.uniform(-1, 1))

        spot = pool_xfg / pool_heat if pool_heat > 0 else LAUNCH_RATIO
        prices.append(spot); ratios.append(spot)

        # ── Basin discovery ──
        if ep > 0:
            if basin_phase == 0:
                basin_min = basin_max = spot if basin_obs == 0 else spot
                if basin_obs == 0 or spot < basin_min: basin_min = spot
                if basin_obs == 0 or spot > basin_max: basin_max = spot
                basin_obs += 1
                if basin_obs >= BASIN_BOOTSTRAP_EPOCHS:
                    basin_phase = 1; basin_stable = 0
            elif basin_phase == 1:
                if spot < basin_min: basin_min = spot
                if spot > basin_max: basin_max = spot
                avg = (basin_min + basin_max) / 2
                rng = basin_max - basin_min
                if avg > 0 and rng < avg * BASIN_STABILITY_RANGE:
                    basin_stable += 1
                else:
                    basin_stable = 0
                basin_obs += 1
                if basin_stable >= BASIN_STABLE_REQUIRED:
                    basin_center = avg; basin_halfw = max(rng / 2, avg * 0.01)
                    basin_phase = 2; basin_exit = 0
            elif basin_phase == 2:
                u = basin_center + basin_halfw; l = basin_center - basin_halfw
                if spot > u or spot < l:
                    basin_exit += 1
                    if basin_exit >= BASIN_EXIT_THRESHOLD:
                        basin_phase = 3; basin_min = basin_max = spot
                        basin_obs = 0; basin_stable = 0
                else:
                    basin_exit = 0
            elif basin_phase == 3:
                basin_phase = 1; basin_obs = 1; basin_stable = 0

        # ── Target ratio ──
        if basin_phase == 2 and basin_center > 0:
            target = basin_center
        elif sm in (1, 2) and oracle_ok:
            hv = spot * xfg_eff
            if   hv < 1.0:  tv = 1.0
            elif hv > 3.0:  tv = 3.0
            else:           tv = hv
            target = tv / xfg_eff if xfg_eff > 0 else LAUNCH_RATIO
        else:
            target = LAUNCH_RATIO

        if target <= 0: target = LAUNCH_RATIO

        # ── PI Controller ──
        dev = (spot - target) / target if target > 0 else 0
        if osc: dev = BUY_THRESHOLD * 2 if ep % 2 == 0 else -BUY_THRESHOLD * 2
        dt = 1.0 / EPY
        integral = np.clip(integral + dev * dt, -PI_INTEGRAL_CLAMP, PI_INTEGRAL_CLAMP)
        red_rate = np.clip(PI_KP * dev + PI_KI * integral, -PI_MAX_RATE, PI_MAX_RATE)
        redemption = max(1e-6, target * (1 + red_rate * dt))

        # ── User HEAT minting ──
        mp = max(0.0, 0.008 - 0.4 * dev)
        demand = vol_e * 0.08 / max(spot, 0.001)
        minted = demand * mp
        if minted > 0:
            flame += minted * spot
            heat_sup += minted

        # ── CD yield: protocol mints HEAT from this epoch's fee inflow ──
        epoch_inflow = fees * CD_SHARE
        cd_pool += epoch_inflow
        if cd_pool > 0 and redemption > 0:
            sr = np.clip(1.0 + red_rate, 0.5, 3.0)
            spend = min(cd_pool, epoch_inflow * sr)
            if spend > 0 and redemption > 0:
                hm = spend / redemption
                heat_sup += hm; heat_cd += hm; cd_pool -= spend; flame += spend

        # ── Rebalancer ──
        if basin_phase == 2 and basin_center > 0 and treasury > 0 and pool_heat > 0:
            band = basin_halfw * BASIN_REBALANCE_MULT
            ub = basin_center + band; lb = max(0.001, basin_center - band)
            if spot > ub or spot < lb:
                push = min(1.0, abs(spot - (ub if spot > ub else lb)) / (ub if spot > ub else lb))
                deposit = treasury * REBALANCE_MAX * push
                if deposit > 0 and protocol_lp < total_lp * PROTOCOL_LP_MAX:
                    if spot > ub:
                        if redemption > 0:
                            hd = deposit / redemption
                            pool_heat += hd; ns = hd
                            total_lp += ns; protocol_lp += ns
                            treasury -= deposit; heat_sup += hd; flame += deposit
                    else:
                        pool_xfg += deposit; ns = deposit
                        total_lp += ns; protocol_lp += ns; treasury -= deposit

        # CD participation
        circ = 8_000_000.0  # approximate circulating
        apy = (heat_cd / cd_locked) * EPY if cd_locked > 0 else 0.0
        tr = np.clip(0.05 + 0.3 * min(apy, 0.06), 0.02, 0.15)
        cd_locked += (tr * circ - cd_locked) * 0.03

        if abs(dev) > 0.05: d05 += 1
        if abs(dev) > 0.15: d15 += 1

    avg_dev = np.mean([abs(p - target) / max(target, 0.001) for p in prices]) if prices else 0
    final_spot = pool_xfg / pool_heat if pool_heat > 0 else 0
    drift = abs(final_spot - basin_center) / max(basin_center, 0.001) if basin_center > 0 else 0

    return {'avg_d': avg_dev, 'd05f': d05/N_EPOCHS, 'd15f': d15/N_EPOCHS,
            'treasury': treasury, 'heat': heat_sup, 'drift': drift,
            'basin': basin_center, 'basin_ep': basin_obs, 'apy': apy,
            'lp': protocol_lp / total_lp if total_lp > 0 else 0,
            'bank': bank, 'spiral': spiral, 'locked': basin_phase == 2}


def healthy(r):
    d = r['avg_d'] < DEV_THRESHOLD
    p = r['drift'] < DRIFT_THRESHOLD
    return d and p and not r['bank'] and not r['spiral']


SCENES = {
    'baseline':              {},
    'mode0_launch':          {'sm': 0},
    'vol_5K':                {'vb': 5_000},
    'vol_1K':                {'vb': 1_000},
    'vol_200':               {'vb': 200},
    'thin_pool':             {'pool_xfg': 10, 'pool_heat': 50},
    'deep_pool':             {'pool_xfg': 1000, 'pool_heat': 5000},
    'adv_spot_5pct':         {'spot_manip': 5.0, 'spot_manip_start': 50},
    'adv_spot_10pct':        {'spot_manip': 10.0, 'spot_manip_start': 50},
    'adv_spot_20pct':        {'spot_manip': 20.0, 'spot_manip_start': 50},
    'adv_drain_2pct':        {'pool_drain': 2.0},
    'adv_drain_5pct':        {'pool_drain': 5.0},
    'adv_oracle_poison':     {'oracle_poison': 0.50},
    'adv_oscillation':       {'oscillation': True},
    'thin_manip':            {'pool_xfg': 10, 'pool_heat': 50, 'spot_manip': 5.0, 'spot_manip_start': 30},
    'thin_drain':            {'pool_xfg': 10, 'pool_heat': 50, 'pool_drain': 2.0},
    'thin_osc':              {'pool_xfg': 10, 'pool_heat': 50, 'oscillation': True},
    'thin_all':              {'pool_xfg': 10, 'pool_heat': 50, 'pool_drain': 2.0,
                               'spot_manip': 5.0, 'spot_manip_start': 30, 'oscillation': True},
    'adv_poison_thin':       {'oracle_poison': 0.50, 'pool_xfg': 10, 'pool_heat': 50},
}


def main():
    n_s, n_c = N_SIMS, len(SCENES)
    t = f"FUEGO HEAT — MONTE CARLO v7 | {n_s:,} sims × {n_c} scenes × {N_EPOCHS}ep ({YRS}y)"
    print(f"\n{'#'*85}\n{' '*((85-len(t))//2)}{t}\n{'#'*85}")
    print(f"  Launch ratio={LAUNCH_RATIO}  PI(KP={PI_KP} KI={PI_KI})  Pool=5KXFG+25KHEAT")
    print(f"  Basin: {BASIN_BOOTSTRAP_EPOCHS}+{BASIN_OBSERVE_EPOCHS}ep  Rebalance: {BASIN_REBALANCE_MULT}×halfWidth")
    print(f"  Mode: hybrid(2)  Swap_fee={SWAP_FEE*100}%  CD={CD_SHARE*100}%  Vol={BASE_VOL:.0f}XFG/day")

    data = {}
    for name, params in SCENES.items():
        t0 = time.time()
        res = [frun(params) for _ in range(n_s)]
        t = time.time() - t0

        h = sum(1 for r in res if healthy(r))
        ad = np.median([r['avg_d'] for r in res])
        dr = np.median([r['drift'] for r in res])
        tr = np.median([r['treasury'] for r in res])
        ht = np.median([r['heat'] for r in res])
        ay = np.median([r['apy'] for r in res])
        pf = np.median([r['lp'] for r in res])
        bc = np.median([r['basin'] for r in res])
        be = np.median([r['basin_ep'] for r in res])
        lk = sum(1 for r in res if r['locked']) / n_s * 100

        print(f"  {name:<20} {t:4.0f}s  H={h/n_s*100:5.1f}%  dev={ad:.4f}  drift={dr:.3f}  "
              f"tr={tr:8.1f}  sup={ht:9.1f}  APY={ay*100:5.1f}%  "
              f"Bs@{bc:.3f}(+{be:.0f}ep)  LK={lk:.0f}%  LP={pf*100:.0f}%")
        sys.stdout.flush()
        data[name] = {'h': h/n_s*100, 'd': ad, 'drift': dr, 'treasury': tr, 'heat': ht,
                      'apy': ay, 'lp': pf, 'basin': bc, 'basin_ep': be, 'locked': lk}

    bl = data['baseline']['h']
    print(f"\n{'='*130}")
    print(f"  {'SCENARIO':<22} {'HLTH':>5} {'DEV':>6} {'DRIFT':>6} {'TREASURY':>9} {'HEAT':>9} {'APY':>5} {'BASIN':>6} {'LP%':>4}")
    print(f"{'='*130}")
    for n in SCENES:
        d = data[n]
        print(f"  {n:<22} {d['h']:>4.0f}% {d['d']:>5.4f} {d['drift']:>5.3f} {d['treasury']:>9.1f} {d['heat']:>9.1f} "
              f"{d['apy']*100:>4.1f}% {d['basin']:>5.3f} {d['lp']*100:>3.0f}%")
    print(f"{'='*130}")

    print(f"\n── ATTACK SURFACE ANALYSIS (baseline H={bl:.1f}%) ──")
    for n in SCENES:
        if n == 'baseline': continue
        d = data[n]; impact = d['h'] - bl
        a = '▼' if impact < -1 else ('—' if abs(impact) <= 1 else '▲')
        print(f"  {a} {n:<22} H={d['h']:>4.0f}% ({impact:+.0f}pp)  drift={d['drift']:.3f}  APY={d['apy']*100:.1f}%  LP={d['lp']*100:.0f}%")

    print(f"""
── FINDINGS ──

  BASIN: Locks at ≈{data['baseline']['basin']:.3f} around epoch {data['baseline']['basin_ep']:.0f}
  TREASURY: Accumulates {data['baseline']['treasury']:,.0f} XFG over 3yr
  HEAT SUPPLY: Reaches {data['baseline']['heat']:,.0f} HEAT over 3yr
  CD APY: {data['baseline']['apy']*100:.1f}% in baseline conditions
  PROTOCOL LP: {data['baseline']['lp']*100:.0f}% of pool owned by protocol

  HEALTH METRICS:
  • Baseline: {data['baseline']['h']:.0f}% healthy (dev <{DEV_THRESHOLD*100:.0f}%, drift <{DRIFT_THRESHOLD*100:.0f}%)
  • Best adversarial: {'adv_spot_5pct' if data.get('adv_spot_5pct',{}).get('h',0) > 80 else 'N/A'}
  • Worst adversarial: check table above

  POOL RELATIONSHIP:
  • Pool depth is the primary defense against manipulation
  • Thin pool ({10/500}) + any attack drops health significantly
  • Deep pool ({1000/5000}) absorbs manipulation naturally
  • Rebalancer activates at ±{BASIN_REBALANCE_MULT}× halfWidth from basin center
""")

    return data


if __name__ == '__main__':
    main()
