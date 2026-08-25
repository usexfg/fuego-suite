---
name: fuego-orchestrator
description: "Central router for Fuego multi-agent system. Analyzes queries, routes to specialized domain agents (currency/swaps/crypto/network/tx/wallet/miner), aggregates results. Auto-routes by default, parallel mode for complex queries."
risk: low
source: user-provided
---

# Fuego Orchestrator

Central supervisor that orchestrates the Fuego multi-agent system. Routes queries to specialized agents and aggregates results.

## Architecture

```
                    ┌─────────────────────────┐
                    │   fuego-orchestrator    │
                    │   (This agent)        │
                    └───────────┬─────────────┘
                                │
    ┌─────────┬─────────┬─────────┼─────────┬─────────┬─────────┐
    │         │         │         │         │         │         │
┌───▼──┐ ┌──▼──┐ ┌──▼──┐ ┌──▼──┐ ┌──▼──┐ ┌──▼──┐ ┌──▼───┐
│cur-   │ │swaps │ │crypto│ │net-  │ │tx   │ │wallet│ │miner │
│rency  │ │     │ │     │ │work │ │     │ │      │ │      │
└──────┘ └─────┘ └─────┘ └─────┘ └──────┘ └──────┘ └──────┘
```

## Supported Agents

| Agent | Scope | Triggers |
|-------|-------|---------|
| fuego-currency | CD interest, APY, deposits, tokenomics, fee pool, loyalty bonus | "interest", "APY", "deposit", "yield", "fee pool", "tokenomics", "loyalty bonus" |
| fuego-swaps | Atomic swaps, LP pools, adaptor signatures, HEARTH AMM, AFK swaps | "swap", "atomic swap", "LP pool", "adaptor", "hearth", "hearth_add", "afk" |
| fuego-heat-and-hearth | HEAT stablecoin, HEARTH AMM, mint, treasury, orderbook | "heat", "hearth", "mint_heat", "heat_metrics", "peg", "flatcoin", "orderbook" |
| fuego-crypto | Ed25519, MLSAG, Pedersen, MuSig2, DLEQ, adaptor signatures | "ed25519", "mlsag", "pedersen", "musig2", "signature", "crypto", "dleq" |
| fuego-network | P2P, peer connections, consensus, Dandelion++ | "p2p", "peer", "network", "consensus", "dandelion" |
| fuego-tx | Transactions, RingCT, inputs/outputs, commitment outputs, v12 auth | "transaction", "ringct", "input", "output", "commitment", "auth" |
| fuego-wallet | Wallet, addresses, keys, balance, hearth commands, deposit secrets | "wallet", "address", "balance", "send", "hearth_xfg", "hearth_heat", "deposit secret" |
| fuego-miner | Mining, difficulty, block rewards | "mine", "difficulty", "hashrate" |

## Trigger Set

**Should trigger on:**
- Any fuego-related query requiring domain expertise
- Multi-domain queries requiring multiple specialized agents
- Complex queries benefiting from parallel agent execution
- "analyze fuego", "how does X work in fuego"
- Query contains multiple domain keywords

**Should NOT trigger on:**
- Non-Fuego blockchain questions
- Generic cryptocurrency queries unrelated to Fuego

## Routing Logic

### Step 1: Intent Analysis

Parse query to identify relevant domains:

```python
def analyze_intent(query: str) -> list[str]:
    domains = []
    query_lower = query.lower()
    
    currency_keywords = ["interest", "apy", "deposit", "yield", "fee pool", "tokenomics", 
                     "certificate of deposit", "cd interest", "cd yield", "epochs"]
    swaps_keywords = ["swap", "atomic swap", "lp pool", "liquidity", "adaptor", "htlc",
                     "hearth", "hearth_add", "hearth_xfg", "hearth_heat", "hearth_exit"]
    heat_keywords = ["heat", "hearth", "mint_heat", "heat_metrics",
                    "xfg_heat_ratio", "heat_peg_usd", "flatcoin", "peg", "heat cd",
                    "heat deposit", "heat stablecoin", "treasury lp", "eternal flame"]
    crypto_keywords = ["ed25519", "mlsag", "pedersen", "musig2", "signature", 
                     "ring signature", "commitment", "hash"]
    network_keywords = ["p2p", "peer", "network", "consensus", "connection", "gossip"]
    tx_keywords = ["transaction", "ringct", "input", "output", "state", "tx"]
    wallet_keywords = ["wallet", "address", "balance", "send", "receive", "keys",
                      "hearth_xfg", "hearth_heat", "hearth_info"]
    miner_keywords = ["mine", "difficulty", "hashrate", "block reward", "proof of work"]
    
    for domain, keywords in DOMAIN_KEYWORDS.items():
        if any(kw in query_lower for kw in keywords):
            domains.append(domain)
    
    return domains
```

### Step 2: Mode Selection

```python
def select_mode(domains: list[str], query_complexity: str = "auto") -> str:
    if len(domains) == 1:
        return "sequential"  # Single agent
    elif query_complexity == "sequential":
        return "sequential"  # User prefers sequential
    else:
        return "parallel"  # Default: parallel for multi-domain
```

### Step 3: Agent Activation

```python
def route_query(query: str, mode: str = "parallel") -> dict:
    domains = analyze_intent(query)
    mode = select_mode(domains) if mode == "auto" else mode
    
    if mode == "sequential":
        # Activate primary domain only
        primary = domains[0] if domains else "currency"
        return {
            "mode": "sequential",
            "activated": [primary],
            "agents": {primary: load_agent(primary)}
        }
    else:
        # Activate all relevant domains in parallel
        return {
            "mode": "parallel",
            "activated": domains,
            "agents": {d: load_agent(d) for d in domains}
        }
```

### Step 4: Result Aggregation

```python
def aggregate_results(agent_results: dict) -> str:
    if len(agent_results) == 1:
        return list(agent_results.values())[0]
    
    # Multi-domain: combine with headers
    combined = []
    for domain, result in agent_results.items():
        combined.append(f"## {domain.upper()}\n{result}")
    
    return "\n\n".join(combined)
```

## Usage

### Query Routing

```python
from references import FuegoOrchestrator

orchestrator = FuegoOrchestrator()

# Simple query - routes to single agent
result = orchestrator.route("What is the CD interest rate?")
# Activates: fuego-currency only

# Multi-domain query - parallel execution
result = orchestrator.route("How do atomic swaps work and what's the P2P protocol?")
# Activates: fuego-swaps + fuego-network in parallel

# Sequential fallback
result = orchestrator.route("Explain CD interest", mode="sequential")
# Activates: fuego-currency only
```

### Full Orchestration

```python
# Complete workflow
query = "Calculate CD interest for 100 XFG over 10 epochs"
response = orchestrator.orchestrate(query)
# 1. Analyze intent → fuego-currency
# 2. Route to agent
# 3. Execute
# 4. Aggregate and return
```

## Parallel vs Sequential

| Scenario | Mode | Agents |
|----------|-----|--------|
| Single domain question | Sequential | 1 agent |
| Multi-domain question | Parallel (default) | N agents |
| Token budget concerns | Sequential | 1 at a time |
| User explicitly requests "parallel" | Parallel | All relevant |

## Constants

```python
DOMAIN_KEYWORDS = {
    "currency": ["interest", "apy", "deposit", "yield", "fee pool", "tokenomics", "loyalty bonus", "cd yield", "epochs"],
    "swaps": ["swap", "atomic swap", "lp pool", "liquidity", "adaptor", "hearth", "afk", "musig2", "dleq"],
    "heat": ["heat", "hearth", "mint_heat", "heat_metrics", "flatcoin", "peg", "orderbook", "heat cd", "heat deposit"],
    "crypto": ["ed25519", "mlsag", "pedersen", "musig2", "signature", "dleq", "adaptor"],
    "network": ["p2p", "peer", "network", "consensus", "dandelion", "gossip"],
    "tx": ["transaction", "ringct", "input", "output", "commitment", "auth", "tx_extra"],
    "wallet": ["wallet", "address", "balance", "send", "hearth_xfg", "hearth_heat", "deposit secret"],
    "miner": ["mine", "difficulty", "hashrate"]
}

PARALLEL_THRESHOLD = 3  # Activate parallel if >3 domains
```

## Utilities

Fuego orchestrator manages access to utilities but they are NOT agents:

| Utility | Purpose |
|---------|---------|
| fuego-rag | Semantic code search |
| fuego-codebase-mapper | File/function search |

These utilities are called BY specialized agents when needed, not activated directly by orchestrator.

## References

See `references/orchestrator.py` for full implementation.
