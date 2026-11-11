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

#include <cstring>
#include <sstream>
#include <string>

#include "gtest/gtest.h"

#include "Common/StdInputStream.h"
#include "Common/StdOutputStream.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/TransactionApi.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "Logging/ConsoleLogger.h"
#include "Serialization/BinaryInputStreamSerializer.h"
#include "Serialization/BinaryOutputStreamSerializer.h"
#include "Transfers/TransfersContainer.h"

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
                                                         const Crypto::KeyImage& keyImage) {
    auto tx = createTransaction();
    TransactionOutputCommitment out;
    out.commitKey = commitKey;
    out.term = parameters::HEAT_TERM;
    tx->addOutput(kAmount, out);

    TransactionOutputInformationIn info{};
    info.type = TransactionTypes::OutputType::Commitment;
    info.amount = kAmount;
    info.globalOutputIndex = 5;
    info.outputInTransaction = 0;
    info.transactionPublicKey = tx->getTransactionPublicKey();
    info.term = parameters::HEAT_TERM;
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

// A cache written before storage version 2 has no commit key. Build a genuine v1
// stream by taking the v2 bytes, excising the 32-byte key, and patching the version.
TEST_F(CommitmentPersistence, Version1CacheStillLoadsAndReportsKeyNotRecorded) {
  const Crypto::PublicKey commitKey = patternKey(2);
  auto tx = addCommitmentTransaction(commitKey, patternImage(2));
  const Crypto::Hash txHash = tx->getTransactionHash();

  std::stringstream v2;
  container.save(v2);
  std::string bytes = v2.str();

  const std::string needle(reinterpret_cast<const char*>(commitKey.data), sizeof(commitKey.data));
  const size_t pos = bytes.find(needle);
  ASSERT_NE(std::string::npos, pos) << "v2 stream must contain the commit key";
  ASSERT_EQ(std::string::npos, bytes.find(needle, pos + 1)) << "the key must appear exactly once";
  bytes.erase(pos, needle.size());
  ASSERT_EQ(2, static_cast<unsigned char>(bytes[0])) << "first byte is the storage version";
  bytes[0] = 1;

  std::stringstream v1(bytes);
  TransfersContainer restored(currency, kSpendableAge);
  restored.load(v1);

  TransactionOutputInformation out;
  ITransfersContainer::TransferState state;
  ASSERT_TRUE(restored.getTransfer(txHash, 0, out, state));
  EXPECT_EQ(Crypto::PublicKey{}, out.commitmentKey)
    << "an unrecorded key must read back as all-zero, not indeterminate bytes";
  EXPECT_EQ(kAmount, out.amount) << "the rest of a v1 record must be intact";
  EXPECT_EQ(parameters::HEAT_TERM, out.term);
}

// Spent outputs go through the same layout and must keep the key too.
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

// Only Commitment records changed layout. A Key output must serialize to exactly the
// same bytes under v1 and v2, or every existing wallet cache would misparse.
TEST(CommitmentPersistenceRecords, KeyOutputLayoutIsUnchanged) {
  TransactionOutputInformationEx key{};
  key.type = TransactionTypes::OutputType::Key;
  key.amount = 77;
  key.outputInTransaction = 1;
  key.outputKey = patternKey(4);
  key.keyImage = patternImage(4);

  auto bytesOf = [](bool v1, TransactionOutputInformationEx value) {
    std::stringstream stream;
    Common::StdOutputStream out(stream);
    BinaryOutputStreamSerializer writer(out);
    if (v1) {
      value.serializeV1(writer);
    } else {
      value.serialize(writer);
    }
    return stream.str();
  };
  EXPECT_EQ(bytesOf(true, key), bytesOf(false, key));
}

// The v1 reader zero-initialises: readSequence default-constructs, so without this the
// key would be whatever the stack held.
TEST(CommitmentPersistenceRecords, V1ReaderZeroInitialisesTheKey) {
  TransactionOutputInformationExV1 reader;
  EXPECT_EQ(Crypto::PublicKey{}, reader.commitmentKey);
  EXPECT_EQ(Crypto::KeyImage{}, reader.keyImage);
}
