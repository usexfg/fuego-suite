---
name: fuego-rag
description: Semantic search and document retrieval for the Fuego codebase. Find relevant code and documentation using keyword and semantic search over indexed chunks.
risk: low
source: user-provided
---

# Fuego RAG System

Semantic search and document retrieval for Fuego codebase:

- **Document Discovery**: Auto-find source code and documentation
- **Semantic Chunking**: Split code by functions/classes
- **Keyword Search**: Find relevant code chunks
- **Context Retrieval**: Get surrounding code context

## Status

Unified MCP server verified: **53 tools** across 16 domains, zero dependencies, Python 3.9+.
RAG index: supports building from up to 1,082 indexed files (C++, Go, Python, Markdown).

## Trigger Set

**Should trigger on:**
- "rag search fuego", "semantic search", "find related code"
- "search documentation", "find code about", "lookup"
- "fuego context", "retrieve relevant code"
- "fuego rag mcp"
- "fuego mcp"

**Should NOT trigger on:**
- Non-Fuego search queries
- General web search

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
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py build_rag_index limit=50
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py search_codebase query="CD interest" top_k=5
python3 /Users/aejt/fuego/scripts/fuego-mcp/fuego_mcp_server.py generate_context query="difficulty adjustment" top_k=3
```

## MCP Tools

| Tool | Description | Params |
|------|-------------|--------|
| `build_rag_index` | Discover docs, read, chunk by function/class | limit? |
| `search_codebase` | Keyword search over indexed chunks | query, top_k? |
| `generate_context` | Search + build LLM-ready context prompt | query, top_k? |

Supporting tools:
| `scan_codebase` | Index file listing into SQLite DB | force_rescan? |
| `get_codebase_stats` | File/line/function/language counts | — |
| `get_source_domains` | 26+ source domains with paths | — |
| `analyze_code_section` | Regex search across all source | pattern |

## Usage

CLI examples:
```bash
# Build index (default limit=50 docs)
python3 /Users/aejt/fuego/fuego_mcp_server.py build_rag_index

# Search indexed chunks
python3 /Users/aejt/fuego/fuego_mcp_server.py search_codebase query="atomic swap state" top_k=5

# Generate LLM-ready context
python3 /Users/aejt/fuego/fuego_mcp_server.py generate_context query="how does CD interest work" top_k=3
```

Python:
```python
from references import FuegoRAGSystem

rag = FuegoRAGSystem("/Users/aejt/fuego")

# Build index (run once per session)
result = rag.build_index(limit=50)

# Keyword search
chunks = rag.search_chunks("CD interest calculation", top_k=5)

# Generate LLM-ready prompt
prompt = rag.generate_response("How does CD interest work?", chunks)
```

## Search Features

- Keyword matching with relevance scoring
- Boost for domain-specific terms in relevant document types
- Returns source file path, line numbers, and content preview
- Generates LLM-ready context prompt with source references
- Chunking by function/class boundaries for code files

## References

See `references/` for:
- `rag.py` - FuegoRAGSystem class
- `mcp_server.py` - MCP server entrypoint
