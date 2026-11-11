#!/usr/bin/env python3
"""Final Design v21 — YEM v4: SWF smoothing, LP/peg feed, tiered CD, market share growth"""
import numpy as np, sys, time

# ── Config ───────────────────────────────────────────────────────────────────
EPY=73; N_SIMS=1000; N_EPOCHS=EPY*30; N_YEARS=30
HEAT_PEG=1.58; ATOMIC_FEE=0.02; HEARTH_FEE_BPS=8
MINT_50_TO_TREASURY=True
TREASURY_LP_PCT=60; TREASURY_PEG_PCT=40

# Market share: XFG captures increasing % of total cross-chain atomic swap volume
TOTAL_MARKET_BASE=500e6      # $500M annual atomic swap market at t=0
TOTAL_MARKET_GROWTH=0.25     # 25% YoY
XFG_SHARE_START=0.002        # 0.2% at t=0
XFG_SHARE_TARGET=0.08        # 8% by year 20
XFG_SHARE_GROWTH=np.log(XFG_SHARE_TARGET/XFG_SHARE_START)/20

XFG_PRICE_START=0.01; XFG_PRICE_ANNUAL=0.08; XFG_PRICE_NOISE=0.20

# YEM
YEM_LAG_EPOCHS=3; YEM_SWF_SAVE_PCT=60; YEM_SWF_COVER_PCT=50
YEM_SWF_DRIP_BPS=100; YEM_LP_FEED_PCT=75; YEM_PEG_FEED_PCT=50
MAX_CD_APY_BPS=8000; CD_FLOOR_DIVISOR=50

def fm(v):
    if abs(v)>=1e9: return f"{v/1e9:.1f}B"
    if abs(v)>=1e6: return f"{v/1e6:.1f}M"
    if abs(v)>=1e3: return f"{v/1e3:.1f}K"
    return f"{v:.0f}"

def xfg_price(t, rng):
    yr = t/EPY; trend = XFG_PRICE_START*np.exp(XFG_PRICE_ANNUAL*yr)
    return max(0.005, trend*(1+rng.normal(0,XFG_PRICE_NOISE)))

def epoch_volume(t, rng):
    yr = t/EPY
    total = TOTAL_MARKET_BASE*(1+TOTAL_MARKET_GROWTH)**yr
    share = min(XFG_SHARE_TARGET, XFG_SHARE_START*np.exp(XFG_SHARE_GROWTH*yr))
    xp = xfg_price(t, rng)
    xfg_vol = total*share/EPY/xp  # XFG per epoch
    return max(100, xfg_vol*(1+rng.normal(0,0.05)))

def cd_tier_multiplier(term_epochs):
    if term_epochs<=9: return 100
    if term_epochs<=30: return 150
    if term_epochs<=71: return 200
    return 250

def run(rng):
    p0=xfg_price(0,rng); t0=HEAT_PEG/p0
    px=500; ph=max(100,px*t0)
    hs=0; tre=10000; trh=0; lpr=10000; prs=0; lpy=0
    cdl=50000; cd_acc=0; vol_total=0; mints_total=0; mint_xfg_total=0
    lo=0; sc=0; ema=0; cd_apy=0
    swf=0; lag=0; lag_done=False
    record=np.zeros((N_YEARS,13)); healthy=0; total_e=0

    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS: break
        xp=xfg_price(ep,rng)

        # Oracle
        os=rng.random()<0.05; op=xp*(1+rng.normal(0,.05))
        if not os: lo=op; sc=0
        else: sc+=1
        price=lo if lo>0 else xp
        ema=0.30*price+0.70*ema if ema>0 else price
        red=max(1e-6,HEAT_PEG/max(ema,.01))

        # Volume
        ve=epoch_volume(ep,rng); vol_total+=ve
        atomic_ve=ve*0.85; cd_acc+=atomic_ve*ATOMIC_FEE

        # Organic Hearth
        sv=ve*0.15
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<.5:c=min(sv/3,px*.005);r_=c/(px+c);ph-=ph*r_;px+=c
                else:c=min(sv/3,ph*.005);r_=c/(ph+c);px-=px*r_;ph+=c

        # Treasury LP deployment
        if lpr>0 and px>0 and ph>0:
            deposit=min(lpr*0.05,px*0.01)
            if deposit>0:
                px+=deposit; lpr-=deposit
                new_sh=deposit*ph/max(px,1e-3); prs+=new_sh
                lpy+=ve*0.003*(prs/max(prs+1e5,1))

        # Peg arb
        if sc<EPY and lo>0:
            for _ in range(40):
                s2=px/max(ph,1e-3); hp2=s2*ema
                gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                if gap<0.0005: break
                a=min(px,ph)*0.03
                if hp2>HEAT_PEG:
                    ht=min(trh,a)
                    if ht>0:
                        out=px*ht*(1-HEARTH_FEE_BPS/10000)/(ph+ht)
                        if 0<out<px: ph+=ht; px-=out; trh-=ht; tre+=out
                    hm=a-ht
                    if hm>0:
                        out=px*hm*(1-HEARTH_FEE_BPS/10000)/(ph+hm)
                        if 0<out<px:
                            hs+=hm; ph+=hm; px-=out
                            xv=hm*red; mint_xfg_total+=xv
                            if MINT_50_TO_TREASURY:
                                ts=xv/2+out; lpr+=ts*TREASURY_LP_PCT/100; tre+=ts*TREASURY_PEG_PCT/100
                else:
                    out_heat=ph*a*(1-HEARTH_FEE_BPS/10000)/(px+a)
                    if 0<out_heat<ph and tre>=a:
                        px+=a; ph-=out_heat; tre-=a; trh+=out_heat

        sp=px/max(ph,1e-3); php=sp*ema; dev=abs(php-HEAT_PEG)/HEAT_PEG
        total_e+=1
        if dev<0.03: healthy+=1

        # Organic mint demand
        mp=max(0,.008-.4*dev); m=atomic_ve*.08/max(sp,1e-3)*mp
        hs+=m; mints_total+=m
        if MINT_50_TO_TREASURY:
            ts=m*red/2; lpr+=ts*TREASURY_LP_PCT/100; tre+=ts*TREASURY_PEG_PCT/100
        mint_xfg_total+=m*red

        # Peg defense
        if dev>0.005 and tre>0 and ph>0:
            absorb=min(tre*0.1,ph*0.02)
            out_heat=px*absorb*(1-HEARTH_FEE_BPS/10000)/(ph+absorb)
            if 0<out_heat<ph: ph+=absorb; px-=out_heat; tre-=absorb; trh+=out_heat

        # Epoch boundary
        epoch = ep%EPY
        if ep>0 and epoch==0:
            base_apy = cd_acc/max(cdl,1)*100 if cdl>0 else 0  # already accumulated over full year
            tier_mul=cd_tier_multiplier(36)/100.0

            # LP yield → SWF
            if lag_done and lpy>0: swf+=lpy*YEM_LP_FEED_PCT/100; lpy*=0.25
            elif not lag_done: swf+=lpy; lpy=0

            # YEM smoothing
            if not lag_done:
                swf+=cd_acc*YEM_SWF_SAVE_PCT/100; lag+=1
                if lag>=YEM_LAG_EPOCHS: lag_done=True
            else:
                capped=min(base_apy*tier_mul,MAX_CD_APY_BPS/100)
                net_yield=capped/100*cdl; surplus=cd_acc-net_yield
                if surplus>0: swf+=surplus*YEM_SWF_SAVE_PCT/100
                else:
                    deficit=-surplus
                    draw=min(deficit*YEM_SWF_COVER_PCT/100,swf); swf-=draw; cd_acc+=draw
                # SWF drip
                if cdl>0: drip=swf*YEM_SWF_DRIP_BPS/10000; cd_acc+=drip; swf-=drip
                # Floor
                floor=swf/(cdl*CD_FLOOR_DIVISOR) if cdl>0 else 0
                if base_apy<floor*100:
                    inj=min((floor*100-base_apy)/100*cdl,swf*0.1); cd_acc+=inj; swf-=inj

            # Execute CD yield: buy HEAT from pool
            if cd_acc>0 and ph>0:
                spend=cd_acc; hb=ph*spend/(px+spend)
                if 0<hb<ph*.95: px+=spend; ph-=hb; hs+=hb
            cd_apy=cd_acc/max(cdl,1)*100 if cdl>0 else 0

            # CD elasticity — stronger response at extreme APY
            if cd_apy>200:   cdl*=1.10
            elif cd_apy>100: cdl*=1.05
            elif cd_apy>50:  cdl*=1.03
            elif cd_apy>30:  cdl*=1.02
            elif cd_apy>15:  cdl*=1.01
            elif cd_apy>5:   cdl*=1.005
            else:            cdl*=0.995
            cdl=max(1e3,cdl); cd_acc=0

        record[yr,0]=php; record[yr,1]=dev*100; record[yr,2]=hs; record[yr,3]=tre
        record[yr,4]=trh; record[yr,5]=cd_apy; record[yr,6]=cdl; record[yr,7]=vol_total/EPY
        record[yr,8]=mints_total/EPY; record[yr,9]=swf; record[yr,10]=lpr
        record[yr,11]=prs; record[yr,12]=mint_xfg_total

    return record, healthy/max(1,total_e)*100

SEED=20260610
print(f"\n{'='*95}")
print(f"  v21 — YEM v4 | SWF smoothing | LP/peg feed | Market-share volume model")
print(f"  XFG=${XFG_PRICE_START:.2f}→${xfg_price(EPY*30, np.random.RandomState(42)):.2f} | HEAT=${HEAT_PEG:.2f} | {N_SIMS:,} sims×{N_YEARS}yr")
print(f"{'='*95}")

t0=time.time(); sys.stdout.write("  Simulating... "); sys.stdout.flush()
batch=[]
for s in range(N_SIMS):
    rng=np.random.RandomState(SEED+hash(str(s))%(2**30))
    rec,health=run(rng); batch.append((rec,health))
med=np.median([b[0] for b in batch],axis=0)
avg_health=np.mean([b[1] for b in batch])
print(f"done ({time.time()-t0:.1f}s)")

print(f"\n  Avg peg health: {avg_health:.1f}% epochs within 3% deviation\n")
hdr=f"  {'Year':>5s}  {'HEAT$':>7s}  {'Dev%':>5s}  {'Supply':>10s}  {'Treas':>9s}  {'T-HEAT':>9s}  {'CD%':>7s}  {'CDlock':>10s}  {'SWF':>10s}  {'Vol/yr':>9s}"
print(hdr); print("  "+"—"*len(hdr))
for y in [1,2,3,5,7,10,15,20,25,30]:
    yi=min(y-1,N_YEARS-1)
    print(f"  Y{y:>2d}    ${med[yi,0]:>5.2f}   {med[yi,1]:>5.2f}%  {fm(med[yi,2]):>10}  {fm(med[yi,3]):>9}  {fm(med[yi,4]):>9}  {med[yi,5]:>6.1f}%  {fm(med[yi,6]):>10}  {fm(med[yi,9]):>10}  {fm(med[yi,7]):>9}")

devs=[np.median(b[0][:,1]) for b in batch]
p5=np.percentile(devs,5); p25=np.percentile(devs,25); p75=np.percentile(devs,75); p95=np.percentile(devs,95)
print(f"\n  ── Peg deviation ──")
print(f"  Median={np.median(devs):.2f}%  P5={p5:.2f}% P25={p25:.2f}% P75={p75:.2f}% P95={p95:.2f}%")

y30=29
print(f"\n  ── Y30 median ──")
print(f"  Price: ${med[y30,0]:.3f}  Dev: {med[y30,1]:.1f}%  Supply: {fm(med[y30,2])}")
print(f"  Treasury: {fm(med[y30,3])}  T-HEAT: {fm(med[y30,4])}")
print(f"  CD_APY: {med[y30,5]:.1f}%  CD_Locked: {fm(med[y30,6])}")
print(f"  SWF: {fm(med[y30,9])}  LP_Res: {fm(med[y30,10])}  LP_Shrs: {fm(med[y30,11])}")
print(f"  Vol/yr: {fm(med[y30,7])}  XFG_burned: {fm(med[y30,12])}")

swf_f=[b[0][y30,9] for b in batch]; cdl_f=[b[0][y30,6] for b in batch]
floors=[s/(c*CD_FLOOR_DIVISOR)*100 if c>0 else 0 for s,c in zip(swf_f,cdl_f)]
print(f"\n  ── SWF robustness ──")
print(f"  SWF: med={fm(np.median(swf_f))}  P5={fm(np.percentile(swf_f,5))}  P95={fm(np.percentile(swf_f,95))}")
print(f"  Floor APY: med={np.median(floors):.2f}%  P5={np.percentile(floors,5):.2f}%  P95={np.percentile(floors,95):.2f}%")
sys.stdout.flush()
