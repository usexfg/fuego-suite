#!/usr/bin/env python3
"""
RAG System for Fuego Blockchain Codebase
"""

import os
import sys
from pathlib import Path
import json
from typing import List, Dict, Any
import hashlib

# RAG Implementation for Fuego
class FuegoRAGSystem:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.documents = []
        self.chunks = []
        
    def discover_documents(self) -> List[Path]:
        """Discover all relevant documents in the Fuego project"""
        documents = []
        
        # Source code files
        code_extensions = {'.cpp', '.hpp', '.h', '.cc', '.c', '.go', '.py'}
        for ext in code_extensions:
            for file_path in self.project_root.rglob(f'*{ext}'):
                if '.git' not in str(file_path):
                    documents.append(file_path)
        
        # Documentation files
        doc_extensions = {'.md', '.txt', '.rst'}
        for ext in doc_extensions:
            for file_path in self.project_root.rglob(f'*{ext}'):
                if '.git' not in str(file_path):
                    documents.append(file_path)
        
        # Specific important files
        important_files = ['README.md', 'CMakeLists.txt', 'Makefile', 'go.mod', 'docs/']
        for file_pattern in important_files:
            file_path = self.project_root / file_pattern
            if file_path.exists():
                if file_path.is_dir():
                    documents.extend(file_path.rglob('*.md'))
                else:
                    documents.append(file_path)
        
        return list(set(documents))  # Remove duplicates
    
    def read_document(self, file_path: Path) -> Dict[str, Any]:
        """Read a document and extract metadata"""
        try:
            content = file_path.read_text(encoding='utf-8', errors='ignore')
            
            return {
                'id': hashlib.md5(str(file_path).encode()).hexdigest()[:16],
                'path': str(file_path.relative_to(self.project_root)),
                'content': content,
                'type': self._get_document_type(file_path),
                'size': len(content),
                'lines': content.count('\n') + 1
            }
        except Exception as e:
            print(f"Error reading {file_path}: {e}")
            return None
    
    def _get_document_type(self, file_path: Path) -> str:
        """Determine document type based on extension and location"""
        ext = file_path.suffix.lower()
        
        if 'docs/' in str(file_path):
            return 'documentation'
        elif 'src/' in str(file_path) and ext in {'.cpp', '.hpp', '.h', '.c', '.cc'}:
            return 'cpp_source'
        elif 'swapxfg/' in str(file_path) and ext == '.go':
            return 'go_source'
        elif 'tui/' in str(file_path) and ext == '.go':
            return 'tui_go_source'
        elif ext in {'.md', '.txt', '.rst'}:
            return 'documentation'
        elif ext in {'.py'}:
            return 'python_script'
        else:
            return 'other'
    
    def semantic_chunking(self, document: Dict[str, Any]) -> List[Dict[str, Any]]:
        """Chunk document based on semantic boundaries"""
        content = document['content']
        chunks = []
        
        # Different chunking strategies based on document type
        if document['type'] == 'cpp_source':
            # Chunk by functions/classes for C++ code
            chunks = self._chunk_cpp_code(content)
        elif document['type'] == 'go_source':
            # Chunk by functions for Go code
            chunks = self._chunk_go_code(content)
        elif document['type'] == 'documentation':
            # Chunk by sections for documentation
            chunks = self._chunk_markdown(content)
        else:
            # Generic chunking by paragraphs
            chunks = self._chunk_by_paragraphs(content)
        
        # Add metadata to each chunk
        for i, chunk_content in enumerate(chunks):
            chunk_id = f"{document['id']}-chunk-{i:03d}"
            chunks[i] = {
                'id': chunk_id,
                'document_id': document['id'],
                'path': document['path'],
                'type': document['type'],
                'content': chunk_content,
                'chunk_index': i,
                'total_chunks': len(chunks)
            }
        
        return chunks
    
    def _chunk_cpp_code(self, content: str) -> List[str]:
        """Chunk C++ code by functions and classes"""
        lines = content.split('\n')
        chunks = []
        current_chunk = []
        in_function = False
        brace_count = 0
        
        for line in lines:
            current_chunk.append(line)
            
            # Count braces to detect function boundaries
            brace_count += line.count('{') - line.count('}')
            
            if '{' in line and not in_function:
                # Start of a function/class
                in_function = True
            
            if brace_count == 0 and in_function and current_chunk:
                # End of function/class
                chunks.append('\n'.join(current_chunk))
                current_chunk = []
                in_function = False
            elif len(current_chunk) >= 50:  # Max 50 lines per chunk
                chunks.append('\n'.join(current_chunk))
                current_chunk = []
        
        if current_chunk:
            chunks.append('\n'.join(current_chunk))
        
        return chunks
    
    def _chunk_go_code(self, content: str) -> List[str]:
        """Chunk Go code by functions"""
        lines = content.split('\n')
        chunks = []
        current_chunk = []
        in_function = False
        brace_count = 0
        
        for line in lines:
            current_chunk.append(line)
            
            # Count braces
            brace_count += line.count('{') - line.count('}')
            
            if 'func ' in line and '{' in line and not in_function:
                in_function = True
            
            if brace_count == 0 and in_function and current_chunk:
                chunks.append('\n'.join(current_chunk))
                current_chunk = []
                in_function = False
            elif len(current_chunk) >= 50:
                chunks.append('\n'.join(current_chunk))
                current_chunk = []
        
        if current_chunk:
            chunks.append('\n'.join(current_chunk))
        
        return chunks
    
    def _chunk_markdown(self, content: str) -> List[str]:
        """Chunk markdown by headers"""
        lines = content.split('\n')
        chunks = []
        current_chunk = []
        current_header = None
        
        for line in lines:
            if line.startswith('#') and len(line.strip()) > 1:
                # New header found
                if current_chunk:
                    chunks.append('\n'.join(current_chunk))
                current_chunk = [line]
                current_header = line
            else:
                current_chunk.append(line)
            
            if len('\n'.join(current_chunk)) > 1000:  # ~1000 chars per chunk
                chunks.append('\n'.join(current_chunk))
                current_chunk = [current_header] if current_header else []
        
        if current_chunk:
            chunks.append('\n'.join(current_chunk))
        
        return chunks
    
    def _chunk_by_paragraphs(self, content: str) -> List[str]:
        """Generic chunking by paragraphs"""
        paragraphs = content.split('\n\n')
        chunks = []
        current_chunk = []
        current_size = 0
        
        for para in paragraphs:
            para_size = len(para)
            if current_size + para_size > 1000 and current_chunk:
                chunks.append('\n\n'.join(current_chunk))
                current_chunk = [para]
                current_size = para_size
            else:
                current_chunk.append(para)
                current_size += para_size
        
        if current_chunk:
            chunks.append('\n\n'.join(current_chunk))
        
        return chunks
    
    def build_rag_index(self):
        """Build the RAG index from all documents"""
        print("Discovering documents...")
        document_paths = self.discover_documents()
        print(f"Found {len(document_paths)} documents")
        
        print("\nReading documents...")
        for i, doc_path in enumerate(document_paths[:50]):  # Limit for demo
            print(f"  [{i+1}/{min(50, len(document_paths))}] {doc_path.relative_to(self.project_root)}")
            document = self.read_document(doc_path)
            if document:
                self.documents.append(document)
                
                # Chunk the document
                chunks = self.semantic_chunking(document)
                self.chunks.extend(chunks)
        
        print(f"\nProcessed {len(self.documents)} documents into {len(self.chunks)} chunks")
        
        # Save metadata
        self._save_metadata()
    
    def _save_metadata(self):
        """Save metadata about the RAG index"""
        metadata = {
            'project_root': str(self.project_root),
            'document_count': len(self.documents),
            'chunk_count': len(self.chunks),
            'documents_by_type': {},
            'chunks_by_type': {}
        }
        
        # Count by type
        for doc in self.documents:
            doc_type = doc['type']
            metadata['documents_by_type'][doc_type] = metadata['documents_by_type'].get(doc_type, 0) + 1
        
        for chunk in self.chunks:
            chunk_type = chunk['type']
            metadata['chunks_by_type'][chunk_type] = metadata['chunks_by_type'].get(chunk_type, 0) + 1
        
        # Save to file
        metadata_path = self.project_root / 'rag_metadata.json'
        with open(metadata_path, 'w') as f:
            json.dump(metadata, f, indent=2)
        
        print(f"\nMetadata saved to: {metadata_path}")
        print(f"Document types: {dict(metadata['documents_by_type'])}")
        print(f"Chunk types: {dict(metadata['chunks_by_type'])}")
    
    def search_chunks(self, query: str, top_k: int = 5) -> List[Dict[str, Any]]:
        """Simple keyword search implementation (basic version)"""
        query_terms = query.lower().split()
        scored_chunks = []
        
        for chunk in self.chunks:
            score = 0
            content_lower = chunk['content'].lower()
            
            # Simple keyword matching
            for term in query_terms:
                if term in content_lower:
                    score += 1
            
            # Boost score for certain document types based on query
            if any(term in ['cd', 'certificate', 'deposit', 'interest'] for term in query_terms):
                if chunk['type'] in ['cpp_source', 'documentation']:
                    score += 2
            
            if score > 0:
                scored_chunks.append((score, chunk))
        
        # Sort by score and return top k
        scored_chunks.sort(key=lambda x: x[0], reverse=True)
        return [chunk for _, chunk in scored_chunks[:top_k]]
    
    def generate_response(self, query: str, chunks: List[Dict[str, Any]]) -> str:
        """Generate response using retrieved chunks"""
        context = ""
        
        for i, chunk in enumerate(chunks):
            context += f"\n--- Source {i+1} ({chunk['path']}) ---\n"
            context += chunk['content'][:500] + "\n"  # Limit context
        
        prompt = f"""Based on the following Fuego codebase context, answer the query.

Query: {query}

Context from codebase:
{context}

Instructions:
1. Answer specifically about Fuego blockchain implementation
2. Reference source files when possible
3. Be precise about CD interest calculations if relevant
4. If information is incomplete, say so

Answer:"""
        
        return prompt

def main():
    """Main function to run the RAG system"""
    project_root = input("Enter Fuego project path (default: current directory): ").strip()
    if not project_root:
        project_root = os.getcwd()
    
    print(f"\nInitializing RAG system for: {project_root}")
    rag_system = FuegoRAGSystem(project_root)
    
    # Build index
    rag_system.build_rag_index()
    
    # Interactive query loop
    print("\n" + "="*60)
    print("RAG SYSTEM READY - Enter queries about Fuego codebase")
    print("Type 'quit' to exit")
    print("="*60)
    
    while True:
        query = input("\nQuery: ").strip()
        if query.lower() in ['quit', 'exit', 'q']:
            break
        
        print(f"\nSearching for: {query}")
        chunks = rag_system.search_chunks(query, top_k=3)
        
        if not chunks:
            print("No relevant chunks found.")
            continue
        
        print(f"\nFound {len(chunks)} relevant chunks:")
        for i, chunk in enumerate(chunks):
            print(f"\n[{i+1}] {chunk['path']} (Type: {chunk['type']})")
            print(f"    Preview: {chunk['content'][:200]}...")
        
        # Generate response
        print("\n" + "="*60)
        print("GENERATED RESPONSE:")
        print("="*60)
        prompt = rag_system.generate_response(query, chunks)
        print(prompt)
        print("="*60)

if __name__ == "__main__":
    main()