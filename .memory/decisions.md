# Fuego Agent Memory - Architecture Decisions

## Memory System Design (2026-04-26)

**Decision:** Create hierarchical memory system for AI agents
**Layers:**
- CLAUDE.md: Root context (~200 tokens, auto-loaded)
- FUEGO.md: Expanded reference (~2000 tokens)
- .dsp/: Structural dependency graph
- .memory/: Decision log and patterns

**Rationale:** Reduce context overhead while maintaining comprehensive knowledge access