#!/usr/bin/env python3
"""
Final Design Simulation v16 — Full System, 20yr
===============================================
M4E hard peg + zero-fee Hearth + dynamic SWF + CD only from atomic fees
"""

import numpy as np, time, sys

EPY=73;N_SIMS=50;N_EPOCHS=EPY*21;N_YEARS=21
HEAT_PEG=1.58;LAUNCH_8=0.125
PI_KP=0.08;PI_KI=0.015
HEARTH_FEE_NOMINAL=0.003  # 0.3% — paid by SWF, not trader
ATOMIC_FEE=0.02           # 2% — cross-chain swaps

# SWF mint split bands
def mint_split(dev):
    if dev<0.01: return 15,60,25     # TRE,MIN,SWF
    elif dev<0.03: return 40,20,40
    else: return 60,10,30

# SWF drip split bands
def drip_split(dev):
    if dev<0.01: return 20,20,30,30  # BOND,CD,MIN,SWF
    elif dev<0.03: return 10,50,10,30
    else: return 5,70,5,20

# Mint premium
def premium(dev):
    if dev<0.01: return 0.0
    elif dev<0.03: return 0.02
    else: return 0.05

def xfp(t,rng):return max(.5,(2+np.exp(.12*t))*float(1+rng.normal(0,.25)))
def fm(v):return f"{v/1e6:7.2f}M"if abs(v)>=1e6 else(f"{v/1e3:7.1f}K"if abs(v)>=1e3 else f"{v:8,.0f}")

def run(rng):
    # Bootstrap pool at peg
    p0=xfp(0,rng);t0=HEAT_PEG/p0;px=5000;ph=px/t0 if t0>0 else 25000
    
    hs=0;tre=0;swf=0;yemr=0;xfb=xfc=0
    cdl=50000;cd_acc=0;lp_fee_owed=0;vol_total=0;mints_total=0
    oracle_start=int(rng.randint(EPY//4,EPY));lo=0;sc=0;ema=0
    integ=0
    
    record=np.zeros((N_YEARS,12));cd_apy=0
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        
        xp=xfp(ep/EPY,rng)
        
        # EMA oracle
        oa=ep>=oracle_start;os=rng.random()<.05 if oa else True;op=xp*float(1+rng.normal(0,.05))
        if oa and not os:lo=op;sc=0
        else:sc+=1
        price=lo if lo>0 else xp
        ema=0.30*price+0.70*ema if ema>0 else price
        red=max(1e-6,HEAT_PEG/max(ema,.01))
        
        # Volume
        ve=2e4*np.clip(xp/2,.3,5)*float(np.exp(rng.normal(0,.1)))
        vol_total+=ve
        
        # Atomic swap fees → 100% CD pool
        atomic_ve=ve*0.15  # 15% of volume is cross-chain
        atomic_fees=atomic_ve*ATOMIC_FEE
        cd_acc+=atomic_fees
        
        # Regular Hearth swaps (zero-fee for trader, SWF pays)
        sv=ve*0.85
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<.5:c=min(sv/3,px*.005);ro_=c/(px+c);ph-=ph*ro_;px+=c
                else:c=min(sv/3,ph*.005);ro_=c/(ph+c);px-=px*ro_;ph+=c
        lp_fee_owed+=ve*HEARTH_FEE_NOMINAL
        
        sp_before=px/max(ph,1e-3)
        
        # === Two-way arb (tight, zero-fee) ===
        if sc<EPY and lo>0:
            for _ in range(40):  # more rounds at zero fee
                s2=px/max(ph,1e-3);hp2=s2*ema;gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                if gap<0.0001:break  # 0.01% convergence
                a=min(px,ph)*0.03
                if hp2>HEAT_PEG:
                    out=px*a*(1-0.0005)/(ph+a)  # 0.05% Hearth fee from SWF
                    if out>0 and out<px:ph+=a;px-=out;hs+=a;xfb+=a*red
                else:
                    out=ph*a*(1-0.0005)/(px+a)
                    if out>0 and out<ph:px+=a;ph-=out;hs-=out;xfc+=out*red
        
        sp=px/max(ph,1e-3);php=sp*ema
        dev=abs(php-HEAT_PEG)/HEAT_PEG
        
        # === Mint with dynamic premium + SWF split ===
        mp=max(0,.008-.4*dev);prem=premium(dev)
        m=atomic_ve*.08/max(sp,1e-3)*mp
        hm=m*(1-prem);tre+=m*prem*sp  # premium → treasury
        hs+=hm;mints_total+=m
        
        mt,mn,ms=mint_split(dev)
        tre+=hm*mt/100*red  # treasury gets TRE% of mint value
        swf+=hm*ms/100*red  # SWF gets SWF%
        # MIN% goes to emission recycling (external to this sim)
        
        # === Treasury absorbs HEAT when cheap ===
        if dev>0.005 and tre>0 and ph>0:
            absorb=min(tre*0.1,ph*0.02)  # 10% of treasury, max 2% of pool
            out=px*absorb*(1-0.0005)/(ph+absorb)
            if out>0 and out<px:ph+=absorb;px-=out;tre-=absorb
        
        # === CD buyback at epoch boundary ===
        if ep>0 and ep%EPY==0:
            if cd_acc>0 and ph>0:
                spend=cd_acc;hb=ph*spend/(px+spend)
                if 0<hb<ph*.95:px+=spend;ph-=hb;hs+=hb
            cd_apy=cd_acc/cdl*100 if cdl>0 else 0
            cdl+=cdl*.05 if cd_apy>30 else(cdl*.02 if cd_apy>15 else(cdl*.005 if cd_apy>5 else(-cdl*.005 if cd_apy<2 else 0)))
            cdl=max(1e3,cdl);cd_acc=0
            
            # === SWF pays LP fees + does drip ===
            bd,cd_d,mn_d,sw_d=drip_split(dev)
            drip=swf*0.05  # 5% of SWF per epoch
            # Pay LP fees from SWF
            fee_cost=lp_fee_owed
            swf-=min(fee_cost,swf)
            # Drip allocations
            cd_acc+=drip*cd_d/100  # CD boost from SWF drip
            
            lp_fee_owed=0
        
        record[yr,0]=php                       # HEAT pool $
        record[yr,1]=dev*100                   # peg deviation %
        record[yr,2]=hs                         # HEAT supply
        record[yr,3]=tre                        # treasury
        record[yr,4]=swf                        # SWF balance
        record[yr,5]=yemr                       # YEM Reserve
        record[yr,6]=cd_apy  # CD APY %
        record[yr,7]=cdl                        # CD locked
        record[yr,8]=vol_total/EPY              # annual volume
        record[yr,9]=lp_fee_owed/EPY            # LP fees owed per year
        record[yr,10]=mints_total/EPY           # mints per year
        record[yr,11]=xfc-xfb                   # net XFG
    
    return record

SEED=20260605
print(f"\n{'='*130}")
print(f"  FINAL DESIGN v16 — M4E + Zero-Fee Hearth + Dynamic SWF + CD from Atomic  |  {N_SIMS} sims × 20yr")
print(f"  HEAT${HEAT_PEG:.2f}  |  Arb: 40 rounds, 0.01% convergence  |  SWF: dynamic mint/drip split")
print(f"{'='*130}")

sys.stdout.write("  Running... ");sys.stdout.flush();t0=time.time()
batch=[run(np.random.RandomState(SEED+hash(str(s))%(2**30))) for s in range(N_SIMS)]
dt=time.time()-t0
med=np.median(batch,axis=0)
std=np.std(batch,axis=0)
print(f"{dt:.0f}s")

labels=['HEAT $','PegDev%','Supply','Treasury','SWF','YEM Reserve',
        'CD APY%','CD Locked','Volume/yr','LP Fees/yr','Mints/yr','Net XFG']

print(f"\n{'Year':>5s}",end='')
for y in[1,3,5,7,10,15,20]:
    print(f"  {f'Y{y}':>9s}",end='')
print(f"\n{'─'*90}")

for i,label in enumerate(labels[:8]):  # show key metrics
    print(f"  {label:<10s}",end='')
    for y in[1,3,5,7,10,15,20]:
        v=med[y,i] if y<N_YEARS else 0
        if i==0:print(f" ${v:>7.2f}",end='')
        elif i==1:print(f" {v:>8.2f}%",end='')
        elif i==6:print(f" {v:>8.1f}%",end='')
        else:print(f" {fm(v):>9}",end='')
    print()

# Summary
print(f"\n{'─'*90}")
print(f"  RESULTS (Y20): HEAT=${med[20,0]:.2f}  PegDev={med[20,1]:.2f}%  SWF={fm(med[20,4])}  CD_APY={med[20,6]:.1f}%")
print(f"  Treasury={fm(med[20,3])}  CD_Locked={fm(med[20,7])}  Supply={fm(med[20,2])}  NetXFG={fm(med[20,11])}")
print(f"  Annual volume: {fm(med[20,8])}  Annual LP fees: {fm(med[20,9])} (SWF-covered)")
print(f"\n  ZERO-FEE HEARTH: Traders see 0% fee, SWF pays LPs. Arb fires at 0.01% gap.")
print(f"  DYNAMIC SPLITS: Mint→TRE/MIN/SWF and Drip→BOND/CD/MIN/SWF based on pool deviation.")
sys.stdout.flush()
