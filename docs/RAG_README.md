# RAG Implementation for Fuego Blockchain

## Overview

This RAG (Retrieval-Augmented Generation) system helps you navigate and understand the Fuego blockchain codebase by providing intelligent document retrieval and context for LLM-based Q&A.

## Available Implementations

### 1. Full RAG System (`rag_system.py`)
**Features:**
- Automatic document discovery
- Semantic chunking (different strategies for C++/Go/Markdown)
- Document type classification
- Metadata tracking
- Interactive query interface

**Installation:**
```bash
pip install chromadb sentence-transformers langchain
```

**Usage:**
```bash
python rag_system.py
```

### 2. Simple RAG System (`simple_rag.py`)
**Features:**
- No external dependencies
- Pre-indexed key Fuego files
- Basic keyword search
- Simple chunking

**Usage:**
```bash
python simple_rag.py
```

## Architecture

```
User Query → Search → Retrieved Chunks → Context Assembly → LLM Prompt → Answer
               |              |                  |                |
        Keyword/      Document Store       Template      Your LLM
        Semantic
```

## Supported Document Types

1. **C++ Source Code** (`src/` directory)
   - Chunked by functions/classes
   - Brace-aware parsing

2. **Go Source Code** (`swapxfg/`, `tui/`)
   - Chunked by functions
   - Go-specific parsing

3. **Documentation** (`docs/`, Markdown files)
   - Chunked by headers/sections
   - Maintains document structure

## Example Queries

The RAG system can answer questions about:

### CD (Certificate of Deposit) System
- Interest calculation formulas
- Fee pool mechanics (80/20 split)
- Epoch-based distribution
- Square root vs proportional scaling
- APY/APR calculations

### Codebase Navigation
- Finding specific functionality
- Understanding component relationships
- Build system guidance
- Architecture patterns

### Blockchain Features
- CryptoNote protocol implementation
- Privacy features (ring signatures, stealth addresses)
- Atomic swap functionality
- Commitment deposit system

## Integration with LLMs

The system generates prompts like:

```
Based on the following Fuego codebase context, answer the query.

Query: How is CD interest calculated?

Context from codebase:
--- Source 1 (src/CryptoNoteCore/Currency.cpp) ---
[Relevant code snippet...]
--- Source 2 (docs/commitment-types.md) ---
[Relevant documentation...]

Instructions:
1. Answer specifically about Fuego blockchain implementation
2. Reference source files when possible
3. Be precise about CD interest calculations if relevant
4. If information is incomplete, say so

Answer:
```

## Extending the System

### Adding New Document Types
1. Add file extension to `discover_documents()`
2. Create chunking function in `semantic_chunking()`
3. Update `_get_document_type()` for classification

### Improving Search
1. Add vector embeddings (ChromaDB)
2. Implement hybrid search (semantic + keyword)
3. Add reranking based on relevance

### Production Deployment
1. Use persistent vector database (ChromaDB, Pinecone, Weaviate)
2. Add caching layer
3. Implement batch indexing
4. Add monitoring and evaluation

## Performance Notes

### For Development/Testing:
- Use `simple_rag.py` (fastest setup)
- Limited to key files
- Basic keyword search

### For Production:
- Use `rag_system.py` with embeddings
- Consider cloud vector databases
- Implement caching
- Add evaluation metrics

## Troubleshooting

### Common Issues:
1. **Missing dependencies**: Install with `pip install -r requirements.txt`
2. **Unreadable files**: Check file permissions and encodings
3. **Slow indexing**: Limit document count or use batch processing
4. **Poor search results**: Adjust chunking strategy or add more documents

### Optimization Tips:
1. Start with key files only
2. Adjust chunk sizes based on content type
3. Use overlap for context continuity
4. Add metadata filtering for better precision

## Next Steps

1. **Immediate**: Run `python simple_rag.py` to test basic functionality
2. **Short-term**: Add vector embeddings for semantic search
3. **Medium-term**: Integrate with your preferred LLM (Claude, GPT, etc.)
4. **Long-term**: Deploy as a service with API endpoints

## Example Session

```bash
$ python simple_rag.py

============================================================
SIMPLE RAG SYSTEM FOR FUEGO
============================================================

Example queries:
  - CD interest calculation
  - swap fees
  - commitment deposit
  - CryptoNote protocol
  - Type 'quit' to exit
============================================================

Query: CD interest calculation

Found 3 results for: CD interest calculation

[1] src/CryptoNoteCore/Currency.cpp (lines 1-20)
    Preview: // Currency.cpp - CD interest rate calculation...

[2] docs/commitment-types.md (lines 41-60)
    Preview: ## Interest Distribution...

[3] src/CryptoNoteCore/Blockchain.cpp (lines 3236-3256)
    Preview: // Epoch-based fee pool distribution...
```

## Support

For questions or issues:
1. Check the README files
2. Review the code comments
3. Test with different queries
4. Adjust chunking parameters as needed