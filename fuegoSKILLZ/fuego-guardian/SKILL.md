---
name: fuego-guardian
description: Recursive multi-agent code verification for the Fuego blockchain codebase. Use when new code is added, a PR is opened, pre-commit verification is needed, or a security audit is requested. Trigger phrases - "verify this code", "check correctness", "audit fuego", "review security", "fuego guardian".
---

# Fuego Guardian — Recursive Multi-Agent Code Verification

Recursive multi-agent system that verifies correctness and security of new code added to the Fuego blockchain codebase. Uses hierarchical agent decomposition with adversarial validation.

## When to Use

Activate this skill when:
- New code is added to any Fuego module (C++, Go, Solidity, Rust, Python, TypeScript)
- A PR is opened against the Fuego repository
- Pre-commit verification is needed before pushing changes
- Manual security audit or correctness review is requested
- Continuous integration verification pipeline runs
- User asks "verify this code", "check correctness", "audit fuego", "review security"

## Integration with OKOC

This skill integrates with OKOC's skill invocation protocol. When OKOC is active and code modifications are detected in the Fuego codebase, this skill auto-triggers. Add to OKOC's default invocation set by placing this skill in the OKOC skill directory or referencing it from OKOC's trigger list.

## Architecture Overview

```
                        ┌─────────────────────────┐
                        │   Guardian Supervisor    │
                        │   (Strategy + Routing)   │
                        └───────────┬─────────────┘
                                    │
                    ┌───────────────┼───────────────┐
                    │               │               │
                    ▼               ▼               ▼
        ┌──────────────────┐ ┌──────────┐ ┌──────────────────┐
        │  Domain Verifiers │ │ Security │ │  Adversarial     │
        │  (8 specialists)  │ │ Auditor  │ │  Validator       │
        └──────────────────┘ │  (007)   │ │  (Red Team)      │
                              └──────────┘ └──────────────────┘
                                    │               │
                                    ▼               ▼
                        ┌─────────────────────────┐
                        │    Consensus Arbiter     │
                        │  (Weighted Voting +      │
                        │   Debate Resolution)     │
                        └───────────┬─────────────┘
                                    │
                                    ▼
                        ┌─────────────────────────┐
                        │   Verification Report    │
                        │  (Score + Veredito +     │
                        │   Required Fixes)        │
                        └─────────────────────────┘
```

## Agent Definitions

### Layer 1: Guardian Supervisor

**Role**: Strategy, decomposition, routing, result aggregation
**Context**: Full project overview via graphify
**Tools**: graphify query, graphify path, graphify explain, bash (git diff, file listing)
**System Prompt Core**:
```
You are the Fuego Guardian Supervisor. Your role is to:
1. Receive verification requests (file paths, diff ranges, PR descriptions)
2. Identify which domains the changes impact (crypto, consensus, p2p, wallet, swap, contracts, rpc, core)
3. Decompose verification into parallel subtasks — one per affected domain
4. Route each subtask to the appropriate domain specialist
5. Set recursion depth based on change complexity (0=file only, 1=+direct callers, 2=+indirect callers)
6. Collect specialist findings and forward to adversarial validator
7. Synthesize final report from consensus arbiter output
8. Output schema: JSON with findings, scores, veredito, and fix requirements

CRITICAL RULES:
- Never skip the adversarial validation phase
- Never accept a specialist finding without cross-referencing with graphify
- For cryptographic code, always route to BOTH crypto-verifier AND 007
- For consensus changes, always run full recursion depth=2
- Set TTL=300s per verification subtask to prevent infinite loops
```

**Communication Protocol**:
- Uses instruction passing to specialists (lean context, not full delegation)
- Specialists return structured JSON findings
- Uses file system checkpointing via `graphify-out/guardian/checkpoints/` for state persistence

### Layer 2: Domain Specialists

#### 2.0 Heat/Hearth Verifier
**Domain**: HEAT stablecoin mint/burn, HEARTH AMM operations, treasury logic
**Focus**: HEAT mint validation, PI controller, basin discovery, constant product AMM math, treasury allocation, oracle integrity
**Tools**: graphify path (trace heat → currency, heat → swaps), bash (run CoreTests), read
**Checklist**:
- [ ] HEAT mint amount matches XFG burned at redemption price (no over/under mint)
- [ ] 50/50 eternal flame / treasury split is exact (no rounding loss)
- [ ] Treasury 60/40 LP/peg split is exact
- [ ] Mint premium (500 bps) applied correctly
- [ ] PI controller: proportional+integral term computed in integer arithmetic (no float)
- [ ] Hill damping: sigmoid applied correctly at extreme deviations
- [ ] Basin discovery: bootstrap/observe/lock/exit transitions valid
- [ ] HEARTH AMM constant product invariant holds after every swap (`xfg × heat = k`)
- [ ] LP share mint/burn is proportional to `sqrt(Δx × Δy)` (no share inflation)
- [ ] Pool reserve markers (`POLX`/`POLH`/`SWRX`) never spendable by wallet
- [ ] Overcollateralization gate fires at correct ratio (1.5x min)
- [ ] `heat_metrics` RPC returns self-consistent data (supply = minted - burned)
- [ ] Oracle: multi-pair TWAP uses correct time windows, stale pair rejection

#### 2.1 Crypto Verifier
**Domain**: `src/crypto/`, cryptographic primitives
**Focus**: MLSAG ring signatures, Pedersen commitments, Musig2, adaptor signatures, DLEQ proofs, subaddresses, hash functions (CryptoNight, Keccak, Blake, Skein, JH, Groestl), secp256k1 operations
**Tools**: graphify path (trace crypto dependencies), bash (compile crypto tests), read
**Checklist**:
- [ ] All secret key operations use constant-time algorithms
- [ ] Zeroization of sensitive key material after use (check for explicit_bzero/memset_s)
- [ ] Ring signature input selection uses OSPEAD correctly (no bias)
- [ ] Pedersen commitment blinding factors are properly random
- [ ] Musig2 signing rounds follow the spec (nonce commitment → partial sig → aggregate)
- [ ] Adaptor signature claim/refund paths are symmetric and complete
- [ ] DLEQ proofs verify before accepting commitments
- [ ] No timing side channels in hash function calls
- [ ] secp256k1 operations use libsecp256k1's constant-time API (not custom implementations)
- [ ] Random number generation uses cryptographically secure RNG (not rand())
- [ ] No hardcoded test vectors in production code paths
- [ ] Subaddress derivation follows CryptoNote spec exactly

#### 2.2 Consensus Verifier
**Domain**: `src/CryptoNoteCore/`, block validation, difficulty, emission
**Focus**: Block/transaction validation, PiController difficulty adjustment, emission schedule, transaction pool, blockchain state transitions, deposit index, commitment index, banking index
**Tools**: graphify path (trace consensus → crypto), bash (run CoreTests)
**Checklist**:
- [ ] Block reward calculation matches emission schedule at every height
- [ ] Difficulty adjustment formula is integer-exact (no floating point)
- [ ] Transaction validation order follows spec (inputs before outputs, fees before rewards)
- [ ] Double-spend protection across all transaction types
- [ ] Block size limits enforced correctly (miner tx excluded from calculation)
- [ ] Timestamp validation: blocks within network-adjusted time window
- [ ] Cumulative difficulty accumulation is monotonic (never decreases)
- [ ] Transaction pool eviction on block acceptance removes exactly the included txs
- [ ] Deposit/commitment/banking indices stay consistent after chain switches
- [ ] AMM pool state updates atomically with block acceptance
- [ ] Staged unlock processing handles all unlock conditions
- [ ] Integer overflow protection on all arithmetic (sum of inputs, fee calculation, reward)
- [ ] Genesis block parameters match config exactly (no drift)
- [ ] Checkpoint hashes for known-good blocks are correct

#### 2.3 P2P Verifier
**Domain**: `src/P2p/`, `src/CryptoNoteProtocol/`, networking
**Focus**: Levin protocol, peer discovery, block/transaction relay, UPnP, Tor/I2P/Meshtastic integration, message serialization
**Tools**: graphify path (trace p2p → system), read
**Checklist**:
- [ ] Incoming message size limits enforced before allocation
- [ ] Peer connection count limits enforced (max inbound, max outbound)
- [ ] Message deserialization handles malformed payloads (no crash on invalid)
- [ ] Ban scoring thresholds are reasonable and trigger bans
- [ ] Peer list persistence handles corrupt peer data files
- [ ] Block relay: first send compact (header), then full on request
- [ ] Transaction relay flood protection (no relay of already-seen txs)
- [ ] Timeouts on all network operations (connect, read, write)
- [ ] No unbounded memory growth from peer message queues
- [ ] Tor/I2P proxy authentication is configured correctly
- [ ] Meshtastic message size respects radio packet limits

#### 2.4 Wallet Verifier
**Domain**: `src/Wallet/`, `src/WalletLegacy/`, `src/SimpleWallet/`, `src/PaymentGate/`
**Focus**: Key management, transaction building, RPC server, payment gateway, mnemonic seed phrases, fusion transactions
**Tools**: graphify path (trace wallet → crypto, wallet → rpc), bash (run wallet tests)
**Checklist**:
- [ ] Private keys never logged, never serialized to plaintext
- [ ] Mnemonic seed generation uses CSPRNG with sufficient entropy
- [ ] Wallet file encryption uses authenticated encryption (not just AES-CBC)
- [ ] Transaction building: change address is always a wallet-owned address
- [ ] Fusion transaction mixing preserves privacy (no information leak in output structure)
- [ ] Payment gateway RPC endpoints require authentication
- [ ] Wallet balance calculation cannot underflow (unsigned arithmetic)
- [ ] Key derivation path follows BIP32/BIP44 correctly
- [ ] No key material in memory dumps (zeroization after use)
- [ ] Import/export operations validate input format before processing
- [ ] Dust output handling: outputs below dust threshold are added to fee

#### 2.5 Swap Verifier
**Domain**: `src/SwapDaemon/`, `contracts/`, `swapxfg/`
**Focus**: Atomic swap state machine, HTLC contracts (Solidity, Rust, Bitcoin Cash script), adaptor signatures, price oracle, swap P2P, Monero/Ethereum/Solana/BCH integrations
**Tools**: graphify path (trace swap → crypto, swap → wallet), read
**Checklist**:
- [ ] Swap state machine transitions are valid and complete (no dead states)
- [ ] HTLC timelocks: claimer timeout > refunder timeout (by safety margin)
- [ ] Hash lock preimage is revealed only by the claiming party
- [ ] Adaptor signature claim uses correct discrete log relationship
- [ ] Refund path works when counterparty abandons (no fund loss)
- [ ] Price oracle data is signed and timestamped (no stale prices)
- [ ] Swap amounts are validated against oracle (no negative, no overflow)
- [ ] Cross-chain transaction IDs are verified before state advance
- [ ] Ethereum HTLC: Solidity contract handles reentrancy, gas limits
- [ ] Solana HTLC: Rust program account validation is complete
- [ ] BCH HTLC: Bitcoin Script correctly implements HASH160 + CSV
- [ ] Swap database handles crash recovery (WAL or atomic writes)
- [ ] Concurrent swap limits prevent resource exhaustion

#### 2.6 Smart Contract Verifier
**Domain**: `contracts/`, `src/SwapDaemon/Ethereum/`, `src/SwapDaemon/Solana/`
**Focus**: Solidity contracts, Rust Solana programs, EVM safety, gas optimization
**Tools**: read (contracts/*.sol, *.rs)
**Checklist (Solidity)**:
- [ ] No reentrancy vulnerabilities (checks-effects-interactions pattern)
- [ ] No integer overflow/underflow (Solidity ^0.8.x built-in or SafeMath)
- [ ] Access control: only authorized accounts can call sensitive functions
- [ ] No tx.origin for authorization (use msg.sender)
- [ ] No delegatecall to untrusted contracts
- [ ] Events emitted for all state-changing operations
- [ ] Gas optimization: storage vs memory, packed variables
- [ ] Constructor is not front-runnable (initialization protection)
**Checklist (Solana/Rust)**:
- [ ] Account ownership validation on all CPI inputs
- [ ] Signer verification for sensitive operations
- [ ] Account data deserialization error handling
- [ ] PDA derivation is deterministic and correct
- [ ] No missing closing of temporary accounts

#### 2.7 Code Quality Reviewer
**Domain**: All source files
**Focus**: Clean code, SOLID principles, naming conventions, code duplication, error handling, documentation
**Tools**: read, bash (git diff)
**Checklist**:
- [ ] Functions are small and single-purpose (<50 lines preferred)
- [ ] No magic numbers (use named constants or enums)
- [ ] Error paths are handled (no swallowed exceptions/errors)
- [ ] RAII used for resource management (no naked new/delete)
- [ ] No raw pointer ownership transfers (use smart pointers)
- [ ] Const-correctness: methods that don't modify state are const
- [ ] Thread safety: shared state uses appropriate synchronization
- [ ] No dead code or commented-out code
- [ ] Logging uses appropriate levels (debug for diagnostics, error for failures)
- [ ] Naming follows project conventions (camelCase, PascalCase, snake_case)
- [ ] Header include order: own header → project headers → external headers → system headers
- [ ] No `using namespace` in header files
- [ ] No platform-specific code without `#ifdef` guards

#### 2.8 Security Auditor (007 Integrated)
**Domain**: All source files, configurations, dependencies
**Focus**: Complete security audit using 007 methodology
**Mode**: Auto-activates when security-sensitive code is modified
**Checklist**: 007's 6-phase audit (Fase 1-6):
- **Fase 1 — Attack Surface Mapping**: Identify all inputs, trust boundaries, secrets, execution points
- **Fase 2 — Threat Model (STRIDE + PASTA)**: Spoofing, Tampering, Repudiation, Info Disclosure, DoS, Elevation
- **Fase 3 — Technical Checklist**: Secrets management, input validation, authN/authZ, cryptography, supply chain
- **Fase 4 — Red Team**: Attack each vulnerability vector with 7 attacker personas
- **Fase 5 — Blue Team**: Propose architectural fixes, guardrails, sandboxing, monitoring
- **Fase 6 — Scoring + Veredito**: Quantitative 0-100 score across 8 domains

**007's non-negotiable principles applied to Fuego**:
1. Zero Trust: All P2P messages, RPC inputs, and API calls are untrusted
2. No Hardcoded Secrets: Check for keys, tokens, passwords in source
3. Sandboxed Execution: External code execution (swap HTLC interactions) must be sandboxed
4. Bounded Automation: Mining, swap daemon, wallet RPC have TTL/cost limits
5. Isolated Agents: Verify agent boundaries in MCP servers, Python analysis tools
6. Assume Breach: Every module designed with compromise assumption
7. Fail Secure: On error, wallet → locked state, swap → refund path, node → safe shutdown
8. Audit Everything: All state-changing operations are logged

### Layer 3: Adversarial Validator

**Role**: Attempts to find vulnerabilities the specialists missed
**Context**: Receives all specialist findings as input
**Personas**: Cycles through 7 attacker personas from 007's Red Team:
1. Malicious RPC caller
2. Malicious P2P peer
3. Compromised swap counterparty
4. Supply chain attacker (modified dependency)
5. Operator error (misconfiguration)
6. Insider with source access
7. Economic attacker (manipulating AMM/CD markets)
**Tools**: All specialist tools, graphify
**Output**: List of vulnerabilities with severity, likelihood, and whether each specialist caught them (false negative analysis)

### Layer 4: Consensus Arbiter

**Role**: Resolves conflicts between specialist findings using weighted voting + debate
**Input**: All specialist reports + adversarial validation
**Protocol**:
1. **Independent Assessment**: Each specialist assigns severity 0-10 to every finding
2. **Weighted Voting**: Weights based on domain relevance (Crypto Verifier ×3 for crypto findings, Security Auditor ×3 for security findings, etc.)
3. **Conflict Detection**: If specialist verdicts diverge by >3 points on severity scale
4. **Debate Resolution**: Conflicting specialists critique each other's reasoning over 2 rounds max
5. **Final Score**: Weighted average after debate resolution
6. **Tiebreaker**: Security Auditor (007) has tiebreaker vote for security findings; Consensus Verifier for correctness findings

**Weight Matrix**:
| Finding Domain | Crypto | Consensus | Security | P2P | Wallet | Swap | Contracts | Quality |
|---------------|--------|-----------|----------|-----|--------|------|-----------|---------|
| Cryptography  | ×3     | ×1        | ×2       | ×1  | ×1     | ×1   | ×1        | ×1      |
| Consensus     | ×1     | ×3        | ×2       | ×1  | ×1     | ×1   | ×1        | ×1      |
| Security      | ×2     | ×2        | ×3       | ×2  | ×2     | ×2   | ×2        | ×1      |
| P2P/Network   | ×1     | ×1        | ×2       | ×3  | ×1     | ×1   | ×1        | ×1      |
| Wallet        | ×2     | ×1        | ×3       | ×1  | ×3     | ×1   | ×1        | ×1      |
| Swap          | ×2     | ×1        | ×2       | ×1  | ×1     | ×3   | ×2        | ×1      |
| Contracts     | ×2     | ×1        | ×2       | ×1  | ×1     | ×2   | ×3        | ×1      |
| Code Quality  | ×1     | ×1        | ×1       | ×1  | ×1     | ×1   | ×1        | ×3      |

## Recursive Verification Protocol

### Recursion Depth Configuration

| Depth | Scope | Trigger | Max Agents |
|-------|-------|---------|------------|
| 0 | Changed files only | Single-file edit | 2 (domain + security) |
| 1 | Changed files + direct callers/callees | Multi-file change | 4 (relevant domains) |
| 2 | +Indirect callers (2 hops) | Consensus or crypto change | 6 (all affected) |
| 3 | +Full community traversal | Critical security fix | 8 (all specialists) |

### Recursion Control

```
recursion_limit = 3  # hard cap
ttl_per_agent = 300  # seconds before timeout
token_budget = 50000  # per verification run

function verify(scope, depth=0):
    if depth > recursion_limit:
        return "MAX_DEPTH_REACHED"
    
    findings = run_all_applicable_specialists(scope, depth)
    adversarial = run_adversarial_validator(findings)
    conflicts = detect_conflicts(findings + adversarial)
    
    if conflicts and depth < 2:
        expanded_scope = graphify_trace(scope, depth + 1)
        deeper_findings = verify(expanded_scope, depth + 1)
        findings = merge(findings, deeper_findings)
    
    return consensus_arbiter.resolve(findings)
```

### Dependency Tracing via Graphify

For each changed file at recursion depth N, trace dependencies:
```bash
# Trace what calls this code (upstream)
graphify path "<changed_function>" "callers" --depth N

# Trace what this code depends on (downstream)
graphify path "<changed_function>" "callees" --depth N

# Trace data flow
graphify query "what data flows through <changed_function>" --budget 20
```

## Verification Workflow

### Manual Trigger
```
User: "verify the new adaptor signature implementation"
  → Guardian Supervisor decomposes
    → Crypto Verifier: check MLSAG + adaptor sig math
    → Swap Verifier: check state machine integration
    → Security Auditor (007): check key handling, constant-time
    → Adversarial Validator: try to forge signatures
  → Consensus Arbiter resolves
  → Report generated
```

### Auto-Trigger (OKOC Integration)
When OKOC detects code changes in the Fuego codebase:
1. Check modified file paths against domain map
2. Route to Guardian Supervisor
3. Run at appropriate recursion depth
4. If CRITICAL finding: block further changes, alert user
5. If HIGH finding: warn with fix suggestions
6. If MEDIUM/LOW: log for review queue

### CI/CD Integration
```bash
# In GitHub Actions or pre-commit hook:
opencode run fuego-guardian verify --files "$CHANGED_FILES" --depth 1
```

## Report Format

```json
{
  "verification_id": "uuid",
  "timestamp": "ISO8601",
  "scope": {
    "files": ["path/to/file1.cpp", "path/to/file2.h"],
    "depth": 2,
    "domains": ["crypto", "swap"]
  },
  "summary": {
    "total_findings": 12,
    "critical": 0,
    "high": 2,
    "medium": 5,
    "low": 3,
    "info": 2,
    "score": 72,
    "veredito": "APPROVED_WITH_RESERVATIONS"
  },
  "findings": [
    {
      "id": "F-001",
      "severity": "HIGH",
      "domain": "cryptography",
      "title": "Missing zeroization of secret key after adaptor signature generation",
      "file": "src/crypto/adaptor_signatures.cpp",
      "line": 247,
      "description": "The secret key 'sk_adaptor' is allocated on stack but not zeroized before function return",
      "fix": "Add explicit_bzero(sk_adaptor, sizeof(sk_adaptor)) before return at line 312",
      "detected_by": "Crypto Verifier",
      "missed_by": ["Swap Verifier"],
      "stride_category": "Information Disclosure"
    }
  ],
  "adversarial_findings": [
    {
      "id": "ADV-001",
      "severity": "HIGH",
      "title": "Timing side channel in scalar multiplication",
      "description": "Adversarial validator identified non-constant-time path in scalar_mult for specific input values",
      "specialist_false_negative": true,
      "missed_by": ["Crypto Verifier"]
    }
  ],
  "required_fixes": [
    {"finding_id": "F-001", "blocking": true},
    {"finding_id": "ADV-001", "blocking": true}
  ],
  "veredito_details": {
    "score": 72,
    "category": "APPROVED_WITH_RESERVATIONS",
    "blocking_issues": 2,
    "can_proceed_after": ["Fix F-001", "Fix ADV-001"]
  }
}
```

## Domain → File Mapping

Used by Supervisor for routing:

| Domain | File Patterns |
|--------|--------------|
| heat | `src/CryptoNoteCore/HeatMintEngine*`, `src/CryptoNoteCore/TransactionExtra*`, `src/Rpc/RpcServer*` (heat_metrics) |
| crypto | `src/crypto/**`, `include/*Crypto*` |
| consensus | `src/CryptoNoteCore/**`, `src/CryptoNoteProtocol/**` |
| p2p | `src/P2p/**`, `src/System/**`, `src/ConnectivityTool/**` |
| wallet | `src/Wallet/**`, `src/WalletLegacy/**`, `src/SimpleWallet/**`, `src/PaymentGate/**`, `src/Transfers/**` |
| swap | `src/SwapDaemon/**`, `swapxfg/**` |
| contracts | `contracts/**`, `src/SwapDaemon/Ethereum/**`, `src/SwapDaemon/Solana/**` |
| rpc | `src/Rpc/**`, `src/JsonRpcServer/**`, `src/NodeRpcProxy/**` |
| core | `src/Daemon/**`, `src/Common/**`, `src/Serialization/**`, `src/Logging/**` |
| build | `CMakeLists.txt`, `src/CMakeLists.txt`, `Makefile`, `**.cmake` |
| docs | `docs/**`, `README.md` |
| tests | `tests/**` |
| scripts | `scripts/**`, `*.py`, `*.sh` |
| tui | `tui/**` |
| deps | `external/**` |

## Skill Trigger Map (for OKOC auto-invocation)

OKOC monitors these file patterns and auto-invokes Guardian:

| File Pattern Changed | Trigger | Depth |
|---------------------|---------|-------|
| `src/crypto/**` | Immediate (CRITICAL path) | 2 |
| `src/CryptoNoteCore/HeatMintEngine*` | Immediate (HEAT mint — funds at risk) | 2 |
| `src/CryptoNoteCore/TransactionExtra*` | Immediate (commitment tags — incorrect split) | 2 |
| `src/Rpc/RpcServer*` (heat_metrics) | On heat_metrics change | 1 |
| `src/CryptoNoteCore/**` | Immediate (consensus) | 2 |
| `src/P2p/**` | Immediate (network surface) | 1 |
| `src/Wallet*/**` | Immediate (key material) | 2 |
| `src/SwapDaemon/**` | Immediate (cross-chain funds) | 2 |
| `contracts/**` | Immediate (immutable deploy) | 2 |
| `src/Rpc/**` | Immediate (API surface) | 1 |
| `CMakeLists.txt` | On build change | 1 |
| `tests/**` | On test change | 0 |
| `docs/**` | Deferred | 0 |

## Integration with Existing Fuego Tooling

| Tool | Integration Point |
|------|------------------|
| **graphify** | Dependency tracing, community traversal, function relationship queries |
| **fuego_blockchain_specialist.py** | Domain knowledge injection for all specialists |
| **analyze_fuego_code.py** | Static analysis baseline before multi-agent verification |
| **codebase_mapper.py** | File→module→domain routing table |
| **mcp/fuego-mcp-server/** | TypeScript MCP tool for agent access to codebase context |
| **Google Test (gtest)** | Run relevant test suites after verification, compare results |
| **GitHub Actions CI** | Verification gate in CI pipeline |
| **Docker monitoring** | Observe verification metrics (time per domain, false negatives) |

## Failure Modes and Mitigations

| Failure Mode | Detection | Mitigation |
|-------------|-----------|------------|
| Supervisor context saturation | Monitor context size, checkpoint every 3 sub-tasks | File system checkpointing, restart from checkpoint |
| Specialist sycophancy (agreeing with others) | Consensus arbiter detects low variance in scores | Force debate protocol, inject adversarial test case |
| Token budget exhaustion | Track cumulative tokens per verification | Hard cap at 50000 tokens, truncate lowest-priority checks |
| Recursive explosion (depth → ∞) | Depth counter | Hard limit at depth=3, force return |
| Specialist timeout | TTL=300s per specialist | Kill and replace with backup specialist, note in report |
| Graphify data staleness | Compare file mtimes with graph timestamp | Auto-run `graphify update .` before verification |
| Adversarial validator false positives | Track adversarial precision over time | Human review for all adversarial HIGH+ findings |
| Cross-language blind spots | Language detection via file extension | Route to language-capable specialist pool |

## Test Scenarios

Run these to validate the multi-agent system:

1. **Crypto correctness test**: Introduce a subtle MLSAG ring index bias, verify Crypto Verifier catches it
2. **Security false negative test**: Add a hardcoded key in wallet code, verify 007 catches it before Quality Reviewer
3. **Consensus fork test**: Change emission formula by 1 satoshi offset, verify Consensus Verifier detects drift
4. **Swap fund loss test**: Swap HTLC timelock with claimer_timeout < refunder_timeout, verify Swap Verifier blocks
5. **Adversarial bypass test**: Fix a vulnerability but leave a related one, verify Adversarial Validator finds the sibling
6. **P2P DoS test**: Unbounded message queue growth, verify P2P Verifier catches
7. **Race condition test**: Shared state without mutex, verify Quality Reviewer catches
8. **Context isolation test**: Run all 8 specialists simultaneously, verify no cross-contamination

## Quick Start

```bash
# Full verification of changed files
opencode run fuego-guardian verify --files "$(git diff --name-only HEAD~1)" --depth 1

# Domain-specific verification
opencode run fuego-guardian verify --domain crypto --depth 2

# Adversarial-only validation (for already-verified code)
opencode run fuego-guardian adversarial --report last_verification.json

# CI pre-commit hook
opencode run fuego-guardian verify --pre-commit --depth 1
```

<!-- Metadata: created 2026-05-13, v1.0.0, requires graphify + 007 + code-reviewer -->
