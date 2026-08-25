---
name: fuego-blockchain-specialist
description: Expert system for Fuego blockchain mechanics. Analyze CD interest calculations, atomic swap mechanics, fee distribution, P2P consensus, and cryptographic primitives.
risk: low
source: user-provided
---

# Fuego Blockchain Specialist

Expert in Fuego blockchain protocol mechanics with analysis tools for:

- **CD Interest**: Certificate of Deposit interest calculations, APY estimates, epoch-based fee rates
- **Atomic Swaps**: State machine analysis, adaptor signatures, fee calculations
- **Fee Distribution**: Epoch fee splitting between CD holders and treasury
- **P2P Networking**: Peer connection health analysis
- **Cryptography**: Signature schemes, hash functions, commitment schemes

## Status

Unified MCP server verified: **53 tools** across 16 domains, zero dependencies, Python 3.9+.

## Trigger Set

**Should trigger on:**
- "fuego CD interest", "calculate CD APY", "certificate of deposit interest"
- "atomic swap mechanics", "swap fee", "adaptor signature"
- "fee distribution", "epoch fees", "treasury share"
- "P2P network", "peer connections", "consensus"
- "cryptographic primitives", "MuSig2", "pedersen commitment"
- "fuego blockchain specialist mcp"
- "fuego mcp"

**Should NOT trigger on:**
- Generic blockchain questions unrelated to Fuego
- Non-Fuego cryptocurrency topics

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

CLI direct call (no MCP needed):
```bash
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py get_chain_info
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py calculate_emission height=500000
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py calculate_swap_fee xfg_amount=100000000000
```

## MCP Tools

Tools most relevant to blockchain mechanics analysis:

| Tool | Description | Params |
|------|-------------|--------|
| `get_chain_info` | Chain parameters: network, ports, block time | — |
| `get_config_constants` | Core constants from CryptoNoteConfig.h | — |
| `get_upgrade_schedule` | Protocol upgrade heights (V2-V11) | — |
| `get_token_supply` | Total supply, emission curve, precision | — |
| `get_fee_structure` | Fee tiers, priority levels, min fee | — |
| `calculate_emission` | Emission at given block height | height |
| `calculate_block_reward` | Block reward at given height | height |
| `calculate_cd_interest` | CD interest over epoch range | amount, creation_height, current_height, epoch_fee_rates[] |
| `estimate_apy` | APY from epoch fees and locked supply | current_epoch_fee, total_cd_locked |
| `calculate_swap_fee` | Swap fee and net for XFG amount | xfg_amount |
| `validate_swap_state` | Validate state machine transition | current_state, target_state |
| `get_swap_state_name` | Human-readable swap state name | state_id |
| `get_swap_info` | Full swap protocol details | — |
| `analyze_peer_health` | P2P network health score | peer_count, target? |
| `analyze_epoch_distribution` | Fee split between CDs and treasury | epoch_swap_fees, total_cd_locked |
| `get_crypto_primitives` | All crypto: signatures, hashes, commitments | — |
| `get_signature_schemes` | Ed25519, Schnorr, MLSAG, MuSig2, Adaptor | — |
| `get_ring_signature_info` | MLSAG, dynamic ring sizes, decoy selection | — |
| `get_hash_functions` | Keccak, Blake2b, CryptoNight variants | — |
| `get_difficulty_info` | DMWDA algorithm and windows | — |
| `get_mining_info` | PoW algorithm, CPU/pool mining | — |
| `get_privacy_model` | Ring sigs, stealth addresses, Dandelion++ | — |
| `get_decoy_info` | OSPEAD decoy selection, age bins | — |
| `get_heat_info` | HEAT token, mint/burn mechanics, treasury split, PI controller, HEARTH AMM | — |
| `get_contract_info` | Ethereum bridge contracts (Solidity) | — |
| `get_alias_info` | On-chain alias system | — |

Full list: 53 tools available via `tools/list` in MCP mode or `list` in CLI.

## Python Usage

```python
# Interest calculation
from references import CDInterestCalculator
calc = CDInterestCalculator(is_testnet=False)
interest = calc.calculate_cd_interest(
    amount=100000000,
    creation_height=1000000,
    current_height=1100000,
    epoch_fee_rates=[1000, 1500, 2000]
)

# Swap analysis
from references import AtomicSwapAnalyzer
analyzer = AtomicSwapAnalyzer()
fee, net = analyzer.analyze_swap_fee(xfg_amount=100000000)

# Fee distribution
from references import FeeDistributionAnalyzer
result = analyzer.analyze_epoch_distribution(500000000, 10000000000)

# P2P health
from references import P2PConsensusAnalyzer
health = analyzer.analyze_peer_connections(peer_count=6, target_count=8)
```

## Configuration

Mainnet defaults:
- Epoch duration: 900 blocks (~5 days)
- Swap fee rate: 100 bps (1%)
- CD share: 80%, Treasury share: 20%
- Fee pool precision: 1e6

## References

See `references/` for Python modules:
- `config.py` - Configuration dataclasses
- `calculator.py` - CD interest calculations
- `swaps.py` - Atomic swap analyzer
- `p2p.py` - P2P consensus analyzer
- `crypto.py` - Cryptographic primitives
- `fees.py` - Fee distribution analyzer
- `mcp_server.py` - MCP server entrypoint
