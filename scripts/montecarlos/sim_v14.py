#!/usr/bin/env python3
# ⚠️ SUPERSEDED by sim_final_v18.py — this sim hallucinates a HEAT→XFG burn path
# that does not exist. HEAT is mint-only. The AMM pool is the sole exit.
"""
HEAT Mode Comparison v14 — Code-Accurate
=========================================
M2: computeTargetRatio → 0.125 × launch_twap/spot (self-ref)
    epoch handler → × CPI_MULT (1.58) → PI controller
M4: redemption = HEAT_PEG / oracle_price (instant)
    two-way arb, symmetric create/destroy, oracle fallback
"""

import numpy as np, time, sys

EPY=73; N_SIMS=150; N_EPOCHS=EPY*11; N_YEARS=11
LAUNCH_8=0.125; CPI_MULT=1.58; HEAT_PEG=1.58
KP=0.08; KI=0.015; PI_MAX=0.5; PI_CLAMP=1.0
SWAP_FEE=0.02; HEARTH_FEE=0.003
CD_SHARE=0.80; TREAS_SHARE=0.16; REBAL_SHARE=0.04
YEM_LAG=3; YEM_SWF_SAVE=0.60; YEM_SWF_DRIP=0.01; YEM_BURN_SCALP=0.08
TIER_CAP_MIN=0.33; TIER_CAP_MAX=0.80; TIER_FULL=72
SEED=20260601

def xfp(t,rng):return max(0.5,(2+np.exp(0.15*t))*float(1+rng.normal(0,0.25)))
def tier_cap(d):return TIER_CAP_MIN+min(max(d,1),TIER_FULL)/TIER_FULL*(TIER_CAP_MAX-TIER_CAP_MIN)
def fmt(v):return f"{v/1e6:7.2f}M" if abs(v)>=1e6 else(f"{v/1e3:7.1f}K" if abs(v)>=1e3 else f"{v:8,.0f}")

# ═══════════════════════════════════════════════════════════════
# MODE 2: Self-referencing 8:1 × CPI, PI controller
#   computeTargetRatio:   target = 0.125 × launch_twap / spot
#   epoch handler:        target = target × CPI_MULT
#   PI controller:        redemption = target × (1 + rate × dt)
# ═══════════════════════════════════════════════════════════════
def sim_m2(rng):
    px,ph=5000,25000;red=LAUNCH_8*CPI_MULT;integ=0;tre=cd=hs=hc=0;lt=None
    swf=yemr=rev=cdl=0.0;cdl_init=50000
    sy=np.zeros(N_YEARS);ty=np.zeros(N_YEARS);ry=np.zeros(N_YEARS)
    dy=np.zeros(N_YEARS);hy=np.zeros(N_YEARS);fdy=np.zeros(N_YEARS);xy=np.zeros(N_YEARS)
    swfy=np.zeros(N_YEARS);yemy=np.zeros(N_YEARS);apyy=np.zeros(N_YEARS)
    pd=[];se=0;spiral=False;mx=0;lag_ep=0;rol=[0]*3;rc=0;ok=False;cd_apy=0
    # CD pool: accumulated IN epoch, spent AT epoch boundary
    cd=0.0;cdl=cdl_init
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=xfp(ep/EPY,rng)
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(rng.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;rev+=fees*REBAL_SHARE;cd+=fees*CD_SHARE
        
        # Random swaps
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<0.5:
                    c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:
                    c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
        
        sp=px/max(ph,1e-3)
        
        # === PI controller (runs at epoch boundary) ===
        # computeTargetRatio: self-ref 0.125 * launch_twap / spot
        if lt is None and ep>EPY:lt=sp
        if lt is not None and sp>1e-6:
            tgt_self = LAUNCH_8 * lt / sp
        else:
            tgt_self = LAUNCH_8
        
        # epoch handler: multiply by CPI
        tgt = tgt_self * CPI_MULT
        tgt = max(1e-6, tgt)
        
        dev = (sp - tgt) / max(tgt, 1e-6)
        dt = 1.0/EPY
        integ = np.clip(integ + dev*dt, -PI_CLAMP, PI_CLAMP)
        red_rate = np.clip(KP*dev + KI*integ, -PI_MAX, PI_MAX)
        red = max(1e-6, tgt * (1 + red_rate*dt))
        
        # PI rate drives CD spend multiplier
        sr = np.clip(1.0 + red_rate, 0.2, 3.0)
        
        d=abs(dev);pd.append(d);mx=max(mx,d)
        if d>0.5:se+=1
        else:se=max(0,se-1)
        if se>EPY:spiral=True
        
        # Organic mint
        mp=max(0,0.008-0.4*d);m=ve*0.08/max(sp,1e-3)*mp;hm=m*0.95
        hs+=hm;hc+=hm;yemr+=m*YEM_BURN_SCALP
        
        # CD buyback (at epoch boundary, NOT per-block)
        if ep>0 and ep%EPY==0:
            # YEM smoothing
            if not ok and lag_ep<YEM_LAG:
                lag_ep+=1
                if lag_ep>=YEM_LAG:ok=True
            else:
                org=cd/cdl if cdl>0 else 0;rol[rc%3]=org;rc+=1
                trate=np.mean(rol[:min(rc,3)]);capped=min(trate,tier_cap(36))
                surp=cd-capped*cdl
                if surp>0:swf+=surp*YEM_SWF_SAVE
                else:
                    draw=min(-surp,swf);swf-=draw
                    if -surp>draw:yemr-=(-surp-draw)
                cd_apy=(capped+swf*YEM_SWF_DRIP/max(cdl,1))*EPY;cdl*=1.001
            
            # Execute CD buyback (using PI-driven multiplier)
            if cd>0 and ph>0:
                spend=min(cd,cd*sr);hb=ph*spend/(px+spend)
                if 0<hb<ph*0.95:px+=spend;ph-=hb;hs+=hb;hc+=hb
            cd=0  # reset for next epoch
        
        # Rebalancer (uses PI redemption price)
        if ep>0 and ep%EPY==0 and rev>0 and red>0:
            reb=min(rev*0.1,px*0.03);ph+=reb/red;rev-=reb;hs+=reb/red
        
        apyy[yr]=cd_apy;swfy[yr]=swf;yemy[yr]=yemr
        sy[yr]=hs;ty[yr]=tre;ry[yr]=sp;fdy[yr]=red;dy[yr]=px;hy[yr]=sp*xp;xy[yr]=xp
    
    sk=int(N_EPOCHS*0.3);h=np.mean([1 if d<0.2 else 0 for d in pd[sk:]])*100 if pd else 0
    ret=[(ry[i]-ry[i-1])/ry[i-1] for i in range(2,N_YEARS) if ry[i-1]>0];vl=np.std(ret) if ret else 0
    return{'S':sy,'T':ty,'R':ry,'Rd':fdy,'D':dy,'HV':hy,'XP':xy,'SWF':swfy,'YEMR':yemy,'APY':apyy,
           'spiral':spiral,'maxD':mx,'H':h,'Vol':vl}


# ═══════════════════════════════════════════════════════════════
# MODE 4: Fixed redemption, two-way arb, oracle-dependent
#   target = HEAT_PEG / oracle_price (instant, no PI)
#   arb pushes pool toward peg via create/destroy
# ═══════════════════════════════════════════════════════════════
def sim_m4(rng):
    p0=xfp(0,rng);t0=HEAT_PEG/p0;px=5000;ph=px/t0 if t0>0 else 25000
    tre=hs=hc=0;xfb=0;xfc=0;swf=yemr=rev=cdl=0.0
    oracle_start=int(rng.randint(EPY//4,EPY));last_oracle=0;sc=0
    sy=np.zeros(N_YEARS);ty=np.zeros(N_YEARS);ry=np.zeros(N_YEARS)
    dy=np.zeros(N_YEARS);hy=np.zeros(N_YEARS);fdy=np.zeros(N_YEARS);xy=np.zeros(N_YEARS)
    swfy=np.zeros(N_YEARS);yemy=np.zeros(N_YEARS);apyy=np.zeros(N_YEARS)
    pd=[];se=0;spiral=False;mx=0;lag_ep=0;rol=[0]*3;rc=0;ok=False;cd_apy=0
    cd=0.0;cdl=50000
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=xfp(ep/EPY,rng)
        
        # Oracle
        oa=ep>=oracle_start;os_=rng.random()<0.05 if oa else True;op=xp*float(1+rng.normal(0,0.05))
        if oa and not os_:last_oracle=op;sc=0
        else:sc+=1
        price=last_oracle if last_oracle>0 else xp
        
        # Fixed redemption
        tgt=max(1e-6,HEAT_PEG/max(price,0.01))
        red=tgt
        
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(rng.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;rev+=fees*REBAL_SHARE;cd+=fees*CD_SHARE
        
        # Random swaps
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<0.5:
                    c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:
                    c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
        
        # Two-way arbitrage
        if sc<EPY and last_oracle>0:
            for _ in range(20):
                s2=px/max(ph,1e-3);hp2=s2*last_oracle;gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                if gap<0.002:break
                a=min(px,ph)*0.03
                if hp2>HEAT_PEG:
                    out=px*a*(1-HEARTH_FEE)/(ph+a)
                    if out>0 and out<px:ph+=a;px-=out;hs+=a;xfb+=a*red
                else:
                    out=ph*a*(1-HEARTH_FEE)/(px+a)
                    if out>0 and out<ph:px+=a;ph-=out;hs-=out;xfc+=out*red
        
        sp=px/max(ph,1e-3);php=sp*price
        d=abs(php-HEAT_PEG)/HEAT_PEG;pd.append(d);mx=max(mx,d)
        if d>0.5 and sc>4:se+=1
        else:se=max(0,se-1)
        if se>EPY:spiral=True
        
        # Organic mint (8% scalp → YEM Reserve)
        mp=max(0,0.008-0.4*d);m=ve*0.08/max(sp,1e-3)*mp;hm=m*0.95
        hs+=hm;hc+=hm;yemr+=m*YEM_BURN_SCALP
        
        # CD buyback at epoch boundary (fixed sr=1.0, no PI)
        if ep>0 and ep%EPY==0:
            if not ok and lag_ep<YEM_LAG:
                lag_ep+=1
                if lag_ep>=YEM_LAG:ok=True
            else:
                org=cd/cdl if cdl>0 else 0;rol[rc%3]=org;rc+=1
                trate=np.mean(rol[:min(rc,3)]);capped=min(trate,tier_cap(36))
                surp=cd-capped*cdl
                if surp>0:swf+=surp*YEM_SWF_SAVE
                else:
                    draw=min(-surp,swf);swf-=draw
                    if -surp>draw:yemr-=(-surp-draw)
                cd_apy=(capped+swf*YEM_SWF_DRIP/max(cdl,1))*EPY;cdl*=1.001
            
            if cd>0 and ph>0:
                spend=cd;hb=ph*spend/(px+spend)
                if 0<hb<ph*0.95:px+=spend;ph-=hb;hs+=hb;hc+=hb
            cd=0
        
        # Minimal rebalancer for extreme imbalance (>3:1 ratio)
        if ep>0 and ep%EPY==0 and rev>0 and red>0:
            if sp<0.15:  # pool heavily HEAT-skewed
                reb=min(rev*0.1,px*0.03);ph+=reb/red;rev-=reb;hs+=reb/red
        
        apyy[yr]=cd_apy;swfy[yr]=swf;yemy[yr]=yemr
        sy[yr]=hs;ty[yr]=tre;ry[yr]=sp;fdy[yr]=red;dy[yr]=px;hy[yr]=php;xy[yr]=price
    
    sk=int(N_EPOCHS*0.3);h=np.mean([1 if d<0.2 else 0 for d in pd[sk:]])*100 if pd else 0
    ret=[(ry[i]-ry[i-1])/ry[i-1] for i in range(2,N_YEARS) if ry[i-1]>0];vl=np.std(ret) if ret else 0
    return{'S':sy,'T':ty,'R':ry,'Rd':fdy,'D':dy,'HV':hy,'XP':xy,'SWF':swfy,'YEMR':yemy,'APY':apyy,
           'spiral':spiral,'maxD':mx,'H':h,'Vol':vl,'xfgb':xfb,'xfgc':xfc,'netX':xfc-xfb}


# ═══════════════════════════════════════════════════════════════
MODELS=[('M2:SelfRef 8:1 x CPI',sim_m2),('M4:FixedPeg 1.58',sim_m4)]
print(f"\n{'='*130}")
print(f"  HEAT v14 — Code-Accurate M2 vs M4  |  {N_SIMS} sims x 10yr  |  HEAT${HEAT_PEG:.2f}")
print(f"  M2: target = 0.125*lt/sp (self-ref) x {CPI_MULT} CPI → PI → redemption")
print(f"  M4: target = {HEAT_PEG:.2f} / oracle_price → instant redemption → two-way arb")
print(f"{'='*130}")

all_results=[]
for name,fn in MODELS:
    sys.stdout.write(f"  {name:<28} ");sys.stdout.flush();t0=time.time()
    batch=[fn(np.random.RandomState(SEED+hash(name+str(s))%(2**30))) for s in range(N_SIMS)]
    dt=time.time()-t0
    med={k:[np.median([b[k][y] for b in batch]) for y in range(N_YEARS)] for k in['S','T','R','Rd','D','HV','XP','SWF','YEMR','APY']}
    for k in['H','Vol','maxD']:med[k]=np.median([b[k] for b in batch])
    med['spiral']=np.mean([b['spiral'] for b in batch])*100
    if'Fixed'in name:
        med['xfgb']=np.median([b['xfgb'] for b in batch]);med['xfgc']=np.median([b['xfgc'] for b in batch])
        med['netX']=np.median([b['netX'] for b in batch])
    all_results.append((name,med))
    print(f"{dt:3.0f}s  H={med['H']:.0f}% spr={med['spiral']:.1f}% maxD={med['maxD']*100:.0f}% "
          f"S5={fmt(med['S'][5])} T5={fmt(med['T'][5])} SWF5={fmt(med['SWF'][5])} APY5={med['APY'][5]:.1f}%")

for label,yrs in[('Y1',[1]),('Y3',[3]),('Y5',[5]),('Y10',[10])]:
    print(f"\n{'─'*120} {label}")
    print(f"  {'MODEL':<28} {'HEAT_p$':>8} {'redm$':>7} {'XFG$':>6} {'Supply':>10} {'Treas':>10} {'SWF':>10} {'YEMR':>10} {'APY':>6}")
    for name,med in all_results:
        for y in yrs:
            if y>=N_YEARS:continue
            hp=med['R'][y]*med['XP'][y];rp=med['Rd'][y]*med['XP'][y]
            print(f"  {name:<28} ${hp:>7.2f} ${rp:>6.2f} ${med['XP'][y]:>5.2f} {fmt(med['S'][y]):>10} {fmt(med['T'][y]):>10} "
                  f"{fmt(med['SWF'][y]):>10} {fmt(med['YEMR'][y]):>10} {med['APY'][y]:>5.1f}%")

nm2,rm2 = all_results[0]; nm4,rm4 = all_results[1]

print(f"\n{'═'*120} M4 SUPPLY (Y10)")
print(f"  XFG_burned(createHEAT)={fmt(rm4['xfgb'])}  XFG_created(destroyHEAT)={fmt(rm4['xfgc'])}  NET={fmt(rm4['netX'])}")
print(f"  {'DEFLATIONARY' if rm4['netX']<0 else 'INFLATIONARY'} — XFG supply {'shrinks' if rm4['netX']<0 else 'grows'}")

print(f"\n{'═'*120} COMPARISON (Y10)")
for metric,m2v,m4v,w in[
    ('HEAT pool $','${:.2f}'.format(rm2['HV'][10]),'${:.2f}'.format(rm4['HV'][10]),'M4' if abs(rm4['HV'][10]-1.58)<abs(rm2['HV'][10]-1.58) else 'M2'),
    ('Redemption $','${:.2f}'.format(rm2['Rd'][10]*rm2['XP'][10]),'${:.2f}'.format(rm4['Rd'][10]*rm4['XP'][10]),'M4'),
    ('Treasury',fmt(rm2['T'][10]),fmt(rm4['T'][10]),'─'),
    ('SWF',fmt(rm2['SWF'][10]),fmt(rm4['SWF'][10]),'─'),
    ('YEM Reserve',fmt(rm2['YEMR'][10]),fmt(rm4['YEMR'][10]),'─'),
    ('CD APY','{:.1f}%'.format(rm2['APY'][10]),'{:.1f}%'.format(rm4['APY'][10]),'─'),
    ('Death Spiral','{:.1f}%'.format(rm2['spiral']),'{:.1f}%'.format(rm4['spiral']),'M4' if rm4['spiral']<rm2['spiral'] else 'M2'),
    ('Peg Healthy','{:.1f}%'.format(rm2['H']),'{:.1f}%'.format(rm4['H']),'M4' if rm4['H']>rm2['H'] else 'M2'),
]:
    print(f"  {metric:<22} {m2v:>18} {m4v:>18} {w:>6}")

print(f"\n  M2 formula: target = 0.125 × lt/sp → ×{CPI_MULT} CPI → PI(dev, integ, rate) → redemption")
print(f"  M4 formula: target = {HEAT_PEG:.2f} / oracle → instant redemption → arb(create/destroy)")
sys.stdout.flush()
