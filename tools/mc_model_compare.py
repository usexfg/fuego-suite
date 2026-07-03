#!/usr/bin/env python3
"""Monte Carlo: compare HEAT stability models over 30 years.

Models:
  A = Current: no PI, no peg arb, no target. HEAT=$1.58 hard peg. Ratio floats free.
  B = Dynamic window: mint when XFG hot (>1.3× rolling 2yr avg), buy when cold (<0.7×).
  C = Old PI + peg arb (mode 2 full float at 8:1)
  D = Old PI + peg arb (mode 1 banded $1.50-$2.50 at 5:1)
"""
import random, math, time, statistics
from collections import deque

# ── constants ──────────────────────────────────────────────────────────
EPOCHS_PER_YEAR = 180
YEARS = 30
N_SIMS = 80
HEAT_PEG = 1.58
FEE_BPS = 30

INIT_XFG = 5_000  # pool XFG (COIN units, ×10^7 atomic)
INIT_HEAT = 5_000
INIT_COIN = 10_000_000  # 1 COIN = 10^7 atomic

XFG_PRICE_START = 1.58
XFG_PRICE_VOL = 0.30    # annual vol (higher = more dynamic range)
XFG_PRICE_DRIFT = 0.02  # slight upward drift (2%/yr)

ANNUAL_VOL_FRAC = 0.50  # 50% of pool turns over per year organically
VOL_SIGMA = 0.3

# ── AMM ─────────────────────────────────────────────────────────────────
def amm_swap(amt_in, rin, rout, fee_bps):
    amt_fee = amt_in * (10000 - fee_bps) // 10000
    if rin + amt_fee == 0: return 0
    return (rout * amt_fee) // (rin + amt_fee)

def pool_ratio(rx, rh):
    return rx / rh if rh > 0 else 1.0

# ── price path ──────────────────────────────────────────────────────────
def price_path():
    n = YEARS * EPOCHS_PER_YEAR
    p = [XFG_PRICE_START]
    for _ in range(n):
        s = random.gauss(0, XFG_PRICE_VOL / math.sqrt(EPOCHS_PER_YEAR))
        d = XFG_PRICE_DRIFT / EPOCHS_PER_YEAR
        p.append(p[-1] * math.exp(d + s))
    return p

def org_vol(rx, rh):
    base = min(rx, rh) * ANNUAL_VOL_FRAC / EPOCHS_PER_YEAR
    noise = max(0, 1 + random.gauss(0, VOL_SIGMA))
    return max(10, int(base * noise))

# ── MODEL A: Current ────────────────────────────────────────────────────
def sim_a(prices):
    rx = INIT_XFG * INIT_COIN
    rh = INIT_HEAT * INIT_COIN
    heat_supply = INIT_HEAT * INIT_COIN
    treasury = 0
    cd_pool = 0
    total_fees = 0
    ratios = []
    apys = []
    last_apy_epoch = 0
    cd_distributed_year = 0

    for e, xfg_usd in enumerate(prices):
        ratio = pool_ratio(rx, rh)
        ratios.append(ratio)

        # Organic trading
        vol = org_vol(rx, rh)
        if random.random() < 0.5:
            if vol <= rx:
                ho = amm_swap(vol, rx, rh, FEE_BPS)
                if ho > 0 and ho <= rh: rx += vol; rh -= ho
        else:
            if vol <= rh:
                xo = amm_swap(vol, rh, rx, FEE_BPS)
                if xo > 0 and xo <= rx: rh += vol; rx -= xo

        # Fee from organic volume: ~0.3% of vol
        fee = int(vol * FEE_BPS / 10000)
        total_fees += fee
        treasury += int(fee * 0.2)
        cd_pool += int(fee * 0.8)  # 80% to CD yield

        # CD yield: buy HEAT from pool
        if cd_pool > 0 and cd_pool <= rx and rh > 0:
            hb = amm_swap(cd_pool, rx, rh, 0)
            if 0 < hb <= rh:
                rx += cd_pool; rh -= hb
                cd_distributed_year += hb
            cd_pool = 0

        # Annual APY snapshot
        if e > 0 and e % EPOCHS_PER_YEAR == 0 and e > last_apy_epoch:
            if rh > 0:
                apy = cd_distributed_year / rh  # already accumulated over a year
                apys.append(min(apy, 1.0))  # cap at 100%
            cd_distributed_year = 0
            last_apy_epoch = e

    avg_ratio = statistics.mean(ratios[-EPOCHS_PER_YEAR*15:]) if len(ratios) > EPOCHS_PER_YEAR*15 else ratios[-1]
    arr = ratios[-EPOCHS_PER_YEAR*YEARS//2:]
    peak = arr[0] if arr else 1
    max_dd = 0
    for r in arr:
        if r > peak: peak = r
        dd = (peak - r) / peak if peak > 0 else 0
        if dd > max_dd: max_dd = dd

    return dict(
        final_ratio=ratio,
        avg_ratio=avg_ratio,
        max_drawdown=max_dd,
        ratio_range=(min(arr), max(arr)) if arr else (1,1),
        xfg_price_range=(min(arr)*HEAT_PEG, max(arr)*HEAT_PEG) if arr else (0,0),
        cd_apy_avg=statistics.mean(apys) if apys else 0,
        cd_apy_final=apys[-1] if apys else 0,
        total_fees=total_fees/INIT_COIN,
        heat_inflation=(heat_supply - INIT_HEAT*INIT_COIN)/(INIT_HEAT*INIT_COIN),
        treasury=treasury/INIT_COIN,
        ratios=ratios,
    )

# ── MODEL B: Dynamic window ────────────────────────────────────────────
def sim_b(prices):
    rx = INIT_XFG * INIT_COIN
    rh = INIT_HEAT * INIT_COIN
    th = 500 * INIT_COIN       # treasury HEAT (protocol-owned) — 500 COIN seed
    tx = 1000 * INIT_COIN      # treasury XFG — 1000 COIN seed
    heat_supply = INIT_HEAT * INIT_COIN
    cd_pool = 0
    total_fees = 0
    ratios = []
    apys = []
    last_apy_epoch = 0
    cd_distributed_year = 0
    window = deque(maxlen=EPOCHS_PER_YEAR * 2)
    up_mul, lo_mul = 1.30, 0.70

    for e, xfg_usd in enumerate(prices):
        ratio = pool_ratio(rx, rh)
        ratios.append(ratio)
        window.append(xfg_usd)

        vol = org_vol(rx, rh)
        if random.random() < 0.5:
            if vol <= rx:
                ho = amm_swap(vol, rx, rh, FEE_BPS)
                if ho > 0 and ho <= rh: rx += vol; rh -= ho
        else:
            if vol <= rh:
                xo = amm_swap(vol, rh, rx, FEE_BPS)
                if xo > 0 and xo <= rx: rh += vol; rx -= xo

        fee = int(vol * FEE_BPS / 10000)
        total_fees += fee
        # 20% treasury, 80% CD yield
        tx += int(fee * 0.2)
        cd_pool += int(fee * 0.8)

        # Dynamic window intervention
        if len(window) > EPOCHS_PER_YEAR:
            ra = sum(window) / len(window)
            upper = ra * up_mul
            lower = ra * lo_mul
            arb = int(min(rx, rh) * 0.03)
            if arb > 0:
                if xfg_usd > upper and th > 0:
                    # XFG hot: sell HEAT from protocol reserve
                    sell = min(arb, th, rh // 4)
                    if sell > 0:
                        xo = amm_swap(sell, rh, rx, FEE_BPS)
                        if xo > 0 and xo <= rx:
                            rh += sell; rx -= xo; th -= sell
                elif xfg_usd < lower and tx >= arb:
                    # XFG cold: buy HEAT into reserve
                    hb = amm_swap(arb, rx, rh, FEE_BPS)
                    if hb > 0 and hb <= rh:
                        rx += arb; rh -= hb; tx -= arb; th += hb

        # CD yield
        if cd_pool > 0 and cd_pool <= rx and rh > 0:
            hb = amm_swap(cd_pool, rx, rh, 0)
            if 0 < hb <= rh:
                rx += cd_pool; rh -= hb
                cd_distributed_year += hb
            cd_pool = 0

        if e > 0 and e % EPOCHS_PER_YEAR == 0 and e > last_apy_epoch:
            if rh > 0:
                apy = cd_distributed_year / rh  # already accumulated over a year
                apys.append(min(apy, 1.0))
            cd_distributed_year = 0
            last_apy_epoch = e

    avg_ratio = statistics.mean(ratios[-EPOCHS_PER_YEAR*15:]) if len(ratios) > EPOCHS_PER_YEAR*15 else ratios[-1]
    arr = ratios[-EPOCHS_PER_YEAR*YEARS//2:]
    peak = arr[0] if arr else 1
    max_dd = 0
    for r in arr:
        if r > peak: peak = r
        dd = (peak - r) / peak if peak > 0 else 0
        if dd > max_dd: max_dd = dd

    return dict(
        final_ratio=ratio,
        avg_ratio=avg_ratio,
        max_drawdown=max_dd,
        ratio_range=(min(arr), max(arr)) if arr else (1,1),
        xfg_price_range=(min(arr)*HEAT_PEG, max(arr)*HEAT_PEG) if arr else (0,0),
        cd_apy_avg=statistics.mean(apys) if apys else 0,
        cd_apy_final=apys[-1] if apys else 0,
        total_fees=total_fees/INIT_COIN,
        heat_inflation=(heat_supply - INIT_HEAT*INIT_COIN)/(INIT_HEAT*INIT_COIN),
        treasury=tx/INIT_COIN,
        treasury_heat=th/INIT_COIN,
        ratios=ratios,
    )

# ── MODEL C: Old PI mode 2 (full float, 8:1) ───────────────────────────
def sim_c(prices):
    ratio_seed = 8.0
    rx = int(INIT_XFG * INIT_COIN * ratio_seed)
    rh = INIT_HEAT * INIT_COIN
    heat_supply = INIT_HEAT * INIT_COIN
    treasury = 0
    cd_pool = 0
    total_fees = 0
    ratios = []
    apys = []
    last_apy_epoch = 0
    cd_distributed_year = 0

    rp = ratio_seed      # redemption price (XFG/HEAT)
    integral = 0.0
    kp, ki = 0.08, 0.015
    clamp = 1.0
    base_rate = 0.50
    abs_max = 10.0

    for e, xfg_usd in enumerate(prices):
        ratio = pool_ratio(rx, rh)
        ratios.append(ratio)

        vol = org_vol(rx, rh)
        if random.random() < 0.5:
            if vol <= rx:
                ho = amm_swap(vol, rx, rh, FEE_BPS)
                if ho > 0 and ho <= rh: rx += vol; rh -= ho
        else:
            if vol <= rh:
                xo = amm_swap(vol, rh, rx, FEE_BPS)
                if xo > 0 and xo <= rx: rh += vol; rx -= xo

        fee = int(vol * FEE_BPS / 10000)
        total_fees += fee
        treasury += int(fee * 0.2)
        cd_pool += int(fee * 0.8)

        # Epoch: PI + peg arb
        if e > 0 and e % EPOCHS_PER_YEAR == 0:
            mp = ratio
            if rp > 0:
                dev = (mp - rp) / rp
            else:
                dev = 0
            integral += dev
            integral = max(-clamp, min(clamp, integral))
            rate = kp * dev + ki * integral
            dm = abs(dev)
            cap = max(base_rate, min(dm * 0.5, abs_max))
            rate = max(-cap, min(cap, rate))
            rp2 = rp * (1 - rate)
            if rp2 < 0.000001: rp2 = 0.000001
            rp = rp2

            # Peg arb 40 rounds
            for _ in range(40):
                pr = rx / rh if rh > 0 else 999
                if rp <= 0: break
                d = (pr - rp) / rp
                if abs(d) < 0.005: break
                arb = int(min(rx, rh) * 0.03)
                if arb <= 0: break
                if d > 0:
                    # pool XFG-heavy: mint HEAT, sell to pool
                    hm = int(arb / rp) if rp > 0 else 0
                    if hm > 0 and hm <= rh // 4:
                        xg = amm_swap(hm, rh, rx, FEE_BPS)
                        if xg > 0 and xg <= rx:
                            rh += hm; rx -= xg; heat_supply += hm
                else:
                    if treasury >= arb:
                        hb = amm_swap(arb, rx, rh, FEE_BPS)
                        if hb > 0 and hb <= rh:
                            rx += arb; rh -= hb; treasury -= arb

        # CD yield
        if cd_pool > 0 and cd_pool <= rx and rh > 0:
            hb = amm_swap(cd_pool, rx, rh, 0)
            if 0 < hb <= rh:
                rx += cd_pool; rh -= hb
                cd_distributed_year += hb
            cd_pool = 0

        if e > 0 and e % EPOCHS_PER_YEAR == 0 and e > last_apy_epoch:
            if rh > 0:
                apy = cd_distributed_year / rh  # already accumulated over a year
                apys.append(min(apy, 1.0))
            cd_distributed_year = 0
            last_apy_epoch = e

    avg_ratio = statistics.mean(ratios[-EPOCHS_PER_YEAR*15:]) if len(ratios) > EPOCHS_PER_YEAR*15 else ratios[-1]
    arr = ratios[-EPOCHS_PER_YEAR*YEARS//2:]
    peak = arr[0] if arr else 1
    max_dd = 0
    for r in arr:
        if r > peak: peak = r
        dd = (peak - r) / peak if peak > 0 else 0
        if dd > max_dd: max_dd = dd

    return dict(
        final_ratio=ratio,
        avg_ratio=avg_ratio,
        max_drawdown=max_dd,
        ratio_range=(min(arr), max(arr)) if arr else (1,1),
        xfg_price_range=(min(arr)*HEAT_PEG, max(arr)*HEAT_PEG) if arr else (0,0),
        cd_apy_avg=statistics.mean(apys) if apys else 0,
        cd_apy_final=apys[-1] if apys else 0,
        total_fees=total_fees/INIT_COIN,
        heat_inflation=(heat_supply - INIT_HEAT*INIT_COIN)/(INIT_HEAT*INIT_COIN),
        treasury=treasury/INIT_COIN,
        ratios=ratios,
    )

# ── MODEL D: Old PI mode 1 (banded $1.50-$2.50, 5:1) ──────────────────
def sim_d(prices):
    ratio_seed = 5.0
    rx = int(INIT_XFG * INIT_COIN * ratio_seed)
    rh = INIT_HEAT * INIT_COIN
    heat_supply = INIT_HEAT * INIT_COIN
    treasury = 0
    cd_pool = 0
    total_fees = 0
    ratios = []
    apys = []
    last_apy_epoch = 0
    cd_distributed_year = 0

    rp = ratio_seed
    integral = 0.0
    kp, ki = 0.08, 0.015
    clamp = 1.0
    base_rate = 0.50
    abs_max = 10.0

    for e, xfg_usd in enumerate(prices):
        ratio = pool_ratio(rx, rh)
        ratios.append(ratio)

        vol = org_vol(rx, rh)
        if random.random() < 0.5:
            if vol <= rx:
                ho = amm_swap(vol, rx, rh, FEE_BPS)
                if ho > 0 and ho <= rh: rx += vol; rh -= ho
        else:
            if vol <= rh:
                xo = amm_swap(vol, rh, rx, FEE_BPS)
                if xo > 0 and xo <= rx: rh += vol; rx -= xo

        fee = int(vol * FEE_BPS / 10000)
        total_fees += fee
        treasury += int(fee * 0.2)
        cd_pool += int(fee * 0.8)

        # Epoch: banded PI
        if e > 0 and e % EPOCHS_PER_YEAR == 0:
            # Band target: $1.50 - $2.50
            hv = ratio * xfg_usd
            if xfg_usd > 0:
                if hv < 1.50:   tr = 1.50 / xfg_usd
                elif hv > 2.50: tr = 2.50 / xfg_usd
                else:           tr = ratio
            else:
                tr = ratio_seed

            mp = ratio
            dev = (mp - tr) / tr if tr > 0 else 0
            integral += dev
            integral = max(-clamp, min(clamp, integral))
            rate = kp * dev + ki * integral
            dm = abs(dev)
            cap = max(base_rate, min(dm * 0.5, abs_max))
            rate = max(-cap, min(cap, rate))
            rp2 = tr * (1 - rate)
            if rp2 < 0.000001: rp2 = 0.000001
            rp = rp2

            for _ in range(40):
                pr = rx / rh if rh > 0 else 999
                if rp <= 0: break
                d = (pr - rp) / rp
                if abs(d) < 0.005: break
                arb = int(min(rx, rh) * 0.03)
                if arb <= 0: break
                if d > 0:
                    hm = int(arb / rp) if rp > 0 else 0
                    if hm > 0 and hm <= rh // 4:
                        xg = amm_swap(hm, rh, rx, FEE_BPS)
                        if xg > 0 and xg <= rx:
                            rh += hm; rx -= xg; heat_supply += hm
                else:
                    if treasury >= arb:
                        hb = amm_swap(arb, rx, rh, FEE_BPS)
                        if hb > 0 and hb <= rh:
                            rx += arb; rh -= hb; treasury -= arb

        if cd_pool > 0 and cd_pool <= rx and rh > 0:
            hb = amm_swap(cd_pool, rx, rh, 0)
            if 0 < hb <= rh:
                rx += cd_pool; rh -= hb
                cd_distributed_year += hb
            cd_pool = 0

        if e > 0 and e % EPOCHS_PER_YEAR == 0 and e > last_apy_epoch:
            if rh > 0:
                apy = cd_distributed_year / rh  # already accumulated over a year
                apys.append(min(apy, 1.0))
            cd_distributed_year = 0
            last_apy_epoch = e

    avg_ratio = statistics.mean(ratios[-EPOCHS_PER_YEAR*15:]) if len(ratios) > EPOCHS_PER_YEAR*15 else ratios[-1]
    arr = ratios[-EPOCHS_PER_YEAR*YEARS//2:]
    peak = arr[0] if arr else 1
    max_dd = 0
    for r in arr:
        if r > peak: peak = r
        dd = (peak - r) / peak if peak > 0 else 0
        if dd > max_dd: max_dd = dd

    return dict(
        final_ratio=ratio,
        avg_ratio=avg_ratio,
        max_drawdown=max_dd,
        ratio_range=(min(arr), max(arr)) if arr else (1,1),
        xfg_price_range=(min(arr)*HEAT_PEG, max(arr)*HEAT_PEG) if arr else (0,0),
        cd_apy_avg=statistics.mean(apys) if apys else 0,
        cd_apy_final=apys[-1] if apys else 0,
        total_fees=total_fees/INIT_COIN,
        heat_inflation=(heat_supply - INIT_HEAT*INIT_COIN)/(INIT_HEAT*INIT_COIN),
        treasury=treasury/INIT_COIN,
        ratios=ratios,
    )

# ── Run ─────────────────────────────────────────────────────────────────
if __name__ == '__main__':
    t0 = time.time()
    results = {k: [] for k in 'ABCD'}

    for sim_i in range(N_SIMS):
        random.seed(42 + sim_i)
        prices = price_path()
        results['A'].append(sim_a(prices))

        random.seed(42 + sim_i)
        prices2 = price_path()
        results['B'].append(sim_b(prices2))

        random.seed(42 + sim_i)
        prices3 = price_path()
        results['C'].append(sim_c(prices3))

        random.seed(42 + sim_i)
        prices4 = price_path()
        results['D'].append(sim_d(prices4))

        if (sim_i + 1) % 20 == 0:
            print(f"  {sim_i+1}/{N_SIMS} sims ({time.time()-t0:.1f}s)", flush=True)

    # Summary
    labels = {'A': 'Current (no target)', 'B': 'Dynamic window 30%',
              'C': 'Old PI mode 2 (8:1)', 'D': 'Old PI mode 1 (band $1.50-2.50)'}

    def ms(vals):
        m = statistics.mean(vals)
        s = statistics.stdev(vals) if len(vals) > 1 else 0
        return m, s

    print("\n" + "="*120)
    print(f"  Monte Carlo: {N_SIMS} sims × {YEARS} years — HEAT model comparison")
    print(f"  HEAT = ${HEAT_PEG} (hard peg), XFG price start=${XFG_PRICE_START}, vol={XFG_PRICE_VOL*100:.0f}%/yr")
    print("="*120)
    hdr = f"  {'Model':<28} {'Avg Ratio':>10} {'HEAT Infl.':>10} {'Max DD':>8} {'Ratio Span':>12} {'CD APY':>8} {'Treasury':>10}"
    unt = f"  {'':28} {'(XFG/HEAT)':>10} {'(%/yr)':>10} {'(%)':>8} {'(low-high)':>12} {'(%)':>8} {'(XFG)':>10}"
    print(hdr)
    print(unt)
    print("-"*120)

    for m in 'ABCD':
        rs = results[m]
        ar_m, ar_s = ms([r['avg_ratio'] for r in rs])
        hi_m, hi_s = ms([r['heat_inflation']*100 for r in rs])
        dd_m, dd_s = ms([r['max_drawdown']*100 for r in rs])
        rr = [r['ratio_range'] for r in rs]
        rlo = statistics.mean([x[0] for x in rr])
        rhi = statistics.mean([x[1] for x in rr])
        ap_m, ap_s = ms([r['cd_apy_avg']*100 for r in rs])
        tr_m, tr_s = ms([r['treasury'] for r in rs])

        print(f"  {labels[m]:<28} {ar_m:>7.3f}±{ar_s:<6.3f} {hi_m:>6.1f}%±{hi_s:<5.1f}% {dd_m:>5.1f}%±{dd_s:<4.1f}% {rlo:>4.2f}-{rhi:<7.2f} {ap_m:>5.2f}%±{ap_s:<4.2f}% {tr_m:>7.0f}±{tr_s:<6.0f}")

    print("="*120)
    print(f"\n  Elapsed: {time.time()-t0:.1f}s")

    # Price swing analysis
    a_ratios = results['A']
    a_ranges = [r['xfg_price_range'] for r in a_ratios]
    all_lows = [x[0] for x in a_ranges]
    all_highs = [x[1] for x in a_ranges]
    print(f"\n  Model A XFG price range (last 15yr): "
          f"${statistics.mean(all_lows):.2f} — ${statistics.mean(all_highs):.2f}")

    # Best model per metric
    print(f"\n  Best model by metric:")
    metrics = [
        ('Lowest HEAT inflation', 'heat_inflation', False),
        ('Lowest max drawdown', 'max_drawdown', False),
        ('Highest CD APY', 'cd_apy_avg', True),
        ('Highest treasury', 'treasury', True),
    ]
    for label, key, want_max in metrics:
        if want_max:
            best = max('ABCD', key=lambda m: ms([r[key] for r in results[m]])[0])
        else:
            best = min('ABCD', key=lambda m: ms([r[key] for r in results[m]])[0])
        val_m, val_s = ms([r[key] for r in results[best]])
        print(f"    {label:<30} {labels[best]:<28} ({val_m:.4f})")
