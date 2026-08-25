# Fuego Brainstorm

## Purpose

Structured brainstorming agent for the Fuego blockchain ecosystem. Loads the complete Fuego tech stack map, identifies cross-component synergies, and generates novel technological possibilities. Produces actionable ideas grounded in what the existing codebase already supports or could realistically extend.

## When to Use

- Brainstorming new features, protocols, or products for the Fuego ecosystem
- Exploring cross-component synergies (e.g., how HEAT stability mechanisms could enhance atomic swap UX)
- Evaluating whether a proposed idea is technically feasible within the existing stack
- Revisiting deprecated features for potential revival or repurposing
- Architecture discussions requiring full-ecosystem context

## Trigger Phrases

"fuego brainstorm", "fuego ideas", "what could we build on fuego", "fuego ecosystem possibilities", "brainstorm with fuego", "fuego feature ideas", "what's possible in fuego", "reuse deprecated fuego"

## Ecosystem Loading

Before any brainstorming session, load the full ecosystem map from `references/fuego-ecosystem-map.md`. This file contains every active component, protocol, data structure, and cross-reference in the Fuego codebase.

If the user requests deprecated feature exploration, additionally load `references/fuego-deprecated-catalog.md`. This is the toggleable subcategory — do NOT load it unless explicitly requested.

## Brainstorming Protocol

### Step 1: Scope Lock

Define the brainstorming boundary before generating ideas:

1. **Domain**: Which Fuego subsystems are in scope? (currency, swaps, HEAT, AMM, crypto, network, tx, wallet, mining, ZK, DIGM, SDK)
2. **Depth**: Conceptual (high-level ideas) or technical (implementation-level feasibility)
3. **Deprecated toggle**: Should deprecated/shelved features be included as potential revival candidates?
4. **Constraint set**: Any hard constraints? (e.g., must be chain-compatible, must not break existing API, must work within current consensus rules)

### Step 2: Cross-Map Analysis

With the ecosystem map loaded, systematically scan for:

1. **Unused combinations**: Component A has capability X, Component B has capability Y — has X+Y been explored?
2. **Borrowed patterns**: Pattern P works well in domain A — could it solve a problem in domain B?
3. **Gap identification**: Component A expects interface X from Component B, but X doesn't exist yet
4. **Extension points**: Where does the codebase have explicit extension hooks, plugin architectures, or modular boundaries?
5. **Deprecated revival**: Any deprecated feature whose underlying problem remains unsolved?

### Step 3: Idea Generation

For each identified opportunity, produce:

| Field | Content |
|-------|---------|
| **Name** | Concise feature/proposal name |
| **Components** | Which Fuego subsystems are involved |
| **Problem** | What problem this solves or what opportunity it exploits |
| **Mechanism** | How it would work (leveraging existing primitives) |
| **Feasibility** | Already possible / needs minor extension / needs new protocol / needs research |
| **Synergy** | How this amplifies existing Fuego capabilities |

### Step 4: Feasibility Classification

Classify each idea against the existing stack:

- **Green**: Can be built today with existing code and protocols. No consensus changes needed.
- **Yellow**: Requires extending existing components (new RPC endpoints, new transaction types, new wallet commands) but no fundamental protocol changes.
- **Red**: Requires consensus-level changes, new cryptographic primitives, or significant protocol redesign.

### Step 5: Priority Matrix

Rank ideas by:

1. **Impact**: How much does this advance Fuego's competitive position?
2. **Effort**: How much work to implement given current codebase state?
3. **Risk**: What could go wrong? Attack surface? Consensus breaking?
4. **Synergy score**: How many other Fuego components does this enhance?

## Ecosystem Quick Reference

### Active Core

| Layer | Components |
|-------|-----------|
| **Consensus** | CryptoNote PoW (CN-FastHash), DMWDA difficulty, 480s blocks |
| **Currency** | XFG (8M8 supply), HEAT (algorithmic flatcoin, $1.58 peg) |
| **CD System** | XFG CDs (6/18/36/72 epochs), HEAT CDs (permanent), 2% APY floor, 150% loyalty bonus |
| **Fee Pool** | 80% CD yield / 20% treasury; 1% atomic swap fee; 0.3% HEARTH swap fee |
| **AMM** | HEARTH AMM (constant product), orderbook (v11+), PI controller (removed → hard peg) |
| **Swaps** | Adaptor signatures (MuSig2/COMIT), AFK v2 (non-interactive), 6 chains (SOL/ETH/XMR/BCH/ARB/BASE) |
| **Privacy** | RingCT (MLSAG 8-32 mixin), Pedersen commitments, Dandelion++, I2P/Tor/Meshtastic(stub) |
| **ZK** | Stark/Winterfell (7 reg, 64 step), Merkle proof, EFier → SP1 path, zkLP design docs |
| **Wallet** | `fire` prefix, dual-key (spend+view), CLI/GUI/Flutter/Go TUI |
| **DIGM** | Decentralized music platform, Paradio streaming, PARA/CURA/DIGM tokens, 10 Rust crates |
| **SDK** | fuego-sdk (Rust/UniFFI), cross-platform (Kotlin/Swift/Dart) |
| **Network** | P2P (8 target peers), Dandelion++ (90% stem), swap gossip, MCP server |

### Key Extension Points

- **Transaction extra tags**: 0x08 (HEAT), 0xCD (COLD), 0xD5 (deposit secret), 0xF5-0xF9 (v12 auth), 0xFA-0xFC (orderbook)
- **RPC namespaces**: `wallet::`, `swap::`, `amm::`, `experimental::`
- **Chain clients**: Modular swap daemon with per-chain adapters (ETH/SOL/XMR/BCH)
- **Fork heights**: UPGRADE_HEIGHT_V11 (HEATWAVE), V12 (auth tags)
- **Deposit term markers**: HEAT_TERM (0xFFFFFFFF), LP (0xFFFFFFFD), POOL_XFG/POOL_HEAT
- **DIGM crates**: chunk-store, fuego-audio (Paradio), parapay, i2p-net, p2p-net, ffi-bridge

## Output Format

Always present brainstorming results as a structured table (Step 3 format), sorted by priority matrix score. Include the feasibility classification badge (Green/Yellow/Red) for each idea.

When the user asks to explore a specific idea deeper, expand into an architecture sketch showing which existing components connect and what new interfaces are needed.

## References

- `references/fuego-ecosystem-map.md` — Full ecosystem map with all components, protocols, constants, and cross-references
- `references/fuego-deprecated-catalog.md` — Catalog of 23 deprecated/experimental/shelved features (load only when deprecated toggle is active)

Base directory for this skill: /Users/aejt/.opencode/skills/fuego-brainstorm
Relative paths in this skill (e.g., references/) are relative to this base directory.
