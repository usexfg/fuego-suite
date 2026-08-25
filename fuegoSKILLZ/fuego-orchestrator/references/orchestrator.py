"""
Fuego Orchestrator - Central router for multi-agent system.
"""

from typing import Optional


DOMAIN_KEYWORDS = {
    "currency": [
        "interest", "apy", "deposit", "yield", "fee pool", "tokenomics",
        "certificate of deposit", "cd interest", "cd yield", "epochs", "epoch",
        "money supply", "coin", "atomic units", "emission"
    ],
    "swaps": [
        "swap", "atomic swap", "lp pool", "liquidity", "adaptor",
        "htlc", "presig", "secret reveal", "escrow", "refund",
        "claim", "swap fee", "swap state"
    ],
    "crypto": [
        "ed25519", "mlsag", "pedersen", "musig2", "musig",
        "signature", "ring signature", "commitment", "hash", "chacha",
        "key image", "DLEQ", "bulletproof", "zk", "zero knowledge"
    ],
    "network": [
        "p2p", "peer", "network", "consensus", "connection",
        "gossip", "handshake", "syncing", "block height"
    ],
    "tx": [
        "transaction", "ringct", "input", "output", "state", "tx",
        "unlock time", "payment id", "extra", " Coinbase"
    ],
    "wallet": [
        "wallet", "address", "balance", "send", "receive",
        "keys", "private key", "public key", "view key", "spend key"
    ],
    "miner": [
        "mine", "difficulty", "hashrate", "block reward", "proof of work",
        "pow", "nonce", "Mining", "miner"
    ]
}

AGENT_PATHS = {
    "currency": "fuego-currency",
    "swaps": "fuego-swaps",
    "crypto": "fuego-crypto",
    "network": "fuego-network",
    "tx": "fuego-tx",
    "wallet": "fuego-wallet",
    "miner": "fuego-miner"
}


class FuegoOrchestrator:
    """
    Central router for Fuego multi-agent system.
    
    Analyzes queries, routes to specialized domain agents,
    and aggregates results.
    """
    
    def __init__(self, source_dir: str = "/Users/aejt/fuego"):
        self.source_dir = source_dir
        self.loaded_agents = {}
        
    def analyze_intent(self, query: str) -> list[str]:
        """Analyze query to identify relevant domains."""
        domains = []
        query_lower = query.lower()
        
        for domain, keywords in DOMAIN_KEYWORDS.items():
            if any(kw in query_lower for kw in keywords):
                domains.append(domain)
        
        # Default to currency if no match
        if not domains:
            domains = ["currency"]
            
        return domains
    
    def select_mode(self, domains: list[str], preference: str = "auto") -> str:
        """Select execution mode (sequential or parallel)."""
        if preference == "auto":
            # Default: parallel if multiple domains
            return "parallel" if len(domains) > 1 else "sequential"
        elif preference in ["sequential", "parallel"]:
            return preference
        else:
            return "parallel"
    
    def route_query(self, query: str, mode: str = "auto") -> dict:
        """
        Route query to appropriate agents.
        
        Returns:
            dict with keys: mode, activated (list), agents (dict)
        """
        domains = self.analyze_intent(query)
        execution_mode = self.select_mode(domains, mode)
        
        if execution_mode == "sequential":
            # Primary domain only
            activated = [domains[0]]
        else:
            # All relevant domains
            activated = domains
        
        return {
            "mode": execution_mode,
            "domains": domains,
            "activated": activated,
            "query": query
        }
    
    def orchestrate(self, query: str, mode: str = "auto") -> str:
        """
        Full orchestration: analyze → route → execute → aggregate.
        
        Returns aggregated response from relevant agents.
        """
        routing = self.route_query(query, mode)
        
        query_lower = query.lower()
        
        # Build responses from activated domains
        responses = []
        
        for domain in routing["activated"]:
            if domain == "currency":
                response = self._handle_currency_query(query)
            elif domain == "swaps":
                response = self._handle_swaps_query(query)
            elif domain == "crypto":
                response = self._handle_crypto_query(query)
            elif domain == "network":
                response = self._handle_network_query(query)
            elif domain == "tx":
                response = self._handle_tx_query(query)
            elif domain == "wallet":
                response = self._handle_wallet_query(query)
            elif domain == "miner":
                response = self._handle_miner_query(query)
            else:
                response = f"No handler for domain: {domain}"
            
            responses.append(response)
        
        return self._aggregate_responses(responses, routing["activated"])
    
    def _aggregate_responses(self, responses: list[str], domains: list[str]) -> str:
        """Aggregate responses from multiple domains."""
        if len(responses) == 1:
            return responses[0]
        
        combined = []
        for domain, response in zip(domains, responses):
            combined.append(f"## {domain.upper()}\n{response}")
        
        return "\n\n".join(combined)
    
    def _handle_currency_query(self, query: str) -> str:
        """Handle currency/CD interest queries."""
        q = query.lower()
        
        if "calculate" in q or "interest" in q or "apy" in q:
            return """### CD Interest Calculation

**Formula:** `interest = amount × Σ (epoch_fee_rate[i] / total_locked_cd[i])`

**Source:** `src/CryptoNoteCore/Currency.cpp::calculateCdInterest()`

**Key Constants:**
- Epoch duration: 900 blocks (~5 days)
- Min deposit term: 16440 blocks (3 months)
- Fee split: 80% CD holders, 20% treasury
- SWAP_FEE_RATE_BPS: 100 (1% swap fee)

**APY Estimate:** `(0.8 × epoch_swap_fees × 73) / total_cd_locked × 100%`

See `fuego-currency` skill for detailed calculations."""
        
        if "deposit" in q:
            return """### Deposits

**Types:**
- HEAT: Burn deposits (forever term, 0.8 XFG min)
- COLD: Locked deposits (3mo-1yr, 0.8 XFG min)

**Min Amount:** AMOUNT_TIER_0 = 8,000,000 atomic (0.8 XFG)
**Min Term:** 16,440 blocks (3 months)
**Max Term:** 65,000 blocks (~1 year)

See `src/CryptoNoteCore/Deposit.cpp` for implementation."""
        
        if "tokenomics" in q or "supply" in q:
            return """### Tokenomics

**Max Supply:** 8,000,008,800,008 (8M8)
**Coin:** 10,000,000 atomic units (10^7)
**Address Prefix:** "fire" (1753191)

See `src/CryptoNoteConfig.h` for constants."""
        
        return """### Currency Domain

Covers: CD interest, deposits, tokenomics, fee pool, emissions.

Key files:
- `src/CryptoNoteCore/Currency.cpp` - CD interest
- `src/CryptoNoteCore/CommitmentIndex.cpp` - Fee pool
- `src/CryptoNoteCore/BankingIndex.cpp` - Deposits
- `src/CryptoNoteConfig.h` - Constants"""
    
    def _handle_swaps_query(self, query: str) -> str:
        """Handle atomic swap queries."""
        q = query.lower()
        
        if "state" in q:
            return """### Atomic Swap States

**Active States:**
```
ADAPTOR_KEYS_EXCHANGED = 10  # Keys distributed
ADAPTOR_ESCROW_FUNDED = 11   # XFG locked
ADAPTOR_PRESIGS_READY = 12   # Presigs ready
ADAPTOR_CTR_LOCKED = 13      # Counterparty locked
ADAPTOR_SECRET_REVEALED = 14  # Secret revealed  
ADAPTOR_XFG_SPENT = 15      # XFG claimed
ADAPTOR_REFUNDED = 16        # Refund on timeout
```

See `src/SwapDaemon/SwapTypes.h`."""
        
        if "fee" in q:
            return """### Swap Fees

**Fee Rate:** 1% (100 basis points)
**Distribution:** 80% CD yield pool, 20% treasury
**Calculated on:** claim or refund amount

See `src/CryptoNoteConfig.h::SWAP_FEE_RATE_BPS`"""
        
        return """### Atomic Swaps Domain

Covers: Atomic swap state machine, adaptor signatures, LP pools, swap fees.

Key files:
- `src/SwapDaemon/SwapTypes.h` - States, constants
- `src/SwapDaemon/SwapStateMachine.cpp` - State transitions
- `src/crypto/musig2.h` - MuSig2 adaptor signatures"""
    
    def _handle_crypto_query(self, query: str) -> str:
        """Handle cryptographic primitive queries."""
        q = query.lower()
        
        if "ed25519" in q or "key" in q:
            return """### Ed25519 Keys

**File:** `src/crypto/crypto.h`, `src/crypto/crypto.cpp`

**Functions:**
- `generate_keys(PublicKey&, SecretKey&)` - Generate keypair
- `check_key(const PublicKey&)` - Validate key
- `generate_signature(Hash, PublicKey, SecretKey, Signature&)`

See `src/crypto/crypto.h` lines 45-90."""
        
        if "mlsag" in q or "ring" in q:
            return """### MLSAG Ring Signatures

**File:** `src/crypto/mlsag.h`, `src/crypto/mlsag.cpp`

**Purpose:** RingCT transaction signing
- `generate_ring_signature()` - Create MLSAG ring sig
- `check_ring_signature()` - Verify ring sig

See `src/crypto/mlsag.h`."""
        
        if "pedersen" in q or "commitment" in q:
            return """### Pedersen Commitments

**File:** `src/crypto/pedersen.h`, `src/crypto/pedersen.cpp`

**Functions:**
- `commit()` - Create Pedersen commitment
- `verify_commitment()` - Verify commitment

Used in RingCT for amount privacy."""
        
        if "musig2" in q:
            return """### MuSig2 Aggregated Signatures

**File:** `src/crypto/musig2.h`, `src/crypto/musig2.cpp`

**Purpose:** Atomic swap adaptor signatures
- Multi-party signing for swap escrow
- DLEQ proofs for non-interactive setup

See `src/crypto/musig2.h`."""
        
        return """### Cryptography Domain

Covers: Ed25519, MLSAG, Pedersen, MuSig2, hashing, signatures.

Key files:
- `src/crypto/crypto.h` - Ed25519
- `src/crypto/mlsag.h` - MLSAG ring signatures
- `src/crypto/pedersen.h` - Pedersen commitments
- `src/crypto/musig2.h` - MuSig2 (atomic swaps)"""
    
    def _handle_network_query(self, query: str) -> str:
        """Handle P2P network queries."""
        q = query.lower()
        
        if "protocol" in q or "command" in q:
            return """### P2P Protocol Commands

**File:** `src/P2p/P2pProtocolDefinitions.h`

| ID | Command | Purpose |
|----|---------|---------|
| 1001 | HANDSHAKE | Node handshake + peers |
| 1002 | TIMED_SYNC | Time sync + height |
| 1003 | PING | Connection alive |
| 1013 | SWAP_OFFER | Swap offer gossip |
| 1014 | SWAP_CANCEL | Cancel offer |
| 1015 | SWAP_TRADE | Completed swap"""
        
        if "peer" in q or "connection" in q:
            return """### Peer Connections

**Target:** 8 connections
**Handshake interval:** Configurable
**Max packet size:** Configurable

Network config in `src/P2p/P2pProtocolDefinitions.h`"""
        
        return """### Network Domain

Covers: P2P protocols, peer connections, consensus, gossip.

Key files:
- `src/P2p/P2pProtocolDefinitions.h` - Commands
- `src/P2p/P2pConnections.h` - Connection management"""
    
    def _handle_tx_query(self, query: str) -> str:
        """Handle transaction queries."""
        q = query.lower()
        
        if "ringct" in q or "privacy" in q:
            return """### RingCT

**Implementation:** CryptoNote privacy protocol
- MLSAG ring signatures for input signing
- Pedersen commitments for amount hiding
- Range proofs for amount validity

Source: `src/crypto/mlsag.h`, `src/crypto/pedersen.h`"""
        
        return """### Transaction Domain

Covers: Transactions, inputs, outputs, RingCT, state.

Key files:
- `src/CryptoNoteCore/Transaction.h` - Transaction structure
- `src/crypto/mlsag.h` - Ring signatures
- `src/crypto/pedersen.h` - Commitments"""
    
    def _handle_wallet_query(self, query: str) -> str:
        """Handle wallet queries."""
        q = query.lower()
        
        if "address" in q:
            return """### Addresses

**Prefix:** "fire" (1753191 in base58)
**Format:** Standard CryptoNote with prefix
**Keys:** Spend key + View key (dual-key system)"""
        
        return """### Wallet Domain

Covers: Wallet operations, addresses, keys, balance.

Key files:
- `src/WalletLegacy/` - CLI wallet
- `src/CryptoNoteCore/TransactionPool.h` - Tx pool"""
    
    def _handle_miner_query(self, query: str) -> str:
        """Handle mining queries."""
        q = query.lower()
        
        if "difficulty" in q:
            return """### Difficulty Adjustment

**Target:** 480 seconds per block
**Algorithm:** DMWDA (Dynamic Multi-Window Difficulty Adjustment)
**Windows:** Short (15), Medium (45), Long (120) blocks

See `src/CryptoNoteConfig.h::DIFFICULTY_TARGET`"""
        
        return """### Mining Domain

Covers: Mining, difficulty, block rewards, PoW.

Key files:
- `src/Miner/` - Mining implementation
- `src/CryptoNoteConfig.h` - Constants"""
    
    def load_agent(self, domain: str):
        """Load a specialized agent (placeholder for skill loading)."""
        # In actual implementation, this would load the skill
        # For now, returns domain info
        return {
            "domain": domain,
            "status": "loaded",
            "agent": AGENT_PATHS.get(domain, "unknown")
        }