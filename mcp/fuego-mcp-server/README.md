# fuego-mcp-server

MCP server providing optimal context for AI models working on the Fuego cryptocurrency codebase.

## Tools

### Embedded Context (no filesystem I/O)
| Tool | Description |
|------|-------------|
| `fuego_get_overview` | Full architecture overview, directory map, tech stack, binary list |
| `fuego_get_module_info` | Deep-dive into a specific module (CryptoNoteCore, Crypto, P2P, Rpc, Wallet, TUI, SwapXFG, Miner, SwapDaemon) |
| `fuego_get_rpc_reference` | Complete RPC API reference for fuegod (port 18180), walletd (port 18282), and TUI extensions |
| `fuego_get_data_types` | C++ type definitions with explanations: Block, Transaction, inputs/outputs, deposits |
| `fuego_get_crypto_explainer` | Ring signatures, stealth addresses, Pedersen commitments, adaptor signatures, MLSAG |
| `fuego_get_token_model` | XFG, CD, HEAT, DIGM/PARA/VOX/CURA token model and economic mechanics |
| `fuego_get_build_guide` | Build prerequisites, CMake/Makefile commands, run commands, Docker |

### Filesystem Tools (dynamic, reads actual code)
| Tool | Description |
|------|-------------|
| `fuego_search_code` | Grep the codebase for symbols, patterns, or text |
| `fuego_read_file` | Read a source file with line numbers and range support |
| `fuego_find_files` | Find files by name/glob pattern |

## Configuration

### Environment Variables
| Variable | Default | Description |
|----------|---------|-------------|
| `FUEGO_ROOT` | Auto-detected (3 levels up from `dist/`) | Path to Fuego project root |

### Claude Code MCP Config
Add to your `.claude/settings.json` or `~/.claude/settings.json`:

```json
{
  "mcpServers": {
    "fuego": {
      "command": "node",
      "args": ["/path/to/mcp/fuego-mcp-server/dist/index.js"],
      "env": {
        "FUEGO_ROOT": "/path/to/fuego"
      }
    }
  }
}
```

## Build

```bash
npm install
npm run build
```

## Usage (Inspector)

```bash
npx @modelcontextprotocol/inspector node dist/index.js
```
