#!/usr/bin/env python3
"""
Simple Fuego Codebase Mapper
A lightweight version that provides core mapping functionality.
"""

import os
import json
import sqlite3
import hashlib
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple, Any
import re
from dataclasses import dataclass, asdict
from datetime import datetime

@dataclass
class FileInfo:
    path: str
    size: int
    modified: float
    file_type: str
    language: str
    lines: int
    sha256: str

@dataclass
class CodebaseStats:
    total_files: int = 0
    total_lines: int = 0
    cpp_files: int = 0
    go_files: int = 0
    py_files: int = 0
    h_files: int = 0
    other_files: int = 0

class SimpleFuegoMapper:
    def __init__(self, root_path: str = "/Users/aejt/fuego"):
        self.root = Path(root_path).absolute()
        self.db_file = self.root / ".fuego_map.db"
        self.conn = None
        self.cursor = None
        
    def initialize(self):
        """Initialize the mapper database"""
        self.conn = sqlite3.connect(self.db_file)
        self.cursor = self.conn.cursor()
        
        # Create tables
        self.cursor.execute('''
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            size INTEGER NOT NULL,
            modified REAL NOT NULL,
            file_type TEXT NOT NULL,
            language TEXT NOT NULL,
            lines INTEGER NOT NULL,
            sha256 TEXT NOT NULL,
            indexed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
        ''')
        
        self.cursor.execute('''
        CREATE TABLE IF NOT EXISTS file_tree (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            parent_id INTEGER,
            path TEXT NOT NULL,
            name TEXT NOT NULL,
            is_file BOOLEAN NOT NULL,
            depth INTEGER NOT NULL,
            FOREIGN KEY (parent_id) REFERENCES file_tree (id)
        )
        ''')
        
        self.cursor.execute('''
        CREATE TABLE IF NOT EXISTS cpp_includes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_file TEXT NOT NULL,
            included_file TEXT NOT NULL,
            line_number INTEGER NOT NULL,
            FOREIGN KEY (source_file) REFERENCES files (path) ON DELETE CASCADE
        )
        ''')
        
        self.conn.commit()
        
    def scan_codebase(self):
        """Scan the entire codebase"""
        print(f"Scanning Fuego codebase at: {self.root}")
        
        # Clear existing data
        self.cursor.execute("DELETE FROM files")
        self.cursor.execute("DELETE FROM file_tree")
        self.cursor.execute("DELETE FROM cpp_includes")
        
        # Walk through directory
        file_count = 0
        for root_dir, dirs, files in os.walk(self.root):
            # Skip hidden directories
            dirs[:] = [d for d in dirs if not d.startswith('.')]
            
            for file in files:
                # Skip hidden files
                if file.startswith('.'):
                    continue
                
                file_path = os.path.join(root_dir, file)
                rel_path = os.path.relpath(file_path, self.root)
                
                # Process file
                self._process_file(rel_path)
                file_count += 1
                
                if file_count % 100 == 0:
                    print(f"Processed {file_count} files...")
        
        # Build file tree
        self._build_file_tree()
        
        # Parse C++ includes
        self._parse_cpp_includes()
        
        self.conn.commit()
        print(f"Scan complete! Processed {file_count} files.")
        
    def _process_file(self, rel_path: str):
        """Process a single file"""
        full_path = self.root / rel_path
        
        if not full_path.exists():
            return
        
        # Get file stats
        stat = full_path.stat()
        
        # Determine file type and language
        file_type = full_path.suffix.lower()
        language = self._get_language(file_type)
        
        # Calculate hash and count lines
        try:
            with open(full_path, 'rb') as f:
                content = f.read()
                sha256 = hashlib.sha256(content).hexdigest()
            
            with open(full_path, 'r', encoding='utf-8', errors='ignore') as f:
                lines = sum(1 for _ in f)
        except Exception as e:
            print(f"Error processing {rel_path}: {e}")
            lines = 0
            sha256 = ""
        
        # Insert into database
        self.cursor.execute('''
        INSERT OR REPLACE INTO files 
        (path, size, modified, file_type, language, lines, sha256)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        ''', (
            rel_path,
            stat.st_size,
            stat.st_mtime,
            file_type,
            language,
            lines,
            sha256
        ))
    
    def _get_language(self, file_type: str) -> str:
        """Map file extension to language"""
        language_map = {
            '.cpp': 'cpp',
            '.h': 'cpp',
            '.hpp': 'cpp',
            '.cc': 'cpp',
            '.cxx': 'cpp',
            '.c': 'c',
            '.go': 'go',
            '.py': 'python',
            '.js': 'javascript',
            '.ts': 'typescript',
            '.md': 'markdown',
            '.txt': 'text',
            '.json': 'json',
            '.yml': 'yaml',
            '.yaml': 'yaml',
            '.toml': 'toml',
            '.cmake': 'cmake',
            '.sh': 'shell',
            '.makefile': 'make',
            '': 'unknown'
        }
        
        return language_map.get(file_type, 'other')
    
    def _build_file_tree(self):
        """Build hierarchical file tree"""
        # Get all files
        self.cursor.execute("SELECT path FROM files ORDER BY path")
        files = [row[0] for row in self.cursor.fetchall()]
        
        # Create root node
        self.cursor.execute(
            "INSERT INTO file_tree (parent_id, path, name, is_file, depth) VALUES (NULL, '', '.', 0, 0)"
        )
        root_id = self.cursor.lastrowid
        
        # Build tree structure
        node_map = {'': root_id}
        
        for file_path in files:
            parts = file_path.split('/')
            current_path = ''
            
            for i, part in enumerate(parts):
                parent_path = '/'.join(parts[:i])
                current_path = '/'.join(parts[:i+1]) if i > 0 else part
                
                if current_path not in node_map:
                    parent_id = node_map.get(parent_path, root_id)
                    is_file = (i == len(parts) - 1)
                    
                    self.cursor.execute('''
                    INSERT INTO file_tree (parent_id, path, name, is_file, depth)
                    VALUES (?, ?, ?, ?, ?)
                    ''', (parent_id, current_path, part, 1 if is_file else 0, i+1))
                    
                    node_map[current_path] = self.cursor.lastrowid
    
    def _parse_cpp_includes(self):
        """Parse C++ files for include statements"""
        print("Parsing C++ includes...")
        
        # Get all C++ files
        self.cursor.execute("SELECT path FROM files WHERE language = 'cpp'")
        cpp_files = [row[0] for row in self.cursor.fetchall()]
        
        include_count = 0
        for rel_path in cpp_files:
            full_path = self.root / rel_path
            
            try:
                with open(full_path, 'r', encoding='utf-8', errors='ignore') as f:
                    lines = f.readlines()
                
                for i, line in enumerate(lines):
                    line = line.strip()
                    if line.startswith('#include'):
                        # Parse include
                        match = re.match(r'#include\s+["<]([^">]+)[">]', line)
                        if match:
                            included = match.group(1)
                            self.cursor.execute('''
                            INSERT INTO cpp_includes (source_file, included_file, line_number)
                            VALUES (?, ?, ?)
                            ''', (rel_path, included, i+1))
                            include_count += 1
                
            except Exception as e:
                print(f"Error parsing includes in {rel_path}: {e}")
        
        print(f"Found {include_count} include statements.")
    
    def get_stats(self) -> CodebaseStats:
        """Get codebase statistics"""
        stats = CodebaseStats()
        
        # Total files and lines
        self.cursor.execute("SELECT COUNT(*), SUM(lines) FROM files")
        result = self.cursor.fetchone()
        stats.total_files = result[0] or 0
        stats.total_lines = result[1] or 0
        
        # File counts by type
        self.cursor.execute("SELECT file_type, COUNT(*) FROM files GROUP BY file_type")
        for file_type, count in self.cursor.fetchall():
            if file_type == '.cpp':
                stats.cpp_files = count
            elif file_type == '.go':
                stats.go_files = count
            elif file_type == '.py':
                stats.py_files = count
            elif file_type == '.h':
                stats.h_files = count
            else:
                stats.other_files += count
        
        return stats
    
    def search_files(self, query: str, limit: int = 20) -> List[Dict]:
        """Search for files by name"""
        results = []
        
        self.cursor.execute('''
        SELECT path, file_type, language, lines 
        FROM files 
        WHERE path LIKE ? 
        ORDER BY path
        LIMIT ?
        ''', (f'%{query}%', limit))
        
        for row in self.cursor.fetchall():
            results.append({
                'path': row[0],
                'type': row[1],
                'language': row[2],
                'lines': row[3]
            })
        
        return results
    
    def get_file_tree(self, max_depth: int = 3) -> str:
        """Get file tree as text"""
        self.cursor.execute('''
        SELECT depth, name, is_file
        FROM file_tree
        WHERE depth <= ?
        ORDER BY path
        ''', (max_depth,))
        
        tree_lines = []
        for depth, name, is_file in self.cursor.fetchall():
            indent = "  " * depth
            icon = "📄" if is_file else "📁"
            tree_lines.append(f"{indent}{icon} {name}")
        
        return "\n".join(tree_lines)
    
    def get_cpp_dependencies(self, file_path: str = None) -> List[Dict]:
        """Get C++ dependency information"""
        if file_path:
            self.cursor.execute('''
            SELECT source_file, included_file, line_number
            FROM cpp_includes
            WHERE source_file = ?
            ORDER BY line_number
            ''', (file_path,))
        else:
            self.cursor.execute('''
            SELECT source_file, included_file, line_number
            FROM cpp_includes
            ORDER BY source_file, line_number
            LIMIT 100
            ''')
        
        results = []
        for source, included, line in self.cursor.fetchall():
            results.append({
                'source': source,
                'included': included,
                'line': line
            })
        
        return results
    
    def export_to_json(self, output_file: str = "fuego_map.json"):
        """Export codebase map to JSON"""
        print(f"Exporting to {output_file}...")
        
        data = {
            'scan_time': datetime.now().isoformat(),
            'root_path': str(self.root),
            'stats': asdict(self.get_stats()),
            'files': [],
            'dependencies': []
        }
        
        # Get all files
        self.cursor.execute("SELECT * FROM files ORDER BY path")
        columns = [col[0] for col in self.cursor.description]
        
        for row in self.cursor.fetchall():
            file_data = dict(zip(columns, row))
            # Convert timestamp to string
            if 'indexed_at' in file_data:
                file_data['indexed_at'] = str(file_data['indexed_at'])
            data['files'].append(file_data)
        
        # Get dependencies
        self.cursor.execute("SELECT * FROM cpp_includes")
        columns = [col[0] for col in self.cursor.description]
        
        for row in self.cursor.fetchall():
            dep_data = dict(zip(columns, row))
            data['dependencies'].append(dep_data)
        
        # Write to file
        with open(output_file, 'w') as f:
            json.dump(data, f, indent=2)
        
        print(f"Export complete! {len(data['files'])} files exported.")
        return output_file
    
    def close(self):
        """Close database connection"""
        if self.conn:
            self.conn.close()


def main():
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Simple Fuego Codebase Mapper")
    parser.add_argument("--scan", action="store_true", help="Scan the codebase")
    parser.add_argument("--stats", action="store_true", help="Show statistics")
    parser.add_argument("--tree", action="store_true", help="Show file tree")
    parser.add_argument("--depth", type=int, default=3, help="Tree depth")
    parser.add_argument("--search", type=str, help="Search for files")
    parser.add_argument("--deps", type=str, help="Show dependencies for file")
    parser.add_argument("--export", action="store_true", help="Export to JSON")
    parser.add_argument("--path", type=str, default="/Users/aejt/fuego", help="Path to codebase")
    
    args = parser.parse_args()
    
    mapper = SimpleFuegoMapper(args.path)
    mapper.initialize()
    
    if args.scan:
        mapper.scan_codebase()
    
    if args.stats:
        stats = mapper.get_stats()
        print("\n=== Fuego Codebase Statistics ===")
        print(f"Total files: {stats.total_files}")
        print(f"Total lines: {stats.total_lines:,}")
        print(f"C++ files: {stats.cpp_files}")
        print(f"Header files: {stats.h_files}")
        print(f"Go files: {stats.go_files}")
        print(f"Python files: {stats.py_files}")
        print(f"Other files: {stats.other_files}")
    
    if args.tree:
        print(f"\n=== File Tree (depth: {args.depth}) ===")
        tree = mapper.get_file_tree(args.depth)
        print(tree)
    
    if args.search:
        print(f"\n=== Search results for '{args.search}' ===")
        results = mapper.search_files(args.search)
        for result in results:
            print(f"{result['path']} ({result['type']}, {result['lines']} lines)")
    
    if args.deps:
        print(f"\n=== Dependencies for '{args.deps}' ===")
        deps = mapper.get_cpp_dependencies(args.deps)
        if deps:
            for dep in deps:
                print(f"Line {dep['line']}: includes {dep['included']}")
        else:
            print("No dependencies found or file not a C++ file.")
    
    if args.export:
        output_file = mapper.export_to_json()
        print(f"Exported to: {output_file}")
    
    # If no arguments, show help
    if not any([args.scan, args.stats, args.tree, args.search, args.deps, args.export]):
        print("Simple Fuego Codebase Mapper")
        print("Available commands:")
        print("  --scan         Scan the codebase")
        print("  --stats        Show statistics")
        print("  --tree         Show file tree (use --depth to change depth)")
        print("  --search QUERY Search for files")
        print("  --deps FILE    Show dependencies for a file")
        print("  --export       Export to JSON")
        print("  --path PATH    Specify codebase path")
    
    mapper.close()


if __name__ == "__main__":
    main()