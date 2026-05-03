// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/Pausable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";
import "./FuegoCOLDAOToken.sol";
import "./interfaces/IArbSys.sol";

/**
 * @title COLD Deposit Proof Verifier
 * @dev Verifies XFG deposit proofs and mints CD tokens on Arbitrum
 * @dev XFG principal is LOCKED (not burned) on Fuego - unlocks handled off-chain
 * @dev Only CD INTEREST is minted to depositor
 * @dev Initial 8 tiers (4 amounts × 2 terms):
 *      Amount Tiers: 0.8 XFG, 8 XFG, 80 XFG, 800 XFG
 *      Time Tiers: 3 months, 12 months
 *      Tier encoding: (amountIndex * 2) + termIndex
 * @dev Initial Tier Matrix:
 *      Tier 0: 0.8 XFG × 3mo   @ 8% APY
 *      Tier 1: 0.8 XFG × 12mo  @ 21% APY
 *      Tier 2: 8 XFG × 3mo     @ 21% APY
 *      Tier 3: 8 XFG × 12mo    @ 33% APY
 *      Tier 4: 80 XFG × 3mo    @ 33% APY
 *      Tier 5: 80 XFG × 12mo   @ 55% APY
 *      Tier 6: 800 XFG × 3mo   @ 55% APY
 *      Tier 7: 800 XFG × 12mo  @ 69% APY
 * @dev Legacy deposits (before 2026-01-01): 800 XFG @ 80% APY (tier 6 & 7 only)
 * @dev DAO-upgradeable: New tiers can be added via governance
 * @dev Privacy-focused: Recommend new addresses per claim
 * @dev API verification via usexfg.org (STARK proof validated off-chain)
 */
contract COLDDepositProofVerifier is Ownable, Pausable, ReentrancyGuard {

    /* -------------------------------------------------------------------------- */
    /*                                   Events                                   */
    /* -------------------------------------------------------------------------- */

    event ProofVerified(
        bytes32 indexed depositTxHash,
        address indexed recipient,
        uint256 cdAmount,
        uint8 tier,
        bytes32 indexed nullifier
    );

    event L1GasPaid(
        address indexed user,
        uint256 gasAmount,
        uint256 ticketId,
        bytes32 indexed commitment
    );

    event APIVerifierUpdated(
        address indexed oldVerifier,
        address indexed newVerifier
    );

    event TierAdded(
        uint256 indexed tierIndex,
        uint256 cdAmount
    );

    event TierUpdated(
        uint256 indexed tierIndex,
        uint256 oldAmount,
        uint256 newAmount
    );

    /* -------------------------------------------------------------------------- */
    /*                                   State                                    */
    /* -------------------------------------------------------------------------- */

    /// @dev CD token contract
    FuegoCOLDAOToken public immutable cdToken;

    /// @dev Trusted API verifier address (usexfg.org backend)
    address public apiVerifier;

    /// @dev Arbitrum messenger precompile (0x64) – used to send L2→L1 message
    IArbSys public constant ARB_SYS = IArbSys(address(0x64));

    /// @dev Legacy deposit cutoff (before 2026-01-01 00:00:00 UTC = 80% APY)
    uint256 public constant LEGACY_CUTOFF_TIMESTAMP = 1735689600; // 2026-01-01 00:00:00 UTC

    /// @dev CD interest amounts per tier (initial 8 tiers: 4 amounts × 2 terms)
    /// @dev CD has 12 decimals (1 CD = 10^12 atomic units)
    /// @dev Formula: (XFG_amount / 100,000) × APY × 10^12 = CD_interest

    // Tier 0: 0.8 XFG × 3mo @ 8% APY
    // (0.8 / 100,000) × 0.08 × 10^12 = 640,000 atomic units
    uint256 public constant TIER0_CD_INTEREST = 640_000;

    // Tier 1: 0.8 XFG × 12mo @ 21% APY
    // (0.8 / 100,000) × 0.21 × 10^12 = 1,680,000 atomic units
    uint256 public constant TIER1_CD_INTEREST = 1_680_000;

    // Tier 2: 8 XFG × 3mo @ 21% APY
    // (8 / 100,000) × 0.21 × 10^12 = 16,800,000 atomic units
    uint256 public constant TIER2_CD_INTEREST = 16_800_000;

    // Tier 3: 8 XFG × 12mo @ 33% APY
    // (8 / 100,000) × 0.33 × 10^12 = 26,400,000 atomic units
    uint256 public constant TIER3_CD_INTEREST = 26_400_000;

    // Tier 4: 80 XFG × 3mo @ 33% APY
    // (80 / 100,000) × 0.33 × 10^12 = 264,000,000 atomic units
    uint256 public constant TIER4_CD_INTEREST = 264_000_000;

    // Tier 5: 80 XFG × 12mo @ 55% APY
    // (80 / 100,000) × 0.55 × 10^12 = 440,000,000 atomic units
    uint256 public constant TIER5_CD_INTEREST = 440_000_000;

    // Tier 6: 800 XFG × 3mo @ 55% APY
    // (800 / 100,000) × 0.55 × 10^12 = 4,400,000,000 atomic units
    uint256 public constant TIER6_CD_INTEREST = 4_400_000_000;

    // Tier 7: 800 XFG × 12mo @ 69% APY
    // (800 / 100,000) × 0.69 × 10^12 = 5,520,000,000 atomic units
    uint256 public constant TIER7_CD_INTEREST = 5_520_000_000;

    /// @dev Legacy deposit CD amounts (80% APY ONLY for 800 XFG deposits before 2026)
    /// @dev Only tier 6 and tier 7 (800 XFG) had legacy deposits
    // Legacy Tier 6: 800 XFG × 3mo @ 80% APY
    // Formula: (800 / 100,000) × 0.80 × 10^12 = 6,400,000,000 atomic units
    uint256 public constant LEGACY_TIER6_CD = 6_400_000_000;

    // Legacy Tier 7: 800 XFG × 12mo @ 80% APY (same as tier 6)
    uint256 public constant LEGACY_TIER7_CD = 6_400_000_000;

    /// @dev Dynamic tier storage for DAO-added tiers (tiers 4+)
    /// @dev Maps tier index to CD interest amount
    mapping(uint256 => uint256) public dynamicTiers;

    /// @dev Maximum tier index currently supported
    uint256 public maxTierIndex;

    /// @dev Fuego Mainnet Network ID (chain ID)
    uint256 public constant FUEGO_MAINNET_NETWORK_ID = 93385046440755750514194170694064996624;

    /// @dev Fuego Testnet Network ID ("TEST FUEGO NET  ")
    uint256 public constant FUEGO_TESTNET_NETWORK_ID = 112015110234323138517908755257434054688;

    /// @dev Used nullifiers to prevent double-spending
    mapping(bytes32 => bool) public nullifiersUsed;

    /// @dev Statistics
    uint256 public totalProofsVerified;
    uint256 public totalCDMinted;
    uint256 public totalClaims;

    /* -------------------------------------------------------------------------- */
    /*                                 Constructor                                */
    /* -------------------------------------------------------------------------- */

    constructor(
        address _cdToken,
        address _apiVerifier,
        address initialOwner
    ) Ownable(initialOwner) {
        require(_cdToken != address(0), "Invalid CD token address");
        require(_apiVerifier != address(0), "Invalid API verifier address");

        cdToken = FuegoCOLDAOToken(_cdToken);
        apiVerifier = _apiVerifier;
        maxTierIndex = 7; // Initial 8 tiers (0-7)
    }

    /* -------------------------------------------------------------------------- */
    /*                              Core Functions                                */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Claim CD interest tokens by providing API-verified deposit proof
     * @dev API verifier (usexfg.org) validates STARK proof off-chain and calls this
     * @param recipient Address to receive CD tokens
     * @param tier Tier index: 0-3 (initial), 4+ (DAO-added)
     * @param nullifier Unique nullifier from STARK proof
     * @param commitment Commitment hash from STARK proof
     * @param networkId Network ID from proof (mainnet or testnet)
     * @param depositTimestamp Timestamp of XFG deposit on Fuego (for legacy detection)
     */
    function claimCD(
        address recipient,
        uint8 tier,
        bytes32 nullifier,
        bytes32 commitment,
        uint256 networkId,
        uint256 depositTimestamp
    ) external payable whenNotPaused nonReentrant {
        require(msg.sender == apiVerifier, "Only API verifier can submit proofs");
        require(recipient != address(0), "Invalid recipient address");
        require(tier <= maxTierIndex, "Invalid tier index");
        require(depositTimestamp > 0, "Invalid deposit timestamp");

        // Verify network ID (mainnet or testnet)
        require(
            networkId == FUEGO_MAINNET_NETWORK_ID || networkId == FUEGO_TESTNET_NETWORK_ID,
            "Invalid network ID"
        );

        // Verify nullifier hasn't been used
        require(!nullifiersUsed[nullifier], "Nullifier already used");

        // Mark nullifier used (prevent replay on L2)
        nullifiersUsed[nullifier] = true;

        // Determine if this is a legacy deposit (before 2026-01-01)
        bool isLegacy = depositTimestamp < LEGACY_CUTOFF_TIMESTAMP;

        // Get CD amount for tier (legacy or standard)
        uint256 cdAmount = isLegacy ? _getLegacyCDAmount(tier) : _getStandardCDAmount(tier);

        // Get current edition ID from CD token
        uint256 editionId = cdToken.currentEditionId() - 1; // Current active edition

        // ------------------------------------------------------------------
        // 📤  SEND MESSAGE TO L1 CD TOKEN CONTRACT VIA ARB SYS
        // ------------------------------------------------------------------

        // Compose calldata for L1 mint function (version 3 = COLD deposits)
        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint256,uint32)",
            commitment,
            recipient,
            editionId,
            cdAmount,
            3  // commitment_version = 3 for COLD deposits
        );

        // Enqueue call via ArbSys with L1 gas fees – returns ticket ID
        uint256 ticketId = ARB_SYS.sendTxToL1{value: msg.value}(address(cdToken), data);

        emit L1GasPaid(msg.sender, msg.value, ticketId, commitment);
        emit ProofVerified(
            depositTxHashFromCommitment(commitment),
            recipient,
            cdAmount,
            tier,
            nullifier
        );

        totalProofsVerified += 1;
        totalCDMinted += cdAmount;
        totalClaims += 1;
    }

    /**
     * @dev Get standard CD amount for tier (post-2026 deposits)
     * @param tier Tier index (0-7 initial, 8+ dynamic)
     * @return cdAmount CD interest amount in atomic units
     */
    function _getStandardCDAmount(uint8 tier) internal view returns (uint256 cdAmount) {
        // Initial hardcoded tiers (0-7)
        if (tier == 0) return TIER0_CD_INTEREST;
        if (tier == 1) return TIER1_CD_INTEREST;
        if (tier == 2) return TIER2_CD_INTEREST;
        if (tier == 3) return TIER3_CD_INTEREST;
        if (tier == 4) return TIER4_CD_INTEREST;
        if (tier == 5) return TIER5_CD_INTEREST;
        if (tier == 6) return TIER6_CD_INTEREST;
        if (tier == 7) return TIER7_CD_INTEREST;

        // Dynamic DAO-added tiers (8+)
        uint256 dynamicAmount = dynamicTiers[tier];
        require(dynamicAmount > 0, "Tier not configured");
        return dynamicAmount;
    }

    /**
     * @dev Get legacy CD amount for tier (pre-2026 deposits @ 80% APY)
     * @dev Only 800 XFG deposits (tier 6 and tier 7) had legacy option
     * @param tier Tier index (0-7 initial, 8+ dynamic)
     * @return cdAmount CD interest amount in atomic units
     */
    function _getLegacyCDAmount(uint8 tier) internal view returns (uint256 cdAmount) {
        // Only tier 6 and tier 7 (800 XFG) get legacy bonus
        if (tier == 6) return LEGACY_TIER6_CD;
        if (tier == 7) return LEGACY_TIER7_CD;

        // All other tiers use standard rates even if deposited before 2026
        return _getStandardCDAmount(tier);
    }

    /**
     * @dev Estimate L1 gas fees for cross-chain CD minting
     * @param recipient Address to receive CD tokens
     * @param tier Combined tier (0-7)
     * @param isLegacy True if legacy deposit (pre-2026)
     * @return estimatedGasFee Estimated L1 gas fee in wei
     */
    function estimateL1GasFee(address recipient, uint8 tier, bool isLegacy)
        external
        view
        returns (uint256 estimatedGasFee)
    {
        require(tier <= 7, "Invalid tier");

        uint256 cdAmount = isLegacy ? _getLegacyCDAmount(tier) : _getStandardCDAmount(tier);

        uint256 editionId = cdToken.currentEditionId() - 1;

        // Compose calldata for L1 mint function
        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint256,uint32)",
            bytes32(0), // placeholder commitment
            recipient,
            editionId,
            cdAmount,
            3  // version 3
        );

        // Estimate L1 gas fee based on calldata size and current L1 gas price
        uint256 calldataSize = data.length;
        uint256 estimatedL1GasPrice = 20 gwei; // Conservative estimate

        // Base cost for L2→L1 message + calldata cost
        estimatedGasFee = (21000 + calldataSize * 16) * estimatedL1GasPrice;

        return estimatedGasFee;
    }

    /**
     * @dev Get recommended L1 gas fee with 20% buffer
     * @param recipient Address to receive CD tokens
     * @param tier Combined tier (0-7)
     * @param isLegacy True if legacy deposit (pre-2026)
     * @return recommendedFee Recommended L1 gas fee with 20% buffer
     */
    function getRecommendedGasFee(address recipient, uint8 tier, bool isLegacy)
        external
        view
        returns (uint256 recommendedFee)
    {
        uint256 baseFee = this.estimateL1GasFee(recipient, tier, isLegacy);
        recommendedFee = (baseFee * 120) / 100; // 20% buffer
        return recommendedFee;
    }

    /**
     * @dev Extract deposit transaction hash from commitment (for events)
     * @param commitment Commitment from STARK proof
     * @return Deposit transaction hash
     */
    function depositTxHashFromCommitment(bytes32 commitment) internal pure returns (bytes32) {
        return keccak256(abi.encodePacked("COLD_DEPOSIT:", commitment));
    }

    /* -------------------------------------------------------------------------- */
    /*                              Admin Functions                               */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Update API verifier address (owner only)
     * @param newVerifier New API verifier address
     */
    function updateAPIVerifier(address newVerifier) external onlyOwner {
        require(newVerifier != address(0), "Invalid verifier address");
        address oldVerifier = apiVerifier;
        apiVerifier = newVerifier;
        emit APIVerifierUpdated(oldVerifier, newVerifier);
    }

    /**
     * @dev Pause the contract (owner only)
     */
    function pause() external onlyOwner {
        _pause();
    }

    /**
     * @dev Unpause the contract (owner only)
     */
    function unpause() external onlyOwner {
        _unpause();
    }

    /**
     * @dev Add a new tier (DAO governance only)
     * @param tierIndex Tier index to add (must be maxTierIndex + 1)
     * @param cdAmount CD interest amount for this tier
     */
    function addTier(uint256 tierIndex, uint256 cdAmount) external onlyOwner {
        require(tierIndex == maxTierIndex + 1, "Must add tiers sequentially");
        require(cdAmount > 0, "CD amount must be greater than 0");
        require(tierIndex <= 255, "Tier index too large");

        dynamicTiers[tierIndex] = cdAmount;
        maxTierIndex = tierIndex;

        emit TierAdded(tierIndex, cdAmount);
    }

    /**
     * @dev Update an existing dynamic tier (DAO governance only)
     * @param tierIndex Tier index to update (must be >= 4)
     * @param cdAmount New CD interest amount
     */
    function updateTier(uint256 tierIndex, uint256 cdAmount) external onlyOwner {
        require(tierIndex >= 4, "Cannot update hardcoded tiers");
        require(tierIndex <= maxTierIndex, "Tier does not exist");
        require(cdAmount > 0, "CD amount must be greater than 0");

        uint256 oldAmount = dynamicTiers[tierIndex];
        dynamicTiers[tierIndex] = cdAmount;

        emit TierUpdated(tierIndex, oldAmount, cdAmount);
    }

    /**
     * @dev Rescue accidentally sent ETH
     */
    function rescueETH() external onlyOwner {
        payable(owner()).transfer(address(this).balance);
    }

    /* -------------------------------------------------------------------------- */
    /*                              View Functions                                */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Check if nullifier has been used
     * @param nullifier Nullifier to check
     * @return used True if nullifier has been used
     */
    function isNullifierUsed(bytes32 nullifier) external view returns (bool used) {
        return nullifiersUsed[nullifier];
    }

    /**
     * @dev Get contract statistics
     * @return stats Array of statistics [totalProofs, totalCD, totalClaims]
     */
    function getStatistics() external view returns (uint256[3] memory stats) {
        stats[0] = totalProofsVerified;
        stats[1] = totalCDMinted;
        stats[2] = totalClaims;
    }

    /**
     * @dev Get tier information
     * @param tier Tier index (0-7 initial, 8+ dynamic)
     * @param isLegacy True to get legacy amount (only applies to tier 6-7)
     * @return cdAmount CD interest amount for tier
     * @return xfgAmount XFG amount for tier (in human readable format)
     * @return lockPeriod Human-readable lock period
     * @return apyBps APY in basis points (e.g., 800 = 8%)
     */
    function getTierInfo(uint8 tier, bool isLegacy) external view returns (
        uint256 cdAmount,
        string memory xfgAmount,
        string memory lockPeriod,
        uint256 apyBps
    ) {
        require(tier <= maxTierIndex, "Invalid tier");

        cdAmount = isLegacy ? _getLegacyCDAmount(tier) : _getStandardCDAmount(tier);

        // Determine XFG amount based on tier (initial tiers only)
        if (tier == 0 || tier == 1) {
            xfgAmount = "0.8 XFG";
        } else if (tier == 2 || tier == 3) {
            xfgAmount = "8 XFG";
        } else if (tier == 4 || tier == 5) {
            xfgAmount = "80 XFG";
        } else if (tier == 6 || tier == 7) {
            xfgAmount = "800 XFG";
        } else {
            xfgAmount = "Custom";  // Dynamic tiers
        }

        // Determine lock period based on tier (even = 3mo, odd = 12mo for initial tiers)
        if (tier <= 7) {
            if (tier % 2 == 0) {
                lockPeriod = "3 months";
            } else {
                lockPeriod = "12 months";
            }
        } else {
            lockPeriod = "Custom";  // Dynamic tiers
        }

        // Determine APY (legacy or standard)
        if (isLegacy && (tier == 6 || tier == 7)) {
            apyBps = 8000; // 80% for legacy 800 XFG deposits
        } else {
            // Standard APYs for initial tiers
            if (tier == 0) apyBps = 800;         // 8%
            else if (tier == 1) apyBps = 2100;   // 21%
            else if (tier == 2) apyBps = 2100;   // 21%
            else if (tier == 3) apyBps = 3300;   // 33%
            else if (tier == 4) apyBps = 3300;   // 33%
            else if (tier == 5) apyBps = 5500;   // 55%
            else if (tier == 6) apyBps = 5500;   // 55%
            else if (tier == 7) apyBps = 6900;   // 69%
            else apyBps = 0;  // Dynamic tiers - APY not stored
        }
    }

    /**
     * @dev Get all initial tier amounts (tiers 0-7)
     * @return tier0 through tier7 CD amounts for initial 8 tiers
     */
    function getAllInitialTierAmounts() external pure returns (
        uint256 tier0,
        uint256 tier1,
        uint256 tier2,
        uint256 tier3,
        uint256 tier4,
        uint256 tier5,
        uint256 tier6,
        uint256 tier7
    ) {
        return (
            TIER0_CD_INTEREST,
            TIER1_CD_INTEREST,
            TIER2_CD_INTEREST,
            TIER3_CD_INTEREST,
            TIER4_CD_INTEREST,
            TIER5_CD_INTEREST,
            TIER6_CD_INTEREST,
            TIER7_CD_INTEREST
        );
    }

    /**
     * @dev Get dynamic tier amount (DAO-added tiers 8+)
     * @param tierIndex Tier index (must be >= 8)
     * @return cdAmount CD interest amount for tier
     */
    function getDynamicTierAmount(uint256 tierIndex) external view returns (uint256 cdAmount) {
        require(tierIndex >= 8, "Use getAllInitialTierAmounts for tiers 0-7");
        require(tierIndex <= maxTierIndex, "Tier does not exist");
        return dynamicTiers[tierIndex];
    }

    /**
     * @dev Get legacy tier amounts (only tier 6 and tier 7 have legacy bonuses)
     * @return tier6 Legacy CD amount for 800 XFG × 3mo
     * @return tier7 Legacy CD amount for 800 XFG × 12mo
     */
    function getLegacyTierAmounts() external pure returns (
        uint256 tier6,
        uint256 tier7
    ) {
        return (
            LEGACY_TIER6_CD,
            LEGACY_TIER7_CD
        );
    }

    /**
     * @dev Check if a deposit qualifies for legacy rates
     * @param depositTimestamp Timestamp of deposit on Fuego
     * @param tier Tier index (must be 6 or 7 for legacy)
     * @return isLegacy True if qualifies for 80% legacy rate
     */
    function isLegacyDeposit(uint256 depositTimestamp, uint8 tier) external pure returns (bool isLegacy) {
        // Only tier 6 and tier 7 (800 XFG) had legacy option
        if (tier != 6 && tier != 7) {
            return false;
        }

        // Check if deposit was before 2026-01-01
        return depositTimestamp < LEGACY_CUTOFF_TIMESTAMP;
    }

    /**
     * @dev Get max tier index currently supported
     * @return Current maximum tier index
     */
    function getMaxTierIndex() external view returns (uint256) {
        return maxTierIndex;
    }

    /* -------------------------------------------------------------------------- */
    /*                              Receive Function                              */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Receive function to accept ETH for L1 gas fees
     */
    receive() external payable {}

} /** winter is coming */
