#!/usr/bin/env python3
"""
MCP Server for Fuego Codebase Mapper
Model Context Protocol server that provides codebase analysis tools.
"""

import os
import sys
import json
from pathlib import Path
from typing import Any, Dict, List, Optional
import asyncio

# MCP imports
from mcp.server import Server, NotificationOptions
from mcp.server.models import InitializationOptions
import mcp.server.stdio
import mcp.types as types

# Local imports
from simple_mapper import SimpleFuegoMapper


class FuegoMapperMCPServer:
    """MCP Server implementation for Fuego Codebase Mapper"""
    
    def __init__(self):
        self.server = Server("fuego-codebase-mapper")
        self.mapper = None
        self.codebase_path = "/Users/aejt/fuego"
        self.setup_handlers()
    
    def setup_handlers(self):
        """Set up MCP server handlers"""
        
        @self.server.list_tools()
        async def handle_list_tools() -> List[types.Tool]:
            return [
                types.Tool(
                    name="fuego_scan_codebase",
                    description="Scan the Fuego codebase and build comprehensive map",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "force_rescan": {
                                "type": "boolean",
                                "description": "Force rescan even if files haven't changed",
                                "default": False
                            }
                        }
                    }
                ),
                types.Tool(
                    name="fuego_get_stats",
                    description="Get statistics about the Fuego codebase",
                    inputSchema={
                        "type": "object",
                        "properties": {}
                    }
                ),
                types.Tool(
                    name="fuego_search_files",
                    description="Search for files in the Fuego codebase",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "query": {
                                "type": "string",
                                "description": "Search query (filename or path)"
                            },
                            "limit": {
                                "type": "number",
                                "description": "Maximum number of results",
                                "default": 20
                            }
                        },
                        "required": ["query"]
                    }
                ),
                types.Tool(
                    name="fuego_get_file_tree",
                    description="Get hierarchical file tree of the codebase",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "depth": {
                                "type": "number",
                                "description": "Maximum depth to show",
                                "default": 3
                            }
                        }
                    }
                ),
                types.Tool(
                    name="fuego_get_dependencies",
                    description="Get C++ dependencies for a file",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "file_path": {
                                "type": "string",
                                "description": "Path to file (relative to codebase root)"
                            }
                        },
                        "required": ["file_path"]
                    }
                ),
                types.Tool(
                    name="fuego_analyze_structure",
                    description="Analyze codebase structure and organization",
                    inputSchema={
                        "type": "object",
                        "properties": {}
                    }
                ),
                types.Tool(
                    name="fuego_find_by_type",
                    description="Find files by type/language",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "file_type": {
                                "type": "string",
                                "description": "File type (e.g., .cpp, .go, .py)",
                                "default": ".cpp"
                            },
                            "limit": {
                                "type": "number",
                                "description": "Maximum number of results",
                                "default": 20
                            }
                        },
                        "required": ["file_type"]
                    }
                )
            ]
        
        @self.server.call_tool()
        async def handle_call_tool(name: str, arguments: dict) -> List[types.TextContent]:
            # Initialize mapper if needed
            if not self.mapper:
                self.mapper = SimpleFuegoMapper(self.codebase_path)
                self.mapper.initialize()
            
            try:
                if name == "fuego_scan_codebase":
                    force_rescan = arguments.get("force_rescan", False)
                    if force_rescan:
                        self.mapper.scan_codebase()
                        return [types.TextContent(
                            type="text",
                            text="Codebase scan complete! Database has been updated."
                        )]
                    else:
                        # Check if we need to scan
                        stats = self.mapper.get_stats()
                        if stats.total_files == 0:
                            self.mapper.scan_codebase()
                            return [types.TextContent(
                                type="text",
                                text="Codebase scan complete! Database has been initialized."
                            )]
                        else:
                            return [types.TextContent(
                                type="text",
                                text=f"Codebase already scanned. Found {stats.total_files} files. Use force_rescan=true to rescan."
                            )]
                
                elif name == "fuego_get_stats":
                    stats = self.mapper.get_stats()
                    
                    text = "## Fuego Codebase Statistics\n\n"
                    text += f"**Total Files:** {stats.total_files}\n"
                    text += f"**Total Lines:** {stats.total_lines:,}\n"
                    text += f"**C++ Files:** {stats.cpp_files}\n"
                    text += f"**Header Files:** {stats.h_files}\n"
                    text += f"**Go Files:** {stats.go_files}\n"
                    text += f"**Python Files:** {stats.py_files}\n"
                    text += f"**Other Files:** {stats.other_files}\n"
                    
                    return [types.TextContent(type="text", text=text)]
                
                elif name == "fuego_search_files":
                    query = arguments["query"]
                    limit = arguments.get("limit", 20)
                    
                    results = self.mapper.search_files(query, limit)
                    
                    if not results:
                        return [types.TextContent(
                            type="text",
                            text=f"No files found matching '{query}'"
                        )]
                    
                    text = f"## Search Results for '{query}'\n\n"
                    for result in results:
                        text += f"- **{result['path']}** ({result['type']}, {result['lines']} lines)\n"
                    
                    if len(results) == limit:
                        text += f"\n*Showing first {limit} results*\n"
                    
                    return [types.TextContent(type="text", text=text)]
                
                elif name == "fuego_get_file_tree":
                    depth = arguments.get("depth", 3)
                    tree = self.mapper.get_file_tree(depth)
                    
                    text = f"## Fuego File Tree (depth: {depth})\n\n```\n{tree}\n```"
                    return [types.TextContent(type="text", text=text)]
                
                elif name == "fuego_get_dependencies":
                    file_path = arguments["file_path"]
                    deps = self.mapper.get_cpp_dependencies(file_path)
                    
                    if not deps:
                        return [types.TextContent(
                            type="text",
                            text=f"No dependencies found for '{file_path}'\n\n*Note: Only C++ files have dependency tracking.*"
                        )]
                    
                    text = f"## Dependencies for '{file_path}'\n\n"
                    
                    # Group by source file
                    current_source = None
                    for dep in deps:
                        if dep['source'] != current_source:
                            current_source = dep['source']
                            text += f"\n### {current_source}:\n"
                        
                        text += f"- Line {dep['line']}: `#include {dep['included']}`\n"
                    
                    return [types.TextContent(type="text", text=text)]
                
                elif name == "fuego_analyze_structure":
                    stats = self.mapper.get_stats()
                    
                    text = "## Fuego Codebase Structure Analysis\n\n"
                    
                    # Analyze by directory
                    text += "### Major Directories:\n"
                    major_dirs = [
                        ("src/", "C++ source code core"),
                        ("swapxfg/", "Go-based TUI for atomic swaps"),
                        ("tui/", "Terminal user interface"),
                        ("tests/", "Test files"),
                        ("docs/", "Documentation"),
                        ("docker/", "Docker configurations"),
                        ("external/", "External dependencies"),
                        ("fuego-prover/", "Zero-knowledge proof components"),
                        ("contracts/", "Smart contracts"),
                        ("scripts/", "Build and utility scripts")
                    ]
                    
                    for dir_name, description in major_dirs:
                        text += f"- **{dir_name}**: {description}\n"
                    
                    # Language distribution
                    text += "\n### Language Distribution:\n"
                    total = stats.total_files
                    
                    if total > 0:
                        text += f"- C++ (.cpp/.h): {stats.cpp_files + stats.h_files} files ({((stats.cpp_files + stats.h_files) / total * 100):.1f}%)\n"
                        text += f"- Go: {stats.go_files} files ({(stats.go_files / total * 100):.1f}%)\n"
                        text += f"- Python: {stats.py_files} files ({(stats.py_files / total * 100):.1f}%)\n"
                        text += f"- Other: {stats.other_files} files ({(stats.other_files / total * 100):.1f}%)\n"
                    
                    # Architecture notes
                    text += "\n### Architecture Notes:\n"
                    text += "- **Core**: C++ CryptoNote-based cryptocurrency implementation\n"
                    text += "- **TUI**: Go-based terminal interface for swaps and wallet management\n"
                    text += "- **Build**: CMake-based build system with Makefile wrapper\n"
                    text += "- **Tests**: Comprehensive test suite in tests/ directory\n"
                    text += "- **Docker**: Containerized deployment options\n"
                    
                    return [types.TextContent(type="text", text=text)]
                
                elif name == "fuego_find_by_type":
                    file_type = arguments["file_type"]
                    limit = arguments.get("limit", 20)
                    
                    # Search for files by type
                    self.mapper.cursor.execute('''
                    SELECT path, lines 
                    FROM files 
                    WHERE file_type = ? 
                    ORDER BY path
                    LIMIT ?
                    ''', (file_type, limit))
                    
                    results = self.mapper.cursor.fetchall()
                    
                    if not results:
                        return [types.TextContent(
                            type="text",
                            text=f"No {file_type} files found."
                        )]
                    
                    text = f"## {file_type} Files\n\n"
                    for path, lines in results:
                        text += f"- **{path}** ({lines} lines)\n"
                    
                    if len(results) == limit:
                        text += f"\n*Showing first {limit} results*\n"
                    
                    return [types.TextContent(type="text", text=text)]
                
                else:
                    return [types.TextContent(
                        type="text",
                        text=f"Unknown tool: {name}"
                    )]
            
            except Exception as e:
                return [types.TextContent(
                    type="text",
                    text=f"Error executing tool '{name}': {str(e)}"
                )]
    
    async def run(self):
        """Run the MCP server"""
        print("Starting Fuego Codebase Mapper MCP server...", file=sys.stderr)
        
        async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
            await self.server.run(
                read_stream,
                write_stream,
                InitializationOptions(
                    server_name="fuego-codebase-mapper",
                    server_version="1.0.0",
                    capabilities=types.ServerCapabilities()
                )
            )


def main():
    """Main entry point for standalone execution"""
    # Check if running as MCP server
    if len(sys.argv) > 1 and sys.argv[1] == "--mcp":
        server = FuegoMapperMCPServer()
        asyncio.run(server.run())
    else:
        # Run as CLI tool
        print("Fuego Codebase Mapper MCP Server")
        print("Usage: python mcp_server.py --mcp")
        print("\nTo use with Claude Desktop:")
        print("1. Add to Claude Desktop config:")
        print('''
{
  "mcpServers": {
    "fuego-mapper": {
      "command": "python",
      "args": ["/path/to/fuego/mcp_server.py", "--mcp"],
      "env": {
        "PYTHONPATH": "/path/to/fuego"
      }
    }
  }
}
        ''')
        
        # Demo the mapper
        print("\n--- Demo Mode ---")
        mapper = SimpleFuegoMapper("/Users/aejt/fuego")
        mapper.initialize()
        
        # Quick scan if needed
        stats = mapper.get_stats()
        if stats.total_files == 0:
            print("Database empty. Running quick scan...")
            mapper.scan_codebase()
            stats = mapper.get_stats()
        
        print(f"\nFuego Codebase: {stats.total_files} files, {stats.total_lines:,} lines")
        print(f"C++: {stats.cpp_files} files, Headers: {stats.h_files} files")
        print(f"Go: {stats.go_files} files, Python: {stats.py_files} files")
        
        # Show some example files
        print("\nExample files:")
        examples = mapper.search_files("main", 5)
        for ex in examples:
            print(f"  - {ex['path']}")
        
        mapper.close()


if __name__ == "__main__":
    main()