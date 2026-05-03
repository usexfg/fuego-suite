// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title ICOLDAO Governor Interface
 * @dev Interface for COLDAO governance contract
 * @dev Provides current APY rate for CD interest calculations
 */
interface ICOLDAOGovernor {
    /**
     * @dev Get current APY rate for CD interest
     * @return apyBps APY in basis points (e.g., 800 = 8%)
     */
    function getCurrentAPY() external view returns (uint256 apyBps);

    /**
     * @dev Propose a new APY rate
     * @param newAPYBps New APY in basis points
     * @return proposalId ID of the created proposal
     */
    function proposeAPYChange(uint256 newAPYBps) external returns (uint256 proposalId);

    /**
     * @dev Vote on an APY proposal
     * @param proposalId ID of the proposal
     * @param support True to support, false to oppose
     */
    function voteOnProposal(uint256 proposalId, bool support) external;

    /**
     * @dev Execute an approved APY proposal
     * @param proposalId ID of the proposal
     */
    function executeProposal(uint256 proposalId) external;
}
