# Brand options — coin marks, Bank of XFG wordmarks, lockups

*Generated 2026-08-10 from brand-assets-guidelines + strategic tone: cold “already won,” sovereign private bank, forge orange on void black. DIGM out of scope.*

These are **concept renders** (not final SVG masters). Pick a direction, then vectorize.

---

## Tone target

| Lever | Expression |
|-------|------------|
| Governing / cold win | Matte steel, hairline orange, no neon bloom, no hype copy |
| Legacy TradFi bank | Vault medallion, shield/seal, tracked type, hairline rules |
| Fuego DNA | Flame / G monogram, `#FF6B35`, black void `#0a0a0f` |

---

## Coin marks

| File | Name | Read | Fit for |
|------|------|------|---------|
| `coin-A-guilloche-seal.jpg` | Guilloche seal | Orange ring + steel knot glyph | Alternate abstract coin (less G recognition) |
| `coin-B-shield-crest.jpg` | Shield crest | Bank shield, geometric flame spikes | Heraldic / formal bank crest |
| `coin-C-steel-flame-G.jpg` | Steel Flame G | Vault ring + brushed G/flame, ember tip | Continuity with live mark, colder |
| `coin-D-flat-app-icon.jpg` | Vault keyhole G | Flat orange G, flame as keyhole spike | App icon / 32px clarity |
| `coin-E-cooled-flame-G.jpg` | Cooled Flame G | Edit of current Flame G → gunmetal + thin orange | **Best coin continuity + bank cold** |

**Primary coin recommendation: `coin-E-cooled-flame-G.jpg`**  
Keeps the existing monogram (recognition, suite icons) but drops crypto-neon glow for vault steel. Secondary for mobile: `coin-D` if E is too detailed at 24px.

Avoid as sole network mark: A (glyph drift), B (reads esports/shield more than bank seal unless refined).

---

## Wordmarks (copy logo)

| File | Name | Read | Fit for |
|------|------|------|---------|
| `wordmark-A-stacked-type.jpg` | Stacked type | Silver `BANK OF` over orange `XFG` | Landing, splash, “already won” silence |
| `wordmark-B-single-line.jpg` | Single line | `BANK OF` silver + `XFG` orange, one line | Nav bars, docs header, legal |

**Primary wordmark recommendation: `wordmark-A-stacked-type.jpg` for marketing; `wordmark-B` for chrome.**  
Type-only is the purest “legacy bank” move. No flame required when the institution is speaking.

Canonical casing in lockups: **BANK OF XFG** (all caps, tracked). Prose: Bank of XFG.

---

## Mixtures / lockups

| File | Name | Read | Fit for |
|------|------|------|---------|
| `lockup-A-vault-medallion.jpg` | Vault medallion | Orange G on steel disc + silver BANK OF / XFG | Product UI header, swap/hearth chrome |
| `lockup-B-faceted-G-stack.jpg` | Faceted G stack | Large orange G + flame + BANK OF XFG | Hero / merch (warmer; less cold) |
| `lockup-C-master-horizontal.jpg` | Sovereign vault | Steel FG medallion + BANK OF / XFG type | **Best bank + win hybrid** |

**Primary lockup recommendation: `lockup-C-master-horizontal.jpg`**  
- Metal medallion = TradFi vault language  
- Thin orange flame = Fuego DNA without screaming  
- `BANK OF` / rule / `XFG` = annual-report hierarchy  
- Optional rim legend “SOVEREIGN VAULT · EST. 2009” aligns with HEAT peg CPI story (Q1 2009) — use as **optional** prestige line, not mandatory on every UI  

Runner-up: `lockup-A` (closer to current Flame G in the disc).  
Use `lockup-B` only when you want hotter forge energy (less “cold win”).

---

## Recommended system (ship set)

| Role | Asset |
|------|--------|
| Network / coin icon | **coin-E** (cooled Flame G); fallback **coin-D** at tiny sizes |
| Bank wordmark (hero) | **wordmark-A** |
| Bank wordmark (UI) | **wordmark-B** |
| Full bank identity | **lockup-C** (or A if you want G not FG monogram) |

That trio matches governing attitude: **steel first, orange as proof of life, silence as confidence.**

---

## Next craft steps

1. Vectorize winners in SVG (single-color mono + color + reverse).  
2. Build 16/32/64 favicon silhouette from coin-D or coin-E.  
3. Replace placeholder Mintlify logos when chosen.  
4. Do **not** commit glow-heavy faceted G as the bank master if cold bank is the mandate.

Session copies also live under the agent session `images/` folder; **repo copies** here are the durable reference.
