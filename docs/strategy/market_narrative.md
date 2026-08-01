# Product Marketing Context

*Last updated: 2026-06-04*

## Product Overview
**One-liner:** Fuego is a sovereign private bank that pays you to disappear.

**What it does:** Fuego combines CryptoNote privacy (ring signatures, stealth addresses) with an on-chain yield-bearing financial system. Users burn the base asset (XFG) to mint HEAT — an algorithmic flatcoin that preserves purchasing power against CPI inflation. HEAT can be locked in Certificates of Deposit earning real yield from protocol fees, not inflation. Cross-chain atomic swaps let users enter and exit without centralized exchanges.

**Product category:** Private DeFi / Sovereign Money Protocol (NOT "privacy coin" — that shelf is too small and misses the entire financial system built on top)

**Product type:** Open-source blockchain protocol + native financial applications (AMM, CDs, atomic swaps, stablecoin/flatcoin)

**Business model:** Extractively-aligned fee economy. 0.1% CD creation fee funds development. 0.3% swap fees feed LP providers and CD holders. No VC tokens, no premine extraction. Protocol revenue flows to users and builders, not investors.

## Target Audience

**Target companies:** Self-sovereign individuals. This is B2C, not B2B.

**Decision-makers:** The user IS the decision-maker. No corporate procurement. The persona is singular.

**Primary use case:** Earning real yield on private savings without surveillance, without centralized exchange custody risk, and without inflation eroding value.

**Jobs to be done:**
- "Protect my capital from inflation, in private, without trusting a bank or exchange"
- "Earn yield on idle crypto without exposing my entire portfolio to public blockchain analytics"
- "Exit volatile positions into a stable asset that can't be frozen or de-platformed"

**Use cases:**
- Privacy-preserving yield savings (CDs at 5-15% APR, real return after CPI)
- Private medium of exchange (HEAT flatcoin, off-chain purchases)
- Cross-chain value transfer without KYC (atomic swaps to/from BTC, ETH, SOL, BCH)
- Inflation hedge for long-term holdings (CPI-pegged flatcoin)
- Network-level anonymity for all financial activity (TOR/I2P integration)

## Personas

| Persona | Cares about | Challenge | Value we promise |
|---------|-------------|-----------|------------------|
| **User (privacy absolutist)** | Financial surveillance, chain analysis exposure, counterparty risk | Can't earn yield on private assets; Monero has no yield products | "Private money that grows" |
| **User (yield farmer)** | Real returns, not inflationary rewards, exchange custody risk | CEX yield products lose 3-8% real value/year to inflation; CEX tracks and freezes accounts | "Real yield, real privacy, real preservation" |
| **User (libertarian)** | Sovereign money, anti-CBDC, anti-freezable assets | Every "stablecoin" has a freeze function; every yield product has KYC | "Money that can't be frozen. Savings that can't be debased." |
| **Champion (developer/contributor)** | Open-source, cypherpunk values, building without VC capture | Projects get diluted by token sales to insiders | "Code you can verify. Fees that fund the resistance, not investors." |
| **Technical Influencer (node operator)** | Network health, protocol revenue, decentralization | Most chains have centralized infrastructure | "Run your own node. Verify your own wealth." |

## Problems & Pain Points

**Core problem:** In the existing financial system, you must choose between privacy OR yield. Monero protects privacy but offers zero yield. DeFi offers yield but exposes your entire portfolio to public blockchain forensics. Centralized exchanges offer both but require KYC, freeze your funds, and rehypothecate your assets.

**Why alternatives fall short:**
- Monero: No yield mechanism. Money under the mattress. Wealth preserved in nominal terms but destroyed by inflation.
- Ethereum DeFi: Public ledger. Every address linked to your identity via on-ramps. Chainalysis surfs your portfolio.
- USDC/USDT: Freezable. Issuer-controlled. Surveillance-native.
- CEX yield products (Binance Earn, etc.): KYC-gated. Custodial risk. "Not your keys." Yield paid in inflationary tokens.
- Other privacy chains with DeFi (Secret, Oasis, Aleo): No flatcoin. No native yield from swap fees. Smart-contract attack surface. Newer, unproven.

**What it costs them:** 3-8% real value destroyed per year by inflation when holding unproductive assets. Exposure of net worth. Risk of account freezes. Counterparty defaults (FTX, Celsius, etc.).

**Emotional tension:** "I'm being bled slowly by inflation while surveillance companies catalog everything I own. I can't save privately AND earn yield. The system is designed to extract from me at every layer."

## Competitive Landscape

**Direct:** Monero — falls short because it's purely transactional. No yield, no stable asset, no financial system. It's cash. Fuego is a bank.

**Secondary:** Secret Network / Oasis / Aleo — fall short because they're smart-contract platforms. The attack surface is orders of magnitude larger. No native flatcoin. Yield depends on external protocols, not protocol revenue.

**Indirect:** USDC/USDT (TradFi stablecoins) — fall short because they're surveillance coins. Every transaction is readable by Chainalysis. Every balance has a freeze function. The issuer can blacklist your address. You don't own the money; Circle/Tether does.

**Indirect:** Proof of Stake yield protocols (Aave, Compound, Lido) — fall short because staking yield flows to token holders proportional to their bags. The entry cost is capital, not work. Validators are the new banking cartel. PoW mining is open to anyone with electricity and a CPU.

**Indirect:** CEX yield products (Binance, Coinbase Earn) — fall short because you don't control the keys. History (FTX, Celsius, BlockFi) shows what happens next.

## Differentiation

**Key differentiators:**
- **Proof of Work.** Fuego is PoW. Entry requires energy and hardware, not a bag of tokens. Anyone with a CPU can mine. PoS is a shareholder program — the rich validate the rich. PoW is a job — anyone who does the work gets paid.
- **Only privacy-preserving yield in crypto.** No other privacy chain has CDs, AMM, or a flatcoin.
- **One-way burn to mint.** HEAT cannot death-spiral into XFG inflation. Burning HEAT unlocks XFG from collateral reserves, not from new emission. No Terra/Luna feedback loop possible.
- **Real yield, not inflationary rewards.** CD yields come from 80% of protocol swap fees. No token printing. No dilution of existing holders.
- **CPI-pegged flatcoin.** HEAT preserves purchasing power. It's a TIPS-equivalent with native privacy. No other flatcoin has this.
- **Anti-delisting atomic swaps.** No exchange can de-platform Fuego. Users swap directly peer-to-peer across chains.
- **CLI-first philosophical filter.** This is not a bug. It's a signal. People who won't open a terminal won't understand financial sovereignty. That's fine.

**How we do it differently:** Instead of building DeFi on a smart-contract layer (Ethereum), we built the financial primitives directly into the L1 protocol. CDs, AMM, flatcoin minting, atomic swaps — all native. No bridge risk. No smart-contract exploit surface. No external dependency.

**Why customers choose us:** "I can earn real yield, on private savings, in an asset that preserves purchasing power, with no KYC, no custodian, and no freeze risk."

## Objections

| Objection | Response |
|-----------|----------|
| "It's CLI only — too hard to use" | The terminal is a filter, not a bug. People who demand browser-extension convenience are welcome to accept the surveillance that comes with it. For everyone else: there's a GUI wallet, and the CLI is well-documented. Financial sovereignty is worth 10 minutes of learning. |
| "Small market cap / low liquidity" | Early. Every sovereign financial system started small. The first people into Bitcoin were told it was worthless. The yield flywheel — swap fees attract LPs, LPs attract swappers, swappers generate fees for CD holders — compounds with adoption. |
| "Another shitcoin" | No ICO. No premine. No VC allocation. No "team tokens." Open-source from day one. Protocol fees fund development, not investors. The code is auditable. The economics are verifiable. |
| "Privacy coins get delisted" | That's the point. Atomic swaps make exchange delistings irrelevant. Fuego cannot be de-platformed. |

**Anti-persona:** Someone who prioritizes convenience over sovereignty. Someone who wants a browser extension that auto-connects to Uniswap and doesn't care who tracks them. Someone who thinks "I have nothing to hide" is a valid argument. These people are not ready for Fuego and should not be marketed to.

## Switching Dynamics

**Push:** Your current yield product pays you in tokens that inflate away. Your stablecoin has a freeze function. Your exchange knows your entire net worth and can lock your account. The surveillance state is cataloging your financial activity. Inflation is destroying your savings in real terms.

**Pull:** Fuego pays real yield from protocol revenue. HEAT preserves purchasing power. Nobody can freeze your funds. Nobody can see your balance. Nobody can trace your transactions. The protocol is economically aligned — every fee stream benefits users, not VCs.

**Habit:** "I already know how to use MetaMask / Binance / Coinbase. Learning a new system is effort. The terminal is intimidating." — These are the habits of convenience, not sovereignty. Breaking them requires making the cost of staying VISIBLE.

**Anxiety:** "What if nobody uses it and I'm stuck with illiquid assets? What if the protocol breaks? What if HEAT loses its peg?" — These are addressed by: (1) the one-way burn mechanism preventing death spirals, (2) open-source verifiable code, (3) PI controller with proven stability math, (4) multi-source CPI oracle, (5) the Treasury backstop.

## Customer Language

**How they describe the problem:**
- "Every exchange I use knows exactly what I hold. I feel naked."
- "I'm earning 8% on USDC but losing 4% to inflation and giving up my privacy. Is that really a win?"
- "Monero protects my privacy but it just sits there. No yield. No growth. Just slow death by inflation."
- "If Binance freezes my account tomorrow, I have nobody to call."

**How they describe us:**
- "It's like Monero had a baby with a Swiss bank account."
- "The only way to save money privately and actually grow it."
- "They built an entire financial system that doesn't ask for your identity."

**Words to use:** Sovereign, private, yield, real return, purchasing power, protocol revenue (never "token emissions"), burn, mint, lock, epoch, Hearth, eternal flame, resistance

**Words to avoid:** Staking (it's not staking — it's CDs), rewards (it's yield from revenue), governance token, decentralized (overused, say "sovereign" or "peer-to-peer"), community (say "mob" or "council"), security (say "privacy" or "sovereignty" — security implies a lock on a door; privacy implies the door doesn't exist)

**Glossary:**
| Term | Meaning |
|------|---------|
| XFG | Base-layer privacy coin. Max supply ~8M. Destroyed to mint HEAT. |
| HEAT | Algorithmic flatcoin. CPI-pegged. Purchasing power preservation. Elastic supply. |
| CD | Certificate of Deposit. Time-locked HEAT earning yield from protocol fees. |
| Hearth | On-chain XFG/HEAT automated market maker. LP fees fund CD yield. |
| Eternal Flame | Accounting system for destroyed base-layer coins. Authorizes future emission. |
| Epoch | ~5-day protocol accounting period. CD terms and fees settle per-epoch. |
| Fire Alias | 8-character on-chain identity. Private, verifiable. @fuegoxfg. |
| Atomic Swap | Cross-chain peer-to-peer exchange. No intermediary. No CEX. |
| Mining | Proof of Work. CPU-mineable. Earn XFG by securing the network. No minimum stake. No validator approval. No slashing. |

## Brand Voice

**Tone:** Hard. Confident. Unapologetic. The tone of someone who has already won and is telling you how. No marketing-speak. No "we're excited to announce." No exclamation points. No emoji overuse.

**Style:** Declarative. Direct. Technical enough to be credible, accessible enough to convert. Write like a manifesto, not a press release. Short sentences. German sentence structures (end on the verb/impact). No passive voice. No hedging ("may," "could," "potentially").

**Personality:** 
- **Sovereign** — answers to no one. Does not "partner" with centralized entities. Does not seek permission.
- **Builder** — ships working code. The opposite of whitepaper vaporware.
- **Patient** — not chasing this cycle's narrative. Building multi-decade infrastructure.
- **Cult-ish** — deliberately creates in-group language. Makes you earn belonging. You get it or you don't.
- **Fatalistic** — "The long night is coming." Prepares for civilizational collapse, not quarterly earnings.

## Visual Identity

**Typography:**
| Role | Font | Source | License |
|------|------|--------|---------|
| **UI/Body/Display** | Saira (variable weight 100–900) | [Google Fonts](https://fonts.google.com/specimen/Saira) / fontsource CDN | OFL-1.1 |
| **Code/Monospace** | IBM Plex Mono (400, 500, 600) | [Google Fonts](https://fonts.google.com/specimen/IBM+Plex+Mono) / fontsource CDN | OFL-1.1 |

**Why Saira:** Squarish, geometric, humanist proportions. Reads like a terminal typeface that learned how to be beautiful. Variable weight axis gives full typographic range from thin UI labels to bold display headlines. Clean at 11px. Striking at 32px. No frills. No serif. No nostalgia.

**Why IBM Plex Mono:** IBM's corporate typeface — engineered for legibility on screen. Neutral, functional, distinctive. Pairs with Saira without competing. Handles hex strings, balances, and code blocks with equal clarity.

**Color palette:** See `dashboard/static/css/style.css` — `--bg-primary: #0a0a0f`, `--accent: #ff6b35` (forge orange), `--green: #00d4aa` (positive/heat), `--red: #ff4757` (negative/sell).

## Proof Points

**Metrics:**
- CD yields paid from 80% of real protocol swap fees (not token printing)
- Flatcoin CPI tracking via 4 independent oracle sources with median selection
- One-way burn-to-mint prevents Terra/Luna-style death spirals
- No ICO. No premine. No VC tokens. Code from day one.
- Proof of Work. CPU-mineable. No validator minimum. No staking cartel.

**Customers:** Early-stage privacy coin community. Contributors, node operators, CLI-native cypherpunks. No enterprise logos to cite — that's the point.

**Testimonials:** None to cite yet. This is pre-testimonial stage. Build the product. The stories will come from people who earned real yield without selling their soul.

**Value themes:**
| Theme | Proof |
|-------|-------|
| Real yield, not token printing | 80% swap fees → CD pool. Verifiable on-chain. |
| No death spiral possible | One-way burn. HEAT→XFG redemption draws from collateral reserve, not new minting. |
| Anti-delisting by design | Atomic swaps. No exchange required. |
| Private by default | CryptoNote ring signatures. Stealth addresses. TOR/I2P at network layer. |
| Proof of Work, not Proof of Stake | Anyone with a CPU can mine. No validator minimum. No staking cartel. Work earns — capital doesn't decide. |
| Sovereign, not "decentralized" | Protocol fees fund development. No VC capture. No central issuance. |

## Goals

**Business goal:** Become the dominant settlement layer for private, yield-bearing savings in crypto. Not the biggest by market cap — the most economically aligned. The protocol people trust because its incentives are visible and its code is auditable.

**Conversion action:** Download the CLI. Burn XFG. Mint HEAT. Lock a CD. Earn real yield in private. Tell one person who will understand.

**Current metrics:** Pre-growth. The window for loudness is now — before the market gets crowded. Position first, then scale.
