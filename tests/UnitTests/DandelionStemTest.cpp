// Copyright (c) 2017-2026 Fuego Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Regression coverage for Dandelion++ stem relay routing.
//
// The hop counter arrives on the wire from an untrusted peer. The original
// code did a bare `arg.hop_count++`, so a peer sending UINT32_MAX wrapped the
// counter to 0 on the first hop. Because 0 is below every max_hops, that handed
// the sender a fresh stem budget on every pass and the hop bound stopped being a
// bound. dandelionAdvanceHop() saturates instead; these tests pin that.

#include "gtest/gtest.h"
#include "CryptoNoteConfig.h"

#include <cstdint>
#include <limits>

using namespace CryptoNote;

namespace {

using parameters::DandelionKind;
using parameters::dandelionAdvanceHop;
using parameters::dandelionShouldStayInStem;

constexpr uint32_t kUint32Max = (std::numeric_limits<uint32_t>::max)();

// ── dandelionAdvanceHop: ordinary progression ────────────────────────────────

TEST(DandelionStem, advanceHopIncrementsWithinBudget) {
  EXPECT_EQ(dandelionAdvanceHop(0, DandelionKind::Tx), 1u);
  EXPECT_EQ(dandelionAdvanceHop(4, DandelionKind::Tx), 5u);
  EXPECT_EQ(dandelionAdvanceHop(0, DandelionKind::SwapOffer), 1u);
  EXPECT_EQ(dandelionAdvanceHop(3, DandelionKind::SwapOffer), 4u);
}

// Advancing to exactly max_hops is allowed — that is the last stem hop. The
// stay-decision, not the advance, is what forces fluff at the boundary.
TEST(DandelionStem, advanceHopReachesExactlyMaxHops) {
  EXPECT_EQ(dandelionAdvanceHop(9, DandelionKind::Tx),
            parameters::DANDELION_STEM_MAX_HOPS);
  EXPECT_EQ(dandelionAdvanceHop(4, DandelionKind::SwapOffer),
            parameters::DANDELION_SWAP_STEM_MAX_HOPS);
}

// ── dandelionAdvanceHop: saturation ──────────────────────────────────────────

TEST(DandelionStem, advanceHopSaturatesAtMaxHops) {
  // Already at or past the budget: must not run past it.
  EXPECT_EQ(dandelionAdvanceHop(parameters::DANDELION_STEM_MAX_HOPS, DandelionKind::Tx),
            parameters::DANDELION_STEM_MAX_HOPS);
  EXPECT_EQ(dandelionAdvanceHop(500, DandelionKind::Tx),
            parameters::DANDELION_STEM_MAX_HOPS);
  EXPECT_EQ(dandelionAdvanceHop(parameters::DANDELION_SWAP_STEM_MAX_HOPS, DandelionKind::SwapOffer),
            parameters::DANDELION_SWAP_STEM_MAX_HOPS);
}

// THE REGRESSION. A peer-supplied UINT32_MAX must saturate, not wrap to 0.
// Under the old `hop_count++` this produced 0, which is below max_hops and so
// granted an unlimited stem budget.
TEST(DandelionStem, advanceHopDoesNotWrapOnMaxUint32) {
  EXPECT_EQ(dandelionAdvanceHop(kUint32Max, DandelionKind::Tx),
            parameters::DANDELION_STEM_MAX_HOPS);
  EXPECT_EQ(dandelionAdvanceHop(kUint32Max, DandelionKind::SwapOffer),
            parameters::DANDELION_SWAP_STEM_MAX_HOPS);

  // Belt and braces: assert it explicitly did not become 0.
  EXPECT_NE(dandelionAdvanceHop(kUint32Max, DandelionKind::Tx), 0u);
  EXPECT_NE(dandelionAdvanceHop(kUint32Max, DandelionKind::SwapOffer), 0u);
}

// ── the invariant that actually matters ──────────────────────────────────────

// Whatever the peer sent, after advancing, the node must not be able to stay in
// stem indefinitely. rand is forced to 0 (best case for staying in stem), so a
// failure here can only come from the hop bound being bypassed.
TEST(DandelionStem, saturatedHopCannotStayInStem) {
  const uint32_t hostile[] = {
    kUint32Max,
    kUint32Max - 1,
    kUint32Max / 2,
    1000,
    100,
  };

  for (uint32_t wire : hostile) {
    for (DandelionKind kind : {DandelionKind::Tx, DandelionKind::SwapOffer}) {
      const uint32_t hop = dandelionAdvanceHop(wire, kind);
      EXPECT_FALSE(dandelionShouldStayInStem(hop, kind, 0))
          << "wire hop_count " << wire << " stayed in stem after advancing to " << hop;
    }
  }
}

// A peer that keeps re-sending the same message cannot reset its own budget:
// once the counter is at max_hops it stays there.
TEST(DandelionStem, repeatedAdvanceConvergesAndStaysFlocked) {
  uint32_t hop = 0;
  for (int i = 0; i < 100; ++i) {
    hop = dandelionAdvanceHop(hop, DandelionKind::SwapOffer);
  }
  EXPECT_EQ(hop, parameters::DANDELION_SWAP_STEM_MAX_HOPS);
  EXPECT_FALSE(dandelionShouldStayInStem(hop, DandelionKind::SwapOffer, 0));
}

// ── dandelionShouldStayInStem: boundary behaviour ────────────────────────────

TEST(DandelionStem, shouldStayRespectsHopBudget) {
  const uint32_t max_hops = parameters::DANDELION_STEM_MAX_HOPS;
  // Inside the budget, with the most favourable roll.
  EXPECT_TRUE(dandelionShouldStayInStem(0, DandelionKind::Tx, 0));
  EXPECT_TRUE(dandelionShouldStayInStem(max_hops - 1, DandelionKind::Tx, 0));
  // At the budget: no.
  EXPECT_FALSE(dandelionShouldStayInStem(max_hops, DandelionKind::Tx, 0));
  EXPECT_FALSE(dandelionShouldStayInStem(max_hops + 1, DandelionKind::Tx, 0));
}

TEST(DandelionStem, shouldStayRespectsStayPercentage) {
  const uint32_t pct = parameters::DANDELION_STEM_STAY_PCT;
  EXPECT_TRUE(dandelionShouldStayInStem(0, DandelionKind::Tx, pct - 1));
  EXPECT_FALSE(dandelionShouldStayInStem(0, DandelionKind::Tx, pct));

  const uint32_t swap_pct = parameters::DANDELION_SWAP_STEM_STAY_PCT;
  EXPECT_TRUE(dandelionShouldStayInStem(0, DandelionKind::SwapOffer, swap_pct - 1));
  EXPECT_FALSE(dandelionShouldStayInStem(0, DandelionKind::SwapOffer, swap_pct));
}

// The two relay paths must keep distinct tuning, or a change to one silently
// re-tunes the other.
TEST(DandelionStem, txAndSwapPathsAreIndependentlyTuned) {
  EXPECT_NE(parameters::DANDELION_STEM_MAX_HOPS,
            parameters::DANDELION_SWAP_STEM_MAX_HOPS);
  EXPECT_NE(parameters::DANDELION_STEM_STAY_PCT,
            parameters::DANDELION_SWAP_STEM_STAY_PCT);

  // A roll that keeps a tx in stem must not keep a swap offer in stem, since
  // the swap path is the stricter of the two.
  const uint32_t roll = parameters::DANDELION_SWAP_STEM_STAY_PCT;
  EXPECT_TRUE(dandelionShouldStayInStem(0, DandelionKind::Tx, roll));
  EXPECT_FALSE(dandelionShouldStayInStem(0, DandelionKind::SwapOffer, roll));
}

// Stay percentages must stay strictly below 100, or a message could never
// leave the stem. (Mirrors the static_asserts in CryptoNoteConfig.h, but
// asserts the values actually loaded at runtime.)
TEST(DandelionStem, stayPercentagesAllowEventualFluff) {
  EXPECT_LT(parameters::DANDELION_STEM_STAY_PCT, 100u);
  EXPECT_LT(parameters::DANDELION_SWAP_STEM_STAY_PCT, 100u);
  EXPECT_GT(parameters::DANDELION_STEM_MAX_HOPS, 0u);
  EXPECT_GT(parameters::DANDELION_SWAP_STEM_MAX_HOPS, 0u);
}

} // namespace
