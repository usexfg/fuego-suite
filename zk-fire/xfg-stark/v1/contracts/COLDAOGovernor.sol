// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/Pausable.sol";
import "./FuegoCOLDAOToken.sol";

/**
 * @title COLDAO Governor
 * @dev Governance contract for Fuego COLDAO (CD token holders)
 * @dev CD token holders can propose and vote on:
 *      - APY rate changes for CD interest
 *      - New edition launches
 *      - Approved interest tokens for v4 (COLD YIELD)
 * @dev Voting power = CD token balance across all editions <----<actually, each of the (4) editions will host its own voting consensus on the platform it was issued. 
 * however, in the case of COLDAO matters as a whole, in other words ALL members of each edition of COLDAO are effected-
  *this 1st Edition ETH ( CODL3 ) issuance will hold voting weight of 40%, with the other 3 COLDAO editions each having 20% voting share; to help avoid voting stalemates & promote dynamic governance.
 */
contract COLDAOGovernor is Ownable, Pausable {

    /* -------------------------------------------------------------------------- */
    /*                                   Events                                   */
    /* -------------------------------------------------------------------------- */

    event ProposalCreated(
        uint256 indexed proposalId,
        address indexed proposer,
        ProposalType proposalType,
        string description,
        uint256 timestamp
    );

    event VoteCast(
        uint256 indexed proposalId,
        address indexed voter,
        bool support,
        uint256 votingPower,
        uint256 timestamp
    );

    event ProposalExecuted(
        uint256 indexed proposalId,
        bool passed,
        uint256 timestamp
    );

    event APYUpdated(
        uint256 oldAPY,
        uint256 newAPY,
        uint256 timestamp
    );

    event QuorumUpdated(
        uint256 oldQuorum,
        uint256 newQuorum
    );

    /* -------------------------------------------------------------------------- */
    /*                                   Structs                                  */
    /* -------------------------------------------------------------------------- */

    enum ProposalType {
        APY_CHANGE,
        EDITION_LAUNCH,
        INTEREST_TOKEN_APPROVAL
    }

    enum ProposalStatus {
        Pending,
        Active,
        Defeated,
        Succeeded,
        Executed
    }

    struct Proposal {
        uint256 id;
        address proposer;
        ProposalType proposalType;
        string description;
        uint256 newAPYBps;           // For APY_CHANGE proposals
        string newEditionName;       // For EDITION_LAUNCH proposals
        address interestToken;       // For INTEREST_TOKEN_APPROVAL proposals
        uint256 votesFor;
        uint256 votesAgainst;
        uint256 startBlock;
        uint256 endBlock;
        ProposalStatus status;
        mapping(address => bool) hasVoted;
    }

    /* -------------------------------------------------------------------------- */
    /*                                   State                                    */
    /* -------------------------------------------------------------------------- */

    /// @dev Fuego COLDAO token contract (CD)
    FuegoCOLDAOToken public immutable cdToken;

    /// @dev Current APY rate in basis points (e.g., 800 = 8%)
    uint256 public currentAPYBps;

    /// @dev Minimum voting period (in blocks)
    uint256 public votingPeriod;

    /// @dev Quorum requirement (basis points, e.g., 2000 = 20% of total supply)
    uint256 public quorumBps;

    /// @dev Minimum CD balance required to create proposals
    uint256 public proposalThreshold;

    /// @dev Proposal counter
    uint256 public proposalCount;

    /// @dev Mapping of proposal ID to proposal
    mapping(uint256 => Proposal) public proposals;

    /// @dev Approved interest tokens for v4 (COLD YIELD)
    mapping(address => bool) public approvedInterestTokens;

    /* -------------------------------------------------------------------------- */
    /*                                 Constructor                                */
    /* -------------------------------------------------------------------------- */

    constructor(
        address _cdToken,
        uint256 _initialAPYBps,
        address initialOwner
    ) Ownable(initialOwner) {
        require(_cdToken != address(0), "Invalid CD token address");
        require(_initialAPYBps > 0 && _initialAPYBps <= 10000, "Invalid APY");

        cdToken = FuegoCOLDAOToken(_cdToken);
        currentAPYBps = _initialAPYBps;
        votingPeriod = 17280; // ~3 days at 15s block time
        quorumBps = 2000; // 20% quorum
        proposalThreshold = 1 * 10**12; // 1 CD minimum to propose
    }

    /* -------------------------------------------------------------------------- */
    /*                          Proposal Functions                                */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Create a proposal to change APY
     * @param newAPYBps New APY in basis points
     * @param description Proposal description
     * @return proposalId ID of the created proposal
     */
    function proposeAPYChange(uint256 newAPYBps, string memory description)
        external
        whenNotPaused
        returns (uint256 proposalId)
    {
        require(newAPYBps > 0 && newAPYBps <= 10000, "Invalid APY");
        require(
            cdToken.getVotingPower(msg.sender) >= proposalThreshold,
            "Insufficient CD balance to propose"
        );

        proposalId = proposalCount++;
        Proposal storage proposal = proposals[proposalId];

        proposal.id = proposalId;
        proposal.proposer = msg.sender;
        proposal.proposalType = ProposalType.APY_CHANGE;
        proposal.description = description;
        proposal.newAPYBps = newAPYBps;
        proposal.startBlock = block.number;
        proposal.endBlock = block.number + votingPeriod;
        proposal.status = ProposalStatus.Active;

        emit ProposalCreated(
            proposalId,
            msg.sender,
            ProposalType.APY_CHANGE,
            description,
            block.timestamp
        );

        return proposalId;
    }

    /**
     * @dev Create a proposal to launch a new edition
     * @param editionName Name for the new edition
     * @param description Proposal description
     * @return proposalId ID of the created proposal
     */
    function proposeEditionLaunch(string memory editionName, string memory description)
        external
        whenNotPaused
        returns (uint256 proposalId)
    {
        require(bytes(editionName).length > 0, "Edition name cannot be empty");
        require(
            cdToken.getVotingPower(msg.sender) >= proposalThreshold,
            "Insufficient CD balance to propose"
        );

        proposalId = proposalCount++;
        Proposal storage proposal = proposals[proposalId];

        proposal.id = proposalId;
        proposal.proposer = msg.sender;
        proposal.proposalType = ProposalType.EDITION_LAUNCH;
        proposal.description = description;
        proposal.newEditionName = editionName;
        proposal.startBlock = block.number;
        proposal.endBlock = block.number + votingPeriod;
        proposal.status = ProposalStatus.Active;

        emit ProposalCreated(
            proposalId,
            msg.sender,
            ProposalType.EDITION_LAUNCH,
            description,
            block.timestamp
        );

        return proposalId;
    }

    /**
     * @dev Create a proposal to approve an interest token for v4
     * @param interestToken Address of interest token to approve
     * @param description Proposal description
     * @return proposalId ID of the created proposal
     */
    function proposeInterestTokenApproval(address interestToken, string memory description)
        external
        whenNotPaused
        returns (uint256 proposalId)
    {
        require(interestToken != address(0), "Invalid token address");
        require(
            cdToken.getVotingPower(msg.sender) >= proposalThreshold,
            "Insufficient CD balance to propose"
        );

        proposalId = proposalCount++;
        Proposal storage proposal = proposals[proposalId];

        proposal.id = proposalId;
        proposal.proposer = msg.sender;
        proposal.proposalType = ProposalType.INTEREST_TOKEN_APPROVAL;
        proposal.description = description;
        proposal.interestToken = interestToken;
        proposal.startBlock = block.number;
        proposal.endBlock = block.number + votingPeriod;
        proposal.status = ProposalStatus.Active;

        emit ProposalCreated(
            proposalId,
            msg.sender,
            ProposalType.INTEREST_TOKEN_APPROVAL,
            description,
            block.timestamp
        );

        return proposalId;
    }

    /**
     * @dev Vote on a proposal
     * @param proposalId ID of the proposal
     * @param support True to support, false to oppose
     */
    function voteOnProposal(uint256 proposalId, bool support) external whenNotPaused {
        Proposal storage proposal = proposals[proposalId];

        require(proposal.status == ProposalStatus.Active, "Proposal not active");
        require(block.number <= proposal.endBlock, "Voting period ended");
        require(!proposal.hasVoted[msg.sender], "Already voted");

        uint256 votingPower = cdToken.getVotingPower(msg.sender);
        require(votingPower > 0, "No voting power");

        proposal.hasVoted[msg.sender] = true;

        if (support) {
            proposal.votesFor += votingPower;
        } else {
            proposal.votesAgainst += votingPower;
        }

        emit VoteCast(proposalId, msg.sender, support, votingPower, block.timestamp);
    }

    /**
     * @dev Execute a proposal after voting period ends
     * @param proposalId ID of the proposal
     */
    function executeProposal(uint256 proposalId) external whenNotPaused {
        Proposal storage proposal = proposals[proposalId];

        require(proposal.status == ProposalStatus.Active, "Proposal not active");
        require(block.number > proposal.endBlock, "Voting period not ended");

        // Check quorum
        uint256 totalVotes = proposal.votesFor + proposal.votesAgainst;
        uint256 requiredQuorum = (cdToken.getTotalSupply() * quorumBps) / 10000;

        bool passed = false;

        if (totalVotes >= requiredQuorum && proposal.votesFor > proposal.votesAgainst) {
            passed = true;
            proposal.status = ProposalStatus.Succeeded;

            // Execute based on proposal type
            if (proposal.proposalType == ProposalType.APY_CHANGE) {
                uint256 oldAPY = currentAPYBps;
                currentAPYBps = proposal.newAPYBps;
                emit APYUpdated(oldAPY, proposal.newAPYBps, block.timestamp);
            } else if (proposal.proposalType == ProposalType.EDITION_LAUNCH) {
                cdToken.createEdition(proposal.newEditionName, cdToken.MAX_SUPPLY_PER_EDITION());
            } else if (proposal.proposalType == ProposalType.INTEREST_TOKEN_APPROVAL) {
                approvedInterestTokens[proposal.interestToken] = true;
            }
        } else {
            proposal.status = ProposalStatus.Defeated;
        }

        proposal.status = ProposalStatus.Executed;

        emit ProposalExecuted(proposalId, passed, block.timestamp);
    }

    /* -------------------------------------------------------------------------- */
    /*                          View Functions                                    */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Get current APY rate (for COLDProofVerifier)
     * @return apyBps APY in basis points
     */
    function getCurrentAPY() external view returns (uint256 apyBps) {
        return currentAPYBps;
    }

    /**
     * @dev Get proposal details
     * @param proposalId ID of the proposal
     * @return id Proposal ID
     * @return proposer Proposer address
     * @return proposalType Type of proposal
     * @return description Proposal description
     * @return votesFor Votes in favor
     * @return votesAgainst Votes against
     * @return startBlock Start block
     * @return endBlock End block
     * @return status Proposal status
     */
    function getProposal(uint256 proposalId)
        external
        view
        returns (
            uint256 id,
            address proposer,
            ProposalType proposalType,
            string memory description,
            uint256 votesFor,
            uint256 votesAgainst,
            uint256 startBlock,
            uint256 endBlock,
            ProposalStatus status
        )
    {
        Proposal storage proposal = proposals[proposalId];
        return (
            proposal.id,
            proposal.proposer,
            proposal.proposalType,
            proposal.description,
            proposal.votesFor,
            proposal.votesAgainst,
            proposal.startBlock,
            proposal.endBlock,
            proposal.status
        );
    }

    /**
     * @dev Check if address has voted on proposal
     * @param proposalId ID of the proposal
     * @param voter Address to check
     * @return hasVoted True if already voted
     */
    function hasVoted(uint256 proposalId, address voter) external view returns (bool) {
        return proposals[proposalId].hasVoted[voter];
    }

    /**
     * @dev Check if interest token is approved for v4
     * @param token Token address to check
     * @return approved True if approved
     */
    function isInterestTokenApproved(address token) external view returns (bool approved) {
        return approvedInterestTokens[token];
    }

    /* -------------------------------------------------------------------------- */
    /*                          Admin Functions                                   */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Update quorum requirement (only owner)
     * @param newQuorumBps New quorum in basis points
     */
    function updateQuorum(uint256 newQuorumBps) external onlyOwner {
        require(newQuorumBps > 0 && newQuorumBps <= 10000, "Invalid quorum");

        uint256 oldQuorum = quorumBps;
        quorumBps = newQuorumBps;

        emit QuorumUpdated(oldQuorum, newQuorumBps);
    }

    /**
     * @dev Update voting period (only owner)
     * @param newVotingPeriod New voting period in blocks
     */
    function updateVotingPeriod(uint256 newVotingPeriod) external onlyOwner {
        require(newVotingPeriod > 0, "Invalid voting period");
        votingPeriod = newVotingPeriod;
    }

    /**
     * @dev Update proposal threshold (only owner)
     * @param newThreshold New threshold in CD atomic units
     */
    function updateProposalThreshold(uint256 newThreshold) external onlyOwner {
        require(newThreshold > 0, "Invalid threshold");
        proposalThreshold = newThreshold;
    }

    /**
     * @dev Pause the contract (emergency use only)
     */
    function pause() external onlyOwner {
        _pause();
    }

    /**
     * @dev Unpause the contract
     */
    function unpause() external onlyOwner {
        _unpause();
    }

} /** winter is coming */
