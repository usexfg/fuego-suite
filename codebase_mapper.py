#!/usr/bin/env python3
"""
Fuego Codebase Mapper - MCP Server
A comprehensive codebase mapping system for the Fuego project.
Provides: file structure mapping, dependency analysis, function/class catalog,
cross-reference database, and real-time search capabilities.
"""

import os
import json
import sqlite3
import hashlib
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import Dict, List, Optional, Set, Tuple, Any
import ast
import re
from concurrent.futures import ThreadPoolExecutor
import time
from datetime import datetime
import sys

# MCP server imports
from mcp.server import Server, NotificationOptions
from mcp.server.models import InitializationOptions
import mcp.server.stdio
import mcp.types as types

@dataclass
class FileInfo:
    """Metadata for a file in the codebase"""
    path: str
    size: int
    modified_time: float
    file_type: str  # .cpp, .h, .go, .py, etc.
    language: str   # cpp, go, python, etc.
    lines: int
    sha256: str

@dataclass
class FunctionInfo:
    """Information about a function in the codebase"""
    name: str
    file_path: str
    line_start: int
    line_end: int
    return_type: str = ""
    parameters: List[str] = None
    is_method: bool = False
    class_name: str = ""
    namespace: str = ""
    
    def __post_init__(self):
        if self.parameters is None:
            self.parameters = []

@dataclass
class ClassInfo:
    """Information about a class in the codebase"""
    name: str
    file_path: str
    line_start: int
    line_end: int
    base_classes: List[str] = None
    methods: List[FunctionInfo] = None
    namespace: str = ""
    
    def __post_init__(self):
        if self.base_classes is None:
            self.base_classes = []
        if self.methods is None:
            self.methods = []

@dataclass
class Dependency:
    """Dependency relationship between files"""
    source_file: str
    target_file: str
    dependency_type: str  # include, import, link, etc.
    line_number: int

class CodebaseMapper:
    """Main codebase mapping engine"""
    
    def __init__(self, root_path: str):
        self.root_path = Path(root_path).absolute()
        self.db_path = self.root_path / ".codebase_map.db"
        
        # Database connection
        self.conn = None
        self.cursor = None
        
        # In-memory indexes
        self.files: Dict[str, FileInfo] = {}
        self.functions: Dict[str, List[FunctionInfo]] = {}
        self.classes: Dict[str, List[ClassInfo]] = {}
        self.dependencies: List[Dependency] = []
        
        # Language-specific parsers
        self.language_parsers = {
            'cpp': self._parse_cpp_file,
            'h': self._parse_cpp_file,
            'hpp': self._parse_cpp_file,
            'go': self._parse_go_file,
            'py': self._parse_python_file,
            'js': self._parse_js_file,
            'ts': self._parse_ts_file,
        }
        
        # File type to language mapping
        self.file_type_to_language = {
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
            '.tsx': 'typescript',
            '.jsx': 'javascript',
            '.md': 'markdown',
            '.txt': 'text',
            '.json': 'json',
            '.yml': 'yaml',
            '.yaml': 'yaml',
            '.toml': 'toml',
            '.cmake': 'cmake',
            '.sh': 'shell',
            '.bash': 'shell',
        }
    
    def initialize_database(self):
        """Initialize the SQLite database"""
        self.conn = sqlite3.connect(self.db_path)
        self.cursor = self.conn.cursor()
        
        # Create tables
        self.cursor.execute('''
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            size INTEGER NOT NULL,
            modified_time REAL NOT NULL,
            file_type TEXT NOT NULL,
            language TEXT NOT NULL,
            lines INTEGER NOT NULL,
            sha256 TEXT NOT NULL,
            indexed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
        ''')
        
        self.cursor.execute('''
        CREATE TABLE IF NOT EXISTS functions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            file_path TEXT NOT NULL,
            line_start INTEGER NOT NULL,
            line_end INTEGER NOT NULL,
            return_type TEXT,
            parameters TEXT,  -- JSON array
            is_method BOOLEAN DEFAULT 0,
            class_name TEXT,
            namespace TEXT,
            FOREIGN KEY (file_path) REFERENCES files (path) ON DELETE CASCADE
        )
        ''')
        
        self.cursor.execute('''
        CREATE TABLE IF NOT EXISTS classes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            file_path TEXT NOT NULL,
            line_start INTEGER NOT NULL,
            line_end INTEGER NOT NULL,
            base_classes TEXT,  -- JSON array
            namespace TEXT,
            FOREIGN KEY (file_path) REFERENCES files (path) ON DELETE CASCADE
        )
        ''')
        
        self.cursor.execute('''
        CREATE TABLE IF NOT EXISTS dependencies (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_file TEXT NOT NULL,
            target_file TEXT NOT NULL,
            dependency_type TEXT NOT NULL,
            line_number INTEGER NOT NULL,
            FOREIGN KEY (source_file) REFERENCES files (path) ON DELETE CASCADE
        )
        ''')
        
        self.cursor.execute('''
        CREATE TABLE IF NOT EXISTS cross_references (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_file TEXT NOT NULL,
            source_line INTEGER NOT NULL,
            target_file TEXT NOT NULL,
            target_line INTEGER,
            reference_type TEXT NOT NULL,  -- call, use, instantiate, etc.
            symbol_name TEXT
        )
        ''')
        
        # Create indexes for faster queries
        self.cursor.execute('CREATE INDEX IF NOT EXISTS idx_files_path ON files(path)')
        self.cursor.execute('CREATE INDEX IF NOT EXISTS idx_functions_name ON functions(name)')
        self.cursor.execute('CREATE INDEX IF NOT EXISTS idx_functions_file ON functions(file_path)')
        self.cursor.execute('CREATE INDEX IF NOT EXISTS idx_classes_name ON classes(name)')
        self.cursor.execute('CREATE INDEX IF NOT EXISTS idx_dependencies_source ON dependencies(source_file)')
        self.cursor.execute('CREATE INDEX IF NOT EXISTS idx_dependencies_target ON dependencies(target_file)')
        
        self.conn.commit()
    
    def scan_codebase(self, force_rescan: bool = False):
        """Scan the entire codebase and build the map"""
        print(f"Scanning codebase at: {self.root_path}")
        
        if force_rescan:
            print("Forcing rescan of entire codebase...")
        
        # Get all files
        all_files = []
        for ext in self.file_type_to_language.keys():
            pattern = f"**/*{ext}"
            files = list(self.root_path.glob(pattern))
            all_files.extend(files)
        
        # Also get files without extensions that might be important
        for pattern in ["**/CMakeLists.txt", "**/Makefile", "**/Dockerfile*", "**/.gitignore"]:
            files = list(self.root_path.glob(pattern))
            all_files.extend(files)
        
        # Remove duplicates and convert to strings
        file_paths = list(set(str(f.relative_to(self.root_path)) for f in all_files))
        total_files = len(file_paths)
        
        print(f"Found {total_files} files to process")
        
        # Process files in parallel
        with ThreadPoolExecutor(max_workers=4) as executor:
            futures = []
            for i, file_path in enumerate(file_paths):
                if i % 100 == 0:
                    print(f"Processing file {i}/{total_files}...")
                
                future = executor.submit(self._process_file, file_path, force_rescan)
                futures.append(future)
            
            # Wait for all to complete
            for future in futures:
                try:
                    future.result()
                except Exception as e:
                    print(f"Error processing file: {e}")
        
        # Build dependency graph
        self._build_dependency_graph()
        
        print(f"Codebase scan complete. Processed {len(self.files)} files.")
    
    def _process_file(self, relative_path: str, force_rescan: bool):
        """Process a single file"""
        full_path = self.root_path / relative_path
        
        if not full_path.exists():
            return
        
        # Get file info
        stat = full_path.stat()
        file_type = full_path.suffix.lower()
        language = self.file_type_to_language.get(file_type, 'unknown')
        
        # Calculate hash
        with open(full_path, 'rb') as f:
            content = f.read()
            sha256 = hashlib.sha256(content).hexdigest()
        
        # Count lines
        try:
            with open(full_path, 'r', encoding='utf-8', errors='ignore') as f:
                lines = sum(1 for _ in f)
        except:
            lines = 0
        
        file_info = FileInfo(
            path=relative_path,
            size=stat.st_size,
            modified_time=stat.st_mtime,
            file_type=file_type,
            language=language,
            lines=lines,
            sha256=sha256
        )
        
        # Check if we need to re-parse
        needs_parse = force_rescan
        if not force_rescan:
            # Check if file has changed
            self.cursor.execute(
                "SELECT sha256 FROM files WHERE path = ?",
                (relative_path,)
            )
            result = self.cursor.fetchone()
            if result is None or result[0] != sha256:
                needs_parse = True
        
        # Store file info
        self.files[relative_path] = file_info
        
        # Insert/update in database
        self.cursor.execute('''
        INSERT OR REPLACE INTO files 
        (path, size, modified_time, file_type, language, lines, sha256)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        ''', (
            relative_path,
            stat.st_size,
            stat.st_mtime,
            file_type,
            language,
            lines,
            sha256
        ))
        
        # Parse file if needed
        if needs_parse and language in ['cpp', 'go', 'python', 'javascript', 'typescript']:
            self._parse_file_content(relative_path, content.decode('utf-8', errors='ignore'))
        
        self.conn.commit()
    
    def _parse_file_content(self, file_path: str, content: str):
        """Parse file content to extract functions, classes, etc."""
        file_type = Path(file_path).suffix.lower().lstrip('.')
        
        if file_type in self.language_parsers:
            try:
                self.language_parsers[file_type](file_path, content)
            except Exception as e:
                print(f"Error parsing {file_path}: {e}")
    
    def _parse_cpp_file(self, file_path: str, content: str):
        """Parse C++ file for functions, classes, and includes"""
        lines = content.split('\n')
        
        # Extract includes
        for i, line in enumerate(lines):
            line = line.strip()
            if line.startswith('#include'):
                # Parse include statement
                match = re.match(r'#include\s+["<]([^">]+)[">]', line)
                if match:
                    target = match.group(1)
                    dep = Dependency(
                        source_file=file_path,
                        target_file=target,
                        dependency_type='include',
                        line_number=i+1
                    )
                    self.dependencies.append(dep)
        
        # Extract functions and classes (simplified regex-based approach)
        # This could be enhanced with a proper C++ parser
        current_namespace = ""
        in_class = False
        current_class = ""
        
        for i, line in enumerate(lines):
            line_stripped = line.strip()
            
            # Track namespaces
            if line_stripped.startswith('namespace'):
                match = re.match(r'namespace\s+(\w+)', line_stripped)
                if match:
                    current_namespace = match.group(1)
            
            # Detect class/struct definitions
            class_match = re.match(r'(class|struct)\s+(\w+)(?:\s*:\s*[^{]*)?\s*{', line_stripped)
            if class_match:
                class_type = class_match.group(1)
                class_name = class_match.group(2)
                in_class = True
                current_class = class_name
                
                # Find class end (simplified)
                class_start = i + 1
                brace_count = 1
                for j in range(i + 1, min(i + 1000, len(lines))):
                    if '{' in lines[j]:
                        brace_count += lines[j].count('{')
                    if '}' in lines[j]:
                        brace_count -= lines[j].count('}')
                    if brace_count == 0:
                        class_end = j + 1
                        break
                else:
                    class_end = i + 1
                
                class_info = ClassInfo(
                    name=class_name,
                    file_path=file_path,
                    line_start=class_start,
                    line_end=class_end,
                    namespace=current_namespace
                )
                
                if file_path not in self.classes:
                    self.classes[file_path] = []
                self.classes[file_path].append(class_info)
                
                # Store in database
                self.cursor.execute('''
                INSERT INTO classes (name, file_path, line_start, line_end, namespace, base_classes)
                VALUES (?, ?, ?, ?, ?, ?)
                ''', (
                    class_name,
                    file_path,
                    class_start,
                    class_end,
                    current_namespace,
                    json.dumps([])  # Empty base classes for now
                ))
            
            # Detect function definitions (simplified)
            func_match = re.match(
                r'(\w+(?:::\w+)*\s+)?(\w+)\s*\(([^)]*)\)\s*(?:const\s*)?(?:{|=\s*default|=\s*delete|override|final|\s*;)',
                line_stripped
            )
            if func_match:
                return_type = func_match.group(1) or "void"
                func_name = func_match.group(2)
                params = func_match.group(3) or ""
                
                # Parse parameters
                param_list = [p.strip() for p in params.split(',') if p.strip()]
                
                # Determine if it's a method
                is_method = in_class
                
                func_info = FunctionInfo(
                    name=func_name,
                    file_path=file_path,
                    line_start=i+1,
                    line_end=i+1,  # Simplified
                    return_type=return_type.strip(),
                    parameters=param_list,
                    is_method=is_method,
                    class_name=current_class if is_method else "",
                    namespace=current_namespace
                )
                
                if file_path not in self.functions:
                    self.functions[file_path] = []
                self.functions[file_path].append(func_info)
                
                # Store in database
                self.cursor.execute('''
                INSERT INTO functions 
                (name, file_path, line_start, line_end, return_type, parameters, is_method, class_name, namespace)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                ''', (
                    func_name,
                    file_path,
                    i+1,
                    i+1,
                    return_type.strip(),
                    json.dumps(param_list),
                    1 if is_method else 0,
                    current_class if is_method else "",
                    current_namespace
                ))
            
            # Reset class context when we leave class scope
            if line_stripped == '}' and in_class:
                in_class = False
                current_class = ""
    
    def _parse_go_file(self, file_path: str, content: str):
        """Parse Go file for functions and structs"""
        lines = content.split('\n')
        
        current_package = ""
        
        for i, line in enumerate(lines):
            line_stripped = line.strip()
            
            # Get package name
            if line_stripped.startswith('package '):
                current_package = line_stripped.replace('package ', '').strip()
            
            # Detect function definitions
            func_match = re.match(r'func\s+(?:\([^)]+\)\s+)?(\w+)\s*\(([^)]*)\)', line_stripped)
            if func_match:
                func_name = func_match.group(1)
                params = func_match.group(2) or ""
                
                # Parse parameters
                param_list = [p.strip() for p in params.split(',') if p.strip()]
                
                func_info = FunctionInfo(
                    name=func_name,
                    file_path=file_path,
                    line_start=i+1,
                    line_end=i+1,  # Simplified
                    return_type="",  # Go returns are after parameters
                    parameters=param_list,
                    is_method='(' in line_stripped and ')' in line_stripped,
                    class_name="",  # Go doesn't have classes
                    namespace=current_package
                )
                
                if file_path not in self.functions:
                    self.functions[file_path] = []
                self.functions[file_path].append(func_info)
                
                # Store in database
                self.cursor.execute('''
                INSERT INTO functions 
                (name, file_path, line_start, line_end, return_type, parameters, is_method, class_name, namespace)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                ''', (
                    func_name,
                    file_path,
                    i+1,
                    i+1,
                    "",
                    json.dumps(param_list),
                    1 if func_info.is_method else 0,
                    "",
                    current_package
                ))
            
            # Detect type definitions (structs)
            type_match = re.match(r'type\s+(\w+)\s+struct\s*{', line_stripped)
            if type_match:
                type_name = type_match.group(1)
                
                # Find struct end (simplified)
                struct_start = i + 1
                brace_count = 1
                for j in range(i + 1, min(i + 500, len(lines))):
                    if '{' in lines[j]:
                        brace_count += lines[j].count('{')
                    if '}' in lines[j]:
                        brace_count -= lines[j].count('}')
                    if brace_count == 0:
                        struct_end = j + 1
                        break
                else:
                    struct_end = i + 1
                
                class_info = ClassInfo(
                    name=type_name,
                    file_path=file_path,
                    line_start=struct_start,
                    line_end=struct_end,
                    namespace=current_package
                )
                
                if file_path not in self.classes:
                    self.classes[file_path] = []
                self.classes[file_path].append(class_info)
                
                # Store in database
                self.cursor.execute('''
                INSERT INTO classes (name, file_path, line_start, line_end, namespace, base_classes)
                VALUES (?, ?, ?, ?, ?, ?)
                ''', (
                    type_name,
                    file_path,
                    struct_start,
                    struct_end,
                    current_package,
                    json.dumps([])
                ))
    
    def _parse_python_file(self, file_path: str, content: str):
        """Parse Python file using ast module"""
        try:
            tree = ast.parse(content)
            
            for node in ast.walk(tree):
                if isinstance(node, ast.FunctionDef):
                    # Get function info
                    func_name = node.name
                    line_start = node.lineno
                    
                    # Get parameters
                    params = []
                    for arg in node.args.args:
                        params.append(arg.arg)
                    
                    # Check if it's a method
                    is_method = False
                    class_name = ""
                    parent = node.parent
                    while parent:
                        if isinstance(parent, ast.ClassDef):
                            is_method = True
                            class_name = parent.name
                            break
                        parent = getattr(parent, 'parent', None)
                    
                    func_info = FunctionInfo(
                        name=func_name,
                        file_path=file_path,
                        line_start=line_start,
                        line_end=node.end_lineno if hasattr(node, 'end_lineno') else line_start,
                        return_type="",  # Python doesn't have explicit return types in definition
                        parameters=params,
                        is_method=is_method,
                        class_name=class_name,
                        namespace=""
                    )
                    
                    if file_path not in self.functions:
                        self.functions[file_path] = []
                    self.functions[file_path].append(func_info)
                    
                    # Store in database
                    self.cursor.execute('''
                    INSERT INTO functions 
                    (name, file_path, line_start, line_end, return_type, parameters, is_method, class_name, namespace)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                    ''', (
                        func_name,
                        file_path,
                        line_start,
                        func_info.line_end,
                        "",
                        json.dumps(params),
                        1 if is_method else 0,
                        class_name,
                        ""
                    ))
                
                elif isinstance(node, ast.ClassDef):
                    class_name = node.name
                    line_start = node.lineno
                    line_end = node.end_lineno if hasattr(node, 'end_lineno') else line_start
                    
                    # Get base classes
                    base_classes = []
                    for base in node.bases:
                        if isinstance(base, ast.Name):
                            base_classes.append(base.id)
                    
                    class_info = ClassInfo(
                        name=class_name,
                        file_path=file_path,
                        line_start=line_start,
                        line_end=line_end,
                        base_classes=base_classes,
                        namespace=""
                    )
                    
                    if file_path not in self.classes:
                        self.classes[file_path] = []
                    self.classes[file_path].append(class_info)
                    
                    # Store in database
                    self.cursor.execute('''
                    INSERT INTO classes (name, file_path, line_start, line_end, namespace, base_classes)
                    VALUES (?, ?, ?, ?, ?, ?)
                    ''', (
                        class_name,
                        file_path,
                        line_start,
                        line_end,
                        "",
                        json.dumps(base_classes)
                    ))
        
        except SyntaxError as e:
            print(f"Syntax error parsing Python file {file_path}: {e}")
    
    def _parse_js_file(self, file_path: str, content: str):
        """Parse JavaScript/TypeScript file (simplified)"""
        lines = content.split('\n')
        
        for i, line in enumerate(lines):
            line_stripped = line.strip()
            
            # Detect function definitions
            func_match = re.match(
                r'(?:export\s+)?(?:async\s+)?(?:function\s+|const\s+|let\s+|var\s+)?(\w+)\s*=\s*(?:async\s*)?\(([^)]*)\)\s*=>',
                line_stripped
            ) or re.match(
                r'(?:export\s+)?(?:async\s+)?function\s+(\w+)\s*\(([^)]*)\)',
                line_stripped
            )
            
            if func_match:
                func_name = func_match.group(1)
                params = func_match.group(2) or ""
                
                # Parse parameters
                param_list = [p.strip() for p in params.split(',') if p.strip()]
                
                func_info = FunctionInfo(
                    name=func_name,
                    file_path=file_path,
                    line_start=i+1,
                    line_end=i+1,
                    return_type="",
                    parameters=param_list,
                    is_method=False,
                    class_name="",
                    namespace=""
                )
                
                if file_path not in self.functions:
                    self.functions[file_path] = []
                self.functions[file_path].append(func_info)
                
                # Store in database
                self.cursor.execute('''
                INSERT INTO functions 
                (name, file_path, line_start, line_end, return_type, parameters, is_method, class_name, namespace)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                ''', (
                    func_name,
                    file_path,
                    i+1,
                    i+1,
                    "",
                    json.dumps(param_list),
                    0,
                    "",
                    ""
                ))
            
            # Detect class definitions
            class_match = re.match(r'(?:export\s+)?class\s+(\w+)', line_stripped)
            if class_match:
                class_name = class_match.group(1)
                
                # Find class end (simplified)
                class_start = i + 1
                brace_count = 1
                for j in range(i + 1, min(i + 200, len(lines))):
                    if '{' in lines[j]:
                        brace_count += lines[j].count('{')
                    if '}' in lines[j]:
                        brace_count -= lines[j].count('}')
                    if brace_count == 0:
                        class_end = j + 1
                        break
                else:
                    class_end = i + 1
                
                class_info = ClassInfo(
                    name=class_name,
                    file_path=file_path,
                    line_start=class_start,
                    line_end=class_end,
                    namespace=""
                )
                
                if file_path not in self.classes:
                    self.classes[file_path] = []
                self.classes[file_path].append(class_info)
                
                # Store in database
                self.cursor.execute('''
                INSERT INTO classes (name, file_path, line_start, line_end, namespace, base_classes)
                VALUES (?, ?, ?, ?, ?, ?)
                ''', (
                    class_name,
                    file_path,
                    class_start,
                    class_end,
                    "",
                    json.dumps([])
                ))
    
    def _parse_ts_file(self, file_path: str, content: str):
        """Parse TypeScript file (same as JS for now)"""
        self._parse_js_file(file_path, content)
    
    def _build_dependency_graph(self):
        """Build dependency graph from parsed information"""
        # Store dependencies in database
        for dep in self.dependencies:
            self.cursor.execute('''
            INSERT INTO dependencies (source_file, target_file, dependency_type, line_number)
            VALUES (?, ?, ?, ?)
            ''', (
                dep.source_file,
                dep.target_file,
                dep.dependency_type,
                dep.line_number
            ))
        
        self.conn.commit()
    
    def search_files(self, query: str, file_type: str = None) -> List[Dict]:
        """Search for files by name or content"""
        results = []
        
        if file_type:
            self.cursor.execute('''
            SELECT path, file_type, language, lines 
            FROM files 
            WHERE path LIKE ? AND file_type = ?
            LIMIT 50
            ''', (f'%{query}%', file_type))
        else:
            self.cursor.execute('''
            SELECT path, file_type, language, lines 
            FROM files 
            WHERE path LIKE ?
            LIMIT 50
            ''', (f'%{query}%',))
        
        for row in self.cursor.fetchall():
            results.append({
                'path': row[0],
                'file_type': row[1],
                'language': row[2],
                'lines': row[3]
            })
        
        return results
    
    def search_functions(self, name: str, class_name: str = None) -> List[Dict]:
        """Search for functions by name"""
        results = []
        
        if class_name:
            self.cursor.execute('''
            SELECT name, file_path, line_start, line_end, return_type, class_name, namespace
            FROM functions
            WHERE name LIKE ? AND class_name LIKE ?
            LIMIT 50
            ''', (f'%{name}%', f'%{class_name}%'))
        else:
            self.cursor.execute('''
            SELECT name, file_path, line_start, line_end, return_type, class_name, namespace
            FROM functions
            WHERE name LIKE ?
            LIMIT 50
            ''', (f'%{name}%',))
        
        for row in self.cursor.fetchall():
            results.append({
                'name': row[0],
                'file': row[1],
                'line_start': row[2],
                'line_end': row[3],
                'return_type': row[4],
                'class': row[5],
                'namespace': row[6]
            })
        
        return results
    
    def get_file_dependencies(self, file_path: str) -> Dict:
        """Get all dependencies for a file"""
        result = {
            'file': file_path,
            'includes': [],
            'imports': [],
            'used_by': []
        }
        
        # Get dependencies FROM this file
        self.cursor.execute('''
        SELECT target_file, dependency_type, line_number
        FROM dependencies
        WHERE source_file = ?
        ''', (file_path,))
        
        for row in self.cursor.fetchall():
            dep_type = row[1]
            if dep_type == 'include':
                result['includes'].append({
                    'file': row[0],
                    'line': row[2]
                })
            else:
                result['imports'].append({
                    'file': row[0],
                    'line': row[2]
                })
        
        # Get dependencies TO this file
        self.cursor.execute('''
        SELECT source_file, dependency_type, line_number
        FROM dependencies
        WHERE target_file = ?
        ''', (file_path,))
        
        for row in self.cursor.fetchall():
            result['used_by'].append({
                'file': row[0],
                'type': row[1],
                'line': row[2]
            })
        
        return result
    
    def get_codebase_stats(self) -> Dict:
        """Get statistics about the codebase"""
        stats = {}
        
        # File counts by language
        self.cursor.execute('''
        SELECT language, COUNT(*) as count
        FROM files
        GROUP BY language
        ORDER BY count DESC
        ''')
        
        stats['files_by_language'] = {}
        for row in self.cursor.fetchall():
            stats['files_by_language'][row[0]] = row[1]
        
        # Total files
        self.cursor.execute('SELECT COUNT(*) FROM files')
        stats['total_files'] = self.cursor.fetchone()[0]
        
        # Total functions
        self.cursor.execute('SELECT COUNT(*) FROM functions')
        stats['total_functions'] = self.cursor.fetchone()[0]
        
        # Total classes
        self.cursor.execute('SELECT COUNT(*) FROM classes')
        stats['total_classes'] = self.cursor.fetchone()[0]
        
        # Total lines (estimated)
        self.cursor.execute('SELECT SUM(lines) FROM files')
        total_lines = self.cursor.fetchone()[0] or 0
        stats['total_lines'] = total_lines
        
        return stats
    
    def close(self):
        """Close the database connection"""
        if self.conn:
            self.conn.close()


class FuegoCodebaseMapperServer:
    """MCP Server for Fuego Codebase Mapper"""
    
    def __init__(self):
        self.server = Server("fuego-codebase-mapper")
        self.mapper = None
        self.setup_handlers()
    
    def setup_handlers(self):
        """Set up MCP server handlers"""
        
        @self.server.list_tools()
        async def handle_list_tools():
            return [
                types.Tool(
                    name="scan_codebase",
                    description="Scan the entire Fuego codebase and build the map",
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
                    name="search_files",
                    description="Search for files in the codebase",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "query": {
                                "type": "string",
                                "description": "Search query (filename or path)"
                            },
                            "file_type": {
                                "type": "string",
                                "description": "Filter by file type (e.g., .cpp, .go, .py)"
                            }
                        },
                        "required": ["query"]
                    }
                ),
                types.Tool(
                    name="search_functions",
                    description="Search for functions in the codebase",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "name": {
                                "type": "string",
                                "description": "Function name to search for"
                            },
                            "class_name": {
                                "type": "string",
                                "description": "Filter by class name"
                            }
                        },
                        "required": ["name"]
                    }
                ),
                types.Tool(
                    name="get_file_dependencies",
                    description="Get dependencies for a specific file",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "file_path": {
                                "type": "string",
                                "description": "Path to the file (relative to codebase root)"
                            }
                        },
                        "required": ["file_path"]
                    }
                ),
                types.Tool(
                    name="get_codebase_stats",
                    description="Get statistics about the codebase",
                    inputSchema={
                        "type": "object",
                        "properties": {}
                    }
                ),
                types.Tool(
                    name="get_file_structure",
                    description="Get the file structure of the codebase",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "max_depth": {
                                "type": "number",
                                "description": "Maximum depth to traverse",
                                "default": 3
                            }
                        }
                    }
                )
            ]
        
        @self.server.call_tool()
        async def handle_call_tool(name: str, arguments: dict):
            if not self.mapper:
                # Initialize with default path
                self.mapper = CodebaseMapper("/Users/aejt/fuego")
                self.mapper.initialize_database()
            
            if name == "scan_codebase":
                force_rescan = arguments.get("force_rescan", False)
                self.mapper.scan_codebase(force_rescan)
                return [
                    types.TextContent(
                        type="text",
                        text=f"Codebase scan complete. Processed {len(self.mapper.files)} files."
                    )
                ]
            
            elif name == "search_files":
                query = arguments["query"]
                file_type = arguments.get("file_type")
                results = self.mapper.search_files(query, file_type)
                
                if not results:
                    return [types.TextContent(type="text", text="No files found.")]
                
                output = "## Search Results:\n"
                for result in results:
                    output += f"- **{result['path']}** ({result['file_type']}, {result['language']}, {result['lines']} lines)\n"
                
                return [types.TextContent(type="text", text=output)]
            
            elif name == "search_functions":
                func_name = arguments["name"]
                class_name = arguments.get("class_name")
                results = self.mapper.search_functions(func_name, class_name)
                
                if not results:
                    return [types.TextContent(type="text", text="No functions found.")]
                
                output = "## Function Search Results:\n"
                for result in results:
                    class_info = f" in class {result['class']}" if result['class'] else ""
                    namespace_info = f" ({result['namespace']})" if result['namespace'] else ""
                    output += f"- **{result['name']}**{class_info}{namespace_info}\n"
                    output += f"  File: {result['file']}:{result['line_start']}\n"
                    if result['return_type']:
                        output += f"  Returns: {result['return_type']}\n"
                
                return [types.TextContent(type="text", text=output)]
            
            elif name == "get_file_dependencies":
                file_path = arguments["file_path"]
                deps = self.mapper.get_file_dependencies(file_path)
                
                output = f"## Dependencies for {file_path}\n\n"
                
                if deps['includes']:
                    output += "### Includes:\n"
                    for inc in deps['includes']:
                        output += f"- {inc['file']} (line {inc['line']})\n"
                
                if deps['imports']:
                    output += "\n### Imports:\n"
                    for imp in deps['imports']:
                        output += f"- {imp['file']} (line {imp['line']})\n"
                
                if deps['used_by']:
                    output += "\n### Used By:\n"
                    for used in deps['used_by']:
                        output += f"- {used['file']} ({used['type']}, line {used['line']})\n"
                
                return [types.TextContent(type="text", text=output)]
            
            elif name == "get_codebase_stats":
                stats = self.mapper.get_codebase_stats()
                
                output = "## Codebase Statistics\n\n"
                output += f"**Total Files:** {stats['total_files']}\n"
                output += f"**Total Lines:** {stats['total_lines']:,}\n"
                output += f"**Total Functions:** {stats['total_functions']}\n"
                output += f"**Total Classes:** {stats['total_classes']}\n\n"
                
                output += "### Files by Language:\n"
                for lang, count in stats['files_by_language'].items():
                    output += f"- {lang}: {count}\n"
                
                return [types.TextContent(type="text", text=output)]
            
            elif name == "get_file_structure":
                max_depth = arguments.get("max_depth", 3)
                # Simplified file structure
                structure = self._get_file_structure(max_depth)
                return [types.TextContent(type="text", text=structure)]
            
            return [types.TextContent(type="text", text=f"Unknown tool: {name}")]
    
    def _get_file_structure(self, max_depth: int) -> str:
        """Get file structure as text"""
        import os
        
        def build_tree(path, depth=0):
            if depth > max_depth:
                return ""
            
            indent = "  " * depth
            result = ""
            
            try:
                items = os.listdir(path)
                dirs = []
                files = []
                
                for item in items:
                    if item.startswith('.'):
                        continue
                    item_path = os.path.join(path, item)
                    if os.path.isdir(item_path):
                        dirs.append(item)
                    else:
                        # Only show certain file types
                        if item.endswith(('.cpp', '.h', '.go', '.py', '.md', '.txt', 'CMakeLists.txt', 'Makefile')):
                            files.append(item)
                
                # Sort
                dirs.sort()
                files.sort()
                
                for d in dirs:
                    result += f"{indent}📁 {d}/\n"
                    result += build_tree(os.path.join(path, d), depth + 1)
                
                for f in files:
                    result += f"{indent}📄 {f}\n"
            
            except PermissionError:
                pass
            
            return result
        
        root = "/Users/aejt/fuego"
        return f"File structure of {root} (max depth: {max_depth}):\n\n{build_tree(root)}"
    
    async def run(self):
        """Run the MCP server"""
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
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Fuego Codebase Mapper")
    parser.add_argument("--scan", action="store_true", help="Scan the codebase")
    parser.add_argument("--server", action="store_true", help="Run as MCP server")
    parser.add_argument("--path", type=str, default="/Users/aejt/fuego", help="Path to codebase")
    parser.add_argument("--search", type=str, help="Search for files")
    parser.add_argument("--stats", action="store_true", help="Show codebase statistics")
    
    args = parser.parse_args()
    
    mapper = CodebaseMapper(args.path)
    mapper.initialize_database()
    
    if args.scan:
        print("Starting codebase scan...")
        mapper.scan_codebase()
        print("Scan complete!")
    
    elif args.search:
        results = mapper.search_files(args.search)
        print(f"Search results for '{args.search}':")
        for result in results:
            print(f"- {result['path']} ({result['file_type']}, {result['lines']} lines)")
    
    elif args.stats:
        stats = mapper.get_codebase_stats()
        print("Codebase Statistics:")
        print(f"  Total Files: {stats['total_files']}")
        print(f"  Total Lines: {stats['total_lines']:,}")
        print(f"  Total Functions: {stats['total_functions']}")
        print(f"  Total Classes: {stats['total_classes']}")
        print("\nFiles by Language:")
        for lang, count in stats['files_by_language'].items():
            print(f"  {lang}: {count}")
    
    elif args.server:
        print("Starting MCP server...")
        server = FuegoCodebaseMapperServer()
        import asyncio
        asyncio.run(server.run())
    
    else:
        # Default: scan and show stats
        print("Scanning codebase...")
        mapper.scan_codebase()
        stats = mapper.get_codebase_stats()
        print(f"\nCodebase mapped: {stats['total_files']} files, {stats['total_lines']:,} lines")
        print("\nRun with --server to start the MCP server")
        print("Run with --search 'query' to search for files")
        print("Run with --stats to show statistics")
    
    mapper.close()


if __name__ == "__main__":
    main()