// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/Pausable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";
import "@openzeppelin/contracts/token/ERC20/IERC20.sol";
import "./FuegoCOLDAOToken.sol";

/**
 * @title LP Rewards Manager
 * @dev Manages time-weighted CD rewards for HEAT/ETH LP providers
 * @dev Rewards based on HEAT amount in LP position with 4 tiers matching COLD/HEAT structure:
 *      - Tier 0: 8M HEAT (0.8 XFG)    → 8% → 18% after 1 year
 *      - Tier 1: 80M HEAT (8 XFG)     → 18% → 33% after 1 year
 *      - Tier 2: 800M HEAT (80 XFG)   → 33% → 55% after 1 year
 *      - Tier 3: 8B HEAT (800 XFG)    → 55% → 69% after 1 year
 * @dev Minimum 8M HEAT (0.8 XFG equivalent) required to receive rewards
 * @dev Time-weighted APY increases linearly from base to target over 1 year
 */
contract LPRewardsManager is Ownable, Pausable, ReentrancyGuard {

    /* -------------------------------------------------------------------------- */
    /*                                 Interfaces                                 */
    /* -------------------------------------------------------------------------- */

    interface IUniswapV2Pair {
        function token0() external view returns (address);
        function token1() external view returns (address);
        function getReserves() external view returns (uint112 reserve0, uint112 reserve1, uint32 blockTimestampLast);
        function totalSupply() external view returns (uint256);
    }

    /* -------------------------------------------------------------------------- */
    /*                                   Events                                   */
    /* -------------------------------------------------------------------------- */

    event LPStaked(
        address indexed user,
        uint256 lpTokenAmount,
        uint8 tier,
        uint256 timestamp
    );

    event RewardsClaimed(
        address indexed user,
        uint256 cdRewards,
        uint256 effectiveAPY,
        uint256 timestamp
    );

    event LPUnstaked(
        address indexed user,
        uint256 lpTokenAmount,
        uint256 finalRewards,
        uint256 timestamp
    );

    event LPTokenUpdated(
        address indexed oldLPToken,
        address indexed newLPToken
    );

    /* -------------------------------------------------------------------------- */
    /*                                   Structs                                  */
    /* -------------------------------------------------------------------------- */

    struct LPPosition {
        uint256 lpTokenAmount;    // Amount of LP tokens staked
        uint256 startTime;        // Position start timestamp
        uint256 lastClaimTime;    // Last reward claim timestamp
        uint8 tier;               // 0, 1, 2, or 3
        bool active;              // Position status
    }

    /* -------------------------------------------------------------------------- */
    /*                                   State                                    */
    /* -------------------------------------------------------------------------- */

    /// @dev Fuego COLDAO Token contract (CD tokens)
    FuegoCOLDAOToken public immutable cdToken;

    /// @dev HEAT token address
    address public immutable heatToken;

    /// @dev HEAT/ETH LP token address (Uniswap V2/V3)
    address public lpToken;

    /// @dev LP positions by holder address
    mapping(address => LPPosition) public lpPositions;

    /// @dev HEAT amount thresholds (atomic units, 18 decimals) - matches TierConversions.sol
    uint256 public constant MIN_HEAT_REQUIRED = 8_000_000 * 10**18;        // 8M HEAT (0.8 XFG)
    uint256 public constant TIER0_HEAT_THRESHOLD = 8_000_000 * 10**18;     // 8M HEAT (0.8 XFG)
    uint256 public constant TIER1_HEAT_THRESHOLD = 80_000_000 * 10**18;    // 80M HEAT (8 XFG)
    uint256 public constant TIER2_HEAT_THRESHOLD = 800_000_000 * 10**18;   // 800M HEAT (80 XFG)
    uint256 public constant TIER3_HEAT_THRESHOLD = 8_000_000_000 * 10**18; // 8B HEAT (800 XFG)

    /// @dev Time constants
    uint256 public constant YEAR_IN_SECONDS = 365 days;

    /// @dev Base APYs (starting rates) per tier in basis points
    uint256 public constant TIER0_BASE_APY = 800;   // 8%
    uint256 public constant TIER1_BASE_APY = 1800;  // 18%
    uint256 public constant TIER2_BASE_APY = 3300;  // 33%
    uint256 public constant TIER3_BASE_APY = 5500;  // 55%

    /// @dev Target APYs (after 1 year) per tier in basis points
    uint256 public constant TIER0_TARGET_APY = 1800;  // 18%
    uint256 public constant TIER1_TARGET_APY = 3300;  // 33%
    uint256 public constant TIER2_TARGET_APY = 5500;  // 55%
    uint256 public constant TIER3_TARGET_APY = 6900;  // 69%

    /// @dev Edition ID for CD token minting
    uint256 public currentEditionId;

    /// @dev Total LP tokens staked
    uint256 public totalLPStaked;

    /// @dev Total HEAT in LP positions
    uint256 public totalHEATStaked;

    /// @dev Total CD rewards distributed
    uint256 public totalCDRewardsDistributed;

    /* -------------------------------------------------------------------------- */
    /*                                 Constructor                                */
    /* -------------------------------------------------------------------------- */

    constructor(
        address _cdToken,
        address _heatToken,
        address _lpToken,
        uint256 _editionId,
        address initialOwner
    ) Ownable(initialOwner) {
        require(_cdToken != address(0), "Invalid CD token address");
        require(_heatToken != address(0), "Invalid HEAT token address");
        require(_lpToken != address(0), "Invalid LP token address");

        cdToken = FuegoCOLDAOToken(_cdToken);
        heatToken = _heatToken;
        lpToken = _lpToken;
        currentEditionId = _editionId;
    }

    /* -------------------------------------------------------------------------- */
    /*                          LP Staking Functions                              */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Stake LP tokens and start earning time-weighted CD rewards
     * @dev Tier is auto-calculated based on HEAT amount in LP position
     * @param lpAmount Amount of LP tokens to stake
     */
    function stakeLPTokens(uint256 lpAmount)
        external
        whenNotPaused
        nonReentrant
    {
        require(lpAmount > 0, "Amount must be greater than 0");
        require(!lpPositions[msg.sender].active, "Already have active LP position");

        // Calculate HEAT amount in LP position
        uint256 heatAmount = getHEATFromLPTokens(lpAmount);
        require(heatAmount >= MIN_HEAT_REQUIRED, "Insufficient HEAT in LP (minimum 8M HEAT required)");

        // Auto-assign tier based on HEAT amount
        uint8 tier = getTierFromHEATAmount(heatAmount);

        // Transfer LP tokens from user to this contract
        IERC20(lpToken).transferFrom(msg.sender, address(this), lpAmount);

        // Create LP position
        lpPositions[msg.sender] = LPPosition({
            lpTokenAmount: lpAmount,
            startTime: block.timestamp,
            lastClaimTime: block.timestamp,
            tier: tier,
            active: true
        });

        // Update global stats
        totalLPStaked += lpAmount;
        totalHEATStaked += heatAmount;

        emit LPStaked(msg.sender, lpAmount, tier, block.timestamp);
    }

    /**
     * @dev Claim CD rewards based on time-weighted APY
     * @param recipient Address to receive CD tokens (use fresh address for privacy)
     * @return cdRewards Amount of CD tokens claimed
     */
    function claimRewards(address recipient)
        external
        whenNotPaused
        nonReentrant
        returns (uint256 cdRewards)
    {
        require(recipient != address(0), "Invalid recipient address");

        LPPosition storage position = lpPositions[msg.sender];
        require(position.active, "No active LP position");

        // Calculate HEAT amount in LP position
        uint256 heatAmount = getHEATFromLPTokens(position.lpTokenAmount);

        // Calculate time-weighted rewards
        uint256 effectiveAPY = calculateTimeWeightedAPY(msg.sender);
        uint256 duration = block.timestamp - position.lastClaimTime;

        // Calculate CD rewards based on HEAT amount
        // Formula: (heatAmount × effectiveAPY × duration) / (365 days × 10000)
        cdRewards = (heatAmount * effectiveAPY * duration) /
                    (YEAR_IN_SECONDS * 10000);

        require(cdRewards > 0, "No rewards to claim");

        // Mint CD tokens to RECIPIENT (not msg.sender) for privacy
        cdToken.mintInterestFromLP(
            recipient,
            currentEditionId,
            cdRewards,
            heatAmount
        );

        // Update position
        position.lastClaimTime = block.timestamp;

        // Update global stats
        totalCDRewardsDistributed += cdRewards;

        emit RewardsClaimed(recipient, cdRewards, effectiveAPY, block.timestamp);

        return cdRewards;
    }

    /**
     * @dev Unstake LP tokens and claim final rewards
     * @param recipient Address to receive final CD tokens (use fresh address for privacy)
     * @return finalRewards Final CD rewards claimed
     */
    function unstakeLPTokens(address recipient)
        external
        whenNotPaused
        nonReentrant
        returns (uint256 finalRewards)
    {
        require(recipient != address(0), "Invalid recipient address");

        LPPosition storage position = lpPositions[msg.sender];
        require(position.active, "No active LP position");

        uint256 lpAmount = position.lpTokenAmount;
        uint256 heatAmount = getHEATFromLPTokens(lpAmount);

        // Claim final rewards
        uint256 effectiveAPY = calculateTimeWeightedAPY(msg.sender);
        uint256 duration = block.timestamp - position.lastClaimTime;

        // Calculate final CD rewards based on HEAT amount
        finalRewards = (heatAmount * effectiveAPY * duration) /
                       (YEAR_IN_SECONDS * 10000);

        if (finalRewards > 0) {
            // Mint final CD tokens to RECIPIENT (not msg.sender) for privacy
            cdToken.mintInterestFromLP(
                recipient,
                currentEditionId,
                finalRewards,
                heatAmount
            );

            // Update global stats
            totalCDRewardsDistributed += finalRewards;
        }

        // Close position
        position.active = false;

        // Update global stats
        totalLPStaked -= lpAmount;
        totalHEATStaked -= heatAmount;

        // Return LP tokens to user (still goes to msg.sender)
        IERC20(lpToken).transfer(msg.sender, lpAmount);

        emit LPUnstaked(msg.sender, lpAmount, finalRewards, block.timestamp);

        return finalRewards;
    }

    /* -------------------------------------------------------------------------- */
    /*                          View Functions                                    */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Calculate time-weighted APY for LP position
     * @dev Formula: effectiveAPY = baseAPY + (targetAPY - baseAPY) × min(duration / 1 year, 1)
     * @param lpHolder Address of LP holder
     * @return effectiveAPY Time-weighted APY in basis points
     */
    function calculateTimeWeightedAPY(address lpHolder)
        public
        view
        returns (uint256 effectiveAPY)
    {
        LPPosition memory position = lpPositions[lpHolder];
        require(position.active, "No active LP position");

        uint256 duration = block.timestamp - position.startTime;
        uint256 baseAPY;
        uint256 targetAPY;

        // Get base and target APY based on tier
        if (position.tier == 0) {
            baseAPY = TIER0_BASE_APY;      // 8%
            targetAPY = TIER0_TARGET_APY;  // 18%
        } else if (position.tier == 1) {
            baseAPY = TIER1_BASE_APY;      // 18%
            targetAPY = TIER1_TARGET_APY;  // 33%
        } else if (position.tier == 2) {
            baseAPY = TIER2_BASE_APY;      // 33%
            targetAPY = TIER2_TARGET_APY;  // 55%
        } else {
            baseAPY = TIER3_BASE_APY;      // 55%
            targetAPY = TIER3_TARGET_APY;  // 69%
        }

        // Linear interpolation from baseAPY to targetAPY over 1 year
        // Formula: effectiveAPY = baseAPY + (targetAPY - baseAPY) × min(duration / 1 year, 1)
        uint256 apyGrowth = targetAPY - baseAPY;
        uint256 timeMultiplier = duration >= YEAR_IN_SECONDS ?
            10000 : (duration * 10000) / YEAR_IN_SECONDS;

        effectiveAPY = baseAPY + ((apyGrowth * timeMultiplier) / 10000);

        return effectiveAPY;
    }

    /**
     * @dev Calculate HEAT amount from LP tokens using pool reserves
     * @param lpAmount Amount of LP tokens
     * @return heatAmount Amount of HEAT represented by LP tokens
     */
    function getHEATFromLPTokens(uint256 lpAmount)
        public
        view
        returns (uint256 heatAmount)
    {
        IUniswapV2Pair pair = IUniswapV2Pair(lpToken);

        // Get reserves from Uniswap pool
        (uint112 reserve0, uint112 reserve1,) = pair.getReserves();
        uint256 totalSupply = pair.totalSupply();

        require(totalSupply > 0, "Pool has no liquidity");

        // Determine which reserve is HEAT
        address token0 = pair.token0();
        uint256 heatReserve = (token0 == heatToken) ? uint256(reserve0) : uint256(reserve1);

        // Calculate HEAT amount in LP position
        // Formula: (lpAmount × heatReserve) / totalSupply
        heatAmount = (lpAmount * heatReserve) / totalSupply;

        return heatAmount;
    }

    /**
     * @dev Determine tier from HEAT amount
     * @param heatAmount Amount of HEAT in atomic units
     * @return tier Tier index (0, 1, 2, or 3)
     */
    function getTierFromHEATAmount(uint256 heatAmount)
        public
        pure
        returns (uint8 tier)
    {
        if (heatAmount >= TIER3_HEAT_THRESHOLD) {
            return 3;  // ≥ 8B HEAT (800 XFG) → Tier 3
        } else if (heatAmount >= TIER2_HEAT_THRESHOLD) {
            return 2;  // 800M - 7.99B HEAT (80 XFG) → Tier 2
        } else if (heatAmount >= TIER1_HEAT_THRESHOLD) {
            return 1;  // 80M - 799M HEAT (8 XFG) → Tier 1
        } else {
            return 0;  // 8M - 79M HEAT (0.8 XFG) → Tier 0
        }
    }

    /**
     * @dev Get LP position details
     * @param lpHolder Address of LP holder
     * @return position LP position struct
     */
    function getLPPosition(address lpHolder)
        external
        view
        returns (LPPosition memory position)
    {
        return lpPositions[lpHolder];
    }

    /**
     * @dev Calculate pending rewards for LP position
     * @param lpHolder Address of LP holder
     * @return pendingRewards Pending CD rewards
     */
    function getPendingRewards(address lpHolder)
        external
        view
        returns (uint256 pendingRewards)
    {
        LPPosition memory position = lpPositions[lpHolder];
        if (!position.active) return 0;

        uint256 heatAmount = getHEATFromLPTokens(position.lpTokenAmount);
        uint256 effectiveAPY = calculateTimeWeightedAPY(lpHolder);
        uint256 duration = block.timestamp - position.lastClaimTime;

        pendingRewards = (heatAmount * effectiveAPY * duration) /
                        (YEAR_IN_SECONDS * 10000);

        return pendingRewards;
    }

    /**
     * @dev Get total rewards earned for LP position (claimed + pending)
     * @param lpHolder Address of LP holder
     * @return totalRewards Total CD rewards (claimed + pending)
     */
    function getTotalRewards(address lpHolder)
        external
        view
        returns (uint256 totalRewards)
    {
        LPPosition memory position = lpPositions[lpHolder];
        if (!position.active) return 0;

        uint256 heatAmount = getHEATFromLPTokens(position.lpTokenAmount);
        uint256 effectiveAPY = calculateTimeWeightedAPY(lpHolder);
        uint256 duration = block.timestamp - position.startTime;

        totalRewards = (heatAmount * effectiveAPY * duration) /
                      (YEAR_IN_SECONDS * 10000);

        return totalRewards;
    }

    /* -------------------------------------------------------------------------- */
    /*                          Admin Functions                                   */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Update LP token address (only owner)
     * @param newLPToken New LP token address
     */
    function updateLPToken(address newLPToken) external onlyOwner {
        require(newLPToken != address(0), "Invalid LP token address");
        address oldLPToken = lpToken;
        lpToken = newLPToken;
        emit LPTokenUpdated(oldLPToken, newLPToken);
    }

    /**
     * @dev Update edition ID for CD token minting (only owner)
     * @param newEditionId New edition ID
     */
    function updateEditionId(uint256 newEditionId) external onlyOwner {
        currentEditionId = newEditionId;
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
