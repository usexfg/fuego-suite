# DeXFG Ordergraph UI — Design Spec

**Date:** 2026-08-10  
**Surface:** `dashboard/static/swapxfg.html` + `swapxfg.js` + `style.css`  
**Data:** `GET /api/swapd` → swapd `buildStatusJson()` (`offers[]`, `swaps[]`)

## Goal

DeXFG is a **menu of availability**, not an insta-swap bridge. Hero is a multi-chain order plot; form and actions sit below.

## Page stack (top → bottom)

1. **Ordergraph** — columns = chains (α name labels, not logos on axis); Y = rate quality vs fair (center = oracle fair); markers = chain-colored disc + logo overlay
2. **Swap amount form** — secondary post/size controls
3. **DeXFG orderbook list** — full offer rows with Accept / related actions; selected row highlighted
4. **Active swaps** — in-flight only

## Marker encoding

| Channel | Meaning |
|---------|---------|
| Column (X) | Chain (stable alphabetical) |
| Y | Distance from fair (0% oracle mid-line) |
| Disc color | Chain brand color |
| Logo overlay | **Fuego** if selling XFG (`isSell`); **chain logo** if buying XFG |
| Radius | Remaining XFG (`xfgAmount − filledAmount`), log-scaled |
| Opacity | Expiry / time left (fainter = sooner) |
| Tooltip | Brief: side, remaining, rate, % fair, expires |
| Click | Scroll to orderbook row, select + highlight |

## Volume without chaos

- Columns **never** auto-reorder by depth
- Under each chain **word label**: depth bar (Σ remaining XFG) + open count

## Orderbook row

Full details + **Accept offer**, **Copy ID**, **Fill form** (prefill amount/chain). Accept uses wallet RPC when available (`initiate_swap` / future soft-take); otherwise toast + CLI hint.

## Out of scope

- Live column re-sort by popularity
- Dual Y scales (foreign vs XFG rate)
- Buttons on the plot itself
