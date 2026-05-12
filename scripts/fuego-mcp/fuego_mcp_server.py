#!/usr/bin/env python3
"""
Fuego MCP Server - Unified tools for all Fuego blockchain domains.
Zero external dependencies. Implements JSON-RPC MCP over stdio.

Domains covered: chain core, currency, wallets, RPC API, transactions,
crypto, mining, P2P, atomic swaps, deposits/burns, privacy, contracts,
aliases, codebase mapper, and RAG.
"""
import sys, json, os, re, hashlib, sqlite3
from pathlib import Path
from typing import Any, Dict, List, Optional
from datetime import datetime
from dataclasses import dataclass

FUEGO_ROOT = os.environ.get("FUEGO_ROOT", str(Path(__file__).parent.absolute()))
ROOT = Path(FUEGO_ROOT)


# ═══════════════════════════════════════════════════════════════════════════
# KNOWLEDGE BASE
# ═══════════════════════════════════════════════════════════════════════════

CHAIN_CONFIG = {
    "mainnet": {"name": "Fuego", "symbol": "XFG", "decimal_places": 8, "atomic_unit": "1e-8 XFG",
        "genesis_timestamp": 1625097600, "block_time_seconds": 480, "blocks_per_day": 180,
        "target_timespan_seconds": 86400, "money_supply": 80000088008,
        "coin": 10000000, "default_port": 11898, "testnet_port": 21898},
    "testnet": {"name": "Fuego Testnet", "symbol": "XFG", "decimal_places": 8, "atomic_unit": "1e-8 XFG",
        "block_time_seconds": 60, "blocks_per_day": 1440, "default_port": 21898},
}

FEE_CONFIG = {
    "minimum_fee": 100000, "fee_per_byte": 2, "priority_levels": [
        {"level": "low", "multiplier": 1}, {"level": "medium", "multiplier": 5},
        {"level": "high", "multiplier": 25}, {"level": "flash", "multiplier": 200}],
    "fee_tiers": [{"min_size": 0, "max_size": 10000, "fee": 100000},
        {"min_size": 10000, "max_size": 50000, "fee": 500000}],
    "deposit_fee": 100000, "alias_fee": 100000000, "swap_fee_rate_bps": 100,
}

UPGRADE_HEIGHTS = [
    ("V2_SWAP", None), ("V3_STARK_COMMITMENTS", None), ("V4_HEAT_BURNS", None),
    ("V5_ALIAS_SYSTEM", 100000), ("V6_TIER_PROOFS", 105000), ("V7_MLSAG", 450000),
    ("V8_ADAPTOR_SWAPS", 450000), ("V9_ADAPTOR_FEES", 450000),
    ("V10_COLD_DEPOSITS", 650000), ("V11_LP_POOL", None),
]

CD_CONFIG = {
    "min_amount": 8000000, "min_term": 16000, "max_term": 65000,
    "testnet_min_term": 8, "testnet_max_term": 42,
    "epoch_duration_blocks": 900, "testnet_epoch_duration_blocks": 10,
    "fee_pool_precision": 1000000, "cd_share_pct": 80, "treasury_share_pct": 20,
    "deposit_term_forever": 4294967295, "staged_unlock_stages": 5,
    "staged_unlock_interval_blocks": 3240,
}

SWAP_STATES = {10: "KEYS_EXCHANGED", 11: "ESCROW_FUNDED", 12: "PRESIGS_READY",
    13: "CTR_LOCKED", 14: "SECRET_REVEALED", 15: "XFG_SPENT", 16: "REFUNDED"}
SWAP_TRANSITIONS = {10: [11], 11: [12, 16], 12: [13], 13: [14, 16], 14: [15], 16: []}

CRYPTO = {"signatures": ["Ed25519", "Schnorr", "MLSAG", "MuSig2", "Adaptor"],
    "hash_functions": ["Keccak (SHA-3)", "Blake2b", "Blake256", "Groestl", "JH", "Skein", "CryptoNight"],
    "key_exchange": ["ECDH", "X25519"],
    "commitments": ["Pedersen", "Tier Proofs (1-of-4 OR)"],
    "zk_proofs": ["ZK-SNARKs", "ZK-STARKs (Winterfell)"],
    "encryption": ["ChaCha8", "AES"],
    "prng": ["CNG random", "platform CSPRNG"]}

SIGNATURE_SCHEMES = {"Ed25519": "Standard Ed25519 signatures (RFC 8032)",
    "Schnorr": "Schnorr-style proof of knowledge for ring signatures",
    "MLSAG": "Multi-layered Linkable Spontaneous Anonymous Group (2-layer: spend auth + balance proof)",
    "MuSig2": "2-of-2 Schnorr aggregated signatures (swap escrow)",
    "Adaptor": "Ed25519 adaptor signatures with DLEQ proofs (atomic swaps)"}

WALLET_INFO = {"types": ["WalletGreen (main)", "WalletLegacy", "SimpleWallet (CLI)", "TestnetWallet"],
    "features": ["Subaddresses", "Fusion optimization", "Staged deposit unlock",
        "CD create/withdraw/rollover", "Burn deposit tracking", "Multi-language mnemonics"],
    "address_prefix": "fue", "address_length": 97, "integrated_address_length": 105,
    "key_types": ["private_spend_key", "private_view_key", "public_spend_key", "public_view_key"]}

RPC_METHODS_DAEMON = ["get_info", "get_blocks", "get_blocks_range", "get_transactions",
    "get_transaction", "get_deposits", "get_random_outs", "get_alias", "get_aliases",
    "get_height", "get_block_count", "get_block", "get_block_hash", "send_transaction",
    "start_mining", "stop_mining", "get_connections", "get_peers", "get_pool",
    "get_commitments", "get_epoch_fee_rate", "get_cd_interest", "swap_management"]

RPC_METHODS_WALLET = ["getbalance", "getaddress", "transfer", "transfer_split",
    "create_deposit", "withdraw_deposit", "rollover_deposit", "get_deposits",
    "get_transfers", "get_transaction", "create_swap", "accept_swap",
    "send_fusion_transaction", "get_alias_by_address", "set_alias", "reset"]

TX_TYPES = {"regular": "Standard XFG transfer with ring signatures",
    "fusion": "Output consolidation with no new destinations",
    "deposit": "Create a COLD certificate of deposit",
    "withdraw_deposit": "Withdraw from a COLD deposit",
    "rollover_deposit": "Roll over a COLD deposit to extend term",
    "burn": "HEAT burn deposit (permanent XFG lock, yields HEAT)",
    "swap": "Atomic swap transaction (HTLC or adaptor signature)",
    "alias": "Register or update an on-chain alias"}

PROTOCOL_VERSIONS = {"P2P": 1, "BLOCKCHAIN": 1}

MINING_INFO = {"algorithm": "CryptoNight (slow hash variant 2)",
    "proof_of_work": "Yes (hashcash-style)", "cpu_mining": True,
    "gpu_mining": False, "pool_mining": True, "solo_mining": True,
    "block_reward_halving_interval": None, "premine": False,
    "difficulty_algorithm": "DMWDA (Dynamic Multi-Window Difficulty Adjustment)",
    "difficulty_windows": {"short": 15, "medium": 45, "long": 120, "emergency": 5}}

PRIVACY_INFO = {"ring_signatures": True, "ring_sizes": "Dynamic (8-18, based on output age)",
    "target_ring_sizes": "18 / 15 / 12 / 10 / 8 (decreasing with age)",
    "mlsag": True, "stealth_addresses": True, "amount_hiding": True,
    "commitment_scheme": "Pedersen commitments", "decoy_selection": "OSPEAD-inspired",
    "decoy_binning": "Logarithmic age bins", "dandelion": "Dandelion++ (stem/fluff)",
    "tor_support": True, "i2p_support": True, "meshtastic_support": True}

PLATFORM_DOMAINS = [
    ("Blockchain Core", "src/CryptoNoteCore/", "Block/chain management, validation, sync, reorg"),
    ("Currency/Monetary", "src/CryptoNoteCore/Currency.*", "Emission curve, fees, deposits, HEAT, burn validation"),
    ("Transactions", "src/CryptoNoteCore/Transaction*", "Tx build, validation, serialization"),
    ("Mempool", "src/CryptoNoteCore/TransactionPool.*", "Unconfirmed tx pool, eviction, relay"),
    ("Cryptography", "src/crypto/", "38+ files: Ed25519, MLSAG, adaptor, MuSig2, Pedersen, hashing, PoW"),
    ("Atomic Swaps", "src/SwapDaemon/", "Cross-chain engine for XMR, ETH, BCH, SOL"),
    ("LP Pool AMM", "src/SwapDaemon/pool_v11/", "Constant product AMM (v11 deferred)"),
    ("P2P Networking", "src/P2p/", "Peer discovery, connections, block/tx relay"),
    ("RPC API", "src/Rpc/", "HTTP/JSON-RPC for chain queries, 80+ commands"),
    ("Wallet", "src/Wallet/", "WalletGreen, key management, tx creation, serialization"),
    ("Mining", "src/Miner/", "CPU miner, block template, pool protocol"),
    ("Aliases", "src/CryptoNoteCore/AliasIndex.*", "On-chain alias registry (Elderfier + regular)"),
    ("Decoy Selection", "src/CryptoNoteCore/OSPEADDecoySelection.*", "Privacy-preserving ring member selection"),
    ("Privacy", "include/DynamicRingSize.h", "Dynamic ring size, amounts, Dandelion++"),
    ("Burn/HEAT", "src/CryptoNoteCore/DepositCommitment.*", "STARK commitments, burn proofs, HEAT conversion"),
    ("Ethereum Bridge", "contracts/*.sol", "HEATClaimer, CheckpointVerifier (Solidity)"),
    ("Mnemonics", "src/Mnemonics/", "Seed phrases in 14 languages"),
    ("Tor", "src/FuegoTor/", "Tor anonymity integration (SOCKS5, hidden services)"),
    ("I2P", "src/FuegoI2P/", "I2P anonymous networking"),
    ("Meshtastic", "src/FuegoMeshtastic/", "Off-grid mesh network integration"),
    ("ZK Prover", "fuego-prover/", "Rust ZK-STARK proof system"),
    ("Serialization", "src/Serialization/", "Binary, JSON, KV serialization formats"),
    ("Daemon", "src/Daemon/", "Node daemon executable and command handler"),
    ("TUI", "tui/", "Go-based terminal UI"),
    ("Tests", "tests/", "Core, unit, integration, performance test suites"),
    ("Documentation", "docs/", "42+ docs (features, design, developer, API reference)"),
]


def _read(rel: str) -> str:
    p = ROOT / rel
    return p.read_text(errors="ignore") if p.exists() else ""


def _extract_config(key: str, pattern: str) -> Optional[str]:
    m = re.search(pattern, _read("src/CryptoNoteConfig.h"))
    return m.group(1) if m else None


# ── Computation Helpers ────────────────────────────────────────────────────

def calculate_emission_at(height: int) -> dict:
    base_reward = 2000 * 100000000  # 2000 XFG per block initially
    emission_so_far = base_reward * height
    remaining = max(0, CHAIN_CONFIG["mainnet"]["money_supply"] * 100000000 - emission_so_far)
    return {"height": height, "emitted": emission_so_far, "remaining": remaining,
        "percent_emitted": round(emission_so_far / (CHAIN_CONFIG["mainnet"]["money_supply"] * 100000000) * 100, 2)}

def calculate_block_reward_at(height: int) -> dict:
    return {"height": height, "reward": 2000 * 100000000, "reward_xfg": f"{2000:.2f} XFG"}

def calculate_tx_fee(tx_size_bytes: int, priority: str = "low") -> dict:
    multipliers = {"low": 1, "medium": 5, "high": 25, "flash": 200}
    mult = multipliers.get(priority, 1)
    fee = max(tx_size_bytes * 2 * mult, 100000)
    return {"tx_size_bytes": tx_size_bytes, "priority": priority, "multiplier": mult,
        "fee": fee, "fee_xfg": f"{fee / 1e8:.4f} XFG"}

def calculate_cd_interest(amount: int, creation_height: int, current_height: int, rates: List[int]) -> int:
    if current_height <= creation_height or not rates:
        return 0
    epoch_dur = CD_CONFIG["epoch_duration_blocks"]
    start = creation_height // epoch_dur
    end = current_height // epoch_dur
    interest = sum(amount * rates[e] // CD_CONFIG["fee_pool_precision"]
                   for e in range(start, min(end + 1, len(rates))))
    return interest

def estimate_apy(current_epoch_fee: int, total_cd_locked: int) -> float:
    if total_cd_locked == 0:
        return 0.0
    return (current_epoch_fee * CD_CONFIG["cd_share_pct"] // 100 * 73) / total_cd_locked * 100

def analyze_swap_fee(xfg_amount: int) -> dict:
    fee = xfg_amount * FEE_CONFIG["swap_fee_rate_bps"] // 10000
    return {"amount_xfg": f"{xfg_amount / 1e8:.2f}", "fee": fee, "fee_xfg": f"{fee / 1e8:.4f}",
        "net": xfg_amount - fee, "rate_bps": FEE_CONFIG["swap_fee_rate_bps"]}

def analyze_peer_health(peer_count: int, target: int = 8) -> dict:
    score = min(peer_count / target, 1.0)
    status = "DISCONNECTED" if peer_count == 0 else "UNDERCONNECTED" if peer_count < target // 2 \
             else "HEALTHY" if peer_count < target else "WELL_CONNECTED"
    return {"peer_count": peer_count, "target": target, "health_score": round(score, 2), "status": status,
        "advice": "Start daemon, check firewall" if peer_count == 0 else
                  f"Only {peer_count}/{target} connections, add seed nodes" if peer_count < target // 2 else
                  f"Good ({peer_count}/{target})" if peer_count < target else "Well-connected"}

def analyze_epoch_distribution(fees: int, locked: int) -> dict:
    cd_share = fees * CD_CONFIG["cd_share_pct"] // 100
    treasury = fees * CD_CONFIG["treasury_share_pct"] // 100
    rate = (cd_share * CD_CONFIG["fee_pool_precision"]) // locked if locked > 0 else 0
    return {"epoch_swap_fees": fees, "cd_share_pct": CD_CONFIG["cd_share_pct"],
        "cd_share": cd_share, "treasury_share_pct": CD_CONFIG["treasury_share_pct"],
        "treasury_share": treasury, "fee_rate": rate}

# ── Codebase Mapper Helpers ────────────────────────────────────────────────

DB = ROOT / ".fuego_mcp.db"

def _db():
    conn = sqlite3.connect(str(DB))
    c = conn.cursor()
    c.execute("CREATE TABLE IF NOT EXISTS files (path TEXT UNIQUE, size INT, mtime REAL, type TEXT, lang TEXT, lines INT, sha TEXT)")
    c.execute("CREATE TABLE IF NOT EXISTS funcs (name TEXT, file TEXT, line INT)")
    conn.commit()
    return conn, c

def _scan(force=False) -> dict:
    conn, c = _db()
    if not force:
        c.execute("SELECT COUNT(*) FROM files")
        if c.fetchone()[0] > 0:
            return {"status": "cached"}
    c.execute("DELETE FROM files"); c.execute("DELETE FROM funcs")
    ext_map = {".cpp": "cpp", ".h": "cpp", ".go": "go", ".py": "python", ".md": "markdown"}
    files = [f for ext in ext_map for f in ROOT.glob(f"**/*{ext}") if ".git" not in str(f)]
    func_pat = re.compile(r"(?:void|int|uint64_t|bool|string|size_t)\s+(\w+)\s*\(")
    for f in files:
        rel = str(f.relative_to(ROOT)); st = f.stat(); lines = 0
        try:
            text = f.read_text(errors="ignore")
            lines = text.count("\n") + 1
            ft = f.suffix.lower()
            c.execute("INSERT OR REPLACE INTO files VALUES (?,?,?,?,?,?,?)",
                      (rel, st.st_size, st.st_mtime, ft, ext_map.get(ft, "?"), lines, hashlib.sha256(f.read_bytes()).hexdigest()))
            if ft in {".cpp", ".h", ".py"}:
                for i, line in enumerate(text.split("\n")):
                    m = func_pat.match(line.strip())
                    if m:
                        c.execute("INSERT INTO funcs VALUES (?,?,?)", (m.group(1), rel, i + 1))
        except:
            pass
    conn.commit()
    return {"status": "ok", "indexed": len(files)}

# ── RAG Helpers ────────────────────────────────────────────────────────────

_rag = {"docs": [], "chunks": []}

def _discover(limit=50):
    docs = []
    for ext in [".cpp", ".h", ".go", ".py", ".md"]:
        for f in ROOT.glob(f"**/*{ext}"):
            if ".git" not in str(f):
                docs.append(f)
    docs = list(set(docs))[:limit]
    result = []
    for d in docs:
        try:
            text = d.read_text(errors="ignore")
            t = "doc" if "docs/" in str(d) else "cpp" if d.suffix in {".cpp", ".h"} else "go" if d.suffix == ".go" else "py"
            result.append({"id": hashlib.md5(str(d).encode()).hexdigest()[:8], "path": str(d.relative_to(ROOT)),
                          "content": text[:50000], "type": t})
        except: pass
    return result

def _chunk(docs):
    chunks = []
    for doc in docs:
        lines = doc["content"].split("\n"); cur = []; brace = 0; in_fn = False
        for line in lines:
            cur.append(line); brace += line.count("{") - line.count("}")
            if "{" in line and not in_fn: in_fn = True
            if brace == 0 and in_fn and cur and len("\n".join(cur)) > 80:
                chunks.append({"id": f"{doc['id']}-{len(chunks)}", "path": doc["path"], "type": doc["type"], "content": "\n".join(cur)})
                cur = []; in_fn = False
        if cur and len("\n".join(cur)) > 80:
            chunks.append({"id": f"{doc['id']}-{len(chunks)}", "path": doc["path"], "type": doc["type"], "content": "\n".join(cur)})
    return chunks


# ═══════════════════════════════════════════════════════════════════════════
# MCP SERVER
# ═══════════════════════════════════════════════════════════════════════════

def _tool(name, desc, props, required=None):
    return (name, {"description": desc,
        "inputSchema": {"type": "object", "properties": props,
                        "required": required or list(props.keys()) if required else []}})

TOOLS = dict([
    # ═══ BLOCKCHAIN CORE ═══
    _tool("get_chain_info", "Main chain parameters: network type, height target, block time, ports, genesis",
        {}),
    _tool("get_config_constants", "Core blockchain constants from CryptoNoteConfig.h: difficulty target, money supply, deposit terms, upgrade heights, mixin sizes, seed nodes",
        {}),
    _tool("get_upgrade_schedule", "Protocol upgrade schedule: V2 through V11 with activation heights and features",
        {}),
    _tool("get_block_info", "Block structure: header fields, body format, size limits, timing targets",
        {}),
    _tool("get_token_supply", "Total token supply, emission curve, circulating supply, coin precision",
        {}),
    _tool("get_fee_structure", "Fee system: minimum fee, per-byte fee, priority levels, fee tiers",
        {}),

    # ═══ CURRENCY / MONETARY ═══
    _tool("get_coin_denominations", "XFG denominations: atomic units, display format, coin name, symbol, decimal places",
        {}),
    _tool("calculate_emission", "Emission at a given block height: total emitted, remaining, percent mined",
        {"height": {"type": "integer", "description": "Block height"}}, ["height"]),
    _tool("calculate_block_reward", "Block reward at a given height",
        {"height": {"type": "integer", "description": "Block height"}}, ["height"]),

    # ═══ WALLETS ═══
    _tool("get_wallet_info", "Wallet system: wallet types (Green, Simple CLI), features, key types, serialization",
        {}),
    _tool("get_address_info", "Address format: prefix, length, integrated addresses, subaddresses",
        {}),
    _tool("get_mnemonic_info", "Mnemonic seed phrases: supported languages (14), word count, algorithm (Electrum-style)",
        {}),
    _tool("get_wallet_rpc_methods", "Key wallet RPC methods: getbalance, transfer, deposit operations, swap management, alias",
        {}),

    # ═══ RPC / API ═══
    _tool("get_daemon_rpc_methods", "Daemon/blockchain RPC methods: chain queries, transactions, deposits, mining, peers, swap management",
        {}),
    _tool("get_rpc_error_codes", "Common RPC error codes and meanings",
        {"code": {"type": "integer", "description": "Optional error code to look up"}}),
    _tool("get_payment_gateway_info", "Payment gateway service: JSON-RPC interface, wallet service, staged deposit operations",
        {}),

    # ═══ TRANSACTIONS ═══
    _tool("analyze_tx_structure", "Transaction structure: inputs, outputs, extra fields (payment ID, deposit commitment, burn proof), ring signatures, signatures",
        {}),
    _tool("get_tx_types", "Transaction types: regular, fusion, deposit, withdraw, rollover, burn, swap, alias -- with descriptions",
        {}),
    _tool("calculate_tx_fee", "Calculate transaction fee for a given size and priority level",
        {"tx_size_bytes": {"type": "integer", "description": "Transaction size in bytes"},
         "priority": {"type": "string", "description": "Priority level: low, medium, high, flash"}}),
    _tool("get_ring_signature_info", "Ring signature configuration: dynamic ring sizes by output age, targets, minimums",
        {}),

    # ═══ CRYPTOGRAPHY ═══
    _tool("get_crypto_primitives", "All cryptographic primitives: signatures, hash functions, key exchange, commitments, ZK proofs, encryption",
        {}),
    _tool("get_signature_schemes", "Signature scheme details: Ed25519, Schnorr, MLSAG, MuSig2, Adaptor signatures with descriptions",
        {}),
    _tool("get_hash_functions", "Hash function variants: Keccak/SHA-3, Blake2b, Blake256, Groestl, JH, Skein, CryptoNight slow hash",
        {}),

    # ═══ MINING ═══
    _tool("get_mining_info", "Mining system: algorithm (CryptoNight variant 2), PoW details, CPU/GPU/pool/solo options",
        {}),
    _tool("get_difficulty_info", "Difficulty algorithm (DMWDA): multi-window adjustment, confidence scoring, anomaly detection",
        {}),
    _tool("get_mining_config", "Mining configuration: setup, block template, solution submission, stratum protocol",
        {}),

    # ═══ P2P NETWORKING ═══
    _tool("analyze_peer_health", "Analyze P2P network health from peer count",
        {"peer_count": {"type": "integer", "description": "Current connected peers"},
         "target_count": {"type": "integer", "description": "Target peer count (default 8)"}}),
    _tool("get_p2p_info", "P2P networking: protocol messages (handshake, sync, relay), peer list (white/gray), Levin protocol",
        {}),
    _tool("get_anonymity_info", "Anonymity networking: Tor SOCKS5/hidden services, I2P, Meshtastic off-grid mesh",
        {}),

    # ═══ ATOMIC SWAPS ═══
    _tool("get_swap_info", "Atomic swap system: state machine, adaptor signature protocol, cross-chain pairs (XMR, ETH, BCH, SOL)",
        {}),
    _tool("calculate_swap_fee", "Calculate atomic swap fee for a given XFG amount",
        {"xfg_amount": {"type": "integer", "description": "XFG amount in atomic units"}}),
    _tool("validate_swap_state", "Validate atomic swap state machine transition",
        {"current_state": {"type": "integer", "description": "Current state (10-16)"},
         "target_state": {"type": "integer", "description": "Target state (10-16)"}},
        ["current_state", "target_state"]),
    _tool("get_swap_state_name", "Get human-readable name for a swap state ID",
        {"state_id": {"type": "integer", "description": "State ID (10-16)"}}, ["state_id"]),
    _tool("get_lp_pool_info", "v11 LP pool AMM: constant product formula, LP shares, fee accumulation, Merkle checkpoints",
        {}),

    # ═══ DEPOSITS / BURNS ═══
    _tool("calculate_cd_interest", "Calculate Certificate of Deposit interest accumulated over epoch range",
        {"amount": {"type": "integer", "description": "CD amount in atomic units"},
         "creation_height": {"type": "integer", "description": "Creation block height"},
         "current_height": {"type": "integer", "description": "Current block height"},
         "epoch_fee_rates": {"type": "array", "items": {"type": "integer"}, "description": "Epoch fee rate array"}},
        ["amount", "creation_height", "current_height", "epoch_fee_rates"]),
    _tool("estimate_apy", "Estimate CD Annual Percentage Yield from current epoch swap fees and total locked",
        {"current_epoch_fee": {"type": "integer", "description": "Swap fees in current epoch"},
         "total_cd_locked": {"type": "integer", "description": "Total XFG locked in CDs"}},
        ["current_epoch_fee", "total_cd_locked"]),
    _tool("get_deposit_info", "Deposit system: COLD CDs, HEAT burns, investment deposits, staged unlocking, terms and minimums",
        {}),

    # ═══ PRIVACY ═══
    _tool("get_privacy_model", "Privacy model: ring signatures, stealth addresses, hidden amounts, Pedersen commitments, Dandelion++",
        {}),
    _tool("get_decoy_info", "Decoy selection (OSPEAD-inspired): logarithmic age bins, spend probability matching, anonymity sets",
        {}),

    # ═══ CONTRACTS / HEAT ═══
    _tool("get_contract_info", "Ethereum bridge contracts: FuegoCheckpointVerifier (ZK checkpoint advancement), HEATClaimer (Merkle proof claims)",
        {}),
    _tool("get_heat_info", "HEAT token: XFG-to-HEAT conversion rate, burn deposit mechanism, ethereal supply tracking, STARK relay",
        {}),

    # ═══ ALIASES ═══
    _tool("get_alias_info", "On-chain alias system: Elderfier vs regular aliases, format rules, registration fee, hash-based lookups",
        {}),

    # ═══ CODEBASE MAPPER ═══
    _tool("scan_codebase", "Scan and index all Fuego codebase files into searchable DB",
        {"force_rescan": {"type": "boolean", "description": "Re-index from scratch"}}),
    _tool("search_files", "Search files by name or path pattern",
        {"query": {"type": "string", "description": "Filename or path pattern"},
         "limit": {"type": "integer", "description": "Max results"}}, ["query"]),
    _tool("search_functions", "Search functions across the codebase by name",
        {"name": {"type": "string", "description": "Function name pattern"},
         "limit": {"type": "integer", "description": "Max results"}}, ["name"]),
    _tool("get_codebase_stats", "Codebase statistics: file counts by language, total lines, function count",
        {}),
    _tool("get_file_tree", "Hierarchical file tree of the codebase structure",
        {"depth": {"type": "integer", "description": "Max directory depth"}}),
    _tool("find_files_by_type", "Find files by extension type",
        {"file_type": {"type": "string", "description": "File extension e.g. .cpp, .go, .py"},
         "limit": {"type": "integer", "description": "Max results"}}, ["file_type"]),

    # ═══ CODEBASE EXPLORER ═══
    _tool("get_source_domains", "All major source code domains with paths and descriptions (34+ domains)",
        {}),
    _tool("analyze_code_section", "Find all files matching a regex pattern across the codebase",
        {"pattern": {"type": "string", "description": "Regex to search for in file contents"}},
        ["pattern"]),

    # ═══ RAG ═══
    _tool("build_rag_index", "Build RAG index: discover docs, read, chunk by function/class boundaries",
        {"limit": {"type": "integer", "description": "Max documents to index"}}),
    _tool("search_codebase", "Search indexed codebase chunks by keyword relevance",
        {"query": {"type": "string", "description": "Search terms"},
         "top_k": {"type": "integer", "description": "Number of results"}}, ["query"]),
    _tool("generate_context", "Search and return LLM-ready context prompt with source references",
        {"query": {"type": "string", "description": "Query to build context for"},
         "top_k": {"type": "integer", "description": "Number of chunks"}}, ["query"]),
])


class FuegoMCPServer:
    def __init__(self):
        self.tools = dict(TOOLS)

    def dispatch(self, name: str, args: dict) -> Any:
        def v(key, default=None):
            return args.get(key, default)

        # ── Blockchain Core ──
        if name == "get_chain_info":
            cfg = CHAIN_CONFIG["mainnet"]
            return {"network": "mainnet", "name": cfg["name"], "symbol": cfg["symbol"],
                "decimal_places": cfg["decimal_places"], "atomic_unit": cfg["atomic_unit"],
                "block_time_seconds": cfg["block_time_seconds"], "blocks_per_day": cfg["blocks_per_day"],
                "default_p2p_port": cfg["default_port"], "testnet_p2p_port": cfg["testnet_port"],
                "genesis_timestamp": cfg["genesis_timestamp"],
                "protocol_versions": PROTOCOL_VERSIONS}

        elif name == "get_config_constants":
            c = _read("src/CryptoNoteConfig.h")
            def x(pat): m = re.search(pat, c); return m.group(1) if m else None
            return {"difficulty_target_seconds": x(r"DIFFICULTY_TARGET\s*=\s*(\d+)"),
                "money_supply": x(r"MONEY_SUPPLY\s*=\s*(\d+)"),
                "coin": x(r"COIN\s*=\s*(\d+)"),
                "minimum_fee": x(r"MINIMUM_FEE\s*=\s*(\d+)"),
                "epoch_duration_blocks": x(r"EPOCH_DURATION_BLOCKS\s*=\s*(\d+)"),
                "swap_fee_rate_bps": x(r"SWAP_FEE_RATE_BPS\s*=\s*(\d+)"),
                "cd_share_pct": x(r"SWAP_FEE_CD_SHARE_PCT\s*=\s*(\d+)"),
                "fee_pool_precision": x(r"FEE_POOL_RATE_PRECISION\s*=\s*(\d+)"),
                "cold_min_term": x(r"COLD_MIN_TERM\s*=\s*(\d+)"),
                "cold_max_term": x(r"COLD_MAX_TERM\s*=\s*(\d+)"),
                "deposit_min_amount": x(r"DEPOSIT_MIN_AMOUNT\s*=\s*(\d+)"),
                "alias_fee": x(r"ALIAS_FEE\s*=\s*(\d+)"),
                "testnet_epoch_duration": x(r"TESTNET_EPOCH_DURATION_BLOCKS\s*=\s*(\d+)"),
                "testnet_cold_min_term": x(r"TESTNET_COLD_MIN_TERM\s*=\s*(\d+)"),
                "testnet_cold_max_term": x(r"TESTNET_COLD_MAX_TERM\s*=\s*(\d+)"),
                "seed_nodes": x(r"SEED_NODES\s*=\s*\{([^}]+)\}")}

        elif name == "get_upgrade_schedule":
            return [{"name": name, "height": h if h else "deferred/unspecified",
                     "active": h is not None} for name, h in UPGRADE_HEIGHTS]

        elif name == "get_block_info":
            return {"header_fields": ["major_version", "minor_version", "timestamp", "previous_block_hash",
                "nonce", "miner_transaction", "transaction_hashes"],
                "max_block_size": "configurable via block grant",
                "block_time_target_seconds": CHAIN_CONFIG["mainnet"]["block_time_seconds"],
                "blocks_per_day": CHAIN_CONFIG["mainnet"]["blocks_per_day"]}

        elif name == "get_token_supply":
            supply = CHAIN_CONFIG["mainnet"]["money_supply"] * 100000000
            return {"total_supply": supply, "total_supply_xfg": f"{supply / 1e8:,.2f} XFG",
                "atomic_units_per_coin": 100000000, "symbol": "XFG", "name": "Fuego",
                "decimal_places": CHAIN_CONFIG["mainnet"]["decimal_places"],
                "coin": CHAIN_CONFIG["mainnet"]["coin"], "mined": True, "premine": False,
                "emission_curve": "Linear per block"}

        elif name == "get_fee_structure":
            return FEE_CONFIG

        # ── Currency / Monetary ──
        elif name == "get_coin_denominations":
            return {"name": "Fuego", "symbol": "XFG", "atomic_unit_name": "atomic unit",
                "atomic_per_coin": 100000000, "decimal_places": 8,
                "display_format": "1234.56789000 XFG", "display_example": "100000000 atomic = 1.00000000 XFG"}

        elif name == "calculate_emission":
            return calculate_emission_at(v("height", 0))

        elif name == "calculate_block_reward":
            return calculate_block_reward_at(v("height", 0))

        # ── Wallets ──
        elif name == "get_wallet_info":
            return WALLET_INFO

        elif name == "get_address_info":
            return WALLET_INFO | {"prefix": WALLET_INFO["address_prefix"],
                "standard_length": WALLET_INFO["address_length"],
                "integrated_length": WALLET_INFO["integrated_address_length"],
                "subaddress_support": True, "public_address_format": "base58 [prefix + public_spend + public_view + checksum]"}

        elif name == "get_mnemonic_info":
            languages = ["english", "english_old", "french", "german", "italian", "spanish",
                "portuguese", "dutch", "russian", "japanese", "chinese_simplified",
                "esperanto", "lojban"]
            return {"algorithm": "Electrum-style", "word_count": 25, "languages": languages,
                "language_count": len(languages), "generates": "private spend + view keys"}

        elif name == "get_wallet_rpc_methods":
            return {"wallet_type": "WalletGreen (primary)", "rpc_server": "WalletRpcServer",
                "methods": RPC_METHODS_WALLET, "method_count": len(RPC_METHODS_WALLET)}

        # ── RPC / API ──
        elif name == "get_daemon_rpc_methods":
            return {"rpc_server": "RpcServer (HTTP/JSON-RPC)", "methods": RPC_METHODS_DAEMON,
                "method_count": len(RPC_METHODS_DAEMON)}

        elif name == "get_rpc_error_codes":
            code = v("code")
            errors = {-1: "Unknown error", -2: "Method not found", -3: "Invalid params",
                -4: "Internal error", -5: "Parse error", -101: "Core busy",
                -102: "Invalid block", -103: "Invalid transaction", -104: "Fee too low"}
            if code:
                return {"code": code, "meaning": errors.get(code, "Unknown code")}
            return errors

        elif name == "get_payment_gateway_info":
            return {"service": "PaymentGateService", "rpc_port": 8070,
                "features": ["Incoming payment monitoring", "Staged deposit unlock RPC",
                    "Wallet daemon mode", "Interactive wallet CLI"],
                "source": "src/PaymentGate/ and src/PaymentGateService/"}

        # ── Transactions ──
        elif name == "analyze_tx_structure":
            return {"inputs": ["Key input (ring signed)", "Deposit input (COLD/HEAT)"],
                "outputs": ["Key output (stealth address)", "Deposit output (COLD commitment)",
                    "Burn output (STARK commitment)", "Change output"],
                "extra_fields": ["Payment ID (encrypted or plain)", "Deposit commitment",
                    "Burn proof data", "Alias registration data",
                    "Swap commitment data (adaptor/MuSig2)"],
                "signatures": ["RingCT (MLSAG)", "Adaptor signatures", "MuSig2 aggregated"]}

        elif name == "get_tx_types":
            return [{"type": k, "description": v} for k, v in TX_TYPES.items()]

        elif name == "calculate_tx_fee":
            return calculate_tx_fee(v("tx_size_bytes", 1000), v("priority", "low"))

        elif name == "get_ring_signature_info":
            return {"algorithm": "MLSAG (Multi-layered Linkable Spontaneous Anonymous Group)",
                "dynamic_ring_sizes": True, "sizes_by_age": {"<1 day": 18, "<1 week": 15,
                    "<1 month": 12, "<6 months": 10, ">=6 months": 8},
                "minimum_ring_size": 8, "decoy_source": "OSPEAD-inspired algorithm",
                "documentation": "docs/DYNAMIC_RING_SIZE.md"}

        # ── Cryptography ──
        elif name == "get_crypto_primitives":
            return CRYPTO

        elif name == "get_signature_schemes":
            return SIGNATURE_SCHEMES

        elif name == "get_hash_functions":
            return {"primary": "Keccak (SHA-3 variant) for cn_fast_hash",
                "variants": {"Blake2b": "Fast hashing", "Blake256": "CryptoNight component",
                    "Groestl": "CryptoNight component", "JH": "CryptoNight component",
                    "Skein": "CryptoNight component",
                    "CryptoNight": "Slow hash for PoW (variant 2)"},
                "tree_hash": "Merkle tree hash", "files": "src/crypto/hash.cpp, src/crypto/*.c",
                "source_dir": "src/crypto/"}

        # ── Mining ──
        elif name == "get_mining_info":
            return MINING_INFO

        elif name == "get_difficulty_info":
            return {"algorithm": "DMWDA (Dynamic Multi-Window Difficulty Adjustment)",
                "windows": {"short": {"blocks": 15, "purpose": "Rapid response"},
                    "medium": {"blocks": 45, "purpose": "Stable adjustment"},
                    "long": {"blocks": 120, "purpose": "Long-term averaging"},
                    "emergency": {"blocks": 5, "purpose": "Hashrate crash recovery"}},
                "features": ["Confidence scoring", "Anomaly detection",
                    "Smoothing between windows"],
                "implementation": "src/CryptoNoteCore/AdaptiveDifficulty.cpp"}

        elif name == "get_mining_config":
            return {"algorithm": "CryptoNight (variant 2, slow hash)",
                "cpu_mining": True, "gpu_mining": False, "pool_mining": True,
                "solo_mining": True, "block_template_source": "daemon RPC",
                "config_file": "miner.conf or CLI args",
                "source": "src/Miner/", "binary": "src/Miner/main.cpp"}

        # ── P2P ──
        elif name == "analyze_peer_health":
            return analyze_peer_health(v("peer_count", 0), v("target_count", 8))

        elif name == "get_p2p_info":
            return {"protocol": "Levin (CryptoNote P2P)",
                "messages": ["HANDSHAKE", "TIMED_SYNC", "NOTIFY_REQUEST_CHAIN",
                    "NOTIFY_RESPONSE_CHAIN_ENTRY", "NOTIFY_NEW_BLOCK", "NOTIFY_NEW_TRANSACTIONS"],
                "peer_lists": {"white": "Reliable peers", "gray": "Discovered but untested"},
                "port": CHAIN_CONFIG["mainnet"]["default_port"],
                "max_connections": "configurable (default ~100)",
                "seed_nodes": "defined in CryptoNoteConfig.h"}

        elif name == "get_anonymity_info":
            return {"tor": {"status": "integrated", "features": ["SOCKS5 proxy", "Hidden service management"],
                    "source": "src/FuegoTor/"},
                "i2p": {"status": "integrated", "features": ["SOCKS5", "Hidden services", "Connection stats"],
                    "source": "src/FuegoI2P/"},
                "meshtastic": {"status": "experimental", "features": ["Off-grid mesh network relay"],
                    "source": "src/FuegoMeshtastic/"}}

        # ── Atomic Swaps ──
        elif name == "get_swap_info":
            return {"protocols": ["Adaptor signature (primary)", "HTLC (legacy)"],
                "state_count": 7, "states": SWAP_STATES,
                "valid_transitions": SWAP_TRANSITIONS,
                "cross_chain_pairs": [{"asset": "XMR", "type": "Monero", "status": "active"},
                    {"asset": "ETH", "type": "Ethereum", "status": "active"},
                    {"asset": "BCH", "type": "Bitcoin Cash", "status": "active"},
                    {"asset": "SOL", "type": "Solana", "status": "active"}],
                "fee_rate_bps": FEE_CONFIG["swap_fee_rate_bps"],
                "escrow_scheme": "MuSig2 2-of-2 multisig",
                "daemon_source": "src/SwapDaemon/"}

        elif name == "calculate_swap_fee":
            return analyze_swap_fee(v("xfg_amount", 0))

        elif name == "validate_swap_state":
            return {"current": v("current_state"), "target": v("target_state"),
                "valid": v("target_state") in SWAP_TRANSITIONS.get(v("current_state"), [])}

        elif name == "get_swap_state_name":
            sid = v("state_id")
            return {"state_id": sid, "name": f"ADAPTOR_{SWAP_STATES.get(sid, 'UNKNOWN')}"}

        elif name == "get_lp_pool_info":
            return {"status": "deferred (v11)", "algorithm": "Constant product AMM (x*y=k)",
                "features": ["LP share minting/burning", "Fee accumulation", "Merkle checkpoint proofs"],
                "source": "src/SwapDaemon/pool_v11/",
                "design_doc": "docs/decentralized-lp-organizer.md"}

        # ── Deposits / Burns ──
        elif name == "calculate_cd_interest":
            interest = calculate_cd_interest(v("amount", 0), v("creation_height", 0),
                v("current_height", 0), v("epoch_fee_rates", []))
            return {"amount": v("amount"), "creation_height": v("creation_height"),
                "current_height": v("current_height"), "epochs_analyzed": len(v("epoch_fee_rates", [])),
                "interest": interest, "interest_xfg": f"{interest / 1e8:.4f} XFG",
                "total_value": v("amount", 0) + interest}

        elif name == "estimate_apy":
            apy = estimate_apy(v("current_epoch_fee", 0), v("total_cd_locked", 0))
            return {"current_epoch_fee": v("current_epoch_fee"), "total_cd_locked": v("total_cd_locked"),
                "estimated_apy_pct": round(apy, 2)}

        elif name == "get_deposit_info":
            return {"types": [
                    {"name": "COLD CD", "description": "Certificate of Deposit, earns swap fee interest",
                     "min_amount": f"{CD_CONFIG['min_amount']} atomic ({CD_CONFIG['min_amount']/1e8:.2f} XFG)",
                     "min_term_blocks": CD_CONFIG["min_term"], "max_term_blocks": CD_CONFIG["max_term"],
                     "staged_unlock": True, "interest_rate": "Variable (epoch fee pool)"},
                    {"name": "HEAT Burn", "description": "Permanent XFG lock, yields HEAT token",
                     "format": "STARK commitment (56-byte preimage)", "tracking": "Ethereal supply via BankingIndex",
                     "convertible": "XFG -> HEAT via conversion rate"},
                    {"name": "Investment Deposit", "description": "Legacy deposit type", "tracking": "InvestmentIndex"}],
                "staged_unlock": {"stages": CD_CONFIG["staged_unlock_stages"],
                    "interval_blocks": CD_CONFIG["staged_unlock_interval_blocks"],
                    "per_stage_pct": 100 // CD_CONFIG["staged_unlock_stages"]},
                "epoch_fee_pool": {"duration_blocks": CD_CONFIG["epoch_duration_blocks"],
                    "cd_share_pct": CD_CONFIG["cd_share_pct"],
                    "treasury_share_pct": CD_CONFIG["treasury_share_pct"],
                    "precision": CD_CONFIG["fee_pool_precision"]}}

        # ── Privacy ──
        elif name == "get_privacy_model":
            return PRIVACY_INFO

        elif name == "get_decoy_info":
            return {"algorithm": "OSPEAD-inspired",
                "features": ["Age-binned output analysis", "Logarithmic binning",
                    "Spend probability matching"],
                "implementation": "src/CryptoNoteCore/OSPEADDecoySelection.cpp",
                "interface": "include/OSPEADDecoySelection.h",
                "ring_size_calc": "include/DynamicRingSize.h",
                "documentation": "docs/PRIVACY_ROADMAP.md"}

        # ── Contracts / HEAT ──
        elif name == "get_contract_info":
            return {"contracts": [
                    {"name": "FuegoCheckpointVerifier", "language": "Solidity",
                     "purpose": "ZK-verified checkpoint advancement for trustless bridging",
                     "proof_system": "SP1 (zkVM)"},
                    {"name": "HEATClaimer", "language": "Solidity",
                     "purpose": "Claim HEAT tokens via burn commitment preimage + Merkle proof"}],
                "bridge_direction": "Fuego -> Ethereum (one-way for claims)",
                "zk_prover": {"system": "Winterfell STARKs", "language": "Rust",
                    "source": "fuego-prover/"}}

        elif name == "get_heat_info":
            return {"burn_mechanism": "STARK commitment (56-byte preimage: amount + destination + metadata)",
                "nullifier": "Anti-double-spend via nullifier in BurnProofDataFileGenerator",
                "validation": "BurnDepositValidationService",
                "conversion_rate": "Currency::convertXfgToHeat / convertHeatToXfg",
                "tracking": "BankingIndex (ethereal XFG supply)",
                "relay": "STARK proof data files for external verification",
                "ethereum_contract": "HEATClaimer.sol"}

        # ── Aliases ──
        elif name == "get_alias_info":
            return {"types": [
                    {"name": "Elderfier alias", "charset": "A-Z, 0-9, & (uppercase)", "fee": "Free",
                     "note": "For verified Elderfier operators"},
                    {"name": "Regular alias", "charset": "a-z, 0-9, & (lowercase)", "fee": "1 XFG"}],
                "privacy": "Stores addressHash (cn_fast_hash), not raw address",
                "lookup_versions": {"V1": "String-based", "V2": "Hash-based (current)"},
                "source": "src/CryptoNoteCore/AliasIndex.cpp",
                "documentation": "docs/FUEGO_CHAIN_ALIAS_SYSTEM.md"}

        # ── Codebase Mapper ──
        elif name == "scan_codebase":
            return _scan(v("force_rescan", False))

        elif name == "search_files":
            _, c = _db()
            c.execute("SELECT path, type, lang, lines FROM files WHERE path LIKE ? LIMIT ?",
                     (f"%{v('query','')}%", v("limit", 20)))
            r = c.fetchall()
            return {"query": v("query"), "count": len(r),
                "results": [{"path": x[0], "type": x[1], "language": x[2], "lines": x[3]} for x in r]}

        elif name == "search_functions":
            _, c = _db()
            c.execute("SELECT name, file, line FROM funcs WHERE name LIKE ? LIMIT ?",
                     (f"%{v('name','')}%", v("limit", 20)))
            r = c.fetchall()
            return {"query": v("name"), "count": len(r),
                "results": [{"name": x[0], "file": x[1], "line": x[2]} for x in r]}

        elif name == "get_codebase_stats":
            _, c = _db()
            c.execute("SELECT COUNT(*), COALESCE(SUM(lines),0) FROM files")
            tf, tl = c.fetchone()
            c.execute("SELECT COUNT(*) FROM funcs")
            tfn = c.fetchone()[0]
            c.execute("SELECT lang, COUNT(*) FROM files GROUP BY lang")
            langs = dict(c.fetchall())
            return {"total_files": tf, "total_lines": tl, "total_functions": tfn, "languages": langs}

        elif name == "get_file_tree":
            max_depth = v("depth", 3)
            def _render(path, depth):
                if depth > max_depth:
                    return ""
                parts = []
                for x in sorted(path.iterdir(), key=lambda e: (not e.is_dir(), e.name)):
                    rel = x.relative_to(ROOT)
                    if x.is_dir() and not x.name.startswith("."):
                        parts.append(f"{'  '*depth}{x.name}/\n" + _render(x, depth+1))
                    elif x.is_file() and x.suffix in {'.cpp','.h','.go','.py','.md'}:
                        parts.append(f"{'  '*depth}{x.name}  ({rel})\n")
                return "".join(parts)
            return {"tree": _render(ROOT, 0)}

        elif name == "find_files_by_type":
            _, c = _db()
            c.execute("SELECT path, lines, lang FROM files WHERE type=? LIMIT ?",
                     (v("file_type"), v("limit", 20)))
            r = c.fetchall()
            return {"file_type": v("file_type"), "count": len(r),
                "results": [{"path": x[0], "lines": x[1], "language": x[2]} for x in r]}

        elif name == "get_source_domains":
            return [{"domain": d, "path": p, "description": desc} for d, p, desc in PLATFORM_DOMAINS]

        elif name == "analyze_code_section":
            pat = v("pattern", "")
            files = []
            for f in ROOT.rglob("*"):
                if f.suffix in {".cpp", ".h", ".go", ".py", ".md"} and ".git" not in str(f):
                    try:
                        text = f.read_text(errors="ignore")
                        if re.search(pat, text, re.IGNORECASE):
                            matches = [(m.group()[:80], m.start()) for m in re.finditer(pat, text, re.IGNORECASE)][:5]
                            files.append({"file": str(f.relative_to(ROOT)), "matches": len(list(re.finditer(pat, text, re.IGNORECASE))),
                                "samples": [{"text": t, "offset": o} for t, o in matches]})
                    except: pass
            return {"pattern": pat, "files_found": len(files), "results": files[:50]}

        # ── RAG ──
        elif name == "build_rag_index":
            _rag["docs"] = _discover(v("limit", 50))
            _rag["chunks"] = _chunk(_rag["docs"])
            return {"documents_indexed": len(_rag["docs"]), "chunks_created": len(_rag["chunks"])}

        elif name == "search_codebase":
            terms = v("query", "").lower().split()
            scored = []
            for ch in _rag["chunks"]:
                score = sum(1 for t in terms if t in ch["content"].lower())
                if any(t in {"cd", "deposit", "interest", "fee"} for t in terms) and ch["type"] in {"cpp", "doc"}:
                    score += 2
                if score:
                    scored.append((score, ch))
            scored.sort(key=lambda x: x[0], reverse=True)
            results = [{"path": c["path"], "type": c["type"], "preview": c["content"][:200],
                       "total_length": len(c["content"])} for _, c in scored[:v("top_k", 5)]]
            return {"query": v("query"), "count": len(results), "results": results}

        elif name == "generate_context":
            if not _rag["chunks"]:
                _rag["docs"] = _discover(50)
                _rag["chunks"] = _chunk(_rag["docs"])
            terms = v("query", "").lower().split()
            scored = []
            for ch in _rag["chunks"]:
                score = sum(1 for t in terms if t in ch["content"].lower())
                if score:
                    scored.append((score, ch))
            scored.sort(key=lambda x: x[0], reverse=True)
            ctx = ""
            for i, (_, ch) in enumerate(scored[:v("top_k", 5)]):
                ctx += f"\n--- Source {i+1}: {ch['path']} ---\n{ch['content'][:500]}\n"
            return {"query": v("query"), "context": ctx if ctx else "No relevant chunks found. Run build_rag_index first."}

        raise ValueError(f"Unknown tool: {name}")


# ═══════════════════════════════════════════════════════════════════════════
# MCP PROTOCOL (JSON-RPC over stdio)
# ═══════════════════════════════════════════════════════════════════════════

def serve():
    import sys
    srv = FuegoMCPServer()
    sys.stderr.write(f"[fuego-mcp] Server ready at {ROOT}\n")
    sys.stderr.flush()
    for line in sys.stdin:
        line = line.strip()
        if not line: continue
        try:
            msg = json.loads(line)
            method, params, rid = msg.get("method"), msg.get("params", {}), msg.get("id")
            if method == "initialize":
                sys.stdout.write(json.dumps({"jsonrpc":"2.0","id":rid,"result":{
                    "protocolVersion":"2024-11-05","capabilities":{"tools":{}},
                    "serverInfo":{"name":"fuego-mcp","version":"2.0.0"}}})+"\n")
            elif method == "notifications/initialized":
                pass
            elif method == "tools/list":
                tools = [{"name":n, "description":d["description"], "inputSchema":d["inputSchema"]}
                         for n,d in srv.tools.items()]
                sys.stdout.write(json.dumps({"jsonrpc":"2.0","id":rid,"result":{"tools":tools}})+"\n")
            elif method == "tools/call":
                try:
                    r = srv.dispatch(params.get("name"), params.get("arguments", {}))
                    t = json.dumps(r, indent=2) if not isinstance(r, str) else r
                    sys.stdout.write(json.dumps({"jsonrpc":"2.0","id":rid,
                        "result":{"content":[{"type":"text","text":t}]}})+"\n")
                except Exception as e:
                    sys.stdout.write(json.dumps({"jsonrpc":"2.0","id":rid,
                        "error":{"code":-32603,"message":str(e)}})+"\n")
            else:
                sys.stdout.write(json.dumps({"jsonrpc":"2.0","id":rid,
                    "error":{"code":-32601,"message":f"Unknown:{method}"}})+"\n")
            sys.stdout.flush()
        except json.JSONDecodeError:
            pass


def cli():
    import argparse
    p = argparse.ArgumentParser(description="Fuego MCP Server v2.0 - 40+ tools across all blockchain domains")
    p.add_argument("--mcp", action="store_true", help="Run as MCP server (stdio)")
    p.add_argument("tool", nargs="?", help="Tool name (direct call)")
    p.add_argument("args", nargs="*", help="key=value arguments")
    a = p.parse_args()

    if a.mcp:
        serve(); return

    if a.tool:
        srv = FuegoMCPServer()
        if a.tool not in srv.tools:
            print(f"Unknown tool: {a.tool}")
            print(f"Available ({len(srv.tools)}):")
            for n in sorted(srv.tools):
                print(f"  {n}")
            return
        kwargs = {}
        for kv in a.args:
            if "=" in kv:
                k, v = kv.split("=", 1)
                try: v = int(v)
                except: 
                    try: v = float(v)
                    except: pass
                kwargs[k] = v
        try:
            r = srv.dispatch(a.tool, kwargs)
            print(json.dumps(r, indent=2))
        except Exception as e:
            print(f"Error: {e}")
        return

    print(f"Fuego MCP Server v2.0 — {len(FuegoMCPServer().tools)} tools across ALL domains")
    print("="*60)
    cats = {"BLOCKCHAIN CORE": [], "CURRENCY": [], "WALLETS": [], "RPC": [],
            "TRANSACTIONS": [], "CRYPTO": [], "MINING": [], "P2P": [],
            "SWAPS": [], "DEPOSITS": [], "PRIVACY": [], "CONTRACTS": [],
            "ALIASES": [], "MAPPER": [], "EXPLORER": [], "RAG": []}
    mapping = {**{n: "BLOCKCHAIN CORE" for n in ["get_chain_info","get_config_constants","get_upgrade_schedule",
        "get_block_info","get_token_supply","get_fee_structure"]},
        **{n: "CURRENCY" for n in ["get_coin_denominations","calculate_emission","calculate_block_reward"]},
        **{n: "WALLETS" for n in ["get_wallet_info","get_address_info","get_mnemonic_info","get_wallet_rpc_methods"]},
        **{n: "RPC" for n in ["get_daemon_rpc_methods","get_rpc_error_codes","get_payment_gateway_info"]},
        **{n: "TRANSACTIONS" for n in ["analyze_tx_structure","get_tx_types","calculate_tx_fee","get_ring_signature_info"]},
        **{n: "CRYPTO" for n in ["get_crypto_primitives","get_signature_schemes","get_hash_functions"]},
        **{n: "MINING" for n in ["get_mining_info","get_difficulty_info","get_mining_config"]},
        **{n: "P2P" for n in ["analyze_peer_health","get_p2p_info","get_anonymity_info"]},
        **{n: "SWAPS" for n in ["get_swap_info","calculate_swap_fee","validate_swap_state","get_swap_state_name","get_lp_pool_info"]},
        **{n: "DEPOSITS" for n in ["calculate_cd_interest","estimate_apy","get_deposit_info"]},
        **{n: "PRIVACY" for n in ["get_privacy_model","get_decoy_info"]},
        **{n: "CONTRACTS" for n in ["get_contract_info","get_heat_info"]},
        **{n: "ALIASES" for n in ["get_alias_info"]},
        **{n: "MAPPER" for n in ["scan_codebase","search_files","search_functions","get_codebase_stats","get_file_tree","find_files_by_type"]},
        **{n: "EXPLORER" for n in ["get_source_domains","analyze_code_section"]},
        **{n: "RAG" for n in ["build_rag_index","search_codebase","generate_context"]}}
    for n in sorted(srv.tools):
        cat = mapping.get(n, "OTHER")
        cats.setdefault(cat, []).append(n)
    for cat, tools in cats.items():
        if tools:
            print(f"\n{'─'*5} {cat} ({len(tools)} tools)")
            for t in tools:
                print(f"  {t}: {srv.tools[t]['description']}")
    print(f"\nUsage:  python fuego_mcp_server.py <tool_name> [key=value ...]")
    print(f"Server: python fuego_mcp_server.py --mcp")


if __name__ == "__main__":
    cli()
