#include "SwapDaemon/PriceOracle.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace XfgSwap;

int main() {
  double arb = PriceOracle::getSeedRate(SwapPair::ARB);
  double eth = PriceOracle::getSeedRate(SwapPair::ETH);
  double base = PriceOracle::getSeedRate(SwapPair::BASE);

  // Current seed economics are 1 XFG = $0.158, so ETH-family rates are
  // roughly 13.5k XFG/ETH (the old >100k assertion predated that seed).
  assert(arb > 10000.0 && "ARB seed rate must be non-trivial");
  assert(std::fabs(arb - eth) < 1.0 && "ARB seed rate must equal ETH");

  assert(base > 10000.0 && "BASE seed rate must be non-trivial");
  assert(std::fabs(base - eth) < 1.0 && "BASE seed rate must equal ETH");

  assert(PriceOracle::ctrDivisor(SwapPair::ARB) == 1e18);
  assert(PriceOracle::ctrDivisor(SwapPair::BASE) == 1e18);

  assert(PriceOracle::ctrDivisor(SwapPair::SOL) == 1e9);
  assert(PriceOracle::ctrDivisor(SwapPair::ETH) == 1e18);
  assert(PriceOracle::ctrDivisor(SwapPair::XMR) == 1e12);
  assert(PriceOracle::ctrDivisor(SwapPair::BCH) == 1e8);
  assert(PriceOracle::ctrDivisor(SwapPair::BNB) == 1e18);
  assert(PriceOracle::ctrDivisor(SwapPair::DCR) == 1e8);

  std::cout << "test_price_oracle_arb PASS\n";
  return 0;
}
