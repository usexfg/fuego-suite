#!/usr/bin/env python3
"""Final Design v20 — treasury HEAT sell path, 50/50 mint split, 100% atomic->CD."""
import numpy as np, sys

EPY=73;N_SIMS=3000;N_EPOCHS=EPY*21;N_YEARS=21
HEAT_PEG=1.58;ATOMIC_FEE=0.02;HEARTH_FEE_BPS=8
MINT_50_TO_TREASURY=True  # 50% of mint burn → treasury

def xfp(t,rng):return max(.5,(2+np.exp(.12*t))*float(1+rng.normal(0,.25)))
def fm(v):return f"{v/1e6:.1f}M" if abs(v)>=1e6 else f"{v/1e3:.1f}K"

def run(rng):
    p0=xfp(0,rng);t0=HEAT_PEG/p0;px=5000;ph=px/t0 if t0>0 else 25000
    hs=0;tre=0;tre_heat=0
    cdl=50000;cd_acc=0;vol_total=0;mints_total=0;mint_xfg_total=0
    lo=0;sc=0;ema=0;cd_apy=0

    record=np.zeros((N_YEARS,10))
    healthy_epochs=0;total_epochs=0

    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=xfp(ep/EPY,rng)

        # Oracle
        os=rng.random()<0.05;op=xp*float(1+rng.normal(0,.05))
        if not os:lo=op;sc=0
        else:sc+=1
        price=lo if lo>0 else xp
        ema=0.30*price+0.70*ema if ema>0 else price
        red=max(1e-6,HEAT_PEG/max(ema,.01))

        ve=2e4*np.clip(xp/2,.3,5)*float(np.exp(rng.normal(0,.1)))
        vol_total+=ve
        atomic_ve=ve*0.15;cd_acc+=atomic_ve*ATOMIC_FEE  # 100% → CD

        # Organic Hearth trading
        sv=ve*0.85
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<.5:c=min(sv/3,px*.005);r_=c/(px+c);ph-=ph*r_;px+=c
                else:c=min(sv/3,ph*.005);r_=c/(ph+c);px-=px*r_;ph+=c

        # AMM arbitrage (mint + sell to pool, or buy from pool)
        if sc<EPY and lo>0:
            for _ in range(40):
                s2=px/max(ph,1e-3);hp2=s2*ema;gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                if gap<0.0005:break
                a=min(px,ph)*0.03
                if hp2>HEAT_PEG:
                    # HEAT overvalued: sell treasury HEAT first, then mint remainder
                    heat_from_treasury = min(tre_heat, a)  # a is HEAT cap in overvalued case
                    if heat_from_treasury > 0:
                        out_xfg = px * heat_from_treasury * (1 - HEARTH_FEE_BPS / 10000) / (ph + heat_from_treasury)
                        if out_xfg > 0 and out_xfg < px:
                            ph += heat_from_treasury; px -= out_xfg
                            tre_heat -= heat_from_treasury; tre += out_xfg
                    heat_to_mint = a - heat_from_treasury
                    if heat_to_mint > 0:
                        out_xfg = px * heat_to_mint * (1 - HEARTH_FEE_BPS / 10000) / (ph + heat_to_mint)
                        if out_xfg > 0 and out_xfg < px:
                            hs += heat_to_mint; ph += heat_to_mint; px -= out_xfg
                            xfg_value = heat_to_mint * red
                            mint_xfg_total += xfg_value
                            if MINT_50_TO_TREASURY:
                                tre += xfg_value / 2 + out_xfg
                else:
                    out_heat=ph*a*(1-HEARTH_FEE_BPS/10000)/(px+a)
                    if out_heat>0 and out_heat<ph and tre>=a:
                        px+=a;ph-=out_heat;tre-=a;tre_heat+=out_heat

        sp=px/max(ph,1e-3);php=sp*ema;dev=abs(php-HEAT_PEG)/HEAT_PEG
        total_epochs+=1
        if dev<0.03:healthy_epochs+=1

        # Organic mint demand (users burn XFG to get HEAT)
        mp=max(0,.008-.4*dev)
        m=atomic_ve*.08/max(sp,1e-3)*mp
        hs+=m;mints_total+=m
        if MINT_50_TO_TREASURY:
            tre+=m*red/2  # 50% of mint value → treasury
        mint_xfg_total+=m*red

        # Treasury peg defense: buy HEAT from pool when under peg
        if dev>0.005 and tre>0 and ph>0:
            absorb=min(tre*0.1,ph*0.02)
            out_heat=px*absorb*(1-HEARTH_FEE_BPS/10000)/(ph+absorb)
            if out_heat>0 and out_heat<ph:
                ph+=absorb;px-=out_heat;tre-=absorb;tre_heat+=out_heat

        # Epoch boundary
        if ep>0 and ep%EPY==0:
            if cd_acc>0 and ph>0:
                spend=cd_acc;hb=ph*spend/(px+spend)
                if 0<hb<ph*.95:px+=spend;ph-=hb;hs+=hb
            cd_apy=cd_acc/max(cdl,1)*100 if cdl>0 else 0
            cdl*=1.02 if cd_apy>30 else(1.01 if cd_apy>15 else(1.005 if cd_apy>5 else 0.995))
            cdl=max(1e3,cdl)
            cd_acc=0

        record[yr,0]=php
        record[yr,1]=dev*100
        record[yr,2]=hs
        record[yr,3]=tre
        record[yr,4]=tre_heat
        record[yr,5]=cd_apy
        record[yr,6]=cdl
        record[yr,7]=vol_total/EPY
        record[yr,8]=mints_total/EPY
        record[yr,9]=mint_xfg_total

    return record, healthy_epochs/max(1,total_epochs)*100

SEED=20260606
print(f"\n{'='*105}")
print(f"  v20 — treasury HEAT sell path | 50/50 mint split | 100% atomic -> CD")
print(f"  HEAT${HEAT_PEG:.2f} | Fee {HEARTH_FEE_BPS}bps | {N_SIMS} sims × 20yr")
print(f"{'='*105}")

sys.stdout.write("  Simulating... ");sys.stdout.flush()
batch=[]
for s in range(N_SIMS):
    rng=np.random.RandomState(SEED+hash(str(s))%(2**30))
    rec,health=run(rng)
    batch.append((rec,health))
med=np.median([b[0] for b in batch],axis=0)
avg_health=np.mean([b[1] for b in batch])
print("done")

print(f"\n  Avg peg health ({N_SIMS:,} sims): {avg_health:.1f}% epochs < 3% deviation\n")

print(f"  {'Year':>5s} {'HEAT$':>7s} {'PegD%':>6s} {'Supply':>10s} {'Treas':>10s} {'CD%':>6s} {'CDlock':>10s} {'Vol':>8s}")
print("  " + "─"*72)
for y in[1,3,5,7,10,15,20]:
    print(f"  Y{y:>2d}  ${med[y,0]:>5.2f} {med[y,1]:>5.2f}% {fm(med[y,2]):>10} {fm(med[y,3]):>10} {med[y,5]:>5.1f}% {fm(med[y,6]):>10} {fm(med[y,7]):>8}")

# Percentiles
devs=[np.median(b[0][:,1]) for b in batch]
p5=np.percentile(devs,5);p25=np.percentile(devs,25);p75=np.percentile(devs,75);p95=np.percentile(devs,95)
print(f"\n  Median peg dev: {np.median(devs):.2f}% | P5={p5:.2f}% P25={p25:.2f}% P75={p75:.2f}% P95={p95:.2f}%")
print(f"  Y20: Treasury={fm(med[20,3])}  CD_APY={med[20,5]:.1f}%  CD_Locked={fm(med[20,6])}  Vol={fm(med[20,7])}")
sys.stdout.flush()
