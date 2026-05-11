# Fuego I2P Integration: Implementation Plan

## Current State

### What Exists
- `docs/I2P_SETUP.md` — Documentation with **inaccurate** CLI flags and port numbers
- `src/FuegoI2P/` — Header + stub implementation of `FuegoI2PManager` (SOCKS5 connect, SAM bridge stubs)
- `src/FuegoTor/` — Header + stub implementation of `FuegoTorManager` (SOCKS5 connect, hidden service stubs)
- Both have CMakeLists.txt that build as static libraries
- `src/P2p/` — Standard CryptoNote P2P layer with IPv4-only `NetworkAddress { uint32_t ip, uint32_t port }`

### What's Missing (Critical Gaps)
1. **No SOCKS5 transport in P2P layer** — `NodeServer::try_to_connect_and_handshake_with_new_peer` uses raw `TcpConnector` directly
2. **No network zone separation** — Single peerlist for all connections; I2P/Tor/clearnet peers would cross-contaminate
3. **No address type support** — `NetworkAddress` can't hold `.i2p` or `.onion` hostnames
4. **No command filtering** — Privacy network connections can leak clearnet IPs via peer exchange
5. **No SAM/hidden service inbound** — I2P needs SAM bridge for inbound; Tor needs hidden service for inbound
6. **Docs reference wrong ports** — i2pd SOCKS default is `4447` (not `9150` which is Tor Browser)

---

## Reference: Monero's Architecture (Proven Model)

Monero's approach (`monero-project/monero`) is the gold standard for privacy network integration in CryptoNote:

### Key Design Decisions
1. **Network zones** — `std::map<zone, network_zone>` with separate peerlists, servers, and connection functions per zone (public/tor/i2p)
2. **Type-erased `network_address`** — Wraps `ipv4_network_address`, `tor_address`, `i2p_address` via shared_ptr/interface
3. **Zone-segregated peerlists** — Peers from Tor handshakes go ONLY into zone::tor's peerlist; never leaked to clearnet
4. **Command filtering** — Only HANDSHAKE, TIMED_SYNC, and NOTIFY_NEW_TRANSACTIONS allowed over anonymity zones
5. **SOCKS5 connector** — `net::socks::connector` for outbound; domain-type CONNECT for `.onion`/`.b32.i2p` addresses
6. **Anonymous inbound** — `--anonymous-inbound <addr>,<bind:port>[,max_conns]` for receiving via hidden service/SAM
7. **`--tx-proxy`** flag — Routes outbound transactions through proxy per zone
8. **DNS leak prevention** — Disables seed node DNS resolution when proxy is active and DNS could leak

---

## Implementation Plan

### Phase 1: Address Types & Network Zone Foundation

**Goal**: Replace the IPv4-only `NetworkAddress` with a type-erased address system that supports I2P, Tor, and IPv4.

| File | Change |
|------|--------|
| `src/P2p/P2pProtocolTypes.h` | Add `enum class AddressType { IPv4, I2P, Tor }`; add `i2p_address` struct (b32 host + port, `get_zone()`, `is_loopback()` = false); add `tor_address` struct (onion v3 host + port, similar); add `network_address` type-erased wrapper (like Monero's) |
| `src/P2p/PeerListManager.h` | Zone-aware peerlist — each zone has its own white/gray/anchor lists |
| `src/P2p/NetNodeCommon.h` | Add `epee::net_utils::zone` enum and zone-related types |

**Design principle**: Follow Monero's pattern where `network_address` is a type-erased wrapper. `i2p_address.host_` is a 61-char buffer for `.b32.i2p` destinations. I2P port is always `1` (SAM streams don't use ports).

### Phase 2: SOCKS5 Transport & Proxy Connector

**Goal**: Add SOCKS5 CONNECT capability to route P2P connections through I2P/Tor proxies.

| File | Change |
|------|--------|
| `src/net/Socks5Client.h` (new) | `Socks5Client` class: SOCKS5 handshake (version 5, no-auth or user/pass, CONNECT with domain/IPv4/IPv6 type) |
| `src/net/Socks5Client.cpp` (new) | Implementation: `connect()` proxies through SOCKS5 to target host:port; supports domain-type for `.i2p`/`.onion` hosts |
| `src/net/Socks5Connector.h` (new) | `Socks5Connector` wraps System::TcpConnector + Socks5Client into a `connect(host, port)` function matching `TcpConnector` interface |
| `src/P2p/NetNodeConfig.h/.cpp` | Add CLI args: `--p2p-use-i2p`, `--i2p-socks-host`, `--i2p-socks-port`, `--p2p-use-tor`, `--tor-socks-host`, `--tor-socks-port`, `--anonymous-inbound`, `--tx-proxy`, `--privacy-network-priority` |
| `src/net/CMakeLists.txt` (new) | Build the net module |

**Key detail**: SOCKS5 uses domain-type (0x03) CONNECT for I2P/Tor addresses. DNS resolution happens at the proxy, not at the node — preventing DNS leaks.

### Phase 3: Network Zone Separation

**Goal**: Separate peerlists, connections, and command handling per network zone (the critical privacy feature).

| File | Change |
|------|--------|
| `src/P2p/NetNode.h` | Add `struct network_zone` containing: `peerlist_manager`, `connect_func*`, `m_proxy_address`, `m_our_address`, `m_seed_nodes`, `m_current_number_of_out_peers/in_peers`, `m_net_server` |
| `src/P2p/NetNode.h` | Add `std::map<zone, network_zone> m_network_zones` |
| `src/P2p/NetNode.cpp` | `init()`: Create 3 zones (public, i2p, tor); load persisted peers into correct zone based on address type |
| `src/P2p/NetNode.cpp` | `start_socks()`: Route outbound connections through `Socks5Connector` for i2p/tor zones; direct TCP for public zone |
| `src/P2p/NetNode.cpp` | `handle_remote_peerlist()`: Add received peers ONLY to the zone matching the connection's zone (Tor peers → tor zone, never to public) |

**Privacy guarantee**: A peer discovered over an I2P connection is stored ONLY in the I2P zone's peerlist. The public zone never sees it, and vice versa. This prevents clearnet IP correlation attacks.

### Phase 4: Command Filtering for Anonymity Zones

**Goal**: Prevent P2P protocol leaks on privacy networks.

| File | Change |
|------|--------|
| `src/P2p/NetNode.cpp` | Add `is_filtered_command(zone, command)` — on i2p/tor zones, only allow: `COMMAND_HANDSHAKE`, `COMMAND_TIMED_SYNC`, `NOTIFY_NEW_TRANSACTIONS`, `NOTIFY_NEW_BLOCK`. Filter out: `COMMAND_REQUEST_STAT_INFO`, `COMMAND_REQUEST_NETWORK_STATE`, etc. |
| `src/P2p/NetNode.cpp` | In `handleCommand()`, check zone before processing |

**Rationale**: Monero filters all "debug" and "network state" commands on anonymity zones because they could reveal clearnet peer IPs in their responses.

### Phase 5: Inbound Connections (SAM Bridge & Hidden Service)

**Goal**: Allow receiving I2P/Tor connections, not just making outgoing ones.

| File | Change |
|------|--------|
| `src/FuegoI2P/src/Fuegoi2p.cpp` | Implement SAM v3.1 bridge integration: `SESSION CREATE STYLE=STREAM`, `NAMING LOOKUP`, forward incoming I2P streams to local TCP port |
| `src/FuegoI2P/include/Fuegoi2p.h` | Add `start SAM session()` method, `createI2PServer(destination, localPort)` |
| `src/FuegoTor/src/Fuegotor.cpp` | Implement Tor hidden service: use control port to `ADD_ONION` with ephemeral key, map to local TCP port |
| `src/FuegoTor/include/Fuegotor.h` (exists as `TorIntegration.h`) | Add `createHiddenService()` method |
| `src/P2p/NetNode.cpp` | On init: if `--anonymous-inbound` is set, start SAM/Tor listener on loopback; register our `.i2p`/`.onion` address as `m_our_address` for that zone |

**I2P inbound flow**: 
1. i2pd runs with SAM enabled (`samport = 7656`)
2. Fuego creates SAM session → gets a `.b32.i2p` destination
3. Fuego listens on `127.0.0.1:localport` for forwarded connections
4. Incoming I2P connections arrive at i2pd, which forwards to Fuego's local port
5. `m_our_address` for the I2P zone is set to the `.b32.i2p` destination

### Phase 6: Privacy Hardening

| Feature | Implementation |
|---------|---------------|
| UPnP disable on privacy zones | In `NetNode::init()`, skip `addPortMapping()` when any privacy zone is active |
| DNS leak prevention | When I2P/Tor proxy is configured, disable DNS-based seed node resolution (`m_enable_dns_seed_nodes = false`) |
| Clearnet isolation mode | Add `--p2p-restrict-to-privacy-net` flag: when set, the public zone TCP listener is disabled; node ONLY communicates via I2P/Tor |
| Bind warning | If `--p2p-use-i2p` is active and `--p2p-bind-ip` is `0.0.0.0`, log a WARNING about potential IP leak |
| TX relay isolation | Transactions relayed on I2P zone only go to I2P peers; never cross zones |
| `default_remote` | For outbound I2P/Tor connections, use `unknown` address (not our real IP) as the sender — matches Monero pattern |

### Phase 7: Fix Documentation

| File | Change |
|------|--------|
| `docs/I2P_SETUP.md` | Rewrite: fix i2pd SOCKS port (4447 not 9150), add SAM bridge config for inbound, add clearnet isolation mode, add privacy warnings, add verification checklist, fix CLI flags to match actual implementation |

---

## Correct I2P Port Reference

| Service | Default Port |
|---------|-------------|
| i2pd SOCKS5 proxy | **4447** |
| i2pd HTTP proxy | 4444 |
| i2pd SAM bridge | 7656 |
| i2pd BOB bridge | 2827 |
| i2pd I2CP | 7654 |
| i2pd Web console | 7070 |
| Tor SOCKS5 (system) | 9050 |
| Tor SOCKS5 (Browser) | 9150 |
| Tor Control | 9051 |

The current doc incorrectly uses 9150 (Tor Browser's SOCKS port) for i2pd.

---

## Implementation Priority

1. **Phase 1 + 2** (Address types + SOCKS5) — Foundation; everything else depends on this
2. **Phase 3** (Zone separation) — Critical for privacy; must ship before any privacy users use I2P
3. **Phase 4** (Command filtering) — Must ship with Phase 3; without it, clearnet IPs leak
4. **Phase 6** (Privacy hardening) — Should ship with Phases 3-4
5. **Phase 5** (Inbound SAM/HS) — Important for being a reachable node but not blocking for outbound
6. **Phase 7** (Docs) — Must be updated before any release

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Cross-zone peerlist leak | **CRITICAL** — deanon privacy users | Zone-segregated peerlists (Phase 3) |
| DNS leak on seed resolution | **HIGH** — reveals node IP | Disable DNS seeds when proxy active (Phase 6) |
| UPnP exposes real IP | **HIGH** — deanon | Skip UPnP on privacy zones (Phase 6) |
| Wrong SOCKS port in docs | **MEDIUM** — connection failure | Fix docs, correct default to 4447 |
| Peer exchange leaks IPs | **HIGH** — deanon via TIMED_SYNC | Command filtering (Phase 4) |
| No inbound I2P connections | **MEDIUM** — reduces network | SAM bridge integration (Phase 5) |