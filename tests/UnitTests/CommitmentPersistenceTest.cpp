// Persistence of the published commitment key in the transfers container.
//
// The defect under test: the signer chooses between the owner-bound and legacy
// derivations from TransactionOutputInformation::commitmentKey, but
// TransactionOutputInformationEx::serialize persisted only `term` for Commitment
// outputs. After a wallet save/reload the key was lost, and every cached HEAT/CD
// output became unspendable ("output matches neither derivation"), legacy ones
// included.
//
// Storage version 2 stores the key. Version 1 and 0 caches must still load, and
// must report the key as not recorded (all-zero) rather than as indeterminate
// bytes, because readSequence default-constructs its elements.

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>

#include "gtest/gtest.h"

#include "Common/StdInputStream.h"
#include "Common/StdOutputStream.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/Account.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/TransactionApi.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "Logging/ConsoleLogger.h"
#include "Serialization/BinaryInputStreamSerializer.h"
#include "Serialization/BinaryOutputStreamSerializer.h"
#include "Transfers/BlockchainSynchronizer.h"
#include "Transfers/TransfersContainer.h"
#include "Transfers/TransfersSynchronizer.h"
#include "NullNode.h"

using namespace CryptoNote;

namespace {

const size_t kSpendableAge = 1;
const uint32_t kHeight = 99;
const uint64_t kAmount = 1000;

Crypto::PublicKey patternKey(uint8_t seed) {
  Crypto::SecretKey secret;
  memset(secret.data, 0, sizeof(secret.data));
  secret.data[0] = seed;
  secret.data[1] = 0x42;
  Crypto::PublicKey pub;
  EXPECT_TRUE(Crypto::secret_key_to_public_key(secret, pub));
  return pub;
}

Crypto::KeyImage patternImage(uint8_t seed) {
  Crypto::KeyImage image;
  memset(image.data, 0, sizeof(image.data));
  image.data[0] = seed;
  image.data[31] = 0x07;
  return image;
}

class CommitmentPersistence : public ::testing::Test {
protected:
  CommitmentPersistence()
    : currency(CurrencyBuilder(logger).currency()),
      container(currency, kSpendableAge) {}

  // A real transaction with one commitment output carrying `commitKey`, and the
  // scanner-style transfer record for it.
  std::unique_ptr<ITransaction> addCommitmentTransaction(const Crypto::PublicKey& commitKey,
                                                         const Crypto::KeyImage& keyImage,
                                                         uint32_t term = parameters::HEAT_TERM) {
    auto tx = createTransaction();
    TransactionOutputCommitment out;
    out.commitKey = commitKey;
    out.term = term;
    tx->addOutput(kAmount, out);

    TransactionOutputInformationIn info{};
    info.type = TransactionTypes::OutputType::Commitment;
    info.amount = kAmount;
    info.globalOutputIndex = 5;
    info.outputInTransaction = 0;
    info.transactionPublicKey = tx->getTransactionPublicKey();
    info.term = term;
    info.keyImage = keyImage;
    info.commitmentKey = commitKey;

    TransactionBlockInfo block{kHeight, 1000000};
    EXPECT_TRUE(container.addTransaction(block, *tx, {info}, {}, nullptr));
    return tx;
  }

  Logging::ConsoleLogger logger;
  Currency currency;
  TransfersContainer container;
};

} // namespace

// The scanner-time record survives a save/load round trip.
TEST_F(CommitmentPersistence, CommitKeySurvivesSaveAndLoad) {
  const Crypto::PublicKey commitKey = patternKey(1);
  const Crypto::KeyImage keyImage = patternImage(1);
  auto tx = addCommitmentTransaction(commitKey, keyImage);
  const Crypto::Hash txHash = tx->getTransactionHash();

  std::stringstream stream;
  container.save(stream);

  TransfersContainer restored(currency, kSpendableAge);
  restored.load(stream);

  TransactionOutputInformation out;
  ITransfersContainer::TransferState state;
  ASSERT_TRUE(restored.getTransfer(txHash, 0, out, state));
  EXPECT_EQ(commitKey, out.commitmentKey) << "the signer needs the published key after a restart";
  EXPECT_EQ(kAmount, out.amount);
  EXPECT_EQ(parameters::HEAT_TERM, out.term);
  EXPECT_EQ(TransactionTypes::OutputType::Commitment, out.type);
}

// Wallets read HEAT through its own type flag; it never counts as XFG or as a deposit.
TEST_F(CommitmentPersistence, HeatIsListedOnlyUnderTheHeatType) {
  addCommitmentTransaction(patternKey(7), patternImage(7));
  std::vector<TransactionOutputInformation> listed;
  container.getOutputs(listed, ITransfersContainer::IncludeTypeHeat | ITransfersContainer::IncludeStateAll);
  ASSERT_EQ(1, listed.size());
  EXPECT_EQ(parameters::HEAT_TERM, listed.front().term);
  listed.clear();
  container.getOutputs(listed, ITransfersContainer::IncludeTypeDeposit | ITransfersContainer::IncludeStateAll);
  EXPECT_TRUE(listed.empty());
  EXPECT_EQ(0, container.balance(ITransfersContainer::IncludeTypeKey | ITransfersContainer::IncludeTypeMultisignature |
                                 ITransfersContainer::IncludeStateAll));
}

// Marker terms are not block counts: LP shares and swap-receive XFG unlock like
// any output instead of waiting height + 0xFFFFFFFD blocks.
TEST_F(CommitmentPersistence, MarkerTermOutputsBecomeSpendable) {
  auto lp = addCommitmentTransaction(patternKey(8), patternImage(8), parameters::DEPOSIT_TERM_LP);
  auto swapReceive = addCommitmentTransaction(patternKey(9), patternImage(9), parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG);
  container.advanceHeight(kHeight + 10);
  for (const auto& tx : {lp->getTransactionHash(), swapReceive->getTransactionHash()}) {
    TransactionOutputInformation out;
    ITransfersContainer::TransferState state;
    ASSERT_TRUE(container.getTransfer(tx, 0, out, state));
    EXPECT_EQ(ITransfersContainer::TransferState::TransferAvailable, state);
  }
}

// A view-only wallet records a zero key image for every owner-bound output; the
// second such output used to collide with the first and fail the sync.
TEST_F(CommitmentPersistence, ViewOnlyWalletKeepsEveryUnverifiedCommitment) {
  for (uint8_t seed = 20; seed < 23; ++seed) {
    auto tx = createTransaction();
    TransactionOutputCommitment out;
    out.commitKey = patternKey(seed);
    out.term = parameters::HEAT_TERM;
    tx->addOutput(kAmount, out);
    TransactionOutputInformationIn info{};
    info.type = TransactionTypes::OutputType::Commitment;
    info.amount = kAmount;
    info.globalOutputIndex = seed;
    info.outputInTransaction = 0;
    info.transactionPublicKey = tx->getTransactionPublicKey();
    info.term = parameters::HEAT_TERM;
    info.commitmentKey = out.commitKey;
    TransactionBlockInfo block{kHeight, 1000000};
    ASSERT_TRUE(container.addTransaction(block, *tx, {info}, {}, nullptr));
  }
  container.advanceHeight(kHeight + 10);
  std::vector<TransactionOutputInformation> heat;
  container.getOutputs(heat, ITransfersContainer::IncludeTypeHeat | ITransfersContainer::IncludeStateUnlocked);
  EXPECT_EQ(3u, heat.size());
}

// A commitment input names the output it spends by key image. The container used
// to index commitment outputs under a descriptor no input could match, so spent
// HEAT, CDs and LP shares stayed available and a rescanned wallet counted them.
template <class CommitmentInput>
void expectInputSpendsTheOutput(TransfersContainer& container, const Crypto::Hash& outputTx,
                                const Crypto::KeyImage& keyImage) {
  auto spend = createTransaction();
  CommitmentInput input{};
  input.amount = kAmount;
  input.outputIndexes = {5};
  input.keyImage = keyImage;
  spend->addInput(input);
  TransactionBlockInfo block{kHeight + 20, 1000200};
  ASSERT_TRUE(container.addTransaction(block, *spend, {}, {}, nullptr));

  TransactionOutputInformation out;
  ITransfersContainer::TransferState state;
  ASSERT_TRUE(container.getTransfer(outputTx, 0, out, state));
  EXPECT_EQ(ITransfersContainer::TransferState::TransferSpent, state);
  std::vector<TransactionOutputInformation> available;
  container.getOutputs(available, ITransfersContainer::IncludeTypeHeat | ITransfersContainer::IncludeStateUnlocked |
                                     ITransfersContainer::IncludeStateLocked | ITransfersContainer::IncludeStateSoftLocked);
  EXPECT_TRUE(available.empty());
}

TEST_F(CommitmentPersistence, CommitmentSpendMarksTheOutputSpent) {
  auto heat = addCommitmentTransaction(patternKey(10), patternImage(10));
  container.advanceHeight(kHeight + 10);
  expectInputSpendsTheOutput<TransactionInputCommitmentSpend>(container, heat->getTransactionHash(), patternImage(10));
}

TEST_F(CommitmentPersistence, CommitmentTransferMarksTheOutputSpent) {
  auto heat = addCommitmentTransaction(patternKey(11), patternImage(11));
  container.advanceHeight(kHeight + 10);
  expectInputSpendsTheOutput<TransactionInputCommitmentTransfer>(container, heat->getTransactionHash(), patternImage(11));
}

// A cache written before storage version 2 has no commit key, and one written before
// version 3 has no Janus flag (the byte that follows the commit key). Build genuine older
// streams from the current bytes by excising those fields and patching the version.
std::string olderContainerStream(const std::string& current, const Crypto::PublicKey& commitKey, uint8_t version) {
  std::string bytes = current;
  const std::string needle(reinterpret_cast<const char*>(commitKey.data), sizeof(commitKey.data));
  const size_t pos = bytes.find(needle);
  EXPECT_NE(std::string::npos, pos) << "the stream must contain the commit key";
  EXPECT_EQ(std::string::npos, bytes.find(needle, pos + 1)) << "the key must appear exactly once";
  EXPECT_EQ(4, static_cast<unsigned char>(bytes[0])) << "first byte is the storage version";
  EXPECT_EQ(0, bytes[pos + needle.size()]) << "the Janus flag follows the commit key";
  bytes.erase(pos + needle.size(), 1);
  if (version < 2) {
    bytes.erase(pos, needle.size());
  }
  bytes[0] = static_cast<char>(version);
  return bytes;
}

// A cache written before version 4 never marked a spent commitment output spent,
// so one holding commitment outputs is refused and the wallet rescans.
TEST_F(CommitmentPersistence, CacheWithCommitmentsFromBeforeVersion4IsRefused) {
  const Crypto::PublicKey commitKey = patternKey(5);
  addCommitmentTransaction(commitKey, patternImage(5));
  std::stringstream current;
  container.save(current);

  std::string version3 = current.str();
  version3[0] = 3;
  for (const std::string& older : {version3, olderContainerStream(current.str(), commitKey, 2),
                                   olderContainerStream(current.str(), commitKey, 1)}) {
    std::stringstream stream(older);
    TransfersContainer restored(currency, kSpendableAge);
    EXPECT_THROW(restored.load(stream), TransfersCacheNeedsRescan);
  }
}

// A cache without commitment outputs (every wallet that never held HEAT, CDs or
// LP shares) still loads from an older version without a rescan.
TEST_F(CommitmentPersistence, CacheWithoutCommitmentsFromVersion3StillLoads) {
  std::stringstream current;
  container.save(current);
  std::string version3 = current.str();
  version3[0] = 3;
  std::stringstream stream(version3);
  TransfersContainer restored(currency, kSpendableAge);
  EXPECT_NO_THROW(restored.load(stream));
}

TEST(CommitmentPersistenceRecords, SpentOutputKeepsCommitKey) {
  SpentTransactionOutput spent{};
  spent.type = TransactionTypes::OutputType::Commitment;
  spent.amount = kAmount;
  spent.outputInTransaction = 3;
  spent.term = parameters::HEAT_TERM;
  spent.commitmentKey = patternKey(3);
  spent.keyImage = patternImage(3);
  spent.inputInTransaction = 9;

  std::stringstream stream;
  {
    Common::StdOutputStream out(stream);
    BinaryOutputStreamSerializer writer(out);
    writer(spent, "spent");
  }

  SpentTransactionOutput back{};
  {
    Common::StdInputStream in(stream);
    BinaryInputStreamSerializer reader(in);
    reader(back, "spent");
  }
  EXPECT_EQ(spent.commitmentKey, back.commitmentKey);
  EXPECT_EQ(spent.keyImage, back.keyImage);
  EXPECT_EQ(spent.inputInTransaction, back.inputInTransaction);
}

// Version 2 changed only Commitment records: a Key output serializes to the same bytes under
// v1 and v2. Version 3 appends the Janus flag byte to every record, read back by version.
TEST(CommitmentPersistenceRecords, KeyOutputLayoutIsUnchanged) {
  TransactionOutputInformationEx key{};
  key.type = TransactionTypes::OutputType::Key;
  key.amount = 77;
  key.outputInTransaction = 1;
  key.outputKey = patternKey(4);
  key.keyImage = patternImage(4);

  auto bytesOf = [](int version, TransactionOutputInformationEx value) {
    std::stringstream stream;
    Common::StdOutputStream out(stream);
    BinaryOutputStreamSerializer writer(out);
    if (version == 1) {
      value.serializeV1(writer);
    } else if (version == 2) {
      value.serializeV2(writer);
    } else {
      value.serialize(writer);
    }
    return stream.str();
  };
  EXPECT_EQ(bytesOf(1, key), bytesOf(2, key));
  EXPECT_EQ(bytesOf(1, key) + std::string(1, '\0'), bytesOf(3, key));
}

// The v1 reader zero-initialises: readSequence default-constructs, so without this the
// key would be whatever the stack held.
TEST(CommitmentPersistenceRecords, V1ReaderZeroInitialisesTheKey) {
  TransactionOutputInformationExV1 reader;
  EXPECT_EQ(Crypto::PublicKey{}, reader.commitmentKey);
  EXPECT_EQ(Crypto::KeyImage{}, reader.keyImage);
}

// A wallet holding commitment outputs saved by a binary from before cache version 4
// must rescan. Loading such a synchronizer state raises TransfersCacheNeedsRescan
// (the wallets catch it and resynchronize) and rolls back every subscription,
// including one under the same view key that loaded before the old cache was met.
namespace {

void addHeatOutput(TransfersContainer& container, uint8_t seed) {
  auto tx = createTransaction();
  TransactionOutputCommitment out;
  out.commitKey = patternKey(seed);
  out.term = parameters::HEAT_TERM;
  tx->addOutput(kAmount, out);
  TransactionOutputInformationIn info{};
  info.type = TransactionTypes::OutputType::Commitment;
  info.amount = kAmount;
  info.globalOutputIndex = seed;
  info.transactionPublicKey = tx->getTransactionPublicKey();
  info.term = parameters::HEAT_TERM;
  info.keyImage = patternImage(seed);
  info.commitmentKey = out.commitKey;
  ASSERT_TRUE(container.addTransaction(TransactionBlockInfo{kHeight, 1000000}, *tx, {info}, {}, nullptr));
}

std::string savedContainer(const TransfersContainer& container) {
  std::stringstream stream;
  const_cast<TransfersContainer&>(container).save(stream);
  return stream.str();
}

}  // namespace

TEST(CommitmentPersistenceSynchronizer, OldCacheWithCommitmentsLoadsFreshForRescan) {
  Logging::ConsoleLogger logger(Logging::ERROR);
  Currency currency = CurrencyBuilder(logger).currency();
  NullNode node;
  AccountBase primary;
  primary.generate();
  AccountSubscription first{};
  first.keys = primary.getAccountKeys();
  first.transactionSpendableAge = kSpendableAge;
  AccountSubscription second = first;
  Crypto::generate_keys(second.keys.address.spendPublicKey, second.keys.spendSecretKey);

  std::string savedState;
  std::string firstState;
  std::string secondState;
  {
    BlockchainSynchronizer chain(node, currency.genesisBlockHash());
    TransfersSyncronizer transfers(currency, logger, chain, node);
    auto& firstContainer = dynamic_cast<TransfersContainer&>(transfers.addSubscription(first).getContainer());
    auto& secondContainer = dynamic_cast<TransfersContainer&>(transfers.addSubscription(second).getContainer());
    addHeatOutput(firstContainer, 30);
    addHeatOutput(secondContainer, 31);
    firstState = savedContainer(firstContainer);
    secondState = savedContainer(secondContainer);
    std::stringstream stream;
    transfers.save(stream);
    savedState = stream.str();
  }

  const size_t firstAt = savedState.find(firstState);
  const size_t secondAt = savedState.find(secondState);
  ASSERT_NE(std::string::npos, firstAt);
  ASSERT_NE(std::string::npos, secondAt);
  const size_t loadedLast = std::max(firstAt, secondAt);
  ASSERT_EQ(4, static_cast<unsigned char>(savedState[loadedLast]));
  savedState[loadedLast] = 3;

  BlockchainSynchronizer chain(node, currency.genesisBlockHash());
  TransfersSyncronizer transfers(currency, logger, chain, node);
  ITransfersContainer& firstContainer = transfers.addSubscription(first).getContainer();
  ITransfersContainer& secondContainer = transfers.addSubscription(second).getContainer();
  std::stringstream stream(savedState);
  EXPECT_THROW(transfers.load(stream), TransfersCacheNeedsRescan);
  EXPECT_EQ(0u, firstContainer.transfersCount()) << "the subscription loaded first is rolled back";
  EXPECT_EQ(0u, secondContainer.transfersCount());
}
