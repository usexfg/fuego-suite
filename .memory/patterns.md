# Fuego Agent Memory - Patterns

## Context Routing

| Task Needed | Load This |
|------------|----------|
| CD interest calculation | `fuego-blockchain-specialist` skill |
| Atomic swap mechanics | `fuego-blockchain-specialist` skill |
| File/function search | `fuego-codebase-mapper` skill |
| Code semantic search | `fuego-rag` skill |
| Dependency graph | Read `.dsp/` directory |
| Full reference | Read `FUEGO.md` |
| Raw source | Search in `src/` |

## Effective Usage Tips

1. Start with root `CLAUDE.md` - it auto-loads
2. For deep mechanics analysis, load `fuego-blockchain-specialist` skill
3. For code search, use `fuego-codebase-mapper` or `fuego-rag` skills
4. For dependency navigation, use `.dsp/` directory
5. Code in `src/` is always the source of truth