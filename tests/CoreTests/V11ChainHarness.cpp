// v11_chain_harness: drives the production CryptoNote::core in-process on a
// testnet currency. Each invocation is one process, so "restart" is a real
// process restart (globals re-initialised), exactly as for the daemon.
#include "CryptoNoteCore/Core.h"
#include "CryptoNoteCore/CoreConfig.h"
#include "CryptoNoteCore/MinerConfig.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/Account.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "CryptoNoteCore/Blockchain.h"
#include "CryptoNoteCore/AmmPool.h"
#include "CryptoNoteCore/TransactionApi.h"
#include "CryptoNoteProtocol/ICryptoNoteProtocolQuery.h"
#include "InProcessNode/InProcessNode.h"
#include "WalletLegacy/WalletLegacy.h"
#include "IWalletLegacy.h"
#include "Logging/LoggerGroup.h"
#include "Logging/ConsoleLogger.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <future>
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

template <class T> bool isCryptoPod() {
  return std::is_same<T, Crypto::Hash>::value || std::is_same<T, Crypto::KeyImage>::value ||
         std::is_same<T, Crypto::PublicKey>::value;
}

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
    auto current = bc.captureOrderbookState();
    os << "digest.orderbookState=" << serializedHex(current).substr(0, 64) << "... len=" << serializedHex(current).size() << "\n";
    os << "digest.bankingIndex=" << Common::podToHex(Crypto::cn_fast_hash(serializedHex(bc.m_bankingIndex).data(), serializedHex(bc.m_bankingIndex).size())) << "\n";
    os << "digest.commitmentIndexRoot=" << Common::podToHex(bc.m_commitmentIndex.computeMerkleRoot()) << "\n";
    os << "digest.hearthstoneCents=" << bc.m_fuegoCostIndex.hearthstoneCents() << "\n";
    os << "vault";
    for (auto partition : {VaultPartition::CD_APY_POOL, VaultPartition::LP_RESERVE, VaultPartition::GENERAL_RESERVE,
                           VaultPartition::SWF, VaultPartition::BONUS_VAULT}) {
      os << " " << vaultPartitionName(partition) << "=" << bc.m_vault.partitionBalance(partition, AssetType::XFG)
         << "/" << bc.m_vault.partitionBalance(partition, AssetType::HEAT);
    }
    os << "\n";
  }

  static void printState(Blockchain& bc, std::ostream& os) {
    auto ob = bc.captureOrderbookState();
    os << "height=" << bc.getCurrentBlockchainHeight() << "\n";
    os << "tail=" << Common::podToHex(bc.getTailId()) << "\n";
    os << "pool.reserveXfg=" << bc.m_ammPool.reserveXfg << " reserveHeat=" << bc.m_ammPool.reserveHeat
       << " totalLpShares=" << bc.m_ammPool.totalLpShares << " pendingXfg=" << bc.m_ammPool.pendingXfg
       << " pendingHeat=" << bc.m_ammPool.pendingHeat << " cdAcc=" << bc.m_ammPool.cdHearthFeeAccumulator << "\n";
    os << "twap.acc.lo=" << (uint64_t)bc.m_twapAccumulator << " twap.acc.hi=" << (uint64_t)(bc.m_twapAccumulator >> 64)
       << " twap.count=" << bc.m_twapBlockCount << " spotHistory=" << bc.m_blockSpotHistory.size() << "\n";
    os << "orderbook.pclear=" << ob.lastClearingPrice << " bootstrap=" << ob.isInBootstrap
       << " bootstrapLeft=" << ob.bootstrapBlocksRemaining << " sinceRegen=" << ob.blocksSinceLastPoolRegen
       << " priorRegenPclear=" << ob.priorPoolRegenPclear << " lastRegenPclear=" << ob.lastRegenPclear
       << " priceHistory=" << ob.priceHistory.size() << " poolOrders=" << ob.poolOrders.size() << "\n";
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

// The non-coinbase transactions of an exported chain, by the height that
// included them.
std::map<uint32_t, std::vector<BinaryArray>> readTransactionsByHeight(const std::string& file) {
  std::map<uint32_t, std::vector<BinaryArray>> byHeight;
  std::ifstream is(file, std::ios::binary);
  uint32_t h, size, ntx;
  while (readU32(is, h) && readU32(is, size)) {
    is.seekg(size, std::ios::cur);
    readU32(is, ntx);
    for (uint32_t i = 0; i < ntx; ++i) {
      uint32_t ts;
      readU32(is, ts);
      BinaryArray tb(ts);
      is.read(reinterpret_cast<char*>(tb.data()), ts);
      byHeight[h].push_back(std::move(tb));
    }
  }
  return byHeight;
}

// Feeds blocks the way the protocol handler does for a relayed block: its
// transactions first (kept_by_block), then the block blob.
int importBlocks(Node& n, const std::string& file, uint32_t from, uint32_t to) {
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
    ++imported;
  }
  return imported;
}

std::string cacheDigest(const std::string& dir, const Currency& currency) {
  std::ifstream is(dir + "/" + currency.blocksCacheFileName(), std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
  Crypto::Hash h = Crypto::cn_fast_hash(bytes.data(), bytes.size());
  return Common::podToHex(h) + " (" + std::to_string(bytes.size()) + " bytes)";
}

// A node with no peers: the in-process daemon is the whole network.
class LoneProtocol : public ICryptoNoteProtocolQuery {
public:
  explicit LoneProtocol(core& node) : m_core(node) {}
  bool addObserver(ICryptoNoteProtocolObserver*) override { return true; }
  bool removeObserver(ICryptoNoteProtocolObserver*) override { return true; }
  uint32_t getObservedHeight() const override { return m_core.get_current_blockchain_height(); }
  size_t getPeerCount() const override { return 0; }
  bool isSynchronized() const override { return true; }
private:
  core& m_core;
};

class WalletEvents : public IWalletLegacyObserver {
public:
  void synchronizationProgressUpdated(uint32_t current, uint32_t) override { m_synced = current; }
  void synchronizationCompleted(std::error_code result) override {
    if (result) {
      std::lock_guard<std::mutex> lk(m_mutex);
      m_syncError = result;
      std::cerr << "wallet sync error: " << result.message() << "\n";
      return;
    }
    m_synced = m_target.load();
  }
  std::error_code m_syncError;
  void sendTransactionCompleted(TransactionId id, std::error_code result) override {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_sent[id] = result;
    m_cv.notify_all();
  }
  std::error_code waitSent(TransactionId id) {
    std::unique_lock<std::mutex> lk(m_mutex);
    if (!m_cv.wait_for(lk, std::chrono::seconds(60), [&] { return m_sent.count(id) > 0; }))
      return std::make_error_code(std::errc::timed_out);
    return m_sent[id];
  }
  std::atomic<uint32_t> m_synced{0};
  std::atomic<uint32_t> m_target{0};
private:
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::map<TransactionId, std::error_code> m_sent;
};

bool waitFor(const std::function<bool()>& condition, int seconds = 60) {
  for (int i = 0; i < seconds * 20; ++i) {
    if (condition()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return condition();
}

int check(bool ok, const std::string& what) {
  std::cout << (ok ? "PASS " : "FAIL ") << what << "\n";
  return ok ? 0 : 1;
}

// The operator's seedboot path through the production wallet: mint HEAT at
// the launch ratio, open the empty pool with the first deposit, withdraw.
int runSeedboot(const std::string& dir, uint32_t startHeight) {
  Node n(dir);
  LoneProtocol protocol(n.node);
  InProcessNode inProcessNode(n.node, protocol);
  std::promise<std::error_code> nodeReady;
  inProcessNode.init([&](std::error_code ec) { nodeReady.set_value(ec); });
  if (nodeReady.get_future().get()) { std::cerr << "node init failed\n"; return 1; }

  WalletEvents events;
  WalletLegacy wallet(n.currency, inProcessNode, n.log);
  wallet.addObserver(&events);
  // Restored from keys, so it scans from genesis: the harness chain's block
  // timestamps predate any wallet creation time.
  const AccountBase payTo = minerAccount(7);
  wallet.initWithKeys(payTo.getAccountKeys(), "seedboot");

  // The wallet funds itself from its own coinbase, then blocks go to a
  // bystander, so every output the wallet spends predates its first send.
  const AccountBase bystander = minerAccount(1);
  const AccountBase* blockRecipient = &payTo;
  auto mineTo = [&](uint32_t height) {
    while (n.height() <= height)
      if (!mineBlock(n, *blockRecipient, 0)) return false;
    events.m_target = n.height();
    return waitFor([&] { return events.m_synced >= n.height() - 1; });
  };
  auto mineOne = [&] { return mineTo(n.height()); };
  int failures = 0;
  const uint64_t C = parameters::COIN;
  const uint64_t fee = n.currency.minimumFee();

  failures += check(mineTo(startHeight), "mined past v11 and the wallet synced");
  const uint64_t needed = 81 * C + 80 * C + 10 * fee;
  for (int block = 0; block < 100 && wallet.actualBalance() < needed; ++block) mineOne();
  failures += check(wallet.actualBalance() >= needed, "wallet funded from its own coinbase: " + std::to_string(wallet.actualBalance()));
  blockRecipient = &bystander;
  const auto& pool = n.chain().getAmmPool();
  failures += check(pool.reserveXfg == 0 && pool.totalLpShares == 0, "pool starts empty (no protocol seed)");
  const uint64_t launchPrice = n.chain().getMintPrice();
  failures += check(launchPrice == C / 8, "mint price is the 8:1 launch ratio: " + std::to_string(launchPrice));

  const uint64_t burn = 81 * C;
  const uint64_t premiumXfg = burn * parameters::HEAT_MINT_PREMIUM_BPS / 10000;
  const uint64_t heat = static_cast<uint64_t>((uint128_t)(burn - premiumXfg) * launchPrice / C);
  const TransactionId mintTx = wallet.mintHeatV10(burn, heat, fee, 0);
  failures += check(mintTx != WALLET_LEGACY_INVALID_TRANSACTION_ID && !events.waitSent(mintTx),
                    "wallet mint of " + std::to_string(heat) + " HEAT relayed");
  const uint32_t mintHeight = n.height();
  failures += check(mineOne(), "mint block mined");
  failures += check(n.chain().getHeatSupply() == heat + premiumXfg * launchPrice / C,
                    "consensus HEAT supply = minted + AuntPoL HEAT leg: " + std::to_string(n.chain().getHeatSupply()));
  failures += check(mineTo(mintHeight + parameters::CRYPTONOTE_DEFAULT_TX_SPENDABLE_AGE + 1),
                    "mined past the spendable age");
  WalletLegacyTransaction mintInfo;
  wallet.getTransaction(mintTx, mintInfo);
  failures += check(mintInfo.blockHeight == mintHeight,
                    "wallet's own mint record confirmed at its block: " + std::to_string(mintInfo.blockHeight));
  size_t mintRecords = 0;
  for (TransactionId t = 0; t < wallet.getTransactionCount(); ++t) {
    WalletLegacyTransaction record;
    if (wallet.getTransaction(t, record) && record.hash == mintInfo.hash) ++mintRecords;
  }
  failures += check(mintRecords == 1, "one wallet record for the mint, not two: " + std::to_string(mintRecords));
  failures += check(waitFor([&] { return wallet.actualHeatBalance() == heat; }),
                    "minted HEAT spendable in the wallet: " + std::to_string(wallet.actualHeatBalance()));
  // Spends select from the wallet's HEAT records, which unlock on the
  // container's unlock notice, a moment after the balance reads unlocked.
  failures += check(waitFor([&] {
                      Deposit record;
                      return wallet.getDepositCount() > 0 && wallet.getDeposit(0, record) && !record.locked;
                    }),
                    "minted HEAT record unlocked for spending");

  const uint64_t depositXfg = 80 * C, depositHeat = 10 * C;
  const TransactionId addTx = wallet.lpAddV10(depositXfg, depositHeat, fee, 0);
  const std::error_code addResult = events.waitSent(addTx);
  failures += check(addTx != WALLET_LEGACY_INVALID_TRANSACTION_ID && !addResult,
                    "wallet first deposit 80 XFG + 10 HEAT relayed" + (addResult ? ": " + addResult.message() : ""));
  const uint32_t depositHeight = n.height();
  failures += check(mineOne(), "deposit block mined");
  WalletLegacyTransaction addInfo;
  waitFor([&] { return wallet.getTransaction(addTx, addInfo) && addInfo.blockHeight == depositHeight; });
  failures += check(addInfo.blockHeight == depositHeight,
                    "wallet's own LP add record confirmed at its block: " + std::to_string(addInfo.blockHeight));
  const uint64_t expectedShares = ammInitialLpShares(depositXfg, depositHeat);
  failures += check(pool.reserveXfg == depositXfg && pool.reserveHeat == depositHeat,
                    "pool opened at 80 / 10: " + std::to_string(pool.reserveXfg) + " / " + std::to_string(pool.reserveHeat));
  failures += check(pool.totalLpShares == expectedShares, "total shares = sqrt(x*y) incl. the locked 1000");
  failures += check(n.chain().getMintPrice() == launchPrice, "mint price still launch ratio until a TWAP exists");
  failures += check(mineOne() && mineOne() && n.chain().getMintPrice() == launchPrice,
                    "TWAP of an 8:1 pool prices mints at 8:1");

  const uint64_t heldShares = expectedShares - AMM_MINIMUM_LIQUIDITY;
  failures += check(mineTo(n.height() + parameters::CRYPTONOTE_DEFAULT_TX_SPENDABLE_AGE), "mined past the LP output's spendable age");
  uint64_t walletLpShares = 0;
  for (size_t d = 0; d < wallet.getDepositCount(); ++d) {
    Deposit record;
    if (wallet.getDeposit(d, record) && record.term == parameters::DEPOSIT_TERM_LP && !record.locked)
      walletLpShares += record.amount;
  }
  failures += check(walletLpShares == heldShares,
                    "wallet holds the LP output for all but the locked shares: " + std::to_string(walletLpShares));
  failures += check(wallet.actualHeatBalance() == heat - depositHeat,
                    "HEAT change returned: " + std::to_string(wallet.actualHeatBalance()));
  const uint64_t xfgBefore = wallet.actualBalance();
  const TransactionId removeTx = wallet.lpRemoveV10(heldShares, 0, 0, fee, 0);
  const std::error_code removeResult = events.waitSent(removeTx);
  failures += check(removeTx != WALLET_LEGACY_INVALID_TRANSACTION_ID && !removeResult,
                    "wallet LP removal of all held shares relayed" + (removeResult ? ": " + removeResult.message() : ""));
  failures += check(mineOne(), "removal block mined");
  failures += check(pool.totalLpShares == AMM_MINIMUM_LIQUIDITY + n.chain().getProtocolLpShares() && pool.reserveXfg > 0 && pool.reserveHeat > 0,
                    "only the locked and AuntPoL shares remain; pool not empty: " + std::to_string(pool.reserveXfg) + " / " +
                        std::to_string(pool.reserveHeat));
  failures += check(waitFor([&] { return wallet.actualBalance() + wallet.pendingBalance() > xfgBefore; }),
                    "withdrawn XFG returned to the wallet");
  failures += check(waitFor([&] { return wallet.actualHeatBalance() + wallet.pendingHeatBalance() > heat - depositHeat &&
                                          wallet.actualHeatBalance() + wallet.pendingHeatBalance() < heat; }),
                    "withdrawn HEAT returned to the wallet: " + std::to_string(wallet.actualHeatBalance() + wallet.pendingHeatBalance()));

  wallet.removeObserver(&events);
  wallet.shutdown();
  inProcessNode.shutdown();
  std::cout << (failures == 0 ? "SEEDBOOT PASS" : "SEEDBOOT FAIL") << "\n";
  return failures == 0 ? 0 : 1;
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
      // mine <dir> <toHeight> <minerSeed> <timestampSalt> [relayFile]: with a
      // relay file, each block first takes the transactions that chain
      // included at the same height, through the mempool as a relayed tx.
      const uint32_t toHeight = std::stoul(argv[3]);
      const AccountBase miner = minerAccount(static_cast<uint8_t>(std::stoul(argv[4])));
      const uint64_t salt = std::stoull(argv[5]);
      const auto relayed = argc > 6 ? readTransactionsByHeight(argv[6]) : std::map<uint32_t, std::vector<BinaryArray>>{};
      Node n(dir);
      while (n.height() <= toHeight) {
        const auto atHeight = relayed.find(n.height());
        if (atHeight != relayed.end()) {
          for (const auto& txBlob : atHeight->second) {
            tx_verification_context tvc = boost::value_initialized<tx_verification_context>();
            n.node.handle_incoming_tx(txBlob, tvc, false);
            if (!tvc.m_added_to_pool) {
              std::cerr << "mine: relayed tx refused at height " << n.height() << "\n";
              return 1;
            }
          }
        }
        const size_t relayedCount = atHeight == relayed.end() ? 0 : atHeight->second.size();
        if (n.node.get_pool_transactions_count() != relayedCount) {
          std::cerr << "mine: mempool holds " << n.node.get_pool_transactions_count() << " txs at height " << n.height()
                    << ", expected " << relayedCount << "\n";
          return 1;
        }
        if (!mineBlock(n, miner, salt)) {
          std::cerr << "mine: block at height " << n.height() << " rejected\n";
          return 1;
        }
      }
      std::cout << "mined to height " << n.height() - 1 << "\n";
    } else if (cmd == "export") {
      // export <dir> <from> <to> <file>
      Node n(dir);
      exportBlocks(n, std::stoul(argv[3]), std::stoul(argv[4]), argv[5]);
    } else if (cmd == "import") {
      // import <dir> <file> <from> <to>
      Node n(dir);
      int imported = importBlocks(n, argv[3], std::stoul(argv[4]), std::stoul(argv[5]));
      std::cout << "imported " << imported << " blocks, height " << n.height() - 1 << "\n";
      if (imported < 0) return 1;
    } else if (cmd == "seedboot") {
      // seedboot <dir> [startHeight]: the wallet flow begins at startHeight (default 60)
      return runSeedboot(dir, argc > 3 ? static_cast<uint32_t>(std::stoul(argv[3])) : 60);
    } else if (cmd == "import2") {
      // import2 <dir> <fileA> <fromA> <toA> <fileB> <fromB> <toB>: two segments, one process
      Node n(dir);
      int a = importBlocks(n, argv[3], std::stoul(argv[4]), std::stoul(argv[5]));
      int b = importBlocks(n, argv[6], std::stoul(argv[7]), std::stoul(argv[8]));
      std::cout << "imported " << a << " + " << b << " blocks, height " << n.height() - 1 << "\n";
      if (a < 0 || b < 0) return 1;
    } else if (cmd == "state") {
      // state <dir>: print consensus state, then a digest of the cache this node writes at shutdown
      Currency currencyCopy = [&] {
        Logging::LoggerGroup l;
        return CurrencyBuilder(l).testnet(true).currency();
      }();
      {
        Node n(dir);
        VaultCoreTestAccess::printState(n.chain(), std::cout);
      }
      std::cout << "cacheBytes=" << cacheDigest(dir, currencyCopy) << "\n";
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
