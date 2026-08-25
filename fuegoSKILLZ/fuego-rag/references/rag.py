import hashlib
from pathlib import Path
from typing import List, Dict, Any


class FuegoRAGSystem:
    def __init__(self, project_root: str = "/Users/aejt/fuego"):
        self.project_root = Path(project_root)
        self.documents = []
        self.chunks = []

    def discover_documents(self) -> List[Path]:
        documents = []
        code_extensions = {".cpp", ".hpp", ".h", ".cc", ".c", ".go", ".py"}
        for ext in code_extensions:
            for file_path in self.project_root.rglob(f"*{ext}"):
                if ".git" not in str(file_path):
                    documents.append(file_path)
        doc_extensions = {".md", ".txt", ".rst"}
        for ext in doc_extensions:
            for file_path in self.project_root.rglob(f"*{ext}"):
                if ".git" not in str(file_path):
                    documents.append(file_path)
        return list(set(documents))

    def read_document(self, file_path: Path) -> Dict[str, Any]:
        try:
            content = file_path.read_text(encoding="utf-8", errors="ignore")
            return {
                "id": hashlib.md5(str(file_path).encode()).hexdigest()[:16],
                "path": str(file_path.relative_to(self.project_root)),
                "content": content,
                "type": self._get_document_type(file_path),
                "size": len(content),
                "lines": content.count("\n") + 1,
            }
        except Exception as e:
            return None

    def _get_document_type(self, file_path: Path) -> str:
        ext = file_path.suffix.lower()
        if "docs/" in str(file_path):
            return "documentation"
        elif "src/" in str(file_path) and ext in {".cpp", ".hpp", ".h", ".c", ".cc"}:
            return "cpp_source"
        elif "swapxfg/" in str(file_path) and ext == ".go":
            return "go_source"
        elif "tui/" in str(file_path) and ext == ".go":
            return "tui_go_source"
        elif ext in {".md", ".txt", ".rst"}:
            return "documentation"
        elif ext in {".py"}:
            return "python_script"
        return "other"

    def semantic_chunking(self, document: Dict[str, Any]) -> List[Dict[str, Any]]:
        content = document["content"]
        chunks = []
        lines = content.split("\n")
        current_chunk = []
        brace_count = 0
        in_function = False

        for line in lines:
            current_chunk.append(line)
            brace_count += line.count("{") - line.count("}")
            if "{" in line and not in_function:
                in_function = True
            if brace_count == 0 and in_function and current_chunk:
                if len("\n".join(current_chunk)) > 50:
                    chunks.append("\n".join(current_chunk))
                    current_chunk = []
                    in_function = False

        if current_chunk and len("\n".join(current_chunk)) > 50:
            chunks.append("\n".join(current_chunk))

        return [
            {
                "id": f"{document['id']}-{i}",
                "document_id": document["id"],
                "path": document["path"],
                "type": document["type"],
                "content": chunk,
                "index": i,
            }
            for i, chunk in enumerate(chunks)
        ]

    def build_index(self, limit: int = 50):
        document_paths = self.discover_documents()
        for doc_path in document_paths[:limit]:
            document = self.read_document(doc_path)
            if document:
                self.documents.append(document)
                chunks = self.semantic_chunking(document)
                self.chunks.extend(chunks)
        return {"documents": len(self.documents), "chunks": len(self.chunks)}

    def search_chunks(self, query: str, top_k: int = 5) -> List[Dict[str, Any]]:
        query_terms = query.lower().split()
        scored_chunks = []

        for chunk in self.chunks:
            score = 0
            content_lower = chunk["content"].lower()
            for term in query_terms:
                if term in content_lower:
                    score += 1
            if any(term in ["cd", "certificate", "deposit", "interest"] for term in query_terms):
                if chunk["type"] in ["cpp_source", "documentation"]:
                    score += 2
            if score > 0:
                scored_chunks.append((score, chunk))

        scored_chunks.sort(key=lambda x: x[0], reverse=True)
        return [chunk for _, chunk in scored_chunks[:top_k]]

    def generate_response(self, query: str, chunks: List[Dict[str, Any]]) -> str:
        context = ""
        for i, chunk in enumerate(chunks):
            context += f"\n--- Source {i+1} ({chunk['path']}) ---\n"
            context += chunk["content"][:500] + "\n"

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