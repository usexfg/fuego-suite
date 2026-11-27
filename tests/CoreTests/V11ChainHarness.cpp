// v11_chain_harness: drives the production CryptoNote::core in-process on a
// testnet currency. Each invocation is one process, so "restart" is a real
// process restart (globals re-initialised), exactly as for the daemon.
// The chains carry coinbases only: PR #83 seeds the Hearth pool at genesis, so
// epoch work, TWAP and the banking, commitment and alias indices all advance
// without a wallet.
#include "CryptoNoteCore/Core.h"
#include "CryptoNoteCore/CoreConfig.h"
#include "CryptoNoteCore/MinerConfig.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/Account.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "CryptoNoteCore/Blockchain.h"
#include "Logging/LoggerGroup.h"
#include "Logging/ConsoleLogger.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <deque>
#include <algorithm>
#include <type_traits>
#include "Treasury/VaultTypes.h"

using namespace CryptoNote;

namespace {
template <class T> struct IsPair : std::false_type {};
template <class A, class B> struct IsPair<std::pair<A, B>> : std::true_type {};

template <class T> struct IsSequence : std::false_type {};
template <class T, class A> struct IsSequence<std::vector<T, A>> : std::true_type {};
template <class T, class A> struct IsSequence<std::deque<T, A>> : std::true_type {};

// Arithmetic and key types by raw bytes; containers element by element;
// everything else through its own serialize(), which never reads padding.
template <class T> std::string serializedHex(const T& value) {
  using U = std::decay_t<T>;
  if constexpr (std::is_arithmetic<U>::value) {
    return Common::podToHex(value);
  } else if constexpr (std::is_same<U, Crypto::Hash>::value || std::is_same<U, Crypto::KeyImage>::value ||
                       std::is_same<U, Crypto::PublicKey>::value) {
    return Common::podToHex(value);
  } else if constexpr (IsPair<U>::value) {
    return serializedHex(value.first) + "," + serializedHex(value.second);
  } else if constexpr (IsSequence<U>::value) {
    std::string all = "[";
    for (const auto& e : value) all += serializedHex(e) + "|";
    return all + "]";
  } else {
    return Common::toHex(CryptoNote::toBinaryArray(value));
  }
}

template <class Entry> std::string entryString(const Entry& e) {
  if constexpr (IsPair<std::decay_t<Entry>>::value) {
    return serializedHex(e.first) + "=" + serializedHex(e.second);
  } else {
    return serializedHex(e);
  }
}

// Order-independent: entries are sorted before hashing, so two hash maps with
// the same content but different insertion history hash equal.
template <class Container> std::string unorderedDigest(const Container& c) {
  std::vector<std::string> entries;
  for (const auto& e : c) entries.push_back(entryString(e));
  std::sort(entries.begin(), entries.end());
  std::string all;
  for (const auto& e : entries) all += e + ";";
  return Common::podToHex(Crypto::cn_fast_hash(all.data(), all.size())) + " n=" + std::to_string(entries.size());
}

template <class T> std::string serializedDigest(const T& value) {
  const std::string hex = serializedHex(value);
  return Common::podToHex(Crypto::cn_fast_hash(hex.data(), hex.size())) + " len=" + std::to_string(hex.size());
}

template <class Sequence> std::string orderedDigest(const Sequence& seq) {
  std::string all;
  for (const auto& e : seq) all += serializedHex(e) + ";";
  return Common::podToHex(Crypto::cn_fast_hash(all.data(), all.size())) + " n=" + std::to_string(seq.size());
}
}  // namespace

namespace CryptoNote {
class VaultCoreTestAccess {
public:
  static void printCanonicalDigests(Blockchain& bc, std::ostream& os) {
    auto& idx = bc.m_indexManager;
    os << "digest.spentKeys=" << unorderedDigest(idx.spentKeys().data()) << "\n";
    os << "digest.keyOutputs=" << unorderedDigest(idx.outputs().data()) << "\n";
    os << "digest.multisigOutputs=" << unorderedDigest(idx.multisigOutputs().data()) << "\n";
    os << "digest.commitmentOutputs=" << unorderedDigest(idx.commitmentOutputs().data()) << "\n";
    os << "digest.swapEscrowOutputs=" << unorderedDigest(idx.swapEscrowOutputs().data()) << "\n";
    os << "digest.limitDeposits=" << unorderedDigest(bc.m_limitDeposits) << "\n";
    os << "digest.cdExpiryByEpoch=" << unorderedDigest(bc.m_cdExpiryByEpoch) << "\n";
    os << "digest.spotHistory=" << orderedDigest(bc.m_blockSpotHistory) << "\n";
    os << "digest.orderFills=" << orderedDigest(bc.m_blockOrderFills) << "\n";
    os << "digest.lpRemove=" << orderedDigest(bc.m_blockLpRemoveAmounts) << "\n";
    os << "digest.swapFeeJournal=" << orderedDigest(bc.m_blockSwapFeeContributions) << "\n";
    os << "digest.epochDistJournal=" << orderedDigest(bc.m_blockEpochDistributions) << "\n";
    std::vector<std::pair<uint64_t, uint64_t>> twapHalves;
    for (const uint128_t v : bc.m_blockTwapContributions) twapHalves.emplace_back((uint64_t)v, (uint64_t)(v >> 64));
    os << "digest.twapJournal=" << orderedDigest(twapHalves) << "\n";
    std::vector<std::pair<uint32_t, Blockchain::OrderbookRollbackSnapshot>> obSnaps(bc.m_orderbookSnapshots.begin(), bc.m_orderbookSnapshots.end());
    os << "digest.orderbookSnapshots=" << orderedDigest(obSnaps) << "\n";
    std::vector<std::pair<uint32_t, Blockchain::EpochStateSnapshot>> epochSnaps(bc.m_epochSnapshots.begin(), bc.m_epochSnapshots.end());
    os << "digest.epochSnapshots=" << orderedDigest(epochSnaps) << "\n";
    if (std::getenv("V11_DUMP")) {
      for (const auto& e : epochSnaps) os << "dump.epochSnapshot " << e.first << " " << serializedHex(e.second) << "\n";
      for (const auto& e : obSnaps) os << "dump.orderbookSnapshot " << e.first << " " << serializedHex(e.second) << "\n";
    }
    os << "digest.bankingIndex=" << serializedDigest(bc.m_bankingIndex) << "\n";
    os << "digest.commitmentIndexRoot=" << Common::podToHex(bc.m_commitmentIndex.computeMerkleRoot()) << "\n";
    std::vector<EpochFeeRateEntry> feeRates;
    for (uint64_t epoch = 0; epoch < bc.m_commitmentIndex.getEpochCount(); ++epoch)
      feeRates.push_back(bc.m_commitmentIndex.getEpochFeeRateEntry(epoch));
    os << "digest.commitmentFeeRates=" << orderedDigest(feeRates) << " commitments=" << bc.m_commitmentIndex.size() << "\n";
    os << "digest.aliasIndex=" << serializedDigest(bc.m_aliasIndex) << " size=" << bc.m_aliasIndex.size() << "\n";
    os << "digest.aliasUndoLog=" << orderedDigest(bc.m_aliasUndoLog) << "\n";
    os << "digest.vaultSpentByTx=" << unorderedDigest(bc.m_vaultSpentByTx) << "\n";
    os << "digest.hearthstoneCents=" << bc.m_fuegoCostIndex.hearthstoneCents() << "\n";
    os << "vault";
    for (auto partition : {VaultPartition::CD_APY_POOL, VaultPartition::LP_RESERVE, VaultPartition::GENERAL_RESERVE,
                           VaultPartition::SWF, VaultPartition::BONUS_VAULT}) {
      os << " " << vaultPartitionName(partition) << "=" << bc.m_vault.partitionBalance(partition, AssetType::XFG)
         << "/" << bc.m_vault.partitionBalance(partition, AssetType::HEAT);
    }
    os << "\n";
  }

  static size_t alternativeChainEntries(Blockchain& bc) { return bc.m_alternative_chains.size(); }

  static void printState(Blockchain& bc, std::ostream& os) {
    os << "height=" << bc.getCurrentBlockchainHeight() << "\n";
    os << "tail=" << Common::podToHex(bc.getTailId()) << "\n";
    os << "pool.reserveXfg=" << bc.m_ammPool.reserveXfg << " reserveHeat=" << bc.m_ammPool.reserveHeat
       << " totalLpShares=" << bc.m_ammPool.totalLpShares << " pendingXfg=" << bc.m_ammPool.pendingXfg
       << " pendingHeat=" << bc.m_ammPool.pendingHeat << " cdAcc=" << bc.m_ammPool.cdHearthFeeAccumulator << "\n";
    os << "twap.acc.lo=" << (uint64_t)bc.m_twapAccumulator << " twap.acc.hi=" << (uint64_t)(bc.m_twapAccumulator >> 64)
       << " twap.count=" << bc.m_twapBlockCount << " spotHistory=" << bc.m_blockSpotHistory.size() << "\n";
    os << "orderbook.pclear=" << bc.getOrderbookClearingPrice() << " bootstrap=" << bc.isOrderbookInBootstrap()
       << " bootstrapLeft=" << bc.orderbookBootstrapBlocksRemaining() << " matches=" << bc.getOrderbookNumMatches() << "\n";
    os << "journals.orderFills=" << bc.m_blockOrderFills.size() << " swapFee=" << bc.m_blockSwapFeeContributions.size()
       << " epochDist=" << bc.m_blockEpochDistributions.size() << " twap=" << bc.m_blockTwapContributions.size()
       << " obSnaps=" << bc.m_orderbookSnapshots.size() << " epochSnaps=" << bc.m_epochSnapshots.size()
       << " lpRemove=" << bc.m_blockLpRemoveAmounts.size() << "\n";
    os << "heatSupply=" << bc.m_heatSupply << " treasuryLpPendingXfg=" << bc.m_treasuryLpPendingXfg
       << " treasuryHeatReserve=" << bc.m_treasuryHeatReserve << " protocolLpShares=" << bc.m_protocolLpShares
       << " vaultCounter=" << bc.m_vaultUtxoCounter << "\n";
    printCanonicalDigests(bc, os);
    os << "epochSwapFees=" << bc.m_currentEpochSwapFees << " totalSwapFees=" << bc.m_totalSwapFeesCollected
       << " feePool=" << bc.m_feePoolBalance << " bonusVault=" << bc.m_bonusVaultBalance << "\n";
  }
};
}  // namespace CryptoNote

namespace {

struct Node {
  Logging::ConsoleLogger console{static_cast<Logging::Level>(std::getenv("V11_LOG") ? std::atoi(std::getenv("V11_LOG")) : Logging::WARNING)};
  Logging::LoggerGroup log;
  Currency currency;
  core node;
  std::string dir;

  explicit Node(const std::string& directory)
      : currency((log.addLogger(console), CurrencyBuilder(log).testnet(true).currency())),
        node(currency, nullptr, log, false, false),
        dir(directory) {
    CoreConfig config;
    config.configFolder = directory;
    config.configFolderDefaulted = false;
    MinerConfig minerConfig;
    if (!node.init(config, minerConfig, true)) throw std::runtime_error("core init failed");
  }
  ~Node() { node.deinit(); }
  Blockchain& chain() { return node.get_blockchain_storage(); }
  uint32_t height() { return chain().getCurrentBlockchainHeight(); }
};

Crypto::SecretKey deterministicSecret(const std::string& label) {
  Crypto::SecretKey secret;
  Crypto::hash_to_scalar(label.data(), label.size(), secret);
  return secret;
}

AccountBase minerAccount(uint8_t seed) {
  AccountBase account;
  AccountKeys keys;
  keys.spendSecretKey = deterministicSecret("v11chain.spend." + std::to_string(seed));
  keys.viewSecretKey = deterministicSecret("v11chain.view." + std::to_string(seed));
  Crypto::secret_key_to_public_key(keys.spendSecretKey, keys.address.spendPublicKey);
  Crypto::secret_key_to_public_key(keys.viewSecretKey, keys.address.viewPublicKey);
  account.setAccountKeys(keys);
  return account;
}

void adjustMergeMiningTag(Block& block) {
  if (block.majorVersion >= BLOCK_MAJOR_VERSION_2) {
    TransactionExtraMergeMiningTag tag;
    tag.depth = 0;
    if (!get_aux_block_header_hash(block, tag.merkleRoot)) throw std::runtime_error("aux header hash");
    block.parentBlock.baseTransaction.extra.clear();
    if (!appendMergeMiningTagToExtra(block.parentBlock.baseTransaction.extra, tag))
      throw std::runtime_error("mm tag");
  }
}

// Pre-v10 difficulty functions reach 0 when blocks are much slower than the
// testnet target, so early blocks keep the 10 s target. From v10 LWMA clamps at
// 1, and blocks space at the mainnet 480 s the alt-chain Poisson check assumes.
uint64_t blockTimestamp(const Currency& currency, uint32_t height) {
  const uint32_t fastUntil = 30;
  const uint64_t genesis = currency.genesisBlock().timestamp;
  if (height <= fastUntil) return genesis + uint64_t(height) * currency.difficultyTarget();
  return genesis + uint64_t(fastUntil) * currency.difficultyTarget() +
         uint64_t(height - fastUntil) * parameters::DIFFICULTY_TARGET;
}

using BlockMutator = std::function<void(Block&, uint32_t height)>;

// Template → (optional mutation) → merge-mining tag → PoW → handle_block_found,
// the same sequence the built-in miner uses. Timestamps advance by exactly the
// difficulty target so LWMA keeps difficulty at its floor.
bool mineBlock(Node& n, const AccountBase& miner, uint64_t timestampSalt, const BlockMutator& mutate = {}) {
  Block b;
  difficulty_type diff;
  uint32_t height;
  BinaryArray exNonce;
  if (!n.node.get_block_template(b, miner.getAccountKeys().address, diff, height, exNonce)) return false;
  b.timestamp = blockTimestamp(n.currency, height) + timestampSalt;
  if (mutate) mutate(b, height);
  adjustMergeMiningTag(b);
  Crypto::cn_context ctx;
  Crypto::Hash pow;
  while (!n.currency.checkProofOfWork(ctx, b, diff, pow)) {
    ++b.nonce;
    if (b.nonce == 0) return false;
  }
  return n.node.handle_block_found(b);
}

void writeU32(std::ostream& os, uint32_t v) { os.write(reinterpret_cast<const char*>(&v), 4); }
bool readU32(std::istream& is, uint32_t& v) { return bool(is.read(reinterpret_cast<char*>(&v), 4)); }

void exportBlocks(Node& n, uint32_t from, uint32_t to, const std::string& file) {
  std::ofstream os(file, std::ios::binary | std::ios::trunc);
  for (uint32_t h = from; h <= to; ++h) {
    Block b;
    if (!n.chain().getBlockByHash(n.chain().getBlockIdByHeight(h), b)) throw std::runtime_error("export block");
    std::vector<Transaction> txs;
    std::list<Crypto::Hash> missed;
    n.chain().getTransactions(b.transactionHashes, txs, missed);
    if (!missed.empty()) throw std::runtime_error("export missed tx");
    BinaryArray blob = toBinaryArray(b);
    writeU32(os, h);
    writeU32(os, static_cast<uint32_t>(blob.size()));
    os.write(reinterpret_cast<const char*>(blob.data()), blob.size());
    writeU32(os, static_cast<uint32_t>(txs.size()));
    for (const auto& tx : txs) {
      BinaryArray txBlob = toBinaryArray(tx);
      writeU32(os, static_cast<uint32_t>(txBlob.size()));
      os.write(reinterpret_cast<const char*>(txBlob.data()), txBlob.size());
    }
  }
}

// Feeds blocks the way the protocol handler does for a relayed block: its
// transactions first (kept_by_block), then the block blob.
int importBlocks(Node& n, const std::string& file, uint32_t from, uint32_t to, int& chainSwitches, size_t& peakAlternativeEntries) {
  std::ifstream is(file, std::ios::binary);
  uint32_t h, size, ntx;
  int imported = 0;
  while (readU32(is, h) && readU32(is, size)) {
    BinaryArray blob(size);
    is.read(reinterpret_cast<char*>(blob.data()), size);
    readU32(is, ntx);
    std::vector<BinaryArray> txBlobs;
    for (uint32_t i = 0; i < ntx; ++i) {
      uint32_t ts;
      readU32(is, ts);
      BinaryArray tb(ts);
      is.read(reinterpret_cast<char*>(tb.data()), ts);
      txBlobs.push_back(tb);
    }
    if (h < from || h > to) continue;
    for (const auto& tb : txBlobs) {
      tx_verification_context tvc = boost::value_initialized<tx_verification_context>();
      n.node.handle_incoming_tx(tb, tvc, true);
    }
    block_verification_context bvc = boost::value_initialized<block_verification_context>();
    n.node.handle_incoming_block_blob(blob, bvc, false, false);
    if (bvc.m_verification_failed) {
      std::cerr << "import: block " << h << " failed verification\n";
      return -1;
    }
    if (bvc.m_switched_to_alt_chain) ++chainSwitches;
    peakAlternativeEntries = std::max(peakAlternativeEntries, VaultCoreTestAccess::alternativeChainEntries(n.chain()));
    ++imported;
  }
  return imported;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: v11chain <cmd> <dir> ...\n";
    return 2;
  }
  const std::string cmd = argv[1];
  const std::string dir = argv[2];
  try {
    if (cmd == "mine") {
      // mine <dir> <toHeight> <minerSeed> <timestampSalt>
      if (argc != 6) { std::cerr << "usage: mine <dir> <toHeight> <minerSeed> <timestampSalt>\n"; return 2; }
      const uint32_t toHeight = std::stoul(argv[3]);
      const AccountBase miner = minerAccount(static_cast<uint8_t>(std::stoul(argv[4])));
      const uint64_t salt = std::stoull(argv[5]);
      Node n(dir);
      while (n.height() <= toHeight) {
        if (!mineBlock(n, miner, salt)) {
          std::cerr << "mine: block at height " << n.height() << " rejected\n";
          return 1;
        }
      }
      std::cout << "mined to height " << n.height() - 1 << "\n";
    } else if (cmd == "export") {
      // export <dir> <from> <to> <file>
      if (argc != 6) { std::cerr << "usage: export <dir> <from> <to> <file>\n"; return 2; }
      Node n(dir);
      exportBlocks(n, std::stoul(argv[3]), std::stoul(argv[4]), argv[5]);
    } else if (cmd == "import" || cmd == "state") {
      // import <dir> (<file> <from> <to>)+   feeds the segments in order within one process
      // state <dir> [(<file> <from> <to>)+]  does the same if given segments, then prints the
      //                                      consensus state of that live process (the import
      //                                      report goes to stderr)
      const bool printsState = cmd == "state";
      const int segmentArgs = argc - 3;
      if (segmentArgs % 3 != 0 || (!printsState && segmentArgs == 0)) {
        std::cerr << "usage: import <dir> (<file> <from> <to>)+ | state <dir> [(<file> <from> <to>)+]\n";
        return 2;
      }
      Node n(dir);
      int total = 0, chainSwitches = 0;
      size_t peakAlternativeEntries = 0;
      for (int i = 3; i < argc; i += 3) {
        const int imported = importBlocks(n, argv[i], std::stoul(argv[i + 1]), std::stoul(argv[i + 2]), chainSwitches,
                                          peakAlternativeEntries);
        if (imported < 0) return 1;
        total += imported;
      }
      std::ostream& report = printsState ? std::cerr : std::cout;
      if (segmentArgs > 0)
        report << "imported " << total << " blocks, height " << n.height() - 1 << ", chain switches " << chainSwitches
               << ", peak alternative chain entries " << peakAlternativeEntries << "\n";
      if (printsState) VaultCoreTestAccess::printState(n.chain(), std::cout);
    } else {
      std::cerr << "unknown command\n";
      return 2;
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
