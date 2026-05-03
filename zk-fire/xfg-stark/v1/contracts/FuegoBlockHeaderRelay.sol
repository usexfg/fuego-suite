// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/access/Ownable.sol";

/**
 * @title FuegoBlockHeaderRelay
 * @dev Relays Fuego block headers to EVM for SPV-style verification
 * @dev Elderfiers submit block headers; contract validates chain continuity
 *
 * Fuego Block Header Structure (serialized):
 * - majorVersion: 1 byte
 * - minorVersion: 1 byte
 * - timestamp: 8 bytes (uint64 LE)
 * - previousBlockHash: 32 bytes
 * - nonce: 4 bytes (uint32 LE)
 * - transactionTreeHash: 32 bytes (Merkle root of transactions)
 *
 * Total: ~78 bytes serialized
 *
 * Note: Fuego uses CN-UPX2 PoW which cannot be efficiently verified on-chain.
 * Instead, we rely on Eldernode consensus (threshold signatures) to attest
 * to block validity. This is a pragmatic tradeoff for cross-chain bridges.
 */
contract FuegoBlockHeaderRelay is Ownable {

    /* -------------------------------------------------------------------------- */
    /*                                   Events                                   */
    /* -------------------------------------------------------------------------- */

    event BlockHeaderSubmitted(
        bytes32 indexed blockHash,
        uint64 blockHeight,
        bytes32 previousHash,
        bytes32 transactionRoot,
        uint64 timestamp
    );

    event ChainReorg(
        uint64 forkHeight,
        bytes32 oldTip,
        bytes32 newTip
    );

    event EldernodeAdded(address indexed eldernode);
    event EldernodeRemoved(address indexed eldernode);
    event CheckpointSet(uint64 height, bytes32 blockHash);

    /* -------------------------------------------------------------------------- */
    /*                                   Structs                                  */
    /* -------------------------------------------------------------------------- */

    /// @dev Fuego block header data
    struct BlockHeader {
        uint8 majorVersion;
        uint8 minorVersion;
        uint64 timestamp;
        bytes32 previousBlockHash;
        uint32 nonce;
        bytes32 transactionTreeHash;  // Merkle root of transactions
        bytes32 blockHash;            // Computed hash of header
        uint64 height;
        uint64 submissionTime;        // When submitted to EVM
        uint64 confirmations;         // Number of eldernode confirmations
    }

    /// @dev Pending block header awaiting confirmations
    struct PendingHeader {
        bytes headerData;
        bytes32 computedHash;
        uint64 height;
        uint64 submissionTime;
        mapping(address => bool) confirmations;
        uint64 confirmationCount;
    }

    /* -------------------------------------------------------------------------- */
    /*                                   State                                    */
    /* -------------------------------------------------------------------------- */

    /// @dev Block headers by hash
    mapping(bytes32 => BlockHeader) public headers;

    /// @dev Block hash by height (main chain)
    mapping(uint64 => bytes32) public mainChain;

    /// @dev Current chain tip
    bytes32 public chainTip;
    uint64 public chainHeight;

    /// @dev Pending headers awaiting confirmations
    mapping(bytes32 => PendingHeader) internal pendingHeaders;

    /// @dev Registered Eldernodes
    mapping(address => bool) public isEldernode;
    address[] public eldernodes;

    /// @dev Confirmation threshold
    uint64 public confirmationThreshold;

    /// @dev Checkpoints (height => required hash)
    mapping(uint64 => bytes32) public checkpoints;
    uint64 public latestCheckpoint;

    /// @dev Genesis hash for chain verification
    bytes32 public genesisHash;

    /// @dev Maximum reorg depth allowed
    uint64 public constant MAX_REORG_DEPTH = 100;

    /// @dev Pending header expiry
    uint64 public constant PENDING_EXPIRY = 2 hours;

    /* -------------------------------------------------------------------------- */
    /*                                 Constructor                                */
    /* -------------------------------------------------------------------------- */

    constructor(
        address[] memory _eldernodes,
        uint64 _threshold,
        bytes32 _genesisHash
    ) Ownable(msg.sender) {
        require(_eldernodes.length >= _threshold, "Not enough eldernodes");
        require(_threshold > 0, "Threshold must be > 0");
        require(_genesisHash != bytes32(0), "Invalid genesis");

        for (uint i = 0; i < _eldernodes.length; i++) {
            require(_eldernodes[i] != address(0), "Invalid eldernode");
            isEldernode[_eldernodes[i]] = true;
            eldernodes.push(_eldernodes[i]);
            emit EldernodeAdded(_eldernodes[i]);
        }

        confirmationThreshold = _threshold;
        genesisHash = _genesisHash;

        // Initialize genesis block
        headers[_genesisHash] = BlockHeader({
            majorVersion: 1,
            minorVersion: 0,
            timestamp: 0,
            previousBlockHash: bytes32(0),
            nonce: 0,
            transactionTreeHash: bytes32(0),
            blockHash: _genesisHash,
            height: 0,
            submissionTime: uint64(block.timestamp),
            confirmations: _threshold
        });

        mainChain[0] = _genesisHash;
        chainTip = _genesisHash;
        chainHeight = 0;
    }

    /* -------------------------------------------------------------------------- */
    /*                             Header Submission                              */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Submit a Fuego block header
     * @param headerData Raw serialized header (78 bytes)
     * @param height Block height
     */
    function submitHeader(bytes calldata headerData, uint64 height) external {
        require(isEldernode[msg.sender], "Not an eldernode");
        require(headerData.length >= 78, "Invalid header length");

        // Parse and compute hash
        (BlockHeader memory header, bytes32 blockHash) = _parseHeader(headerData, height);

        // Check if already finalized
        if (headers[blockHash].blockHash != bytes32(0)) {
            return; // Already submitted and finalized
        }

        // Get or create pending header
        PendingHeader storage pending = pendingHeaders[blockHash];

        if (pending.computedHash == bytes32(0)) {
            // New submission
            pending.headerData = headerData;
            pending.computedHash = blockHash;
            pending.height = height;
            pending.submissionTime = uint64(block.timestamp);
            pending.confirmationCount = 0;
        } else {
            // Check expiry
            require(
                block.timestamp <= pending.submissionTime + PENDING_EXPIRY,
                "Pending header expired"
            );
        }

        // Add confirmation
        require(!pending.confirmations[msg.sender], "Already confirmed");
        pending.confirmations[msg.sender] = true;
        pending.confirmationCount++;

        // Check threshold
        if (pending.confirmationCount >= confirmationThreshold) {
            _finalizeHeader(header, blockHash, height);
        }
    }

    /**
     * @dev Parse raw header data into BlockHeader struct
     */
    function _parseHeader(bytes calldata data, uint64 height) internal pure returns (BlockHeader memory header, bytes32 blockHash) {
        // Parse header fields (little-endian where applicable)
        header.majorVersion = uint8(data[0]);
        header.minorVersion = uint8(data[1]);

        // Timestamp (8 bytes LE)
        header.timestamp = uint64(bytes8(data[2:10]));

        // Previous block hash (32 bytes)
        header.previousBlockHash = bytes32(data[10:42]);

        // Nonce (4 bytes LE)
        header.nonce = uint32(bytes4(data[42:46]));

        // Transaction tree hash (32 bytes)
        header.transactionTreeHash = bytes32(data[46:78]);

        header.height = height;

        // Compute block hash (keccak256 of header)
        // Note: Fuego actually uses different hash, but for bridge purposes
        // we use keccak256 as the canonical identifier on EVM
        blockHash = keccak256(data);
        header.blockHash = blockHash;

        return (header, blockHash);
    }

    /**
     * @dev Finalize a confirmed header
     */
    function _finalizeHeader(BlockHeader memory header, bytes32 blockHash, uint64 height) internal {
        // Validate chain continuity
        if (height > 0) {
            require(
                headers[header.previousBlockHash].blockHash != bytes32(0),
                "Previous block not found"
            );
            require(
                headers[header.previousBlockHash].height == height - 1,
                "Height mismatch"
            );
        }

        // Check checkpoint
        if (checkpoints[height] != bytes32(0)) {
            require(checkpoints[height] == blockHash, "Checkpoint mismatch");
        }

        // Store header
        header.submissionTime = uint64(block.timestamp);
        header.confirmations = pendingHeaders[blockHash].confirmationCount;
        headers[blockHash] = header;

        // Update main chain
        _updateMainChain(blockHash, height);

        emit BlockHeaderSubmitted(
            blockHash,
            height,
            header.previousBlockHash,
            header.transactionTreeHash,
            header.timestamp
        );

        // Clean up pending
        delete pendingHeaders[blockHash];
    }

    /**
     * @dev Update main chain, handling reorgs
     */
    function _updateMainChain(bytes32 blockHash, uint64 height) internal {
        if (height > chainHeight) {
            // Extension of main chain
            mainChain[height] = blockHash;
            chainTip = blockHash;
            chainHeight = height;
        } else if (mainChain[height] != blockHash) {
            // Potential reorg - check if this chain is longer
            // For simplicity, we just extend; proper reorg handling would
            // trace back to find common ancestor
            require(height > chainHeight - MAX_REORG_DEPTH, "Reorg too deep");

            bytes32 oldTip = chainTip;
            mainChain[height] = blockHash;

            // Walk forward to update
            uint64 h = height;
            bytes32 current = blockHash;
            while (h <= chainHeight) {
                if (mainChain[h] == current) break;
                mainChain[h] = current;
                h++;

                // Find next block (would need index; simplified for now)
                break;
            }

            emit ChainReorg(height, oldTip, blockHash);
        }
    }

    /* -------------------------------------------------------------------------- */
    /*                           Transaction Verification                         */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Verify a transaction exists in a block using Merkle proof
     * @param txHash Transaction hash
     * @param blockHash Block containing the transaction
     * @param proof Merkle proof (sibling hashes)
     * @param txIndex Index of transaction in block
     * @return isValid True if transaction is verified
     */
    function verifyTransaction(
        bytes32 txHash,
        bytes32 blockHash,
        bytes32[] calldata proof,
        uint256 txIndex
    ) external view returns (bool isValid) {
        BlockHeader storage header = headers[blockHash];
        require(header.blockHash != bytes32(0), "Block not found");

        // Verify Merkle proof against transaction tree root
        return _verifyMerkleProof(txHash, proof, txIndex, header.transactionTreeHash);
    }

    /**
     * @dev Internal Merkle proof verification
     */
    function _verifyMerkleProof(
        bytes32 leaf,
        bytes32[] calldata proof,
        uint256 index,
        bytes32 root
    ) internal pure returns (bool) {
        bytes32 computedHash = leaf;

        for (uint256 i = 0; i < proof.length; i++) {
            if (index % 2 == 0) {
                computedHash = keccak256(abi.encodePacked(computedHash, proof[i]));
            } else {
                computedHash = keccak256(abi.encodePacked(proof[i], computedHash));
            }
            index = index / 2;
        }

        return computedHash == root;
    }

    /**
     * @dev Check if a block is confirmed (on main chain with sufficient depth)
     * @param blockHash Block hash to check
     * @param requiredConfirmations Minimum confirmations required
     */
    function isBlockConfirmed(bytes32 blockHash, uint64 requiredConfirmations) external view returns (bool) {
        BlockHeader storage header = headers[blockHash];
        if (header.blockHash == bytes32(0)) return false;

        // Check if on main chain
        if (mainChain[header.height] != blockHash) return false;

        // Check confirmations (blocks built on top)
        uint64 confirmations = chainHeight - header.height;
        return confirmations >= requiredConfirmations;
    }

    /* -------------------------------------------------------------------------- */
    /*                              Admin Functions                               */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Set a checkpoint
     */
    function setCheckpoint(uint64 height, bytes32 blockHash) external onlyOwner {
        require(height > latestCheckpoint, "Cannot set past checkpoint");
        checkpoints[height] = blockHash;
        latestCheckpoint = height;
        emit CheckpointSet(height, blockHash);
    }

    /**
     * @dev Add an eldernode
     */
    function addEldernode(address eldernode) external onlyOwner {
        require(eldernode != address(0), "Invalid address");
        require(!isEldernode[eldernode], "Already eldernode");

        isEldernode[eldernode] = true;
        eldernodes.push(eldernode);
        emit EldernodeAdded(eldernode);
    }

    /**
     * @dev Remove an eldernode
     */
    function removeEldernode(address eldernode) external onlyOwner {
        require(isEldernode[eldernode], "Not an eldernode");
        require(eldernodes.length > confirmationThreshold, "Would break threshold");

        isEldernode[eldernode] = false;

        for (uint i = 0; i < eldernodes.length; i++) {
            if (eldernodes[i] == eldernode) {
                eldernodes[i] = eldernodes[eldernodes.length - 1];
                eldernodes.pop();
                break;
            }
        }

        emit EldernodeRemoved(eldernode);
    }

    /**
     * @dev Update confirmation threshold
     */
    function setThreshold(uint64 newThreshold) external onlyOwner {
        require(newThreshold > 0, "Threshold must be > 0");
        require(newThreshold <= eldernodes.length, "Exceeds eldernode count");
        confirmationThreshold = newThreshold;
    }

    /* -------------------------------------------------------------------------- */
    /*                              View Functions                                */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Get block header by hash
     */
    function getHeader(bytes32 blockHash) external view returns (
        uint8 majorVersion,
        uint8 minorVersion,
        uint64 timestamp,
        bytes32 previousBlockHash,
        bytes32 transactionTreeHash,
        uint64 height,
        uint64 confirmations
    ) {
        BlockHeader storage h = headers[blockHash];
        return (
            h.majorVersion,
            h.minorVersion,
            h.timestamp,
            h.previousBlockHash,
            h.transactionTreeHash,
            h.height,
            h.confirmations
        );
    }

    /**
     * @dev Get main chain block at height
     */
    function getBlockAtHeight(uint64 height) external view returns (bytes32) {
        return mainChain[height];
    }

    /**
     * @dev Get chain tip info
     */
    function getChainTip() external view returns (bytes32 tip, uint64 height) {
        return (chainTip, chainHeight);
    }

    /**
     * @dev Get eldernode count
     */
    function getEldernodeCount() external view returns (uint256) {
        return eldernodes.length;
    }

    /**
     * @dev Get pending header info
     */
    function getPendingHeader(bytes32 blockHash) external view returns (
        uint64 height,
        uint64 confirmationCount,
        uint64 submissionTime
    ) {
        PendingHeader storage p = pendingHeaders[blockHash];
        return (p.height, p.confirmationCount, p.submissionTime);
    }
}
