---
name: fuego-code-analyzer
description: Analyze Fuego blockchain source code. Extract CD interest formulas, atomic swap states, P2P protocol versions, cryptographic implementations, and fee distribution logic.
risk: low
source: user-provided
---

# Fuego Code Analyzer

Analyze Fuego source code to extract blockchain mechanics:

- **CD Interest Code**: Find calculateCdInterest, epoch fee rate implementations
- **Atomic Swap Code**: Extract swap states, adaptor signature usage, HTLC patterns
- **P2P Code**: Protocol versions, consensus implementations
- **Crypto Code**: MuSig2, Pedersen, Bulletproof implementations
- **Fee Distribution**: Epoch processing, treasury handling

## Status

Unified MCP server verified: **53 tools** across 16 domains, zero dependencies, Python 3.9+.
Codebase scanned: **1,082 files**, 329,578 lines, 4,840 functions indexed.

## Trigger Set

**Should trigger on:**
- "analyze fuego code", "extract formulas", "find interest implementation"
- "swap code", "atomic swap states", "adaptor signature"
- "P2P protocol", "consensus code", "network implementation"
- "cryptographic primitives", "MuSig2", "musig2 implementation"
- "fee distribution code", "epoch processing"
- "fuego code analyzer mcp"
- "fuego mcp"

**Should NOT trigger on:**
- Generic code analysis unrelated to Fuego
- Questions about other blockchain codebases

## MCP Server

Unified Fuego MCP server (all domains in one zero-dependency script):

```bash
python3 scripts/fuego-mcp/fuego_mcp_server.py --mcp
```

MCP config:
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

CLI direct call:
```bash
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py get_config_constants
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py analyze_code_section pattern="calculateCdInterest|epoch.*fee"
```

## MCP Tools

Tools for source code analysis:

| Tool | Description | Params |
|------|-------------|--------|
| `get_config_constants` | Extract constants from CryptoNoteConfig.h | — |
| `get_source_domains` | All 26+ source domains with paths | — |
| `analyze_code_section` | Regex search across all source files | pattern |
| `get_difficulty_info` | DMWDA algorithm implementation | — |
| `get_swap_info` | Swap state machine, adaptor, HTLC | — |
| `get_tx_types` | All 8 transaction types | — |
| `get_crypto_primitives` | All crypto: signatures, hashes, ZK | — |
| `get_signature_schemes` | Ed25519, MLSAG, MuSig2, Adaptor | — |
| `get_hash_functions` | Hash implementations, PoW | — |
| `get_ring_signature_info` | MLSAG, ring sizes, decoy | — |
| `get_anonymity_info` | Tor, I2P, Meshtastic integration | — |
| `get_mining_config` | Mining algorithm, pool protocol | — |
| `get_privacy_model` | Ring sigs, stealth, Dandelion++ | — |
| `get_decoy_info` | OSPEAD decoy selection | — |
| `get_heat_info` | STARK commitments, burn validation | — |
| `get_contract_info` | Solidity bridge contracts | — |
| `get_alias_info` | On-chain alias registry | — |
| `get_lp_pool_info` | v11 AMM constant product design | — |
| `get_p2p_info` | Levin protocol, messages, peer lists | — |
| `get_daemon_rpc_methods` | All daemon RPC methods | — |
| `get_wallet_rpc_methods` | Wallet RPC methods | — |
| `get_rpc_error_codes` | RPC error code mappings | — |
| `get_payment_gateway_info` | PaymentGate service | — |

Codebase search tools also available (see fuego-codebase-mapper).

## Usage

CLI examples:
```bash
# Extract config constants
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py get_config_constants

# Search for interest-related code
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py analyze_code_section pattern="calculateCdInterest|epoch.*fee"

# List all source domains
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py get_source_domains
```

Python:
```python
from references import FuegoCodeAnalyzer
analyzer = FuegoCodeAnalyzer(source_dir="/Users/aejt/fuego")

cd_result = analyzer.analyze_cd_interest_code()
swap_result = analyzer.analyze_atomic_swap_code()
p2p_result = analyzer.analyze_p2p_code()
crypto_result = analyzer.analyze_crypto_code()
fee_result = analyzer.analyze_fee_distribution_code()
report = analyzer.generate_comprehensive_report()
```

## Output Examples

### CD Interest Analysis
Returns files containing interest calculations, extracted formulas, and config values (EPOCH_DURATION_BLOCKS, SWAP_FEE_RATE_BPS, etc.)

### Atomic Swap Analysis
Returns C++ files with swap logic, swap states from documentation, and adaptor signature usage

### Crypto Analysis
Returns crypto directory files, identifies MuSig2 implementation and its use for swaps

## References

See `references/` for:
- `analyzer.py` - FuegoCodeAnalyzer class
- `mcp_server.py` - MCP server entrypoint
