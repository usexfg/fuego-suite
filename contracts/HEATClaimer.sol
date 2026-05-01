// SPDX-License-Identifier: MIT
pragma solidity ^0.8.24;

interface IStarkVerifier {
    function verify(
        bytes calldata starkProof,
        bytes32 commitment,
        bytes32 nullifier,
        uint256 amount,
        bytes32 txnHash
    ) external view returns (bool);
}

interface IFuegoCommitmentMerkleVerifier {
    function verifyCommitment(bytes32 commitment, bytes32[] calldata proof, uint256 leafIndex) external view returns (bool);
    function isNullifierUsed(bytes32 nullifier) external view returns (bool);
    function markNullifierUsed(bytes32 nullifier) external;
}

interface IHEATToken {
    function mint(address to, uint256 amount) external;
}

contract HEATClaimer {
    IStarkVerifier public immutable starkVerifier;
    IFuegoCommitmentMerkleVerifier public immutable merkleVerifier;
    IHEATToken public immutable heatToken;

    uint256 public constant HEAT_DECIMALS = 18;
    uint256 public constant XFG_DECIMALS  = 7;
    uint256 public constant HEAT_PER_XFG_ATOM = 10 ** (HEAT_DECIMALS - XFG_DECIMALS);

    mapping(bytes32 => bool) public usedNullifiers;

    event HEATClaimed(
        bytes32 indexed commitment,
        bytes32 indexed nullifier,
        address indexed recipient,
        uint256 heatAmount
    );

    error AlreadyClaimed();
    error InvalidMerkleProof();
    error InvalidStarkProof();

    constructor(address _starkVerifier, address _merkleVerifier, address _heatToken) {
        starkVerifier = IStarkVerifier(_starkVerifier);
        merkleVerifier = IFuegoCommitmentMerkleVerifier(_merkleVerifier);
        heatToken = IHEATToken(_heatToken);
    }

    function claimHEATWithStark(
        bytes calldata starkProof,
        bytes32 commitment,
        bytes32 nullifier,
        uint256 amount,
        bytes32 txnHash,
        bytes32[] calldata merkleProof,
        uint256 leafIndex,
        address recipient
    ) external {
        if (merkleVerifier.isNullifierUsed(nullifier)) revert AlreadyClaimed();
        if (usedNullifiers[nullifier]) revert AlreadyClaimed();

        if (!starkVerifier.verify(starkProof, commitment, nullifier, amount, txnHash)) {
            revert InvalidStarkProof();
        }

        if (!merkleVerifier.verifyCommitment(commitment, merkleProof, leafIndex)) {
            revert InvalidMerkleProof();
        }

        merkleVerifier.markNullifierUsed(nullifier);
        usedNullifiers[nullifier] = true;

        uint256 heatAmount = amount * HEAT_PER_XFG_ATOM;
        heatToken.mint(recipient, heatAmount);

        emit HEATClaimed(commitment, nullifier, recipient, heatAmount);
    }

    function _verifyMerkleProof(
        bytes32 leaf,
        bytes32[] calldata proof,
        uint256 index,
        bytes32 root
    ) internal pure returns (bool) {
        bytes32 current = leaf;
        for (uint256 i = 0; i < proof.length; i++) {
            if (index % 2 == 0) {
                current = keccak256(abi.encodePacked(current, proof[i]));
            } else {
                current = keccak256(abi.encodePacked(proof[i], current));
            }
            index /= 2;
        }
        return current == root;
    }
}
