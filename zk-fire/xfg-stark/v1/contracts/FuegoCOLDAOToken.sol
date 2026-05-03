// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/token/ERC1155/ERC1155.sol";
import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/Pausable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";

/**
 * @title Fuego COLDAO Token (CD)
 * @dev ERC-1155 semi-fungible token representing DAO voting power & interest earned
 * @dev CD tokens are minted as INTEREST ONLY - XFG principal stays locked on Fuego
 * @dev CD tokens serve multiple purposes:
 *      1. Voting power in COLDAO governance
 *      2. Interest earned from locked XFG deposits (highest APY)
 *      3. Liquidity rewards for HEAT/ETH pair LPs (lower APY than XFG deposits)
 * @dev Interest rate hierarchy: XFG principal deposits > HEAT/ETH LP rewards
 * @dev Supply ratio: 1 COLD : 100,000 XFG (1 XFG = 0.00001 COLD)
 * @dev Edition-based: Max 20.000000000000 COLD per edition (12 decimals)
 * @dev Multiple editions possible: 4 editions = 80 COLD total theoretical max
 * @dev Interest calculation: Supply ratio first, then APY applied
 *
 * Example: 0.8 XFG deposit at 8% APY
 *   1. Supply ratio: 0.8 / 100,000 = 0.000008 COLD base
 *   2. Interest: 0.000008 × 0.08 = 0.00000064 CD minted
 */
contract FuegoCOLDAOToken is ERC1155, Ownable, Pausable, ReentrancyGuard {

    /* -------------------------------------------------------------------------- */
    /*                                   Events                                   */
    /* -------------------------------------------------------------------------- */

    event CDMinted(
        address indexed to,
        uint256 indexed editionId,
        uint256 amount,
        uint256 xfgPrincipal,
        uint256 timestamp
    );

    event EditionCreated(
        uint256 indexed editionId,
        string name,
        uint256 maxSupply,
        uint256 timestamp
    );

    event MinterUpdated(address indexed oldMinter, address indexed newMinter);
    event COLDAOGovernorUpdated(address indexed oldGovernor, address indexed newGovernor);
    event CDMintedFromL2(
        bytes32 indexed commitment,
        address indexed recipient,
        uint256 indexed editionId,
        uint256 amount,
        uint32 version,
        uint256 timestamp
    );

    event MinterAuthorized(address indexed minter);
    event MinterRevoked(address indexed minter);

    /* -------------------------------------------------------------------------- */
    /*                                   State                                    */
    /* -------------------------------------------------------------------------- */

    /// @dev Authorized minters (COLDProofVerifier, LPRewardsManager)
    mapping(address => bool) public authorizedMinters;

    /// @dev COLDAO governance contract (CD holders vote on APY, editions, etc)
    address public coldaoGovernor;

    /// @dev CD token decimals (12 decimals like COLD)
    uint8 public constant DECIMALS = 12;

    /// @dev Supply ratio: 1 COLD : 100,000 XFG
    uint256 public constant SUPPLY_RATIO_DENOMINATOR = 100_000;

    /// @dev Maximum supply per edition (20 COLD with 12 decimals)
    uint256 public constant MAX_SUPPLY_PER_EDITION = 20 * 10**12;

    /// @dev Current edition ID (increments with each new edition)
    uint256 public currentEditionId;

    /// @dev Total CD minted across all editions
    uint256 public totalCDMinted;

    /// @dev Total XFG principal locked from COLD deposits (tracked in atomic units: 7 decimals)
    uint256 public totalXFGPrincipalLocked;

    /// @dev Total HEAT staked in LP positions earning CD rewards (tracked in atomic units: 18 decimals)
    uint256 public totalHEATInLPRewards;

    /// @dev Mapping of edition ID to edition metadata
    struct Edition {
        string name;
        uint256 maxSupply;
        uint256 totalMinted;
        uint256 createdAt;
        bool active;
    }

    mapping(uint256 => Edition) public editions;

    /// @dev Mapping to track used commitments for L2 minting (prevents replay attacks)
    mapping(bytes32 => bool) public usedCommitments;

    /// @dev Mapping to track deposit metadata per holder
    struct DepositInfo {
        uint256 totalPrincipal;    // Total XFG locked (atomic units)
        uint256 totalInterest;     // Total CD interest minted
        uint256 firstDepositTime;  // Timestamp of first deposit
    }

    mapping(address => DepositInfo) public holderDeposits;

    /* -------------------------------------------------------------------------- */
    /*                                 Constructor                                */
    /* -------------------------------------------------------------------------- */

    constructor(
        address _initialMinter,
        address _coldaoGovernor,
        address initialOwner
    ) ERC1155("https://fuego.io/api/cd/{id}.json") Ownable(initialOwner) {
        require(_initialMinter != address(0), "Invalid minter address");
        require(_coldaoGovernor != address(0), "Invalid COLDAO governor address");

        // Authorize initial minter (COLDProofVerifier)
        authorizedMinters[_initialMinter] = true;
        coldaoGovernor = _coldaoGovernor;

        // Create first edition
        _createEdition("Fuego COLDAO 1st Edition ETH", MAX_SUPPLY_PER_EDITION);
    }

    /* -------------------------------------------------------------------------- */
    /*                              Minting Functions                             */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Mint CD interest tokens from XFG COLD deposits
     * @param to Recipient of CD tokens
     * @param editionId Edition ID to mint
     * @param interestAmount Amount of CD interest to mint (in atomic units with 12 decimals)
     * @param xfgPrincipal Amount of XFG principal locked (in atomic units with 7 decimals)
     */
    function mintInterestFromDeposit(
        address to,
        uint256 editionId,
        uint256 interestAmount,
        uint256 xfgPrincipal
    ) external whenNotPaused nonReentrant {
        require(authorizedMinters[msg.sender], "Not authorized to mint");
        require(to != address(0), "Cannot mint to zero address");
        require(interestAmount > 0, "Amount must be greater than 0");
        require(xfgPrincipal > 0, "XFG principal must be greater than 0");

        Edition storage edition = editions[editionId];
        require(edition.active, "Edition not active");
        require(
            edition.totalMinted + interestAmount <= edition.maxSupply,
            "Would exceed edition max supply"
        );

        // Mint CD interest tokens
        _mint(to, editionId, interestAmount, "");

        // Update edition stats
        edition.totalMinted += interestAmount;

        // Update global stats
        totalCDMinted += interestAmount;
        totalXFGPrincipalLocked += xfgPrincipal;

        // Update holder deposit info
        DepositInfo storage deposit = holderDeposits[to];
        deposit.totalPrincipal += xfgPrincipal;
        deposit.totalInterest += interestAmount;
        if (deposit.firstDepositTime == 0) {
            deposit.firstDepositTime = block.timestamp;
        }

        emit CDMinted(to, editionId, interestAmount, xfgPrincipal, block.timestamp);
    }

    /**
     * @dev Mint CD interest tokens from HEAT LP rewards
     * @param to Recipient of CD tokens
     * @param editionId Edition ID to mint
     * @param interestAmount Amount of CD interest to mint (in atomic units with 12 decimals)
     * @param heatAmount Amount of HEAT staked in LP (in atomic units with 18 decimals)
     */
    function mintInterestFromLP(
        address to,
        uint256 editionId,
        uint256 interestAmount,
        uint256 heatAmount
    ) external whenNotPaused nonReentrant {
        require(authorizedMinters[msg.sender], "Not authorized to mint");
        require(to != address(0), "Cannot mint to zero address");
        require(interestAmount > 0, "Amount must be greater than 0");
        require(heatAmount > 0, "HEAT amount must be greater than 0");

        Edition storage edition = editions[editionId];
        require(edition.active, "Edition not active");
        require(
            edition.totalMinted + interestAmount <= edition.maxSupply,
            "Would exceed edition max supply"
        );

        // Mint CD interest tokens
        _mint(to, editionId, interestAmount, "");

        // Update edition stats
        edition.totalMinted += interestAmount;

        // Update global stats
        totalCDMinted += interestAmount;
        totalHEATInLPRewards += heatAmount;

        emit CDMinted(to, editionId, interestAmount, heatAmount, block.timestamp);
    }

    /**
     * @dev Mint CD from L2 verification via Arbitrum bridge
     * @dev Only callable by Arbitrum's Outbox contract
     * @param commitment Commitment from XFG STARK proof (prevents replay)
     * @param recipient Address to receive CD tokens
     * @param editionId Edition ID to mint
     * @param cdAmount Amount of CD interest to mint (fixed per tier)
     * @param version Commitment format version (should be 3 for COLD deposits)
     */
    function mintFromL2(
        bytes32 commitment,
        address recipient,
        uint256 editionId,
        uint256 cdAmount,
        uint32 version
    ) external whenNotPaused nonReentrant {
        // Only Arbitrum's Outbox can call this
        require(msg.sender == 0x0B9857ae2D4A3DBe74ffE1d7DF045bb7F96E4840, "Only Arbitrum Outbox");
        require(recipient != address(0), "Cannot mint to zero address");
        require(cdAmount > 0, "Amount must be greater than 0");
        require(version == 3, "Unsupported commitment version (must be 3 for COLD deposits)");

        // Check if commitment has already been used (prevents replay)
        require(!usedCommitments[commitment], "Commitment already used");

        // Mark commitment as used
        usedCommitments[commitment] = true;

        Edition storage edition = editions[editionId];
        require(edition.active, "Edition not active");
        require(
            edition.totalMinted + cdAmount <= edition.maxSupply,
            "Would exceed edition max supply"
        );

        // Mint CD interest tokens
        _mint(recipient, editionId, cdAmount, "");

        // Update edition stats
        edition.totalMinted += cdAmount;

        // Update global stats
        totalCDMinted += cdAmount;

        emit CDMinted(recipient, editionId, cdAmount, 0, block.timestamp);
        emit CDMintedFromL2(commitment, recipient, editionId, cdAmount, version, block.timestamp);
    }

    /**
     * @dev Check if a commitment has been used
     * @param commitment Commitment to check
     * @return True if commitment has been used
     */
    function isCommitmentUsed(bytes32 commitment) external view returns (bool) {
        return usedCommitments[commitment];
    }

    /* -------------------------------------------------------------------------- */
    /*                          Edition Management                                */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Create a new CD edition (only callable by COLDAO governor)
     * @param name Edition name
     * @param maxSupply Maximum supply for this edition
     */
    function createEdition(string memory name, uint256 maxSupply) external {
        require(msg.sender == coldaoGovernor, "Only COLDAO governor can create editions");
        _createEdition(name, maxSupply);
    }

    /**
     * @dev Internal function to create edition
     */
    function _createEdition(string memory name, uint256 maxSupply) internal {
        require(maxSupply > 0, "Max supply must be greater than 0");
        require(maxSupply <= MAX_SUPPLY_PER_EDITION, "Max supply exceeds limit");

        uint256 editionId = currentEditionId;

        editions[editionId] = Edition({
            name: name,
            maxSupply: maxSupply,
            totalMinted: 0,
            createdAt: block.timestamp,
            active: true
        });

        currentEditionId++;

        emit EditionCreated(editionId, name, maxSupply, block.timestamp);
    }

    /**
     * @dev Deactivate an edition (only callable by COLDAO governor)
     * @param editionId Edition ID to deactivate
     */
    function deactivateEdition(uint256 editionId) external {
        require(msg.sender == coldaoGovernor, "Only COLDAO governor can deactivate editions");
        require(editions[editionId].active, "Edition already inactive");

        editions[editionId].active = false;
    }

    /* -------------------------------------------------------------------------- */
    /*                          Admin Functions                                   */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Add an authorized minter (COLDProofVerifier, LPRewardsManager, etc)
     * @param newMinter Address to authorize
     */
    function addAuthorizedMinter(address newMinter) external onlyOwner {
        require(newMinter != address(0), "Invalid minter address");
        require(!authorizedMinters[newMinter], "Already authorized");

        authorizedMinters[newMinter] = true;
        emit MinterAuthorized(newMinter);
    }

    /**
     * @dev Remove an authorized minter
     * @param minter Address to revoke
     */
    function removeAuthorizedMinter(address minter) external onlyOwner {
        require(authorizedMinters[minter], "Not authorized");

        authorizedMinters[minter] = false;
        emit MinterRevoked(minter);
    }

    /**
     * @dev Check if address is authorized minter
     * @param minter Address to check
     * @return True if authorized
     */
    function isAuthorizedMinter(address minter) external view returns (bool) {
        return authorizedMinters[minter];
    }

    /**
     * @dev Update the COLDAO governor address
     * @param newGovernor New COLDAO governor address
     */
    function updateCOLDAOGovernor(address newGovernor) external onlyOwner {
        require(newGovernor != address(0), "Invalid governor address");
        address oldGovernor = coldaoGovernor;
        coldaoGovernor = newGovernor;
        emit COLDAOGovernorUpdated(oldGovernor, newGovernor);
    }

    /**
     * @dev Update token URI
     * @param newuri New base URI for token metadata
     */
    function setURI(string memory newuri) external onlyOwner {
        _setURI(newuri);
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

    /* -------------------------------------------------------------------------- */
    /*                          View Functions                                    */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Get edition information
     * @param editionId Edition ID
     * @return edition Edition struct
     */
    function getEdition(uint256 editionId) external view returns (Edition memory edition) {
        return editions[editionId];
    }

    /**
     * @dev Get deposit information for a holder
     * @param holder Address of CD holder
     * @return depositInfo Deposit information struct
     */
    function getDepositInfo(address holder) external view returns (DepositInfo memory depositInfo) {
        return holderDeposits[holder];
    }

    /**
     * @dev Get voting power for a holder (sum of all CD balances across editions)
     * @param holder Address of CD holder
     * @return votingPower Total CD balance for DAO voting
     */
    function getVotingPower(address holder) external view returns (uint256 votingPower) {
        votingPower = 0;
        for (uint256 i = 0; i < currentEditionId; i++) {
            votingPower += balanceOf(holder, i);
        }
        return votingPower;
    }

    /**
     * @dev Get total XFG locked in human-readable format
     * @return xfgLocked Total XFG locked (with 7 decimal places)
     */
    function getTotalXFGLockedReadable() external view returns (uint256 xfgLocked) {
        // XFG has 7 decimal places, so divide by 10^7
        return totalXFGPrincipalLocked / 10_000_000;
    }

    /**
     * @dev Get total CD supply across all editions
     * @return totalSupply Total CD minted
     */
    function getTotalSupply() external view returns (uint256 totalSupply) {
        return totalCDMinted;
    }

    /**
     * @dev Get available supply for current edition
     * @return available Available supply in current edition
     */
    function getAvailableSupply() external view returns (uint256 available) {
        if (currentEditionId == 0) return 0;

        uint256 activeEditionId = currentEditionId - 1;
        Edition memory edition = editions[activeEditionId];

        if (!edition.active) return 0;

        return edition.maxSupply - edition.totalMinted;
    }

    /* -------------------------------------------------------------------------- */
    /*                          ERC1155 Overrides                                 */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Override safeTransferFrom to respect pause state
     */
    function safeTransferFrom(
        address from,
        address to,
        uint256 id,
        uint256 amount,
        bytes memory data
    ) public override whenNotPaused {
        super.safeTransferFrom(from, to, id, amount, data);
    }

    /**
     * @dev Override safeBatchTransferFrom to respect pause state
     */
    function safeBatchTransferFrom(
        address from,
        address to,
        uint256[] memory ids,
        uint256[] memory amounts,
        bytes memory data
    ) public override whenNotPaused {
        super.safeBatchTransferFrom(from, to, ids, amounts, data);
    }

} /** winter is coming */
