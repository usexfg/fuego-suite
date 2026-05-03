// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/Pausable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";
import "./HEATToken.sol";
import "./interfaces/IArbSys.sol";
import "./TierConversions.sol";
import "./FuegoCommitmentMerkleVerifier.sol";

/**
 * @title HEAT Burn Proof Verifier (v3 — EFier consensus)
 * @dev Verifies XFG burn commitments via merkle proof against EFier-finalized root
 * @dev No trusted API — user submits proof directly, contract verifies against
 *      merkle root that was finalized by Elderfier Ed25519 signature consensus
 *
 * Flow:
 *   1. User burns XFG on Fuego L1 (0x08 tag, FOREVER term)
 *   2. EFiers sign commitment merkle root → someone calls submitRoot() on MerkleVerifier
 *   3. User calls claimHEAT() with merkle proof → contract verifies against finalized root
 *   4. L2→L1 message mints HEAT on Ethereum via ARB_SYS
 *
 * 4 burn tiers: 0.8 XFG → 8M HEAT, 8 → 80M, 80 → 800M, 800 → 8B
 */
contract HEATBurnProofVerifier is Ownable, Pausable, ReentrancyGuard {

    /* ========================================================================== */
    /*                                   Events                                   */
    /* ========================================================================== */

    event HEATClaimed(
        bytes32 indexed commitment,
        address indexed recipient,
        uint256 heatAmount,
        uint8 tier,
        bytes32 indexed nullifier
    );

    event L1MessageSent(
        address indexed recipient,
        uint256 heatAmount,
        uint256 ticketId,
        bytes32 indexed commitment
    );

    /* ========================================================================== */
    /*                                   State                                    */
    /* ========================================================================== */

    /// @dev HEAT token contract on L1
    EmbersTokenHEAT public immutable heatToken;

    /// @dev Shared merkle verifier (EFier-finalized roots + nullifier tracking)
    FuegoCommitmentMerkleVerifier public immutable merkleVerifier;

    /// @dev Arbitrum L2→L1 messenger precompile
    IArbSys public constant ARB_SYS = IArbSys(address(0x64));

    /// @dev Fuego network ID
    uint256 public constant FUEGO_NETWORK_ID = 93385046440755750514194170694064996624;

    /// @dev Statistics
    uint256 public totalClaims;
    uint256 public totalHEATMinted;

    /* ========================================================================== */
    /*                                 Constructor                                */
    /* ========================================================================== */

    constructor(
        address _heatToken,
        address _merkleVerifier,
        address _owner
    ) Ownable(_owner) {
        require(_heatToken != address(0), "Invalid HEAT token");
        require(_merkleVerifier != address(0), "Invalid merkle verifier");

        heatToken = EmbersTokenHEAT(_heatToken);
        merkleVerifier = FuegoCommitmentMerkleVerifier(_merkleVerifier);
    }

    /* ========================================================================== */
    /*                              Claim Function                                */
    /* ========================================================================== */

    /**
     * @dev Claim HEAT tokens by providing merkle proof of burn commitment
     * @dev Anyone can call — no API verifier needed
     * @dev Merkle proof is verified against EFier-finalized root (cheap, no sig check)
     *
     * @param recipient ETH address to receive HEAT tokens on L1
     * @param burnTier HEAT tier (0-3): 0=0.8 XFG, 1=8 XFG, 2=80 XFG, 3=800 XFG
     * @param nullifier Nullifier derived from commitment secret (prevents double-claim)
     * @param commitment Commitment hash from STARK proof
     * @param merkleProof Sibling hashes from leaf to root
     * @param leafIndex Index of commitment in the merkle tree
     */
    function claimHEAT(
        address recipient,
        uint8 burnTier,
        bytes32 nullifier,
        bytes32 commitment,
        bytes32[] calldata merkleProof,
        uint256 leafIndex
    ) external payable whenNotPaused nonReentrant {
        require(recipient != address(0), "Invalid recipient");
        require(TierConversions.isValidTier(burnTier), "Invalid HEAT tier: must be 0-3");

        // Check nullifier not already used (shared across HEAT+COLD)
        require(!merkleVerifier.isNullifierUsed(nullifier), "Already claimed");

        // Verify commitment exists in EFier-finalized merkle tree
        require(
            merkleVerifier.verifyCommitment(commitment, merkleProof, leafIndex),
            "Invalid merkle proof"
        );

        // Mark nullifier used (prevents replay)
        merkleVerifier.markNullifierUsed(nullifier);

        // Get HEAT amount for tier
        uint256 heatAmount = TierConversions.getHEATForTier(burnTier);

        // Send L2→L1 message to mint HEAT on Ethereum
        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint32)",
            commitment,
            recipient,
            heatAmount,
            3  // commitment version = 3
        );

        uint256 ticketId = ARB_SYS.sendTxToL1{value: msg.value}(
            address(heatToken), data
        );

        emit HEATClaimed(commitment, recipient, heatAmount, burnTier, nullifier);
        emit L1MessageSent(recipient, heatAmount, ticketId, commitment);

        totalClaims++;
        totalHEATMinted += heatAmount;
    }

    /* ========================================================================== */
    /*                              View Functions                                */
    /* ========================================================================== */

    /**
     * @dev Estimate L1 gas fee for cross-chain HEAT mint
     */
    function estimateL1GasFee(address recipient, uint8 burnTier)
        external view returns (uint256)
    {
        require(TierConversions.isValidTier(burnTier), "Invalid tier");
        uint256 heatAmount = TierConversions.getHEATForTier(burnTier);

        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint32)",
            bytes32(0), recipient, heatAmount, 3
        );

        // Conservative L1 gas estimate
        return (21000 + data.length * 16) * 20 gwei;
    }

    function getTierInfo(uint8 tier) external pure returns (
        uint256 xfgAmount,
        uint256 heatAmount,
        string memory tierName
    ) {
        require(TierConversions.isValidTier(tier), "Invalid tier");
        return (
            TierConversions.getXFGForTier(tier),
            TierConversions.getHEATForTier(tier),
            TierConversions.getTierName(tier)
        );
    }

    function getStatistics() external view returns (
        uint256 claims, uint256 heatMinted
    ) {
        return (totalClaims, totalHEATMinted);
    }

    /* ========================================================================== */
    /*                              Admin Functions                               */
    /* ========================================================================== */

    function pause() external onlyOwner { _pause(); }
    function unpause() external onlyOwner { _unpause(); }

    function rescueETH() external onlyOwner {
        payable(owner()).transfer(address(this).balance);
    }

    receive() external payable {}

} /** winter is coming */
