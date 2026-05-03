// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/Pausable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";
import "./FuegoCOLDAOToken.sol";
import "./interfaces/IArbSys.sol";
import "./TierConversions.sol";
import "./FuegoCommitmentMerkleVerifier.sol";

/**
 * @title COLD Deposit Proof Verifier (v3 — EFier consensus)
 * @dev Verifies XFG deposit commitments via merkle proof against EFier-finalized root
 * @dev No trusted API — user submits proof directly, contract verifies against
 *      merkle root that was finalized by Elderfier Ed25519 signature consensus
 *
 * Flow:
 *   1. User deposits XFG on Fuego L1 (0xCD tag, 3mo or 12mo term)
 *   2. EFiers sign commitment merkle root → someone calls submitRoot() on MerkleVerifier
 *   3. User calls claimCD() with merkle proof → contract verifies against finalized root
 *   4. L2→L1 message mints CD interest tokens on Ethereum via ARB_SYS
 *
 * 8 deposit tiers (4 amounts x 2 terms): tier = (amountIndex * 2) + termIndex
 *   Tier 0: 0.8 XFG x 3mo  @ 8%  → 640,000 CD
 *   Tier 1: 0.8 XFG x 12mo @ 27% → 2,160,000 CD
 *   Tier 2: 8 XFG x 3mo    @ 18% → 14,400,000 CD
 *   Tier 3: 8 XFG x 12mo   @ 33% → 26,400,000 CD
 *   Tier 4: 80 XFG x 3mo   @ 27% → 216,000,000 CD
 *   Tier 5: 80 XFG x 12mo  @ 42% → 336,000,000 CD
 *   Tier 6: 800 XFG x 3mo  @ 33% → 2,640,000,000 CD
 *   Tier 7: 800 XFG x 12mo @ 69% → 5,520,000,000 CD
 *   Legacy (tier 6-7 before 2026-01-01): 80% → 6,400,000,000 CD
 */
contract COLDProofVerifier is Ownable, Pausable, ReentrancyGuard {

    /* ========================================================================== */
    /*                                   Events                                   */
    /* ========================================================================== */

    event CDClaimed(
        bytes32 indexed commitment,
        address indexed recipient,
        uint256 xfgPrincipal,
        uint256 cdInterest,
        uint8 tier,
        bytes32 indexed nullifier
    );

    event L1MessageSent(
        address indexed recipient,
        uint256 cdInterest,
        uint256 ticketId,
        bytes32 indexed commitment
    );

    /* ========================================================================== */
    /*                                   State                                    */
    /* ========================================================================== */

    /// @dev CD token contract on L1 (ERC-1155)
    FuegoCOLDAOToken public immutable cdToken;

    /// @dev Shared merkle verifier (EFier-finalized roots + nullifier tracking)
    FuegoCommitmentMerkleVerifier public immutable merkleVerifier;

    /// @dev Arbitrum L2→L1 messenger precompile
    IArbSys public constant ARB_SYS = IArbSys(address(0x64));

    /// @dev Fuego network IDs
    uint256 public constant FUEGO_MAINNET_ID = 93385046440755750514194170694064996624;
    uint256 public constant FUEGO_TESTNET_ID = 112015110234323138517908755257434054688;

    /// @dev Statistics
    uint256 public totalClaims;
    uint256 public totalCDMinted;
    uint256 public totalXFGLocked;

    /* ========================================================================== */
    /*                                 Constructor                                */
    /* ========================================================================== */

    constructor(
        address _cdToken,
        address _merkleVerifier,
        address _owner
    ) Ownable(_owner) {
        require(_cdToken != address(0), "Invalid CD token");
        require(_merkleVerifier != address(0), "Invalid merkle verifier");

        cdToken = FuegoCOLDAOToken(_cdToken);
        merkleVerifier = FuegoCommitmentMerkleVerifier(_merkleVerifier);
    }

    /* ========================================================================== */
    /*                              Claim Function                                */
    /* ========================================================================== */

    /**
     * @dev Claim CD interest tokens by providing merkle proof of deposit commitment
     * @dev Anyone can call — no API verifier needed
     * @dev Merkle proof is verified against EFier-finalized root (cheap, no sig check)
     *
     * @param recipient ETH address to receive CD tokens on L1
     * @param depositTier COLD tier (0-7): tier = (amountIndex * 2) + termIndex
     * @param nullifier Nullifier derived from commitment secret (prevents double-claim)
     * @param commitment Commitment hash from STARK proof
     * @param isLegacy True only for pre-v3 deposits migrated via 0xCE tag (MultisignatureOutput confirmed on L1)
     * @param merkleProof Sibling hashes from leaf to root
     * @param leafIndex Index of commitment in the merkle tree
     */
    function claimCD(
        address recipient,
        uint8 depositTier,
        bytes32 nullifier,
        bytes32 commitment,
        bool isLegacy,
        bytes32[] calldata merkleProof,
        uint256 leafIndex
    ) external payable whenNotPaused nonReentrant {
        require(recipient != address(0), "Invalid recipient");
        require(TierConversions.isValidColdTier(depositTier), "Invalid COLD tier: must be 0-7");

        // Check nullifier not already used (shared across HEAT+COLD)
        require(!merkleVerifier.isNullifierUsed(nullifier), "Already claimed");

        // Verify commitment exists in EFier-finalized merkle tree
        require(
            merkleVerifier.verifyCommitment(commitment, merkleProof, leafIndex),
            "Invalid merkle proof"
        );

        // Mark nullifier used (prevents replay)
        merkleVerifier.markNullifierUsed(nullifier);

        // Get XFG principal and CD interest for tier (with legacy rate if applicable)
        // isLegacy is set on L1 by Blockchain.cpp — only valid for confirmed MultisignatureOutput migrations
        uint256 xfgPrincipal = TierConversions.getColdXFGForTier(depositTier);
        uint256 cdInterest = TierConversions.getColdCDInterestWithLegacyBool(
            depositTier, isLegacy
        );
        require(cdInterest > 0, "Zero interest");

        // Get current edition from CD token
        uint256 editionId = cdToken.currentEditionId() - 1;

        // Send L2→L1 message to mint CD on Ethereum
        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint256,uint256,uint32)",
            commitment,
            recipient,
            editionId,
            cdInterest,
            xfgPrincipal,
            3  // commitment version = 3
        );

        uint256 ticketId = ARB_SYS.sendTxToL1{value: msg.value}(
            address(cdToken), data
        );

        emit CDClaimed(commitment, recipient, xfgPrincipal, cdInterest, depositTier, nullifier);
        emit L1MessageSent(recipient, cdInterest, ticketId, commitment);

        totalClaims++;
        totalCDMinted += cdInterest;
        totalXFGLocked += xfgPrincipal;
    }

    /* ========================================================================== */
    /*                              View Functions                                */
    /* ========================================================================== */

    /**
     * @dev Estimate L1 gas fee for cross-chain CD mint
     */
    function estimateL1GasFee(address recipient, uint8 depositTier)
        external view returns (uint256)
    {
        require(TierConversions.isValidColdTier(depositTier), "Invalid tier");
        uint256 xfgPrincipal = TierConversions.getColdXFGForTier(depositTier);
        uint256 cdInterest = TierConversions.getColdCDInterest(depositTier);
        uint256 editionId = cdToken.currentEditionId() - 1;

        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint256,uint256,uint32)",
            bytes32(0), recipient, editionId, cdInterest, xfgPrincipal, 3
        );

        return (21000 + data.length * 16) * 20 gwei;
    }

    function getTierInfo(uint8 tier) external pure returns (
        uint256 xfgAmount,
        uint256 cdInterest,
        uint8 lockMonths,
        string memory tierName
    ) {
        require(TierConversions.isValidColdTier(tier), "Invalid tier");
        return (
            TierConversions.getColdXFGForTier(tier),
            TierConversions.getColdCDInterest(tier),
            TierConversions.getColdLockMonths(tier),
            TierConversions.getColdTierName(tier)
        );
    }

    function getStatistics() external view returns (
        uint256 claims, uint256 cdMinted, uint256 xfgLocked
    ) {
        return (totalClaims, totalCDMinted, totalXFGLocked);
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
