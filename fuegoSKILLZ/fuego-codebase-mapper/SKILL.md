---
name: fuego-codebase-mapper
description: Map the Fuego codebase structure. Search files, functions, classes, dependencies, and get codebase statistics. Provides file tree navigation and code discovery.
risk: low
source: user-provided
---

# Fuego Codebase Mapper

Map and navigate the Fuego codebase:

- **File Search**: Find files by name or pattern
- **Function Search**: Locate functions across the codebase
- **Statistics**: File counts, line counts, language distribution
- **File Tree**: Hierarchical view of codebase structure
- **Dependencies**: C++ include tracking

## Status

**Verified: 1,082 files indexed, 329,578 lines, 4,840 functions.**
Languages: 933 C++, 30 Go, 79 Markdown, 40 Python.
Unified MCP server: **53 tools**, zero dependencies, Python 3.9+.

## Trigger Set

**Should trigger on:**
- "map fuego", "codebase structure", "file tree"
- "search fuego files", "find function", "locate class"
- "fuego statistics", "file count", "line count"
- "codebase dependencies", "include graph"
- "fuego codebase mapper mcp"
- "fuego mcp"

**Should NOT trigger on:**
- Non-Fuego codebase questions
- Generic code search without fuego context

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
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py scan_codebase
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py get_codebase_stats
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py search_files query=miner
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py search_functions name=validate
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py get_file_tree depth=2
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py find_files_by_type file_type=.go
```

## MCP Tools

| Tool | Description | Params |
|------|-------------|--------|
| `scan_codebase` | Index all files into searchable DB | force_rescan? |
| `get_codebase_stats` | File/line/function/language counts | — |
| `search_files` | Find files by name/pattern | query, limit? |
| `search_functions` | Find functions by name | name, limit? |
| `get_file_tree` | Hierarchical directory tree | depth? |
| `find_files_by_type` | Find files by extension | file_type, limit? |
| `get_source_domains` | 26+ source code domains with paths | — |
| `analyze_code_section` | Regex search across all source | pattern |

## Usage

CLI examples:
```bash
# Initial scan (creates DB)
python3 /Users/aejt/fuego/fuego_mcp_server.py scan_codebase

# Get stats
python3 /Users/aejt/fuego/fuego_mcp_server.py get_codebase_stats
# → 1082 files, 329578 lines, 4840 functions

# Search for files
python3 /Users/aejt/fuego/fuego_mcp_server.py search_files query=blockchain

# Search for functions
python3 /Users/aejt/fuego/fuego_mcp_server.py search_functions name=validate limit=10

# Get file tree
python3 /Users/aejt/fuego/fuego_mcp_server.py get_file_tree depth=2

# Find Go files
python3 /Users/aejt/fuego/fuego_mcp_server.py find_files_by_type file_type=.go
```

Python:
```python
from references import CodebaseMapper

mapper = CodebaseMapper("/Users/aejt/fuego")
mapper.initialize_database()
mapper.scan_codebase()

results = mapper.search_files("Blockchain", limit=10)
funcs = mapper.search_functions("calculateCdInterest")
stats = mapper.get_stats()
tree = mapper.get_file_tree(depth=3)
```

## References

See `references/` for:
- `mapper.py` - CodebaseMapper class
- `mcp_server.py` - MCP server entrypoint
