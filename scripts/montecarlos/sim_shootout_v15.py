#!/usr/bin/env python3
"""
HEAT Model Shootout v15 — Comprehensive Peg Architecture Comparison
=====================================================================
Tests 10 models across 8 scenarios, 200 sims each, 20-year horizon.
Goal: find optimal peg mechanism for Fuego (8M8 max supply constraint).

Models:
  M0   — CPI Band (5:1, $1.50-$2.50, CPI auto-inflation, dormant)
  M1   — Oracle Band (5:1, $1.50-$2.50 clamped, XFG≥$5 gate)
  M2   — 8:1 Full Float (self-ref × 1.58 CPI, PI controller) CURRENT
  M3   — Sigmoid Transition (self-ref → oracle peg, smooth)
  M4   — Fixed Peg (1.58/oracle, instant, two-way arb) PROPOSED
  M4E  — Fixed Peg + EMA oracle (3-epoch smooth, less arb churn)
  M4T  — Fixed Peg + TWAP only (SwapXFG, no Exbitron dependency)
  M4P  — Fixed Peg + PI CD yield (redemption=instant, CD spend=PI-driven)
  T2   — Two-Phase (M2 below $5 XFG, M4 above)
  DB   — Dual-Band (tight 5% PI clamp near peg, relax to 20%)

Scenarios:
  BASELINE — XFG $2→$25, 60% vol, Hearth 5K XFG + 25K HEAT
  HIGH_VOL — 120% vol
  LOW_VOL  — 20% vol
  ORACLE_FAIL — kill oracle at year 5
  XFG_CRASH  — price drops 80% over 1 year at year 5
  XFG_MOON   — price 10x over 1 year at year 5
  THIN_POOL  — 500 XFG + 2500 HEAT (10% depth)
  DEEP_POOL  — 50K XFG + 250K HEAT (10x depth)
"""

import numpy as np, time, sys

EPY=73; N_SIMS=25; N_EPOCHS=EPY*11; N_YEARS=11; DT=1.0/EPY
LAUNCH_5=0.2; LAUNCH_8=0.125; CPI_MULT=1.58; HEAT_PEG=1.58
KP=0.08; KI=0.015; PI_MAX=0.5; PI_CLAMP=1.0
SWAP_FEE=0.02; HEARTH_FEE=0.003
CD_SHARE=0.80; TREAS_SHARE=0.16; REBAL_SHARE=0.04
YEM_LAG=3; YEM_SWF_SAVE=0.60; YEM_SWF_DRIP=0.01; YEM_BURN_SCALP=0.08
TIER_CAP_MIN=0.33; TIER_CAP_MAX=0.80; TIER_FULL=72
SEED=20260601

# ═══════════════════════════════════════════════════════════════
#  Scenario generators
# ═══════════════════════════════════════════════════════════════

def scenario_baseline(rng):
    def f(t): return max(0.5,(2+np.exp(0.12*t))*float(1+rng.normal(0,0.25)))
    return f, 5000, 25000, False, 0

def scenario_high_vol(rng):
    def f(t): return max(0.5,(2+np.exp(0.12*t))*float(1+rng.normal(0,0.50)))
    return f, 5000, 25000, False, 0

def scenario_low_vol(rng):
    def f(t): return max(0.5,(2+np.exp(0.12*t))*float(1+rng.normal(0,0.10)))
    return f, 5000, 25000, False, 0

def scenario_oracle_fail(rng):
    def f(t): return max(0.5,(2+np.exp(0.12*t))*float(1+rng.normal(0,0.25)))
    return f, 5000, 25000, True, 5  # kill oracle at year 5

def scenario_xfg_crash(rng):
    def f(t):
        base = max(0.5,(2+np.exp(0.12*t))*float(1+rng.normal(0,0.25)))
        crash = 1.0 - 0.80*max(0,min(1,(t-5.0)))
        return base*crash
    return f, 5000, 25000, False, 0

def scenario_xfg_moon(rng):
    def f(t):
        base = max(0.5,(2+np.exp(0.12*t))*float(1+rng.normal(0,0.25)))
        moon = 1.0 + 9.0*max(0,min(1,(t-5.0)))
        return base*moon
    return f, 5000, 25000, False, 0

def scenario_thin_pool(rng):
    def f(t): return max(0.5,(2+np.exp(0.12*t))*float(1+rng.normal(0,0.25)))
    return f, 500, 2500, False, 0

def scenario_deep_pool(rng):
    def f(t): return max(0.5,(2+np.exp(0.12*t))*float(1+rng.normal(0,0.25)))
    return f, 50000, 250000, False, 0

def tier_cap(d): return TIER_CAP_MIN+min(max(d,1),TIER_FULL)/TIER_FULL*(TIER_CAP_MAX-TIER_CAP_MIN)

# ═══════════════════════════════════════════════════════════════
#  SIMULATION FUNCTIONS
# ═══════════════════════════════════════════════════════════════

def sim_base(price_fn, px0, ph0, oracle_kill_yr, oracle_kill):
    """Shared infrastructure: oracle, volume, fees, swaps, CD book"""
    px,ph=px0,ph0
    tre=cd=rev=hs=0; swf=0; yemr=0
    sy=np.zeros(N_YEARS);ty=np.zeros(N_YEARS);ry=np.zeros(N_YEARS)
    dy=np.zeros(N_YEARS);hy=np.zeros(N_YEARS);fdy=np.zeros(N_YEARS);xy=np.zeros(N_YEARS)
    swfy=np.zeros(N_YEARS);yemy=np.zeros(N_YEARS);apyy=np.zeros(N_YEARS)
    pd=[];se=0;spiral=False;mx=0
    lag_ep=0;rol=[0]*3;rc=0;ok=False;cd_apy=0;cd_pool_acc=0;cdl=px0*20
    
    oracle_start=max(1,73//2);last_oracle=0;sc=0
    return locals()

def epoch_boundary(s, yr, ph, px, cd_pool_acc, hs, tre, rev, red, oracle_active,
                   sr_mult, swf, yemr, cdl, lag_ep, rol, rc, ok, cd_apy, sy, ty, ry, dy, hy, fdy, xy, swfy, yemy, apyy):
    """YEM v3 epoch boundary: fee split → SWF smoothing → CD buyback"""
    # Fee split
    # (cd_pool_acc already holds accumulated CD share)
    
    if not ok and lag_ep<YEM_LAG:
        lag_ep+=1
        if lag_ep>=YEM_LAG: ok=True
        swf+=cd_pool_acc
    else:
        org=cd_pool_acc/cdl if cdl>0 else 0
        rol[rc%3]=org;rc+=1
        trate=np.mean(rol[:min(rc,3)])
        capped=min(trate,tier_cap(36))
        surp=cd_pool_acc-capped*cdl
        if surp>0:swf+=surp*YEM_SWF_SAVE
        else:
            draw=min(-surp,swf);swf-=draw
            if -surp>draw:yemr-=(-surp-draw)
        cd_apy=(capped+swf*YEM_SWF_DRIP/max(cdl,1))*EPY
        cdl*=1.001
    
    # CD buyback
    if cd_pool_acc>0 and ph>0:
        spend=min(cd_pool_acc,cd_pool_acc*sr_mult)
        hb=ph*spend/(px+spend)
        if 0<hb<ph*0.95:px+=spend;ph-=hb;hs+=hb
    
    # Rebalancer
    if rev>0 and red>0:
        reb=min(rev*0.1,px*0.03);ph+=reb/red;rev-=reb;hs+=reb/red
    
    apyy[yr]=cd_apy;swfy[yr]=swf;yemy[yr]=yemr
    sy[yr]=hs;ty[yr]=tre;ry[yr]=px/max(ph,1e-3);fdy[yr]=red;dy[yr]=px
    xy[yr]=0  # filled by caller
    
    return lag_ep, rol, rc, ok, cd_apy, swf, yemr, cdl, px, ph, hs, rev, tre

def finalize(s, pd, sy, ty, ry, dy, hy, fdy, xy, swfy, yemy, apyy, spiral, mx):
    sk=int(N_EPOCHS*0.3)
    h=np.mean([1 if d<0.2 else 0 for d in pd[sk:]])*100 if pd else 0
    ret=[(ry[i]-ry[i-1])/ry[i-1] for i in range(2,N_YEARS) if ry[i-1]>0]
    vl=np.std(ret) if ret else 0
    return{'S':sy,'T':ty,'R':ry,'Rd':fdy,'D':dy,'HV':hy,'XP':xy,
           'SWF':swfy,'YEMR':yemy,'APY':apyy,'spiral':spiral,'maxD':mx,'H':h,'Vol':vl}

def fmt(v):return f"{v/1e6:7.2f}M" if abs(v)>=1e6 else(f"{v/1e3:7.1f}K" if abs(v)>=1e3 else f"{v:8,.0f}")


# ═══════════════════════════════════════════════════════════════
#  MODEL IMPLEMENTATIONS
# ═══════════════════════════════════════════════════════════════

def run_M0(rng, price_fn, px0, ph0, oracle_kill_yr, oracle_kill):
    """CPI Band: 5:1, $1.50-$2.50 × CPI, auto-inflation 2.5%/yr"""
    s=sim_base(price_fn,px0,ph0,oracle_kill_yr,oracle_kill)
    integ=0;cpi_cur=100;cpi_launch=100;lt=None
    px=s['px'];ph=s['ph'];tre=s['tre'];cd=s['cd'];rev=s['rev'];hs=s['hs']
    swf=s['swf'];yemr=s['yemr'];sr_mult=1.0
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        t=ep/EPY
        xp=s['price_fn'](t)
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(np.random.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;rev+=fees*REBAL_SHARE;s['cd_pool_acc']+=fees*CD_SHARE
        
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if np.random.random()<0.5:c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
        
        sp=px/max(ph,1e-3)
        if lt is None and ep>EPY:lt=sp
        
        # CPI auto-inflation: 2.5%/yr
        if ep%EPY==0:cpi_cur*=(1+0.025/EPY)
        
        if lt is not None and xp>=5.0 and sp>1e-6:
            cpi_ratio=cpi_cur/cpi_launch
            floor=1.50*cpi_ratio;ceil=2.50*cpi_ratio
            hv=sp*xp;tv=np.clip(hv,floor,ceil)
            tgt=tv/xp if xp>0 else LAUNCH_5
        else:
            tgt=LAUNCH_5*lt/sp if(lt and sp>1e-6)else LAUNCH_5
        tgt=max(1e-6,tgt);dev=(sp-tgt)/max(tgt,1e-6)
        integ=np.clip(integ+dev/EPY,-PI_CLAMP,PI_CLAMP)
        red=max(1e-6,tgt*(1+np.clip(KP*dev+KI*integ,-PI_MAX,PI_MAX)/EPY))
        sr_mult=np.clip(1+np.clip(KP*dev+KI*integ,-PI_MAX,PI_MAX),0.2,3)
        
        s['pd'].append(abs(dev));s['mx']=max(s['mx'],abs(dev))
        if abs(dev)>0.5:s['se']+=1
        else:s['se']=max(0,s['se']-1)
        if s['se']>EPY:s['spiral']=True
        
        mp=max(0,0.008-0.4*abs(dev));m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*YEM_BURN_SCALP
        
        if ep>0 and ep%EPY==0:
            out=epoch_boundary(s,yr,ph,px,s['cd_pool_acc'],hs,tre,rev,red,True,sr_mult,swf,yemr,s['cdl'],*[s[k] for k in['lag_ep','rol','rc','ok','cd_apy']],s['sy'],s['ty'],s['ry'],s['dy'],s['hy'],s['fdy'],s['xy'],s['swfy'],s['yemy'],s['apyy'])
            s['lag_ep'],s['rol'],s['rc'],s['ok'],s['cd_apy'],swf,yemr,s['cdl'],px,ph,hs,rev,tre=out[0:14]
            s['cd_pool_acc']=0
        
        s['sy'][yr]=hs;s['ty'][yr]=tre;s['ry'][yr]=sp;s['fdy'][yr]=red;s['dy'][yr]=px;s['hy'][yr]=sp*xp;s['xy'][yr]=xp
    
    return finalize(s,s['pd'],s['sy'],s['ty'],s['ry'],s['dy'],s['hy'],s['fdy'],s['xy'],s['swfy'],s['yemy'],s['apyy'],s['spiral'],s['mx'])


def run_M2(rng, price_fn, px0, ph0, oracle_kill_yr, oracle_kill):
    """8:1 SelfRef × CPI, PI controller (CURRENT)"""
    s=sim_base(price_fn,px0,ph0,oracle_kill_yr,oracle_kill)
    integ=0;lt=None
    px,ph=s['px'],s['ph'];tre=s['tre'];cd=s['cd'];rev=s['rev'];hs=s['hs']
    swf=s['swf'];yemr=s['yemr'];sr_mult=1.0
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=s['price_fn'](ep/EPY)
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(np.random.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;rev+=fees*REBAL_SHARE;s['cd_pool_acc']+=fees*CD_SHARE
        
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if np.random.random()<0.5:c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
        
        sp=px/max(ph,1e-3)
        if lt is None and ep>EPY:lt=sp
        
        tgt=LAUNCH_8*CPI_MULT*lt/sp if(lt and sp>1e-6)else LAUNCH_8*CPI_MULT
        tgt=max(1e-6,tgt);dev=(sp-tgt)/max(tgt,1e-6)
        integ=np.clip(integ+dev/EPY,-PI_CLAMP,PI_CLAMP)
        rr=np.clip(KP*dev+KI*integ,-PI_MAX,PI_MAX)
        red=max(1e-6,tgt*(1+rr/EPY))
        sr_mult=np.clip(1+rr,0.2,3)
        
        s['pd'].append(abs(dev));s['mx']=max(s['mx'],abs(dev))
        if abs(dev)>0.5:s['se']+=1
        else:s['se']=max(0,s['se']-1)
        if s['se']>EPY:s['spiral']=True
        
        mp=max(0,0.008-0.4*abs(dev));m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*YEM_BURN_SCALP
        
        if ep>0 and ep%EPY==0:
            out=epoch_boundary(s,yr,ph,px,s['cd_pool_acc'],hs,tre,rev,red,True,sr_mult,swf,yemr,s['cdl'],*[s[k] for k in['lag_ep','rol','rc','ok','cd_apy']],s['sy'],s['ty'],s['ry'],s['dy'],s['hy'],s['fdy'],s['xy'],s['swfy'],s['yemy'],s['apyy'])
            s['lag_ep'],s['rol'],s['rc'],s['ok'],s['cd_apy'],swf,yemr,s['cdl'],px,ph,hs,rev,tre=out[0:14]
            s['cd_pool_acc']=0
    
    return finalize(s,s['pd'],s['sy'],s['ty'],s['ry'],s['dy'],s['hy'],s['fdy'],s['xy'],s['swfy'],s['yemy'],s['apyy'],s['spiral'],s['mx'])


def run_M4(rng, price_fn, px0, ph0, oracle_kill_yr, oracle_kill, use_ema=False, use_twap_only=False):
    """Fixed peg with two-way arb. Variants: EMA smoothing, TWAP-only oracle."""
    s=sim_base(price_fn,px0,ph0,oracle_kill_yr,oracle_kill)
    px=s['px'];ph=s['ph'];tre=s['tre'];rev=s['rev'];hs=s['hs']
    swf=s['swf'];yemr=s['yemr'];xfb=0;xfc=0;ema_price=0
    
    t0=s['price_fn'](0);tr=HEAT_PEG/t0;ph=px/tr if tr>0 else ph
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        t=ep/EPY;xp=s['price_fn'](t)
        
        # Oracle
        oa=ep>=s['oracle_start']
        if oracle_kill and t>=oracle_kill_yr:oa=False
        os_=np.random.random()<0.05 if oa else True
        op=xp*float(1+np.random.normal(0,0.05))
        if oa and not os_:s['last_oracle']=op;s['sc']=0
        else:s['sc']+=1
        price=s['last_oracle'] if s['last_oracle']>0 else xp
        
        if use_ema:
            alpha=0.30;ema_price=alpha*price+(1-alpha)*ema_price if ema_price>0 else price
            price=ema_price
        
        red=max(1e-6,HEAT_PEG/max(price,0.01))
        
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(np.random.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;rev+=fees*REBAL_SHARE;s['cd_pool_acc']+=fees*CD_SHARE
        
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if np.random.random()<0.5:c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
        
        # Two-way arbitrage
        if s['sc']<EPY and s['last_oracle']>0:
            for _ in range(20):
                s2=px/max(ph,1e-3);hp2=s2*price;gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                if gap<0.002:break
                a=min(px,ph)*0.03
                if hp2>HEAT_PEG:
                    out=px*a*(1-HEARTH_FEE)/(ph+a)
                    if out>0 and out<px:ph+=a;px-=out;hs+=a;xfb+=a*red
                else:
                    out=ph*a*(1-HEARTH_FEE)/(px+a)
                    if out>0 and out<ph:px+=a;ph-=out;hs-=out;xfc+=out*red
        
        sp=px/max(ph,1e-3);php=sp*price
        d=abs(php-HEAT_PEG)/HEAT_PEG;s['pd'].append(d);s['mx']=max(s['mx'],d)
        if d>0.5 and s['sc']>4:s['se']+=1
        else:s['se']=max(0,s['se']-1)
        if s['se']>EPY:s['spiral']=True
        
        mp=max(0,0.008-0.4*d);m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*YEM_BURN_SCALP
        
        if ep>0 and ep%EPY==0:
            out=epoch_boundary(s,yr,ph,px,s['cd_pool_acc'],hs,tre,rev,red,True,1.0,swf,yemr,s['cdl'],*[s[k] for k in['lag_ep','rol','rc','ok','cd_apy']],s['sy'],s['ty'],s['ry'],s['dy'],s['hy'],s['fdy'],s['xy'],s['swfy'],s['yemy'],s['apyy'])
            s['lag_ep'],s['rol'],s['rc'],s['ok'],s['cd_apy'],swf,yemr,s['cdl'],px,ph,hs,rev,tre=out[0:14]
            s['cd_pool_acc']=0
    
    r=finalize(s,s['pd'],s['sy'],s['ty'],s['ry'],s['dy'],s['hy'],s['fdy'],s['xy'],s['swfy'],s['yemy'],s['apyy'],s['spiral'],s['mx'])
    r['xfg_burned']=xfb;r['xfg_created']=xfc;r['net_xfg']=xfc-xfb
    return r


def run_M4P(rng, price_fn, px0, ph0, oracle_kill_yr, oracle_kill):
    """Fixed peg + PI-driven CD spend (hybrid: peg=instant, CD=PI)"""
    s=sim_base(price_fn,px0,ph0,oracle_kill_yr,oracle_kill)
    px=s['px'];ph=s['ph'];tre=s['tre'];rev=s['rev'];hs=s['hs']
    swf=s['swf'];yemr=s['yemr'];xfb=0;xfc=0;integ=0
    
    t0=s['price_fn'](0);tr=HEAT_PEG/t0;ph=px/tr if tr>0 else ph
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=s['price_fn'](ep/EPY)
        oa=ep>=s['oracle_start']
        if oracle_kill and ep/EPY>=oracle_kill_yr:oa=False
        os_=np.random.random()<0.05 if oa else True;op=xp*float(1+np.random.normal(0,0.05))
        if oa and not os_:s['last_oracle']=op;s['sc']=0
        else:s['sc']+=1
        price=s['last_oracle'] if s['last_oracle']>0 else xp
        red=max(1e-6,HEAT_PEG/max(price,0.01))
        
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(np.random.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;rev+=fees*REBAL_SHARE;s['cd_pool_acc']+=fees*CD_SHARE
        
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if np.random.random()<0.5:c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
        
        # Two-way arb (same as M4)
        if s['sc']<EPY and s['last_oracle']>0:
            for _ in range(20):
                s2=px/max(ph,1e-3);hp2=s2*price;gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                if gap<0.002:break
                a=min(px,ph)*0.03
                if hp2>HEAT_PEG:
                    out=px*a*(1-HEARTH_FEE)/(ph+a)
                    if out>0 and out<px:ph+=a;px-=out;hs+=a;xfb+=a*red
                else:
                    out=ph*a*(1-HEARTH_FEE)/(px+a)
                    if out>0 and out<ph:px+=a;ph-=out;hs-=out;xfc+=out*red
        
        sp=px/max(ph,1e-3);php=sp*price
        d=abs(php-HEAT_PEG)/HEAT_PEG;s['pd'].append(d);s['mx']=max(s['mx'],d)
        
        # PI for CD spend multiplier only
        dev=(sp-red)/max(red,1e-6)
        integ=np.clip(integ+dev/EPY,-PI_CLAMP,PI_CLAMP)
        sr_mult=np.clip(1+np.clip(KP*dev+KI*integ,-PI_MAX,PI_MAX),0.2,3)
        
        if d>0.5 and s['sc']>4:s['se']+=1
        else:s['se']=max(0,s['se']-1)
        if s['se']>EPY:s['spiral']=True
        
        mp=max(0,0.008-0.4*d);m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*YEM_BURN_SCALP
        
        if ep>0 and ep%EPY==0:
            out=epoch_boundary(s,yr,ph,px,s['cd_pool_acc'],hs,tre,rev,red,True,sr_mult,swf,yemr,s['cdl'],*[s[k] for k in['lag_ep','rol','rc','ok','cd_apy']],s['sy'],s['ty'],s['ry'],s['dy'],s['hy'],s['fdy'],s['xy'],s['swfy'],s['yemy'],s['apyy'])
            s['lag_ep'],s['rol'],s['rc'],s['ok'],s['cd_apy'],swf,yemr,s['cdl'],px,ph,hs,rev,tre=out[0:14]
            s['cd_pool_acc']=0
    
    r=finalize(s,s['pd'],s['sy'],s['ty'],s['ry'],s['dy'],s['hy'],s['fdy'],s['xy'],s['swfy'],s['yemy'],s['apyy'],s['spiral'],s['mx'])
    r['xfg_burned']=xfb;r['xfg_created']=xfc;r['net_xfg']=xfc-xfb
    return r


def run_T2(rng, price_fn, px0, ph0, oracle_kill_yr, oracle_kill):
    """Two-Phase: M2 below $5, M4 above"""
    s=sim_base(price_fn,px0,ph0,oracle_kill_yr,oracle_kill)
    integ=0;lt=None;px=s['px'];ph=s['ph'];tre=s['tre'];rev=s['rev'];hs=s['hs']
    swf=s['swf'];yemr=s['yemr'];xfb=0;xfc=0

    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=s['price_fn'](ep/EPY)
        oa=ep>=s['oracle_start']
        if oracle_kill and ep/EPY>=oracle_kill_yr:oa=False
        os_=np.random.random()<0.05 if oa else True;op=xp*float(1+np.random.normal(0,0.05))
        if oa and not os_:s['last_oracle']=op;s['sc']=0
        else:s['sc']+=1
        price=s['last_oracle'] if s['last_oracle']>0 else xp
        
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(np.random.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;rev+=fees*REBAL_SHARE;s['cd_pool_acc']+=fees*CD_SHARE
        
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if np.random.random()<0.5:c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
        
        sp=px/max(ph,1e-3)
        
        if price>=5.0 and s['last_oracle']>0:
            # M4 mode
            red=max(1e-6,HEAT_PEG/max(price,0.01))
            dev_peg=abs(sp*price-HEAT_PEG)/HEAT_PEG;dev=dev_peg
            sr_mult=1.0
            # M4 arb
            if s['sc']<EPY:
                for _ in range(20):
                    s2=px/max(ph,1e-3);hp2=s2*price;gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                    if gap<0.002:break
                    a=min(px,ph)*0.03
                    if hp2>HEAT_PEG:
                        out=px*a*(1-HEARTH_FEE)/(ph+a)
                        if out>0 and out<px:ph+=a;px-=out;hs+=a;xfb+=a*red
                    else:
                        out=ph*a*(1-HEARTH_FEE)/(px+a)
                        if out>0 and out<ph:px+=a;ph-=out;hs-=out;xfc+=out*red
        else:
            # M2 mode
            if lt is None and ep>EPY:lt=sp
            tgt=LAUNCH_8*CPI_MULT*lt/sp if(lt and sp>1e-6)else LAUNCH_8*CPI_MULT
            tgt=max(1e-6,tgt);dev=(sp-tgt)/max(tgt,1e-6)
            integ=np.clip(integ+dev/EPY,-PI_CLAMP,PI_CLAMP)
            rr=np.clip(KP*dev+KI*integ,-PI_MAX,PI_MAX)
            red=max(1e-6,tgt*(1+rr/EPY))
            sr_mult=np.clip(1+rr,0.2,3)
        
        s['pd'].append(abs(dev));s['mx']=max(s['mx'],abs(dev))
        if abs(dev)>0.5:s['se']+=1
        else:s['se']=max(0,s['se']-1)
        if s['se']>EPY:s['spiral']=True
        
        mp=max(0,0.008-0.4*abs(dev));m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*YEM_BURN_SCALP
        
        if ep>0 and ep%EPY==0:
            out=epoch_boundary(s,yr,ph,px,s['cd_pool_acc'],hs,tre,rev,red,True,sr_mult,swf,yemr,s['cdl'],*[s[k] for k in['lag_ep','rol','rc','ok','cd_apy']],s['sy'],s['ty'],s['ry'],s['dy'],s['hy'],s['fdy'],s['xy'],s['swfy'],s['yemy'],s['apyy'])
            s['lag_ep'],s['rol'],s['rc'],s['ok'],s['cd_apy'],swf,yemr,s['cdl'],px,ph,hs,rev,tre=out[0:14]
            s['cd_pool_acc']=0
    
    r=finalize(s,s['pd'],s['sy'],s['ty'],s['ry'],s['dy'],s['hy'],s['fdy'],s['xy'],s['swfy'],s['yemy'],s['apyy'],s['spiral'],s['mx'])
    r['xfg_burned']=xfb;r['xfg_created']=xfc;r['net_xfg']=xfc-xfb
    return r


# ═══════════════════════════════════════════════════════════════
#  RUNNER
# ═══════════════════════════════════════════════════════════════

SCENARIOS=[
    ('BASELINE',scenario_baseline),
    ('HIGH_VOL',scenario_high_vol),
    ('LOW_VOL',scenario_low_vol),
    ('ORACLE_FAIL',scenario_oracle_fail),
    ('XFG_CRASH',scenario_xfg_crash),
    ('XFG_MOON',scenario_xfg_moon),
    ('THIN_POOL',scenario_thin_pool),
    ('DEEP_POOL',scenario_deep_pool),
]

MODELS=[
    ('M2: SelfRef×CPI',run_M2),
    ('M4: FixedPeg',lambda rng,fn,px0,ph0,ok,oky:run_M4(rng,fn,px0,ph0,ok,oky)),
    ('M4E: +EMA oracle',lambda rng,fn,px0,ph0,ok,oky:run_M4(rng,fn,px0,ph0,ok,oky,use_ema=True)),
    ('M4P: +PI CD yield',run_M4P),
    ('T2: Two-Phase',run_T2),
]

print(f"\n{'='*140}")
print(f"  HEAT MODEL SHOOTOUT v15 — {len(MODELS)} models × {len(SCENARIOS)} scenarios × {N_SIMS} sims × 20yr")
print(f"{'='*140}")

all_results={}

for sn,sfn in SCENARIOS:
    print(f"\n  [{sn}]")
    for mn,mfn in MODELS:
        sys.stdout.write(f"    {mn:<22} ");sys.stdout.flush();t0=time.time()
        batch=[]
        for s in range(N_SIMS):
            rng=np.random.RandomState(SEED+hash(sn+mn+str(s))%(2**30))
            fn,px0,ph0,ok,oky=sfn(rng)
            batch.append(mfn(rng,fn,px0,ph0,ok,oky))
        dt=time.time()-t0
        
        med={k:[np.median([b[k][y] for b in batch]) for y in range(N_YEARS)] for k in['S','T','R','Rd','D','HV','XP','SWF','YEMR','APY']}
        for k in['H','Vol','maxD']:med[k]=np.median([b[k] for b in batch])
        med['spiral']=np.mean([b['spiral'] for b in batch])*100
        if'xfg_burned' in batch[0]:med['netX']=np.median([b['net_xfg'] for b in batch])
        
        all_results[(sn,mn)]=med
        hv=med['R'][10]*med['XP'][10] if med['XP'][10]>0 else 0
        print(f"{dt:3.0f}s  H={med['H']:.0f}%  spr={med['spiral']:.1f}%  maxD={med['maxD']*100:.0f}%  "
              f"HV10=${hv:.2f}  T10={fmt(med['T'][10])}  APY10={med['APY'][10]:.1f}%")

# Summary matrix
print(f"\n{'='*140}")
print(f"  SUMMARY MATRIX — HEAT Pool Price at Year 10 (target: ${HEAT_PEG:.2f})")
print(f"{'='*140}")
models_print=[m[0] for m in MODELS]
print(f"  {'SCENARIO':<16}",end='')
for m in models_print:print(f" {m:<24}",end='')
print()
print("  "+"─"*115)
for sn,_ in SCENARIOS:
    print(f"  {sn:<16}",end='')
    for mn in models_print:
        r=all_results[(sn,mn)];hv=r['R'][10]*r['XP'][10] if r['XP'][10]>0 else 0
        mark='✓' if abs(hv-1.58)<0.31 else('~' if abs(hv-1.58)<1.58 else '✗')
        print(f" ${hv:>5.2f}{mark:<19}",end='')
    print()

# Score matrix
print(f"\n{'='*140}")
print(f"  COMPOSITE SCORE — Higher = Better (PegAccuracy×30% + LowSpiral×25% + Treasury×25% + LowMaxDev×20%)")
print(f"{'='*140}")
for sn,_ in SCENARIOS:
    best=None;best_n=None
    for mn in models_print:
        r=all_results[(sn,mn)];hv=r['R'][10]*r['XP'][10] if r['XP'][10]>0 else 0
        peg_score=max(0,100-abs(hv-1.58)/1.58*100)/100
        spr_score=(100-r['spiral'])/100
        tre_score=r['T'][10]/max(1,200000)/100
        dev_score=max(0,100-r['maxD']*100)/100/10
        score=peg_score*0.30+spr_score*0.25+tre_score*0.25+dev_score*0.20
        if best is None or score>best:best=score;best_n=mn
        all_results[(sn,mn)]['_score']=score
    # Mark winner
    for mn in models_print:
        r=all_results[(sn,mn)]
        mark=' ★' if mn==best_n else '  '
        print(f"  {sn:<16} {mn:<22} {r['_score']:.3f}{mark}")

sys.stdout.flush()
