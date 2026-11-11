# libp2p Swap Network Rewrite — Design Plan & Developer Implementation Guide

> **Status:** Plan / guide only — not implemented  
> **Related:** `docs/plans/swap_spv_master_roadmap.md` Phase 6  
> **Audience:** Implementers of SwapDaemon networking + SPV peer discovery  
> **Date:** 2026-08-09  

---

## 1. Why rewrite?

### Today (baseline)

| Surface | Implementation | Limitations |
|---------|----------------|-------------|
| Swap peer protocol | `SwapP2P` — raw TCP, length-framed messages, default bind `127.0.0.1` | Peer must know `host:port` out-of-band; no discovery |
| Auth | Ed25519 `PeerMessage` signatures + optional `expectedPeerSwapPubKey` | Good crypto; weak topology |
| Transport privacy | Optional SOCKS5 (Tor) on outbound only | No NAT traversal; inbound still needs port or Tor hidden service |
| Offers / orderbook | `SwapOfferRelay` over **Fuego Levin P2P** | Separate from swap-session TCP |
| Electrum SPV servers | Config lists (`bch_spv_server_*`, …) | Static; ops burden; eclipse risk if list is small |
| Neutrino filters | Local client | No filter relay network |

### Goals of libp2p rewrite

1. **Discover** swap peers and (optionally) Electrum/filter providers without hardcoding endpoints.  
2. **Multiplex** authenticated swap sessions over one long-lived connection (less connection churn).  
3. **Keep** existing protocol semantics (`SwapPeerProtocol` message types, signatures, expected peer key).  
4. **Do not** replace Fuego consensus Levin P2P in one jump — stage coexistence.  
5. **Optional** relay of BIP-158 compact filters later (depends on Neutrino maturity).

### Non-goals (v1)

- Replacing `fuegod` peer protocol  
- Full IPFS / Filecoin stack  
- Browser WASM libp2p in the dashboard (future)  
- Guaranteed global DHT reliability without bootstrap nodes  

---

## 2. Architecture overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        SwapDaemon                                 │
│  ┌──────────────┐   ┌──────────────────┐   ┌─────────────────┐  │
│  │ State machine│   │ PeerProtocol     │   │ OfferManager    │  │
│  │ + DB         │   │ (signed JSON)    │   │ + SwapOfferRelay│  │
│  └──────┬───────┘   └────────┬─────────┘   └────────┬────────┘  │
│         │                    │                      │           │
│         │           ┌────────▼─────────┐            │           │
│         │           │ ISwapTransport   │◄───────────┘ optional  │
│         │           │  (abstraction)   │   dual-path            │
│         │           └────────┬─────────┘                        │
│         │         ┌──────────┴──────────┐                       │
│         │         ▼                     ▼                       │
│         │  TcpSwapTransport      Libp2pSwapTransport            │
│         │  (today SwapP2P)       (new)                          │
│         │                                                       │
│  ┌──────▼────────────────────────────────────────────────────┐  │
│  │ Libp2pHost (one process-wide node)                        │  │
│  │  - Identity (Ed25519 peer id)                             │  │
│  │  - Transports: TCP (+ QUIC later), Noise, Yamux           │  │
│  │  - Discovery: mDNS (LAN), Kademlia DHT, bootstrap list    │  │
│  │  - Protocols:                                             │  │
│  │      /xfg/swap/1.0.0          swap sessions               │  │
│  │      /xfg/electrum-hint/1.0.0 Electrum server gossip      │  │
│  │      /xfg/cbf/1.0.0           compact filter hints (later)│  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

**Key principle:** Introduce `ISwapTransport` so `SwapDaemon::deliverPeerMessage` / inbound callback stop depending on `host:port` strings alone. Peers are addressed by **PeerId** (libp2p) and/or multiaddr, with optional multiaddr cache next to `peerEndpoint`.

---

## 3. Protocol mapping (preserve security)

### 3.1 Wire payload (do not reinvent)

Keep **`serializePeerMessage` / `deserializePeerMessage`** and **`signPeerMessage` / `verifyPeerMessage`**.

libp2p stream body (v1):

```
[u32 BE length][utf8 PeerMessage JSON including signature]
```

Same as today’s “payload” inside `SwapMsgType::PEER_PROTOCOL`, but carried on a **named protocol stream** instead of one-shot TCP connect.

### 3.2 Identity dual-key model

| Key | Role |
|-----|------|
| **libp2p PeerId key** | Network identity, Noise handshake, DHT |
| **swap Ed25519 key** (`ourSwapPubKey`) | Application-level message auth (already exist) |

**Binding (mandatory):** After Noise handshake, first application message on `/xfg/swap/1.0.0` is still `KEY_EXCHANGE` signed by swap key. Receiver checks:

1. Noise peer is stable for the session.  
2. `expectedPeerSwapPubKey` matches (already implemented).  
3. Optional later: signed “libp2p PeerId ↔ swapPubKey” attestation to prevent key-swap after connect.

Do **not** replace swap-key signatures with “Noise is enough” — application auth must survive multi-hop relays.

### 3.3 Addressing migration

| Field | Today | libp2p era |
|-------|--------|------------|
| `peerEndpoint` | `"host:port"` | Keep for TCP fallback; prefer multiaddr |
| New `peerMultiaddrs` | — | `std::vector<std::string>` e.g. `/ip4/…/tcp/…/p2p/12D3…` |
| New `peerId` | — | base58 PeerId string |

CLI/RPC: accept either `peer` host:port **or** `peer_multiaddr` / `peer_id`.

---

## 4. Library / language choice (decision)

| Option | Pros | Cons | Recommendation |
|--------|------|------|----------------|
| **A. cpp-libp2p** (Parity / Boost-based) | Native C++, same process as SwapDaemon | Heavy Boost; API churn; build complexity | **Preferred long-term** if CMake/Boost budget OK |
| **B. rust-libp2p** via C ABI crate | Mature swarm; Noise/Yamux/Kad solid | FFI boundary; two toolchains | Good if team owns Rust (adaptor crate already) |
| **C. Go libp2p** sidecar | Excellent DX | Extra process; IPC trust | Avoid for core daemon |
| **D. Minimal DHT DIY** | No dep | Security footguns | Reject |

**Recommended path for Fuego:**

1. **Phase L0–L1:** Define `ISwapTransport` + keep TCP; optional rust-libp2p **sidecar later** only if cpp-libp2p fails.  
2. **Phase L2:** Integrate **cpp-libp2p** (or rust-libp2p staticlib) behind `Libp2pSwapTransport`.  
3. Document Boost version alignment with Fuego’s Boost 1.86+ requirement.

### Suggested CMake feature flag

```cmake
option(ENABLE_LIBP2P "SwapDaemon libp2p transport" OFF)
# When ON: fetch/find libp2p, define XFG_ENABLE_LIBP2P, link Libp2pSwapTransport.cpp
```

Default **OFF** until green CI on macOS + Ubuntu.

---

## 5. Implementation phases (developer checklist)

### Phase L0 — Transport abstraction (1–3 days)

**Files to create/modify:**

| Path | Work |
|------|------|
| `src/SwapDaemon/Net/ISwapTransport.h` | Interface |
| `src/SwapDaemon/Net/TcpSwapTransport.{h,cpp}` | Move/wrap current `SwapP2P` |
| `SwapDaemon.cpp` | Depend on `ISwapTransport*`, not `SwapP2P` concrete |
| Tests | Existing P2P tests still pass via TCP adapter |

**Interface sketch:**

```cpp
class ISwapTransport {
public:
  virtual ~ISwapTransport() = default;
  virtual bool start() = 0;
  virtual void stop() = 0;
  // peerRef: "host:port" OR multiaddr OR peer_id (implementation-defined)
  virtual bool sendTo(const std::string& peerRef, const PeerMessage& msg) = 0;
  virtual void setInboundHandler(std::function<void(const PeerMessage&)> cb) = 0;
  virtual void setSocks5Proxy(const std::string& proxy) = 0;
  virtual std::string localAddressHint() const = 0; // for logs / offer ads
};
```

**Exit criteria:** Daemon binary behavior unchanged with TCP transport.

---

### Phase L1 — Peer addressing & config (2–4 days)

| Item | Detail |
|------|--------|
| Config | `swap_libp2p_enabled`, `swap_libp2p_listen` multiaddrs, `swap_libp2p_bootstrap[]`, `swap_libp2p_enable_mdns` |
| `SwapParams` | `peerId`, `peerMultiaddrs`, keep `peerEndpoint` |
| Offer gossip | Optional field on soft offers: maker’s multiaddrs (signed with maker key already on offer) |
| RPC | `initiate_swap` accepts `peer_multiaddr` + `expected_peer_pubkey` |

**Exit criteria:** Offers can advertise multiaddrs; initiate can store them without libp2p stack yet (ignored until L2).

---

### Phase L2 — libp2p host + `/xfg/swap/1.0.0` (1–2 weeks)

| Step | Work |
|------|------|
| 2.1 | Build/link libp2p (cpp or rust staticlib) under `ENABLE_LIBP2P` |
| 2.2 | `Libp2pHost` singleton: generate/load node key from `dataDir/libp2p.key` (0600) |
| 2.3 | Listen TCP multiaddrs; Noise XX; Yamux |
| 2.4 | Protocol handler: open stream, length-prefix, PeerMessage JSON |
| 2.5 | `Libp2pSwapTransport::sendTo`: resolve peerId → multiaddr (cache or DHT), open stream, write |
| 2.6 | Dual-stack: if libp2p fails and `peerEndpoint` set, fall back to TCP |
| 2.7 | Integration tests: two daemons on loopback with multiaddrs, full KEY_EXCHANGE → ADAPTOR_EXCHANGE |

**Security checklist (L2):**

- [ ] Node key never in swap DB records  
- [ ] `expectedPeerSwapPubKey` still enforced  
- [ ] Rate-limit inbound streams per PeerId  
- [ ] Max message size (reuse 1MB class limits)  
- [ ] No cleartext secrets on streams (already true for signed messages; secrets only SECRET_REVEAL after auth)  

**Exit criteria:** Two SwapDaemons complete adaptor handshake with **empty host:port**, multiaddr-only.

---

### Phase L3 — Discovery (1–2 weeks)

| Mechanism | Use | Priority |
|-----------|-----|----------|
| **Bootstrap peers** | Config multiaddrs (Fuego-operated + community) | P0 |
| **mDNS** | LAN auto-find for dev/desktop | P0 |
| **Kademlia DHT** | Provide/find `xfg-swap` peer records | P1 |
| **Rendezvous / bootstrap HTTP** | Optional simple list endpoint | P2 |

**Peer record content (signed by swap or node key):**

```json
{
  "peer_id": "12D3KooW...",
  "multiaddrs": ["/ip4/.../tcp/.../p2p/..."],
  "swap_pubkeys": ["hex..."],  
  "pairs": ["BCH","ETH"],
  "ts": 1730000000
}
```

Validate age (e.g. reject > 24h) and optional `expectedPeerSwapPubKey` match when dialing a known counterparty.

**Exit criteria:** Third node finds two others via mDNS or DHT without static peer list (bootstrap only).

---

### Phase L4 — Electrum / SPV discovery (1 week)

**Protocol:** `/xfg/electrum-hint/1.0.0`

Gossip **hints** only (not full headers):

```json
{ "chain": "BCH", "host": "…", "port": 50002, "tls": true, "score": 0.8, "ts": … }
```

Rules:

- Never trust a single hint for confirmations — still require `minServers` agreement in `ElectrumSpvClient`.  
- Prefer hints that match chain checkpoints.  
- Cap gossip rate; score by observed sync success.

**Exit criteria:** Daemon with empty electrum list can populate ≥ minServers from network and complete SPV sync on testnet.

---

### Phase L5 — Compact filter relay (optional, after Neutrino)

**Protocol:** `/xfg/cbf/1.0.0`

- Advertise filter header tips.  
- Serve GCS filters for height ranges.  
- Verify against local header store before use.

**Depends on:** Phase 4 Neutrino production path.

---

### Phase L6 — Offer path unification (optional)

Today offers use Levin (`SwapOfferRelay`). Options:

| Approach | Effort | Note |
|----------|--------|------|
| Keep Levin for offers; libp2p for sessions only | Low | **Recommended first year** |
| Dual-publish offers on libp2p gossipsub | Medium | Better decentralization |
| Drop Levin offers entirely | High | Only after libp2p battle-tested |

---

## 6. Directory layout (target)

```
src/SwapDaemon/Net/
  ISwapTransport.h
  TcpSwapTransport.h / .cpp      # wraps SwapP2P
  Libp2pSwapTransport.h / .cpp   # ENABLE_LIBP2P
  Libp2pHost.h / .cpp
  PeerAddress.h                  # parse host:port | multiaddr | peer id
  protocols/
    SwapProtocol.h / .cpp        # /xfg/swap/1.0.0
    ElectrumHintProtocol.h       # /xfg/electrum-hint/1.0.0
    CbfProtocol.h                # later

src/SwapDaemon/tests/
  test_tcp_transport.cpp
  test_libp2p_swap_roundtrip.cpp # if ENABLE_LIBP2P
  test_peer_address_parse.cpp
```

Keep `SwapP2P.*` until TcpSwapTransport is a thin wrapper; then deprecate.

---

## 7. Config sketch

```json
{
  "swap_libp2p_enabled": true,
  "swap_libp2p_listen": ["/ip4/0.0.0.0/tcp/18903"],
  "swap_libp2p_announce": ["/ip4/203.0.113.5/tcp/18903"],
  "swap_libp2p_bootstrap": [
    "/dns4/bootstrap1.usexfg.org/tcp/18903/p2p/12D3KooW..."
  ],
  "swap_libp2p_enable_mdns": true,
  "swap_libp2p_enable_dht": true,
  "swap_libp2p_electrum_gossip": true,
  "swap_p2p_port": 18901,
  "swap_p2p_bind": "127.0.0.1",
  "swap_transport": "dual"
}
```

`swap_transport`: `tcp` | `libp2p` | `dual` (try libp2p then TCP).

---

## 8. Testing strategy

| Level | What |
|-------|------|
| Unit | PeerAddress parse; message length limits; stream framing |
| Integration | Two processes, multiaddr dial, KEY_EXCHANGE + expected key reject |
| Chaos | Kill one peer mid-swap; reconnect with same PeerId; resume from DB state |
| Security | Impersonation (wrong swap key); oversized messages; stream flood |
| SPV | Electrum hints don’t bypass minServers |

Use existing style: `assert` + `int main()` tests in `src/SwapDaemon/tests/`.

---

## 9. Security & privacy notes

1. **Public bind:** libp2p listen on `0.0.0.0` increases attack surface — default dual mode should still prefer loopback TCP for local wallets; public multiaddr only if operator opts in.  
2. **DHT leakage:** Publishing swap intent on DHT can link IP ↔ trading activity. Mitigations: Tor multiaddrs (`/onion3/…` if supported later), delay announcements, no amounts on DHT records.  
3. **expectedPeerSwapPubKey** remains primary anti-griefing control for session hijack.  
4. **Tor:** Today SOCKS5 outbound works; libp2p + Tor needs explicit multiaddr/transport support — treat as L3+.  

---

## 10. Dependencies & sequencing vs other roadmap items

```
L0 ISwapTransport ──────────────────────────── no deps
L1 addressing/config ───────────────────────── L0
L2 libp2p host + swap protocol ─────────────── L1
L3 discovery (mDNS/DHT) ────────────────────── L2
L4 Electrum gossip ─────────────────────────── L2 (SPV clients exist)
L5 CBF relay ───────────────────────────────── L4 + Neutrino production
L6 offer gossip unification ────────────────── L3 (optional)
```

**Does NOT block:** Sia renterd, Helios FFI, new chain clients.  
**Helios** stays separate (`IEvmLightClient`); libp2p does not replace EVM light clients.  
**TLS Electrum** (`useTls=true`) is orthogonal and already partially available — keep for static server lists even after gossip.

---

## 11. Suggested first PR sequence

| PR | Title | Scope |
|----|--------|--------|
| 1 | `net: introduce ISwapTransport + TcpSwapTransport` | No behavior change |
| 2 | `net: peer multiaddr fields on SwapParams + config parse` | Storage only |
| 3 | `feat(libp2p): host + /xfg/swap/1.0.0 behind ENABLE_LIBP2P` | Experimental flag |
| 4 | `feat(libp2p): dual dial + mDNS bootstrap` | Dev UX |
| 5 | `feat(libp2p): electrum-hint gossip` | SPV ops relief |

---

## 12. Open decisions (resolve before L2 code freeze)

1. **cpp-libp2p vs rust-libp2p FFI** — spike both build on CI macOS/Ubuntu (1–2 days).  
2. **Public bootstrap operators** — who runs bootstrap multiaddrs?  
3. **Whether AFK offers must embed multiaddrs** or only interactive swaps.  
4. **DHT namespace string** — propose `/xfg/swap/kad/1.0.0`.  
5. **Browser peers** — out of scope; desktop/daemon only.  

---

## 13. References

- Roadmap Phase 6: `docs/plans/swap_spv_master_roadmap.md`  
- Current TCP: `src/SwapDaemon/SwapP2P.{h,cpp}`  
- App protocol: `src/SwapDaemon/SwapPeerProtocol.{h,cpp}`  
- Offers: `src/CryptoNoteCore/SwapOfferRelay.*`  
- SPV: `src/SwapDaemon/Spv/*`  
- Peer auth: `expectedPeerSwapPubKey` in `SwapTypes.h` / KEY_EXCHANGE handling  
- libp2p specs: https://github.com/libp2p/specs  
- rust-libp2p: https://github.com/libp2p/rust-libp2p  
- cpp-libp2p: https://github.com/libp2p/cpp-libp2p  

---

## 14. One-page “how to start coding tomorrow”

```bash
# 1) Abstraction only (no new deps)
#    - Extract ISwapTransport
#    - TcpSwapTransport wraps SwapP2P
#    - SwapDaemon uses unique_ptr<ISwapTransport>

# 2) Spike ENABLE_LIBP2P=ON on a branch
#    - Add rustc/c++ lib build
#    - Hello-world: two processes open stream /xfg/swap/1.0.0, echo PeerMessage

# 3) Replace echo with real serializePeerMessage + handlePeerMessage callback

# 4) Dual dial: multiaddr first, peerEndpoint fallback

# 5) mDNS for local demos; bootstrap list for CI
```

Do not enable libp2p by default until PR 3+ has soak tests and dual-stack fallback proven.

---

*End of plan. Implementation starts at Phase L0 when scheduled.*
