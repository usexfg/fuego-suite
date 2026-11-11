#!/usr/bin/env python3
# ⚠️ SUPERSEDED by sim_final_v18.py — this sim hallucinates a HEAT→XFG burn path
# that does not exist. HEAT is mint-only. The AMM pool is the sole exit.
"""
Fuego HEAT — Multi-Scenario Model Comparison
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Tests Fixed, Damped, Two-Phase across XFG appreciation regimes.
"""

import numpy as np, time, sys

BPE=900; EPY=73; LAUNCH=0.2; KP=0.08; KI=0.015; N=150; N_EPOCHS=EPY*10
np.random.seed(42)

SCENARIOS = {
    'EARLY ($5 by Yr1)':        {'growth': 8.0,  'vol': 0.20, 'oracle_start': 1},
    'MID ($5 by Yr3)':          {'growth': 5.0,  'vol': 0.20, 'oracle_start': EPY},
    'LATE ($5 by Yr8)':         {'growth': 3.5,  'vol': 0.20, 'oracle_start': EPY},
    'SLOW (never hits $5)':     {'growth': 2.5,  'vol': 0.20, 'oracle_start': EPY},
    'EARLY VOLATILE (σ=0.50)':  {'growth': 8.0,  'vol': 0.50, 'oracle_start': 1},
    'EARLY EXTREME (σ=0.80)':   {'growth': 8.0,  'vol': 0.80, 'oracle_start': 1},
}

def run_model(label, use_damped, use_two_phase, sc):
    supplies=[]; treasuries=[]; ratios=[]; act_pcts=[]; heat_vals=[]
    for s in range(N):
        rng=np.random.RandomState(s%2**31)
        px=5000.0; ph=25000.0; redemption=LAUNCH; integral=0.0
        treasury=0.0; cd_pool=0.0; heat_sup=0.0
        xfg_p=0.01; launch_twap=0.2; activated_ep=0; final_hv=0
        
        for ep in range(N_EPOCHS):
            t=ep/N_EPOCHS
            xfg_p = 0.01 * np.exp(sc['growth']*t**1.2) * (1+rng.normal(0,sc['vol']))
            oracle_active = ep >= sc['oracle_start']
            oracle_stale = rng.random() < 0.03
            oracle_p = xfg_p * (1+rng.normal(0,0.05))
            
            vol_e = 20000*(BPE/180)*np.clip(xfg_p/0.01,0.3,5)*np.exp(rng.normal(0,0.10))
            fees=vol_e*0.02; treasury+=fees*0.20; cd_pool+=fees*0.80
            
            sv=vol_e*0.05
            if px>0 and ph>0:
                for _ in range(3):
                    if rng.random()<0.5:
                        capped=min(sv/3,px*0.005); out=ph*capped/(px+capped); px+=capped; ph-=out
                    else: capped=min(sv/3,ph*0.005); out=px*capped/(ph+capped); ph+=capped; px-=out
            spot=px/max(ph,0.001)
            if ep==EPY//2 and launch_twap==0.2: launch_twap=spot
            
            activated = use_two_phase and oracle_active and not oracle_stale and oracle_p>=5.0
            if activated: activated_ep+=1
            
            if use_damped and launch_twap>0 and spot>0:
                pr = max(0.0001, launch_twap/max(0.0001, spot))
                target = LAUNCH * (pr**0.25)
            elif activated:
                hv = spot*oracle_p; tv = np.clip(hv,1.0,3.0)
                target = tv/oracle_p
            else: target = LAUNCH
            
            target=max(0.00001,min(10.0,target))
            dev=(spot-target)/target if target>0 else 0; dt=1/EPY
            integral=np.clip(integral+dev*dt,-1,1)
            red_rate=np.clip(KP*dev+KI*integral,-0.5,0.5)
            redemption=max(1e-6,target*(1+red_rate*dt))
            heat_sup+=vol_e*0.08/max(spot,0.001)*max(0,0.008-0.4*dev)
            if cd_pool>0 and ph>0:
                sr=np.clip(1+red_rate,0.2,3); spend=min(cd_pool,cd_pool*sr)
                hb=ph*spend/(px+spend)
                if 0<hb<ph*0.95: px+=spend; ph-=hb; heat_sup+=hb; cd_pool-=spend
            if ph>0 and treasury>0 and px/max(ph,0.001)>3:
                reb=min(treasury*0.1,px*0.03)
                if redemption>0: ph+=reb/redemption; treasury-=reb; heat_sup+=reb/redemption
        
        supplies.append(heat_sup); treasuries.append(treasury)
        ratios.append(px/max(ph,0.001)); act_pcts.append(activated_ep/N_EPOCHS*100)
        if use_two_phase and activated_ep>0:
            heat_vals.append(np.clip(spot*oracle_p,1.0,3.0))
        else:
            heat_vals.append(LAUNCH * xfg_p)

    ms=np.median(supplies); mt=np.median(treasuries); mr=np.median(ratios); ma=np.median(act_pcts)
    mhv=np.median(heat_vals); cov=mt/max(1e-6,LAUNCH)/max(1,ms)*100
    return ms, mt, mr, cov, ma, mhv

print(f"\n{'#'*120}")
print(f"  MULTI-SCENARIO MODEL COMPARISON ({N} sims × {N_EPOCHS} epochs)")
print(f"{'#'*120}")

for sc_name, sc in SCENARIOS.items():
    print(f"\n── {sc_name} — growth={sc['growth']:.1f}, vol=σ{sc['vol']:.2f} ──")
    print(f"  {'Model':<22} {'Supply':>10} {'Treas':>8} {'Pool':>6} {'Cov':>4} {'Act':>4} {'HEAT≈':>6}")
    
    for label, use_damped, use_two_phase in [
        ('FIXED 0.2', False, False),
        ('DAMPED', True, False),
        ('TWO-PHASE', False, True),
    ]:
        ms,mt,mr,cov,ma,mhv = run_model(label, use_damped, use_two_phase, sc)
        print(f"  {label:<22} {ms:>10,.0f} {mt:>8,.0f} {mr:>4.1f}:1 {cov:>3.0f}% {ma:>3.0f}% \${mhv:.2f}")
        sys.stdout.flush()

print(f"\n── SUMMARY ──")
print(f"  All three models behave identically until XFG reaches \$5.")
print(f"  At activation, Two-Phase constrains HEAT to \$1-\$3 band.")
print(f"  Fixed/Damped let HEAT track XFG proportionally at 5:1.")
print(f"  Damped differs from Fixed only when pool TWAP diverges from launch.")
print(f"  Early + volatile scenarios test oracle reliability under stress.")
