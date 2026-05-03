//! Fuego Daemon RPC Client
//!
//! Provides a blocking HTTP client for querying the Fuego daemon's
//! commitment index, merkle proofs, and consensus data.
//! Used by xfg-stark-cli to fetch on-chain data for proof packaging.
//!
//! ## Connection Strategy
//!
//! The client tries to connect in order:
//! 1. User-specified daemon address (if provided via `--daemon` flag)
//! 2. Localhost (`127.0.0.1`) on the default RPC port
//! 3. Known seed nodes on their RPC ports (fallback for users without a local daemon)
//!
//! Commitment data is network-wide state — any fully synced node can serve it.

use serde::{Deserialize, Serialize};

/// Default Fuego daemon RPC port (mainnet)
pub const DEFAULT_RPC_PORT: u16 = 18180;
/// Default Fuego daemon RPC port (testnet)
pub const DEFAULT_TESTNET_RPC_PORT: u16 = 28280;

/// Mainnet seed nodes (IPs from CryptoNoteConfig.h, using RPC port 18180)
pub const MAINNET_SEED_RPC_NODES: &[(&str, u16)] = &[
    ("3.16.217.33", 18180),
    ("80.89.228.157", 18180),
    ("207.244.247.64", 18180),
    ("216.145.66.224", 18180),
];

/// Testnet seed nodes (IPs from CryptoNoteConfig.h, using RPC port 28280)
pub const TESTNET_SEED_RPC_NODES: &[(&str, u16)] = &[
    ("103.101.201.136", 28280),
    ("216.145.84.248", 28280),
    ("80.89.228.157", 28280),
    ("207.244.247.64", 28280),
    ("216.145.66.224", 28280),
];

/// Which network the client is targeting
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum FuegoNetwork {
    /// Mainnet (RPC port 18180)
    Mainnet,
    /// Testnet (RPC port 28280)
    Testnet,
}

/// Fuego daemon RPC client
pub struct FuegoRpcClient {
    /// Base URL for RPC calls (e.g., "http://127.0.0.1:18180")
    base_url: String,
    /// HTTP client
    client: reqwest::blocking::Client,
    /// Which network we're on (for fallback node selection)
    network: FuegoNetwork,
    /// Whether this client was created via auto-connect (for logging)
    is_auto_connected: bool,
    /// Description of connected node (for user display)
    connected_to: String,
}

// ──────────────────────────────────────────────
// Response types mirroring C++ RPC structs
// ──────────────────────────────────────────────

/// Response from /get_commitment
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CommitmentResponse {
    /// Whether the commitment was found
    pub found: bool,
    /// Hex commitment hash
    #[serde(default)]
    pub commitment_hash: String,
    /// Hex transaction hash
    #[serde(default)]
    pub tx_hash: String,
    /// Block height of the commitment
    #[serde(default)]
    pub block_height: u32,
    /// Amount in atomic units
    #[serde(default)]
    pub amount: u64,
    /// Deposit term (0xFFFFFFFF for HEAT, blocks for COLD)
    #[serde(default)]
    pub term: u32,
    /// Type: 0=HEAT, 1=YIELD/COLD, 2=ELDERFIER_STAKING
    #[serde(default, rename = "type")]
    pub commitment_type: u8,
    /// Target chain ID
    #[serde(default)]
    pub target_chain_id: u32,
    /// Leaf index in merkle tree
    #[serde(default)]
    pub leaf_index: u32,
    /// RPC status
    #[serde(default)]
    pub status: String,
}

/// Response from /get_commitment_stats
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CommitmentStatsResponse {
    /// Total number of commitments indexed
    pub total_commitments: u64,
    /// Number of HEAT burn commitments
    pub heat_commitments: u64,
    /// Number of COLD deposit commitments
    pub cold_commitments: u64,
    /// Highest block with commitments
    pub highest_block: u32,
    /// Current merkle root (hex)
    pub merkle_root: String,
    /// Elderfier consensus percentage
    pub consensus_percentage: u64,
    /// Signed elderfier IDs
    #[serde(default)]
    pub signed_elderfier_ids: Vec<u8>,
    /// Pending elderfier IDs
    #[serde(default)]
    pub pending_elderfier_ids: Vec<u8>,
    /// RPC status
    pub status: String,
}

/// Response from /get_commitment_merkle_root
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MerkleRootResponse {
    /// Hex merkle root
    pub merkle_root: String,
    /// Total leaves in tree
    pub total_leaves: u64,
    /// Highest block indexed
    pub highest_block: u32,
    /// Consensus percentage
    pub consensus_percentage: u64,
    /// RPC status
    pub status: String,
}

/// Response from /get_commitment_merkle_proof
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MerkleProofResponse {
    /// Whether the commitment was found
    pub found: bool,
    /// Current merkle root (hex)
    #[serde(default)]
    pub merkle_root: String,
    /// The commitment being proved (hex)
    #[serde(default)]
    pub leaf_hash: String,
    /// Sibling hashes along the proof path (hex)
    #[serde(default)]
    pub proof_path: Vec<String>,
    /// Direction indices (0=left, 1=right) at each level
    #[serde(default)]
    pub proof_indices: Vec<u32>,
    /// Leaf index in tree
    #[serde(default)]
    pub leaf_index: u32,
    /// Consensus percentage at time of query
    #[serde(default)]
    pub consensus_percentage: u64,
    /// RPC status
    #[serde(default)]
    pub status: String,
}

/// A single EFier signature entry from get_elderfier_signatures RPC
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ElderfierSignatureInfo {
    /// Elderfier ID (0-255)
    pub elderfier_id: u8,
    /// Ed25519 signing pubkey (hex, 64 chars)
    #[serde(default)]
    pub signing_pubkey: String,
    /// Ed25519 signature (hex, 128 chars)
    #[serde(default)]
    pub signature: String,
    /// Block height when signed
    pub block_height: u64,
    /// Signature timestamp
    pub timestamp: u64,
    /// Whether signature passed local verification
    pub is_valid: bool,
}

/// Response from /get_elderfier_signatures
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ElderfierSignaturesResponse {
    /// Validated signatures with pubkeys
    #[serde(default)]
    pub signatures: Vec<ElderfierSignatureInfo>,
    /// Current merkle root (hex)
    #[serde(default)]
    pub current_merkle_root: String,
    /// Current block height
    pub current_block_height: u64,
    /// Total registered EFiers
    pub total_registered_elderfiers: u64,
    /// Number of signatures received
    pub signatures_received: u64,
    /// Consensus percentage
    pub consensus_percentage: u8,
    /// Whether threshold is met (>= 69%)
    pub threshold_met: bool,
    /// EFiDs that have signed
    #[serde(default)]
    pub signed_by: Vec<u8>,
    /// EFiDs still pending
    #[serde(default)]
    pub pending: Vec<u8>,
    /// RPC status
    pub status: String,
}

/// Response from /check_commitment_exists
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CommitmentExistsResponse {
    /// Whether the commitment exists
    pub exists: bool,
    /// RPC status
    pub status: String,
}

/// Response from /get_height
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HeightResponse {
    /// Current blockchain height
    pub height: u64,
    /// RPC status
    pub status: String,
}

// ──────────────────────────────────────────────
// Request types
// ──────────────────────────────────────────────

#[derive(Debug, Serialize)]
struct CommitmentHashRequest {
    commitment_hash: String,
}

#[derive(Debug, Serialize)]
struct EmptyRequest {}

// ──────────────────────────────────────────────
// Client implementation
// ──────────────────────────────────────────────

impl FuegoRpcClient {
    /// Create a new RPC client for mainnet (port 18180)
    pub fn new_mainnet(host: &str) -> Self {
        Self::new(host, DEFAULT_RPC_PORT)
    }

    /// Create a new RPC client for testnet (port 28280)
    pub fn new_testnet(host: &str) -> Self {
        Self::new(host, DEFAULT_TESTNET_RPC_PORT)
    }

    /// Create a new RPC client with custom host and port
    pub fn new(host: &str, port: u16) -> Self {
        let network = if port == DEFAULT_TESTNET_RPC_PORT {
            FuegoNetwork::Testnet
        } else {
            FuegoNetwork::Mainnet
        };
        Self {
            base_url: format!("http://{}:{}", host, port),
            client: Self::build_client(30),
            network,
            is_auto_connected: false,
            connected_to: format!("{}:{}", host, port),
        }
    }

    /// Auto-connect: tries localhost first, then seed nodes as fallback.
    ///
    /// This is the recommended constructor for CLI tools. It ensures the user
    /// can always query commitment data even without a local daemon running,
    /// since commitment/merkle data is network-wide state that any synced node serves.
    pub fn auto_connect(network: FuegoNetwork) -> crate::Result<Self> {
        let (default_port, seed_nodes) = match network {
            FuegoNetwork::Mainnet => (DEFAULT_RPC_PORT, MAINNET_SEED_RPC_NODES),
            FuegoNetwork::Testnet => (DEFAULT_TESTNET_RPC_PORT, TESTNET_SEED_RPC_NODES),
        };

        // Build a short-timeout client for connection probing
        let probe_client = Self::build_client(5);

        // 1. Try localhost first (most common case — user runs their own node)
        let localhost_url = format!("http://127.0.0.1:{}", default_port);
        if Self::probe_node(&probe_client, &localhost_url) {
            return Ok(Self {
                base_url: localhost_url,
                client: Self::build_client(30),
                network,
                is_auto_connected: true,
                connected_to: format!("localhost:{} (local daemon)", default_port),
            });
        }

        // 2. Try seed nodes as fallback
        for (host, port) in seed_nodes {
            let url = format!("http://{}:{}", host, port);
            if Self::probe_node(&probe_client, &url) {
                return Ok(Self {
                    base_url: url,
                    client: Self::build_client(30),
                    network,
                    is_auto_connected: true,
                    connected_to: format!("{}:{} (seed node)", host, port),
                });
            }
        }

        // 3. Nothing reachable — return error
        Err(crate::XfgStarkError::ParseError(format!(
            "Cannot reach any Fuego daemon. Tried localhost:{} and {} seed nodes. \
             Start fuegod locally or check your network connection.",
            default_port,
            seed_nodes.len()
        )))
    }

    /// Auto-connect with an optional user override.
    ///
    /// If `daemon_addr` is Some("host:port"), connects directly to that address.
    /// Otherwise, falls back to auto_connect() (localhost → seed nodes).
    pub fn connect(daemon_addr: Option<&str>, network: FuegoNetwork) -> crate::Result<Self> {
        match daemon_addr {
            Some(addr) => {
                let parts: Vec<&str> = addr.split(':').collect();
                let host = parts[0];
                let default_port = match network {
                    FuegoNetwork::Mainnet => DEFAULT_RPC_PORT,
                    FuegoNetwork::Testnet => DEFAULT_TESTNET_RPC_PORT,
                };
                let port: u16 = if parts.len() > 1 {
                    parts[1].parse().unwrap_or(default_port)
                } else {
                    default_port
                };
                Ok(Self::new(host, port))
            }
            None => Self::auto_connect(network),
        }
    }

    /// Human-readable description of which node we're connected to
    pub fn connected_to(&self) -> &str {
        &self.connected_to
    }

    /// Whether this client auto-connected (vs explicit address)
    pub fn is_auto_connected(&self) -> bool {
        self.is_auto_connected
    }

    /// Which network this client targets
    pub fn network(&self) -> FuegoNetwork {
        self.network
    }

    /// Check if the daemon is reachable
    pub fn health_check(&self) -> bool {
        self.get_height().is_ok()
    }

    /// Get current blockchain height
    pub fn get_height(&self) -> crate::Result<HeightResponse> {
        self.post_json("/get_height", &EmptyRequest {})
    }

    /// Look up a commitment by its hash
    pub fn get_commitment(&self, commitment_hash: &str) -> crate::Result<CommitmentResponse> {
        self.post_json("/get_commitment", &CommitmentHashRequest {
            commitment_hash: commitment_hash.to_string(),
        })
    }

    /// Get commitment index statistics
    pub fn get_commitment_stats(&self) -> crate::Result<CommitmentStatsResponse> {
        self.post_json("/get_commitment_stats", &EmptyRequest {})
    }

    /// Get current merkle root
    pub fn get_merkle_root(&self) -> crate::Result<MerkleRootResponse> {
        self.post_json("/get_commitment_merkle_root", &EmptyRequest {})
    }

    /// Get merkle proof for a commitment (the key bridge function)
    pub fn get_merkle_proof(&self, commitment_hash: &str) -> crate::Result<MerkleProofResponse> {
        self.post_json("/get_commitment_merkle_proof", &CommitmentHashRequest {
            commitment_hash: commitment_hash.to_string(),
        })
    }

    /// Get EFier signatures for current merkle root (with pubkeys, for L2 batching)
    pub fn get_elderfier_signatures(&self) -> crate::Result<ElderfierSignaturesResponse> {
        self.post_json("/get_elderfier_signatures", &EmptyRequest {})
    }

    /// Check if a commitment exists on-chain
    pub fn check_commitment_exists(&self, commitment_hash: &str) -> crate::Result<CommitmentExistsResponse> {
        self.post_json("/check_commitment_exists", &CommitmentHashRequest {
            commitment_hash: commitment_hash.to_string(),
        })
    }

    /// Build an HTTP client with given timeout in seconds
    fn build_client(timeout_secs: u64) -> reqwest::blocking::Client {
        reqwest::blocking::Client::builder()
            .timeout(std::time::Duration::from_secs(timeout_secs))
            .build()
            .expect("Failed to build HTTP client")
    }

    /// Probe whether a node is reachable by hitting /get_height
    fn probe_node(client: &reqwest::blocking::Client, base_url: &str) -> bool {
        let url = format!("{}/get_height", base_url);
        client
            .post(&url)
            .json(&EmptyRequest {})
            .send()
            .map(|r| r.status().is_success())
            .unwrap_or(false)
    }

    /// Internal: POST JSON to daemon endpoint and deserialize response
    fn post_json<Req: Serialize, Res: serde::de::DeserializeOwned>(
        &self,
        endpoint: &str,
        request: &Req,
    ) -> crate::Result<Res> {
        let url = format!("{}{}", self.base_url, endpoint);

        let response = self.client
            .post(&url)
            .json(request)
            .send()
            .map_err(|e| crate::XfgStarkError::ParseError(
                format!("RPC connection failed ({}): {}", url, e)
            ))?;

        if !response.status().is_success() {
            return Err(crate::XfgStarkError::ParseError(
                format!("RPC error: HTTP {}", response.status())
            ));
        }

        let res: Res = response.json()
            .map_err(|e| crate::XfgStarkError::ParseError(
                format!("RPC response parse error: {}", e)
            ))?;

        Ok(res)
    }
}

impl Default for FuegoRpcClient {
    fn default() -> Self {
        // Default tries auto-connect; falls back to localhost if all probes fail
        // (so callers that don't care about error handling still get a client)
        Self::auto_connect(FuegoNetwork::Mainnet).unwrap_or_else(|_| {
            Self::new_mainnet("127.0.0.1")
        })
    }
}
