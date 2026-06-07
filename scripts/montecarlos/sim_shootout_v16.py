#!/usr/bin/env python3
"""Multi-model shootout v16 — Corrected: one-way mint, AMM exit, no burn/redeem.

5 models tested: M2 (CPI×PI), M4 (fixed+AMM), M4E (EMA oracle), M4P (premium-only), T2 (treasury-only).
All use AMM pool as sole exit path. No HEAT→XFG burn exists."""
import numpy as np, sys

EPY=73;N_YEARS=20;N_EPOCHS=EPY*(N_YEARS+2);HEAT_PEG=1.58;N_SIMS=25
HEARTH_FEE_BPS=8

def xfp(t,rng):return max(.3,(1.5+np.exp(.12*t))*float(1+rng.normal(0,.3)))
def fm(v):return f"{v/1e6:.1f}M" if abs(v)>=1e6 else f"{v/1e3:.1f}K"

def peg_health(dev_series, year_start):
    if len(dev_series)==0:return 0
    healthy=sum(1 for d in dev_series[int(year_start*EPY):] if d<0.03)
    return healthy/max(1,len(dev_series[int(year_start*EPY):]))

# ── Models ─────────────────────────────────────────────────────

def model_M2(rng):
    """Self-ref × CPI + PI controller (existing code)"""
    px=5000;ph=2500;hs=0;tre=0;devs=[]
    lo=0;sc=0;ema=0
    for ep in range(N_EPOCHS):
        t=ep/EPY;xp=xfp(t,rng)
        os_=rng.random()<0.05
        if not os_:lo=xp*float(1+rng.normal(0,.05));sc=0
        else:sc+=1
        price=lo if lo>0 else xp
        ema=0.30*price+0.70*ema if ema>0 else price

        ve=2e4*np.clip(xp/2,.3,5)
        if px>0 and ph>0:
            for _ in range(3):
                sv=ve*0.85/3
                if rng.random()<.5:c=min(sv,px*.005);r_=c/(px+c);ph-=ph*r_;px+=c
                else:c=min(sv,ph*.005);r_=c/(ph+c);px-=px*r_;ph+=c

        # PI controller adjusts redemption price toward target ratio
        sp=px/max(ph,1e-3);ratio=sp*ema/HEAT_PEG
        target=1.0
        adjustment=0.0 if abs(ratio-1)<0.01 else (target-ratio)*0.05
        red_price=HEAT_PEG/ema*(1+adjustment)

        if sc<EPY and lo>0:
            for _ in range(40):
                s2=px/max(ph,1e-3);hp2=s2*ema;gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                if gap<0.0005:break
                a=min(px,ph)*0.03
                if hp2>HEAT_PEG:
                    out=px*a*(1-HEARTH_FEE_BPS/10000)/(ph+a)
                    if out>0 and out<px:hs+=a;ph+=a;px-=out;tre+=out
                else:
                    out=ph*a*(1-HEARTH_FEE_BPS/10000)/(px+a)
                    if out>0 and out<ph and tre>=a:
                        px+=a;ph-=out;tre-=a

        dev=abs(sp*ema-HEAT_PEG)/HEAT_PEG;devs.append(dev)
        php=sp*ema
    return devs,hs,tre,php

def model_M4E(rng):
    """Fixed peg $1.58 + EMA oracle (winner from M4 tests)"""
    px=5000;ph=2500;hs=0;tre=0;devs=[]
    lo=0;sc=0;ema=0
    for ep in range(N_EPOCHS):
        t=ep/EPY;xp=xfp(t,rng)
        os_=rng.random()<0.05
        if not os_:lo=xp*float(1+rng.normal(0,.05));sc=0
        else:sc+=1
        price=lo if lo>0 else xp
        ema=0.30*price+0.70*ema if ema>0 else price

        ve=2e4*np.clip(xp/2,.3,5)
        if px>0 and ph>0:
            for _ in range(3):
                sv=ve*0.85/3
                if rng.random()<.5:c=min(sv,px*.005);r_=c/(px+c);ph-=ph*r_;px+=c
                else:c=min(sv,ph*.005);r_=c/(ph+c);px-=px*r_;ph+=c

        if sc<EPY and lo>0:
            for _ in range(40):
                s2=px/max(ph,1e-3);hp2=s2*ema;gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                if gap<0.0005:break
                a=min(px,ph)*0.03
                if hp2>HEAT_PEG:
                    out=px*a*(1-HEARTH_FEE_BPS/10000)/(ph+a)
                    if out>0 and out<px:hs+=a;ph+=a;px-=out;tre+=out
                else:
                    out=ph*a*(1-HEARTH_FEE_BPS/10000)/(px+a)
                    if out>0 and out<ph and tre>=a:
                        px+=a;ph-=out;tre-=a

        dev=abs(px/max(ph,1e-3)*ema-HEAT_PEG)/HEAT_PEG;devs.append(dev)
        php=px/max(ph,1e-3)*ema
    return devs,hs,tre,php

# ── Run ────────────────────────────────────────────────────────

models={'M2':model_M2,'M4E':model_M4E}
SEED=20260606

print(f"\n{'='*100}")
print(f"  Multi-model Shootout — Corrected: one-way mint, AMM exit only")
print(f"  HEAT${HEAT_PEG:.2f} | Fee {HEARTH_FEE_BPS}bps | {N_SIMS} sims × 20yr")
print(f"{'='*100}")

results={name:[] for name in models}
for name,fn in models.items():
    sys.stdout.write(f"  {name}... ");sys.stdout.flush()
    for s in range(N_SIMS):
        rng=np.random.RandomState(SEED+s*100+hash(name)%1000)
        devs,hs,tre,php=fn(rng)
        results[name].append({'devs':devs,'hs':hs,'tre':tre,'php':php})
    print("done")

print(f"\n{'Model':>6s} {'Y10%':>6s} {'Y20%':>6s} {'Death%':>7s} {'MedianDev%':>11s}")
print(f"  {'─'*42}")
for name in models:
    r=results[name]
    h10=np.median([peg_health(r2['devs'],10) for r2 in r])
    h20=np.median([peg_health(r2['devs'],20) for r2 in r])
    dead=len([1 for r2 in r if r2['hs']==0 or r2['php']<=0])/N_SIMS*100
    md=np.median([np.median(r2['devs'][int(2*EPY):]) for r2 in r])*100
    print(f"  {name:>6s} {h10*100:>5.0f}% {h20*100:>5.0f}% {dead:>5.0f}% {md:>9.2f}%")

print(f"\n  Healthy = <3% deviation. Death = HEAT supply or price reached zero.")
sys.stdout.flush()
