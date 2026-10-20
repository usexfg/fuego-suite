#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace CryptoNote {

// On-chain consumer-price index built from miner basket votes.
//
// Every price in this class is an integer number of cents. Consensus never
// touches floating point; the miner converts from its local currency and
// rounds to the nearest cent before the vote is sealed into the coinbase.
class FuegoCostIndex {
public:
  static constexpr size_t COMMODITY_COUNT = 6;

  struct Snapshot {
    std::array<uint32_t, COMMODITY_COUNT> medianPrices;
    // Per-category last-quorum epochs travel with the snapshot so a reorg
    // rewinds the quarterly gate along with the medians. Replaying an epoch
    // boundary against a stale quorum history would otherwise let the
    // reference move on categories that never cleared quorum again.
    std::array<uint64_t, COMMODITY_COUNT> lastQuorumEpochs;
    uint64_t indexCents;
    uint64_t hearthstoneCents;
  };

  struct EpochTally {
    std::array<uint32_t, COMMODITY_COUNT> voteCounts;
    std::array<uint32_t, COMMODITY_COUNT> medianPrices;
    uint64_t indexCents;
    uint64_t hearthstoneCents;
    bool referenceUpdated;
  };

  FuegoCostIndex();

  void reset();
  Snapshot snapshot() const;
  void restore(const Snapshot& state);
  bool addCoinbaseVote(const std::vector<uint8_t>& extra);
  bool removeCoinbaseVote(const std::vector<uint8_t>& extra);
  EpochTally closeEpoch(uint64_t completedEpochs, uint8_t blockMajorVersion,
                        uint32_t epochBlocks);

  uint64_t indexCents() const;
  uint64_t hearthstoneCents() const;

  static uint32_t minimumQuorumForEpoch(uint32_t epochBlocks);

private:
  using BasketValues = std::array<uint32_t, COMMODITY_COUNT>;
  using EpochVotes = std::array<std::vector<uint32_t>, COMMODITY_COUNT>;

  static bool decodeVote(const std::vector<uint8_t>& extra,
                         uint8_t& presentMask, BasketValues& values);
  static uint32_t trimmedMedian(std::vector<uint32_t> values);
  uint64_t calculateIndexCents() const;
  size_t freshQuorumsInQuarter(uint64_t completedEpochs) const;
  void clearEpochVotes();

  BasketValues m_medianPrices;
  EpochVotes m_epochVotes;
  // Last completed epoch in which each category cleared quorum. Zero means
  // the category has never cleared quorum.
  std::array<uint64_t, COMMODITY_COUNT> m_lastQuorumEpochs;
  uint64_t m_indexCents;
  uint64_t m_hearthstoneCents;
};

}
