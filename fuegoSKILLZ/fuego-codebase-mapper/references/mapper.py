import os
import json
import sqlite3
import hashlib
from pathlib import Path
from dataclasses import dataclass
from typing import Dict, List, Optional
import re
from concurrent.futures import ThreadPoolExecutor


@dataclass
class FileInfo:
    path: str
    size: int
    modified_time: float
    file_type: str
    language: str
    lines: int
    sha256: str


class CodebaseMapper:
    def __init__(self, root_path: str = "/Users/aejt/fuego"):
        self.root_path = Path(root_path).absolute()
        self.db_path = self.root_path / ".codebase_map.db"
        self.conn = None
        self.cursor = None
        self.files: Dict[str, FileInfo] = {}

    def initialize_database(self):
        self.conn = sqlite3.connect(self.db_path)
        self.cursor = self.conn.cursor()
        self.cursor.execute("""
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            size INTEGER NOT NULL,
            modified_time REAL NOT NULL,
            file_type TEXT NOT NULL,
            language TEXT NOT NULL,
            lines INTEGER NOT NULL,
            sha256 TEXT NOT NULL
        )""")
        self.cursor.execute("""
        CREATE TABLE IF NOT EXISTS functions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            file_path TEXT NOT NULL,
            line_start INTEGER NOT NULL,
            line_end INTEGER NOT NULL,
            return_type TEXT,
            parameters TEXT,
            is_method INTEGER DEFAULT 0,
            class_name TEXT
        )""")
        self.cursor.execute("""
        CREATE TABLE IF NOT EXISTS classes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            file_path TEXT NOT NULL,
            line_start INTEGER NOT NULL,
            line_end INTEGER NOT NULL,
            namespace TEXT
        )""")
        self.conn.commit()

    def scan_codebase(self):
        file_type_map = {
            ".cpp": "cpp", ".h": "cpp", ".hpp": "cpp", ".cc": "cpp",
            ".c": "c", ".go": "go", ".py": "python",
            ".js": "javascript", ".ts": "typescript",
            ".md": "markdown", ".json": "json"
        }
        all_files = []
        for ext in file_type_map.keys():
            all_files.extend(self.root_path.glob(f"**/*{ext}"))
        all_files = [f for f in all_files if ".git" not in str(f)]
        for f in all_files:
            rel_path = str(f.relative_to(self.root_path))
            stat = f.stat()
            sha256 = hashlib.sha256(f.read_bytes()).hexdigest()
            lines = sum(1 for _ in f.read_text(errors="ignore").split("\n"))
            file_type = f.suffix.lower()
            language = file_type_map.get(file_type, "unknown")
            self.files[rel_path] = FileInfo(
                path=rel_path, size=stat.st_size,
                modified_time=stat.st_mtime,
                file_type=file_type, language=language,
                lines=lines, sha256=sha256
            )
            self.cursor.execute(
                "INSERT OR REPLACE INTO files VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (rel_path, stat.st_size, stat.st_mtime, file_type, language, lines, sha256)
            )
        self.conn.commit()
        self._parse_code_files()

    def _parse_code_files(self):
        for rel_path, file_info in self.files.items():
            if file_info.language in ["cpp", "python"]:
                self._parse_file(rel_path)

    def _parse_file(self, rel_path: str):
        full_path = self.root_path / rel_path
        content = full_path.read_text(errors="ignore")
        if not content:
            return
        lines = content.split("\n")
        for i, line in enumerate(lines):
            line = line.strip()
            if line.startswith("#include"):
                match = re.match(r'#include\s+["<]([^">]+)[">]', line)
                if match:
                    pass
            func_match = re.match(r"(?:void|int|uint64_t|bool|string)\s+(\w+)\s*\(", line)
            if func_match:
                func_name = func_match.group(1)
                self.cursor.execute(
                    "INSERT INTO functions (name, file_path, line_start, line_end) VALUES (?, ?, ?, ?)",
                    (func_name, rel_path, i + 1, i + 1)
                )

    def search_files(self, query: str, limit: int = 20) -> List[Dict]:
        self.cursor.execute(
            "SELECT path, file_type, language, lines FROM files WHERE path LIKE ? LIMIT ?",
            (f"%{query}%", limit)
        )
        return [{"path": r[0], "type": r[1], "language": r[2], "lines": r[3]}
                for r in self.cursor.fetchall()]

    def search_functions(self, name: str, limit: int = 20) -> List[Dict]:
        self.cursor.execute(
            "SELECT name, file_path, line_start, line_end FROM functions WHERE name LIKE ? LIMIT ?",
            (f"%{name}%", limit)
        )
        return [{"name": r[0], "file": r[1], "line": r[2]} for r in self.cursor.fetchall()]

    def get_stats(self) -> Dict:
        self.cursor.execute("SELECT COUNT(*) FROM files")
        total_files = self.cursor.fetchone()[0]
        self.cursor.execute("SELECT SUM(lines) FROM files")
        total_lines = self.cursor.fetchone()[0] or 0
        self.cursor.execute("SELECT COUNT(*) FROM functions")
        total_functions = self.cursor.fetchone()[0]
        self.cursor.execute("SELECT language, COUNT(*) FROM files GROUP BY language")
        by_lang = dict(self.cursor.fetchall())
        return {
            "total_files": total_files,
            "total_lines": total_lines,
            "total_functions": total_functions,
            "files_by_language": by_lang
        }

    def get_file_tree(self, depth: int = 3) -> str:
        def build_tree(path: Path, indent: int = 0) -> str:
            if indent > depth:
                return ""
            result = ""
            try:
                items = sorted(path.iterdir())
                dirs = [i for i in items if i.is_dir() and not i.name.startswith(".")]
                files = [i for i in items if i.is_file() and i.suffix in {".cpp", ".h", ".go", ".py", ".md"}]
                for d in dirs:
                    result += f"{'  ' * indent}📁 {d.name}/\n"
                    result += build_tree(d, indent + 1)
                for f in files:
                    result += f"{'  ' * indent}📄 {f.name}\n"
            except PermissionError:
                pass
            return result
        return build_tree(self.root_path)

    def close(self):
        if self.conn:
            self.conn.close()