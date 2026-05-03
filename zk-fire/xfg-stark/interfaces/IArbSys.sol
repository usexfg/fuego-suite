// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title IArbSys - Arbitrum System Pre-compile Interface
 * @dev Interface for Arbitrum's system pre-compile at address 0x64
 * @dev Used for L2→L1 message passing via sendTxToL1
 */
interface IArbSys {
    /**
     * @dev Send a transaction to L1
     * @param destination L1 destination address
     * @param calldataForL1 Calldata to be executed on L1
     * @return ticketId Unique identifier for the L2→L1 message
     */
    function sendTxToL1(
        address destination,
        bytes calldata calldataForL1
    ) external payable returns (uint256 ticketId);

    /**
     * @dev Get the current L2 block number
     * @return Current L2 block number
     */
    function arbBlockNumber() external view returns (uint256);

    /**
     * @dev Get the current L2 block timestamp
     * @return Current L2 block timestamp
     */
    function arbBlockTimestamp() external view returns (uint256);

    /**
     * @dev Get the L1 block number when this L2 block was created
     * @return L1 block number
     */
    function arbBlockHash(uint256 blockNumber) external view returns (bytes32);

    /**
     * @dev Get the L1 block number when this L2 block was created
     * @return L1 block number
     */
    function arbL1BlockNumber() external view returns (uint256);
}
