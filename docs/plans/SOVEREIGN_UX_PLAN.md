# SOVEREIGN_UX_PLAN — SWAPXFG TUI

## Current state

The TUI (`swapxfg/app/`) is a Bubbletea app with:

- **Sidebar**: views m/c/s, pair list, ETH/SOL/BCH bridge status, XFG balance
- **Markets view**: ticker row, chart (left), orderbook + tape (right)
- **CD view**: offer list with accept
- **Status view**: daemon height, active offers, in-flight swaps
- **Input bar**: slash-cmd entry, wallet balance display
- **Swap modal**: `swap <amt> [pair]` opens confirmation overlay, signs offer, submits to daemon
- **Order entry**: `order` opens form overlay (side/pair/price/amount/TTL), signs + places via daemon RPC
- **P2P commands**: `cancelorder <id>`, `myorders`
- **Bridge**: `connect metamask|phantom|bch`, auto-connect on pair switch

### What works

| Feature | Status |
|---------|--------|
| Multi-pair display (SOL/ETH/XMR/BCH/ARB/BASE/BNB) | done |
| P2P orderbook depth ladder (asks/bids) | done |
| Order book aggregation (toggle raw/agg with `d`) | done |
| Spread line (absolute + percentage + mid-price) | done |
| Trade tape (recent fills) | done |
| Price chart (ASCII, candlestick + line) | done |
| Chart timeframes (5m/15m/1h/4h/1d/1w via `[/]`) | done |
| Chart daemon candles (OHLCV from `/getcandles`) | done |
| Swap modal (sign + submit) | done |
| Order entry form overlay | done |
| Amount validation (min 10 XFG, balance check) | done |
| Fill tracking (post-place partial fill %) | done |
| Form keyboard help (contextual hints) | done |
| Pair switch in order form (`p` key) | done |
| Open orders panel (`O` key, select + cancel) | done |
| Trade history panel (`h` key, scrollable) | done |
| Bridge connect (ETH/SOL/BCH) | done |
| Wallet balance in input bar | done |
| Daemon status view | done |
| CD market depth ladder (aggregated by tier) | done |
| CD create form (`n` key, amount + term) | done |

## What's missing

### 1. CD create form submit
CD create form renders but `Enter` on submit doesn't call `CreateCd` RPC yet.

### 2. CD accept offer flow
`Enter` on selected CD offer shows a status message but doesn't trigger accept/swap flow.

## Not planned

These are explicitly out of scope:

- **Omnibar with autocomplete**: The slash-cmd input works. Adding autocomplete adds complexity without clear value — users learn the 10 commands fast.
- **Persistent sidebar navigation rewrite**: Current m/c/s sidebar works. No need to redesign the navigation hierarchy.
- **Auto-bridge handshakes beyond ETH/SOL**: BCH is already RPC-connected. Other chains (XMR, ARB) are future work and don't need auto-connect yet.
- **Mobile/responsive layout**: Terminal-only app. Responsive is not a concern.
- **Dark/light theme toggle**: Single theme, hardcoded. No plan to add theme switching.

## Completed

All 10 original priority items are done:

| # | Item | Status |
|---|------|--------|
| 1 | Open orders panel | done — `O` key overlay |
| 2 | Amount validation | done — min 10 XFG, balance check |
| 3 | Fill tracking | done — post-place partial fill % |
| 4 | Form keyboard help | done — contextual hints |
| 5 | Order book aggregation | done — `d` toggle, 1% buckets |
| 6 | Trade history panel | done — `h` key overlay |
| 7 | Chart timeframes | done — `[/]` cycles 5m→1w |
| 8 | Pair switch in order form | done — `p` key cycles |
| 9 | CD orderbook | done — depth ladder by tier |
| 10 | Spread line polish | done — %, mid-price |

## Architecture notes

- All new panels follow the same pattern: model struct in its own file, `Render*` function, toggled by key or command, overlaid via `overlayAt`.
- Fetch in `FetchAll` runs parallel RPC calls. Add new endpoints to the parallel fan-out.
- Form overlays (swap modal, order entry) take priority in `Update()` — key events don't fall through to the main view.
- Orderbook data is already in `AllPairData.Books[pair]`. No new data structures needed for aggregation — just a render-time transform.
