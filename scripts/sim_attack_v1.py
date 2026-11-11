#!/usr/bin/env python3
"""Attack Simulation v1 — Stress test the finalized design"""
import numpy as np, sys
EPY=73;N_SIMS=25;N_EPOCHS=EPY*11;N_YEARS=11;HEAT_PEG=158;ATOMIC_FEE=0.02;HEARTH_FEE_BPS=8

def xfp(t,r):return max(.5,(2+np.exp(.12*t))*float(1+r.normal(0,.25)))
def fm(v):return f"{v/1e6:7.2f}M"if abs(v)>=1e6 else(f"{v/1e3:7.1f}K"if abs(v)>=1e3 else f"{v:8,.0f}")

def split_mint(dev):
    if dev<.01: return 15,60,25
    if dev<.03: return 40,20,40
    return 60,10,30

def run_base(rng,oracle_poison=0,pool_drain=0,thin=False):
    px0=500 if thin else 5000;ph0=2500 if thin else 25000
    p0=xfp(0,rng);t0=HEAT_PEG/100/p0;px=px0;ph=px/t0 if t0>0 else ph0
    hs=tre=swf=yemr=xfb=xfc=0;cdl=px0*10;ca=0;lo=0;sc=0;ema=0;cd_apy=0;ava=0
    poison_active=False;poison_epoch=0
    rec=np.zeros((N_YEARS,7))
    
    for ep in range(N_EPOCHS):
        yr=ep//EPY
        if yr>=N_YEARS:break
        t=ep/EPY;xp=xfp(t,rng)
        
        # Oracle with optional poisoning
        os=rng.random()<0.05;op=xp*float(1+rng.normal(0,.05))
        if oracle_poison>0 and ep>EPY*2 and ep<EPY*3:
            op=oracle_poison  # attacker sets fake price
            poison_active=True
        if not os:lo=op;sc=0
        else:sc+=1
        price=lo if lo>0 else xp
        ema=0.30*price+0.70*ema if ema>0 else price
        red=max(1e-6,HEAT_PEG/100/max(ema,.01))
        
        ve=2e4*np.clip(xp/2,.3,5)*float(np.exp(rng.normal(0,.1)))
        af=ve*.15*ATOMIC_FEE;ca+=af;ava=0.7*ava+0.3*ve*.15 if ava>0 else ve*.15
        
        sv=ve*.85
        if px>0 and ph>0:
            for _ in range(3):
                if rng.random()<.5:c=min(sv/3,px*.005);r_=c/(px+c);ph-=ph*r_;px+=c
                else:c=min(sv/3,ph*.005);r_=c/(ph+c);px-=px*r_;ph+=c
        
        # Pool drain attack
        if pool_drain>0 and ep>EPY*2 and ep<EPY*3:
            drain=ph*pool_drain/100;out=px*drain*(1-HEARTH_FEE_BPS/10000)/(ph+drain)
            if out>0 and out<px:ph+=drain;px-=out
        
        # Two-way arb
        if sc<EPY and lo>0:
            for _ in range(40):
                s2=px/max(ph,1e-3);hp2=s2*ema;gap=abs(hp2-1.58)/1.58
                if gap<.0005:break
                a=min(px,ph)*.03
                if hp2>1.58:
                    out=px*a*(1-HEARTH_FEE_BPS/10000)/(ph+a)
                    if out>0 and out<px:ph+=a;px-=out;hs+=a;xfb+=a*red
                else:
                    out=ph*a*(1-HEARTH_FEE_BPS/10000)/(px+a)
                    if out>0 and out<ph:px+=a;ph-=out;hs-=out;xfc+=out*red
        
        sp=px/max(ph,1e-3);php=sp*price;dev=abs(php-1.58)/1.58
        
        pr=0 if dev<.01 else(.02 if dev<.03 else .05)
        mp=max(0,.008-.4*dev);m=ve*.15*.08/max(sp,1e-3)*mp
        yemr+=m*.08;rem=m*.92;hm=rem*(1-pr);tre+=rem*pr*sp;hs+=hm
        mt,mn,ms=split_mint(dev);tre+=hm*mt/100*red;swf+=hm*ms/100*red
        
        if dev>.005 and tre>0 and ph>0:
            ab=min(tre*.1,ph*.02)
            out=px*ab*(1-HEARTH_FEE_BPS/10000)/(ph+ab)
            if out>0 and out<px:ph+=ab;px-=out;tre-=ab
        
        if ep>0 and ep%EPY==0:
            if ca>0 and ph>0:
                hb=ph*ca/(px+ca)
                if 0<hb<ph*.95:px+=ca;ph-=hb;hs+=hb
            cd_apy=ca/cdl*100 if cdl>0 else 5
            cdl+=cdl*.05 if cd_apy>30 else(cdl*.02 if cd_apy>15 else(cdl*.005 if cd_apy>5 else(-cdl*.005 if cd_apy<2 else 0)))
            cdl=max(1e3,cdl)
            drip=swf*.05/EPY;swf-=min(swf,drip);ca+=drip*.3
            ca=0
        
        rec[yr,0]=php;rec[yr,1]=dev*100;rec[yr,2]=swf;rec[yr,3]=tre
        rec[yr,4]=yemr;rec[yr,5]=cd_apy;rec[yr,6]=hs
    
    # Did peg survive the attack?
    under_attack=rec[2:4,:]  # years 2-4
    max_dev_attack=np.max(under_attack[:,1]) if under_attack.size>0 else 0
    recovery_dev=rec[5,1] if N_YEARS>5 else 0  # year 5
    survived=max_dev_attack<20 and recovery_dev<15
    
    return{'rec':rec,'survived':survived,'max_dev':max_dev_attack,'recovery':recovery_dev}

attacks=[
    ('BASELINE',0,0,False),
    ('ORACLE $0.50',0.50,0,False),
    ('ORACLE $10.00',10.0,0,False),
    ('DRAIN 5%',0,5,False),
    ('DRAIN 20%',0,20,False),
    ('ORACLE+5% DRAIN',0.50,5,False),
    ('ORACLE+20% DRAIN',10.0,20,False),
    ('THIN POOL',0,0,True),
    ('THIN+ORACLE 10',10.0,0,True),
    ('THIN+DRAIN 5%',0,5,True),
]

print(f"\n{'='*105}  ATTACK SIMULATION — Finalized Design")
print(f"  {N_SIMS} sims × 10yr | Attacks at years 2-3 | HEAT${HEAT_PEG/100:.2f} | Fee {HEARTH_FEE_BPS}bps")
print(f"{'='*105}")
print(f"  {'ATTACK':<22} {'Survive%':>8} {'MaxDev%':>8} {'Y5Dev%':>8} {'SWF_Y3':>9} {'TRE_Y3':>9} {'HEAT_Y3':>8}")
print("  " + "─"*80)

for name,poison,drain,thin in attacks:
    bt=[run_base(np.random.RandomState(20260609+hash(name+str(s))%(2**30)),poison,drain,thin) for s in range(N_SIMS)]
    sv=sum(1 for b in bt if b['survived'])/N_SIMS*100
    md=np.median([b['max_dev'] for b in bt])
    rd=np.median([b['recovery'] for b in bt])
    sw=np.median([b['rec'][3,2] for b in bt])
    tr=np.median([b['rec'][3,3] for b in bt])
    hp=np.median([b['rec'][3,0] for b in bt])
    print(f"  {name:<22} {sv:>7.0f}% {md:>7.1f}% {rd:>7.1f}% {fm(sw):>9} {fm(tr):>9} ${hp:>6.2f}")

print(f"\n  >20% deviation during attack = system broken. <15% recovery by Y5 = system recovered.")
sys.stdout.flush()
