#!/usr/bin/env python3
"""Final Design v17 — All fixes applied. 20yr horizon."""
import numpy as np, sys

EPY=73;N_SIMS=30;N_EPOCHS=EPY*21;N_YEARS=21;HEAT_PEG=1.58;ATOMIC_FEE=0.02
HEARTH_FEE_BPS=8;ARB_THRESHOLD=0.001  # 0.1%
CD_FLOOR_APY=5.0

def mint_split(dev):
    if dev<0.01: return 15,60,25
    elif dev<0.03: return 40,20,40
    else: return 60,10,30

def drip_split(dev, atomic_vol):
    bond=20;mining=30;swf_restake=30
    if atomic_vol<50: cd_boost=50
    elif atomic_vol<200: cd_boost=35
    elif atomic_vol<500: cd_boost=20
    else: cd_boost=5
    total=bond+cd_boost+mining+swf_restake
    return bond/total*100,cd_boost/total*100,mining/total*100,swf_restake/total*100

def premium(dev):
    if dev<0.01: return 0.0
    elif dev<0.03: return 0.02
    else: return 0.05

def xfp(t,rng):return max(.5,(2+np.exp(.12*t))*float(1+rng.normal(0,.25)))
def fm(v):return f"{v/1e6:7.2f}M"if abs(v)>=1e6 else(f"{v/1e3:7.1f}K"if abs(v)>=1e3 else f"{v:8,.0f}")

def run(rng):
    p0=xfp(0,rng);t0=HEAT_PEG/p0;px=5000;ph=px/t0 if t0>0 else 25000
    hs=0;tre=0;swf=0;yemr=0;xfb=xfc=0
    cdl=50000;cd_acc=0;lp_fee_owed=0;vol_total=0;mints_total=0
    lo=0;sc=0;ema=0;cd_apy=0;atomic_vol_avg=0
    
    record=np.zeros((N_YEARS,13))
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=xfp(ep/EPY,rng)
        
        # Oracle: Exbitron live immediately (no bootstrap delay)
        os=rng.random()<0.05;op=xp*float(1+rng.normal(0,.05))
        if not os:lo=op;sc=0
        else:sc+=1
        price=lo if lo>0 else xp
        ema=0.30*price+0.70*ema if ema>0 else price
        red=max(1e-6,HEAT_PEG/max(ema,.01))
        
        ve=2e4*np.clip(xp/2,.3,5)*float(np.exp(rng.normal(0,.1)))
        vol_total+=ve
        
        atomic_ve=ve*0.15;atomic_fees=atomic_ve*ATOMIC_FEE
        cd_acc+=atomic_fees
        
        sv=ve*0.85
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<.5:c=min(sv/3,px*.005);r_=c/(px+c);ph-=ph*r_;px+=c
                else:c=min(sv/3,ph*.005);r_=c/(ph+c);px-=px*r_;ph+=c
        lp_fee_owed+=ve*HEARTH_FEE_BPS/10000
        
        # Two-way arb
        if sc<EPY and lo>0:
            for _ in range(40):
                s2=px/max(ph,1e-3);hp2=s2*ema;gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                if gap<0.0005:break  # 0.05%
                a=min(px,ph)*0.03
                if hp2>HEAT_PEG:
                    out=px*a*(1-HEARTH_FEE_BPS/10000)/(ph+a)
                    if out>0 and out<px:ph+=a;px-=out;hs+=a;xfb+=a*red
                else:
                    out=ph*a*(1-HEARTH_FEE_BPS/10000)/(px+a)
                    if out>0 and out<ph:px+=a;ph-=out;hs-=out;xfc+=out*red
        
        sp=px/max(ph,1e-3);php=sp*ema;dev=abs(php-HEAT_PEG)/HEAT_PEG
        
        # Mint with premium, 8% YEM scalp before split
        prem=premium(dev);mp=max(0,.008-.4*dev)
        m=atomic_ve*.08/max(sp,1e-3)*mp
        yemr+=m*0.08  # 8% scalp → YEM Reserve
        remaining=m*0.92
        hm=remaining*(1-prem);tre+=remaining*prem*sp
        hs+=hm;mints_total+=remaining
        
        mt,mn,ms=mint_split(dev)
        tre+=hm*mt/100*red
        swf+=hm*ms/100*red
        
        if dev>0.005 and tre>0 and ph>0:
            absorb=min(tre*0.1,ph*0.02)
            out=px*absorb*(1-HEARTH_FEE_BPS/10000)/(ph+absorb)
            if out>0 and out<px:ph+=absorb;px-=out;tre-=absorb
        
        if ep>0 and ep%EPY==0:
            if cd_acc>0 and ph>0:
                spend=cd_acc;hb=ph*spend/(px+spend)
                if 0<hb<ph*.95:px+=spend;ph-=hb;hs+=hb
            cd_apy=cd_acc/cdl*100 if cdl>0 else CD_FLOOR_APY
            cdl+=cdl*.05 if cd_apy>30 else(cdl*.02 if cd_apy>15 else(cdl*.005 if cd_apy>5 else(-cdl*.005 if cd_apy<2 else 0)))
            cdl=max(1e3,cdl)
            
            bd,cd_d,mn_d,sw_d=drip_split(dev,atomic_vol_avg)
            drip=swf*0.05/EPY  # 5% annual
            swf-=min(swf,drip)  # pay drip
            cd_acc+=drip*cd_d/100
            
            if atomic_vol_avg==0:atomic_vol_avg=atomic_ve
            else:atomic_vol_avg=0.7*atomic_vol_avg+0.3*atomic_ve
            cd_acc=0
            lp_fee_owed=0
        
        record[yr,0]=php
        record[yr,1]=dev*100
        record[yr,2]=hs
        record[yr,3]=tre
        record[yr,4]=swf
        record[yr,5]=yemr
        record[yr,6]=cd_apy
        record[yr,7]=cdl
        record[yr,8]=vol_total/EPY
        record[yr,9]=0
        record[yr,10]=mints_total/EPY
        record[yr,11]=xfc-xfb
        record[yr,12]=cd_acc
    
    return record

SEED=20260606
print(f"\n{'='*115}")
print(f"  FINAL v17 — Zero oracle delay, 8% YEM scalp, dynamic CD floor, 5% annual SWF drip")
print(f"  HEAT${HEAT_PEG:.2f} | Fee {HEARTH_FEE_BPS}bps | Arb threshold {ARB_THRESHOLD*100:.1f}% | CD floor {CD_FLOOR_APY:.0f}%")
print(f"{'='*115}")

sys.stdout.write("  Simulating... ");sys.stdout.flush()
batch=[run(np.random.RandomState(SEED+hash(str(s))%(2**30))) for s in range(N_SIMS)]
med=np.median(batch,axis=0)
print("done")

for label in['FINAL DESIGN v17 RESULTS']:
    print(f"\n{'Year':>5s} {'HEAT$':>7s} {'PegD%':>6s} {'Supply':>10s} {'Treas':>8s} {'SWF':>8s} {'YEMR':>8s} {'CD%':>6s} {'CDlock':>10s} {'Vol':>8s}")
    print("  " + "─"*90)
    for y in[1,3,5,7,10,15,20]:
        print(f"  Y{y:>2d}  ${med[y,0]:>5.2f} {med[y,1]:>5.2f}% {fm(med[y,2]):>10} {fm(med[y,3]):>8} {fm(med[y,4]):>8} {fm(med[y,5]):>8} {med[y,6]:>5.1f}% {fm(med[y,7]):>10} {fm(med[y,8]):>8}")

print(f"\n  Y20: HEAT=${med[20,0]:.2f}  PegDev={med[20,1]:.2f}%  SWF={fm(med[20,4])}  CD_APY={med[20,6]:.1f}%")
print(f"  Treasury={fm(med[20,3])}  YEM_R={fm(med[20,5])}  CD_Locked={fm(med[20,7])}  NetXFG={fm(med[20,11])}")
sys.stdout.flush()
