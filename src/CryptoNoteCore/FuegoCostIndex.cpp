#include "FuegoCostIndex.h"

#include <algorithm>
#include <limits>
#include <typeinfo>

#include "Common/Int128.h"
#include "CryptoNoteConfig.h"
#include "TransactionExtra.h"

namespace CryptoNote {
namespace {

constexpr std::array<uint32_t, FuegoCostIndex::COMMODITY_COUNT> BASELINES = {
    parameters::FCI_BASELINE_POWER,
    parameters::FCI_BASELINE_MILK,
    parameters::FCI_BASELINE_BREAD,
    parameters::FCI_BASELINE_EGGS,
    parameters::FCI_BASELINE_GAS,
    parameters::FCI_BASELINE_RICE};

constexpr std::array<uint32_t, FuegoCostIndex::COMMODITY_COUNT> WEIGHTS = {
    parameters::FCI_WEIGHT_POWER,
    parameters::FCI_WEIGHT_MILK,
    parameters::FCI_WEIGHT_BREAD,
    parameters::FCI_WEIGHT_EGGS,
    parameters::FCI_WEIGHT_GAS,
    parameters::FCI_WEIGHT_RICE};

constexpr uint8_t ALL_COMMODITIES_MASK =
    (1u << FuegoCostIndex::COMMODITY_COUNT) - 1u;

static_assert(parameters::FCI_WEIGHT_POWER + parameters::FCI_WEIGHT_GAS +
                  parameters::FCI_WEIGHT_RICE + parameters::FCI_WEIGHT_MILK +
                  parameters::FCI_WEIGHT_BREAD + parameters::FCI_WEIGHT_EGGS == 100,
              "FCI weights must total 100");
static_assert(parameters::FCI_EPOCHS_PER_QUARTER > 0,
              "FCI quarter must include at least one epoch");
static_assert(parameters::FCI_MIN_FRESH_QUORUM > 0 &&
                  parameters::FCI_MIN_FRESH_QUORUM <= FuegoCostIndex::COMMODITY_COUNT,
              "FCI fresh-quorum threshold must be within the basket size");
static_assert(parameters::FCI_MIN_FRESH_QUORUM < FuegoCostIndex::COMMODITY_COUNT,
              "A unanimous fresh-quorum gate reintroduces the all_of rule");

}

FuegoCostIndex::FuegoCostIndex() {
  reset();
}

void FuegoCostIndex::reset() {
  m_medianPrices = BASELINES;
  m_lastQuorumEpochs.fill(0);
  m_indexCents = parameters::FCI_INDEX_SCALE;
  m_hearthstoneCents = parameters::FCI_HEARTHSTONE_INITIAL_CENTS;
  clearEpochVotes();
}

FuegoCostIndex::Snapshot FuegoCostIndex::snapshot() const {
  return {m_medianPrices, m_lastQuorumEpochs, m_indexCents, m_hearthstoneCents};
}

void FuegoCostIndex::restore(const Snapshot& state) {
  m_medianPrices = state.medianPrices;
  m_lastQuorumEpochs = state.lastQuorumEpochs;
  m_indexCents = state.indexCents;
  m_hearthstoneCents = state.hearthstoneCents;
  clearEpochVotes();
}

bool FuegoCostIndex::decodeVote(const std::vector<uint8_t>& extra,
                                uint8_t& presentMask, BasketValues& values) {
  std::vector<TransactionExtraField> fields;
  if (!parseTransactionExtra(extra, fields)) return false;

  const TransactionExtraMinerBasketVote* basket = nullptr;
  for (const auto& field : fields) {
    if (field.type() != typeid(TransactionExtraMinerBasketVote)) continue;
    if (basket != nullptr) return false;
    basket = &boost::get<TransactionExtraMinerBasketVote>(field);
  }
  if (basket == nullptr || basket->present_mask == 0 ||
      (basket->present_mask & ~ALL_COMMODITIES_MASK) != 0) {
    return false;
  }

  presentMask = basket->present_mask;
  values = {basket->power, basket->milk, basket->bread, basket->eggs,
            basket->gas, basket->rice};
  for (size_t i = 0; i < COMMODITY_COUNT; ++i) {
    if ((presentMask & (1u << i)) != 0 && values[i] == 0) return false;
  }
  return true;
}

bool FuegoCostIndex::addCoinbaseVote(const std::vector<uint8_t>& extra) {
  uint8_t presentMask = 0;
  BasketValues values{};
  if (!decodeVote(extra, presentMask, values)) return false;

  for (size_t i = 0; i < COMMODITY_COUNT; ++i) {
    if ((presentMask & (1u << i)) != 0) m_epochVotes[i].push_back(values[i]);
  }
  return true;
}

bool FuegoCostIndex::removeCoinbaseVote(const std::vector<uint8_t>& extra) {
  uint8_t presentMask = 0;
  BasketValues values{};
  if (!decodeVote(extra, presentMask, values)) return false;

  for (size_t i = 0; i < COMMODITY_COUNT; ++i) {
    if ((presentMask & (1u << i)) == 0) continue;
    if (m_epochVotes[i].empty() || m_epochVotes[i].back() != values[i])
      return false;
  }
  for (size_t i = 0; i < COMMODITY_COUNT; ++i) {
    if ((presentMask & (1u << i)) != 0) m_epochVotes[i].pop_back();
  }
  return true;
}

uint32_t FuegoCostIndex::trimmedMedian(std::vector<uint32_t> values) {
  std::sort(values.begin(), values.end());
  const size_t trimCount = values.size() * parameters::FCI_TRIM_PCT / 100;
  const size_t first = trimCount;
  const size_t last = values.size() - trimCount;
  const size_t count = last - first;
  const size_t middle = first + count / 2;
  if ((count & 1u) != 0) return values[middle];
  return values[middle - 1] + (values[middle] - values[middle - 1]) / 2;
}

uint64_t FuegoCostIndex::calculateIndexCents() const {
  uint128_t weightedIndex = 0;
  for (size_t i = 0; i < COMMODITY_COUNT; ++i) {
    weightedIndex += (uint128_t(m_medianPrices[i]) * WEIGHTS[i] *
                      parameters::FCI_INDEX_SCALE) /
                     BASELINES[i];
  }
  return static_cast<uint64_t>(weightedIndex / 100);
}

size_t FuegoCostIndex::freshQuorumsInQuarter(uint64_t completedEpochs) const {
  if (completedEpochs < parameters::FCI_EPOCHS_PER_QUARTER) return 0;
  const uint64_t windowFloor =
      completedEpochs - parameters::FCI_EPOCHS_PER_QUARTER;
  return static_cast<size_t>(std::count_if(
      m_lastQuorumEpochs.begin(), m_lastQuorumEpochs.end(),
      [windowFloor](uint64_t quorumEpoch) { return quorumEpoch > windowFloor; }));
}

void FuegoCostIndex::clearEpochVotes() {
  for (auto& votes : m_epochVotes) votes.clear();
}

FuegoCostIndex::EpochTally FuegoCostIndex::closeEpoch(
    uint64_t completedEpochs, uint8_t blockMajorVersion,
    uint32_t epochBlocks) {
  EpochTally tally{};
  const uint32_t quorum = minimumQuorumForEpoch(epochBlocks);

  for (size_t i = 0; i < COMMODITY_COUNT; ++i) {
    const size_t count = m_epochVotes[i].size();
    tally.voteCounts[i] = count > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(count);
    if (count >= quorum && quorum > 0) {
      m_medianPrices[i] = trimmedMedian(m_epochVotes[i]);
      m_lastQuorumEpochs[i] = completedEpochs;
    }
  }

  m_indexCents = calculateIndexCents();
  tally.referenceUpdated = false;

  const bool quarterBoundary =
      completedEpochs > 0 &&
      completedEpochs % parameters::FCI_EPOCHS_PER_QUARTER == 0 &&
      blockMajorVersion >= BLOCK_MAJOR_VERSION_12;

  // The basket only re-bases when enough categories were actually re-priced
  // inside the trailing quarter. Categories carried forward at their previous
  // median are excluded from the count, so a thin quarter cannot drag the
  // reference on stale data.
  if (quarterBoundary &&
      freshQuorumsInQuarter(completedEpochs) >=
          parameters::FCI_MIN_FRESH_QUORUM) {
    const uint64_t targetReference = static_cast<uint64_t>(
        (uint128_t(parameters::FCI_HEARTHSTONE_INITIAL_CENTS) * m_indexCents) /
        parameters::FCI_INDEX_SCALE);
    const uint64_t maxChange = static_cast<uint64_t>(
        (uint128_t(m_hearthstoneCents) *
         parameters::FCI_MAX_QUARTERLY_DRIFT_BPS) /
        10000);
    if (targetReference > m_hearthstoneCents) {
      const uint64_t difference = targetReference - m_hearthstoneCents;
      if (difference > maxChange) {
        m_hearthstoneCents += maxChange;
        tally.referenceUpdated = maxChange > 0;
      } else {
        m_hearthstoneCents = targetReference;
        tally.referenceUpdated = difference > 0;
      }
    } else if (m_hearthstoneCents > targetReference) {
      const uint64_t difference = m_hearthstoneCents - targetReference;
      if (difference > maxChange) {
        m_hearthstoneCents -= maxChange;
        tally.referenceUpdated = maxChange > 0;
      } else {
        m_hearthstoneCents = targetReference;
        tally.referenceUpdated = difference > 0;
      }
    }
  }

  clearEpochVotes();
  tally.medianPrices = m_medianPrices;
  tally.indexCents = m_indexCents;
  tally.hearthstoneCents = m_hearthstoneCents;
  return tally;
}

uint64_t FuegoCostIndex::indexCents() const {
  return m_indexCents;
}

uint64_t FuegoCostIndex::hearthstoneCents() const {
  return m_hearthstoneCents;
}

uint32_t FuegoCostIndex::minimumQuorumForEpoch(uint32_t epochBlocks) {
  if (epochBlocks == 0) return 0;
  const uint64_t numerator = uint64_t(parameters::FCI_MIN_QUORUM) * epochBlocks;
  const uint64_t scaledQuorum =
      (numerator + parameters::FCI_EPOCH_BLOCKS - 1) /
      parameters::FCI_EPOCH_BLOCKS;
  return static_cast<uint32_t>(
      std::min<uint64_t>(epochBlocks, std::max<uint64_t>(1, scaledQuorum)));
}

}
