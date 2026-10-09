# Fuego (XFG)

**Tagline**: Private banking for sovereign individuals.

**Founded**: 2019

**Category**: CryptoNote privacy network — p2p atomic swaps, on-chain AMM, certificate of deposit yield

---

## Brand Narrative

Fuego is a privacy-first p2p banking network built on CryptoNote. Every
transaction is private by default — ring signatures, stealth addresses, no
on-chain identity. The network earns real protocol revenue from atomic swap
fees and Hearth AMM taker fees, distributing yield to CD holders. No
custodians. No surveillance. No compromise.

---

## Value Proposition

Earn real yield from protocol revenue — anonymously, in custody, without
ever revealing who you are.

---

## Voice & Tone

- Direct — say the thing, skip the preamble. One claim. One sentence.
- Precise — exact numbers only. "1% taker fee, 70% to CD yield pool." Not "low fees" or "most revenue distributed."
- Confident — assert, don't hedge. "XFG earns" not "XFG might help you earn."
- Private — no personal data in copy, no "your identity" framings. The user is always sovereign.
- Terse — no filler. No opener sentences. No "In today's world."

### Do
- Use exact numbers: 1%, 70%, 69/11/20, 8,000,008.8, ring size 8
- Lead with outcome: "Earn without exposure" not "Atomic swap fee distribution"
- Use present tense for product claims
- Name things by their canonical names: Hearth, DeXFG, HEAT, XFG, CD

### Don't
- Don't use "simple", "easy", "revolutionary", "game-changing"
- Don't hedge: "we believe", "might", "could possibly"
- Don't write filler openers
- Don't refer to users by identity — they are sovereign, unnamed
- Don't say "Monaco Terminal" in UI text — that is a style description, not a brand name

---

## Color Palette

### Brand Gold (Burnished — not yellow)
- gold: #b8922a
- gold-bright: #c9a44c
- gold-dim: rgba(201, 164, 76, 0.14)

### Firegold (Primary Action / Accent)
- firegold: #ff9100
- firegold-bright: #ffa726
- firegold-dim: rgba(255, 145, 0, 0.14)
- firegold-hover-dark: #c95f00
- firegold-bright-hover: #e07200

### HEAT Blue (White-Hot Electric — HEAT token only)
- heat-blue: #ebd9ff
- heat-blue-hot: #ffffff
- heat-blue-dim: rgba(235, 217, 255, 0.14)
- heat-blue-hover: #f2e6ff
- heat-blue-hover-dark: #c299ff
- heat-blue-hot-hover: #e6ccff
- heat-border-bright: #f8f2ff

### Semantic
- success: #00ffaa
- success-hover: #22ffbb
- error: #ff3355
- error-hover: #ff2255
- accent: #ff6b35

### Surface (Black Canvas)
- bg-primary: #000000
- bg-secondary: #07070a
- bg-tertiary: #0e0e14
- bg-card: rgba(10, 10, 16, 0.92)

### Text
- text-primary: #ffffff
- text-secondary: #a0a5b8
- text-muted: #5d6175

### Borders
- border: rgba(255, 255, 255, 0.1)
- border-bright: rgba(255, 255, 255, 0.2)
- border-gold: rgba(201, 164, 76, 0.35)
- border-firegold: rgba(255, 145, 0, 0.4)
- heat-border: rgba(0, 240, 255, 0.45)

---

## Typography

- display: "Saira"
- body: "Saira"
- mono: "IBM Plex Mono"

### Weights in Use
- 400 — body text, form inputs
- 500 — medium emphasis mono
- 600 — mono medium, semibold labels
- 700 — subheadings, card headers
- 800 — section titles, nav labels
- 900 — display, spot price, brand marks

### Mono Usage
Use `IBM Plex Mono` for: all numeric values, prices, balances, hashes,
timestamps, code blocks, terminal output, data labels. Never for body copy.

---

## Core Messages

- XFG is private by default — every transaction, unlinkable
- Earn real yield from protocol swap fees, not from inflation
- CD interest is on-chain, auditable, non-custodial
- HEAT is a flatcoin pegged to purchasing power, not price
- Atomic swaps via adaptor signatures — trustless, zero KYC

---

## Messaging Pillars

### Privacy by Design
**Claim**: Every transaction is unlinkable by default.
**Proof**:
- Ring signatures — 32, 16, or minimum of 8 as txn size and decoys per amount allow. (BlockMajorVersion 10+)
- Stealth receiving addresses using one-time subaddresses
- No on-chain identity leakage by metadata
- Addresses begin with "fire" — indistinguishable from master and subaddresses

### Real Protocol Yield
**Claim**: CD yield comes from real network revenue, not token inflation.
**Proof**:
- Source 1: Hearth taker fee — 1%, 70% to CD APY pool
- Source 2: Atomic swap fee — 2% round-trip, 69% to CD yield / 11% Bonus Vault / 20% Treasury
- No new XFG minted for yield — fixed supply of 8,000,008

### Sovereign Exchange
**Claim**: Trade cross-chain without custodians, KYC, or counterparty risk.
**Proof**:
- Adaptor signatures — cryptographically enforced atomic settlement
- DeXFG Ordergraph — live liquidity across 30+ chains
- No custody of funds at any stage

### Flatcoin Stability
**Claim**: HEAT tracks purchasing power, not a fiat price.
**Proof**:
- Peg reference: $1.58 USD (Q1 2009 base, Fuego Cost Index (FCI) adjusted)
- Minted by burning XFG at Hearth TWAP price
- Not algorithmic — backed by XFG burned in the Hearth

---

## Audience Segments

| Audience | Lead message | Hook |
|----------|-------------|------|
| Privacy-focused holders | "Your money is your business." | Ring sigs, stealth addrs, zero identity |
| Yield seekers | "CD interest from real fees — not printing." | 69-11-20 routing, on-chain, auditable |
| Traders / swappers | "Swap cross-chain. No custody, no KYC." | Adaptor sigs, 30+ chains, DeXFG |
| Builders / devs | "Privacy primitives you can build on." | MLSAG, MuSig2, adaptor sigs, RPC |

---

## Objection Responses

| Objection | Response |
|-----------|----------|
| "Privacy coins enable crime." | "Privacy is a property of money, not a crime. Cash is private. So is XFG." |
| "Where does the yield come from?" | "Protocol swap fees. 1% taker on Hearth, 2% on atomic swaps. On-chain, auditable." |
| "How is HEAT stable if it's backed by a volatile asset?" | "HEAT isn't price-stable — it tracks purchasing power. Backed by burned XFG." |
| "Isn't this too technical?" | "The wallet handles it. Burn XFG for HEAT. Open a CD. Earn fees." |

---

## Product / Feature Name Glossary

| Canonical Name | Do Not Use |
|---------------|-----------|
| XFG | Fuego coin, $XFG (no dollar sign needed) |
| HEAT | Heat, $HEAT, Embers |
| Hearth | Hearth Exchange, Hearth AMM, the fireplace |
| DeXFG | Dexfg, dex-xfg, atomic dex |
| Ordergraph | Order Graph, OrderGraph, order book |
| CD | Certificate of Deposit, deposit, vault |
| SwapXFG | swapxfg, swap xfg |
| fire_wallet | Fire Wallet, firewallet |

---

## Logo

Primary logo files: `dashboard/static/coin-icons/fuego.png`

Coin icon used in: nav brand, ordergraph markers, chain selectors.

See `skills/brand/references/logo-usage-rules.md` for usage guidelines.

---

## Social / Channel Presence

| Channel | Handle | Content type |
|---------|--------|-------------|
| X / Twitter | @fuegoxfg | Protocol updates, privacy education, CD yields |
| GitHub | usexfg | Source releases, build docs |
