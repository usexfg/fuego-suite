# Fuego Network & Bank of XFG — Brand Assets Guidelines

*Last updated: 2026-08-10*  
*Scope: **Fuego Network**, **Bank of XFG**, **XFG**, **HEAT / ΗΞΔŦ**, **Hearth**, **swapXFG**, suite UI.*  
*Out of scope: **DIGM** — separate brand system (own mark, palette, and guidelines later).*

Companion documents:

| Document | Role |
|----------|------|
| [`xfg-marketing-guidelines-copy.md`](./xfg-marketing-guidelines-copy.md) | Positioning, voice, personas, language |
| [`market_narrative.md`](./market_narrative.md) | Same narrative spine (strategy mirror) |
| `dashboard/static/css/style.css` | Live UI token source of truth |
| `docs/docs.json` | Mintlify docs theme colors |
| `tui/main.go` | Terminal palette |

This file is the **visual + naming system**. Marketing copy owns voice and messaging; this owns marks, color, type, layout rules, and asset inventory.

---

## 1. Brand family

```
Fuego Network          ← protocol / chain / infrastructure identity
└── Bank of XFG        ← product framing: sovereign private bank metaphor
    ├── XFG            ← base-layer privacy money (ticker + coin mark)
    ├── HEAT / ΗΞΔŦ    ← flatcoin product brand (family member, not orphan)
    ├── Hearth         ← on-chain exchange / AMM + CLOB hybrid
    └── swapXFG        ← cross-chain atomic swap surface
```

**DIGM is not part of this family for brand work.** Do not co-brand DIGM with Fuego marks in lockups, do not reuse DIGM-only assets as “Fuego suite defaults,” and do not put DIGM under Bank of XFG lockups until DIGM has its own brand guide.

### One-liner (from strategy)

> Fuego is a sovereign private bank that pays you to disappear.

Category language: **Private DeFi / Sovereign Money Protocol** — not “privacy coin” as the whole product.

---

## 2. Naming system

### Canonical names

| Use | Canonical | Acceptable short | Avoid |
|-----|-----------|------------------|--------|
| Protocol / chain | **Fuego Network** | Fuego | “Fuego coin” as sole identity |
| Product frame | **Bank of XFG** | Bank of XFG | “Fuego Bank” alone (daemon banner uses *Fuego Bank of XFG* — keep full form in formal copy) |
| Base asset | **XFG** | XFG | XFG Coin, FuegoCoin |
| Flatcoin (prose / UI labels) | **HEAT** | HEAT | Heat, heat (except code identifiers) |
| Flatcoin (display / cult mark) | **ΗΞΔŦ** | ΗΞΔŦ | Random Greek mashups; mixing ΗΞΔŦ inside sentences mid-paragraph without explanation |
| Exchange | **Hearth** | Hearth | HearthDEX, “the DEX” as sole name |
| Swaps product | **swapXFG** / **SwapDaemon** | swapXFG | AtomicDEX, MM2 (removed) |
| Suite | **Fuego Suite** | fuego-suite (repo) | “Fuego ecosystem apps” fluff |

### HEAT / ΗΞΔŦ rules

| Context | Form |
|---------|------|
| Docs body, API, wallets, code comments (user-facing) | **HEAT** |
| Headlines, splash, merch, cult/in-group art | **ΗΞΔŦ** (optional co-mark with HEAT) |
| First use in long-form | Prefer: `HEAT (ΗΞΔŦ)` once, then **HEAT** |
| Ticker tables, charts, RPC | **HEAT** only |
| Never | “stablecoin” as primary noun when you mean **flatcoin** (CPI / purchasing-power framing). “Stablecoin” is OK only when contrasting freezable USDC-class assets. |

**ΗΞΔŦ composition (display mark, not a second ticker):**

| Glyph | Role | Visual read |
|-------|------|-------------|
| **Η** | Eta | H |
| **Ξ** | Xi | E (triple bar) |
| **Δ** | Delta | A (triangle) |
| **Ŧ** | T with stroke | T |

Use a font that renders Greek + stroke-T cleanly (Saira often needs a fallback for Ξ/Δ/Ŧ — pair with system Greek or a display face that covers them). Prefer all-caps tracking slightly open (`letter-spacing: 0.04–0.08em`) for ΗΞΔŦ lockups.

### Bank of XFG

- Formal product identity for the **sovereign bank metaphor**: CDs, HEAT savings, private settlement.
- Daemon UI string (code): `Fuego Bank of XFG` (`DaemonCommandsHandler`).
- Network string: `Fuego Network` / `Fuego TESTNET` (`CryptoNoteProtocolHandler`).
- Do not brand CDs as “staking.” CDs are **Certificates of Deposit**.

### Product glossary (brand-facing)

| Term | Meaning |
|------|---------|
| **XFG** | Base-layer privacy coin; burned to mint HEAT |
| **HEAT / ΗΞΔŦ** | Algorithmic flatcoin; purchasing-power oriented; peg reference `HEAT_PEG_USD = 1.58` (CPI-adjusted design path) |
| **CD** | Certificate of Deposit — term-locked **HEAT**, yield from protocol fees |
| **Hearth** | On-chain XFG/HEAT market (AMM + orderbook hybrid) |
| **Eternal Flame** | Accounting for destroyed base-layer coins |
| **Epoch** | Protocol accounting period (~5 days) |
| **Fire Alias** | Short on-chain identity |
| **Atomic swap** | Peer-to-peer cross-chain exchange (swapXFG) |

Words to prefer vs avoid: see marketing guidelines (`sovereign`, `yield`, `protocol revenue` — not staking/rewards/governance token).

---

## 3. Logo & mark system

### 3.1 Primary mark — Flame G (Fuego / XFG)

**Canonical asset paths:**

| Asset | Path | Spec |
|-------|------|------|
| Fuego app / chain icon | `dashboard/static/coin-icons/fuego.png` | 200×200 PNG, RGB |
| XFG ticker icon (dashboard) | `dashboard/static/coin-icons/xfg.png` | 200×200 PNG |
| XFG ticker icon (suite icons) | `dashboard/coin icons/xfg.png` | 200×200 PNG |

**Description:** Ember-orange geometric **G** (or flame-form monogram) with a vertical flame tongue through the center, soft outer glow, on black (or transparent when exported). Reads as forge + fire, not cartoon campfire.

**Usage:**

| Surface | Treatment |
|---------|-----------|
| App icon, favicon, nav brand | Flame G alone |
| Coin lists / swap pair rows | Flame G as XFG |
| Marketing hero | Flame G + wordmark **FUEGO** or **Fuego Network** |
| Bank framing | Flame G + **Bank of XFG** wordmark (Saira Bold / ExtraBold) |
| Docs (Mintlify) | Prefer SVG lockups: `docs/images/logo-dark.svg`, `logo-light.svg`, `favicon.svg` (ensure these match Flame G; regenerate if still placeholder) |

**Clear space:** Minimum clear space = **½ the mark height** on all sides. No text, UI chrome, or competing logos inside that box.

**Minimum size:**

| Medium | Min mark height |
|--------|-----------------|
| UI / nav | 24px |
| Print | 12 mm |
| Favicon | 16px (simplified solid silhouette if glow fails) |

**Backgrounds:**

| Background | Mark |
|------------|------|
| `#0a0a0f` / near-black | Full color with glow |
| Light (`#f5f5f7` / white) | Full color **or** dark monochrome; avoid washing orange into white without contrast check |
| Busy photography | Place mark in a dark circular badge (diameter ≥ 1.25× mark) |

**Do not:**

- Recolor Flame G to green/purple “for fun”
- Flatten into low-contrast gray on dark UI
- Stretch, skew, or add drop shadows beyond the built-in glow
- Place on mid-gray where orange disappears
- Combine with DIGM logo in a single lockup

### 3.2 HEAT / ΗΞΔŦ marks

| Asset | Path | Notes |
|-------|------|-------|
| HEAT flame + word (orange type) | `digm-platform/frontend/public/assets/heatlogo.png` | Shared historical asset; **may** be used for HEAT product art until suite-owned path is copied under `dashboard/` or `docs/images/` |
| HEAT flame + word (violet type) | `digm-platform/frontend/public/assets/heatsologo.png` | Alternate type color — use for violet accent moments only |
| Suite coin icon | Prefer a dedicated `dashboard/coin icons/` / `static/coin-icons/` HEAT icon (add if missing; do not silently reuse DIGM logo) |

**Description:** Faceted / low-poly flame (orange–gold core, cool purple edges) with **HEAT** wordmark centered. Black field.

**HEAT mark rules:**

- HEAT is a **family member** of Fuego: use Flame G for network; use flame + HEAT / ΗΞΔŦ for flatcoin product surfaces.
- Do not use HEAT flame as a substitute for Flame G on node/wallet “Fuego Network” chrome.
- **ΗΞΔŦ** lockups: custom type or carefully set Unicode; keep flame optional behind or beside, not crushing letterforms.
- Orange HEAT type ≈ default product; violet HEAT type ≈ night / premium / “ember core” variant only.

### 3.3 Wordmarks

| Wordmark | Type | Weight | Notes |
|----------|------|--------|-------|
| **FUEGO** | Saira | 700–800 | Tracking slightly tight |
| **Fuego Network** | Saira | 600 title / 400 sub | Two-line OK |
| **Bank of XFG** | Saira | 600–700 | “of” can be 400–500, slightly smaller |
| **XFG** | Saira or IBM Plex Mono | 600–700 | Mono for tables |
| **HEAT** | Saira | 700 | Match heatlogo geometry when possible |
| **ΗΞΔŦ** | Display / Saira + Greek fallback | 700 | Special-case; see §2 |
| **Hearth** | Saira | 600 | Never all-caps unless UI chrome |
| **swapXFG** | Saira or Mono | 600 | Camel **XFG** |

### 3.4 Lockups

1. **Network lockup:** `[Flame G]  Fuego Network`  
2. **Bank lockup:** `[Flame G]  Bank of XFG`  
3. **Dual asset:** `[Flame G] XFG  ·  [HEAT flame] HEAT` (equal optical weight)  
4. **ΗΞΔŦ splash:** Flame G small + **ΗΞΔŦ** large, or HEAT flame + **ΗΞΔŦ** — never both flames fighting at equal size  
5. **Hearth:** Flame G or no mark + **Hearth** wordmark + forge orange underline or hairline

Horizontal lockups preferred. Stacked only when width &lt; 200px.

---

## 4. Color system

### 4.1 Core palette (dashboard source of truth)

From `dashboard/static/css/style.css` `:root`:

| Token | Hex | Role |
|-------|-----|------|
| `--bg-primary` | `#0a0a0f` | App canvas, void |
| `--bg-secondary` | `#12121a` | Nav, secondary surfaces |
| `--bg-tertiary` | `#1a1a25` | Elevated panels |
| `--bg-card` | `#141420` | Cards |
| `--bg-hover` | `#1e1e2d` | Hover fill |
| `--border` | `#2a2a3a` | Default borders |
| `--border-bright` | `#3a3a50` | Emphasis borders |
| `--text-primary` | `#e8e8f0` | Body text on dark |
| `--text-secondary` | `#8888a0` | Labels, secondary |
| `--text-muted` | `#555570` | Meta, disabled-adjacent |
| **`--accent` (Forge Orange)** | **`#ff6b35`** | Primary brand accent, CTAs, active nav |
| `--accent-dim` | `rgba(255, 107, 53, 0.15)` | Accent wash |
| `--green` | `#00d4aa` | Positive / bid / HEAT success moments |
| `--red` | `#ff4757` | Negative / ask / error |
| `--blue` | `#4a9eff` | Info / links secondary |
| `--yellow` | `#ffc107` | Warning |
| `--purple` | `#a855f7` | Rare accent (pairs with HEAT flame cool edge) |

**Forge Orange** is the brand spine. Green is **signal**, not logo color. Do not replace orange with teal for “crypto default.”

### 4.2 Mintlify / docs theme

From `docs/docs.json`:

| Token | Hex | Role |
|-------|-----|------|
| `colors.primary` | `#FF4500` | Docs primary (OrangeRed — hotter than UI accent) |
| `colors.light` | `#FF6B35` | Light mode accent (= dashboard accent) |
| `colors.dark` | `#CC3700` | Dark mode pressed / deep forge |

**Reconciliation rule:** Product UI uses **`#ff6b35`**. Marketing flame art may run hotter toward **`#FF4500`–`#FF6B35`**. Docs may use `#FF4500` as Mintlify primary. When exporting a single brand book swatch for “primary orange,” list:

1. **UI Accent / Forge Orange** — `#FF6B35`  
2. **Docs / Flame Hot** — `#FF4500`  
3. **Deep Forge** — `#CC3700` / `#CC4400` (TUI `accentDim`)

### 4.3 Terminal (TUI)

From `tui/main.go`:

| Token | Hex |
|-------|-----|
| accent | `#FF6B35` |
| accentDim | `#CC4400` |
| bg | `#0a0a0a` |
| fg | `#D0D0D0` |
| muted | `#555555` |
| border | `#2a2a2a` |
| good | `#00CC66` |
| warn | `#FFAA00` |
| bad | `#FF4444` |

Terminal green/red may differ slightly from dashboard — keep **accent orange identical**.

### 4.4 HEAT-specific color moments

| Moment | Color |
|--------|-------|
| HEAT balance positive | `#00d4aa` (dashboard green) or HEAT orange type |
| ΗΞΔŦ display type | `#FF6B35` default; `#a855f7` / heatlogo violet for night variant |
| Flame core | Gold–white `#FFE6A0` → orange `#FF6B35` → deep `#CC3700` |
| Flame cool edge | Soft violet / blue-purple (as in heatlogo assets) |

### 4.5 Contrast & accessibility

- Body text on `#0a0a0f`: use `#e8e8f0` (not pure white noise for long form).  
- Accent orange on black: OK for large text / icons; **not** for 12px body links without underline or secondary cue.  
- Never put `#ff6b35` text on `#00d4aa` or `#ff4757` fills.

### 4.6 Gradients (allowed)

- Flame: radial or multi-stop orange–gold–deep forge.  
- UI cards: flat preferred; optional 2% noise, no candy gradients.  
- Avoid rainbow crypto-bro gradients.

---

## 5. Typography

| Role | Font | Weights | Source / license |
|------|------|---------|------------------|
| UI / body / display | **Saira** (variable 100–900) | 400 body, 500 labels, 600–800 titles | Google Fonts / fontsource · OFL-1.1 · vendored in `dashboard/static/vendor/fonts/` |
| Code / mono / amounts | **IBM Plex Mono** | 400, 500, 600 | Google Fonts / fontsource · OFL-1.1 · vendored |

**Why Saira:** Squarish geometric humanist; terminal discipline with display range.  
**Why IBM Plex Mono:** Neutral engineering mono; balances and hex without competing with Saira.

**Fallbacks:**

```text
--font-sans: 'Saira', -apple-system, BlinkMacSystemFont, sans-serif;
--font-mono: 'IBM Plex Mono', 'SF Mono', 'Fira Code', monospace;
```

**Scale (dashboard baseline 14px):**

| Step | Size | Use |
|------|------|-----|
| meta | 11px | Uppercase labels (`letter-spacing: 0.5px`) |
| ui | 12–13px | Tables, nav |
| body | 14px | Default |
| title | 16–20px | Card / page titles |
| display | 28–40px | Marketing / ΗΞΔŦ |
| hero | 48px+ | Landing only |

**Do not** use Poppins/Lora (Anthropic skill defaults) or Inter as brand identity fonts. Inter is acceptable only as OS fallback, not marketing lockups.

---

## 6. UI surfaces & motion

### Surfaces

- **Dark-first.** Light mode is secondary; if required, invert carefully — keep forge orange, do not invent a pastel brand.
- Radius: `--radius: 8px`, `--radius-sm: 4px`.
- Shadows: soft, dark (`0 2px 8px rgba(0,0,0,0.3)`), never colorful glows on every card.
- Nav height ~48px; brand mark 24px in nav.

### Motion

- Fast, functional (150ms color/border transitions).
- No bounce, no confetti, no “we’re excited” animation language.
- Status: green pulse for online is OK (already in CSS `.status-dot.online`).

### Iconography

- Coin icons live under `dashboard/coin icons/` and `dashboard/static/coin-icons/`.
- XFG / Fuego always Flame G; foreign chains keep their own marks (do not recolor them to forge orange).
- Prefer flat circular or rounded-square badges for multi-chain rows.

---

## 7. Voice (visual-adjacent)

Full voice system: marketing guidelines. Visual summary:

| Do | Don’t |
|----|-------|
| Hard, declarative, short | Exclamation spam, emoji walls |
| Manifesto density | Press-release fluff |
| Cult-in-group marks (ΗΞΔŦ, Eternal Flame) earned | Random meme fonts |
| Show the terminal | Fake “easy money” lifestyle stock |

Photography / illustration: forge, ember, void black, metal, night city without neon overload. Avoid corporate handshake stock.

---

## 8. Application map

| Surface | Brand expression |
|---------|------------------|
| **fuegod / node** | “Fuego Network” / “Fuego Bank of XFG” in CLI banners |
| **fire_wallet / wallets** | Flame G + XFG/HEAT balances; HEAT never as “token rewards” |
| **TUI** | Orange accent, dark void, Saira-equivalent terminal hierarchy |
| **Hearth dashboard** | `hearth.html` — Fuego nav mark, forge orange chrome, green/red book |
| **swapXFG UI** | Flame G for XFG side of bridge; pair foreign icons as-is |
| **Mintlify docs** | `docs.json` oranges; logo SVGs; **content must match product** (see §11) |
| **Social / avatars** | Flame G on black; HEAT flame for HEAT-only channels |
| **Merch** | ΗΞΔŦ or Flame G; one mark per item |

---

## 9. Asset inventory & ownership

### In-repo (suite)

| Asset | Location | Brand owner |
|-------|----------|-------------|
| Flame G PNG | `dashboard/static/coin-icons/fuego.png`, `xfg.png`; `dashboard/coin icons/xfg.png` | Fuego / XFG |
| Dashboard CSS tokens | `dashboard/static/css/style.css` | Fuego UI |
| Saira / IBM Plex Mono | `dashboard/static/vendor/fonts/` | Fuego UI |
| TUI colors | `tui/main.go` | Fuego TUI |
| Mintlify colors + logo paths | `docs/docs.json`, `docs/images/*` | Docs |
| Strategy / voice | `docs/strategy/*` | Brand narrative |

### Shared / relocate candidates

| Asset | Current | Action |
|-------|---------|--------|
| `heatlogo.png`, `heatsologo.png` | under digm-platform public assets | **Copy** into `docs/images/heat/` and `dashboard/static/` for suite use; DIGM keeps its own copies. Do not treat digm-platform as the long-term HEAT brand home. |

### Missing / TODO assets

- [ ] Canonical SVG of Flame G (color + mono + favicon silhouette)
- [ ] Official **Bank of XFG** horizontal SVG lockup
- [ ] Official **ΗΞΔŦ** wordmark SVG (paths, not only Unicode)
- [ ] Suite-local HEAT coin icon (200×200) under `dashboard/coin icons/`
- [ ] Light-mode logo variants that still pass contrast
- [ ] Open Graph / social share default (1200×630, Flame G + one-liner)

---

## 10. Explicit non-goals (this guide)

| Out | Why |
|-----|-----|
| **DIGM** logo, digm-orange systems, DIGM lockups | Own brand later |
| Anthropic Poppins/Lora palette from skill scaffolding | Pattern only — not Fuego identity |
| “Community mascot” that dilutes forge seriousness | Unless deliberately cult and still on-palette |
| Green primary brand | Green = PnL / success signal |

---

## 11. Docs & Mintlify (ops reminder)

**Action item (not visual design):** Invoke **Mintlify** and overhaul **https://docs.usexfg.org** so public docs match current Fuego:

1. Add **current** features: HEAT flatcoin mint path, HEAT CDs (not legacy “lock XFG CD” framing), Hearth AMM/orderbook, swapXFG multi-chain, Eternal Flame, fee routing as implemented.
2. **Replace incorrect basics** — intro and feature pages still mix deprecated models (e.g. STARK-burn-only HEAT, “lock XFG for CDs,” fee tables that disagree with code, DIGM-first nav weight, outdated burn2mint-only stories).
3. Align `docs/docs.json` navigation with reality; keep DIGM as a thin optional section or separate site later.
4. Sync brand: logo SVGs = Flame G; colors already forge-family (`#FF4500` / `#FF6B35` / `#CC3700`).
5. Source of product truth while rewriting: `AGENTS.md`, `DEPOSIT_ARCHITECTURE.md` (if present), live code paths (`mintHeatV10`, Hearth, SwapDaemon), not stale `docs/heat/*` vision-only files without a “status” banner.

Brand assets do not ship correctness. **Docs correctness is a separate, high-priority content task.**

---

## 12. Quick checklist (ship gate)

Before publishing any Fuego / Bank of XFG / HEAT asset:

- [ ] Correct mark (Flame G vs HEAT flame vs ΗΞΔŦ)?
- [ ] No DIGM co-brand unless explicitly dual-product and approved?
- [ ] Forge orange `#FF6B35` (UI) or documented hot `#FF4500` (docs/flame art)?
- [ ] Saira + IBM Plex Mono only for identity type?
- [ ] Clear space and min size respected?
- [ ] Naming: HEAT / flatcoin / CD / Hearth / sovereign — no staking/rewards/VC speak?
- [ ] Dark canvas default; glow not mud?

---

## 13. Versioning

| Version | Date | Notes |
|---------|------|-------|
| 0.1 | 2026-08-10 | Consolidated from strategy marketing copy, dashboard CSS, TUI, Mintlify `docs.json`, Flame G + HEAT logo inventory. DIGM excluded. |

When tokens change in `style.css` or `docs.json`, update **this file in the same PR**.
