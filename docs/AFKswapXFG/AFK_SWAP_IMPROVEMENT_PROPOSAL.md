# Fuego AFK Swap Improvement Proposal v2

## 1. Summary

SwapXFG's current "AFK" model lets makers publish soft orders (off-chain intents) and auto-lock XFG when a taker arrives. But the maker daemon must stay online during the entire trade, `proofOfFunds` validation is unimplemented, partial fills are unsupported, and pricing is manual.

This proposal redesigns the AFK experience in three phases, turning SwapDaemon into a proper always-on market-making service with griefing protection, partial fills, auto-pricing, and crash recovery.

## 2. Current State

### What Works
- **Soft orders** (`isSoftOrder` flag in `SwapOfferMsg`) — maker publishes intent without on-chain lock
- **Auto-lock on taker arrival** (`SwapDaemon::handleSwapRequest`) — daemon creates AFK lock when taker sends `SWAP_REQUEST`
- **AFK state machine** (states 100-103: `AFK_OFFER_LOCKED` -> `AFK_OFFER_ACCEPTED` -> `AFK_CLAIMED` / `AFK_REFUNDED`)
- **PriceOracle** with composite pricing, TWAP, and external source support
- **SwapDatabase** with persistent state machine storage

### What's Broken
1. **`proofOfFunds` is a TODO** — `handleSwapRequest` accepts the parameter but never validates it. Any taker can trigger a lock, griefing the maker.
2. **No partial fills** — locks `targetOffer.xfgAmount` regardless of the `amount` parameter. A maker offering 1000 XFG must fill it all-or-nothing.
3. **No auto-pricing** — soft order prices are static. PriceOracle exists but isn't wired to soft order management.
4. **Daemon is not a service** — requires an active terminal session. No crash recovery for in-flight swaps.

## 3. Design

### Phase 1: Griefing Protection + Partial Fills

#### 3.1 Reserve Proof Handshake

Current flow:
```
Taker -> SWAP_REQUEST(offerId, amount, takerPubKey, proofOfFunds) -> Maker
Maker -> createAfkLock() immediately (proofOfFunds ignored)
```

New flow:
```
1. Taker generates reserve proof from their counterparty wallet
2. Taker -> SWAP_REQUEST(offerId, amount, takerPubKey, reserveProof) -> Maker
3. Maker validates reserveProof via IChainClient::verifyReserveProof()
   - Proof must cover >= required ctrAmount for the requested XFG amount
   - Proof must be recent (within last N blocks, chain-specific)
4. Valid   -> createAfkLock(amount, ...) with 15-min timeout
5. Invalid -> reject with PROOF_INVALID error code
```

**Implementation:**

- Add `verifyReserveProof(proof, minAmount) -> bool` to `IChainClient` interface
- Implement per-chain in `Solana/SolClient`, `Ethereum/EthClient`, `Monero/XmrClient`, `BitcoinCash/BchClient`
- Wire validation into `SwapDaemon::handleSwapRequest()` before the `createAfkLock` call
- Reduce default AFK lock timeout from 1 hour to 15 minutes (taker is proven and online)

**Files:** `IChainClient.h`, `Solana/SolClient.cpp`, `Ethereum/EthClient.cpp`, `Monero/XmrClient.cpp`, `BitcoinCash/BchRpcClient.cpp`, `SwapDaemon.cpp`

#### 3.2 Partial Fills

Current: `m_rpc.createAfkLock(targetOffer.xfgAmount, ...)` ignores the `amount` parameter.

New:
- Lock `min(amount, offer.xfgAmount - offer.filledAmount)` instead of full `xfgAmount`
- Add `filledAmount` field to `SwapOfferMsg` in `SwapOfferRelay.h`
- After successful lock, increment `filledAmount` on the soft order
- When `filledAmount >= xfgAmount`, remove offer from relay
- When `filledAmount < xfgAmount`, broadcast updated available amount via P2P
- `swapxfg` TUI shows "500/1000 XFG remaining" on partially-filled offers

**Files:** `SwapOfferRelay.h/cpp`, `SwapDaemon.cpp`, `SwapP2P.cpp`, `swapxfg/app/tui.go`

### Phase 2: Auto-Pricing + Multi-Offer Management

#### 3.3 OfferManager

New class that owns and manages a portfolio of soft orders:

```
OfferManager
  ├── loadConfig(path)           // pairs, amounts, slippage bands
  ├── tick()                     // called every ~30s by daemon timer
  │   ├── for each managed offer:
  │   │   ├── query PriceOracle::getCompositePrice(pair)
  │   │   ├── if |currentRate - compositeRate| > allowedSlippagePct:
  │   │   │   ├── cancel old soft order
  │   │   │   └── submit new soft order at compositeRate
  │   │   └── if offer expired (ttlBlocks elapsed):
  │   │       └── resubmit with fresh timestamp
  └── getStatus()                // for monitoring
```

- Uses `allowedSlippagePct` already present in `SwapOfferMsg`
- Repricing is free (soft orders are off-chain P2P messages, no tx fees)
- Config file format (JSON):
  ```json
  {
    "offers": [
      {"pair": "SOL", "side": "sell", "xfgAmount": 100000000000, "slippagePct": 5},
      {"pair": "ETH", "side": "sell", "xfgAmount": 500000000000, "slippagePct": 3}
    ]
  }
  ```

**Files:** New `OfferManager.h/cpp`, `SwapDaemon.cpp` (timer integration), `PriceOracle.h` (existing)

### Phase 3: Daemon-as-Service + Crash Recovery

#### 3.4 Background Service Mode

- Add `--service` flag to `SwapDaemon/main.cpp` for headless operation (no stdin, log to file)
- Provide systemd unit file (`contrib/swapd.service`) and launchd plist
- Local status endpoint via Unix socket exposing:
  - Active soft orders (pair, amount, price, fill status)
  - In-flight swaps (state, counterparty, timeout remaining)
  - Wallet balance
  - PriceOracle rates
- `swapxfg` TUI connects to this socket for read-only monitoring (new "Daemon Status" view)

**Files:** `main.cpp`, new `DaemonStatus.h/cpp`, `contrib/swapd.service`, `swapxfg/app/tui.go`

#### 3.5 Crash Recovery

On daemon restart:
1. `SwapDatabase::listSwaps()` reloads all persisted state machines
2. For each swap in an active state (`AFK_OFFER_LOCKED` or `AFK_OFFER_ACCEPTED`):
   - Query current block height
   - If `xfgTimeoutHeight - currentHeight < 2` (safety margin): trigger refund
   - If timeout has passed: execute refund transaction
   - Otherwise: resume normal state machine polling
3. For soft orders managed by OfferManager: resubmit to P2P relay
4. Log recovery summary

State machines already persist via `SwapDatabase` (`loadSwap`/`saveSwap`). The missing piece is the restart-scan logic in `SwapDaemon::init()`.

**Files:** `SwapDaemon.cpp` (init recovery scan), `SwapDatabase.cpp` (existing)

## 4. Fee Structure (Unchanged)

| Parameter | Value |
|-----------|-------|
| Swap fee | 1% (100 bps) |
| CD yield pool share | 80% |
| Treasury share | 20% |

Partial fills: fee calculated per-fill, not per-offer.

## 5. State Machine (Unchanged)

```
AFK_OFFER_LOCKED (100)
  -> AFK_OFFER_ACCEPTED (101)  [taker locked counterparty]
  -> AFK_REFUNDED (103)        [timeout]

AFK_OFFER_ACCEPTED (101)
  -> AFK_CLAIMED (102)         [both sides claimed]
  -> AFK_REFUNDED (103)        [timeout]
```

No new states needed. Partial fills create separate state machine instances per fill.

## 6. Security

| Threat | Mitigation |
|--------|-----------|
| Taker griefing (lock funds, abandon) | Reserve proof required before lock; 15-min timeout |
| Price manipulation via stale oracle | OfferManager uses composite pricing (TWAP + external sources) |
| Daemon crash mid-swap | State persisted to SwapDatabase; restart recovery scan |
| Reserve proof replay | Require proof freshness (recent block height) |
| Partial fill spam | Minimum fill amount enforced (e.g., 1 XFG) |

## 7. Implementation Order

| Phase | Scope | Key Files |
|-------|-------|-----------|
| 1 | Reserve proof + partial fills | `IChainClient.h`, chain clients, `SwapDaemon.cpp`, `SwapOfferRelay.h/cpp` |
| 2 | OfferManager + auto-pricing | New `OfferManager.h/cpp`, `SwapDaemon.cpp`, `PriceOracle.h` |
| 3 | Service mode + crash recovery | `main.cpp`, new `DaemonStatus.h/cpp`, `swapxfg/app/tui.go` |

Each phase is independently shippable. Phase 2 depends on Phase 1 (partial fills inform OfferManager's fill tracking). Phase 3 is independent.

## 8. Future Work (Not In Scope)

- **AMM pool liquidity layer** — passive LP deposits into pools, pool-backed soft orders. Requires solving cross-chain settlement for pool-backed trades.
- **Cross-chain alias ZK mapping** — `@alice` -> counterparty addresses via ZK proofs.
- **Taker reputation system** — track success/abandon ratios per pubkey. Could supplement or replace reserve proofs for repeat takers.
