// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/access/Ownable.sol";

/**
 * @title FuegoCommitmentMerkleVerifier
 * @dev Verifies Merkle proofs for Fuego HEAT/COLD commitments
 * @dev Elderfier consensus: registered Ed25519 pubkeys sign merkle roots on Fuego L1
 * @dev Root finalization: batch of EFier signatures verified once per root update
 * @dev Claims: just merkle proof against finalized root (no sig re-verification)
 *
 * Architecture:
 *   1. Owner registers EFier Ed25519 pubkeys (from elderking_ceremony on Fuego)
 *   2. Anyone can call submitRoot() with a batch of EFier signatures
 *   3. Contract verifies Ed25519 sigs against registered pubkeys
 *   4. When threshold met (e.g., 5 of 8), root is finalized
 *   5. HEATBurnProofVerifier / COLDProofVerifier call verifyCommitment()
 *      which only checks merkle proof against the finalized root (cheap)
 */
contract FuegoCommitmentMerkleVerifier is Ownable {

    /* ========================================================================== */
    /*                                   Events                                   */
    /* ========================================================================== */

    event MerkleRootFinalized(
        bytes32 indexed newRoot,
        uint64 commitmentCount,
        uint64 highestBlock,
        uint64 timestamp,
        uint8 signaturesVerified
    );

    event ElderfierRegistered(uint8 indexed efid, bytes32 pubkey);
    event ElderfierRemoved(uint8 indexed efid);
    event ThresholdUpdated(uint8 oldThreshold, uint8 newThreshold);
    event VerifierAuthorized(address indexed verifier);
    event VerifierRevoked(address indexed verifier);
    event NullifierUsed(bytes32 indexed nullifier, address indexed verifier);

    /* ========================================================================== */
    /*                                   Structs                                  */
    /* ========================================================================== */

    /// @dev Finalized merkle root with metadata
    struct FinalizedRoot {
        bytes32 root;
        uint64 commitmentCount;
        uint64 highestBlock;
        uint64 timestamp;
        uint8 sigCount;
    }

    /// @dev Registered Elderfier
    struct Elderfier {
        bytes32 pubkey;       // Ed25519 public key (32 bytes)
        bool active;
    }

    /* ========================================================================== */
    /*                                   State                                    */
    /* ========================================================================== */

    /// @dev Current finalized merkle root
    FinalizedRoot public currentRoot;

    /// @dev Historical finalized roots
    mapping(bytes32 => FinalizedRoot) public historicalRoots;

    /// @dev Registered Elderfiers by EFiD (0-255)
    mapping(uint8 => Elderfier) public elderfiers;
    uint8[] public activeEfids;

    /// @dev Signature threshold for root finalization
    uint8 public signatureThreshold;

    /// @dev Contracts authorized to mark nullifiers (HEAT/COLD verifiers)
    mapping(address => bool) public authorizedVerifiers;

    /// @dev Used nullifiers (shared across HEAT and COLD)
    mapping(bytes32 => bool) public usedNullifiers;

    /// @dev Ed25519 verifier contract (modular — can swap implementation)
    IEd25519Verifier public ed25519Verifier;

    /// @dev Minimum time between root updates
    uint64 public constant MIN_ROOT_INTERVAL = 5 minutes;

    /* ========================================================================== */
    /*                                 Constructor                                */
    /* ========================================================================== */

    /**
     * @param _genesisRoot Initial merkle root (from current Fuego commitment tree state)
     * @param _commitmentCount Number of commitments in genesis tree
     * @param _highestBlock Highest Fuego block in genesis tree
     * @param _efids Array of Elderfier IDs to register
     * @param _pubkeys Array of Ed25519 pubkeys (must match _efids length)
     * @param _threshold Minimum signatures for root finalization
     * @param _ed25519Verifier Address of Ed25519 verification contract
     */
    constructor(
        bytes32 _genesisRoot,
        uint64 _commitmentCount,
        uint64 _highestBlock,
        uint8[] memory _efids,
        bytes32[] memory _pubkeys,
        uint8 _threshold,
        address _ed25519Verifier
    ) Ownable(msg.sender) {
        require(_efids.length == _pubkeys.length, "EFiD/pubkey length mismatch");
        require(_efids.length >= _threshold, "Not enough EFiers for threshold");
        require(_threshold > 0, "Threshold must be > 0");
        require(_ed25519Verifier != address(0), "Invalid Ed25519 verifier");

        // Register EFiers
        for (uint i = 0; i < _efids.length; i++) {
            require(_pubkeys[i] != bytes32(0), "Invalid pubkey");
            elderfiers[_efids[i]] = Elderfier({pubkey: _pubkeys[i], active: true});
            activeEfids.push(_efids[i]);
            emit ElderfierRegistered(_efids[i], _pubkeys[i]);
        }

        signatureThreshold = _threshold;
        ed25519Verifier = IEd25519Verifier(_ed25519Verifier);

        // Set genesis root
        currentRoot = FinalizedRoot({
            root: _genesisRoot,
            commitmentCount: _commitmentCount,
            highestBlock: _highestBlock,
            timestamp: uint64(block.timestamp),
            sigCount: _threshold  // genesis is trusted
        });
        historicalRoots[_genesisRoot] = currentRoot;

        emit MerkleRootFinalized(
            _genesisRoot, _commitmentCount, _highestBlock,
            uint64(block.timestamp), _threshold
        );
    }

    /* ========================================================================== */
    /*                         Root Submission (batch)                             */
    /* ========================================================================== */

    /**
     * @dev Submit a new merkle root with a batch of EFier Ed25519 signatures
     * @dev Anyone can call this — the contract verifies signatures, not the caller
     * @dev Ed25519 verification happens ONCE here; claims just check merkle proof
     *
     * @param root New merkle root
     * @param commitmentCount Total commitments in tree
     * @param highestBlock Highest Fuego block included
     * @param efids Array of EFier IDs that signed
     * @param signatures Array of Ed25519 signatures (64 bytes each: R[32] + S[32])
     */
    function submitRoot(
        bytes32 root,
        uint64 commitmentCount,
        uint64 highestBlock,
        uint8[] calldata efids,
        bytes[] calldata signatures
    ) external {
        require(root != bytes32(0), "Invalid root");
        require(commitmentCount > 0, "No commitments");
        require(highestBlock > currentRoot.highestBlock, "Root not newer");
        require(efids.length == signatures.length, "EFiD/sig length mismatch");
        require(efids.length >= signatureThreshold, "Not enough signatures");
        require(
            block.timestamp >= currentRoot.timestamp + MIN_ROOT_INTERVAL,
            "Update too soon"
        );

        // Verify each signature against registered pubkeys
        uint8 validCount = 0;
        uint256 seenBitmap = 0;  // Bitmap to prevent duplicate EFiDs

        for (uint i = 0; i < efids.length; i++) {
            uint8 efid = efids[i];

            // Check not duplicate in this batch
            require(seenBitmap & (1 << efid) == 0, "Duplicate EFiD in batch");
            seenBitmap |= (1 << efid);

            // Check EFier is registered and active
            Elderfier storage ef = elderfiers[efid];
            if (!ef.active || ef.pubkey == bytes32(0)) {
                continue;  // Skip unregistered/inactive — don't revert, just don't count
            }

            // Check signature is 64 bytes (Ed25519: R[32] + S[32])
            if (signatures[i].length != 64) {
                continue;
            }

            // Verify Ed25519 signature: sig was computed over the merkle root hash
            bool valid = ed25519Verifier.verify(
                ef.pubkey,
                root,  // message = merkle root (32 bytes)
                signatures[i]
            );

            if (valid) {
                validCount++;
            }
        }

        // Check threshold met
        require(validCount >= signatureThreshold, "Threshold not met");

        // Finalize root
        currentRoot = FinalizedRoot({
            root: root,
            commitmentCount: commitmentCount,
            highestBlock: highestBlock,
            timestamp: uint64(block.timestamp),
            sigCount: validCount
        });
        historicalRoots[root] = currentRoot;

        emit MerkleRootFinalized(
            root, commitmentCount, highestBlock,
            uint64(block.timestamp), validCount
        );
    }

    /* ========================================================================== */
    /*                           Merkle Verification                              */
    /* ========================================================================== */

    /**
     * @dev Verify a commitment exists in the finalized merkle tree
     * @dev Called by HEAT/COLD verifier contracts during claims
     * @dev Only checks merkle proof — no signature verification (already done at root finalization)
     *
     * @param commitment The commitment hash (leaf)
     * @param proof Array of sibling hashes from leaf to root
     * @param leafIndex Index of the commitment in the tree
     * @return True if proof is valid against current finalized root
     */
    function verifyCommitment(
        bytes32 commitment,
        bytes32[] calldata proof,
        uint256 leafIndex
    ) external view returns (bool) {
        require(currentRoot.root != bytes32(0), "No root finalized");
        return _verifyProof(commitment, proof, leafIndex, currentRoot.root);
    }

    /**
     * @dev Verify against a specific historical root
     */
    function verifyCommitmentAgainstRoot(
        bytes32 commitment,
        bytes32[] calldata proof,
        uint256 leafIndex,
        bytes32 root
    ) external view returns (bool) {
        require(historicalRoots[root].root != bytes32(0), "Unknown root");
        return _verifyProof(commitment, proof, leafIndex, root);
    }

    /**
     * @dev Internal merkle proof verification
     * @dev Same algorithm as Fuego's CommitmentIndex::getMerkleProof
     */
    function _verifyProof(
        bytes32 commitment,
        bytes32[] calldata proof,
        uint256 leafIndex,
        bytes32 root
    ) internal pure returns (bool) {
        bytes32 hash = commitment;
        uint256 idx = leafIndex;

        for (uint256 i = 0; i < proof.length; i++) {
            if (idx % 2 == 0) {
                hash = keccak256(abi.encodePacked(hash, proof[i]));
            } else {
                hash = keccak256(abi.encodePacked(proof[i], hash));
            }
            idx >>= 1;
        }

        return hash == root;
    }

    /* ========================================================================== */
    /*                           Nullifier Tracking                               */
    /* ========================================================================== */

    /**
     * @dev Mark a nullifier as used — restricted to authorized verifier contracts
     */
    function markNullifierUsed(bytes32 nullifier) external {
        require(authorizedVerifiers[msg.sender], "Not authorized verifier");
        require(!usedNullifiers[nullifier], "Nullifier already used");
        usedNullifiers[nullifier] = true;
        emit NullifierUsed(nullifier, msg.sender);
    }

    function isNullifierUsed(bytes32 nullifier) external view returns (bool) {
        return usedNullifiers[nullifier];
    }

    /* ========================================================================== */
    /*                              Admin Functions                               */
    /* ========================================================================== */

    function registerElderfier(uint8 efid, bytes32 pubkey) external onlyOwner {
        require(pubkey != bytes32(0), "Invalid pubkey");
        require(!elderfiers[efid].active, "EFiD already active");

        elderfiers[efid] = Elderfier({pubkey: pubkey, active: true});
        activeEfids.push(efid);
        emit ElderfierRegistered(efid, pubkey);
    }

    function removeElderfier(uint8 efid) external onlyOwner {
        require(elderfiers[efid].active, "EFiD not active");
        require(activeEfids.length - 1 >= signatureThreshold, "Would break threshold");

        elderfiers[efid].active = false;

        // Remove from activeEfids array
        for (uint i = 0; i < activeEfids.length; i++) {
            if (activeEfids[i] == efid) {
                activeEfids[i] = activeEfids[activeEfids.length - 1];
                activeEfids.pop();
                break;
            }
        }
        emit ElderfierRemoved(efid);
    }

    function setThreshold(uint8 newThreshold) external onlyOwner {
        require(newThreshold > 0 && newThreshold <= activeEfids.length, "Invalid threshold");
        uint8 old = signatureThreshold;
        signatureThreshold = newThreshold;
        emit ThresholdUpdated(old, newThreshold);
    }

    function authorizeVerifier(address verifier) external onlyOwner {
        require(verifier != address(0), "Invalid address");
        authorizedVerifiers[verifier] = true;
        emit VerifierAuthorized(verifier);
    }

    function revokeVerifier(address verifier) external onlyOwner {
        authorizedVerifiers[verifier] = false;
        emit VerifierRevoked(verifier);
    }

    function updateEd25519Verifier(address newVerifier) external onlyOwner {
        require(newVerifier != address(0), "Invalid address");
        ed25519Verifier = IEd25519Verifier(newVerifier);
    }

    /* ========================================================================== */
    /*                              View Functions                                */
    /* ========================================================================== */

    function getCurrentRoot() external view returns (bytes32) {
        return currentRoot.root;
    }

    function getActiveEfidCount() external view returns (uint256) {
        return activeEfids.length;
    }

    function getActiveEfids() external view returns (uint8[] memory) {
        return activeEfids;
    }

    function getElderfierPubkey(uint8 efid) external view returns (bytes32) {
        return elderfiers[efid].pubkey;
    }
}

/* ========================================================================== */
/*                        Ed25519 Verifier Interface                           */
/* ========================================================================== */

/**
 * @dev Interface for Ed25519 signature verification
 * @dev Deploy a concrete implementation and pass address to MerkleVerifier constructor
 *
 * Implementations:
 * - Ed25519VerifierSolidity: Pure Solidity (works everywhere, ~500k gas per verify)
 * - Ed25519VerifierPrecompile: Uses EVM precompile when available (cheap)
 * - Ed25519VerifierStylus: Arbitrum Stylus contract (Rust, ~50k gas)
 */
interface IEd25519Verifier {
    /**
     * @dev Verify an Ed25519 signature
     * @param pubkey 32-byte Ed25519 public key
     * @param message 32-byte message hash that was signed
     * @param signature 64-byte signature (R[32] || S[32])
     * @return True if signature is valid
     */
    function verify(
        bytes32 pubkey,
        bytes32 message,
        bytes calldata signature
    ) external view returns (bool);
}
