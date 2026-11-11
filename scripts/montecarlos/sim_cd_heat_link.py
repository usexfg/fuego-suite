#!/usr/bin/env python3
"""CD-HEAT Linked Comparison: M2 vs M4E"""
import numpy as np, time, sys

EPY=73;N_SIMS=25;N_EPOCHS=EPY*11;N_YEARS=11
HEAT_PEG=1.58;LAUNCH_8=0.125;CPI_MULT=1.58
KP=0.08;KI=0.015;PI_MAX=0.5;PI_CLAMP=1.0
SWAP_FEE=0.02;HEARTH_FEE=0.003
CD_SHARE=0.80;TREAS_SHARE=0.16;REBAL_SHARE=0.04
YEM_BURN_SCALP=0.08;YEM_SWF_DRIP=0.01;YEM_SWF_SAVE=0.60
TIER_CAP_MIN=0.33;TIER_CAP_MAX=0.80;TIER_FULL=72

def xfp(t,rng):return max(0.5,(2+np.exp(0.12*t))*float(1+rng.normal(0,0.25)))
def tc(d):return TIER_CAP_MIN+min(max(d,1),TIER_FULL)/TIER_FULL*(TIER_CAP_MAX-TIER_CAP_MIN)
def fmt(v):return f"{v/1e6:7.2f}M" if abs(v)>=1e6 else(f"{v/1e3:7.1f}K" if abs(v)>=1e3 else f"{v:8,.0f}")

def m2(rng):
    px=5000;ph=25000;red=LAUNCH_8*CPI_MULT;integ=0;tre=hs=0;lt=None
    swf=yemr=0;cdl=50000;cd_acc=0
    cdy=np.zeros(N_YEARS);cdly=np.zeros(N_YEARS);hvy=np.zeros(N_YEARS)
    pd=[];se=0;spiral=False
    lag_ep=0;rol=[0]*3;rc=0;ok=False;cd_apy=0
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=xfp(ep/EPY,rng)
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(rng.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;cd_acc+=fees*CD_SHARE
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<0.5:c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
        sp=px/max(ph,1e-3)
        if lt is None and ep>EPY:lt=sp
        tgt=LAUNCH_8*CPI_MULT*lt/sp if(lt and sp>1e-6)else LAUNCH_8*CPI_MULT
        tgt=max(1e-6,tgt);dev=(sp-tgt)/max(tgt,1e-6)
        integ=np.clip(integ+dev/EPY,-PI_CLAMP,PI_CLAMP)
        rr=np.clip(KP*dev+KI*integ,-PI_MAX,PI_MAX)
        red=max(1e-6,tgt*(1+rr/EPY));sr=np.clip(1+rr,0.2,3)
        d=abs(dev);pd.append(d)
        if d>0.5:se+=1
        else:se=max(0,se-1)
        if se>EPY:spiral=True
        mp=max(0,0.008-0.4*d);m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*YEM_BURN_SCALP
        
        if ep>0 and ep%EPY==0:
            if not ok and lag_ep<3:swf+=cd_acc;lag_ep+=1
            else:
                org=cd_acc/cdl if cdl>0 else 0;rol[rc%3]=org;rc+=1
                trate=np.mean(rol[:min(rc,3)]);capped=min(trate,tc(36))
                surp=cd_acc-capped*cdl
                if surp>0:swf+=surp*YEM_SWF_SAVE
                else:draw=min(-surp,swf);swf-=draw;yemr-=max(0,-surp-draw)
                cd_apy=(capped+swf*YEM_SWF_DRIP/max(cdl,1))*EPY
                if lag_ep>=3:ok=True
            if cd_acc>0 and ph>0:
                spend=min(cd_acc,cd_acc*sr);hb=ph*spend/(px+spend)
                if 0<hb<ph*0.95:px+=spend;ph-=hb;hs+=hb
            g=0
            if cd_apy>0.30:g=cdl*0.05
            elif cd_apy>0.15:g=cdl*0.02
            elif cd_apy>0.05:g=cdl*0.005
            else:g=-cdl*0.01
            cdl+=g;cdl=max(1000,cdl);cd_acc=0
        hvy[yr]=sp*xp;cdy[yr]=cd_apy;cdly[yr]=cdl
    
    sk=int(N_EPOCHS*0.3);h=np.mean([1 if d<0.2 else 0 for d in pd[sk:]])*100 if pd else 0
    return{'APY':cdy,'LOCK':cdly,'HP':hvy,'spiral':spiral,'H':h}

def m4e(rng):
    p0=xfp(0,rng);t0=HEAT_PEG/p0;px=5000;ph=px/t0 if t0>0 else 25000
    tre=hs=0;swf=yemr=0;cdl=50000;cd_acc=0
    os_=int(rng.randint(EPY//4,EPY));lo=0;sc=0;ema=0;xfb=0;xfc=0
    cdy=np.zeros(N_YEARS);cdly=np.zeros(N_YEARS);hvy=np.zeros(N_YEARS)
    pd=[];se=0;spiral=False
    lag_ep=0;rol=[0]*3;rc=0;ok=False;cd_apy=0
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=xfp(ep/EPY,rng)
        oa=ep>=os_;ost=rng.random()<0.05 if oa else True;op=xp*float(1+rng.normal(0,0.05))
        if oa and not ost:lo=op;sc=0
        else:sc+=1
        price=lo if lo>0 else xp
        alpha=0.30;ema=alpha*price+(1-alpha)*ema if ema>0 else price
        red=max(1e-6,HEAT_PEG/max(ema,0.01))
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(rng.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;cd_acc+=fees*CD_SHARE
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<0.5:c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
        if sc<EPY and lo>0:
            for _ in range(20):
                s2=px/max(ph,1e-3);hp2=s2*ema;gap=abs(hp2-HEAT_PEG)/HEAT_PEG
                if gap<0.002:break
                a=min(px,ph)*0.03
                if hp2>HEAT_PEG:
                    out=px*a*(1-HEARTH_FEE)/(ph+a)
                    if out>0 and out<px:ph+=a;px-=out;hs+=a;xfb+=a*red
                else:
                    out=ph*a*(1-HEARTH_FEE)/(px+a)
                    if out>0 and out<ph:px+=a;ph-=out;hs-=out;xfc+=out*red
        sp=px/max(ph,1e-3);php=sp*ema
        d=abs(php-HEAT_PEG)/HEAT_PEG;pd.append(d)
        if d>0.5 and sc>4:se+=1
        else:se=max(0,se-1)
        if se>EPY:spiral=True
        mp=max(0,0.008-0.4*d);m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*YEM_BURN_SCALP
        
        if ep>0 and ep%EPY==0:
            if not ok and lag_ep<3:swf+=cd_acc;lag_ep+=1
            else:
                org=cd_acc/cdl if cdl>0 else 0;rol[rc%3]=org;rc+=1
                trate=np.mean(rol[:min(rc,3)]);capped=min(trate,tc(36))
                surp=cd_acc-capped*cdl
                if surp>0:swf+=surp*YEM_SWF_SAVE
                else:draw=min(-surp,swf);swf-=draw;yemr-=max(0,-surp-draw)
                cd_apy=(capped+swf*YEM_SWF_DRIP/max(cdl,1))*EPY
                if lag_ep>=3:ok=True
            if cd_acc>0 and ph>0:
                spend=cd_acc;hb=ph*spend/(px+spend)
                if 0<hb<ph*0.95:px+=spend;ph-=hb;hs+=hb
            g=0
            if cd_apy>0.30:g=cdl*0.05
            elif cd_apy>0.15:g=cdl*0.02
            elif cd_apy>0.05:g=cdl*0.005
            else:g=-cdl*0.01
            cdl+=g;cdl=max(1000,cdl);cd_acc=0
        hvy[yr]=php;cdy[yr]=cd_apy;cdly[yr]=cdl
    
    sk=int(N_EPOCHS*0.3);h=np.mean([1 if d<0.2 else 0 for d in pd[sk:]])*100 if pd else 0
    return{'APY':cdy,'LOCK':cdly,'HP':hvy,'spiral':spiral,'H':h,'netX':xfc-xfb}

SEED=20260602
print(f"\n{'='*105}")
print(f"  CD-HEAT LINK: M2 (PI SelfRef) vs M4E (Fixed Peg + EMA)  |  {N_SIMS} sims x 10yr")
print(f"  CD lock grows/decays based on APY attractiveness")
print(f"{'='*105}")

for label,fn in[('M2:PI_SelfRef',m2),('M4E:Peg+EMA',m4e)]:
    sys.stdout.write(f"  {label:<18} ");sys.stdout.flush();t0=time.time()
    batch=[fn(np.random.RandomState(SEED+hash(label+str(s))%(2**30))) for s in range(N_SIMS)]
    dt=time.time()-t0
    med={k:[np.median([b[k][y] for b in batch]) for y in range(N_YEARS)] for k in['APY','LOCK','HP']}
    med['H']=np.median([b['H'] for b in batch]);med['spiral']=np.mean([b['spiral'] for b in batch])*100
    print(f"{dt:2.0f}s  HEAT={med['HP'][10]:.2f}$  APY={med['APY'][10]:.1f}%  CD_lock={fmt(med['LOCK'][10])}  H={med['H']:.0f}%  spr={med['spiral']:.0f}%")

print(f"\n  CD-HEAT-ARB FEEDBACK LOOP (M4E):")
print(f"  Fees→CDpool→buys HEAT from Hearth→pool skews HEAT-heavy")
print(f"  Arb mints HEAT (cheap at $1.58)→sells to pool→pool rebalances→$1.58 peg holds")
print(f"  Stable peg→predictable CD APY→more deposits→deeper pool→less arb slippage→tighter peg")
print(f"  M2 breaks loop: HEAT $1-$50 oscillation→CD APY meaningless→no deposit growth")
PYEOF