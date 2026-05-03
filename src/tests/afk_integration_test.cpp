#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "--- Testing AFK Swap Legitimacy Logic ---" << std::endl;

    // Constants (simulating CryptoNote::parameters)
    const uint64_t SWAP_FEE_RATE_BPS = 100;
    const uint64_t SWAP_FEE_RATE_DIVISOR = 10000;
    const uint64_t MIN_FEE = 80000; // 0.008 XFG

    // Test Case: 100 XFG Offer
    uint64_t baseAmount = 100 * 10000000; // 100 XFG
    
    // 1. Maker's Lock Calculation
    uint64_t feeBob = (baseAmount * SWAP_FEE_RATE_BPS) / SWAP_FEE_RATE_DIVISOR;
    uint64_t totalLocked = baseAmount + feeBob;
    
    std::cout << "Maker Locks: " << totalLocked / 1e7 << " XFG (Base + 1% fee)" << std::endl;
    
    // 2. Taker's Payout Calculation (Net 99%)
    // We want Alice to receive exactly 99% after paying the miner fee.
    uint64_t takerNet = baseAmount - (baseAmount * SWAP_FEE_RATE_BPS / SWAP_FEE_RATE_DIVISOR);
    uint64_t takerGross = takerNet + MIN_FEE;
    
    // 3. Protocol Fee Calculation
    // Protocol gets the remainder: TotalLocked - TakerGross - MinerFee
    uint64_t protocolFee = totalLocked - takerGross - MIN_FEE;

    std::cout << "Taker Gross Receipt: " << takerGross / 1e7 << " XFG" << std::endl;
    std::cout << "Taker Net (after paying miner fee): " << (takerGross - MIN_FEE) / 1e7 << " XFG" << std::endl;
    std::cout << "Protocol Fee: " << protocolFee / 1e7 << " XFG" << std::endl;

    // Assertions
    assert(totalLocked == 101000000);
    assert(takerGross - MIN_FEE == 99000000); // Exactly 99%
    assert(protocolFee == 1984000); // 101M - 99.008M - 0.008M = 1.984M

    std::cout << "SUCCESS: Net 99% logic verified. Bob's fee is locked, not sunk." << std::endl;
    return 0;
}
