// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title Tier Conversions Library
 * @dev Shared tier constants for XFG ↔ HEAT/CD conversions across all contracts
 * @dev Used by HEATBurnProofVerifier, COLDProofVerifier, and LPRewardsManager
 *
 * HEAT tiers (0-3): amount-based only (4 tiers)
 *   tier = amountIndex
 *
 * COLD tiers (0-7): amount × time (8 tiers, v3 unified EF sigs)
 *   tier = (amountIndex * 2) + termIndex
 *   amountIndex: 0=0.8 XFG, 1=8 XFG, 2=80 XFG, 3=800 XFG
 *   termIndex: 0=3mo, 1=12mo
 */
library TierConversions {

    /* -------------------------------------------------------------------------- */
    /*                    XFG Amount Constants (shared, 4 amounts)               */
    /* -------------------------------------------------------------------------- */

    /// @dev XFG has 7 decimals (1 XFG = 10,000,000 atomic units)
    uint256 public constant XFG_AMT_0 = 8_000_000;        // 0.8 XFG
    uint256 public constant XFG_AMT_1 = 80_000_000;       // 8 XFG
    uint256 public constant XFG_AMT_2 = 800_000_000;      // 80 XFG
    uint256 public constant XFG_AMT_3 = 8_000_000_000;    // 800 XFG

    // Legacy aliases for backward compatibility with HEAT contracts
    uint256 public constant TIER0_XFG = XFG_AMT_0;
    uint256 public constant TIER1_XFG = XFG_AMT_1;
    uint256 public constant TIER2_XFG = XFG_AMT_2;
    uint256 public constant TIER3_XFG = XFG_AMT_3;

    /* -------------------------------------------------------------------------- */
    /*                          HEAT Tier Constants (4 Tiers)                    */
    /* -------------------------------------------------------------------------- */

    /// @dev HEAT has 18 decimals (standard ERC-20); 1 XFG = 10M HEAT
    uint256 public constant TIER0_HEAT = 8_000_000 * 10**18;        // 8M HEAT  (0.8 XFG burned)
    uint256 public constant TIER1_HEAT = 80_000_000 * 10**18;       // 80M HEAT (8 XFG burned)
    uint256 public constant TIER2_HEAT = 800_000_000 * 10**18;      // 800M HEAT (80 XFG burned)
    uint256 public constant TIER3_HEAT = 8_000_000_000 * 10**18;    // 8B HEAT  (800 XFG burned)

    /* -------------------------------------------------------------------------- */
    /*                 COLD CD Interest Constants (8 Tiers, v3)                  */
    /* -------------------------------------------------------------------------- */

    /// @dev CD has 12 decimals. Formula: (XFG / 100,000) × APY × 10^12
    /// Standard rates (post-2026-01-01):
    uint256 public constant COLD_TIER0_CD = 640_000;         // 0.8 XFG × 3mo  @ 8%  APY
    uint256 public constant COLD_TIER1_CD = 2_160_000;       // 0.8 XFG × 12mo @ 27% APY
    uint256 public constant COLD_TIER2_CD = 14_400_000;      // 8 XFG × 3mo    @ 18% APY
    uint256 public constant COLD_TIER3_CD = 26_400_000;      // 8 XFG × 12mo   @ 33% APY
    uint256 public constant COLD_TIER4_CD = 216_000_000;     // 80 XFG × 3mo   @ 27% APY
    uint256 public constant COLD_TIER5_CD = 336_000_000;     // 80 XFG × 12mo  @ 42% APY
    uint256 public constant COLD_TIER6_CD = 2_640_000_000;   // 800 XFG × 3mo  @ 33% APY
    uint256 public constant COLD_TIER7_CD = 5_520_000_000;   // 800 XFG × 12mo @ 69% APY

    /// @dev Legacy rates (pre-2026-01-01, 800 XFG only — tiers 6 & 7):
    uint256 public constant LEGACY_CUTOFF_TIMESTAMP = 1_735_689_600; // 2026-01-01 00:00:00 UTC
    uint256 public constant COLD_LEGACY_TIER6_CD = 6_400_000_000;    // 800 XFG × 3mo  @ 80% APY
    uint256 public constant COLD_LEGACY_TIER7_CD = 6_400_000_000;    // 800 XFG × 12mo @ 80% APY

    /* -------------------------------------------------------------------------- */
    /*                          HEAT Conversion Functions                        */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Get HEAT amount for HEAT burn tier (0-3)
     */
    function getHEATForTier(uint8 tier) internal pure returns (uint256) {
        if (tier == 0) return TIER0_HEAT;
        if (tier == 1) return TIER1_HEAT;
        if (tier == 2) return TIER2_HEAT;
        if (tier == 3) return TIER3_HEAT;
        revert("Invalid HEAT tier: must be 0-3");
    }

    /**
     * @dev Get XFG amount for HEAT burn tier (0-3)
     * @dev Backward-compatible alias — HEAT tiers share XFG amounts with COLD amount indices
     */
    function getXFGForTier(uint8 tier) internal pure returns (uint256) {
        if (tier == 0) return XFG_AMT_0;
        if (tier == 1) return XFG_AMT_1;
        if (tier == 2) return XFG_AMT_2;
        if (tier == 3) return XFG_AMT_3;
        revert("Invalid HEAT tier: must be 0-3");
    }

    /**
     * @dev Validate HEAT burn tier (0-3)
     */
    function isValidTier(uint8 tier) internal pure returns (bool) {
        return tier <= 3;
    }

    /**
     * @dev Get HEAT tier name
     */
    function getTierName(uint8 tier) internal pure returns (string memory) {
        if (tier == 0) return "0.8 XFG -> 8M HEAT";
        if (tier == 1) return "8 XFG -> 80M HEAT";
        if (tier == 2) return "80 XFG -> 800M HEAT";
        if (tier == 3) return "800 XFG -> 8B HEAT";
        revert("Invalid HEAT tier: must be 0-3");
    }

    /* -------------------------------------------------------------------------- */
    /*                     COLD Conversion Functions (v3)                        */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Validate COLD tier (0-7, 8 tiers = 4 amounts × 2 terms)
     */
    function isValidColdTier(uint8 tier) internal pure returns (bool) {
        return tier <= 7;
    }

    /**
     * @dev Get XFG principal for COLD tier (0-7)
     * @dev Even tiers (0,2,4,6) = 3-month lock; odd tiers (1,3,5,7) = 12-month lock
     */
    function getColdXFGForTier(uint8 tier) internal pure returns (uint256) {
        if (tier == 0 || tier == 1) return XFG_AMT_0;  // 0.8 XFG
        if (tier == 2 || tier == 3) return XFG_AMT_1;  // 8 XFG
        if (tier == 4 || tier == 5) return XFG_AMT_2;  // 80 XFG
        if (tier == 6 || tier == 7) return XFG_AMT_3;  // 800 XFG
        revert("Invalid COLD tier: must be 0-7");
    }

    /**
     * @dev Get lock period in months for COLD tier
     * @return months 3 or 12
     */
    function getColdLockMonths(uint8 tier) internal pure returns (uint8 months) {
        require(isValidColdTier(tier), "Invalid COLD tier: must be 0-7");
        return (tier % 2 == 0) ? 3 : 12;
    }

    /**
     * @dev Get standard CD interest for COLD tier (post-2026-01-01)
     */
    function getColdCDInterest(uint8 tier) internal pure returns (uint256) {
        if (tier == 0) return COLD_TIER0_CD;
        if (tier == 1) return COLD_TIER1_CD;
        if (tier == 2) return COLD_TIER2_CD;
        if (tier == 3) return COLD_TIER3_CD;
        if (tier == 4) return COLD_TIER4_CD;
        if (tier == 5) return COLD_TIER5_CD;
        if (tier == 6) return COLD_TIER6_CD;
        if (tier == 7) return COLD_TIER7_CD;
        revert("Invalid COLD tier: must be 0-7");
    }

    /**
     * @dev Get CD interest for COLD tier, accounting for legacy rate
     * @param tier COLD tier (0-7)
     * @param depositTimestamp Unix timestamp of the original Fuego deposit
     * @return cdInterest CD interest in atomic units (12 decimals)
     */
    function getColdCDInterestWithLegacy(uint8 tier, uint256 depositTimestamp)
        internal pure returns (uint256 cdInterest)
    {
        if (depositTimestamp < LEGACY_CUTOFF_TIMESTAMP && tier == 6) return COLD_LEGACY_TIER6_CD;
        if (depositTimestamp < LEGACY_CUTOFF_TIMESTAMP && tier == 7) return COLD_LEGACY_TIER7_CD;
        return getColdCDInterest(tier);
    }

    /**
     * @dev Get CD interest for COLD tier using explicit legacy flag
     * @param tier COLD tier (0-7)
     * @param isLegacy True only for pre-v3 deposits migrated via 0xCE tag (confirmed MultisignatureOutput)
     * @return cdInterest CD interest in atomic units (12 decimals)
     * @dev isLegacy is set on-chain by Fuego L1 — callers cannot forge it without a valid
     *      merkle proof of a CommitmentEntry that had isLegacyMigration = true
     */
    function getColdCDInterestWithLegacyBool(uint8 tier, bool isLegacy)
        internal pure returns (uint256 cdInterest)
    {
        if (isLegacy && tier == 6) return COLD_LEGACY_TIER6_CD;
        if (isLegacy && tier == 7) return COLD_LEGACY_TIER7_CD;
        return getColdCDInterest(tier);
    }

    /**
     * @dev Get COLD tier name
     */
    function getColdTierName(uint8 tier) internal pure returns (string memory) {
        if (tier == 0) return "0.8 XFG x 3mo @ 8%";
        if (tier == 1) return "0.8 XFG x 12mo @ 27%";
        if (tier == 2) return "8 XFG x 3mo @ 18%";
        if (tier == 3) return "8 XFG x 12mo @ 33%";
        if (tier == 4) return "80 XFG x 3mo @ 27%";
        if (tier == 5) return "80 XFG x 12mo @ 42%";
        if (tier == 6) return "800 XFG x 3mo @ 33%";
        if (tier == 7) return "800 XFG x 12mo @ 69%";
        revert("Invalid COLD tier: must be 0-7");
    }
}
