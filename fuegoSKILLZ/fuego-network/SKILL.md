---
name: fuego-network
description: "Fuego domain expert for P2P networking: peer connections, network protocol, consensus, node communication, and atomic swap gossip."
risk: low
source: user-provided
---

# Fuego Network Expert

Domain expert for Fuego P2P networking: protocols, peers, consensus, and gossip.

## Scope

- **P2P Protocol**: Network commands and message types
- **Peer Connections**: Node discovery and management
- **Consensus**: Block and transaction validation
- **Gossip**: Atomic swap offer propagation

## Trigger Set

**Should trigger on:**
- "p2p", "peer", "network", "connections"
- "protocol", "command", "message"
- "consensus", "block sync", "syncing"
- "gossip", "broadcast", "propagation"
- "handshake", "node"

**Should NOT trigger on:**
- Non-Fuego network questions
- Generic networking unrelated to Fuego

## P2P Protocol Commands

**Source:** `src/P2p/P2pProtocolDefinitions.h`

| ID | Command | Purpose |
|----|---------|---------|
| 1001 | COMMAND_HANDSHAKE | Node handshake + peer exchange |
| 1002 | COMMAND_TIMED_SYNC | Time sync + block height |
| 1003 | COMMAND_PING | Connection alive check |
| 1004 | COMMAND_REQUEST_STAT_INFO | Debug: node statistics |
| 1005 | COMMAND_REQUEST_NETWORK_STATE | Debug: peer network state |
| 1006 | COMMAND_REQUEST_PEER_ID | Request peer ID |
| 1013 | COMMAND_SWAP_OFFER | Atomic swap offer gossip |
| 1014 | COMMAND_SWAP_CANCEL | Cancel swap offer |
| 1015 | COMMAND_SWAP_TRADE | Completed swap broadcast |

## Network Configuration

**Source:** `src/P2p/P2pProtocolDefinitions.h`

```cpp
struct network_config {
    uint32_t connections_count;        // Target connections
    uint32_t handshake_interval;       // Seconds between handshakes
    uint32_t packet_max_size;         // Max packet size
    uint32_t config_id;               // Config version
};
```

**Default:**
- Target connections: 8
- Handshake interval: 60 seconds
- Max packet size: 20 MB
- Max incoming connections: 250
- Max inbound per IP: 8

## Dandelion++ (v10+)

**Source:** `src/CryptoNoteConfig.h`

Transaction relay protocol for enhanced privacy:

| Parameter | Value | Purpose |
|-----------|-------|---------|
| DANDELION_STEM_MAX_HOPS | 10 | Max stem hops for tx |
| DANDELION_STEM_STAY_PCT | 90 | Stay probability per hop |
| DANDELION_SWAP_STEM_MAX_HOPS | 5 | Max stem hops for swap offers |
| DANDELION_SWAP_STEM_STAY_PCT | 80 | Stay probability for swaps |
| DANDELION_EMBARGO_SECONDS | 30 | Stem black-hole guard |
| DANDELION_EPOCH_SECONDS | 90 | Rotate stem successor interval |

**Flow:**
1. Transaction enters stem phase (sequential forwarding)
2. At each hop: coin flip decides stay (continue stem) or fluff (broadcast)
3. After MAX_HOPS or fluff: broadcast to all peers
4. EMBARGO: if stem tx not seen in core within 30s, promote to fluff

## Peer Discovery

1. **Seed Nodes**: Bootstrap from known seed nodes
2. **Gossip**: Exchange peer lists on handshake
3. **Connection**: Accept inbound if under max

## Block Synchronization

1. **Handshake**: Exchange chain height
2. **Sync Request**: Request missing blocks
3. **Chain Validation**: Verify block difficulty/timestamps
4. **State Update**: Update local chain state

## Usage

```python
from references import NetworkExpert

expert = NetworkExpert(source_dir="/Users/aejt/fuego")

# Get protocol commands
commands = expert.get_protocol_commands()

# Analyze peer health
health = expert.analyze_peer_health(peer_count=6, target=8)

# Get network config
config = expert.get_network_config()

# Analyze sync status
sync_status = expert.analyze_sync_status(local_height=1000000, remote_height=1000100)
```

## Key Files

| File | Purpose |
|------|---------|
| `src/P2p/P2pProtocolDefinitions.h` | Commands, constants |
| `src/P2p/P2pConnections.h` | Connection management |
| `src/P2p/P2pNode.cpp` | P2P node implementation |
| `src/P2p/P2pContext.h` | Connection context |

## Utilities

- Use `fuego-rag` for semantic code search
- Use `fuego-codebase-mapper` for file/function search
