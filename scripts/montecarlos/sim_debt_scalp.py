#!/usr/bin/env python3
"""Debt payoff sim: elevated scalp rates to clear OG bond debt.

OG deposits: 254,250 XFG locked, 80% target APY = 203,400 XFG/yr interest.
Uses realistic organic mint demand model + arb mints.
"""
import numpy as np, sys

EPY=73;N_YEARS=21;N_EPOCHS=EPY*N_YEARS;HEAT_PEG=1.58;N_SIMS=30

OG_PRINCIPAL=254250; OG_ANNUAL_INTEREST=int(OG_PRINCIPAL*0.80)
TOTAL_DEBT=OG_PRINCIPAL+OG_ANNUAL_INTEREST

SCALP_RATES=[800,1500,2000,2500,3000,4000,5000]

# Mint demand drivers
ORGANIC_MINT_PCT=0.08     # 8% of swap volume = organic HEAT demand
MINT_ELASTICITY=3.0       # demand multiplier when peg < 1%
ARB_ROUNDS=8              # simplified arb rounds

def xfp(t,rng):
    return max(.5,(2+np.exp(.12*t))*float(1+rng.normal(0,.25)))

def fm(v):
    if abs(v)>=1e6: return f"{v/1e6:7.2f}M"
    if abs(v)>=1e3: return f"{v/1e3:7.1f}K"
    return f"{v:8,.0f}"

def run(rng, scalp_rate):
    """scalp_rate = decimal (0.08 = 8%)"""
    p0=xfp(0,rng);t0=HEAT_PEG/p0;px=5000;ph=px/t0 if t0>0 else 25000
    hs=0;ema=0.0;lo=0.0;sc=0;debt=float(TOTAL_DEBT);total_scalped=0.0;yr_paid=None

    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS: break
        xp=xfp(ep/EPY,rng)

        # Oracle (simplified: 0-latency)
        price=xp;ema=0.30*price+0.70*ema if ema>0 else price

        # Swap volume drives everything
        ve=2e4*np.clip(xp/2,.3,5)*float(np.exp(rng.normal(0,.1)))

        # Simplified arb: ~8 rounds mixing + price convergence
        for _ in range(ARB_ROUNDS):
            if rng.random()<.5:
                c=min(ve/ARB_ROUNDS,px*.005)
                if c>0: r_=c/(px+c);ph-=ph*r_;px+=c
            else:
                c=min(ve/ARB_ROUNDS,ph*.005)
                if c>0: r_=c/(ph+c);px-=px*r_;ph+=c

        sp=px/max(ph,1e-3);php=sp*ema;dev=abs(php-HEAT_PEG)/HEAT_PEG

        # MINT MODEL: organic demand + arb
        # Organic: people want flatcoin regardless of peg
        organic=ve*ORGANIC_MINT_PCT
        # Arb boost: when HEAT > peg, more minting
        arb_boost=organic*MINT_ELASTICITY*max(0,(php/HEAT_PEG-1.0))
        # Arb burn: when HEAT < peg, some HEAT gets burned (simplified)
        if php<HEAT_PEG*0.99:
            arb_burn=organic*0.5*max(0,(1.0-php/HEAT_PEG))
            hs=max(0,hs-arb_burn)
        else:
            arb_burn=0

        mint=(organic+arb_boost)*(1+0.5*rng.random())  # random variance
        mint=max(0,mint)
        if mint>1e-3:
            scalped=mint*scalp_rate
            total_scalped+=scalped

            if debt>0:
                debt-=min(debt,scalped)
                if debt<=0 and yr_paid is None:
                    yr_paid=ep/EPY

            # HEAT enters supply (minus premium ≈2-5%)
            prem_pct=0.02 if dev<0.03 else 0.05
            hs+=mint*(1-prem_pct)

    return {'yr_paid':yr_paid,'debt_rem':max(0,debt),'total_scalped':total_scalped}

SEED=20260606
print(f"\n{'='*115}")
print(f"  OG Debt Payoff Sim — Elevated Scalp Rates (organic mint demand model)")
print(f"  Principal: {OG_PRINCIPAL:,} XFG  |  Interest/yr: {OG_ANNUAL_INTEREST:,} XFG (80%)")
print(f"  Gross debt: {TOTAL_DEBT:,} XFG  |  Organic mint: {ORGANIC_MINT_PCT*100:.0f}% of swap vol")
print(f"  {N_SIMS} sims × {len(SCALP_RATES)} rates | 21yr horizon")
print(f"{'='*115}")

print(f"\n{'Scalp':>7s} {'Med Yr':>7s} {'<5yr':>7s} {'<10yr':>7s}  {'Remain':>10s}  {'Scalped':>10s}  {'95%<':>7s}")
print(f"  {'─'*65}")

for sr in SCALP_RATES:
    res=[]
    for s in range(N_SIMS):
        rng=np.random.RandomState(int(SEED+s*1000+sr*73))
        res.append(run(rng, sr/10000.0))

    paid=[r['yr_paid'] for r in res if r['yr_paid'] is not None]
    rem=np.median([r['debt_rem'] for r in res])
    scp=np.median([r['total_scalped'] for r in res])
    med_yr=np.median(paid) if paid else None
    p5=len([y for y in paid if y<5])/N_SIMS*100
    p10=len([y for y in paid if y<10])/N_SIMS*100
    p95=np.percentile(paid,95) if len(paid)>1 else None

    tag=f"{sr/100:.0f}%"
    if med_yr:
        p95s=f"{p95:.1f}" if p95 else "?"
        print(f"  {tag:>6s}  {med_yr:>5.1f}yr  {p5:>5.0f}%  {p10:>5.0f}%  {fm(rem):>10}  {fm(scp):>10}  {p95s:>5s}yr")
    else:
        pct_never=len([r for r in res if r['yr_paid'] is None])/N_SIMS*100
        print(f"  {tag:>6s}  {pct_never:>3.0f}%NEVER {p5:>5.0f}%  {p10:>5.0f}%  {fm(rem):>10}  {fm(scp):>10}    -")

print(f"\n  Debt = principal + 1yr interest ({TOTAL_DEBT:,})")
print(f"  Mint = organic({ORGANIC_MINT_PCT*100:.0f}% of swap vol) + arb boost")
sys.stdout.flush()
