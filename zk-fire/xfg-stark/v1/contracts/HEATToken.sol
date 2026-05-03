// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/token/ERC20/ERC20.sol";
import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/Pausable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";

/**
 * @title Fuego Ξmbers Token (HEAT)
 * @dev Fuego Ξmbers (HEAT) token mints on Ethereum L1 after exact atomic (heat) amount of XFG is burned on Fuego L1 and verified by XFG 𝞝lderfiers, zkSTARK validation on Arbitrum L2, Arbitrum outbox to L1 relays mintHEAT call on L1.
 * @dev Only HEATBurnProofVerifier contract has authority to mint HEAT tokens
 * @dev Standardized burn amount: 0.8 XFG = 8M HEAT
 * @dev Large burn amount: 800 XFG = 8 Billion HEAT
 * @dev HEAT serves as gas token on C0DL3 rollup
 */
contract EmbersTokenHEAT is ERC20, Ownable, Pausable, ReentrancyGuard {
    
    /* -------------------------------------------------------------------------- */
    /*                                   Events                                   */
    /* -------------------------------------------------------------------------- */
    
    event HEATMinted(address indexed to, uint256 amount, uint256 timestamp);
    event HEATBurned(address indexed from, uint256 amount, uint256 timestamp);
    event HEATCollectedForGas(address indexed from, uint256 amount, uint256 timestamp);
    event HEATBurnedForGas(address indexed from, uint256 amount, uint256 timestamp);
    event HEATBurnedByTreasury(address indexed treasury, uint256 amount, uint256 timestamp);
    event MinterUpdated(address indexed oldMinter, address indexed newMinter);
    event CODL3GasCollectorUpdated(address indexed oldCollector, address indexed newCollector);
    event CODL3TreasuryUpdated(address indexed oldTreasury, address indexed newTreasury);
    event HEATMintedFromL2(bytes32 indexed commitment, address indexed recipient, uint256 amount, uint32 version, uint256 timestamp);
    
    /* -------------------------------------------------------------------------- */
    /*                                   State                                    */
    /* -------------------------------------------------------------------------- */
    
    /// @dev Only this contract can mint HEAT tokens (HEATBurnProofVerifier)
    address public minter;
    
    /// @dev Only C0DL3 rollup can collect HEAT for gas fees
    address public codl3GasCollector;
    
    /// @dev C0DL3 treasury address for gas fee collection
    address public codl3Treasury;
    
    /// @dev Total HEAT minted through XFG burns
    uint256 public totalMintedFromBurns;
    
    /// @dev Total HEAT burned (user burns)
    uint256 public totalBurned;
    
    /// @dev Total HEAT collected for C0DL3 gas fees (20% of total to C0LDIGM treasury)
    uint256 public totalCollectedForGas;
    
    /// @dev Total HEAT burned for C0DL3 gas fees (no longer used - send to treasury)
    uint256 public totalBurnedForGas;
    
    /// @dev Total HEAT burned by treasury (quarterly)
    uint256 public totalBurnedByTreasury;
    
    /// @dev Backstop maximum supply of HEAT tokens (69 trillion)
    /// @dev This is a theoretical backstop, not actively enforced
    uint256 public constant BACKSTOP_MAX_SUPPLY = 69_000_000_000_000 * 10**18;
    
    /// @dev Standardized XFG burn amount (0.8 XFG)
    uint256 public constant STANDARDIZED_XFG_BURN = 8_000_000; // 0.8 XFG in atomic units
    
    /// @dev Standardized HEAT mint amount (8M HEAT)
    uint256 public constant STANDARDIZED_HEAT_MINT = 8_000_000 * 10**18;
    
    /// @dev Large XFG burn amount (800 XFG)
    uint256 public constant LARGE_XFG_BURN = 8_000_000_000; // 800 XFG in atomic units

    /// @dev Large HEAT mint amount (8B HEAT)
    uint256 public constant LARGE_HEAT_MINT = 8_000_000_000 * 10**18;

    /// @dev Version 2 - Medium XFG burn amount (80 XFG)
    uint256 public constant MEDIUM_XFG_BURN = 800_000_000; // 80 XFG in atomic units

    /// @dev Version 2 - Medium HEAT mint amount (800M HEAT)
    uint256 public constant MEDIUM_HEAT_MINT = 800_000_000 * 10**18;

    /// @dev Mapping to track used commitments for L2 minting
    mapping(bytes32 => bool) public usedCommitments;
    
    /* -------------------------------------------------------------------------- */
    /*                                 Constructor                                */
    /* -------------------------------------------------------------------------- */
    
    constructor(
        address _initialOwner,
        address _initialMinter
    ) ERC20(unicode"Fuego Ξmbers", "HEAT") Ownable(_initialOwner) {
        require(_initialMinter != address(0), "Invalid minter address");
        minter = _initialMinter;
        codl3GasCollector = address(0); // Will be set when C0DL3 is deployed
        codl3Treasury = address(0); // Will be set when C0DL3 treasury is deployed
        
        // N0 PREMINT SUPPLY - ALL Fuego Ξmbers minted only by XFG burn
    }
    
    /* -------------------------------------------------------------------------- */
    /*                              Minting Functions                             */
    /* -------------------------------------------------------------------------- */
    
    /**
     * @dev Mint HEAT tokens from Elderfier consensus of XFG burn proof
     * @param to Recipient of HEAT tokens
     * @param amount Amount of HEAT to mint (8M HEAT for 0.8 XFG burn or 8B HEAT for 800 XFG burn)
     */

    function mintFromBurnProof(address to, uint256 amount)
        external
        whenNotPaused
        nonReentrant
    {
        require(msg.sender == minter, "Only minter can mint from burn proofs");
        require(to != address(0), "Cannot mint to zero address");
        require(amount > 0, "Amount must be greater than 0");
        require(
            amount == STANDARDIZED_HEAT_MINT || amount == MEDIUM_HEAT_MINT || amount == LARGE_HEAT_MINT,
            "Amount must be 8M, 800M, or 8B HEAT"
        );
        require(totalSupply() + amount <= BACKSTOP_MAX_SUPPLY, "Would exceed backstop max supply"); // if triggered while year < 2034, then ~ wtf (else year>2034:DAOvote)
        
        _mint(to, amount);
        totalMintedFromBurns += amount;
        
        emit HEATMinted(to, amount, block.timestamp);
    }

    /**
     * @dev Mint HEAT from L2 verification via Arbitrum bridge
     * @dev Only callable by Arbitrum's Outbox contract
     * @param commitment Commitment from XFG STARK proof (prevents replay)
     * @param recipient Address to receive HEAT tokens
     * @param amount Amount of HEAT to mint
     * @param version Commitment format version (for future upgrades)
     */

    function mintFromL2(
        bytes32 commitment,
        address recipient,
        uint256 amount,
        uint32 version
    ) external whenNotPaused nonReentrant {
        // Only Arbitrum's Outbox can call this
        require(msg.sender == 0x0B9857ae2D4A3DBe74ffE1d7DF045bb7F96E4840, "Only Arbitrum Outbox");
        require(recipient != address(0), "Cannot mint to zero address");
        require(amount > 0, "Amount must be greater than 0");
        require(
            amount == STANDARDIZED_HEAT_MINT || amount == MEDIUM_HEAT_MINT || amount == LARGE_HEAT_MINT,
            "Amount must be 8M, 800M, or 8B HEAT"
        );
        require(totalSupply() + amount <= BACKSTOP_MAX_SUPPLY, "Would exceed backstop max supply");
        require(version == 1 || version == 2, "Unsupported commitment version");
        
        // Check if commitment has already been used (prevents replay)
        require(!usedCommitments[commitment], "Commitment already used");
        
        // Mark commitment as used
        usedCommitments[commitment] = true;
        
        // Mint tokens
        _mint(recipient, amount);
        totalMintedFromBurns += amount;
        
        emit HEATMinted(recipient, amount, block.timestamp);
        emit HEATMintedFromL2(commitment, recipient, amount, version, block.timestamp);
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
    /*                          HEAT Burning Functions                            */
    /* -------------------------------------------------------------------------- */
    
    /**
     * @dev Burn HEAT tokens
     * @param amount Amount of HEAT to burn
     */
    function burn(uint256 amount) external whenNotPaused {
        require(amount > 0, "Amount must be greater than 0");
        require(balanceOf(msg.sender) >= amount, "Insufficient balance");
        
        _burn(msg.sender, amount);
        totalBurned += amount;
        
        emit HEATBurned(msg.sender, amount, block.timestamp);
    }
    
    /**
     * @dev Burn HEAT tokens from a specific address (with allowance)
     * @param from Address to burn from
     * @param amount Amount of HEAT to burn
     */
    function burnFrom(address from, uint256 amount) external whenNotPaused {
        require(amount > 0, "Amount must be greater than 0");
        require(balanceOf(from) >= amount, "Insufficient balance");
        require(allowance(from, msg.sender) >= amount, "Insufficient allowance");
        
        _spendAllowance(from, msg.sender, amount);
        _burn(from, amount);
        totalBurned += amount;
        
        emit HEATBurned(from, amount, block.timestamp);
    }
    
    /* -------------------------------------------------------------------------- */
    /*                           C0DL3 Gas Functions                              */
    /* -------------------------------------------------------------------------- */
    
    /**
     * @dev Collect HEAT tokens for C0DL3 gas fees (only callable by C0DL3 rollup)
     * @param from Address to collect HEAT from
     * @param totalAmount Total amount of HEAT for gas fees
     * @dev 20% to treasury, 80% to miners/validators
     */
    function collectForCODL3Gas(address from, uint256 totalAmount) 
        external 
        whenNotPaused 
    {
        require(msg.sender == codl3GasCollector, "Only C0DL3 gas_collector can collect for gas");
        require(from != address(0), "Cant collect from zero address");
        require(totalAmount > 0, "Amount must be greater than 0");
        require(balanceOf(from) >= totalAmount, "Insufficient balance");
        
        // Calculate fee distribution
        uint256 treasuryAmount = (totalAmount * 20) / 100; // 20% to COLDIGM treasuries
        uint256 minerAmount = totalAmount - treasuryAmount; // 80% to validators & miners
        
        // Transfer 20% to COLDIGM treasury
        _transfer(from, codl3Treasury, treasuryAmount);
        totalCollectedForGas += treasuryAmount;
        emit HEATCollectedForGas(from, treasuryAmount, block.timestamp);
        
        // Transfer 80% to miners/validators (handled by CODL3)
        // Note: This amount is already deducted from user's balance above
        // C0DL3 will handle the distribution to miners/validators
    }
    
    /**
     * @dev Collect HEAT for C0DL3 gas fees with allowance (only callable by C0DL3 rollup)
     * @param from Address to collect HEAT from
     * @param spender Address that has allowance
     * @param totalAmount Total amount of HEAT for gas fees
     * @dev 20% to C0LDIGM treasury, 80% to miners/validators
     */
    function collectForCODL3GasFrom(address from, address spender, uint256 totalAmount) 
        external 
        whenNotPaused 
    {
        require(msg.sender == codl3GasCollector, "Only C0DL3 gas collector can collect for gas");
        require(from != address(0), "Cant collect from zero address");
        require(totalAmount > 0, "Amount must be greater than 0");
        require(balanceOf(from) >= totalAmount, "Insufficient balance");
        require(allowance(from, spender) >= totalAmount, "Insufficient allowance");
        
        // Calculate fee distribution
        uint256 treasuryAmount = (totalAmount * 20) / 100; // 20% to C0LDIGM treasury
        uint256 minerAmount = totalAmount - treasuryAmount; // 80% to miners
        
        // Spend allowance for the total amount
        _spendAllowance(from, spender, totalAmount);
        
        // Transfer 20% to C0LDIGM treasury
        _transfer(from, codl3Treasury, treasuryAmount);
        totalCollectedForGas += treasuryAmount;
        emit HEATCollectedForGas(from, treasuryAmount, block.timestamp);
        
        // Transfer 80% to miners/validators (handled by C0DL3)
        // Note: This amount is already deducted from user's balance above
        // C0DL3 will handle the distribution to miners/validators
    }
    
    /**
     * @dev Burn HEAT tokens from treasury (only callable by treasury)
     * @param amount Amount of HEAT to burn
     */
    function burnFromTreasury(uint256 amount) 
        external 
        whenNotPaused 
    {
        require(msg.sender == codl3Treasury, "Only C0LDIGM DAO can burn from treasury");
        require(amount > 0, "Amount must be greater than 0");
        require(balanceOf(codl3Treasury) >= amount, "Insufficient treasury balance");
        
        _burn(codl3Treasury, amount);
        totalBurnedByTreasury += amount;
        
        emit HEATBurnedByTreasury(codl3Treasury, amount, block.timestamp);
    }
    
    /* -------------------------------------------------------------------------- */
    /*                        Admin / C0LDDAO Functions                       */
    /* -------------------------------------------------------------------------- */
    
    /**
     * @dev Update the minter address
     * @param newMinter New minter address
     */
    function updateMinter(address newMinter) external onlyOwner {
        require(newMinter != address(0), "Invalid minter address");
        address oldMinter = minter;
        minter = newMinter;
        
        emit MinterUpdated(oldMinter, newMinter);
    }
    
    /**
     * @dev Update the C0DL3 gas collector address
     * @param newGasCollector New C0DL3 gas collector address
     */
    function updateCODL3GasCollector(address newGasCollector) external onlyOwner {
        address oldGasCollector = codl3GasCollector;
        codl3GasCollector = newGasCollector;
        
        emit CODL3GasCollectorUpdated(oldGasCollector, newGasCollector);
    }
    
    /**
     * @dev Update the C0DL3 treasury address
     * @param newTreasury New C0DL3 treasury address
     */
    function updateCODL3Treasury(address newTreasury) external onlyOwner {
        require(newTreasury != address(0), "Invalid treasury address");
        address oldTreasury = codl3Treasury;
        codl3Treasury = newTreasury;
        
        emit CODL3TreasuryUpdated(oldTreasury, newTreasury);
    }
    
    /**
     * @dev Pause all token transfers and minting
     */
    function pause() external onlyOwner {
        _pause();
    }
    
    /**
     * @dev Unpause all token transfers and minting
     */
    function unpause() external onlyOwner {
        _unpause();
    }
    
    /* -------------------------------------------------------------------------- */
    /*                           HEAT View Functions                              */
    /* -------------------------------------------------------------------------- */
    
    /**
     * @dev Get HEAT statistics
     * @return _totalSupply Current total HEAT supply
     * @return _totalMintedFromBurns Total minted from XFG burns (should be == totalSupply)
     * @return _totalBurned Total HEAT burned (user burns)
     * @return _totalCollectedForGas Total collected for C0DL3 gas fees (8% to treasury)
     * @return _totalBurnedForGas Total burned for C0DL3 gas fees (2% immediate burn) no longer used
     * @return _totalBurnedByTreasury Total HEAT burned (by treasury) (quarterly?)
     * @return _backstopMaxSupply Theoretical backstop supply
     */
    function getStats() external view returns (
        uint256 _totalSupply,
        uint256 _totalMintedFromBurns,
        uint256 _totalBurned,
        uint256 _totalCollectedForGas,
        uint256 _totalBurnedForGas,
        uint256 _totalBurnedByTreasury,
        uint256 _backstopMaxSupply
    ) {
        return (
            totalSupply(),
            totalMintedFromBurns,
            totalBurned,
            totalCollectedForGas,
            totalBurnedForGas,
            totalBurnedByTreasury,
            BACKSTOP_MAX_SUPPLY
        );
    }
    
    /**
     * @dev Check if address is current minter
     * @param addr Address to check
     * @return True if address is the minter
     */
    function isMinter(address addr) external view returns (bool) {
        return addr == minter;
    }
    
    /**
     * @dev Check if an address is the current C0DL3 gas_collector
     * @param addr Address to check
     * @return True if address is the C0DL3 gas_collector
     */
    function isCODL3GasCollector(address addr) external view returns (bool) {
        return addr == codl3GasCollector;
    }
    
    /**
     * @dev Check if an address is the current C0DL3 treasury (C0LDIGM)
     * @param addr Address to check
     * @return True if address is the C0DL3 treasury
     */
    function isCODL3Treasury(address addr) external view returns (bool) {
        return addr == codl3Treasury;
    }
    
    /* -------------------------------------------------------------------------- */
    /*                              Override Functions                            */
    /* -------------------------------------------------------------------------- */
    
    /**
     * @dev Override transfer to check for paused state
     */
    function transfer(address to, uint256 amount) 
        public 
        override 
        whenNotPaused 
        returns (bool) 
    {
        return super.transfer(to, amount);
    }
    
    /**
     * @dev Override transferFrom to check for paused state
     */
    function transferFrom(address from, address to, uint256 amount) 
        public 
        override 
        whenNotPaused 
        returns (bool) 
    {
        return super.transferFrom(from, to, amount);
    }
    
    /**
     * @dev Override approve to check for paused state
     */
    function approve(address spender, uint256 amount) 
        public 
        override 
        whenNotPaused 
        returns (bool) 
    {
        return super.approve(spender, amount);
    }
    

} /** winter is coming */
