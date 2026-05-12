# Fuego MCP Server

Zero-dependency MCP server with 53 tools for the Fuego blockchain codebase. Covers 16 domains: chain core, currency, wallets, RPC, transactions, crypto, mining, P2P, atomic swaps, deposits, privacy, contracts, aliases, codebase mapper, explorer, and RAG.

## Usage

### CLI — direct calls, no MCP client needed

```bash
python3 scripts/fuego-mcp/fuego_mcp_server.py get_chain_info
python3 scripts/fuego-mcp/fuego_mcp_server.py get_codebase_stats
python3 scripts/fuego-mcp/fuego_mcp_server.py calculate_emission height=500000
python3 scripts/fuego-mcp/fuego_mcp_server.py search_files query=miner
python3 scripts/fuego-mcp/fuego_mcp_server.py get_file_tree depth=2
```

### MCP mode — for any MCP-compatible client

```bash
python3 scripts/fuego-mcp/fuego_mcp_server.py --mcp
```

Then send JSON-RPC requests over stdin/stdout:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"get_chain_info","arguments":{}}}
```

### MCP config for Claude / opencode

```json
{
  "mcpServers": {
    "fuego-mcp": {
      "command": "python3",
      "args": ["/Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py", "--mcp"]
    }
  }
}
```

## Requirements

- Python 3.9+ (system Python is fine)
- No pip packages needed
- No external dependencies

## All 53 Tools

### Blockchain Core
| Tool | Description |
|------|-------------|
| `get_chain_info` | Chain parameters: network, block time, ports |
| `get_config_constants` | Constants from CryptoNoteConfig.h |
| `get_upgrade_schedule` | Protocol upgrade heights (V2–V11) |
| `get_block_info` | Block structure, header fields, size limits |
| `get_token_supply` | Total supply, emission curve, precision |
| `get_fee_structure` | Fee tiers, priority levels, minimum fee |

### Currency / Monetary
| Tool | Description |
|------|-------------|
| `get_coin_denominations` | XFG atomic units, display format |
| `calculate_emission` | Emission at a given block height |
| `calculate_block_reward` | Block reward at a given height |

### Wallets
| Tool | Description |
|------|-------------|
| `get_wallet_info` | Wallet types (Green, Simple CLI), key types |
| `get_address_info` | Address format, integrated addresses, subaddresses |
| `get_mnemonic_info` | Mnemonic seeds: 14 languages, Electrum-style |
| `get_wallet_rpc_methods` | Key wallet RPC methods |

### RPC / API
| Tool | Description |
|------|-------------|
| `get_daemon_rpc_methods` | Blockchain RPC methods |
| `get_rpc_error_codes` | RPC error code mappings |
| `get_payment_gateway_info` | PaymentGate service details |

### Transactions
| Tool | Description |
|------|-------------|
| `get_tx_types` | All 8 transaction types with descriptions |
| `analyze_tx_structure` | Input, output, signature structure |
| `calculate_tx_fee` | Transaction fee for given size/priority |

### Cryptography
| Tool | Description |
|------|-------------|
| `get_crypto_primitives` | All crypto: signatures, hashes, commitments, ZK |
| `get_signature_schemes` | Ed25519, Schnorr, MLSAG, MuSig2, Adaptor |
| `get_ring_signature_info` | MLSAG, dynamic ring sizes, OSPEAD decoys |
| `get_hash_functions` | Keccak, Blake2b, CryptoNight variants |
| `get_privacy_model` | Ring sigs, stealth addresses, Dandelion++ |
| `get_decoy_info` | Decoy selection, logarithmic age bins |

### Mining
| Tool | Description |
|------|-------------|
| `get_mining_config` | PoW algorithm, CPU/pool mining config |
| `get_mining_info` | Mining algorithm, difficulty algorithm |
| `get_difficulty_info` | DMWDA algorithm, windows, confidence scores |

### P2P Networking
| Tool | Description |
|------|-------------|
| `get_p2p_info` | Levin protocol, messages, peer lists |
| `analyze_peer_health` | Network health score, connection status |
| `get_anonymity_info` | Tor, I2P, Meshtastic integration |

### Atomic Swaps
| Tool | Description |
|------|-------------|
| `get_swap_info` | Full swap protocol, states, cross-chain pairs |
| `calculate_swap_fee` | Swap fee calculation (1% rate) |
| `validate_swap_state` | State machine transition validation |
| `get_swap_state_name` | Human-readable state name from ID |

### Deposits / Burns
| Tool | Description |
|------|-------------|
| `calculate_cd_interest` | CD interest over epoch range |
| `estimate_apy` | APY from epoch fees and locked supply |
| `get_deposit_info` | CD system: COLD, HEAT, staged unlocking |
| `get_lp_pool_info` | v11 AMM: constant product, LP shares |

### Privacy, Contracts, Aliases
| Tool | Description |
|------|-------------|
| `get_contract_info` | Ethereum bridge contracts (Solidity) |
| `get_heat_info` | HEAT token, burn mechanism, STARK relay |
| `get_alias_info` | On-chain alias system |

### Codebase Mapper
| Tool | Description |
|------|-------------|
| `scan_codebase` | Index all files into searchable DB |
| `get_codebase_stats` | File/line/function/language counts |
| `search_files` | Find files by name or path pattern |
| `search_functions` | Find functions by name |
| `get_file_tree` | Hierarchical directory tree |
| `find_files_by_type` | Find files by extension |

### Codebase Explorer
| Tool | Description |
|------|-------------|
| `get_source_domains` | 26+ source domains with paths |
| `analyze_code_section` | Regex search across all source files |

### RAG
| Tool | Description |
|------|-------------|
| `build_rag_index` | Discover docs, read, chunk by function/class |
| `search_codebase` | Keyword search over indexed chunks |
| `generate_context` | Search + build LLM-ready context prompt |

## DB Files

The server creates two SQLite databases in the project root on first use:

- `.fuego_mcp.db` — File/func index (used by mapper tools)
- `.fuego_rag.db` — Chunk index (used by RAG tools)

## Architecture

```
fuego_mcp_server.py
├── Knowledge base (CHAIN_CONFIG, FEE_CONFIG, CD_CONFIG, etc.)
├── Tool definitions (53 entries with JSON Schema input specs)
├── MCP dispatcher (stdin/stdout JSON-RPC 2.0)
├── CLI argument parser (direct tool invocation)
├── Codebase scanner (SQLite-backed file/func index)
└── RAG engine (document discovery, chunking, keyword search)
```

## Tips

- `scan_codebase` needs to be run once before `get_codebase_stats`, `search_files`, `search_functions`, `get_file_tree`, and `find_files_by_type`
- `build_rag_index` needs to be run before `search_codebase` and `generate_context`
- Pass `force_rescan=true` to `scan_codebase` to re-index from scratch
