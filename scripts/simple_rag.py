#!/usr/bin/env python3
"""
Simple RAG System for Fuego - No External Dependencies
"""

import os
import sys
import json
import hashlib
from pathlib import Path

class SimpleFuegoRAG:
    def __init__(self, project_root):
        self.project_root = Path(project_root)
        self.chunks = []
    
    def build_simple_index(self):
        """Build a simple text-based index"""
        index_file = self.project_root / "rag_simple_index.json"
        
        if index_file.exists():
            print("Loading existing index...")
            with open(index_file, 'r') as f:
                self.chunks = json.load(f)
        else:
            print("Building new index...")
            self._create_index()
            with open(index_file, 'w') as f:
                json.dump(self.chunks, f, indent=2)
        
        print(f"Index ready with {len(self.chunks)} chunks")
    
    def _create_index(self):
        """Create a simple index of key Fuego files"""
        key_files = [
            "README.md",
            "docs/commitment-types.md",
            "src/CryptoNoteCore/Currency.cpp",
            "src/CryptoNoteCore/Blockchain.cpp",
            "swapxfg/main.go",
            "tui/main.go.original"
        ]
        
        for file_path in key_files:
            full_path = self.project_root / file_path
            if full_path.exists():
                try:
                    content = full_path.read_text(encoding='utf-8', errors='ignore')
                    # Simple chunking
                    lines = content.split('\n')
                    for i in range(0, len(lines), 20):  # 20 lines per chunk
                        chunk = lines[i:i+20]
                        if chunk:
                            self.chunks.append({
                                'path': file_path,
                                'content': '\n'.join(chunk),
                                'line_start': i+1,
                                'line_end': i+len(chunk)
                            })
                except Exception as e:
                    print(f"Error reading {file_path}: {e}")
    
    def search(self, query):
        """Simple keyword search"""
        query_words = query.lower().split()
        results = []
        
        for chunk in self.chunks:
            score = 0
            content_lower = chunk['content'].lower()
            
            for word in query_words:
                if word in content_lower:
                    score += 1
            
            if score > 0:
                results.append({
                    'score': score,
                    'chunk': chunk
                })
        
        results.sort(key=lambda x: x['score'], reverse=True)
        return [r['chunk'] for r in results[:3]]

# Example usage
if __name__ == "__main__":
    # Get current directory if no argument provided
    project_path = sys.argv[1] if len(sys.argv) > 1 else "."
    
    rag = SimpleFuegoRAG(project_path)
    rag.build_simple_index()
    
    print("\n" + "="*60)
    print("SIMPLE RAG SYSTEM FOR FUEGO")
    print("="*60)
    print("\nExample queries:")
    print("  - CD interest calculation")
    print("  - swap fees")
    print("  - commitment deposit")
    print("  - CryptoNote protocol")
    print("  - Type 'quit' to exit")
    print("="*60)
    
    while True:
        query = input("\nQuery: ").strip()
        if query.lower() in ['quit', 'exit', 'q']:
            break
        
        results = rag.search(query)
        print(f"\nFound {len(results)} results for: {query}")
        
        for i, result in enumerate(results):
            print(f"\n[{i+1}] {result['path']} (lines {result['line_start']}-{result['line_end']})")
            print(f"    Preview: {result['content'][:150]}...")
        
        if results:
            print("\n" + "-"*60)
            print("CONTEXT FOR LLM:")
            print("-"*60)
            context = ""
            for i, result in enumerate(results):
                context += f"\n--- Source {i+1}: {result['path']} ---\n"
                context += result['content'] + "\n"
            
            prompt = f"""Based on the Fuego codebase context below, answer: {query}

{context}

Answer specifically about Fuego blockchain implementation:"""
            
            print(prompt)
            print("-"*60)