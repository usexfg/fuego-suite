# DIGM CD — Lock + Platform Fee Yield (Final Design)

## Model: Principal Intact, Yield from Platform Fees

```
Lock:    1000 DIGM → held in reserve (not burned)
Mature:  1000 DIGM returned + Y DIGM yield
Yield:   From platform fees (album sales, subscriptions, ads, artist services)
```

**No burning. No inflation. Principal returned. Yield from real economic activity.**

---

## How HEAT CDs Work (reference)

Every block, XFG swap fees accumulate in `m_cdYieldPool`. At epoch boundary:
1. XFG converted to HEAT at AMM pool rate
2. Added to `m_heatCdFeePool`
3. HEAT CD holders claim proportional share

---

## DIGM CD Circular Economy

### Revenue Sources (all DIGM-denominated)
| Source | Fee Rate | Example |
|--------|----------|---------|
| Album/track sales | 10-15% | 100 DIGM album →15 DIGM platform fee |
| Streaming subscriptions | Monthly DIGM | 5 DIGM/month sub → platform keeps portion |
| Artist services | Variable | Analytics, promotion, distribution tools |
| Advertising | Revenue share | Ad impressions → DIGM revenue to platform |
| Secondary NFT sales | 2-5% royalty | Music NFT resale → platform + artist cut |

### The Flywheel
```
┌──────────────────────────────────────────────────────────┐
│               DIGM CIRCULAR ECONOMY                      │
│                                                          │
│  Users buy DIGM ──────────▶ Platform activity            │
│       │                          │                       │
│       │                          ▼                       │
│       │                    Platform fees                  │
│       │                    (DIGM collected)               │
│       │                          │                       │
│       │              ┌───────────┴───────────┐           │
│       │              ▼                       ▼           │
│       │         DIGM CDs              Platform treasury  │
│       │         (lock DIGM)           (operational)      │
│       │              │                                    │
│       │              ▼                                    │
│       │     Circulating supply ↓                          │
│       │              │                                    │
│       │              ▼                                    │
│       │     Peg strengthens (scarcer)                    │
│       │              │                                    │
│       │              ▼                                    │
│       └──── More demand to hold/use DIGM                 │
│                                                          │
│  CD holders earn: proportional share of platform fees    │
│  Yield source: real revenue, not inflation               │
└──────────────────────────────────────────────────────────┘
```

### Why This Works

1. **Real yield**: Platform generates actual revenue from real users buying music
2. **No inflation**: Yield paid from collected fees, not new minting
3. **Principal safe**: Users get DIGM back at maturity
4. **Scarcity flywheel**: Locked DIGM reduces circulating supply → stronger peg → more demand
5. **Aligned incentives**: More platform activity → more fees → more yield → more CDs locked → less circulating supply → stronger peg → more platform activity

---

## Implementation Design

### On-Chain Components

**New state variables in Blockchain:**
```cpp
uint64_t m_digmCdFeePool = 0;      // DIGM fees accumulated for CD yield
uint64_t m_digmCdTotalLocked = 0;  // Total DIGM locked in CDs
```

**New struct:**
```cpp
struct DigmCdInfo {
    uint64_t totalLocked;       // Total DIGM locked in all CDs
    uint64_t feePool;           // Available yield (DIGM)
    uint64_t apy;               // Estimated APY (basis points)
    uint64_t activeCds;         // Number of active CDs
};
```

**New RPC endpoints:**
| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/digm_cd_info` | GET | Pool stats, APY, locked amount |
| `/digm_cd_create` | POST | Lock DIGM in CD |
| `/digm_cd_claim` | POST | Claim yield from matured CD |
| `/digm_cd_list` | GET | List user's active CDs |

### CD Creation Flow
```
1. User calls /digm_cd_create with amount + term
2. Protocol validates:
   - Amount >= DIGM_CD_MIN_LOCK (10 DIGM)
   - Term >= DIGM_CD_MIN_TERM (1 day)
   - User has sufficient DIGM balance
3. DIGM transferred from user to CD reserve (on-chain lock)
4. CD record created:
   - owner: user's public key
   - amount: locked DIGM
   - created_at: block height
   - maturity: block height + term
   - yield_accrued: 0
5. m_digmCdTotalLocked += amount
6. m_digmSupply effectively reduced (locked, not burned)
```

### Fee Collection Flow (per block)
```
1. Platform transactions include DIGM fee outputs
2. Fee outputs tagged with DIGM_CD_FEE_TERM
3. On block commit:
   - Sum all DIGM_CD_FEE outputs → add to m_digmCdFeePool
4. On epoch boundary:
   - Calculate proportional yield per locked DIGM:
     yield_per_digm = m_digmCdFeePool / m_digmCdTotalLocked
   - Each CD's accrued yield += locked_amount * yield_per_digm
   - m_digmCdFeePool = 0 (distributed)
```

### Yield Claim Flow
```
1. User calls /digm_cd_claim with CD ID
2. Protocol validates:
   - CD exists and belongs to user
   - Current block >= CD maturity
3. Return principal:
   - m_digmCdTotalLocked -= cd.amount
   - Transfer cd.amount DIGM back to user
4. Pay yield:
   - Transfer cd.yield_accrued DIGM to user
5. Mark CD as claimed
```

### Early Withdrawal (optional)
```
- Forfeit accrued yield (keep only principal)
- Principal returned minus penalty (e.g., 5%)
- Penalty goes to fee pool (benefits remaining CD holders)
```

---

## Configuration Parameters

```cpp
// DIGM CD parameters
const uint64_t DIGM_CD_MIN_LOCK         = 100000000ULL;    // 10 DIGM minimum
const uint64_t DIGM_CD_MAX_LOCK         = 5000000000ULL;   // 5,000 DIGM maximum per CD
const uint64_t DIGM_CD_MIN_TERM         = 144;              // 1 day (144 blocks)
const uint64_t DIGM_CD_MAX_TERM         = 525600;           // 1 year (365 days)
const uint64_t DIGM_CD_EARLY_WITHDRAWAL_PENALTY_BPS = 500;  // 5% penalty
const uint64_t DIGM_CD_FEE_TERM         = 0x44494D46;       // 'DIMG' — DIGM CD fee marker
```

---

## Yield Sustainability Analysis

### Conservative Estimate (Year1)
| Revenue Source | Monthly DIGM | Annual DIGM |
|----------------|-------------|-------------|
| Album sales (100 albums × 15 DIGM avg) | 1,500 | 18,000 |
| Subscriptions (500 users × 5 DIGM) | 2,500 | 30,000 |
| Artist services | 500 | 6,000 |
| Ads (at scale) | 1,000 | 12,000 |
| **Total** | **5,500** | **66,000** |

If1,000 DIGM locked in CDs:
- Yield per DIGM = 66,000 /1,000 =66 DIGM/year
- **APY =6,600%** (unsustainable at scale — will attract more CDs)

If10,000 DIGM locked (full supply):
- Yield per DIGM = 66,000 /10,000 =6.6 DIGM/year
- **APY =66%** (still high, but real revenue)

At maturity of market:
- Revenue grows with user base
- APY normalizes to 10-30% range
- Still attractive vs. traditional savings

### Key Insight
The APY is self-correcting:
- High APY → more CDs locked → more supply locked → yield dilutes → APY drops
- Low APY → fewer CDs → less supply locked → yield concentrates → APY rises
- Equilibrium reached naturally

---

## Comparison to HEAT CDs

| Feature | HEAT CDs | DIGM CDs |
|---------|----------|----------|
| Principal | HEAT (locked) | DIGM (locked) |
| Yield source | XFG swap fees → converted to HEAT | Platform fees in DIGM |
| Yield asset | HEAT (native) | DIGM (stablecoin) |
| Supply effect | Reduces HEAT circulation | Reduces DIGM circulation |
| Peg impact | None (HEAT is base) | Strengthens DIGM peg |
| Deflationary | No | Yes (locked = less circulating) |
| Real yield | Yes | Yes |

---

## Summary

- **Lock** DIGM in CDs (not burn)
- **Yield** from platform fees (real revenue)
- **Principal** returned at maturity
- **Scarcity** from locking, not burning
- **Circular**: activity → fees → yield → more locking → stronger peg → more activity
- **Self-correcting** APY equilibrium
- **No inflation, no uncollateralized tokens**
