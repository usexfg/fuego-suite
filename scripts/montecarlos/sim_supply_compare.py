#!/usr/bin/env python3
"""M2 vs M4E: Supply comparison + PI→CD linkage analysis"""
import numpy as np, sys

EPY=73;N_SIMS=25;N_EPOCHS=EPY*11;N_YEARS=11;HEAT_PEG=1.58;LAUNCH_8=0.125;CPI=1.58
KP=0.08;KI=0.015;PI_MAX=0.5;PI_CLAMP=1.0;HEARTH_FEE=0.003
SWAP_FEE=0.02;CD_SHARE=0.80;BURN_SCALP=0.08;YEM_SWF_DRIP=0.01

def xfp(t,rng): return max(0.5,(2+np.exp(0.12*t))*float(1+rng.normal(0,0.25)))
def tc(d):
    return 0.33+min(max(d,1),72)/72*(0.80-0.33)
def fm(v): return f"{v/1e6:7.2f}M" if abs(v)>=1e6 else(f"{v/1e3:7.1f}K" if abs(v)>=1e3 else f"{v:8,.0f}")

def m2(rng):
    px,ph=5000,25000;red=LAUNCH_8*CPI;integ=0;hs=0;lt=None
    cdl=50000;cd_acc=0;swf=0;yemr=0
    sy=np.zeros(N_YEARS);hpy=np.zeros(N_YEARS);lky=np.zeros(N_YEARS);apy_y=np.zeros(N_YEARS)
    pd=[];se=0;spiral=False;le=0;ro=[0]*3;rc=0;ok=False;apy=0
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=xfp(ep/EPY,rng)
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(rng.normal(0,0.1)))
        fees=ve*SWAP_FEE;cd_acc+=fees*CD_SHARE
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<0.5:c=min(sv/3,px*0.005);ro_=c/(px+c);ph-=ph*ro_;px+=c
                else:c=min(sv/3,ph*0.005);ro_=c/(ph+c);px-=px*ro_;ph+=c
        sp=px/max(ph,1e-3)
        if lt is None and ep>EPY:lt=sp
        tgt=LAUNCH_8*CPI*lt/sp if(lt and sp>1e-6)else LAUNCH_8*CPI
        tgt=max(1e-6,tgt);dev=(sp-tgt)/max(tgt,1e-6)
        integ=np.clip(integ+dev/EPY,-PI_CLAMP,PI_CLAMP)
        rr=np.clip(KP*dev+KI*integ,-PI_MAX,PI_MAX)
        red=max(1e-6,tgt*(1+rr/EPY));sr=np.clip(1+rr,0.2,3)
        d=abs(dev);pd.append(d)
        if d>0.5:se+=1
        else:se=max(0,se-1)
        if se>EPY:spiral=True
        mp=max(0,0.008-0.4*d);m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*BURN_SCALP
        
        if ep>0 and ep%EPY==0:
            if not ok and le<3:swf+=cd_acc;le+=1
            if le>=3:ok=True
            if ok:
                org=cd_acc/cdl if cdl>0 else 0;ro[rc%3]=org;rc+=1
                trate=np.mean(ro[:min(rc,3)]);capped=min(trate,tc(36))
                surp=cd_acc-capped*cdl
                if surp>0:swf+=surp*0.60
                else:draw=min(-surp,swf);swf-=draw;yemr-=max(0,-surp-draw)
                apy=(capped+swf*YEM_SWF_DRIP/max(cdl,1))*EPY
            if cd_acc>0 and ph>0:
                spend=min(cd_acc,cd_acc*sr);hb=ph*spend/(px+spend)
                if 0<hb<ph*0.95:px+=spend;ph-=hb;hs+=hb
            g=0
            if apy>0.30:g=cdl*0.05
            elif apy>0.15:g=cdl*0.02
            elif apy>0.05:g=cdl*0.005
            else:g=-cdl*0.01
            cdl+=g;cdl=max(1000,cdl);cd_acc=0
        hpy[yr]=sp*xp;sy[yr]=hs;lky[yr]=cdl;apy_y[yr]=apy
    
    sk=int(N_EPOCHS*0.3)
    h=np.mean([1 if d<0.2 else 0 for d in pd[sk:]])*100 if pd else 0
    return{'S':sy,'HP':hpy,'LK':lky,'APY':apy_y,'spiral':spiral,'H':h}

def m4e(rng):
    p0=xfp(0,rng);t0=HEAT_PEG/p0;px=5000;ph=px/t0 if t0>0 else 25000
    hs=0;cdl=50000;cd_acc=0;swf=yemr=0;xfb=xfc=0
    ostart=int(rng.randint(EPY//4,EPY));lo=0;sc=0;ema=0
    sy=np.zeros(N_YEARS);hpy=np.zeros(N_YEARS);lky=np.zeros(N_YEARS);apy_y=np.zeros(N_YEARS)
    pd=[];se=0;spiral=False;le=0;ro=[0]*3;rc=0;ok=False;apy=0
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=xfp(ep/EPY,rng)
        oa=ep>=ostart;ost=rng.random()<0.05 if oa else True;op=xp*float(1+rng.normal(0,0.05))
        if oa and not ost:lo=op;sc=0
        else:sc+=1
        price=lo if lo>0 else xp
        ema=0.30*price+0.70*ema if ema>0 else price
        red=max(1e-6,HEAT_PEG/max(ema,0.01))
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(rng.normal(0,0.1)))
        fees=ve*SWAP_FEE;cd_acc+=fees*CD_SHARE
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<0.5:c=min(sv/3,px*0.005);ro_=c/(px+c);ph-=ph*ro_;px+=c
                else:c=min(sv/3,ph*0.005);ro_=c/(ph+c);px-=px*ro_;ph+=c
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
        mp=max(0,0.008-0.4*d);m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*BURN_SCALP
        
        if ep>0 and ep%EPY==0:
            if not ok and le<3:swf+=cd_acc;le+=1
            if le>=3:ok=True
            if ok:
                org=cd_acc/cdl if cdl>0 else 0;ro[rc%3]=org;rc+=1
                trate=np.mean(ro[:min(rc,3)]);capped=min(trate,tc(36))
                surp=cd_acc-capped*cdl
                if surp>0:swf+=surp*0.60
                else:draw=min(-surp,swf);swf-=draw;yemr-=max(0,-surp-draw)
                apy=(capped+swf*YEM_SWF_DRIP/max(cdl,1))*EPY
            if cd_acc>0 and ph>0:
                spend=cd_acc;hb=ph*spend/(px+spend)
                if 0<hb<ph*0.95:px+=spend;ph-=hb;hs+=hb
            g=0
            if apy>0.30:g=cdl*0.05
            elif apy>0.15:g=cdl*0.02
            elif apy>0.05:g=cdl*0.005
            else:g=-cdl*0.01
            cdl+=g;cdl=max(1000,cdl);cd_acc=0
        hpy[yr]=php;sy[yr]=hs;lky[yr]=cdl;apy_y[yr]=apy
    
    sk=int(N_EPOCHS*0.3)
    h=np.mean([1 if d<0.2 else 0 for d in pd[sk:]])*100 if pd else 0
    return{'S':sy,'HP':hpy,'LK':lky,'APY':apy_y,'spiral':spiral,'H':h,'netX':xfc-xfb}

SEED=20260603
print(f"\n{'='*110}")
print(f"  M2 vs M4E: Supply + PI→CD Link Analysis  |  {N_SIMS} sims x 10yr  |  HEAT target=${HEAT_PEG:.2f}")
print(f"{'='*110}")

models={'M2:PI SelfRef':m2,'M4E:FixedPeg':m4e}
for name,fn in models.items():
    sys.stdout.write(f"  {name:<22} ");sys.stdout.flush()
    batch=[fn(np.random.RandomState(SEED+hash(name+str(s))%(2**30))) for s in range(N_SIMS)]
    med={k:[np.median([b[k][y] for b in batch]) for y in range(N_YEARS)] for k in['S','HP','LK','APY']}
    med['H']=np.median([b['H'] for b in batch]);med['spiral']=np.mean([b['spiral'] for b in batch])*100
    if'netX'in batch[0]:med['netX']=np.median([b['netX'] for b in batch])
    else:med['netX']=0
    
    print(f"HEAT$={med['HP'][10]:.2f}  S10={fm(med['S'][10])}  CD_APY={med['APY'][10]:.1f}%  "
          f"CD_lock={fm(med['LK'][10])}  H={med['H']:.0f}%  spr={med['spiral']:.0f}%  netX={fm(med['netX'])}")

    if'Fixed'in name:
        print(f"      Year │ HEAT pool $ │ Supply (HEAT) │ CD APY  │ CD Locked (XFG)")
        for y in[1,3,5,7,10]:
            print(f"      Y{y:>2d}  │ ${med['HP'][y]:>9.2f}  │ {fm(med['S'][y]):>13} │ {med['APY'][y]:>5.1f}%  │ {fm(med['LK'][y])}")
