// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";
import "./FuegoCOLDAOToken.sol";

/**
 * @title Batched Rewards Pool
 * @dev Batches CD reward claims to break timing correlation
 * @dev Users commit to claim, reveal recipient later when batch processes
 */
contract BatchedRewardsPool is Ownable, ReentrancyGuard {

    /* -------------------------------------------------------------------------- */
    /*                                   Structs                                  */
    /* -------------------------------------------------------------------------- */

    struct PendingClaim {
        address claimer;          // Who made the claim
        uint256 cdAmount;         // How much CD to receive
        bytes32 recipientHash;    // Hash of recipient address (hidden)
        uint256 timestamp;        // When claim was made
        bool processed;           // Whether claim has been paid out
    }

    struct Batch {
        uint256 startTime;        // When batch started
        uint256 claimCount;       // Number of claims in batch
        bool finalized;           // Whether batch is finalized
        mapping(uint256 => PendingClaim) claims;  // Claims in this batch
    }

    /* -------------------------------------------------------------------------- */
    /*                                   State                                    */
    /* -------------------------------------------------------------------------- */

    /// @dev CD token contract
    FuegoCOLDAOToken public immutable cdToken;

    /// @dev Minimum claims per batch before processing
    uint256 public constant MIN_BATCH_SIZE = 10;

    /// @dev Maximum time to wait for batch to fill (7 days)
    uint256 public constant MAX_BATCH_WAIT = 7 days;

    /// @dev Current batch ID
    uint256 public currentBatchId;

    /// @dev Batches by ID
    mapping(uint256 => Batch) public batches;

    /// @dev Current batch claim count
    uint256 public currentBatchClaimCount;

    /// @dev Mapping from claimer to their pending claim ID
    mapping(address => uint256) public pendingClaimIds;

    /* -------------------------------------------------------------------------- */
    /*                                   Events                                   */
    /* -------------------------------------------------------------------------- */

    event ClaimCommitted(
        address indexed claimer,
        uint256 indexed batchId,
        uint256 claimId,
        uint256 cdAmount,
        bytes32 recipientHash
    );

    event BatchFinalized(
        uint256 indexed batchId,
        uint256 claimCount
    );

    event ClaimProcessed(
        address indexed claimer,
        address indexed recipient,
        uint256 cdAmount,
        uint256 batchId
    );

    /* -------------------------------------------------------------------------- */
    /*                                Constructor                                 */
    /* -------------------------------------------------------------------------- */

    constructor(address _cdToken, address initialOwner) Ownable(initialOwner) {
        require(_cdToken != address(0), "Invalid CD token");
        cdToken = FuegoCOLDAOToken(_cdToken);
        currentBatchId = 0;
    }

    /* -------------------------------------------------------------------------- */
    /*                              Core Functions                                */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Commit to claiming rewards (Step 1)
     * @dev User provides hash of recipient address (keeps it private)
     * @param cdAmount Amount of CD tokens to claim
     * @param recipientHash Hash of recipient address (keccak256(abi.encodePacked(recipient, secret)))
     */
    function commitClaim(uint256 cdAmount, bytes32 recipientHash) external nonReentrant {
        require(cdAmount > 0, "Invalid claim amount");
        require(recipientHash != bytes32(0), "Invalid recipient hash");
        require(pendingClaimIds[msg.sender] == 0, "Already have pending claim");

        // Get current batch
        Batch storage batch = batches[currentBatchId];

        // Add claim to batch
        uint256 claimId = currentBatchClaimCount;
        batch.claims[claimId] = PendingClaim({
            claimer: msg.sender,
            cdAmount: cdAmount,
            recipientHash: recipientHash,
            timestamp: block.timestamp,
            processed: false
        });

        // Update counters
        currentBatchClaimCount++;
        batch.claimCount++;
        pendingClaimIds[msg.sender] = claimId;

        // Set batch start time if this is first claim
        if (batch.claimCount == 1) {
            batch.startTime = block.timestamp;
        }

        emit ClaimCommitted(msg.sender, currentBatchId, claimId, cdAmount, recipientHash);

        // Auto-finalize batch if threshold reached
        if (batch.claimCount >= MIN_BATCH_SIZE) {
            _finalizeBatch(currentBatchId);
        }
    }

    /**
     * @dev Reveal recipient and process claim (Step 2)
     * @dev Can only be called after batch is finalized
     * @param recipient Address to receive CD tokens
     * @param secret Secret used in recipientHash
     */
    function revealAndClaim(address recipient, bytes32 secret) external nonReentrant {
        require(recipient != address(0), "Invalid recipient");

        uint256 claimId = pendingClaimIds[msg.sender];
        require(claimId != 0 || batches[0].claims[0].claimer == msg.sender, "No pending claim");

        // Find which batch this claim is in
        uint256 batchId = _findBatchForClaim(msg.sender);
        require(batches[batchId].finalized, "Batch not finalized yet");

        Batch storage batch = batches[batchId];
        PendingClaim storage claim = batch.claims[claimId];

        require(claim.claimer == msg.sender, "Not your claim");
        require(!claim.processed, "Already processed");

        // Verify recipient hash
        bytes32 computedHash = keccak256(abi.encodePacked(recipient, secret));
        require(computedHash == claim.recipientHash, "Invalid recipient proof");

        // Mark as processed
        claim.processed = true;
        delete pendingClaimIds[msg.sender];

        // Mint CD tokens to recipient
        cdToken.mintInterestFromLP(
            recipient,
            0, // editionId (would come from LP contract normally)
            claim.cdAmount,
            0  // heatAmount (not needed for privacy pool)
        );

        emit ClaimProcessed(msg.sender, recipient, claim.cdAmount, batchId);
    }

    /**
     * @dev Finalize current batch (internal)
     * @dev Moves to next batch, allows reveals for finalized batch
     */
    function _finalizeBatch(uint256 batchId) internal {
        Batch storage batch = batches[batchId];
        require(!batch.finalized, "Already finalized");
        require(batch.claimCount > 0, "No claims in batch");

        // Finalize batch
        batch.finalized = true;

        // Start new batch
        currentBatchId++;
        currentBatchClaimCount = 0;

        emit BatchFinalized(batchId, batch.claimCount);
    }

    /**
     * @dev Force finalize batch if max wait time exceeded (public)
     * @dev Anyone can call this to unstick batches
     */
    function forceFinalizeBatch() external {
        Batch storage batch = batches[currentBatchId];
        require(!batch.finalized, "Already finalized");
        require(batch.claimCount > 0, "No claims to finalize");
        require(
            block.timestamp >= batch.startTime + MAX_BATCH_WAIT,
            "Batch wait time not exceeded"
        );

        _finalizeBatch(currentBatchId);
    }

    /**
     * @dev Find which batch contains a claim for an address
     */
    function _findBatchForClaim(address claimer) internal view returns (uint256) {
        // Simple linear search (could optimize with mapping)
        for (uint256 i = 0; i <= currentBatchId; i++) {
            Batch storage batch = batches[i];
            for (uint256 j = 0; j < batch.claimCount; j++) {
                if (batch.claims[j].claimer == claimer) {
                    return i;
                }
            }
        }
        revert("Claim not found");
    }

    /* -------------------------------------------------------------------------- */
    /*                              View Functions                                */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Get current batch info
     */
    function getCurrentBatchInfo() external view returns (
        uint256 batchId,
        uint256 claimCount,
        uint256 startTime,
        bool finalized,
        uint256 timeRemaining
    ) {
        batchId = currentBatchId;
        Batch storage batch = batches[batchId];
        claimCount = batch.claimCount;
        startTime = batch.startTime;
        finalized = batch.finalized;

        if (startTime > 0 && !finalized) {
            uint256 elapsed = block.timestamp - startTime;
            timeRemaining = elapsed < MAX_BATCH_WAIT ? MAX_BATCH_WAIT - elapsed : 0;
        }
    }

    /**
     * @dev Check if user has pending claim
     */
    function hasPendingClaim(address user) external view returns (bool) {
        return pendingClaimIds[user] != 0 || batches[0].claims[0].claimer == user;
    }

    /**
     * @dev Get pending claim details
     */
    function getPendingClaim(address user) external view returns (
        uint256 cdAmount,
        bytes32 recipientHash,
        uint256 timestamp,
        bool canReveal
    ) {
        uint256 claimId = pendingClaimIds[user];
        uint256 batchId = _findBatchForClaim(user);

        PendingClaim storage claim = batches[batchId].claims[claimId];
        cdAmount = claim.cdAmount;
        recipientHash = claim.recipientHash;
        timestamp = claim.timestamp;
        canReveal = batches[batchId].finalized;
    }
}

/** winter is coming */
