#!/usr/bin/env python3
import numpy as np, time, sys
from collections import OrderedDict

EPY=73;N_SIMS=100;N_EPOCHS=EPY*11;N_YEARS=11;LAUNCH_8=0.125;CPI_MULT=1.58;HEAT_PEG=1.58
KP=0.08;KI=0.015;PI_MAX=0.5;PI_CLAMP=1.0;SWAP_FEE=0.02;HEARTH_FEE=0.003
CD_SHARE=0.80;TREAS_SHARE=0.16;REBAL_SHARE=0.04
YEM_LAG=3;YEM_SWF_SAVE=0.60;YEM_SWF_DRIP=0.01;YEM_BURN_SCALP=0.08
TIER_CAP_MIN=0.33;TIER_CAP_MAX=0.80;TIER_FULL=72

def xfp(t,rng):
    return max(0.5,(2+np.exp(0.15*t))*float(1+rng.normal(0,0.25)))
def tc(d):
    return TIER_CAP_MIN+min(max(d,1),TIER_FULL)/TIER_FULL*(TIER_CAP_MAX-TIER_CAP_MIN)

def m2(rng):
    px=5000;ph=25000;red=LAUNCH_8*CPI_MULT;integ=0;tre=cd=hs=0;lt=None
    swf=0;yemr=0;rev=0;cdl=10000
    sy=np.zeros(N_YEARS);ty=np.zeros(N_YEARS);ry=np.zeros(N_YEARS)
    dy=np.zeros(N_YEARS);hy=np.zeros(N_YEARS);fdy=np.zeros(N_YEARS);xy=np.zeros(N_YEARS)
    swfy=np.zeros(N_YEARS);yemy=np.zeros(N_YEARS);apyy=np.zeros(N_YEARS)
    pd=[];se=0;spiral=False;mx=0;lag_ep=0;rol=[0]*3;rc=0;ok=False;apy=0
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=xfp(ep/EPY,rng)
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(rng.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;rev+=fees*REBAL_SHARE;cd+=fees*CD_SHARE
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<0.5:
                    c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:
                    c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
        sp=px/max(ph,1e-3)
        if lt is None and ep>EPY:lt=sp
        tgt=LAUNCH_8*CPI_MULT*lt/sp if(lt and sp>1e-6)else LAUNCH_8*CPI_MULT
        tgt=max(1e-6,tgt);dev=(sp-tgt)/max(tgt,1e-6)
        integ=np.clip(integ+dev/EPY,-PI_CLAMP,PI_CLAMP)
        rr=np.clip(KP*dev+KI*integ,-PI_MAX,PI_MAX)
        red=max(1e-6,tgt*(1+rr/EPY));sr=np.clip(1+rr,0.2,3)
        d=abs(dev);pd.append(d);mx=max(mx,d)
        if d>0.5:se+=1
        else:se=max(0,se-1)
        if se>EPY:spiral=True
        mp=max(0,0.008-0.4*d);m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*YEM_BURN_SCALP
        if cd>0 and ph>0:
            spd=min(cd,cd*sr);hb=ph*spd/(px+spd)
            if 0<hb<ph*0.95:px+=spd;ph-=hb;hs+=hb;cd-=spd
        if ep>0 and ep%EPY==0:
            if not ok and lag_ep<YEM_LAG:
                swf+=cd;cd=0;lag_ep+=1
                if lag_ep>=YEM_LAG:ok=True
            else:
                org=cd/cdl if cdl>0 else 0;rol[rc%3]=org;rc+=1
                trate=np.mean(rol[:min(rc,3)]);capped=min(trate,tc(36))
                surp=cd-capped*cdl
                if surp>0:swf+=surp*YEM_SWF_SAVE
                else:draw=min(-surp,swf);swf-=draw;yemr-=max(0,-surp-draw)
                apy=(capped+swf/100*YEM_SWF_DRIP)*EPY;cdl*=1.001
            if rev>0 and red>0:
                reb=min(rev*0.1,px*0.03);ph+=reb/red;rev-=reb;hs+=reb/red
        apyy[yr]=apy;swfy[yr]=swf;yemy[yr]=yemr
        sy[yr]=hs;ty[yr]=tre;ry[yr]=sp;fdy[yr]=red;dy[yr]=px;hy[yr]=sp*xp;xy[yr]=xp
    sk=int(N_EPOCHS*0.3);h=np.mean([1 if d<0.2 else 0 for d in pd[sk:]])*100 if pd else 0
    ret=[(ry[i]-ry[i-1])/ry[i-1] for i in range(2,N_YEARS) if ry[i-1]>0];vl=np.std(ret) if ret else 0
    return{'S':sy,'T':ty,'R':ry,'Rd':fdy,'D':dy,'HV':hy,'XP':xy,'SWF':swfy,'YEMR':yemy,'APY':apyy,'spiral':spiral,'maxD':mx,'H':h,'Vol':vl}

def m4(rng):
    p0=xfp(0,rng);t0=HEAT_PEG/p0;px=5000;ph=px/t0 if t0>0 else 25000
    tre=cd=hs=0;xfb=0;xfc=0;swf=0;yemr=0;rev=0;cdl=10000
    oracle_start=int(rng.randint(EPY//4,EPY));last_oracle=0;sc=0;ts=0
    sy=np.zeros(N_YEARS);ty=np.zeros(N_YEARS);ry=np.zeros(N_YEARS)
    dy=np.zeros(N_YEARS);hy=np.zeros(N_YEARS);fdy=np.zeros(N_YEARS);xy=np.zeros(N_YEARS)
    swfy=np.zeros(N_YEARS);yemy=np.zeros(N_YEARS);apyy=np.zeros(N_YEARS)
    pd=[];se=0;spiral=False;mx=0;lag_ep=0;rol=[0]*3;rc=0;ok=False;apy=0
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        xp=xfp(ep/EPY,rng)
        oa=ep>=oracle_start;os_=rng.random()<0.05 if oa else True;op=xp*float(1+rng.normal(0,0.05))
        if oa and not os_:last_oracle=op;sc=0
        else:sc+=1;ts+=1
        price=last_oracle if last_oracle>0 else xp
        tgt=HEAT_PEG/max(price,0.01);red=tgt
        ve=2e4*np.clip(xp/2,0.3,5)*float(np.exp(rng.normal(0,0.1)))
        fees=ve*SWAP_FEE;tre+=fees*TREAS_SHARE;rev+=fees*REBAL_SHARE;cd+=fees*CD_SHARE
        sv=ve*0.05
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<0.5:
                    c=min(sv/3,px*0.005);ro=c/(px+c);ph-=ph*ro;px+=c
                else:
                    c=min(sv/3,ph*0.005);ro=c/(ph+c);px-=px*ro;ph+=c
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
        mp=max(0,0.008-0.4*d);m=ve*0.08/max(sp,1e-3)*mp;hs+=m*0.95;yemr+=m*YEM_BURN_SCALP
        if cd>0 and ph>0:
            spd=cd;hb=ph*spd/(px+spd)
            if 0<hb<ph*0.95:px+=spd;ph-=hb;hs+=hb;cd-=spd
        if ep>0 and ep%EPY==0:
            if not ok and lag_ep<YEM_LAG:
                swf+=cd;cd=0;lag_ep+=1
                if lag_ep>=YEM_LAG:ok=True
            else:
                org=cd/cdl if cdl>0 else 0;rol[rc%3]=org;rc+=1
                trate=np.mean(rol[:min(rc,3)]);capped=min(trate,tc(36))
                surp=cd-capped*cdl
                if surp>0:swf+=surp*YEM_SWF_SAVE
                else:draw=min(-surp,swf);swf-=draw;yemr-=max(0,-surp-draw)
                apy=(capped+swf/100*YEM_SWF_DRIP)*EPY;cdl*=1.001
            if rev>0 and red>0:
                reb=min(rev*0.1,px*0.03);ph+=reb/red;rev-=reb;hs+=reb/red
        apyy[yr]=apy;swfy[yr]=swf;yemy[yr]=yemr
        sy[yr]=hs;ty[yr]=tre;ry[yr]=sp;fdy[yr]=red;dy[yr]=px;hy[yr]=php;xy[yr]=price
    sk=int(N_EPOCHS*0.3);h=np.mean([1 if d<0.2 else 0 for d in pd[sk:]])*100 if pd else 0
    ret=[(ry[i]-ry[i-1])/ry[i-1] for i in range(2,N_YEARS) if ry[i-1]>0];vl=np.std(ret) if ret else 0
    return{'S':sy,'T':ty,'R':ry,'Rd':fdy,'D':dy,'HV':hy,'XP':xy,'SWF':swfy,'YEMR':yemy,'APY':apyy,
           'spiral':spiral,'maxD':mx,'H':h,'Vol':vl,'xfgb':xfb,'xfgc':xfc,'netX':xfc-xfb}

SEED=20260530
def fmt(v,s=''):return f"{v/1e6:7.2f}M{s}" if abs(v)>=1e6 else(f"{v/1e3:7.1f}K{s}" if abs(v)>=1e3 else f"{v:8,.0f}{s}")

MODELS=OrderedDict([('M2+YEM',m2),('M4+YEM',m4)])
print(f"\n{'='*135}")
print(f"  HEAT v13 — M2 vs M4 + YEM v3 Full System  |  {N_SIMS} sims x 10yr  |  HEAT${HEAT_PEG:.2f}")
print(f"  Fee split: {int(CD_SHARE*100)}/{int(TREAS_SHARE*100)}/{int(REBAL_SHARE*100)} | YEM: lag({YEM_LAG}ep) save({YEM_SWF_SAVE*100:.0f}%) scalp({YEM_BURN_SCALP*100:.0f}%)")
print(f"{'='*135}")

results=OrderedDict()
for name,fn in MODELS.items():
    sys.stdout.write(f"  {name:<20} ");sys.stdout.flush();t0=time.time()
    batch=[fn(np.random.RandomState(SEED+hash(name+str(s))%(2**30))) for s in range(N_SIMS)]
    dt=time.time()-t0
    med={k:[np.median([b[k][y] for b in batch]) for y in range(N_YEARS)] for k in['S','T','R','Rd','D','HV','XP','SWF','YEMR','APY']}
    for k in['H','Vol','maxD']:med[k]=np.median([b[k] for b in batch])
    med['spiral']=np.mean([b['spiral'] for b in batch])*100
    if'M4'in name:
        med['xfgb']=np.median([b['xfgb'] for b in batch]);med['xfgc']=np.median([b['xfgc'] for b in batch])
        med['netX']=np.median([b['netX'] for b in batch])
    results[name]=med
    print(f"{dt:3.0f}s  H={med['H']:.0f}%  spr={med['spiral']:.1f}%  maxD={med['maxD']*100:.0f}%  SWF5={fmt(med['SWF'][5])}  YEMR5={fmt(med['YEMR'][5])}  APY5={med['APY'][5]:.1f}%")

for label,years in[('Y3',[3]),('Y5',[5]),('Y10',[10])]:
    print(f"\n{'─'*125} {label}")
    print(f"  {'MODEL':<20} {'HEAT_p$':>8} {'redm$':>7} {'XFG$':>6} {'Supply':>10} {'Treas':>10} {'SWF':>10} {'YEMR':>10} {'APY':>6} {'Pool':>6}")
    for name,r in results.items():
        for y in years:
            if y>=N_YEARS:continue
            hp=r['R'][y]*r['XP'][y];rp=r['Rd'][y]*r['XP'][y]
            print(f"  {name:<20} ${hp:>7.2f} ${rp:>6.2f} ${r['XP'][y]:>5.2f} {fmt(r['S'][y]):>10} {fmt(r['T'][y]):>10} "
                  f"{fmt(r['SWF'][y]):>10} {fmt(r['YEMR'][y]):>10} {r['APY'][y]:>5.1f}% {1/r['R'][y] if r['R'][y]>0 else 0:>5.1f}:1")

r2=results['M2+YEM'];r4=results['M4+YEM']
print(f"\n{'═'*125} M4 ELASTIC SUPPLY (Y10)")
print(f"  XFG_burned(createHEAT)={fmt(r4['xfgb'])}  XFG_created(destroyHEAT)={fmt(r4['xfgc'])}  NET={fmt(r4['netX'])}")
print(f"  {'DEFLATIONARY' if r4['netX']<0 else 'INFLATIONARY'}: XFG supply {'shrinks' if r4['netX']<0 else 'grows'}")

print(f"\n{'═'*125} COMPARISON Y10")
for metric,m2v,m4v,w in[
    ('HEAT pool $','${:.2f}'.format(r2['HV'][10]),'${:.2f}'.format(r4['HV'][10]),'M4'),
    ('Treasury',fmt(r2['T'][10]),fmt(r4['T'][10]),'─'),
    ('SWF',fmt(r2['SWF'][10]),fmt(r4['SWF'][10]),'─'),
    ('YEM Reserve',fmt(r2['YEMR'][10]),fmt(r4['YEMR'][10]),'─'),
    ('CD APY','{:.1f}%'.format(r2['APY'][10]),'{:.1f}%'.format(r4['APY'][10]),'─'),
    ('Death Spiral','{:.1f}%'.format(r2['spiral']),'{:.1f}%'.format(r4['spiral']),'M4' if r4['spiral']<r2['spiral'] else 'M2'),
    ('Peg Healthy','{:.1f}%'.format(r2['H']),'{:.1f}%'.format(r4['H']),'M4' if r4['H']>r2['H'] else 'M2'),
]:
    print(f"  {metric:<20} {m2v:>18} {m4v:>18} {w:>8}")

print(f"\n  PI→CD LINK: M2 uses PI red_rate for CD spend multiplier. M4 has fixed sr=1.0.")
print(f"  YEM v3 decouples CD yield from PI — smoothed rolling average + tier caps + SWF drip.")
sys.stdout.flush()
