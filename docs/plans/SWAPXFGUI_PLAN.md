# SwapXFG GUI — Fuego Design System · Full Customization Plan

> **Objective:** Reskin the C++/QML AtomicDEX desktop codebase into a unified Fuego wallet + SwapXFG DEX that is unrecognizable from Komodo/FiroDEX. Uses the Fuego/HEAT design system throughout. Maintains mm2 RPC compatibility for Komodo ecosystem tool interoperability.

---

## Source Code

**Fork lineage:** `komodo-wallet-desktop` → `FiroDEX-Desktop` → `swapxfgui`

**Repo:** `aejontargaryen/swapxfgui` (branch: `dev`, forked from `firoorg/FiroDEX-Desktop`)

**Backend daemon:** `xfg-community/fuego-swapd` (fork of `KomodoPlatform/komodo-defi-framework`)

**Stats:** 11,032 commits, 45.3% C++, 43.7% QML, 4.5% C, 2.2% CMake

**License:** GPL-2.0 (inherited)

---

## Architecture

```
swapxfgui/                              komodo-defi-framework (Rust)
├── src/                                ├── mm2src/
│   ├── app/              ◄─RPC──►      │   ├── mm2_main/     — daemon entry
│   ├── core/atomicdex/                 │   ├── mm2_core/     — swap orchestration
│   │   ├── mm2_client*    ──RPC──►     │   ├── mm2_rpc/      — JSON-RPC handlers
│   │   ├── fuego_rpc*     ──RPC──►     │   ├── coins/        — chain adapters
│   │   ├── hearth_rpc*    ──RPC──►     │   ├── mm2_p2p/      — libp2p networking
│   │   └── cd_rpc*        ──RPC──►     │   ├── mm2_state_machine/ — swap state machines
│   ├── tests/                          │   ├── trading_api/  — orderbook, buy/sell
│   └── CMakeLists.txt                  │   └── crypto/       — crypto primitives
├── atomic_defi_design/                 └── ...
│   ├── Dex/              (*) = new C++ bridge files
│   ├── assets/
│   └── imports/
├── cmake/
├── vendor/
├── wally/
├── CMakeLists.txt
└── vcpkg.json
```

**Key fact:** The C++ backend (`core/atomicdex/`) talks JSON-RPC to a local daemon on `127.0.0.1:7783`. We replace `mm2_firo` with `fuego-swapd` — our fork of the Komodo DeFi Framework that adds XFG coin support, adaptor signature swap protocol, and Fuego-specific RPC extensions (AMM, CDs, Burn2Mint, alias system).

---

## Daemon Strategy: Fork mm2 → `fuego-swapd`

### Why fork instead of wrapping

The Komodo DeFi Framework (`komodo-defi-framework`, Rust, v3.0.0-beta) already has:
- BIP39 HD wallet (seed → keys for 100+ coins)
- Electrum SPV (balances, UTXOs, tx history)
- libp2p P2P orderbook (shared with Komodo/Firo ecosystem)
- Pluggable swap protocol architecture (HTLC, EVM, ZHTLC, Tendermint)
- Coin activation flow (`enable`, `electrum` RPC)

We extend it rather than building our own wallet/orderbook stack:

```
fuego-swapd (our fork)
├── Unchanged from upstream:
│   ├── BIP39 HD wallet + Electrum SPV
│   ├── libp2p P2P orderbook (shared netid)
│   ├── HTLC swaps (for non-XFG pairs)
│   ├── Coin activation + config
│   └── All existing RPC endpoints (mm2 compatible)
│
├── New: XFG coin support
│   ├── Coin config (address prefixes: pub=0xB7, p2sh=0x05, wif=0xF8)
│   ├── ElectrumX server list
│   └── SPV wallet adapter
│
├── New: Adaptor Signature Protocol
│   ├── 7-state state machine (mm2_state_machine/adaptor.rs)
│   ├── Crypto module (Musig2, DLEQ, adaptor sigs) (crypto/adaptor.rs)
│   ├── P2P exchange messages (mm2_p2p/)
│   └── swap_method: "htlc" | "adaptor" field on orders
│
├── New: Fuego Custom RPC
│   ├── Hearth AMM endpoints (pool stats, swap, add/remove liquidity)
│   ├── CD endpoints (create, claim, market, buy/sell)
│   ├── Burn2Mint endpoint
│   ├── Alias system endpoints
│   └── Fee pool / treasury endpoints
│
└── Rebranded: fuego-swapd binary (replaces mm2_firo)
```

### Orderbook

| Pair | Protocol | Orderbook Source |
|------|----------|-----------------|
| XFG/ETH | Adaptor sig (XFG) + HTLC (ETH) | fuego-swapd P2P (shared with Komodo net) |
| XFG/SOL | Adaptor sig (XFG) + HTLC (SOL) | fuego-swapd P2P |
| XFG/BCH | Adaptor sig (XFG) + HTLC (BCH) | fuego-swapd P2P |
| XFG/ARB | Adaptor sig (XFG) + HTLC (ARB) | fuego-swapd P2P |
| XFG/XMR | Adaptor sig (XFG) + ZHTLC (XMR) | fuego-swapd P2P |
| XFG/BTC | Adaptor sig (XFG) + HTLC (BTC) | fuego-swapd P2P |
| XFG/HEAT | Hearth AMM (on-chain pool) | fuego-swapd `/amm_quote` |

All orders go through the same libp2p orderbook. The `swap_method` field on each order tells the counterparty which protocol to use. Orders from vanilla mm2 users (HTLC-only) are visible but require HTLC-compatible coin support on both sides. Adaptor orders are visible to all fuego-swapd users.

### Implementation Status

| Component | Status |
|-----------|--------|
| swapxfgui Qt frontend | Phase 1 design system in progress |
| swapxfgui → fuego-swapd RPC bridge | Not started |
| XFG coin config for mm2 | Not started |
| Adaptor sig protocol in Rust | Not started (ported from C++ `fuego_swapd`) |
| Fuego custom RPC endpoints | Not started |
| fuego-swapd build + release | Not started |

---

## Design System

> Sourced from the Fuego/HEAT brand design system. All values must be applied uniformly across every QML file — no Komodo default colors or fonts survive.

### Colors — Fuego/HEAT

| Token | Hex | QML Usage |
|-------|-----|-----------|
| Heat Orange (primary) | `#FF4D00` | CTAs, active states, price up, brand accents |
| Orange Dim | `#CC3D00` | Hover/pressed states |
| Ember | `#FF6B2B` | Secondary accent, gradient endpoints |
| Amber | `#FFB347` | Warm gradient endpoints |
| Deep Black (bg) | `#0a0a0a` | Primary dark background |
| Card background | `#141414` | Cards, panels |
| Card border | `#2a2a2a` | Subtle borders |
| White Hot | `#FFFDF8` | Light theme background |
| Neutrals | `#0d0d0d` → `#f5f2ed` | 950–50 scale, warm cream undertone |
| Positive | `#4CAF50` | Buy, success, up |
| Negative | `#F44336` | Sell, error, down |

### Typography

| Role | Font | QML `font.family` |
|------|------|-------------------|
| Headers / Display | **Chakra Petch Bold 700** | `"Chakra Petch"` |
| Body / UI | **Space Grotesk 400–700** | `"Space Grotesk"` |
| Monospace (addresses, hashes) | **JetBrains Mono** | `"JetBrains Mono"` |
| Counters / Badges | **Orbitron** | `"Orbitron"` |
| Dense data / Tables | **Rajdhani** | `"Rajdhani"` |

All fonts are Google Fonts. Bundle as `.ttf` in `assets/fonts/` and load via `FontLoader` in QML.

### Borders, Shadows, Radii

| Element | Value |
|---------|-------|
| Card border | `1px solid #2a2a2a` (dark), `1px solid #E5E0D8` (light) |
| Card background | `#141414` (dark), `#FFFFFF` (light) |
| Orange-accent card | `rgba(255,77,0,0.04)` bg, `#FF4D00` border |
| Card radius | 14–20px |
| Small element radius | 6px |
| Pill / tag radius | 9999px (full) |
| Coin/token icon radius | 50% (circle) |
| App icon radius | 18% of size |
| Dark shadow | `rgba(0,0,0,0.3–0.5)` |
| Orange glow (sparingly) | `0 0 12-48px rgba(255,77,0,0.12-0.25)` |
| Light shadow | `rgba(0,0,0,0.06–0.1)` |

**Card style rule:** Use outer borders only. No colored left-border accent pattern. Cards are clean rectangles with uniform borders.

### Animation & Motion

| Property | Value |
|----------|-------|
| Default easing | `cubic-bezier(0.4, 0, 0.2, 1)` |
| Bounce easing (playful) | `cubic-bezier(0.34, 1.56, 0.64, 1)` |
| Micro-interactions | 150ms |
| State changes | 250ms |
| Reveals / transitions | 400ms |
| Heat shimmer (logo) | Subtle horizontal oscillation + brightness pulse |
| Glow pulse (emphasis) | `drop-shadow` animation, dark backgrounds only |

**Animation rules:**
- No heavy transitions or page-level animations. The brand is precise, not flashy.
- Glow is used sparingly — hero moments, active states, and the logo mark only.
- Logo heat shimmer activates on app launch and on hover.

### Hover & Press States (Detailed)

```
Dark background elements:
  Hover → brightness increase (element lightens slightly)
  Press → scale(0.98)

Interactive orange elements (buttons, CTAs):
  Hover → color shifts to #FF6B2B (Ember)
  Press → color darkens to #CC3D00

No opacity-based hover (avoids washed-out feel on dark backgrounds).
```

### Transparency Rules

- **No frosted-glass or backdrop-blur** patterns anywhere.
- Orange transparency `rgba(255,77,0,0.04–0.12)` for tinted card backgrounds and glow effects only.
- Background gradients: occasional subtle radial gradient with orange glow (`rgba(255,77,0,0.06)`) for hero moments only.

### Copy & Tone (for all UI strings)

- Technical confidence: use precise terms without hedging ("MLSAG", "Q64.64 fixed-point", "constant-product AMM")
- No marketing buzzwords. "Irreversible" not "innovative". "Deterministic" not "revolutionary".
- Product names: ALL-CAPS (XFG, HEAT). Features: PascalCase (Burn2Mint, Hearth, SwapXFG).
- No emoji in UI strings or labels. Ever.
- User-facing: second person ("You burn XFG to mint HEAT"). Protocol-facing: third person ("Fuego implements...").
- Example button labels: "Burn & Mint HEAT", "Add Liquidity", "Create CD", "Confirm Swap"

### Icons

Use **Lucide Icons** (thin geometric line icons — match the clean/technical brand). Bundle as SVG in `assets/icons/`. No icon font or sprite sheet.

Key icons needed:
- `flame` — Fuego logo mark / XFG
- `sun` — HEAT
- `arrow-left-right` — Swap
- `wallet` — Wallet
- `chart-candlestick` — Portfolio
- `flame-kindling` — Hearth / Burn2Mint
- `calendar-clock` — CDs
- `settings` — Settings
- `plug` — Bridge connections
- `circle-dot` — Status indicators (connected/disconnected)

---

## Application Layout

```
┌──────────────────────────────────────────────────┐
│ ┌──────────┐                                     │
│ │  Fuego   │  [Active View Content]              │
│ │   Logo   │                                     │
│ │          │                                     │
│ │ Wallet   │                                     │
│ │ Portfolio│                                     │
│ │ Hearth   │                                     │
│ │ HEAT CDs │                                     │
│ │ Swap     │                                     │
│ │          │                                     │
│ │ Settings │                                     │
│ │          │                                     │
│ │          │                                     │
│ └──────────┘                                     │
│                     ⬤ XFG: 1.23M  ⬤ ETH ⬤ SOL   │
└──────────────────────────────────────────────────┘
```

**Sidebar** (persistent, ~200px, Deep Black `#0a0a0a`):
- Fuego flame logo at top
- Nav items: Wallet, Portfolio, Hearth, HEAT CDs, Swap, Settings
- Active item: Heat Orange `#FF4D00` left-border accent (3px)
- Inactive items: neutral `#888888`, hover to white

**Status Bar** (bottom, 28px):
- Block height from fuegod RPC
- mm2 daemon connection indicator
- Bridge status dots (ETH/SOL/BSC)
- Active network label (mainnet/testnet)

---

## Views

### 1. Wallet View

```
┌──────────────────────────────────────────────────┐
│  WALLET                                          │
│                                                  │
│  ┌──────────────┐  ┌──────────────┐              │
│  │   XFG        │  │   HEAT       │              │
│  │   1,234.567  │  │   45,678.90  │              │
│  │   ≈ $3,702   │  │   ≈ $45,679  │              │
│  └──────────────┘  └──────────────┘              │
│                                                  │
│  [Receive]  [Send]  [Copy Address]               │
│                                                  │
│  ── Burn2Mint ───────────────────────────────     │
│  Burn  [  100.00  ] XFG                          │
│  Mint  [   3,700  ] HEAT     (1 XFG = 37 HEAT)  │
│  Rate: determined by Hearth AMM spot price      │
│  [Burn & Mint HEAT]                              │
│                                                  │
│  ── Recent Activity ──────────────────────────   │
│  +100 XFG   mined           Block 1,234,567    │
│  -50 XFG    → CD #1247      2026-06-15 14:23   │
│  +3,700 HEAT Burn2Mint     2026-06-14 09:12   │
└──────────────────────────────────────────────────┘
```

**Functions:**
- XFG + HEAT dual balance cards (exchange rate shown)
- Send / Receive with address QR and alias (@name) support
- Burn2Mint "mini-bridge": user enters XFG amount, system calculates HEAT output from Hearth spot price
- P2P transfer (address or alias destination)
- Recent activity feed (mined blocks, sends, receives, CD creates, Burn2Mint events)

### 2. Portfolio View

```
┌──────────────────────────────────────────────────┐
│  PORTFOLIO                        [USD ▼] [1D ▼] │
│                                                  │
│  ┌──────────────────────────────────────────────┐│
│  │          TradingView Chart Widget            ││
│  │          XFG/USD · 1D · Candles              ││
│  │          ████████░░░░████████░░░░░░          ││
│  │          ██░░░░████░░░░██████████░░          ││
│  │          ░░████░░░░████████████████          ││
│  └──────────────────────────────────────────────┘│
│                                                  │
│  XFG Price Tickers                                │
│  ┌──────────┬──────────┬──────────┬──────────┐  │
│  │ XFG/USD  │ XFG/ETH  │ XFG/SOL  │ XFG/BTC  │  │
│  │  $3.00   │ 0.0012   │  0.023   │ 0.000045 │  │
│  │  +2.4%   │  +1.8%   │  -0.3%   │  +3.1%   │  │
│  └──────────┴──────────┴──────────┴──────────┘  │
│  ┌──────────┬──────────┬──────────┬──────────┐  │
│  │ XFG/BCH  │ XFG/ARB  │ XFG/XMR  │ XFG/EUR  │  │
│  │  0.0089  │  0.0034  │  0.018   │  €2.76   │  │
│  └──────────┴──────────┴──────────┴──────────┘  │
│                                                  │
│  Total Value: $3,702.00   |   XFG: 1,234.567    │
└──────────────────────────────────────────────────┘
```

**Functions:**
- TradingView Advanced Chart widget (webview embedding) — default pair: XFG/USD
- All swappable pairs as price tiles: XFG/ETH, XFG/SOL, XFG/BCH, XFG/ARB, XFG/XMR, XFG/BTC
- Multi-fiat selector (Settings): USD, EUR, GBP, CNY, JPY, KRW, etc.
- Timeframe selector: 1H, 1D, 1W, 1M, 1Y
- Pair selector switches the main chart
- Total portfolio value in selected fiat

### 3. Hearth View — XFG/HEAT AMM

```
┌──────────────────────────────────────────────────┐
│  HEARTH — XFG/HEAT AMM                           │
│                                                  │
│  ┌──────────────────────────────────────────────┐│
│  │          TradingView Chart Widget            ││
│  │          XFG/HEAT · 1D                       ││
│  │          ████████░░░░████░░░░████████        ││
│  └──────────────────────────────────────────────┘│
│                                                  │
│  Pool Stats                                       │
│  ┌──────────────────┬──────────────────────────┐ │
│  │ XFG in Pool      │ HEAT in Pool             │ │
│  │ 500,000 XFG      │ 18,500,000 HEAT          │ │
│  │ $1,500,000       │ $18,500,000              │ │
│  └──────────────────┴──────────────────────────┘ │
│  Current Rate: 1 XFG = 37.00 HEAT               │
│  PI Controller: ±0.02% from peg                 │
│  Total Liquidity: $20,000,000                    │
│  24h Volume: $450,000                            │
│                                                  │
│  ── Trade ──────────────────────────────────     │
│  [Buy HEAT ▼]  Amount: [  100.00  ] XFG         │
│  You receive:  ~3,700 HEAT                       │
│  Price Impact: 0.02%                             │
│  [Execute Swap]                                  │
│                                                  │
│  ── Your LP Position ────────────────────────    │
│  ┌──────────────────────────────────────────────┐│
│  │ Pooled:  10,000 XFG  +  370,000 HEAT        ││
│  │ Share:   2.00% of pool                      ││
│  │ Fees Earned: 45.23 XFG / 1,673 HEAT         ││
│  │ [Add Liquidity]  [Remove Liquidity]         ││
│  └──────────────────────────────────────────────┘│
└──────────────────────────────────────────────────┘
```

**Functions:**
- TradingView chart (XFG/HEAT pair)
- Pool stats (XFG balance, HEAT balance, current rate, PI controller deviation, TVL, 24h volume)
- Buy HEAT / Sell HEAT toggle with amount input and calculated output
- LP position section: pooled amounts, pool share %, fees earned
- Add Liquidity flow: deposit XFG + HEAT at current ratio
- Remove Liquidity flow: withdraw proportional share
- No LP? Show "Provide Liquidity" CTA instead of position

### 4. HEAT CDs View

```
┌──────────────────────────────────────────────────┐
│  HEAT CERTIFICATES OF DEPOSIT                    │
│                                                  │
│  ── Your CDs ────────────────────────────────    │
│  ┌─────────────────────────────────────────────┐ │
│  │ CD #1247  │ 5.2% APY │ 30d lock │ 150 XFG  │ │
│  │ Matures: 2026-07-15  │ Accrued: 0.64 XFG   │ │
│  │ [Claim on Maturity]                        │ │
│  ├─────────────────────────────────────────────┤ │
│  │ CD #1193  │ 3.1% APY │ 90d lock │ 500 XFG  │ │
│  │ Matures: 2026-09-13  │ Accrued: 3.82 XFG   │ │
│  │ [Claim on Maturity]                        │ │
│  └─────────────────────────────────────────────┘ │
│                                                  │
│  [Create New CD]                                 │
│                                                  │
│  ── CD Market ───────────────────────────────    │
│  ┌─────────────────────────────────────────────┐ │
│  │ CD #1244  │ 4.8% APY │ 14d left │ 200 XFG  │ │
│  │ Seller: @alice     │ Price: 195 XFG        │ │
│  │ [Buy CD]                                    │ │
│  ├─────────────────────────────────────────────┤ │
│  │ CD #1238  │ 6.2% APY │ 21d left │ 100 XFG  │ │
│  │ Seller: @bob       │ Price: 102 XFG        │ │
│  │ [Buy CD]                                    │ │
│  └─────────────────────────────────────────────┘ │
│                                                  │
│  [List CD for Sale]                              │
└──────────────────────────────────────────────────┘
```

**Functions:**
- Your active CDs: list with APY, lock period, deposited amount, maturity date, accrued interest
- Claim matured CDs (one-click)
- Create CD flow: amount → lock period (7d/30d/90d/180d/365d) → APY shown → confirm
- CD Market: browse listed CDs from other users
- Buy CD from market: view CD details → enter bid → confirm
- List CD for sale: select your CD → set price → list
- Manage listing: adjust price, cancel listing

### 5. Swap View

```
┌──────────────────────────────────────────────────┐
│  SWAP                                            │
│                                                  │
│  ┌──────────────────────────────────────────────┐│
│  │          Order Book                          ││
│  │          XFG / ETH                           ││
│  │          Bids (green)  │  Asks (red)        ││
│  └──────────────────────────────────────────────┘│
│                                                  │
│  Swap XFG → ETH                                  │
│  ┌──────────────────────────────────────────────┐│
│  │ You send:  [  100.00  ] XFG                 ││
│  │ You get:    ~0.0523 ETH                      ││
│  │ Rate:       1 XFG = 0.000523 ETH             ││
│  │ Fee:        0.001 XFG                        ││
│  │                                              ││
│  │ [Confirm Swap]        [Cancel]               ││
│  └──────────────────────────────────────────────┘│
│                                                  │
│  Pair: [XFG/ETH ▼]                               │
│  Available pairs: ETH, SOL, BCH, ARB, XMR, BTC  │
└──────────────────────────────────────────────────┘
```

**Functions:**
- Pair selector dropdown (all swappable assets: ETH, SOL, BCH, ARB, XMR, BTC)
- For cross-chain pairs: order book from mm2 `orderbook` RPC (Komodo ecosystem compatible)
- For XFG/HEAT pair: order book from fuegod `/getorderbook` RPC (Fuego native P2P CLOB)
- Single-command swap: enter amount → see rate + fee → confirm
- Pairs: XFG/ETH, XFG/SOL, XFG/BCH, XFG/ARB, XFG/XMR, XFG/BTC, XFG/HEAT

### 6. Settings View

```
┌──────────────────────────────────────────────────┐
│  SETTINGS                                        │
│                                                  │
│  ── Appearance ──────────────────────────────    │
│  Theme:        [Dark ●] [Dim ○]                  │
│  Chart theme:  [Dark ▼]                          │
│                                                  │
│  ── Fiat Currency ───────────────────────────    │
│  Display in:   [USD ▼]                           │
│  Options: USD, EUR, GBP, CNY, JPY, KRW, SGD, RMB│
│                                                  │
│  ── Wallet ──────────────────────────────────    │
│  Remote Node:  [127.0.0.1:7783          ]       │
│  Userpass:     [••••••••                ]       │
│  Seed Phrase:  [Backup] [Restore]               │
│                                                  │
│  ── Fuego Node ──────────────────────────────    │
│  RPC URL:      [http://127.0.0.1:18081  ]       │
│  Status:       ⬤ Connected · Block 1,234,567    │
│                                                  │
│  ── Bridge ──────────────────────────────────    │
│  Ethereum:     [Connect MetaMask]  ⬤ Disconnected│
│  Solana:       [Connect Phantom]   ⬤ Connected   │
│  BSC:          [Connect Wallet]    ⬤ Disconnected│
│                                                  │
│  ── About ──────────────────────────────────     │
│  SwapXFG v0.1.0 · Fuego ÆzorAhai                │
│  Powered by Komodo DeFi Framework               │
│  Built on Qt 5.15 · GPL-2.0                     │
└──────────────────────────────────────────────────┘
```

**Functions:**
- Dark mode (Deep Black #0a0a0a) vs Dim mode (#121212)
- Multi-fiat selector for all price displays
- mm2 daemon connection config (remote node support)
- Wallet seed phrase backup/restore
- Fuego node RPC connection (fuegod)
- Bridge connections: MetaMask (ETH), Phantom (SOL), BSC wallet
- About with version, upgrade name (ÆzorAhai), licenses

---

## Komodo Ecosystem Compatibility

### We Keep mm2 RPC Untouched

The GUI talks to mm2 via these RPC methods. All community tools (mm2-client, PytomicDEX, mmtools/mpm, libmarketmaker, dexstats, dexplorer) work because they talk to the same mm2 daemon:

```
orderbook(base, rel)          — order book display
setprice(base, rel, ...)      — create/update orders
buy(base, rel, price, vol)    — taker order
sell(base, rel, price, vol)   — taker order
my_orders / my_recent_swaps   — order history
my_balance / my_tx_history    — wallet state
enable_coin / disable_coin    — chain management
start_simple_market_maker_bot — automated MM (CLI tools use this)
stop_simple_market_maker_bot  — stop MM bot
```

**No changes needed.** The Komodo tool ecosystem is compatible by definition — we share the mm2 daemon.

### What SwapXFG GUI Adds Over Komodo Tools

| Feature | Komodo tools | SwapXFG GUI |
|---------|-------------|-------------|
| Wallet UI | CLI / Qt (basic) | Fuego-branded dual-balance cards + Burn2Mint + alias support |
| Portfolio | None (balance only) | TradingView charts, XFG priced in all pairs, multi-fiat |
| Hearth AMM | None | Full XFG/HEAT swap + LP management UI |
| HEAT CDs | None | CD creation, market, management, claiming |
| Order book | CLI / Qt (existing) | Reskinned, dark theme, Lucide icons |
| Dark theme | Basic dark | Deep Black #0a0a0a + Fuego orange accents |
| Fonts | System default | Chakra Petch / Space Grotesk / JetBrains Mono |

---

## Implementation Phases

### Phase 0 — Backend: Fork mm2 → `fuego-swapd` (Parallel Track)

**Goal:** Working `fuego-swapd` binary that swapxfgui can spawn. This runs in parallel with the GUI frontend work.

1. **Clone & build** `KomodoPlatform/komodo-defi-framework` (Rust, `cargo build`)
2. **Add XFG coin config** — `coins` file entry with address prefixes, Electrum servers, protocol type
3. **Add adaptor signature crypto module** — port from `fuego_swapd` C++ to Rust:
   - `crypto/adaptor.rs` — Musig2 key aggregation, DLEQ proof, adaptor sign/extract
   - Unit tests for all crypto operations
4. **Add adaptor state machine** — `mm2_state_machine/adaptor.rs`:
   - 7 states (KEYS_EXCHANGED, ESCROW_FUNDED, PRESIGS_READY, CTR_LOCKED, SECRET_REVEALED, XFG_SPENT, REFUNDED)
   - Cooperative refund handling
   - Integration with existing swap lifecycle
5. **Add P2P message types** — `mm2_p2p/` adaptor exchange messages (key exchange, nonces, presigs)
6. **Add `swap_method` field** to orderbook/buy/sell/order structs — default `"htlc"`, optional `"adaptor"`
7. **Add Fuego custom RPC endpoints** — AMM quote, pool info, CDs, Burn2Mint, alias
8. **Rebrand** — rename binary to `fuego-swapd`, update metadata
9. **Build + release** — CI pipeline for macOS/Windows/Linux binaries

**Dependencies:** Rust toolchain, protobuf compiler, `cargo build`

---

### Phase 1 — Design System + Rebrand (Sprint 1)

**Goal:** The app builds and runs with Fuego colors, fonts, and branding. Unrecognizable from Komodo/Firo.

1. **Clone & build** swapxfgui on macOS (Qt 5.15 + vcpkg + CMake)
2. **Font installation** — bundle Chakra Petch, Space Grotesk, JetBrains Mono, Orbitron, Rajdhani as `.ttf` in `assets/fonts/`
3. **Global find/replace** — all string references:
   - `FiroDEX` / `firodex` → `SwapXFG` / `swapxfg`
   - `Firo` (brand) → `XFG`
   - `Komodo Wallet` / `AtomicDEX` (brand) → `SwapXFG`
   - `komodoplatform.com` / `firo.org` → (blank or swapxfg)
   - `GLEEC` references → remove
4. **QML theme injection** — create `FuegoTheme.qml` with all design tokens:
   ```
   // FuegoTheme.qml — single source of truth for all colors + fonts
   property color heatOrange: "#FF4D00"
   property color deepBlack: "#0a0a0a"
   property color cardBg: "#141414"
   property color cardBorder: "#2a2a2a"
   property font headerFont: Qt.font({ family: "Chakra Petch", weight: Font.Bold })
   property font bodyFont: Qt.font({ family: "Space Grotesk" })
   property font monoFont: Qt.font({ family: "JetBrains Mono" })
   property font counterFont: Qt.font({ family: "Orbitron" })
   ```
5. **Replace all assets** — icons (Lucide SVGs), app icon, splash screen, tray icon
6. **Update metadata** — CMakeLists.txt, README, LICENSE, vcpkg.json, app bundle ID
7. **Verify build** — `cmake --build .` must succeed, app opens with new branding

### Phase 2 — Sidebar + Navigation (Sprint 2)

**Goal:** New layout with persistent sidebar, status bar, 5 main views wired up.

1. **Strip old navigation** — remove bottom tabs, top bars, Komodo nav patterns
2. **Build sidebar QML component** — 200px wide, Deep Black, Fuego logo, nav items with orange active accent
3. **Build status bar QML component** — 28px, block height, mm2 status, bridge dots
4. **Wire view switching** — sidebar clicks load Wallet/Portfolio/Hearth/CDs/Swap/Settings into main area
5. **Test 3 platforms** — verify sidebar renders identically on macOS, Windows, Linux

### Phase 3 — Wallet + Burn2Mint (Sprint 3)

**Goal:** Fully functional Wallet view with dual balances, send/receive, Burn2Mint.

1. **Dual balance cards** — XFG card + HEAT card, exchange rate from Hearth RPC
2. **Send/Receive flow** — address input + QR display, alias (@name) lookup via fuegod RPC
3. **Burn2Mint widget** — XFG amount input → HEAT output calculation (from Hearth spot price) → confirm + broadcast
4. **Activity feed** — pull from fuegod RPC: mined blocks, sends, receives, CD deposits, Burn2Mint events
5. **C++ bridge** — `fuego_rpc.h` with `getBalance()`, `sendTransaction()`, `burn2Mint()`, `getAlias()`, `getActivity()`

### Phase 4 — Portfolio + TradingView (Sprint 4)

**Goal:** Portfolio view with TradingView charts and XFG price tiles for all swappable pairs.

1. **TradingView webview widget** — embed TradingView Advanced Chart via Qt WebEngine
2. **Default pair** — XFG/USD on first load
3. **Price tiles** — grid of XFG/ETH, XFG/SOL, XFG/BCH, XFG/ARB, XFG/XMR, XFG/BTC
4. **Pair switching** — click a price tile → main chart switches to that pair
5. **Multi-fiat selector** — Settings dropdown → all prices convert to selected fiat
6. **Total portfolio value** — sum of XFG + HEAT in selected fiat

### Phase 5 — Hearth AMM (Sprint 5)

**Goal:** Full Hearth view with chart, trade widget, and LP management.

1. **TradingView chart** — XFG/HEAT pair
2. **Pool stats** — XFG balance, HEAT balance, rate, PI controller deviation, TVL, 24h volume (from fuegod/Hearth RPC)
3. **Buy/Sell HEAT toggle** — amount input, calculated output, price impact, execute
4. **LP position section** — pooled amounts, % share, fees earned, add/remove liquidity flows
5. **C++ bridge** — `hearth_rpc.h` with `getPoolStats()`, `swap()`, `addLiquidity()`, `removeLiquidity()`, `getPosition()`

### Phase 6 — HEAT CDs (Sprint 6)

**Goal:** Complete CD management — create, view, claim, market.

1. **Your CDs list** — active CDs with APY, lock period, amount, maturity, accrued interest
2. **Create CD flow** — amount → lock period selector (7d/30d/90d/180d/365d) → APY display → confirm
3. **Claim matured CD** — one-click redeem
4. **CD Market** — browse listed CDs, buy/sell flow, manage listings
5. **CD calculator** — projected returns for amount + period (inline in Create CD flow)
6. **C++ bridge** — `cd_rpc.h` with `getCDs()`, `createCD()`, `claimCD()`, `getCDMarket()`, `buyCD()`, `listCD()`

### Phase 7 — Swap View + Backend Integration (Sprint 7)

**Goal:** Swap view wired to `fuego-swapd` daemon, orderbook from shared P2P network.

1. **Order book** — from `orderbook` RPC on `fuego-swapd` (same mm2-compatible format), styled with Fuego theme
2. **Swap modal** — single-command swap: amount → rate + fee → confirm → broadcast via `buy`/`sell` RPC
3. **Swap method indicator** — show whether the order uses HTLC or adaptor signature; adaptor shown with privacy badge
4. **Dark/Dim toggle** — Dark (#0a0a0a) vs Dim (#121212), saved to settings
5. **Toast notifications** — swap confirmations, CD maturity alerts, daemon connect/disconnect
6. **Performance pass** — QML profiler, reduce unnecessary re-renders, smooth sidebar transitions
7. **CI pipeline** — GitHub Actions for macOS/Windows/Linux builds
8. **fuego-swapd binary bundling** — download/pack `fuego-swapd` in CI, ship with .dmg/.exe/.AppImage

### Phase 8 — Bridge Integration (Sprint 8)

**Goal:** MetaMask/Phantom bridge connections with auto-detect.

1. **Bridge status bar dots** — green for connected, red for disconnected, gray for not configured
2. **Auto-detect wallets** — check for MetaMask/Phantom on app launch
3. **Connect flow** — Settings → click "Connect MetaMask" → browser prompt → poll for connection
4. **Bridge health** — periodic reconnect check, toast on disconnect
5. **C++ bridge** — `bridge_manager.h` extending existing `bridge.go` logic to Qt

---

## C++ Backend Changes Summary

### Keep (mm2 RPC client — unchanged)

| File | Purpose |
|------|---------|
| `src/core/atomicdex/api/mm2/mm2.client.h/.cpp` | **KEEP** — mm2 JSON-RPC client. All Komodo ecosystem compatibility. Talks to `fuego-swapd` which exposes the same RPC interface. |
| `src/core/atomicdex/api/mm2/rpc_v1/` | **KEEP** — v1 RPC structs (enable, electrum, my_balance, buy, sell, my_orders, etc.) |
| `src/core/atomicdex/api/mm2/rpc_v2/` | **KEEP** — v2 RPC structs (orderbook, best_orders, trade_preimage, withdraw, etc.) |

### Modify (swap daemon spawning + coin config)

| File | Change |
|------|--------|
| `src/core/atomicdex/services/mm2/mm2.service.cpp` | **MODIFY** — spawn `fuego-swapd` binary instead of `mm2_firo`. Keep same RPC port (7783). |
| `src/core/atomicdex/services/mm2/mm2.service.cpp` | **MODIFY** — add XFG coin to default activation list alongside KMD/BTC |
| `src/core/atomicdex/config/coins.cfg.cpp` | **MODIFY** — add XFG coin definition (address prefixes, Electrum servers, coin_type) |

### New (fuego-swapd custom RPC bridges)

| File | Purpose | Endpoints Wrapped |
|------|---------|-------------------|
| `src/core/fuego_rpc.h/.cpp` | **NEW** — Fuego daemon RPC bridge | `getFuegoBalance()`, `sendTransaction()`, `burn2Mint()`, `getAlias()`, `resolveAlias()`, `getActivity()`, `getBlockHeight()` |
| `src/core/hearth_rpc.h/.cpp` | **NEW** — Hearth AMM RPC bridge | `getPoolStats()`, `getAmmQuote()`, `addLiquidity()`, `removeLiquidity()`, `getPosition()` |
| `src/core/cd_rpc.h/.cpp` | **NEW** — CD RPC bridge | `getCDs()`, `createCD()`, `claimCD()`, `getCDMarket()`, `buyCD()`, `listCDForSale()`, `cancelCDListing()` |
| `src/core/fuego_ws_client.h` | **NEW** — WebSocket for real-time block/swap events | `onBlock()`, `onSwapUpdate()`, `onOrderFill()` |

### New (QML theming)

| File | Purpose |
|------|---------|
| `src/app/FuegoTheme.qml` | **NEW** — QML singleton: all design tokens (colors, fonts, shadows, animation) |

---

## Risks

| Risk | Mitigation |
|------|-----------|
| Qt 5.15 EOL / Qt 6 QML breaking changes | Stick with Qt 5.15 for initial release; plan Qt 6 migration separately |
| fuego-swapd compatibility with upstream mm2 | Pin against specific komodo-defi-framework commit; rebase periodically |
| 11K+ commit codebase | Only touch QML files for UI changes; keep all C++ mm2 code unchanged |
| Cross-platform QML rendering differences | CI matrix from Phase 2; test macOS/Windows/Linux every sprint |
| GPL-2.0 license | All modifications also GPL-2.0; document third-party code |
| TradingView widget requires internet | Graceful fallback: "Chart unavailable offline" placeholder |
| Adaptor sig protocol unproven in mm2 | Port from production `fuego_swapd` C++; exhaustive unit tests; differential test against HTLC flow |
| Komodo upstream rejects adaptor sigs | We own the fork; adaptor sigs are optional per-order; HTLC fallback always available |
| Rust learning curve | Porting adaptor sigs from C++ to Rust is mechanical (EC math is EC math); core team already knows Rust |
