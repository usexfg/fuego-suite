# Dashboard RPC Proxy — Local-Process Trust Escalation

**Status:** open finding — no fix applied
**Severity:** Medium (local-process threat model only; **not** remotely reachable)
**Component:** `dashboard/main.go`
**Last updated:** 2026-10-04

## Summary

The dashboard's JSON-RPC proxy holds the daemon swap-control token and injects it into
forwarded requests. Combined with an allowlist that does not distinguish read from
state-changing methods, this downgrades a daemon capability from *"possess the token"* to
*"reach loopback"*.

This does **not** weaken the dashboard against remote or browser attackers — those controls
are correct and comprehensive (see *What is already correct*). It is a finding about other
software running on the same machine.

## Mechanism

`dashboard/main.go:515` injects the operator's token unconditionally into every proxied call:

```go
if cfg.token != "" {
    proxyReq.Header.Set("X-Swap-Token", cfg.token)
}
```

The token comes from `-swapd-rpc-token` or `$XFG_SWAPD_RPC_TOKEN` (`main.go:45,52`).

The daemon treats those methods as privileged. `RpcServer.cpp:374` `checkSwapControlAuth()`
is fail-closed — if no token is configured, **all** swap-control access is denied — and its
comment states the intent plainly:

> This prevents unauthenticated placement of swap offers / orders / fee changes on a daemon
> exposed to the network (or via CSRF).

Ten RPCs are registered through the `jsonMethodSwapAuth` wrapper that enforces it:

| Command | Effect |
|---|---|
| `COMMAND_RPC_INITIATE_SWAP` | locks funds |
| `COMMAND_RPC_ACCEPT_SWAP` | locks funds |
| `COMMAND_RPC_PROCESS_SWAP` | progresses a swap |
| `COMMAND_RPC_REFUND_SWAP` | refunds |
| `COMMAND_RPC_SUBMIT_SWAP_OFFER` | places an offer |
| `COMMAND_RPC_REQUEST_SWAP` | requests |
| `COMMAND_RPC_CANCEL_SWAP_OFFER` | cancels offer |
| `COMMAND_RPC_PLACE_ORDER` | places an order |
| `COMMAND_RPC_CANCEL_ORDER` | cancels order |
| `COMMAND_RPC_ADD_SWAP_FEE` | changes swap fees |

Read RPCs (`getbalance`, `list_swaps`, `swap_status`, `list_chains`, `get_reserve_proof`,
`check_timeouts`, …) are registered with the plain `jsonMethod` wrapper and never call
`checkSwapControlAuth`.

### The mismatch

The proxied routes are `/api/wallet` (`main.go:645`) and `/api/swapd-rpc`
(`main.go:649`). `swapdAllowedMethods` (`main.go:405-414`) merges both tiers and the proxy lends the token to
all of them:

| Dashboard method | Daemon-gated? | Dashboard behaviour |
|---|---|---|
| `initiate_swap` | yes | token injected → reachable |
| `accept` | yes | token injected → reachable |
| `refund` | yes | token injected → reachable |
| `list_swaps` | no | token injected (unnecessary) |
| `swap_status` | no | token injected (unnecessary) |
| `get_reserve_proof` | no | token injected (unnecessary) |
| `check_timeouts` | no | token injected (unnecessary) |
| `list_chains` | no | token injected (unnecessary) |

So the dashboard re-grants exactly the capability the daemon's token exists to withhold.

## Impact

Any process on the host can `POST http://127.0.0.1:18918/api/swapd-rpc` with
`{"jsonrpc":"2.0","method":"initiate_swap",...}` and drive swap initiation, acceptance,
processing and refunds — without possessing `XFG_SWAPD_RPC_TOKEN`.

The same applies to `walletProxyHandler`'s write methods (`place_limit_order`,
`cancel_limit_order`, `amm_swap` at `main.go:400-402`), subject to whether walletd's own
listener requires separate credentials.

Note this is a genuine *widening* of reach, not a no-op: the daemons listen on loopback
ports of their own (18902, 18183), and a local process talking to them directly would still
need the token. The dashboard removes that requirement.

Secondary: the rate limiter keys on `RemoteAddr` (`main.go:465`), which is always
`127.0.0.1` for loopback callers. It therefore never discriminates between local processes
and behaves as a global throttle.

## What is already correct

Recorded so a future change does not regress it. All verified in `main.go`:

| Control | Line | Effect |
|---|---|---|
| Binds `127.0.0.1:%d`, no flag to widen | `:691` | Not reachable off-host |
| Host allowlist `127.0.0.1`/`localhost`/`::1` | `:351-353` | Global middleware — DNS rebinding blocked for every route, not just the websocket |
| `Origin` must equal `Host` when present | `:359` | Cross-origin browser requests rejected |
| CSP `connect-src 'self' ws://127.0.0.1:*` | `:336` | No remote egress target |
| POST-only, per-IP limit, 1 MB body cap | `:457-474` | Bounds the proxy |
| Body re-serialized through a struct | `:474-502` | Drops any field the dashboard does not model |
| Internal fetches hardcoded to `127.0.0.1` | `:209-272` | No SSRF surface |
| Zero outbound requests to non-local hosts | — | No telemetry, no CDN, no remote fonts; charting libs vendored |

The dashboard makes **no** external network calls. It is a well-built local operator tool.

## Remediation options

The fix is not purely mechanical — it collides with the dashboard's trading UI, so it is a
product decision.

**Option A — dashboard becomes read-only.** Split each allowlist into read and write sets.
The proxy injects nothing. Reads keep working (the daemon does not gate them). Write methods
return `403` with a pointer to the Valise app, which already holds the vault seed and talks
to walletd on `127.0.0.1:18189` with its own credentials.

*Strongest.* Removes the token from the dashboard entirely, deletes `-swapd-rpc-token` and
`XFG_SWAPD_RPC_TOKEN` handling, and reduces the proxy to pure read fan-out. Costs the
dashboard's order placement and `amm_swap` UI.

**Option B — split by tier, caller presents the token for writes.** As A, but write methods
forward only when the caller supplies a valid `X-Swap-Token` themselves, which the proxy
validates and passes through instead of lending.

*Keeps* the trading UI for anyone willing to supply the token per call. The token must then
reach the browser, which is precisely what the current design avoids — so this only helps if
the browser obtains it out-of-band (prompt, or a local-only exchange).

**Option C — bind writes to a second listener.** Serve reads on `127.0.0.1:18918` and writes
on a separate port or unix socket with its own stricter policy.

*Preserves* the current split but adds surface and still does not help against a local
process, since any local process can reach either listener.

Recommendation: **Option A**, then reintroduce trading deliberately if it proves needed —
via the Valise app, which already has the stronger credential story.

If a local-process adversary is out of scope for this deployment, close this as
accepted-risk with a note in `PRE_PRODUCTION_CHECKLIST.md` rather than leaving it
undocumented. It is still worth stating, because the current code reads as though the token
boundary is enforced when in fact the dashboard is the one component that steps around it.

## Verification

Unverified against a running daemon — this review was static, on `dashboard/main.go` at
suite `e89b3ea1`. To confirm the reachability claim end to end:

1. Start `xfg-swapd` with `-swap-control-token <secret>`.
2. Without the secret: `curl -s -X POST http://127.0.0.1:18902/ -d '{"jsonrpc":"2.0","id":1,"method":"list_swaps"}'` → succeeds (ungated read).
3. Without the secret: same with `"method":"initiate_swap"` → `401 Unauthorized`.
4. Through the dashboard, without the secret:
   `curl -s -X POST http://127.0.0.1:18918/api/swapd-rpc -H 'Content-Type: application/json' -d '{"jsonrpc":"2.0","id":1,"method":"initiate_swap"}'` → **succeeds**, which is the finding.