// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "INode.h"
#include "crypto/crypto.h" //for rand()
#include <iostream>
#include "CryptoNoteCore/Account.h"
#include "DynamicRingSize.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/TransactionApi.h"
#include "CryptoNoteCore/CryptoNoteBasicImpl.h"
#include "WalletLegacy/WalletTransactionSender.h"
#include "WalletLegacy/WalletUtils.h"
#include "CryptoNoteCore/DepositCommitment.h"
#include "CryptoNoteCore/BurnProofDataFileGenerator.h"
#include "Common/FileSystem.h"
#include "Common/PathTools.h"
#include "INode.h"

#include <Logging/LoggerGroup.h>
#include <array>
#include <cstring>
#include <numeric>
#include <random>
#include <set>
#include "CryptoNoteCore/TransactionExtra.h"

using namespace Crypto;

namespace
{
  using namespace CryptoNote;

  // Build a 36-byte map key from (txHash, outputIndex) for sub-address output lookup
  // std::array has lexicographic operator<, so no custom comparator needed
  std::array<uint8_t, 36> makeSubAddrOutputKey(const Crypto::Hash& txHash, uint32_t outputIdx) {
    std::array<uint8_t, 36> key;
    std::memcpy(key.data(), txHash.data, 32);
    key[32] = outputIdx & 0xFF;
    key[33] = (outputIdx >> 8) & 0xFF;
    key[34] = (outputIdx >> 16) & 0xFF;
    key[35] = (outputIdx >> 24) & 0xFF;
    return key;
  }

  uint64_t countNeededMoney(uint64_t fee, const std::vector<WalletLegacyTransfer> &transfers)
  {
    uint64_t needed_money = fee;
    for (auto &transfer : transfers)
    {
      throwIf(transfer.amount == 0, error::ZERO_DESTINATION);
      throwIf(transfer.amount < 0, error::WRONG_AMOUNT);

      needed_money += transfer.amount;
      throwIf(static_cast<int64_t>(needed_money) < transfer.amount, error::SUM_OVERFLOW);
    }

    return needed_money;
  }

  uint64_t getSumWithOverflowCheck(uint64_t amount, uint64_t fee)
  {
    CryptoNote::throwIf(std::numeric_limits<uint64_t>::max() - amount < fee, error::SUM_OVERFLOW);

    return amount + fee;
  }

  void createChangeDestinations(const AccountPublicAddress &address, uint64_t neededMoney, uint64_t foundMoney, TransactionDestinationEntry &changeDts)
  {
    if (neededMoney < foundMoney)
    {
      changeDts.addr = address;
      changeDts.amount = foundMoney - neededMoney;
    }
  }

  void constructTx(const AccountKeys keys, const std::vector<TransactionSourceEntry> &sources, const std::vector<TransactionDestinationEntry> &splittedDests,
                   const std::string &extra, uint64_t unlockTimestamp, uint64_t sizeLimit, Transaction &tx, const std::vector<tx_message_entry> &messages, uint64_t ttl, Crypto::SecretKey &transactionSK)
  {
    std::vector<uint8_t> extraVec;
    extraVec.reserve(extra.size());
    std::for_each(extra.begin(), extra.end(), [&extraVec](const char el) { extraVec.push_back(el); });

    Logging::LoggerGroup nullLog;
    Crypto::SecretKey txSK;
    bool r = constructTransaction(keys, sources, splittedDests, messages, ttl, extraVec, tx, unlockTimestamp, nullLog, txSK);
    transactionSK = txSK;

    throwIf(!r, error::INTERNAL_WALLET_ERROR);
    throwIf(getObjectBinarySize(tx) >= sizeLimit, error::TRANSACTION_SIZE_TOO_BIG);
  }

  std::unique_ptr<WalletLegacyEvent> makeCompleteEvent(WalletUserTransactionsCache &transactionCache, size_t transactionId, std::error_code ec)
  {
    transactionCache.updateTransactionSendingState(transactionId, ec);
    return std::unique_ptr<WalletSendTransactionCompletedEvent>(new WalletSendTransactionCompletedEvent(transactionId, ec));
  }

  std::vector<TransactionTypes::InputKeyInfo> convertSources(std::vector<TransactionSourceEntry> &&sources)
  {
    std::vector<TransactionTypes::InputKeyInfo> inputs;
    inputs.reserve(sources.size());

    for (TransactionSourceEntry &source : sources)
    {
      TransactionTypes::InputKeyInfo input;
      input.amount = source.amount;

      input.outputs.reserve(source.outputs.size());
      for (const TransactionSourceEntry::OutputEntry &sourceOutput : source.outputs)
      {
        TransactionTypes::GlobalOutput output;
        output.outputIndex = sourceOutput.first;
        output.targetKey = sourceOutput.second;

        input.outputs.emplace_back(std::move(output));
      }

      input.realOutput.transactionPublicKey = source.realTransactionPublicKey;
      input.realOutput.outputInTransaction = source.realOutputIndexInTransaction;
      input.realOutput.transactionIndex = source.realOutput;

      inputs.emplace_back(std::move(input));
    }

    return inputs;
  }

  std::vector<uint64_t> splitAmount(uint64_t amount, uint64_t dustThreshold)
  {
    std::vector<uint64_t> amounts;

    decompose_amount_into_digits(
        amount, dustThreshold,
        [&](uint64_t chunk) { amounts.push_back(chunk); },
        [&](uint64_t dust) { amounts.push_back(dust); });

    return amounts;
  }

  Transaction convertTransaction(const ITransaction &transaction, size_t upperTransactionSizeLimit)
  {
    BinaryArray serializedTransaction = transaction.getTransactionData();
    CryptoNote::throwIf(serializedTransaction.size() >= upperTransactionSizeLimit, error::TRANSACTION_SIZE_TOO_BIG);

    Transaction result;
    Crypto::Hash transactionHash;
    Crypto::Hash transactionPrefixHash;
    if (!parseAndValidateTransactionFromBinaryArray(serializedTransaction, result, transactionHash, transactionPrefixHash))
    {
      throw std::system_error(make_error_code(error::INTERNAL_WALLET_ERROR), "Cannot convert transaction");
    }

    return result;
  }

  uint64_t checkDepositsAndCalculateAmount(const std::vector<DepositId> &depositIds, const WalletUserTransactionsCache &transactionsCache)
  {
    uint64_t amount = 0;

    for (const auto &id : depositIds)
    {
      Deposit deposit;
      throwIf(!transactionsCache.getDeposit(id, deposit), error::DEPOSIT_DOESNOT_EXIST);
      throwIf(deposit.locked, error::DEPOSIT_LOCKED);


      amount += deposit.amount;
    }

    return amount;
  }

  void countDepositsTotalSumAndInterestSum(const std::vector<DepositId> &depositIds, WalletUserTransactionsCache &depositsCache,
                                           uint64_t &totalSum, uint64_t &interestsSum)
  {
    totalSum = 0;
    interestsSum = 0;


    for (auto id : depositIds)
    {
      Deposit &deposit = depositsCache.getDeposit(id);

      totalSum += deposit.amount;
      interestsSum += deposit.interest;


    }
  }

} //namespace

namespace CryptoNote
{
  WalletTransactionSender::WalletTransactionSender(const Currency &currency, WalletUserTransactionsCache &transactionsCache, AccountKeys keys, ITransfersContainer &transfersContainer, INode &node) : m_currency(currency),
                                                                                                                                                                                                       m_transactionsCache(transactionsCache),
                                                                                                                                                                                                       m_isStoping(false),
                                                                                                                                                                                                       m_keys(keys),
                                                                                                                                                                                                       m_transferDetails(transfersContainer),
                                                                                                                                                                                                       m_upperTransactionSizeLimit(m_currency.transactionMaxSize()),
                                                                                                                                                                                                       m_node(node)
  {
  }

  void WalletTransactionSender::stop()
  {
    m_isStoping = true;
  }

  void WalletTransactionSender::addSubAddress(const AccountKeys& subKeys, ITransfersContainer& subContainer)
  {
    m_subAddressSources.push_back({subKeys, &subContainer});

    // Index all currently known unlocked outputs from this sub-address container
    // so prepareKeyInputs can look up the correct signing keys per output
    std::vector<TransactionOutputInformation> outputs;
    subContainer.getOutputs(outputs, ITransfersContainer::IncludeKeyUnlocked);
    for (const auto& out : outputs) {
      if (out.type == TransactionTypes::OutputType::Key) {
        m_subAddressOutputKeys[makeSubAddrOutputKey(out.transactionHash, out.outputInTransaction)] = subKeys;
      }
    }
  }

  bool WalletTransactionSender::validateDestinationAddress(const std::string &address)
  {
    AccountPublicAddress ignore;
    return m_currency.parseAccountAddressString(address, ignore);
  }

  void WalletTransactionSender::validateTransfersAddresses(const std::vector<WalletLegacyTransfer> &transfers)
  {
    for (const WalletLegacyTransfer &tr : transfers)
    {
      if (!validateDestinationAddress(tr.address))
      {
        throw std::system_error(make_error_code(error::BAD_ADDRESS));
      }
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeSendRequest(
      Crypto::SecretKey &transactionSK,
      bool optimize,
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      std::vector<WalletLegacyTransfer> &transfers,
      uint64_t fee,
      const std::string &extra,
      uint64_t mixIn,
      uint64_t unlockTimestamp,
      const std::vector<TransactionMessage> &messages,
      uint64_t ttl)
  {
    throwIf(transfers.empty(), error::ZERO_DESTINATION);
    validateTransfersAddresses(transfers);
    uint64_t neededMoney;

    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    if (optimize)
    {
      context->foundMoney = selectNTransfersToSend(context->selectedTransfers);
      neededMoney = context->foundMoney;
      throwIf(context->foundMoney < fee, error::WRONG_AMOUNT);
      transfers[0].amount = neededMoney - fee;
    }
    else
    {
      neededMoney = countNeededMoney(fee, transfers);
      context->foundMoney = selectTransfersToSend(neededMoney, false, context->dustPolicy.dustThreshold, context->selectedTransfers);

      // Probe with maxMixin outputs per amount; actual ring size is selected in
      // sendTransactionRandomOutsByAmount once we know what the daemon has available.
      mixIn = m_currency.maxMixin();
      context->dynamicRingSize = true;
    }
    throwIf(context->foundMoney < neededMoney, error::WRONG_AMOUNT);

    transactionId = m_transactionsCache.addNewTransaction(neededMoney, fee, extra, transfers, unlockTimestamp, messages);
    context->transactionId = transactionId;
    context->mixIn = mixIn;
    context->ttl = ttl;

    for (const TransactionMessage &message : messages)
    {
      AccountPublicAddress address;
      bool extracted = m_currency.parseAccountAddressString(message.address, address);
      if (!extracted)
      {
        throw std::system_error(make_error_code(error::BAD_ADDRESS));
      }

      context->messages.push_back({message.message, true, address});
    }

    if (context->mixIn != 0)
    {
      return makeGetRandomOutsRequest(std::move(context), false, transactionSK);
    }

    return doSendTransaction(std::move(context), events, transactionSK);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeDepositRequest(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t term,
      uint64_t amount,
      uint64_t fee,
      uint64_t mixIn)
  {
    return makeDepositRequest(transactionId, events, term, amount, fee, std::string(), mixIn);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeDepositRequest(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t term,
      uint64_t amount,
      uint64_t fee,
      const std::string& extra,
      uint64_t mixIn)
  {

    // Skip term range validation for special terms (FOREVER burns)
    bool isSpecialTerm = (term == CryptoNote::parameters::DEPOSIT_TERM_FOREVER);
    if (!isSpecialTerm) {
      throwIf(term < m_currency.depositMinTerm(), error::DEPOSIT_TERM_TOO_SMALL);
      throwIf(term > m_currency.depositMaxTerm(), error::DEPOSIT_TERM_TOO_BIG);
    }
    throwIf(amount != CryptoNote::parameters::TEST_AMOUNT_TIER_0 && amount < m_currency.depositMinAmount(), error::DEPOSIT_AMOUNT_TOO_SMALL);

    // use dynamic max mixin; DynamicRingSizeCalculator uses highest achievable ring size
    // (18, 15, 12, 10, or 8) from whatever daemon actually has available.
    mixIn = m_currency.maxMixin();

    uint64_t neededMoney = getSumWithOverflowCheck(amount, fee);
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dynamicRingSize = true;
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();

    context->foundMoney = selectTransfersToSend(neededMoney, false, context->dustPolicy.dustThreshold, context->selectedTransfers);

    throwIf(context->foundMoney < neededMoney, error::WRONG_AMOUNT);

    transactionId = m_transactionsCache.addNewTransaction(neededMoney, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;
    context->mixIn = mixIn;
    context->depositTerm = static_cast<uint32_t>(term);

    context->extra = extra;

    // Always fetch random outputs — mixin is now always >= requiredMixin
    {
      Crypto::SecretKey transactionSK;
      return makeGetRandomOutsRequest(std::move(context), true, transactionSK);
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeWithdrawDepositRequest(TransactionId &transactionId,
                                                                                     std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
                                                                                     const std::vector<DepositId> &depositIds,
                                                                                     uint64_t fee)
  {

    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();

    context->foundMoney = selectDepositTransfers(depositIds, context->selectedTransfers);
    throwIf(context->foundMoney < fee, error::WRONG_AMOUNT);

    transactionId = m_transactionsCache.addNewTransaction(context->foundMoney, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;
    context->mixIn = 0;

    setSpendingTransactionToDeposits(transactionId, depositIds);

    // If any selected transfer is a commitment output, use the ring-sig withdrawal path.
    bool isCommitment = false;
    for (const auto& t : context->selectedTransfers) {
      if (t.type == TransactionTypes::OutputType::Commitment) {
        isCommitment = true;
        break;
      }
    }

    if (isCommitment) {
      // All deposits in this withdrawal_ring must share same amount for decoy selection.
      const uint64_t depositAmount = context->selectedTransfers.empty() ? 0 : context->selectedTransfers[0].amount;
      return makeGetRandomCommitmentOutsRequest(std::move(context), depositAmount, depositIds);
    }

    return doSendDepositWithdrawTransaction(std::move(context), events, depositIds);
  }

  std::shared_ptr<WalletRequest> WalletTransactionSender::makeSendFusionRequest(TransactionId &transactionId, std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
                                                                                const std::vector<WalletLegacyTransfer> &transfers, const std::list<TransactionOutputInformation> &fusionInputs, uint64_t fee, const std::string &extra, uint64_t mixIn, uint64_t unlockTimestamp)
  {

    using namespace CryptoNote;

    throwIf(transfers.empty(), error::ZERO_DESTINATION);
    validateTransfersAddresses(transfers);
    uint64_t neededMoney = countNeededMoney(fee, transfers);

    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();

    for (auto &out : fusionInputs)
    {
      context->foundMoney += out.amount;
    }

    std::vector<TransactionOutputInformation> fusionInputsVec{std::begin(fusionInputs), std::end(fusionInputs)};

    throwIf(context->foundMoney < neededMoney, error::WRONG_AMOUNT);
    context->selectedTransfers = fusionInputsVec;

    const std::vector<TransactionMessage> messages;

    transactionId = m_transactionsCache.addNewTransaction(neededMoney, fee, extra, transfers, unlockTimestamp, messages);
    context->transactionId = transactionId;
    context->mixIn = mixIn;
    Crypto::SecretKey transactionSK;

    if (context->mixIn)
    {
      return makeGetRandomOutsRequest(std::move(context), false, transactionSK);
    }

    return doSendTransaction(std::move(context), events, transactionSK);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeGetRandomOutsRequest(std::shared_ptr<SendTransactionContext> &&context, bool isMultisigTransaction, Crypto::SecretKey &transactionSK)
  {
    uint64_t outsCount = context->mixIn + 1; // add one to make possible (if need) to skip real output key
    std::vector<uint64_t> amounts;

    for (const auto &td : context->selectedTransfers)
    {
      amounts.push_back(td.amount);
    }

    return std::unique_ptr<WalletRequest>(new WalletGetRandomOutsByAmountsRequest(amounts, outsCount, context,
                                                                                  std::bind(&WalletTransactionSender::sendTransactionRandomOutsByAmount, this, isMultisigTransaction, context, std::ref(transactionSK),
                                                                                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeGetRandomCommitmentOutsRequest(
      std::shared_ptr<SendTransactionContext>&& context,
      uint64_t amount,
      const std::vector<DepositId>& depositIds)
  {
    uint64_t outsCount = m_currency.maxMixin() + 1; // probe with max + 1 for real output
    
    // To maximize interest payout, select decoys created at or before the real CD.
    // This ensures the "youngest" ring member is as old as possible.
    uint32_t maxHeight = 0;
    if (!depositIds.empty()) {
      CryptoNote::Deposit deposit;
      if (m_transactionsCache.getDeposit(depositIds[0], deposit)) {
        maxHeight = static_cast<uint32_t>(deposit.height);
      }
    }

    return std::unique_ptr<WalletRequest>(new WalletGetRandomCommitmentOutsRequest(
      amount, outsCount, maxHeight, context,
        std::bind(&WalletTransactionSender::sendCommitmentWithdrawRandomOutsByAmount, this,
          context, depositIds,
          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
  }

  void WalletTransactionSender::sendCommitmentWithdrawRandomOutsByAmount(
      std::shared_ptr<SendTransactionContext> context,
      const std::vector<DepositId> depositIds,
      std::deque<std::unique_ptr<WalletLegacyEvent>>& events,
      std::unique_ptr<WalletRequest>& nextRequest,
      std::error_code ec)
  {
    if (m_isStoping) {
      ec = make_error_code(error::TX_CANCELLED);
    }

    if (ec) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, ec));
      return;
    }

    // use DynamicRingSizeCalculator to get optimal ring size from available commitment outputs
    const size_t available = context->commitmentOuts.size();
    std::vector<CryptoNote::OutputInfo> outputInfos;
    outputInfos.emplace_back(0, available);

    size_t ringSize = CryptoNote::DynamicRingSizeCalculator::calculateOptimalRingSize(
      0, outputInfos,
      CryptoNote::BLOCK_MAJOR_VERSION_10,
      m_currency.minMixin(CryptoNote::BLOCK_MAJOR_VERSION_10),
      m_currency.maxMixin()
    );

    if (ringSize == 0) {
      // if not enough commitment outputs yet — fall back to minimum if we have at least 1.
      // Commitment pool is new; allow single-member ring until pool grows.
      ringSize = available > 0 ? available : 0;
    }

    if (ringSize == 0) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId,
        make_error_code(error::MIXIN_COUNT_TOO_BIG)));
      return;
    }

    context->mixIn = static_cast<uint64_t>(ringSize);
    nextRequest = doSendCommitmentWithdrawTransaction(std::move(context), events, depositIds);
  }

  void WalletTransactionSender::sendTransactionRandomOutsByAmount(bool isMultisigTransaction,
                                                                  std::shared_ptr<SendTransactionContext> context,
                                                                  Crypto::SecretKey &transactionSK,
                                                                  std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
                                                                  std::unique_ptr<WalletRequest> &nextRequest,
                                                                  std::error_code ec)
  {
    if (m_isStoping)
    {
      ec = make_error_code(error::TX_CANCELLED);
    }

    if (ec)
    {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, ec));
      return;
    }

    if (context->dynamicRingSize) {
      // Determine the binding constraint: the minimum outs actually returned by the daemon
      // across all input amounts. Each input ring must independently satisfy the ring size.
      size_t minAvailable = context->outs.empty() ? 0 : SIZE_MAX;
      for (const auto& oa : context->outs) {
        minAvailable = std::min(minAvailable, oa.outs.size());
      }

      // Build a single OutputInfo representing the binding constraint and run DynamicRingSizeCalculator.
      std::vector<CryptoNote::OutputInfo> outputInfos;
      outputInfos.emplace_back(0, minAvailable);

      size_t optimalRingSize = CryptoNote::DynamicRingSizeCalculator::calculateOptimalRingSize(
        0,
        outputInfos,
        CryptoNote::BLOCK_MAJOR_VERSION_10,
        m_currency.minMixin(CryptoNote::BLOCK_MAJOR_VERSION_10),
        m_currency.maxMixin()
      );

      if (optimalRingSize == 0) {
        events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::MIXIN_COUNT_TOO_BIG)));
        return;
      }

      context->mixIn = static_cast<uint64_t>(optimalRingSize);
    } else {
      if (!checkIfEnoughMixins(context->outs, context->mixIn))
      {
        events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::MIXIN_COUNT_TOO_BIG)));
        return;
      }
    }

    if (isMultisigTransaction)
    {
      nextRequest = doSendMultisigTransaction(std::move(context), events);
    }
    else
    {
      nextRequest = doSendTransaction(std::move(context), events, transactionSK);
    }
  }

  bool WalletTransactionSender::checkIfEnoughMixins(const std::vector<COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount> &outs, uint64_t mixIn)
  {
    auto scanty_it = std::find_if(outs.begin(), outs.end(), [&](const COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount &out) {
      return out.outs.size() < mixIn;
    });

    return scanty_it == outs.end();
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendTransaction(std::shared_ptr<SendTransactionContext> &&context,
                                                                            std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
                                                                            Crypto::SecretKey &transactionSK)
  {

    if (m_isStoping)
    {

      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return std::unique_ptr<WalletRequest>();
    }

    try
    {

      WalletLegacyTransaction &transaction = m_transactionsCache.getTransaction(context->transactionId);

      std::vector<TransactionSourceEntry> sources;
      prepareKeyInputs(context->selectedTransfers, context->outs, sources, context->mixIn);

      TransactionDestinationEntry changeDts;
      changeDts.amount = 0;
      uint64_t totalAmount = -transaction.totalAmount;
      createChangeDestinations(m_keys.address, totalAmount, context->foundMoney, changeDts);

      std::vector<TransactionDestinationEntry> splittedDests;
      splitDestinations(transaction.firstTransferId, transaction.transferCount, changeDts, context->dustPolicy, splittedDests);

      Transaction tx;
      constructTx(m_keys, sources, splittedDests, transaction.extra, transaction.unlockTime, m_upperTransactionSizeLimit, tx, context->messages, context->ttl, transactionSK);

      getObjectHash(tx, transaction.hash);
      transaction.secretKey = transactionSK;

      m_transactionsCache.updateTransaction(context->transactionId, tx, totalAmount, context->selectedTransfers);

      notifyBalanceChanged(events);

      return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(tx, std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
                                                                                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    }
    catch (std::system_error &ec)
    {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, ec.code()));
    }
    catch (std::exception &)
    {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
    }

    return std::unique_ptr<WalletRequest>();
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendMultisigTransaction(std::shared_ptr<SendTransactionContext> &&context, std::deque<std::unique_ptr<WalletLegacyEvent>> &events)
  {
    if (m_isStoping)
    {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return std::unique_ptr<WalletRequest>();
    }

    try
    {
      //TODO decompose this method
      WalletLegacyTransaction &transactionInfo = m_transactionsCache.getTransaction(context->transactionId);
      std::unique_ptr<ITransaction> transaction = createTransaction();

      uint64_t totalAmount = std::abs(transactionInfo.totalAmount);

      // Keep TransactionSourceEntry sources so we can read hasCustomKeys/customKeys
      // for sub-address outputs. convertSources() drops that info into InputKeyInfo.
      std::vector<TransactionSourceEntry> sources;
      prepareKeyInputs(context->selectedTransfers, context->outs, sources, context->mixIn);
      std::vector<TransactionTypes::InputKeyInfo> inputs = convertSources(std::vector<TransactionSourceEntry>(sources));

      std::vector<uint64_t> decomposedChange = splitAmount(context->foundMoney - totalAmount, context->dustPolicy.dustThreshold);

      // --- Fuego Ring-Signature Commitment Output ---
      // Derive the deposit secret deterministically (COLD) or discard it (HEAT burns).
      //
      // COLD deposits (finite term): depositSecret = H("fuego_commit_v1" || ECDH || outputIndex)
      //   where ECDH = txSecretKey * viewPublicKey — the same shared secret used by CryptoNote
      //   stealth address scanning. The depositor can re-derive this with their view key + the
      //   tx public key found in the blockchain, so it is fully recoverable from seed.
      //
      // HEAT burns (FOREVER term): random ephemeral key is generated and discarded immediately.
      //   No one knows the keyScalar → output is permanently non-spendable as a ring member only.
      const uint32_t commitOutputIndex = static_cast<uint32_t>(transaction->getOutputCount());
      std::array<uint8_t, 32> depositSecret;

      // All deposit types (COLD, HEAT/burn, special staking) use deterministic ECDH derivation.
      // depositSecret = H(ECDH(txSecretKey, viewPubKey) || outputIndex_LE32)
      // This makes every commitment output re-detectable on wallet rescan using the view key.
      // HEAT burns are "permanent" because term=FOREVER has no withdrawal path in the UI,
      // not because the secret is unrecoverable.
      {
        Crypto::SecretKey txSecretKey;
        if (!transaction->getTransactionSecretKey(txSecretKey)) {
          throw std::runtime_error("deposit: could not retrieve tx secret key for commitment derivation");
        }
        Crypto::KeyDerivation ecdh;
        if (!Crypto::generate_key_derivation(m_keys.address.viewPublicKey, txSecretKey, ecdh)) {
          throw std::runtime_error("deposit: ECDH key derivation failed");
        }
        // Mix in output index (LE32) so multiple commitment outputs per tx are independent.
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        preimage[32] = commitOutputIndex & 0xFF;
        preimage[33] = (commitOutputIndex >> 8) & 0xFF;
        preimage[34] = (commitOutputIndex >> 16) & 0xFF;
        preimage[35] = (commitOutputIndex >> 24) & 0xFF;
        Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
        memcpy(depositSecret.data(), h.data, 32);
      }

      CryptoNote::DepositCommitmentKeys commitKeys = CryptoNote::deriveCommitmentKeys(depositSecret);

      CryptoNote::TransactionOutputCommitment commitOut;
      commitOut.commitKey = commitKeys.commitKey;
      commitOut.term      = static_cast<uint32_t>(context->depositTerm);

      auto bankingIndex = transaction->addOutput(
          std::abs(transactionInfo.totalAmount) - transactionInfo.fee,
          commitOut);

      for (uint64_t changeOut : decomposedChange)
      {
        transaction->addOutput(changeOut, m_keys.address);
      }

      transaction->setUnlockTime(transactionInfo.unlockTime);

      // Add extra data if provided (for deposit commitments)
      if (!context->extra.empty()) {
        CryptoNote::BinaryArray extraData(context->extra.begin(), context->extra.end());
        transaction->appendExtra(extraData);
      }

      std::vector<KeyPair> ephKeys;
      ephKeys.reserve(inputs.size());

      for (size_t i = 0; i < inputs.size(); ++i)
      {
        KeyPair ephKey;
        const AccountKeys &keys = (i < sources.size() && sources[i].hasCustomKeys)
                                      ? sources[i].customKeys
                                      : m_keys;
        transaction->addInput(keys, inputs[i], ephKey);
        ephKeys.push_back(std::move(ephKey));
      }

      for (size_t i = 0; i < inputs.size(); ++i)
      {
        transaction->signInputKey(i, inputs[i], ephKeys[i]);
      }

      // Deposit commitment detection:
      // - 0x08 (HEAT) for FOREVER/burn deposits
      // - 0xCD (COLD) for term deposits
      // The wallet already created and appended the commitment to context->extra (line 551-554).
      // Detect the type directly from the tag byte — parseTransactionExtra has stream
      // position issues with commitment-only extras that cause silent failures.

      bool isHeatDeposit = false;
      bool isColdDeposit = false;
      Deposit::Type detectedType = Deposit::Type::COLD;

      if (!context->extra.empty()) {
        uint8_t tag = static_cast<uint8_t>(context->extra[0]);
        if (tag == TX_EXTRA_HEAT_COMMITMENT) {
          detectedType = Deposit::Type::HEAT;
          isHeatDeposit = true;
         } else if (tag == TX_EXTRA_SIMPLE_CD) {
           detectedType = Deposit::Type::COLD;
           isColdDeposit = true;
         }
      } else {
        // No wallet-provided commitment — generate one based on deposit term
        std::vector<uint8_t> generatedExtra;

        if (context->depositTerm == parameters::DEPOSIT_TERM_FOREVER) {
          uint64_t depositAmount = std::abs(transactionInfo.totalAmount) - transactionInfo.fee;
          auto [commitment, secret] = CryptoNote::DepositCommitmentGenerator::generateHeatCommitmentWithSecret(
            depositAmount, std::vector<uint8_t>());

          if (!CryptoNote::createTxExtraWithHeatCommitment(commitment.commitment, depositAmount, commitment.metadata, generatedExtra)) {
            throw std::runtime_error("Failed to generate HEAT commitment for burn deposit");
          }

          transaction->appendExtra(generatedExtra);
          isHeatDeposit = true;
          detectedType = Deposit::Type::HEAT;
        } else {
          uint64_t depositAmount = std::abs(transactionInfo.totalAmount) - transactionInfo.fee;
          auto commitment = CryptoNote::DepositCommitmentGenerator::generateYieldCommitment(
            context->depositTerm, depositAmount, std::vector<uint8_t>());

          std::vector<uint8_t> emptyMetadata;
          std::vector<uint8_t> emptyGiftSecret;
          if (!CryptoNote::createTxExtraWithSimpleCDCommitment(commitment.commitment, depositAmount, context->depositTerm, generatedExtra)) {
            throw std::runtime_error("Failed to generate SimpleCD commitment for term deposit");
          }

          transaction->appendExtra(generatedExtra);
          detectedType = Deposit::Type::COLD;
          isColdDeposit = true;
        }
      }

      transactionInfo.hash = transaction->getTransactionHash();

      Deposit deposit;
      deposit.amount = std::abs(transactionInfo.totalAmount) - transactionInfo.fee;
      deposit.term = context->depositTerm;
      deposit.creatingTransactionId = context->transactionId;
      deposit.spendingTransactionId = WALLET_LEGACY_INVALID_TRANSACTION_ID;
      deposit.locked = true;
      deposit.height = transactionInfo.blockHeight;
      deposit.unlockHeight = transactionInfo.blockHeight + context->depositTerm;
      deposit.transactionHash = transaction->getTransactionHash();
      deposit.outputInTransaction = bankingIndex;
      deposit.depositType = detectedType;
      deposit.interest = 0; // No interest for any deposits (field kept for compatibility)

      deposit.amount = std::abs(transactionInfo.totalAmount) - transactionInfo.fee;

      // Set extra field
      if (!context->extra.empty()) {
        deposit.extra = context->extra;
      }

      // For burn deposits, use a special banking index since there's no regular output
      DepositId depositId = 0;
      if (context->depositTerm == parameters::DEPOSIT_TERM_FOREVER) {
        depositId = m_transactionsCache.insertDeposit(deposit, 0, transaction->getTransactionHash());
      } else {
        depositId = m_transactionsCache.insertDeposit(deposit, bankingIndex, transaction->getTransactionHash());
      }

      // HEAT secret notification: the wallet that created the burn commitment
      // holds the secret key. We only emit this event for completeness.
      if (isHeatDeposit || isColdDeposit) {
        std::string txHashStr = Common::podToHex(transactionInfo.hash);
        // Store the ECDH-derived commitKeys.keyScalar for future withdrawals
        // This secret is deterministically derived from: H(ECDH(txSecretKey, viewPubKey) || outputIndex)
        std::vector<uint8_t> secretMetadata;
        if (isColdDeposit) {
          // For COLD deposits, store commitment metadata if available
          secretMetadata.assign(context->extra.begin(), context->extra.end());
        }
        events.push_back(std::unique_ptr<WalletBurnDepositSecretCreatedEvent>(
          new WalletBurnDepositSecretCreatedEvent(txHashStr, commitKeys.keyScalar, deposit.amount, secretMetadata)));
      }

      transactionInfo.firstDepositId = depositId;
      transactionInfo.depositCount = 1;

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, totalAmount, context->selectedTransfers);

      // For burn deposits, there's no interest - the full amount is burned
      uint64_t depositTotal = 0;
      if (context->depositTerm == parameters::DEPOSIT_TERM_FOREVER) {
        depositTotal = deposit.amount; // No interest for burn deposits
      } else {
        depositTotal = deposit.amount; // No interest for any deposits
      }
      m_transactionsCache.addCreatedDeposit(depositId, depositTotal);

      notifyBalanceChanged(events);

      std::vector<DepositId> deposits{depositId};

      return std::unique_ptr<WalletRequest>(new WalletRelayDepositTransactionRequest(lowlevelTransaction, std::bind(&WalletTransactionSender::relayDepositTransactionCallback, this, context,
                                                                                                                    deposits, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    }
    catch (std::system_error &ec)
    {
      std::cerr << "[doSendMultisig] system_error: " << ec.what() << std::endl;
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, ec.code()));
    }
    catch (std::exception &e)
    {
      std::cerr << "[doSendMultisig] exception: " << e.what() << std::endl;
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
    }

    return std::unique_ptr<WalletRequest>();
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendDepositWithdrawTransaction(std::shared_ptr<SendTransactionContext> &&context,
                                                                                           std::deque<std::unique_ptr<WalletLegacyEvent>> &events, const std::vector<DepositId> &depositIds)
  {
    if (m_isStoping)
    {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return std::unique_ptr<WalletRequest>();
    }

    try
    {
      WalletLegacyTransaction &transactionInfo = m_transactionsCache.getTransaction(context->transactionId);

      std::unique_ptr<ITransaction> transaction = createTransaction();
      std::vector<MultisignatureInput> inputs = prepareMultisignatureInputs(context->selectedTransfers);

      std::vector<uint64_t> outputAmounts = splitAmount(context->foundMoney - transactionInfo.fee, context->dustPolicy.dustThreshold);

      for (const auto &input : inputs)
      {
        transaction->addInput(input);
      }

      for (auto amount : outputAmounts)
      {
        transaction->addOutput(amount, m_keys.address);
      }

      transaction->setUnlockTime(transactionInfo.unlockTime);

      assert(inputs.size() == context->selectedTransfers.size());
      for (size_t i = 0; i < inputs.size(); ++i)
      {
        transaction->signInputMultisignature(i, context->selectedTransfers[i].transactionPublicKey, context->selectedTransfers[i].outputInTransaction, m_keys);
      }

      transactionInfo.hash = transaction->getTransactionHash();

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));

      uint64_t interestsSum;
      uint64_t totalSum;
      countDepositsTotalSumAndInterestSum(depositIds, m_transactionsCache, totalSum, interestsSum);

      UnconfirmedSpentDepositDetails unconfirmed;
      unconfirmed.depositsSum = totalSum;
      unconfirmed.fee = transactionInfo.fee;
      unconfirmed.transactionId = context->transactionId;
      m_transactionsCache.addDepositSpendingTransaction(transaction->getTransactionHash(), unconfirmed);

      return std::unique_ptr<WalletRelayDepositTransactionRequest>(new WalletRelayDepositTransactionRequest(lowlevelTransaction,
                                                                                                            std::bind(&WalletTransactionSender::relayDepositTransactionCallback, this, context, depositIds, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    }
    catch (std::system_error &ec)
    {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, ec.code()));
    }
    catch (std::exception &)
    {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
    }

    return std::unique_ptr<WalletRequest>();
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendCommitmentWithdrawTransaction(
      std::shared_ptr<SendTransactionContext>&& context,
      std::deque<std::unique_ptr<WalletLegacyEvent>>& events,
      const std::vector<DepositId>& depositIds)
  {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return std::unique_ptr<WalletRequest>();
    }

    try {
      WalletLegacyTransaction& transactionInfo = m_transactionsCache.getTransaction(context->transactionId);
      std::unique_ptr<ITransaction> transaction = createTransaction();

      // Split withdrawal proceeds to wallet address.
      std::vector<uint64_t> outputAmounts = splitAmount(
          context->foundMoney - transactionInfo.fee, context->dustPolicy.dustThreshold);
      for (auto amount : outputAmounts) {
        transaction->addOutput(amount, m_keys.address);
      }
      transaction->setUnlockTime(transactionInfo.unlockTime);

      const size_t ringSize = static_cast<size_t>(context->mixIn);
      const auto& decoys = context->commitmentOuts; // returned by getRandomCommitmentOutsForAmount

      // Select `ringSize` decoys from the returned pool (already randomly chosen by daemon).
      // The real spend is added at a random position within the ring.
      for (size_t depositIdx = 0; depositIdx < context->selectedTransfers.size(); ++depositIdx) {
        const TransactionOutputInformation& transfer = context->selectedTransfers[depositIdx];

        // Re-derive the depositKeyScalar deterministically.
        // At deposit creation we use: depositSecret = H(ECDH(txSecretKey, viewPubKey) || outputIndex)
        Crypto::KeyDerivation ecdh;
        if (!Crypto::generate_key_derivation(transfer.transactionPublicKey, m_keys.viewSecretKey, ecdh)) {
          throw std::runtime_error("Commitment withdrawal: ECDH derivation failed");
        }
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        const uint32_t outIdx = transfer.outputInTransaction;
        preimage[32] = outIdx & 0xFF;
        preimage[33] = (outIdx >> 8) & 0xFF;
        preimage[34] = (outIdx >> 16) & 0xFF;
        preimage[35] = (outIdx >> 24) & 0xFF;
        Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));

        std::array<uint8_t, 32> depositSecret;
        memcpy(depositSecret.data(), h.data, 32);

        CryptoNote::DepositCommitmentKeys commitKeys = CryptoNote::deriveCommitmentKeys(depositSecret);
        KeyPair commitmentKeyPair;
        commitmentKeyPair.publicKey  = commitKeys.commitKey;
        commitmentKeyPair.secretKey  = commitKeys.keyScalar;

        // Filter decoys: exclude the real output's global index to prevent duplicate
        // ring members (daemon returns all outputs including the real when pool is small).
        std::vector<CryptoNote::COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry> filteredDecoys;
        filteredDecoys.reserve(decoys.size());
        for (const auto& d : decoys) {
          if (d.global_amount_index != static_cast<uint32_t>(transfer.globalOutputIndex)) {
            filteredDecoys.push_back(d);
          }
        }

        // Decide how many decoys we can actually use (capped at ringSize - 1, leaving 1 slot for real).
        const size_t numDecoys = std::min(filteredDecoys.size(), ringSize - 1);
        const size_t actualRingSize = numDecoys + 1;

        // Pick a random position for the real spend within the ring.
        const size_t realPos = Crypto::rand<size_t>() % actualRingSize;

        // Build ordered ring of global indices (relative-encoded) and public keys.
        std::vector<uint32_t> absIndices;
        std::vector<const Crypto::PublicKey*> ringKeys;

        size_t decoyPos = 0;
        for (size_t slot = 0; slot < actualRingSize; ++slot) {
          if (slot == realPos) {
            // Real spend: global index from the transfer record.
            absIndices.push_back(transfer.globalOutputIndex);
            ringKeys.push_back(&commitmentKeyPair.publicKey);
          } else {
            absIndices.push_back(filteredDecoys[decoyPos].global_amount_index);
            ringKeys.push_back(&filteredDecoys[decoyPos].commit_key);
            ++decoyPos;
          }
        }

        // Sort ring by global index (ascending) — same as KeyInput convention.
        // Recompute realPos after sort.
        std::vector<size_t> order(actualRingSize);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
          return absIndices[a] < absIndices[b];
        });
        size_t sortedRealPos = 0;
        std::vector<uint32_t> sortedAbs(actualRingSize);
        std::vector<const Crypto::PublicKey*> sortedKeys(actualRingSize);
        for (size_t s = 0; s < actualRingSize; ++s) {
          sortedAbs[s]  = absIndices[order[s]];
          sortedKeys[s] = ringKeys[order[s]];
          if (order[s] == realPos) sortedRealPos = s;
        }

        // Convert absolute indices to relative offsets (delta-encoded).
        std::vector<uint32_t> relOffsets(actualRingSize);
        relOffsets[0] = sortedAbs[0];
        for (size_t s = 1; s < actualRingSize; ++s) {
          relOffsets[s] = sortedAbs[s] - sortedAbs[s - 1];
        }

        // Build the TransactionInputCommitmentSpend.
        TransactionInputCommitmentSpend csInput;
        csInput.amount        = transfer.amount;
        csInput.outputIndexes = relOffsets;
        csInput.keyImage      = commitKeys.keyImage;
        transaction->addInput(csInput);

        // Sign the input.
        transaction->signInputCommitmentSpend(depositIdx, sortedKeys, commitmentKeyPair, sortedRealPos);
      }

      transactionInfo.hash = transaction->getTransactionHash();

      Transaction lowlevelTx = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));

      uint64_t interestsSum, totalSum;
      countDepositsTotalSumAndInterestSum(depositIds, m_transactionsCache, totalSum, interestsSum);

      UnconfirmedSpentDepositDetails unconfirmed;
      unconfirmed.depositsSum = totalSum;
      unconfirmed.fee         = transactionInfo.fee;
      unconfirmed.transactionId = context->transactionId;
      m_transactionsCache.addDepositSpendingTransaction(transaction->getTransactionHash(), unconfirmed);

      return std::unique_ptr<WalletRelayDepositTransactionRequest>(
        new WalletRelayDepositTransactionRequest(lowlevelTx,
          std::bind(&WalletTransactionSender::relayDepositTransactionCallback, this,
            context, depositIds, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    }
    catch (std::system_error& err) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, err.code()));
    }
    catch (std::exception&) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId,
        make_error_code(error::INTERNAL_WALLET_ERROR)));
    }

    return std::unique_ptr<WalletRequest>();
  }

  void WalletTransactionSender::relayTransactionCallback(std::shared_ptr<SendTransactionContext> context, std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
                                                         std::unique_ptr<WalletRequest> &nextRequest, std::error_code ec)
  {
    if (m_isStoping)
    {
      return;
    }

    events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, ec));
  }

  void WalletTransactionSender::relayDepositTransactionCallback(std::shared_ptr<SendTransactionContext> context,
                                                                std::vector<DepositId> deposits,
                                                                std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
                                                                std::unique_ptr<WalletRequest> &nextRequest,
                                                                std::error_code ec)
  {
    if (m_isStoping)
    {
      return;
    }

    events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, ec));
    events.push_back(std::unique_ptr<WalletDepositsUpdatedEvent>(new WalletDepositsUpdatedEvent(std::move(deposits))));

    //  Handle burn deposit secrets
    if (context->depositTerm == parameters::DEPOSIT_TERM_FOREVER) {
      // This is a burn deposit - the secret should be handled by the wallet
      // In a more complete implementation, we would pass the secret back to the wallet
    }
  }

  void WalletTransactionSender::splitDestinations(TransferId firstTransferId, size_t transfersCount, const TransactionDestinationEntry &changeDts,
                                                  const TxDustPolicy &dustPolicy, std::vector<TransactionDestinationEntry> &splittedDests)
  {
    uint64_t dust = 0;

    digitSplitStrategy(firstTransferId, transfersCount, changeDts, dustPolicy.dustThreshold, splittedDests, dust);

    throwIf(dustPolicy.dustThreshold < dust, error::INTERNAL_WALLET_ERROR);
    if (0 != dust && !dustPolicy.addToFee)
    {
      splittedDests.push_back(TransactionDestinationEntry(dust, dustPolicy.addrForDust));
    }
  }

  void WalletTransactionSender::digitSplitStrategy(TransferId firstTransferId, size_t transfersCount,
                                                   const TransactionDestinationEntry &change_dst, uint64_t dust_threshold,
                                                   std::vector<TransactionDestinationEntry> &splitted_dsts, uint64_t &dust)
  {
    splitted_dsts.clear();
    dust = 0;

    for (TransferId idx = firstTransferId; idx < firstTransferId + transfersCount; ++idx)
    {
      WalletLegacyTransfer &de = m_transactionsCache.getTransfer(idx);

      AccountPublicAddress addr;
      if (!m_currency.parseAccountAddressString(de.address, addr))
      {
        throw std::system_error(make_error_code(error::BAD_ADDRESS));
      }

      decompose_amount_into_digits(
          de.amount, dust_threshold,
          [&](uint64_t chunk) { splitted_dsts.push_back(TransactionDestinationEntry(chunk, addr)); },
          [&](uint64_t a_dust) { splitted_dsts.push_back(TransactionDestinationEntry(a_dust, addr)); });
    }

    decompose_amount_into_digits(
        change_dst.amount, dust_threshold,
        [&](uint64_t chunk) { splitted_dsts.push_back(TransactionDestinationEntry(chunk, change_dst.addr)); },
        [&](uint64_t a_dust) { dust = a_dust; });
  }

  void WalletTransactionSender::prepareKeyInputs(
      const std::vector<TransactionOutputInformation> &selectedTransfers,
      std::vector<COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount> &outs,
      std::vector<TransactionSourceEntry> &sources, uint64_t mixIn)
  {

    size_t i = 0;

    for (const auto &td : selectedTransfers)
    {
      assert(td.type == TransactionTypes::OutputType::Key);

      sources.resize(sources.size() + 1);
      TransactionSourceEntry &src = sources.back();

      src.amount = td.amount;

      //paste mixin transaction
      if (outs.size())
      {
        std::sort(outs[i].outs.begin(), outs[i].outs.end(),
                  [](const COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::out_entry &a, const COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::out_entry &b) { return a.global_amount_index < b.global_amount_index; });
        for (auto &daemon_oe : outs[i].outs)
        {
          if (td.globalOutputIndex == daemon_oe.global_amount_index)
            continue;
          TransactionSourceEntry::OutputEntry oe;
          oe.first = static_cast<uint32_t>(daemon_oe.global_amount_index);
          oe.second = daemon_oe.out_key;
          src.outputs.push_back(oe);
          if (src.outputs.size() >= mixIn)
            break;
        }
      }

      //paste real transaction to the random index
      auto it_to_insert = std::find_if(src.outputs.begin(), src.outputs.end(), [&](const TransactionSourceEntry::OutputEntry &a) { return a.first >= td.globalOutputIndex; });

      TransactionSourceEntry::OutputEntry real_oe;
      real_oe.first = td.globalOutputIndex;
      real_oe.second = td.outputKey;

      auto interted_it = src.outputs.insert(it_to_insert, real_oe);

      src.realTransactionPublicKey = td.transactionPublicKey;
      src.realOutput = interted_it - src.outputs.begin();
      src.realOutputIndexInTransaction = td.outputInTransaction;

      // Attach sub-address signing keys if this output came from a sub-address container
      auto keyIt = m_subAddressOutputKeys.find(makeSubAddrOutputKey(td.transactionHash, td.outputInTransaction));
      if (keyIt != m_subAddressOutputKeys.end()) {
        src.hasCustomKeys = true;
        src.customKeys = keyIt->second;
      }

      ++i;
    }
  }

  std::vector<TransactionTypes::InputKeyInfo> WalletTransactionSender::prepareKeyInputs(const std::vector<TransactionOutputInformation> &selectedTransfers,
                                                                                        std::vector<COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount> &outs,
                                                                                        uint64_t mixIn)
  {
    std::vector<TransactionSourceEntry> sources;
    prepareKeyInputs(selectedTransfers, outs, sources, mixIn);

    return convertSources(std::move(sources));
  }

  std::vector<MultisignatureInput> WalletTransactionSender::prepareMultisignatureInputs(const std::vector<TransactionOutputInformation> &selectedTransfers)
  {
    std::vector<MultisignatureInput> inputs;
    inputs.reserve(selectedTransfers.size());

    for (const auto &output : selectedTransfers)
    {
      assert(output.type == TransactionTypes::OutputType::Multisignature);
      assert(output.requiredSignatures == 1); //Other types are currently unsupported

      MultisignatureInput input;
      input.amount = output.amount;
      input.signatureCount = output.requiredSignatures;
      input.outputIndex = output.globalOutputIndex;
      input.term = output.term;

      inputs.emplace_back(std::move(input));
    }

    return inputs;
  }

  void WalletTransactionSender::notifyBalanceChanged(std::deque<std::unique_ptr<WalletLegacyEvent>> &events)
  {
    uint64_t unconfirmedOutsAmount = m_transactionsCache.unconfrimedOutsAmount();
    uint64_t change = unconfirmedOutsAmount - m_transactionsCache.unconfirmedTransactionsAmount();

    uint64_t actualBalance = m_transferDetails.balance(ITransfersContainer::IncludeKeyUnlocked) - unconfirmedOutsAmount;
    uint64_t pendingBalance = m_transferDetails.balance(ITransfersContainer::IncludeKeyNotUnlocked) + change;

    events.push_back(std::unique_ptr<WalletActualBalanceUpdatedEvent>(new WalletActualBalanceUpdatedEvent(actualBalance)));
    events.push_back(std::unique_ptr<WalletPendingBalanceUpdatedEvent>(new WalletPendingBalanceUpdatedEvent(pendingBalance)));
  }

  namespace
  {

    template <typename URNG, typename T>
    T popRandomValue(URNG &randomGenerator, std::vector<T> &vec)
    {
      assert(!vec.empty());

      if (vec.empty())
      {
        return T();
      }

      std::uniform_int_distribution<size_t> distribution(0, vec.size() - 1);
      size_t idx = distribution(randomGenerator);

      T res = vec[idx];
      if (idx + 1 != vec.size())
      {
        vec[idx] = vec.back();
      }
      vec.resize(vec.size() - 1);

      return res;
    }

  } // namespace

  uint64_t WalletTransactionSender::selectNTransfersToSend(std::vector<TransactionOutputInformation> &selectedTransfers)
  {
    std::vector<size_t> unusedTransfers;

    std::vector<TransactionOutputInformation> outputs;
    m_transferDetails.getOutputs(outputs, ITransfersContainer::IncludeKeyUnlocked);

    for (size_t i = 0; i < outputs.size(); ++i)
    {
      if (!m_transactionsCache.isUsed(outputs[i]))
      {
        unusedTransfers.push_back(i);
      }
    }

    std::default_random_engine randomGenerator(Crypto::rand<std::default_random_engine::result_type>());
    uint64_t foundMoney = 0;
    size_t i = 0;
    while (!unusedTransfers.empty() && i < CryptoNote::parameters::CRYPTONOTE_OPTIMIZE_SIZE)
    {
      size_t idx = popRandomValue(randomGenerator, unusedTransfers);
      selectedTransfers.push_back(outputs[idx]);
      foundMoney += outputs[idx].amount;
      ++i;
    }

    return foundMoney;
  }

  /** Select the transfers to send for either a transaction or a deposit. The output selection is
   * based on separating the available outputs into base10 buckets and then picking outputs from
   * each bucket until have enough for the transfer. We only select outputs above the dust threshold
   * so if we want to include dust we need to set it accordingly. (Credit to TRTL)*/
  uint64_t WalletTransactionSender::selectTransfersToSend(
      uint64_t neededMoney,
      bool addDust,
      uint64_t dust,
      std::vector<TransactionOutputInformation> &selectedTransfers)
  {
    uint64_t foundMoney = 0;

    /** Get all the unlocked outputs from the wallet (main + sub-addresses) */
    std::vector<TransactionOutputInformation> outputs;
    m_transferDetails.getOutputs(outputs, ITransfersContainer::IncludeKeyUnlocked);
    for (const auto& src : m_subAddressSources) {
      std::vector<TransactionOutputInformation> subOutputs;
      src.container->getOutputs(subOutputs, ITransfersContainer::IncludeKeyUnlocked);
      // Index any newly arrived outputs that weren't in the map yet
      for (const auto& out : subOutputs) {
        if (out.type == TransactionTypes::OutputType::Key) {
          m_subAddressOutputKeys[makeSubAddrOutputKey(out.transactionHash, out.outputInTransaction)] = src.keys;
        }
      }
      outputs.insert(outputs.end(), subOutputs.begin(), subOutputs.end());
    }

    /** Before picking the input buckets, lets shuffle all
     * the available outputs for privacy */
    std::shuffle(outputs.begin(), outputs.end(), std::random_device{});

    /** Split the inputs into buckets based on what power of ten they are in
     * (For example, [1, 2, 5, 7], [20, 50, 80, 80], [100, 600, 700]), though
     * we will ignore dust for the time being. */
    std::unordered_map<uint64_t, std::vector<TransactionOutputInformation>> buckets;

    for (const auto &walletAmount : outputs)
    {
      // Skip outputs already pending in unconfirmed transactions
      if (m_transactionsCache.isUsed(walletAmount)) {
        continue;
      }

      /** Use the number of digits to determine which bucket they fit in */
      int numberOfDigits = floor(log10(walletAmount.amount)) + 1;

      /** If the amount is larger than the current dust threshold
       * insert the amount into the correct bucket */
      if (walletAmount.amount > dust)
      {
        buckets[numberOfDigits].push_back(walletAmount);
      }
    }

    while (foundMoney < neededMoney && !buckets.empty())
    {
      /* Take one element from each bucket, smallest first. */
      for (auto bucket = buckets.begin(); bucket != buckets.end();)
      {
        /* Bucket has been exhausted, remove from list */
        if (bucket->second.empty())
        {
          bucket = buckets.erase(bucket);
        }
        else
        {
          /** Add the amount to the selected transfers so long as
           * foundMoney is still less than neededMoney. This prevents
           * larger outputs than we need when we already have enough funds */
          if (foundMoney < neededMoney)
          {
            selectedTransfers.push_back(bucket->second.back());
            foundMoney += bucket->second.back().amount;
          }

          /* Remove amount we just added */
          bucket->second.pop_back();
          bucket++;
        }
      }
    }

    return foundMoney;
  }

  uint64_t WalletTransactionSender::selectDepositTransfers(const std::vector<DepositId> &depositIds, std::vector<TransactionOutputInformation> &selectedTransfers)
  {
    uint64_t foundMoney = 0;

    for (auto id : depositIds)
    {
      Hash transactionHash;
      uint32_t outputInTransaction;
      throwIf(m_transactionsCache.getDepositInTransactionInfo(id, transactionHash, outputInTransaction) == false, error::DEPOSIT_DOESNOT_EXIST);

      {
        TransactionOutputInformation transfer;
        ITransfersContainer::TransferState state;
        throwIf(m_transferDetails.getTransfer(transactionHash, outputInTransaction, transfer, state) == false, error::DEPOSIT_DOESNOT_EXIST);
        throwIf(state != ITransfersContainer::TransferState::TransferAvailable, error::DEPOSIT_LOCKED);
        selectedTransfers.push_back(std::move(transfer));
      }

      Deposit deposit;
      bool r = m_transactionsCache.getDeposit(id, deposit);
      assert(r);

      foundMoney += deposit.amount;
    }

    return foundMoney;
  }

  void WalletTransactionSender::setSpendingTransactionToDeposits(TransactionId transactionId, const std::vector<DepositId> &depositIds)
  {
    for (auto id : depositIds)
    {
      Deposit &deposit = m_transactionsCache.getDeposit(id);
      deposit.spendingTransactionId = transactionId;
    }
  }

} /* namespace CryptoNote */
