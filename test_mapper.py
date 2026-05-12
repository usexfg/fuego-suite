#!/usr/bin/env python3
"""
Test script for Fuego Codebase Mapper
"""

import sys
sys.path.insert(0, '/Users/aejt/fuego')

from simple_mapper import SimpleFuegoMapper

def test_basic_functionality():
    """Test basic mapper functionality"""
    print("Testing Fuego Codebase Mapper...")
    
    # Initialize mapper
    mapper = SimpleFuegoMapper("/Users/aejt/fuego")
    mapper.initialize()
    
    # Check if we need to scan
    stats = mapper.get_stats()
    
    if stats.total_files == 0:
        print("No data found. Running scan...")
        mapper.scan_codebase()
        stats = mapper.get_stats()
    
    print(f"\n=== Test Results ===")
    print(f"Total files: {stats.total_files}")
    print(f"Total lines: {stats.total_lines:,}")
    print(f"C++ files: {stats.cpp_files}")
    print(f"Header files: {stats.h_files}")
    print(f"Go files: {stats.go_files}")
    print(f"Python files: {stats.py_files}")
    
    # Test file search
    print(f"\n=== File Search Test ===")
    results = mapper.search_files("main", 5)
    print(f"Found {len(results)} files with 'main':")
    for result in results:
        print(f"  - {result['path']} ({result['type']})")
    
    # Test file tree
    print(f"\n=== File Tree Test (depth 2) ===")
    tree = mapper.get_file_tree(2)
    print(tree[:500] + "..." if len(tree) > 500 else tree)
    
    # Test dependencies
    print(f"\n=== Dependency Test ===")
    # Look for a C++ file to test dependencies
    cpp_results = mapper.search_files(".cpp", 3)
    if cpp_results:
        test_file = cpp_results[0]['path']
        deps = mapper.get_cpp_dependencies(test_file)
        print(f"Dependencies for {test_file}: {len(deps)} found")
        if deps:
            for dep in deps[:3]:  # Show first 3
                print(f"  - {dep['included']} (line {dep['line']})")
    
    # Export test
    print(f"\n=== Export Test ===")
    export_file = mapper.export_to_json("test_export.json")
    print(f"Exported to: {export_file}")
    
    mapper.close()
    print("\n=== All tests passed! ===")

def test_mcp_server():
    """Test MCP server initialization"""
    print("\n=== MCP Server Test ===")
    
    from mcp_server import FuegoMapperMCPServer
    
    server = FuegoMapperMCPServer()
    print("MCP Server initialized successfully")
    
    # Test tool listing
    import asyncio
    
    async def list_tools():
        tools = await server.server.list_tools()
        print(f"Available tools: {len(tools)}")
        for tool in tools:
            print(f"  - {tool.name}: {tool.description}")
    
    try:
        asyncio.run(list_tools())
        print("MCP Server test passed!")
    except Exception as e:
        print(f"MCP Server test failed: {e}")

if __name__ == "__main__":
    test_basic_functionality()
    test_mcp_server()