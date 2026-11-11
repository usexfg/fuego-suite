#!/usr/bin/env python3
import numpy as np, sys
EPY=73;N_SIMS=25;N_EPOCHS=EPY*11;N_YEARS=11;HEAT_PEG=158;ATOMIC_FEE=0.02;HEARTH_FEE_BPS=8

def xfp_n(t,r):return max(.5,(2+np.exp(.12*t))*float(1+r.normal(0,.25)))
def xfp_tr(t,r):return max(0.50,1.40+0.10*np.sin(t/5)+float(r.normal(0,.03)))
def xfp_rc(t,r):return 1.20 if t<3 else(2+np.exp(.12*(t-3))*float(1+r.normal(0,.25)))
def fm(v):return f"{v/1e6:7.2f}M"if abs(v)>=1e6 else(f"{v/1e3:7.1f}K"if abs(v)>=1e3 else f"{v:8,.0f}")

def run(rng,price_fn):
    p0=price_fn(0,rng);t0=HEAT_PEG/100/p0;px=5000;ph=px/t0 if t0>0 else 25000
    hs=tre=swf=yemr=xfb=xfc=0;cdl=50000;ca=0;lo=0;sc=0;ema=0;cd_apy=0;ava=0
    rec=np.zeros((N_YEARS,6))
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        t=ep/EPY;xp=price_fn(t,rng)
        os=rng.random()<0.05;op=xp*float(1+rng.normal(0,.05))
        if not os:lo=op;sc=0
        else:sc+=1
        price=lo if lo>0 else xp
        ema=0.30*price+0.70*ema if ema>0 else price
        red=max(1e-6,HEAT_PEG/100/max(ema,.01))
        
        can_mint = price > 1.58
        below_peg_time = not can_mint
        
        ve=2e4*np.clip(xp/2,.3,5)*float(np.exp(rng.normal(0,.1)))
        af=ve*.15*ATOMIC_FEE;ca+=af;ava=0.7*ava+0.3*ve*.15 if ava>0 else ve*.15
        
        sv=ve*.85
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<.5:c=min(sv/3,px*.005);r_=c/(px+c);ph-=ph*r_;px+=c
                else:c=min(sv/3,ph*.005);r_=c/(ph+c);px-=px*r_;ph+=c
        
        if sc<EPY and lo>0:
            arb_a = 0
            for _ in range(40):
                s2=px/max(ph,1e-3);hp2=s2*ema;gap=abs(hp2-1.58)/1.58
                if gap<0.0005:break
                arb_a=min(px,ph)*.03
                if hp2>1.58 and can_mint:
                    out=px*arb_a*(1-HEARTH_FEE_BPS/10000)/(ph+arb_a)
                    if out>0 and out<px:ph+=arb_a;px-=out;hs+=arb_a;xfb+=arb_a*red
                elif hp2<1.58:
                    out=ph*arb_a*(1-HEARTH_FEE_BPS/10000)/(px+arb_a)
                    if out>0 and out<ph:px+=arb_a;ph-=out;hs-=out;xfc+=out*red
        
        sp=px/max(ph,1e-3);php=sp*price;dev=abs(php-1.58)/1.58
        
        if can_mint:
            pr=0 if dev<0.01 else(0.02 if dev<0.03 else 0.05)
            mp=max(0,.008-.4*dev);m=ve*.15*.08/max(sp,1e-3)*mp
            yemr+=m*.08;rem=m*.92;hm=rem*(1-pr);tre+=rem*pr*sp;hs+=hm
            mt,mn,ms=(15,60,25) if dev<0.01 else((40,20,40) if dev<0.03 else(60,10,30))
            tre+=hm*mt/100*red;swf+=hm*ms/100*red
        
        if dev>0.005 and tre>0 and ph>0:
            ab=min(tre*.1,ph*.02)
            out=px*ab*(1-HEARTH_FEE_BPS/10000)/(ph+ab)
            if out>0 and out<px:ph+=ab;px-=out;tre-=ab
        
        if ep>0 and ep%EPY==0:
            if ca>0 and ph>0:
                spd=ca;hb=ph*spd/(px+spd)
                if 0<hb<ph*.95:px+=spd;ph-=hb;hs+=hb
            cd_apy=ca/cdl*100 if cdl>0 else 5
            cdl+=cdl*.05 if cd_apy>30 else(cdl*.02 if cd_apy>15 else(cdl*.005 if cd_apy>5 else(-cdl*.005 if cd_apy<2 else 0)))
            cdl=max(1e3,cdl)
            drip=swf*.05/EPY;swf-=min(swf,drip);ca+=drip*.3
            ca=0
        
        rec[yr,0]=php;rec[yr,1]=dev*100;rec[yr,2]=swf;rec[yr,3]=yemr;rec[yr,4]=cd_apy;rec[yr,5]=hs
    
    return rec

scenarios=[('NORMAL ($2→$25)',xfp_n),('TRAPPED ($1.30-1.50)',xfp_tr),('RECOVERY (low 3yr→normal)',xfp_rc)]

print(f"\n{'='*100}  XFG BELOW PEG STRESS TEST")
for name,fn in scenarios:
    bt=[run(np.random.RandomState(20260608+hash(name+str(s))%(2**30)),fn) for s in range(N_SIMS)]
    m=np.median(bt,axis=0)
    print(f"\n  [{name}]")
    print(f"  {'Year':>5s} {'HEAT$':>7s} {'Peg%':>6s} {'SWF':>8s} {'YEMR':>8s} {'CD%':>6s} {'Supply':>10s}")
    for y in[1,3,5,7,10]:
        print(f"  Y{y:>2d}  ${m[y,0]:>5.2f} {m[y,1]:>5.2f}% {fm(m[y,2]):>8} {fm(m[y,3]):>8} {m[y,4]:>5.1f}% {fm(m[y,5]):>10}")
sys.stdout.flush()
