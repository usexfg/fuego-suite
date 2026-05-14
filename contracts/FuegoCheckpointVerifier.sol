// SPDX-License-Identifier: MIT
pragma solidity ^0.8.24;

interface ISP1Verifier {
    function verifyProof(
        bytes32 programVKey,
        bytes calldata publicValues,
        bytes calldata proofBytes
    ) external view;
}

contract FuegoCheckpointVerifier {
    struct Checkpoint {
        bytes32 checkpointHash;
        bytes32 merkleRoot;
        uint32  heightEnd;
    }

    Checkpoint public latest;
    ISP1Verifier public immutable verifier;
    bytes32 public immutable programVKey;
    address public owner;

    event CheckpointUpdated(bytes32 indexed merkleRoot, uint32 heightEnd, bytes32 checkpointHash);

    error CheckpointHashMismatch();
    error HeightNotContinuous();
    error NotOwner();

    modifier onlyOwner() {
        if (msg.sender != owner) revert NotOwner();
        _;
    }

    constructor(address _verifier, bytes32 _programVKey, bytes32 genesisCheckpointHash) {
        verifier = ISP1Verifier(_verifier);
        programVKey = _programVKey;
        owner = msg.sender;
        latest = Checkpoint({
            checkpointHash: genesisCheckpointHash,
            merkleRoot: bytes32(0),
            heightEnd: 0
        });
    }

    /// Submit a ZK proof advancing the checkpoint from latest to a new state.
    /// Public values ABI-encoded: (prevCheckpointHash, newCheckpointHash, newMerkleRoot, heightStart, heightEnd, difficultyTarget)
    function updateCheckpoint(
        bytes calldata proof,
        bytes32 prevCheckpointHash,
        bytes32 newCheckpointHash,
        bytes32 newMerkleRoot,
        uint32  heightStart,
        uint32  heightEnd,
        uint32  difficultyTarget
    ) external {
        if (prevCheckpointHash != latest.checkpointHash) revert CheckpointHashMismatch();
        if (latest.heightEnd != 0 && heightStart != latest.heightEnd + 1) revert HeightNotContinuous();

        bytes memory publicValues = abi.encode(
            prevCheckpointHash,
            newCheckpointHash,
            newMerkleRoot,
            heightStart,
            heightEnd,
            difficultyTarget
        );
        verifier.verifyProof(programVKey, publicValues, proof);

        latest = Checkpoint({
            checkpointHash: newCheckpointHash,
            merkleRoot: newMerkleRoot,
            heightEnd: heightEnd
        });

        emit CheckpointUpdated(newMerkleRoot, heightEnd, newCheckpointHash);
    }

    function transferOwnership(address newOwner) external onlyOwner {
        owner = newOwner;
    }
}
