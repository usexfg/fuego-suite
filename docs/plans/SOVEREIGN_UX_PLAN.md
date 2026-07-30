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
| Multi-pair display (SOL/ETH/XMR/BCH/ARB) | done |
| P2P orderbook depth ladder (asks/bids) | done |
| Trade tape (recent fills) | done |
| Price chart (ASCII) | done |
| Swap modal (sign + submit) | done |
| Order entry form overlay | done |
| Bridge connect (ETH/SOL/BCH) | done |
| Wallet balance in input bar | done |
| Daemon status view | done |
| CD offer list | done |

## What's missing

### 1. Open orders panel
No persistent view of your own open orders. `myorders` dumps to status bar text — should be a dedicated panel or overlay.

**Implementation**: Add `OpenOrders` field to `tuiModel`, fetch in `FetchAll`. Render as a persistent bottom panel in markets view or a toggleable overlay (`O` key). Show orderId, side, pair, price, amount, filled, TTL countdown. Allow selection + cancel via Enter or `x`.

### 2. Trade history panel
Tape shows last N trades but no scrollable history. Useful for checking fills.

**Implementation**: `GET /trades?pair=X&limit=100` RPC (may need daemon-side addition). Render as scrollable list in overlay (`h` key) or replace tape on toggle.

### 3. Partial fill feedback
When an order is placed and partially filled, the TUI shows a one-shot status message. No tracking of unfilled portion.

**Implementation**: After `PlaceOrder`, if `filled < amount`, store the orderId + remaining amount. On each refresh tick, check `myorders` for fill updates. Show live fill progress in status bar or open orders panel.

### 4. Order book spread line
The depth ladder shows asks and bids but no explicit spread indicator between best bid and best ask.

**Implementation**: Already computed in `RenderOrderbook` (spread line). Verify it renders at the boundary between asks and bids sections with the spread value displayed.

### 5. Amount input validation
Order entry form accepts any string. No guard against amounts exceeding wallet balance or below minimum trade size.

**Implementation**: In `ParseAmount()`, validate against `m.balance.Available` (if wallet connected). Show warning in form if amount > balance. Add minimum amount constant (e.g., 10 XFG = 100000000 atomic).

### 6. Pair quick-switch from order entry
Order entry form defaults to `m.activePair` but has no way to change pair without closing the form.

**Implementation**: Add `p` key in order entry form to cycle pairs. Or add a pair selector field to the form.

### 7. Order book depth aggregation
Current depth ladder shows individual price levels. For active pairs with many levels, this is noisy.

**Implementation**: Aggregate levels within 1% of each other into buckets. Show fine granularity near spread, coarse granularity further out. Toggle with `d` key (raw / aggregated).

### 8. Keyboard shortcut reference
`?` shows a one-line help string. No discoverability for form-specific keys.

**Implementation**: When order entry is active, show form-specific help line (Tab/ShiftTab: navigate, Space: toggle side, Enter: submit, Esc: cancel). When swap modal is active, show (y: confirm, n: cancel). Update help line contextually based on active overlay.

### 9. Chart timeframe
Chart is a single view of recent trades. No way to zoom in/out or switch timeframes.

**Implementation**: Add `1h / 4h / 1d / 1w` timeframe selector. Fetch candle data via `GET /candles?pair=X&interval=1h`. Render with adaptive X-axis labels. `[/]` keys to cycle timeframes.

### 10. CD market integration
CD view shows offers but no orderbook or price ladder. CD trading is buy/sell at protocol-set rates.

**Implementation**: Fetch CD orderbook via `GET /cdorderbook`. Render depth ladder same as P2P orderbook. Add CD-specific entry form (`cd order`).

## Not planned

These are explicitly out of scope:

- **Omnibar with autocomplete**: The slash-cmd input works. Adding autocomplete adds complexity without clear value — users learn the 10 commands fast.
- **Persistent sidebar navigation rewrite**: Current m/c/s sidebar works. No need to redesign the navigation hierarchy.
- **Auto-bridge handshakes beyond ETH/SOL**: BCH is already RPC-connected. Other chains (XMR, ARB) are future work and don't need auto-connect yet.
- **Mobile/responsive layout**: Terminal-only app. Responsive is not a concern.
- **Dark/light theme toggle**: Single theme, hardcoded. No plan to add theme switching.

## Priority order

| # | Item | Effort | Impact |
|---|------|--------|--------|
| 1 | Open orders panel | medium | high — you can't see your own orders |
| 2 | Amount validation | small | high — prevents bad orders |
| 3 | Fill tracking | medium | high — know when your order fills |
| 4 | Form keyboard help | small | medium — discoverability |
| 5 | Order book aggregation | medium | medium — reduces noise |
| 6 | Trade history panel | medium | medium — audit trail |
| 7 | Chart timeframes | medium | medium — analysis |
| 8 | Pair switch in order form | small | low — minor QoL |
| 9 | CD orderbook | large | low — CD is niche |
| 10 | Spread line polish | small | low — already mostly there |

## Architecture notes

- All new panels follow the same pattern: model struct in its own file, `Render*` function, toggled by key or command, overlaid via `overlayAt`.
- Fetch in `FetchAll` runs parallel RPC calls. Add new endpoints to the parallel fan-out.
- Form overlays (swap modal, order entry) take priority in `Update()` — key events don't fall through to the main view.
- Orderbook data is already in `AllPairData.Books[pair]`. No new data structures needed for aggregation — just a render-time transform.
