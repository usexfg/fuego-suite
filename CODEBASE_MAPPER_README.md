# Fuego Codebase Mapper

A comprehensive codebase mapping system for the Fuego cryptocurrency project. This system provides real-time navigation, search, and analysis capabilities for the entire Fuego codebase.

## Features

1. **File Structure Mapping**: Hierarchical view of the entire codebase
2. **Dependency Analysis**: C++ include dependency tracking
3. **Function/Class Catalog**: Automatic extraction of functions and classes
4. **Cross-reference Database**: SQLite database with full-text search
5. **Real-time Search**: Fast search across file names and paths
6. **MCP Server Integration**: Model Context Protocol server for Claude Desktop
7. **Statistics & Analytics**: Codebase metrics and analysis

## Architecture

The mapper consists of three main components:

1. **SimpleFuegoMapper** (`simple_mapper.py`): Core mapping engine with SQLite storage
2. **MCP Server** (`mcp_server.py`): Model Context Protocol server for AI integration
3. **Full Parser** (`codebase_mapper.py`): Advanced parser with function/class extraction

## Quick Start

### Basic Usage

```bash
# Scan the codebase
python3 simple_mapper.py --scan

# Get statistics
python3 simple_mapper.py --stats

# Search for files
python3 simple_mapper.py --search "wallet"

# Show file tree
python3 simple_mapper.py --tree --depth 3

# Show dependencies for a file
python3 simple_mapper.py --deps "src/CryptoNoteCore/CryptoNoteBasicImpl.cpp"

# Export to JSON
python3 simple_mapper.py --export
```

### MCP Server (Claude Desktop Integration)

1. Install required packages:
```bash
pip install mcp
```

2. Run the MCP server:
```bash
python3 mcp_server.py --mcp
```

3. Configure Claude Desktop (see `claude_desktop_config.json`)

## MCP Tools Available

When integrated with Claude Desktop, the following tools are available:

- `fuego_scan_codebase`: Scan the Fuego codebase
- `fuego_get_stats`: Get codebase statistics
- `fuego_search_files`: Search for files by name/path
- `fuego_get_file_tree`: Get hierarchical file tree
- `fuego_get_dependencies`: Get C++ include dependencies
- `fuego_analyze_structure`: Analyze codebase structure
- `fuego_find_by_type`: Find files by type (.cpp, .go, .py, etc.)

## Database Schema

The mapper uses SQLite with the following tables:

- `files`: File metadata (path, size, type, language, lines, hash)
- `file_tree`: Hierarchical file structure
- `cpp_includes`: C++ include dependency tracking
- (Advanced) `functions`: Extracted function definitions
- (Advanced) `classes`: Extracted class definitions
- (Advanced) `dependencies`: Cross-file dependencies

## Example Usage with Claude

```
User: Can you scan the Fuego codebase and show me statistics?

Claude: I'll use the fuego_codebase_mapper to scan and get statistics...
[Uses fuego_scan_codebase, then fuego_get_stats]

User: Find all wallet-related files

Claude: Searching for wallet files...
[Uses fuego_search_files with query "wallet"]

User: Show me the structure of the src directory

Claude: Getting file tree for src directory...
[Uses fuego_get_file_tree with appropriate filtering]

User: What are the dependencies of the main CryptoNote implementation?

Claude: Analyzing dependencies...
[Uses fuego_get_dependencies with specific file path]
```

## Codebase Statistics (Current Scan)

Based on the initial scan:
- **Total files**: 2,650
- **Total lines**: 5,362,462
- **C++ files**: 355
- **Header files**: 573
- **Go files**: 30
- **Python files**: 38
- **C++ includes**: 5,344

## Project Structure Analysis

The Fuego codebase is organized as follows:

- **Core** (`src/`): C++ CryptoNote implementation (931 C++/header files)
- **TUI** (`swapxfg/`, `tui/`): Go-based terminal interface for atomic swaps
- **Prover** (`fuego-prover/`): Zero-knowledge proof components (Rust)
- **Tests** (`tests/`): Comprehensive test suite
- **Docs** (`docs/`): Extensive documentation
- **Scripts** (`scripts/`): Build and utility scripts
- **Docker** (`docker/`): Containerized deployment
- **External** (`external/`): Third-party dependencies

## Advanced Features (codebase_mapper.py)

The advanced parser includes:

- Function extraction from C++, Go, Python, JavaScript/TypeScript
- Class/structure extraction
- Parameter and return type parsing
- Namespace tracking
- Method vs function classification

## Performance

- Initial scan: ~30 seconds for 2,650 files
- Database queries: < 100ms
- Memory usage: ~50MB
- Storage: ~20MB for database

## Integration with Other Systems

The mapper can be integrated with:

- **CI/CD pipelines**: Track codebase changes over time
- **Documentation generators**: Auto-generate API docs
- **Code review tools**: Provide context during reviews
- **AI assistants**: Enhanced code understanding for AI agents

## Future Enhancements

Planned features:
- Cross-language call graph analysis
- Code complexity metrics
- Change impact analysis
- Visual dependency graphs
- Integration with IDEs (VS Code, IntelliJ)
- Real-time file watching
- API documentation generation

## License

Part of the Fuego project. See main LICENSE file for details.

## Support

For issues or feature requests:
- Open an issue on the Fuego GitHub repository
- Contact the development team via Discord
- Check the documentation in `docs/` directory