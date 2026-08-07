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

#include "CryptoNoteCore/TransactionExtra.h"
#include "INode.h"
#include "crypto/crypto.h" //for rand()
#include <iostream>
#include "CryptoNoteCore/Account.h"
#include "DynamicRingSize.h"
#include "OSPEADDecoySelection.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/TransactionApi.h"
#include "CryptoNoteCore/CryptoNoteBasicImpl.h"
#include "WalletLegacy/WalletTransactionSender.h"
#include "WalletLegacy/WalletUtils.h"
#include "CryptoNoteCore/DepositCommitment.h"
#include "Common/FileSystem.h"
#include "Common/PathTools.h"
#include "WalletLegacy/WalletRequest.h"  // for WalletGetOutputsHeightsRequest
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
    bool isSpecialTerm = (term == CryptoNote::parameters::HEAT_TERM);
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

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeWithdrawLegacyBondRequest(TransactionId &transactionId,
                                                                                       std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
                                                                                       DepositId depositId,
                                                                                       uint64_t interest,
                                                                                       uint64_t fee)
  {
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();

    std::vector<DepositId> depositIds = {depositId};
    context->foundMoney = selectDepositTransfers(depositIds, context->selectedTransfers);
    
    // Total money including interest (for transaction building)
    uint64_t totalMoney = context->foundMoney + interest;
    throwIf(totalMoney < fee, error::WRONG_AMOUNT);

    // Add transaction to cache (record the total output amount)
    transactionId = m_transactionsCache.addNewTransaction(totalMoney, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;
    context->mixIn = 0;

    setSpendingTransactionToDeposits(transactionId, depositIds);

    return doSendLegacyBondWithdrawTransaction(std::move(context), events, depositId, interest);
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

    // use DynamicRingSizeCalculator to get optimal ring size from available commitment outputs.
    // Intentional: applyOspeadFilter is NOT invoked on this path. The commitment-output pool is
    // small/new and OSPEAD's age-bin model would either bail under min-mixin or filter pointlessly
    // on a near-uniform age distribution. Re-evaluate once the commitment pool ages out and a
    // real spend-pattern is observable.
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
    if (context->isV10AmmSwap) {
      nextRequest = doSendAmmSwapV10CommitmentTransaction(std::move(context), events,
        context->v10SwapDirection, context->v10SwapInput,
        context->v10SwapOutput, context->v10SwapMinOutput);
    } else if (context->isV10LpAdd) {
      nextRequest = doSendLpAddV10Transaction(std::move(context), events,
        context->v10XfgBurned, context->v10HeatMinted);
    } else if (context->isV10LpRemove) {
      nextRequest = doSendLpRemoveV10Transaction(std::move(context), events,
        context->v10LpSharesBurned, context->v10LpMinXfg, context->v10LpMinHeat);
    } else if (context->isV10LpClaim) {
      nextRequest = doSendLpClaimFeesTransaction(std::move(context), events,
        context->v10LpClaimShares, context->v10LpClaimMinXfg, context->v10LpClaimMinHeat);
    } else if (context->isV10AmmSwap && context->depositTerm > 0 && context->depositTerm < parameters::DEPOSIT_TERM_LP) {
      // HEAT CD deposit: spend HEAT → CD commitment output
      nextRequest = doSendHeatDepositV10Transaction(std::move(context), events,
        context->v10SwapInput, context->v10SwapOutput, context->depositTerm);
    } else if (context->isV10HeatTransfer) {
      nextRequest = doSendHeatTransferV10Transaction(std::move(context), events,
        context->v10HeatRecipient, context->v10HeatTransferAmount);
    } else {
      nextRequest = doSendCommitmentWithdrawTransaction(std::move(context), events, depositIds);
    }
  }

  // ── OSPEAD async pipeline (replaces prior blocking applyOspeadFilter) ──
  //
  // Altitude note: OSPEAD runs WALLET-side, after the daemon returns decoys, not
  // inside Blockchain::getRandomOutsByAmount. Wastes RPC bandwidth (we over-
  // request then prune) but keeps daemon decoy selection consensus-neutral and
  // lets each wallet pick its own quality bar. Daemon-side OSPEAD would push
  // state-tracking and policy onto every node.
  //
  // The previous blocking implementation called m_node.getOutputsHeights and
  // then fut.get() on the same event loop that dispatches the RPC reply —
  // deadlock on every send. This version splits the work in two: a chain-decision
  // check populates context->ospeadHeightQueries and returns a new WalletRequest
  // that asynchronously fetches heights; when its callback re-enters
  // sendTransactionRandomOutsByAmount, applyOspeadMaskFromHeights consumes the
  // pre-fetched data and compacts context->outs in place.

  // Returns true if a height-fetch request should be chained. When true, also
  // populates context->ospeadHeightQueries as a side effect. Returns false on
  // second-pass entry (heights already fetched), capability mismatch, or empty
  // pool.
  bool WalletTransactionSender::shouldChainOspeadHeights(std::shared_ptr<SendTransactionContext> context) {
    if (!context->dynamicRingSize) return false;
    if (!m_node.supportsOutputsHeights()) return false;
    if (!context->ospeadHeights.empty()) return false;  // second pass — heights already in hand
    if (context->ospeadHeightsRequested) return false;   // guard: already attempted, don't re-chain

    context->ospeadHeightQueries.clear();
    for (const auto& oa : context->outs) {
      for (const auto& oe : oa.outs) {
        context->ospeadHeightQueries.emplace_back(oa.amount, static_cast<uint32_t>(oe.global_amount_index));
      }
    }
    if (context->ospeadHeightQueries.empty()) return false;
    context->ospeadHeightsRequested = true; // set BEFORE returning true so re-entry is blocked
    return true;
  }

  // Consumes context->ospeadHeights (populated by WalletGetOutputsHeightsRequest)
  // and compacts context->outs in place by spend probability. No-op if heights
  // are missing/misshapen or filtering would drop any ring below min mixin.
  void WalletTransactionSender::applyOspeadMaskFromHeights(std::shared_ptr<SendTransactionContext> context) {
    auto& heights = context->ospeadHeights;
    auto& queries = context->ospeadHeightQueries;
    if (heights.empty() || heights.size() != queries.size()) return;

    const uint32_t currentHeight = m_node.getLastLocalBlockHeight();
    if (currentHeight == 0) return;  // wallet not synced; skip filter

    // Log-age bin pattern, no real spend history wired yet. Same threshold as
    // DynamicRingSize.cpp:208.
    auto spendPattern = CryptoNote::OSPEADDecoySelector::analyzeSpendPatterns({}, currentHeight);
    constexpr double kKeepThreshold = 0.01;

    // Single pass: compute keep-mask aligned with queries; track per-amount
    // survivor count so we can bail before mutating if any ring would drop below
    // min mixin.
    std::vector<bool> keepMask(queries.size(), false);
    const size_t minMix = m_currency.minMixin(CryptoNote::BLOCK_MAJOR_VERSION_10);
    size_t qi = 0;
    for (const auto& oa : context->outs) {
      size_t survivors = 0;
      for (size_t j = 0; j < oa.outs.size(); ++j, ++qi) {
        if (heights[qi] == 0) continue;  // unknown height — leave masked-out
        const uint64_t age = currentHeight - heights[qi];
        const double prob = CryptoNote::OSPEADDecoySelector::calculateSpendProbability(
          age, currentHeight, spendPattern);
        if (prob > kKeepThreshold) {
          keepMask[qi] = true;
          ++survivors;
        }
      }
      if (survivors < minMix) return;  // filter too aggressive; bail
    }

    // Apply mask via two-pointer compaction.
    qi = 0;
    for (auto& oa : context->outs) {
      size_t w = 0;
      for (size_t r = 0; r < oa.outs.size(); ++r, ++qi) {
        if (keepMask[qi]) {
          if (w != r) oa.outs[w] = oa.outs[r];
          ++w;
        }
      }
      oa.outs.resize(w);
    }
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

    // OSPEAD async pipeline: if we have decoys but no heights yet, chain a
    // WalletGetOutputsHeightsRequest. On reentry the heights will be present
    // and we drop through to applyOspeadMaskFromHeights below.
    if (shouldChainOspeadHeights(context)) {
      nextRequest.reset(new WalletGetOutputsHeightsRequest(
        context,
        std::bind(&WalletTransactionSender::sendTransactionRandomOutsByAmount, this,
                  isMultisigTransaction, context, std::ref(transactionSK),
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
      return;
    }

    if (context->dynamicRingSize) {
      // OSPEAD: compact context->outs in place by spend probability using the
      // pre-fetched heights (no-op if heights weren't gathered, e.g. daemon
      // doesn't support /get_outputs_heights or the filter would over-prune).
      applyOspeadMaskFromHeights(context);

      // Determine the binding constraint: the minimum outs actually returned by the daemon
      // across all input amounts. Each input ring must independently satisfy the ring size.
      size_t minAvailable = context->outs.empty() ? 0 : SIZE_MAX;
      for (const auto& oa : context->outs) {
        minAvailable = std::min(minAvailable, oa.outs.size());
      }

      // Build a single OutputInfo representing the binding constraint and run DynamicRingSizeCalculator.
      std::vector<CryptoNote::OutputInfo> outputInfos;
      outputInfos.emplace_back(0, minAvailable);

      // On testnet allow bootstrap ring sizes (pool is small); mainnet enforces minMixin.
      size_t minRing = m_currency.isTestnet() ? 0 : m_currency.minMixin(CryptoNote::BLOCK_MAJOR_VERSION_10);
      size_t optimalRingSize = CryptoNote::DynamicRingSizeCalculator::calculateOptimalRingSize(
        0,
        outputInfos,
        CryptoNote::BLOCK_MAJOR_VERSION_10,
        minRing,
        m_currency.maxMixin()
      );

      if (optimalRingSize == 0) {
        // Testnet bootstrap: ring size 0 (no decoys) when output pool is empty.
        if (m_currency.isTestnet()) {
          optimalRingSize = 0;
        } else {
          events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::MIXIN_COUNT_TOO_BIG)));
          return;
        }
      }

      context->mixIn = static_cast<uint64_t>(optimalRingSize);
    } else {
      if (!checkIfEnoughMixins(context->outs, context->mixIn))
      {
        events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::MIXIN_COUNT_TOO_BIG)));
        return;
      }
    }

    if (context->isV10LpAdd)
    {
      uint64_t heatAmount = context->v10HeatMinted;
      std::vector<DepositId> heatIds;
      nextRequest = makeGetRandomCommitmentOutsRequest(std::move(context), heatAmount, heatIds);
      return;
    }
    else if (context->isV10HeatMint)
    {
      nextRequest = doSendHeatMintV10Transaction(std::move(context), events,
        context->v10XfgBurned, context->v10HeatMinted);
    }
    else if (context->isV10AmmSwap)
    {
      nextRequest = doSendAmmSwapV10Transaction(std::move(context), events,
        context->v10SwapDirection, context->v10SwapInput,
        context->v10SwapOutput, context->v10SwapMinOutput);
    }
    else if (context->isV11LimitDeposit)
    {
      nextRequest = doSendPlaceOrderV13Transaction(std::move(context), events,
        context->v11DepositSide, context->v11DepositAmount, context->v11DepositTargetPrice,
        context->v11DepositExpiration, context->v11DepositOrderId,
        context->v11DepositAddressHash);
    }
    else if (context->isV11LimitWithdraw)
    {
      nextRequest = doSendCancelOrderV13Transaction(std::move(context), events,
        context->v11WithdrawOrderId);
    }
    else if (isMultisigTransaction)
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

  // Derive deterministic deposit secret via ECDH for commitment output.
  // depositSecret = H(ECDH(txSecretKey, viewPubKey) || outputIndex_LE32)
  // Returns a 32-byte secret re-derivable from seed using the view key.
  static std::array<uint8_t, 32> deriveCommitmentSecret(
      const ITransaction& tx,
      const Crypto::PublicKey& viewPublicKey) {
    Crypto::SecretKey txSecretKey;
    if (!tx.getTransactionSecretKey(txSecretKey)) {
      throw std::runtime_error("deposit: could not retrieve tx secret key for commitment derivation");
    }
    Crypto::KeyDerivation ecdh;
    if (!Crypto::generate_key_derivation(viewPublicKey, txSecretKey, ecdh)) {
      throw std::runtime_error("deposit: ECDH key derivation failed");
    }
    const uint32_t commitOutputIndex = static_cast<uint32_t>(tx.getOutputCount());
    uint8_t preimage[36];
    memcpy(preimage, &ecdh, 32);
    preimage[32] = commitOutputIndex & 0xFF;
    preimage[33] = (commitOutputIndex >> 8) & 0xFF;
    preimage[34] = (commitOutputIndex >> 16) & 0xFF;
    preimage[35] = (commitOutputIndex >> 24) & 0xFF;
    Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
    std::array<uint8_t, 32> secret;
    memcpy(secret.data(), h.data, 32);
    return secret;
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
      WalletLegacyTransaction &transactionInfo = m_transactionsCache.getTransaction(context->transactionId);
      std::unique_ptr<ITransaction> transaction = createTransaction();

      uint64_t totalAmount = std::abs(transactionInfo.totalAmount);
      uint64_t depositAmount = totalAmount - transactionInfo.fee;

      // ── Inputs ──────────────────────────────────────────────────────
      std::vector<TransactionSourceEntry> sources;
      prepareKeyInputs(context->selectedTransfers, context->outs, sources, context->mixIn);
      std::vector<TransactionTypes::InputKeyInfo> inputs = convertSources(std::vector<TransactionSourceEntry>(sources));

      std::vector<uint64_t> decomposedChange = splitAmount(context->foundMoney - totalAmount, context->dustPolicy.dustThreshold);

      // ── Commitment output ────────────────────────────────────────────
      auto depositSecret = deriveCommitmentSecret(*transaction, m_keys.address.viewPublicKey);
      CryptoNote::DepositCommitmentKeys commitKeys = CryptoNote::deriveCommitmentKeys(depositSecret);

      CryptoNote::TransactionOutputCommitment commitOut;
      commitOut.commitKey = commitKeys.commitKey;
      commitOut.term      = static_cast<uint32_t>(context->depositTerm);

      auto bankingIndex = transaction->addOutput(depositAmount, commitOut);

      for (uint64_t changeOut : decomposedChange)
      {
        transaction->addOutput(changeOut, m_keys.address);
      }

      transaction->setUnlockTime(transactionInfo.unlockTime);

      // ── Extra data ───────────────────────────────────────────────────
      if (!context->extra.empty()) {
        CryptoNote::BinaryArray extraData(context->extra.begin(), context->extra.end());
        transaction->appendExtra(extraData);
      }

      // ── Sign key inputs ──────────────────────────────────────────────
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

      // ── Deposit type detection & commitment ──────────────────────────
      bool isHeatDeposit = false;
      // bool isColdDeposit = false;
      Deposit::Type detectedType = Deposit::Type::HEAT;

      if (!context->extra.empty()) {
        uint8_t tag = static_cast<uint8_t>(context->extra[0]);
        if (tag == TX_EXTRA_HEAT_COMMITMENT) {
          detectedType = Deposit::Type::HEAT;
          isHeatDeposit = true;
        }
        // } else if (tag == TX_EXTRA_SIMPLE_CD) {
        //   detectedType = Deposit::Type::COLD;
        //   isColdDeposit = true;
        // }
      } else {
        std::vector<uint8_t> generatedExtra;
        if (context->depositTerm == parameters::HEAT_TERM) {
          auto [commitment, secret] = CryptoNote::DepositCommitmentGenerator::generateHeatCommitmentWithSecret(
            depositAmount, std::vector<uint8_t>());
          if (!CryptoNote::createTxExtraWithHeatCommitment(commitment.commitment, depositAmount, commitment.metadata, generatedExtra)) {
            throw std::runtime_error("Failed to generate HEAT commitment for burn deposit");
          }
          transaction->appendExtra(generatedExtra);
          isHeatDeposit = true;
          detectedType = Deposit::Type::HEAT;
        } else {
          // REMOVED: COLD deposit creation (was for non-HEAT term deposits)
          // COLD deposits are no longer supported. Only HEAT deposits (HEAT_TERM) are allowed.
          throw std::runtime_error("COLD deposits are no longer supported. Use HEAT deposit with HEAT_TERM.");
        }
      }

      transactionInfo.hash = transaction->getTransactionHash();

      // ── Deposit record ───────────────────────────────────────────────
      Deposit deposit;
      deposit.amount = depositAmount;
      deposit.term = context->depositTerm;
      deposit.creatingTransactionId = context->transactionId;
      deposit.spendingTransactionId = WALLET_LEGACY_INVALID_TRANSACTION_ID;
      deposit.locked = true;
      deposit.height = transactionInfo.blockHeight;
      deposit.unlockHeight = transactionInfo.blockHeight + context->depositTerm;
      deposit.transactionHash = transaction->getTransactionHash();
      deposit.outputInTransaction = bankingIndex;
      deposit.depositType = detectedType;
      deposit.interest = 0;
      if (!context->extra.empty()) {
        deposit.extra = context->extra;
      }

      DepositId depositId = (context->depositTerm == parameters::HEAT_TERM)
          ? m_transactionsCache.insertDeposit(deposit, 0, transaction->getTransactionHash())
          : m_transactionsCache.insertDeposit(deposit, bankingIndex, transaction->getTransactionHash());

      if (isHeatDeposit) {
        std::string txHashStr = Common::podToHex(transactionInfo.hash);
        std::vector<uint8_t> secretMetadata;
        // if (isColdDeposit) {
        //   secretMetadata.assign(context->extra.begin(), context->extra.end());
        // }
        events.push_back(std::unique_ptr<WalletBurnDepositSecretCreatedEvent>(
          new WalletBurnDepositSecretCreatedEvent(txHashStr, commitKeys.keyScalar, depositAmount, secretMetadata)));
      }

      transactionInfo.firstDepositId = depositId;
      transactionInfo.depositCount = 1;

      // ── Finalize ─────────────────────────────────────────────────────
      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, totalAmount, context->selectedTransfers);

      m_transactionsCache.addCreatedDeposit(depositId, depositAmount);
      notifyBalanceChanged(events);

      std::vector<DepositId> deposits{depositId};
      return std::unique_ptr<WalletRequest>(new WalletRelayDepositTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayDepositTransactionCallback, this, context,
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

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendLegacyBondWithdrawTransaction(std::shared_ptr<SendTransactionContext> &&context,
                                                                                             std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
                                                                                             DepositId depositId,
                                                                                             uint64_t interest)
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

      // Interest claim 0xCC extra
      std::vector<uint8_t> extra;
      TransactionExtraLegacyBondClaim claim;
      claim.claimedInterest = interest;
      addLegacyBondClaimToExtra(extra, claim);
      transaction->appendExtra(extra);

      // Output amount = principal + interest - fee
      uint64_t totalAmount = context->foundMoney + interest;
      std::vector<uint64_t> outputAmounts = splitAmount(totalAmount - transactionInfo.fee, context->dustPolicy.dustThreshold);

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

      UnconfirmedSpentDepositDetails unconfirmed;
      unconfirmed.depositsSum = context->foundMoney;
      unconfirmed.fee = transactionInfo.fee;
      unconfirmed.transactionId = context->transactionId;
      m_transactionsCache.addDepositSpendingTransaction(transaction->getTransactionHash(), unconfirmed);

      return std::unique_ptr<WalletRelayDepositTransactionRequest>(new WalletRelayDepositTransactionRequest(lowlevelTransaction,
                                                                                                            std::bind(&WalletTransactionSender::relayDepositTransactionCallback, this, context, std::vector<DepositId>{depositId}, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
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

      // ── Calculate accrued CD interest for each deposit ──────────────────────
      // The blockchain debits the fee pool and vault CD_APY_POOL when
      // claimedInterest > 0 on a CommitmentSpend input.  We must compute
      // the correct interest via getCdInterest (→ CommitmentIndex epoch
      // accumulator) and include it in both the input and output.
      uint32_t currentHeight = m_node.getLastLocalBlockHeight();
      uint64_t totalInterest = 0;
      std::vector<uint64_t> perDepositInterest;
      perDepositInterest.reserve(depositIds.size());

      for (size_t i = 0; i < depositIds.size(); ++i) {
        Deposit dep;
        if (m_transactionsCache.getDeposit(depositIds[i], dep) && dep.term != parameters::HEAT_TERM) {
          // Non-HEAT commitment (COLD CD): claim accrued fee-pool interest.
          uint64_t interest = 0;
          std::error_code ec = m_node.getCdInterest(dep.amount,
              static_cast<uint32_t>(dep.height), currentHeight, interest);
          if (ec) {
            // If node can't compute (e.g. remote daemon), fall back to zero.
            // The blockchain will still accept the withdrawal at 0 interest.
            interest = 0;
          }
          perDepositInterest.push_back(interest);
          totalInterest += interest;
        } else {
          // HEAT_TERM burns: no interest accrues (permanent burn, no fee pool).
          perDepositInterest.push_back(0);
        }
      }

      // Output amount = principal + accrued interest − fee.
      std::vector<uint64_t> outputAmounts = splitAmount(
          context->foundMoney + totalInterest - transactionInfo.fee, context->dustPolicy.dustThreshold);
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
        csInput.claimedInterest = (depositIdx < perDepositInterest.size())
            ? perDepositInterest[depositIdx] : 0;
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
    if (context->depositTerm == parameters::HEAT_TERM) {
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

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeHeatMintV10Request(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t xfgBurned,
      uint64_t heatMinted,
      uint64_t fee,
      uint64_t mixIn)
  {
    throwIf(xfgBurned == 0 || heatMinted == 0, error::WRONG_AMOUNT);

    uint64_t neededMoney = getSumWithOverflowCheck(xfgBurned, fee);
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn;

    context->foundMoney = selectTransfersToSend(neededMoney, false, context->dustPolicy.dustThreshold, context->selectedTransfers);
    throwIf(context->foundMoney < neededMoney, error::WRONG_AMOUNT);

    context->isV10HeatMint = true;
    context->v10XfgBurned = xfgBurned;
    context->v10HeatMinted = heatMinted;
    context->dynamicRingSize = m_currency.isTestnet();  // allow bootstrap ring sizes on testnet

    transactionId = m_transactionsCache.addNewTransaction(neededMoney, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;

    // Build auth tag extra inline — will be appended in doSendHeatMintV10Transaction
    std::vector<uint8_t> extra;
    addHeatMintAuthToExtra(extra, xfgBurned, heatMinted);
    context->extra = std::string(extra.begin(), extra.end());

    Crypto::SecretKey transactionSK;
    return makeGetRandomOutsRequest(std::move(context), false, transactionSK);
  }

  // Split a HEAT amount into standard bill denominations so every output
  // at a given bill size pools into the same decoy set. Greedy largest-first.
  static std::vector<uint64_t> decomposeHeatIntoBills(uint64_t amount) {
    const auto& denoms = CryptoNote::parameters::HEAT_BILL_DENOMINATIONS;
    std::vector<uint64_t> bills;
    uint64_t rem = amount;
    for (uint64_t bill : denoms) {
      while (rem >= bill) {
        bills.push_back(bill);
        rem -= bill;
      }
    }
    if (rem > 0 && !bills.empty())
      bills.front() += rem;
    else if (bills.empty())
      bills.push_back(amount);
    return bills;
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendHeatMintV10Transaction(
      std::shared_ptr<SendTransactionContext> &&context,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t xfgBurned,
      uint64_t heatMinted)
  {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }

    try {
      WalletLegacyTransaction &transactionInfo = m_transactionsCache.getTransaction(context->transactionId);
      std::unique_ptr<ITransaction> transaction = createTransaction();
      uint64_t fee = m_currency.minimumFee();

      uint64_t changeAmount = context->foundMoney - xfgBurned - fee;
      std::vector<uint64_t> decomposedChange = splitAmount(changeAmount, context->dustPolicy.dustThreshold);

      // Build HEAT commitment outputs — split into bill denominations
      // so all mints at the same bill size share a decoy pool.
      std::vector<uint64_t> heatBills = decomposeHeatIntoBills(heatMinted);
      
      Crypto::SecretKey txSecretKey;
      transaction->getTransactionSecretKey(txSecretKey);
      Crypto::KeyDerivation ecdh;
      Crypto::generate_key_derivation(m_keys.address.viewPublicKey, txSecretKey, ecdh);

      for (uint64_t billAmount : heatBills) {
        const uint32_t commitOutputIndex = static_cast<uint32_t>(transaction->getOutputCount());
        std::array<uint8_t, 32> depositSecret;
        {
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
        CryptoNote::TransactionOutputCommitment heatOut;
        heatOut.commitKey = commitKeys.commitKey;
        heatOut.term = parameters::HEAT_TERM;
        transaction->addOutput(billAmount, heatOut);
      }

      // Change outputs
      for (uint64_t changeOut : decomposedChange)
        transaction->addOutput(changeOut, m_keys.address);
      transaction->setUnlockTime(0);

      // Build auth tag extra
      std::vector<uint8_t> extra;
      addHeatMintAuthToExtra(extra, xfgBurned, heatMinted);
      CryptoNote::BinaryArray extraData(extra.begin(), extra.end());
      transaction->appendExtra(extraData);

      // Inputs with KeyInput ring signing
      std::vector<TransactionSourceEntry> sources;
      prepareKeyInputs(context->selectedTransfers, context->outs, sources, context->mixIn);
      std::vector<TransactionTypes::InputKeyInfo> inputs = convertSources(std::vector<TransactionSourceEntry>(sources));

      std::vector<KeyPair> ephKeys;
      for (size_t i = 0; i < inputs.size(); ++i) {
        KeyPair ephKey;
        transaction->addInput(m_keys, inputs[i], ephKey);
        ephKeys.push_back(std::move(ephKey));
      }
      for (size_t i = 0; i < inputs.size(); ++i)
        transaction->signInputKey(i, inputs[i], ephKeys[i]);

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, xfgBurned, context->selectedTransfers);
      notifyBalanceChanged(events);

      return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    } catch (std::exception &e) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
      return {};
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeHeatDepositV10Request(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t amount,
      uint32_t termEpochs,
      uint64_t bankingFee,
      uint64_t fee,
      uint64_t mixIn)
  {
    throwIf(amount == 0, error::WRONG_AMOUNT);

    uint64_t neededHeat = amount + bankingFee;
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = m_currency.maxMixin();

    std::vector<DepositId> heatDepositIds;
    size_t depositCount = m_transactionsCache.getDepositCount();
    for (size_t i = 0; i < depositCount; ++i) {
      Deposit d;
      if (m_transactionsCache.getDeposit(i, d) && d.term == parameters::HEAT_TERM && !d.locked && d.spendingTransactionId == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
        heatDepositIds.push_back(i);
      }
    }

    uint64_t totalHeat = 0;
    std::vector<DepositId> selectedIds;
    for (auto id : heatDepositIds) {
      Deposit d;
      m_transactionsCache.getDeposit(id, d);
      totalHeat += d.amount;
      selectedIds.push_back(id);
      if (totalHeat >= neededHeat) break;
    }
    throwIf(totalHeat < neededHeat, error::WRONG_AMOUNT);

    context->foundMoney = selectDepositTransfers(selectedIds, context->selectedTransfers);
    throwIf(context->foundMoney < neededHeat, error::WRONG_AMOUNT);

    transactionId = m_transactionsCache.addNewTransaction(neededHeat, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;
    context->depositTerm = termEpochs;
    context->isV10AmmSwap = true;  // reuse flag for CD deposit routing
    context->v10SwapInput = amount;
    context->v10SwapOutput = bankingFee;
    setSpendingTransactionToDeposits(transactionId, selectedIds);

    uint64_t depositAmount = context->selectedTransfers.empty() ? 0 : context->selectedTransfers[0].amount;
    return makeGetRandomCommitmentOutsRequest(std::move(context), depositAmount, selectedIds);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeHeatTransferV10Request(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      const AccountPublicAddress& recipient,
      uint64_t amount,
      uint64_t fee,
      uint64_t mixIn)
  {
    throwIf(amount == 0, error::WRONG_AMOUNT);

    uint64_t neededHeat = amount;
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = m_currency.maxMixin();

    std::vector<DepositId> heatDepositIds;
    size_t depositCount = m_transactionsCache.getDepositCount();
    for (size_t i = 0; i < depositCount; ++i) {
      Deposit d;
      if (m_transactionsCache.getDeposit(i, d) && d.term == parameters::HEAT_TERM && !d.locked && d.spendingTransactionId == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
        heatDepositIds.push_back(i);
      }
    }

    uint64_t totalHeat = 0;
    std::vector<DepositId> selectedIds;
    for (auto id : heatDepositIds) {
      Deposit d;
      m_transactionsCache.getDeposit(id, d);
      totalHeat += d.amount;
      selectedIds.push_back(id);
      if (totalHeat >= neededHeat) break;
    }
    throwIf(totalHeat < neededHeat, error::WRONG_AMOUNT);

    context->foundMoney = selectDepositTransfers(selectedIds, context->selectedTransfers);
    throwIf(context->foundMoney < neededHeat, error::WRONG_AMOUNT);

    transactionId = m_transactionsCache.addNewTransaction(neededHeat, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;
    context->isV10HeatTransfer = true;
    context->v10HeatRecipient = recipient;
    context->v10HeatTransferAmount = amount;
    setSpendingTransactionToDeposits(transactionId, selectedIds);

    uint64_t depositAmount = context->selectedTransfers.empty() ? 0 : context->selectedTransfers[0].amount;
    return makeGetRandomCommitmentOutsRequest(std::move(context), depositAmount, selectedIds);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeLpAddV10Request(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t amountXfg,
      uint64_t amountHeat,
      uint64_t fee,
      uint64_t mixIn)
  {
    throwIf(amountXfg == 0 || amountHeat == 0, error::WRONG_AMOUNT);

    uint64_t neededXfg = getSumWithOverflowCheck(amountXfg, fee);
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn;

    context->foundMoney = selectTransfersToSend(neededXfg, false, context->dustPolicy.dustThreshold, context->selectedTransfers);
    throwIf(context->foundMoney < neededXfg, error::WRONG_AMOUNT);

    std::vector<DepositId> heatDepositIds;
    size_t depositCount = m_transactionsCache.getDepositCount();
    for (size_t i = 0; i < depositCount; ++i) {
      Deposit d;
      if (m_transactionsCache.getDeposit(i, d) && d.term == parameters::HEAT_TERM && !d.locked && d.spendingTransactionId == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
        heatDepositIds.push_back(i);
      }
    }

    uint64_t totalHeat = 0;
    std::vector<DepositId> selectedHeatIds;
    for (auto id : heatDepositIds) {
      Deposit d;
      m_transactionsCache.getDeposit(id, d);
      totalHeat += d.amount;
      selectedHeatIds.push_back(id);
      if (totalHeat >= amountHeat) break;
    }
    throwIf(totalHeat < amountHeat, error::WRONG_AMOUNT);

    selectDepositTransfers(selectedHeatIds, context->selectedTransfers);

    transactionId = m_transactionsCache.addNewTransaction(neededXfg + amountHeat, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;
    context->depositTerm = amountHeat;
    context->isV10LpAdd = true;
    context->v10XfgBurned = amountXfg;
    context->v10HeatMinted = amountHeat;
    setSpendingTransactionToDeposits(transactionId, selectedHeatIds);

    Crypto::SecretKey transactionSK;
    return makeGetRandomOutsRequest(std::move(context), false, transactionSK);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeLpRemoveV10Request(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t lpShares,
      uint64_t minXfg,
      uint64_t minHeat,
      uint64_t fee,
      uint64_t mixIn)
  {
    throwIf(lpShares == 0, error::WRONG_AMOUNT);

    // Find LP (DEPOSIT_TERM_LP) deposits
    std::vector<DepositId> lpDepositIds;
    size_t depositCount = m_transactionsCache.getDepositCount();
    for (size_t i = 0; i < depositCount; ++i) {
      Deposit d;
      if (m_transactionsCache.getDeposit(i, d) && d.term == parameters::DEPOSIT_TERM_LP && !d.locked && d.spendingTransactionId == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
        lpDepositIds.push_back(i);
      }
    }

    // Select LP deposits to burn
    uint64_t totalLp = 0;
    std::vector<DepositId> selectedIds;
    for (auto id : lpDepositIds) {
      Deposit d;
      m_transactionsCache.getDeposit(id, d);
      totalLp += d.amount;
      selectedIds.push_back(id);
      if (totalLp >= lpShares) break;
    }
    throwIf(totalLp < lpShares, error::WRONG_AMOUNT);

    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn;

    context->foundMoney = selectDepositTransfers(selectedIds, context->selectedTransfers);
    throwIf(context->foundMoney < lpShares, error::WRONG_AMOUNT);

    transactionId = m_transactionsCache.addNewTransaction(lpShares, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;
    context->isV10LpRemove = true;
    context->v10LpSharesBurned = lpShares;
    context->v10LpMinXfg = minXfg;
    context->v10LpMinHeat = minHeat;
    setSpendingTransactionToDeposits(transactionId, selectedIds);

    uint64_t depositAmount = context->selectedTransfers.empty() ? 0 : context->selectedTransfers[0].amount;
    return makeGetRandomCommitmentOutsRequest(std::move(context), depositAmount, selectedIds);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeAmmSwapV10Request(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint8_t direction,
      uint64_t inputAmount,
      uint64_t outputAmount,
      uint64_t minOutput,
      uint64_t fee,
      uint64_t mixIn)
  {
    throwIf(inputAmount == 0 || outputAmount == 0, error::WRONG_AMOUNT);

    // For direction=1 (HEAT→XFG): select HEAT commitment deposits, fetch commitment decoys.
    // For direction=0 (XFG→HEAT): standard KeyInput selection.
    if (direction == 1) {
      std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
      context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
      context->mixIn = m_currency.maxMixin();

      // Gather all HEAT (FOREVER term) deposit IDs from the wallet
      std::vector<DepositId> heatDepositIds;
      size_t depositCount = m_transactionsCache.getDepositCount();
      for (size_t i = 0; i < depositCount; ++i) {
        Deposit d;
        if (m_transactionsCache.getDeposit(i, d) && d.term == parameters::HEAT_TERM && !d.locked && d.spendingTransactionId == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
          heatDepositIds.push_back(i);
        }
      }

      uint64_t neededHeat = inputAmount;
      uint64_t totalSelected = 0;
      std::vector<DepositId> selectedIds;
      for (auto id : heatDepositIds) {
        Deposit d;
        m_transactionsCache.getDeposit(id, d);
        totalSelected += d.amount;
        selectedIds.push_back(id);
        if (totalSelected >= neededHeat) break;
      }
      throwIf(totalSelected < neededHeat, error::WRONG_AMOUNT);

      context->foundMoney = selectDepositTransfers(selectedIds, context->selectedTransfers);
      throwIf(context->foundMoney < neededHeat, error::WRONG_AMOUNT);

      transactionId = m_transactionsCache.addNewTransaction(neededHeat, fee, std::string(), {}, 0, {});
      context->transactionId = transactionId;
      context->depositTerm = direction;
      context->isV10AmmSwap = true;
      context->v10SwapDirection = direction;
      context->v10SwapInput = inputAmount;
      context->v10SwapOutput = outputAmount;
      context->v10SwapMinOutput = minOutput;
      setSpendingTransactionToDeposits(transactionId, selectedIds);

      uint64_t depositAmount = context->selectedTransfers.empty() ? 0 : context->selectedTransfers[0].amount;
      return makeGetRandomCommitmentOutsRequest(std::move(context), depositAmount, selectedIds);
    }

    // direction == 0 (XFG→HEAT): standard KeyInput selection
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn;

    uint64_t neededMoney = getSumWithOverflowCheck(inputAmount, fee);
    context->foundMoney = selectTransfersToSend(neededMoney, false, context->dustPolicy.dustThreshold, context->selectedTransfers);
    throwIf(context->foundMoney < neededMoney, error::WRONG_AMOUNT);

    transactionId = m_transactionsCache.addNewTransaction(neededMoney, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;
    context->isV10AmmSwap = true;
    context->v10SwapDirection = direction;
    context->v10SwapInput = inputAmount;
    context->v10SwapOutput = outputAmount;
    context->v10SwapMinOutput = minOutput;

    uint64_t depositAmount = context->selectedTransfers.empty() ? 0 : context->selectedTransfers[0].amount;
    return makeGetRandomCommitmentOutsRequest(std::move(context), depositAmount, std::vector<DepositId>());
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeLpClaimFeesRequest(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t lpShares,
      uint64_t minXfg,
      uint64_t minHeat,
      uint64_t fee,
      uint64_t mixIn)
  {
    throwIf(lpShares == 0, error::WRONG_AMOUNT);

    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn;

    // Fee claim: select a small XFG output to pay the network fee
    // The actual claimed fees come from the pool, not from user inputs
    context->foundMoney = selectTransfersToSend(fee, false, context->dustPolicy.dustThreshold, context->selectedTransfers);
    throwIf(context->foundMoney < fee, error::WRONG_AMOUNT);

    transactionId = m_transactionsCache.addNewTransaction(0, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;
    context->isV10LpClaim = true;
    context->v10LpClaimShares = lpShares;
    context->v10LpClaimMinXfg = minXfg;
    context->v10LpClaimMinHeat = minHeat;

    // Append TransactionExtraAmmClaim to the cached transaction extra
    WalletLegacyTransaction &txInfo = m_transactionsCache.getTransaction(transactionId);
    std::vector<uint8_t> extra;
    addAmmClaimToExtra(extra, lpShares, minXfg, minHeat);
    txInfo.extra.insert(txInfo.extra.end(), extra.begin(), extra.end());

    Crypto::SecretKey transactionSK = reinterpret_cast<const Crypto::SecretKey&>(txInfo.secretKey);
    return makeGetRandomOutsRequest(std::move(context), false, transactionSK);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendAmmSwapV10Transaction(
      std::shared_ptr<SendTransactionContext> &&context,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint8_t direction,
      uint64_t inputAmount,
      uint64_t outputAmount,
      uint64_t minOutput)
  {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }

    try {
      std::unique_ptr<ITransaction> transaction = createTransaction();
      uint64_t fee = m_currency.minimumFee();

      uint64_t changeAmount = context->foundMoney - inputAmount - fee;
      std::vector<uint64_t> decomposedChange = splitAmount(changeAmount, context->dustPolicy.dustThreshold);

      Crypto::PublicKey poolKey = computePoolCommitKey();

      // Build pool-deposit commitment output
      uint32_t poolTerm = (direction == 0) ? parameters::DEPOSIT_TERM_POOL_XFG : parameters::DEPOSIT_TERM_POOL_HEAT;
      CryptoNote::TransactionOutputCommitment poolOut;
      poolOut.commitKey = poolKey;
      poolOut.term = poolTerm;
      transaction->addOutput(inputAmount, poolOut);

      // Build user-receive commitment output
      uint32_t receiveTerm = (direction == 0) ? parameters::HEAT_TERM : parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG;
      const uint32_t commitIdx = static_cast<uint32_t>(transaction->getOutputCount());
      std::array<uint8_t, 32> receiveSecret;
      {
        Crypto::SecretKey txSecretKey;
        transaction->getTransactionSecretKey(txSecretKey);
        Crypto::KeyDerivation ecdh;
        Crypto::generate_key_derivation(m_keys.address.viewPublicKey, txSecretKey, ecdh);
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        preimage[32] = commitIdx & 0xFF;
        preimage[33] = (commitIdx >> 8) & 0xFF;
        preimage[34] = (commitIdx >> 16) & 0xFF;
        preimage[35] = (commitIdx >> 24) & 0xFF;
        Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
        memcpy(receiveSecret.data(), h.data, 32);
      }
      CryptoNote::DepositCommitmentKeys receiveKeys = CryptoNote::deriveCommitmentKeys(receiveSecret);
      CryptoNote::TransactionOutputCommitment receiveOut;
      receiveOut.commitKey = receiveKeys.commitKey;
      receiveOut.term = receiveTerm;
      transaction->addOutput(outputAmount, receiveOut);

      // Change outputs
      for (uint64_t changeOut : decomposedChange)
        transaction->addOutput(changeOut, m_keys.address);
      transaction->setUnlockTime(0);

      // Auth tag extra
      std::vector<uint8_t> extra;
      addAmmSwapAuthToExtra(extra, direction, inputAmount, outputAmount, minOutput);
      CryptoNote::BinaryArray extraData(extra.begin(), extra.end());
      transaction->appendExtra(extraData);

      // Inputs with KeyInput ring signing
      std::vector<TransactionSourceEntry> sources;
      prepareKeyInputs(context->selectedTransfers, context->outs, sources, context->mixIn);
      std::vector<TransactionTypes::InputKeyInfo> inputs = convertSources(std::vector<TransactionSourceEntry>(sources));

      std::vector<KeyPair> ephKeys;
      for (size_t i = 0; i < inputs.size(); ++i) {
        KeyPair ephKey;
        transaction->addInput(m_keys, inputs[i], ephKey);
        ephKeys.push_back(std::move(ephKey));
      }
      for (size_t i = 0; i < inputs.size(); ++i)
        transaction->signInputKey(i, inputs[i], ephKeys[i]);

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, inputAmount, context->selectedTransfers);
      notifyBalanceChanged(events);

      return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    } catch (std::exception &e) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
      return {};
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendAmmSwapV10CommitmentTransaction(
      std::shared_ptr<SendTransactionContext> &&context,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint8_t direction,
      uint64_t inputAmount,
      uint64_t outputAmount,
      uint64_t minOutput)
  {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }

    try {
      std::unique_ptr<ITransaction> transaction = createTransaction();
      uint64_t fee = m_currency.minimumFee();

      // Build pool-deposit HEAT output (POOL_HEAT term, pool commitKey)
      Crypto::PublicKey poolKey = computePoolCommitKey();
      CryptoNote::TransactionOutputCommitment poolOut;
      poolOut.commitKey = poolKey;
      poolOut.term = parameters::DEPOSIT_TERM_POOL_HEAT;
      transaction->addOutput(inputAmount, poolOut);

      // Build user-receive XFG output (SWRX term, user's commitKey)
      const uint32_t commitIdx = static_cast<uint32_t>(transaction->getOutputCount());
      uint64_t netXfg = (outputAmount > fee) ? (outputAmount - fee) : 0;
      if (netXfg > 0) {
        std::array<uint8_t, 32> receiveSecret;
        {
          Crypto::SecretKey txSecretKey;
          transaction->getTransactionSecretKey(txSecretKey);
          Crypto::KeyDerivation ecdh;
          Crypto::generate_key_derivation(m_keys.address.viewPublicKey, txSecretKey, ecdh);
          uint8_t preimage[36];
          memcpy(preimage, &ecdh, 32);
          preimage[32] = commitIdx & 0xFF;
          preimage[33] = (commitIdx >> 8) & 0xFF;
          preimage[34] = (commitIdx >> 16) & 0xFF;
          preimage[35] = (commitIdx >> 24) & 0xFF;
          Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
          memcpy(receiveSecret.data(), h.data, 32);
        }
        CryptoNote::DepositCommitmentKeys receiveKeys = CryptoNote::deriveCommitmentKeys(receiveSecret);
        CryptoNote::TransactionOutputCommitment receiveOut;
        receiveOut.commitKey = receiveKeys.commitKey;
        receiveOut.term = parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG;
        transaction->addOutput(netXfg, receiveOut);
      }

      // HEAT change output (if selected HEAT > inputAmount)
      uint64_t totalHeat = context->foundMoney;
      if (totalHeat > inputAmount) {
        uint64_t changeHeat = totalHeat - inputAmount;
        const uint32_t chIdx = static_cast<uint32_t>(transaction->getOutputCount());
        std::array<uint8_t, 32> changeSecret;
        {
          Crypto::SecretKey txSecretKey;
          transaction->getTransactionSecretKey(txSecretKey);
          Crypto::KeyDerivation ecdh;
          Crypto::generate_key_derivation(m_keys.address.viewPublicKey, txSecretKey, ecdh);
          uint8_t preimage[36];
          memcpy(preimage, &ecdh, 32);
          preimage[32] = chIdx & 0xFF;
          preimage[33] = (chIdx >> 8) & 0xFF;
          preimage[34] = (chIdx >> 16) & 0xFF;
          preimage[35] = (chIdx >> 24) & 0xFF;
          Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
          memcpy(changeSecret.data(), h.data, 32);
        }
        CryptoNote::DepositCommitmentKeys cKeys = CryptoNote::deriveCommitmentKeys(changeSecret);
        CryptoNote::TransactionOutputCommitment changeOut;
        changeOut.commitKey = cKeys.commitKey;
        changeOut.term = parameters::HEAT_TERM;
        transaction->addOutput(changeHeat, changeOut);
      }

      transaction->setUnlockTime(0);

      // Auth tag extra
      std::vector<uint8_t> extra;
      addAmmSwapAuthToExtra(extra, direction, inputAmount, outputAmount, minOutput);
      CryptoNote::BinaryArray extraData(extra.begin(), extra.end());
      transaction->appendExtra(extraData);

      // Build commitment spend inputs from selected HEAT deposits
      const size_t ringSize = static_cast<size_t>(context->mixIn);
      const auto& decoys = context->commitmentOuts;
      for (size_t depositIdx = 0; depositIdx < context->selectedTransfers.size(); ++depositIdx) {
        const TransactionOutputInformation& transfer = context->selectedTransfers[depositIdx];

        // Re-derive key scalar via ECDH
        Crypto::KeyDerivation ecdh;
        Crypto::generate_key_derivation(transfer.transactionPublicKey, m_keys.viewSecretKey, ecdh);
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
        commitmentKeyPair.publicKey = commitKeys.commitKey;
        commitmentKeyPair.secretKey = commitKeys.keyScalar;

        // Filter decoys
        std::vector<CryptoNote::COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry> filteredDecoys;
        for (const auto& d : decoys) {
          if (d.global_amount_index != static_cast<uint32_t>(transfer.globalOutputIndex)) {
            filteredDecoys.push_back(d);
          }
        }
        const size_t numDecoys = std::min(filteredDecoys.size(), ringSize - 1);
        const size_t actualRingSize = numDecoys + 1;
        const size_t realPos = Crypto::rand<size_t>() % actualRingSize;

        // Build ring
        std::vector<uint32_t> absIndices;
        std::vector<const Crypto::PublicKey*> ringKeys;
        size_t decoyPos = 0;
        for (size_t slot = 0; slot < actualRingSize; ++slot) {
          if (slot == realPos) {
            absIndices.push_back(transfer.globalOutputIndex);
            ringKeys.push_back(&commitmentKeyPair.publicKey);
          } else {
            absIndices.push_back(filteredDecoys[decoyPos].global_amount_index);
            ringKeys.push_back(&filteredDecoys[decoyPos].commit_key);
            ++decoyPos;
          }
        }

        // Sort by global index
        std::vector<size_t> order(actualRingSize);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
          return absIndices[a] < absIndices[b];
        });
        size_t sortedRealPos = 0;
        std::vector<uint32_t> sortedAbs(actualRingSize);
        std::vector<const Crypto::PublicKey*> sortedKeys(actualRingSize);
        for (size_t s = 0; s < actualRingSize; ++s) {
          sortedAbs[s] = absIndices[order[s]];
          sortedKeys[s] = ringKeys[order[s]];
          if (order[s] == realPos) sortedRealPos = s;
        }

        // Convert to relative offsets
        std::vector<uint32_t> relOffsets(actualRingSize);
        relOffsets[0] = sortedAbs[0];
        for (size_t s = 1; s < actualRingSize; ++s)
          relOffsets[s] = sortedAbs[s] - sortedAbs[s - 1];

        // Build and sign commitment input
        TransactionInputCommitmentSpend csInput;
        csInput.amount = transfer.amount;
        csInput.outputIndexes = relOffsets;
        csInput.keyImage = commitKeys.keyImage;
        transaction->addInput(csInput);
        transaction->signInputCommitmentSpend(depositIdx, sortedKeys, commitmentKeyPair, sortedRealPos);
      }

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, inputAmount, context->selectedTransfers);
      notifyBalanceChanged(events);

      return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    } catch (std::exception &e) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
      return {};
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendLpAddV10Transaction(
      std::shared_ptr<SendTransactionContext> &&context,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t amountXfg,
      uint64_t amountHeat)
  {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }
    try {
      std::unique_ptr<ITransaction> transaction = createTransaction();
      uint64_t fee = m_currency.minimumFee();

      uint64_t xfgChange = context->foundMoney - amountXfg - fee;
      std::vector<uint64_t> decomposedChange = splitAmount(xfgChange, context->dustPolicy.dustThreshold);

      // Compute LP shares: for first deposit, shares = sqrt(amountXfg * amountHeat)
      // For simplicity, shares = amountXfg + amountHeat (proportional fallback)
      uint64_t computedShares = amountXfg + amountHeat;

      // XFG change outputs
      for (uint64_t changeOut : decomposedChange)
        transaction->addOutput(changeOut, m_keys.address);

      // LP shares commitment output (term=LP)
      const uint32_t lpIdx = static_cast<uint32_t>(transaction->getOutputCount());
      std::array<uint8_t, 32> lpSecret;
      {
        Crypto::SecretKey txSecretKey;
        transaction->getTransactionSecretKey(txSecretKey);
        Crypto::KeyDerivation ecdh;
        Crypto::generate_key_derivation(m_keys.address.viewPublicKey, txSecretKey, ecdh);
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        preimage[32] = lpIdx & 0xFF; preimage[33] = (lpIdx >> 8) & 0xFF;
        preimage[34] = (lpIdx >> 16) & 0xFF; preimage[35] = (lpIdx >> 24) & 0xFF;
        Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
        memcpy(lpSecret.data(), h.data, 32);
      }
      CryptoNote::DepositCommitmentKeys lpKeys = CryptoNote::deriveCommitmentKeys(lpSecret);
      CryptoNote::TransactionOutputCommitment lpOut;
      lpOut.commitKey = lpKeys.commitKey;
      lpOut.term = parameters::DEPOSIT_TERM_LP;
      transaction->addOutput(computedShares, lpOut);
      transaction->setUnlockTime(0);

      // Auth tag extra
      std::vector<uint8_t> extra;
      addLpAddAuthToExtra(extra, amountXfg, amountHeat, computedShares);
      CryptoNote::BinaryArray extraData(extra.begin(), extra.end());
      transaction->appendExtra(extraData);

      // XFG KeyInput ring signing
      std::vector<TransactionSourceEntry> sources;
      prepareKeyInputs(context->selectedTransfers, context->outs, sources, context->mixIn);
      std::vector<TransactionTypes::InputKeyInfo> inputs = convertSources(std::vector<TransactionSourceEntry>(sources));

      std::vector<KeyPair> ephKeys;
      for (size_t i = 0; i < inputs.size(); ++i) {
        KeyPair ephKey;
        transaction->addInput(m_keys, inputs[i], ephKey);
        ephKeys.push_back(std::move(ephKey));
      }
      for (size_t i = 0; i < inputs.size(); ++i)
        transaction->signInputKey(i, inputs[i], ephKeys[i]);

      // HEAT commitment spend inputs
      const size_t ringSize = static_cast<size_t>(context->mixIn);
      const auto& decoys = context->commitmentOuts;
      size_t commitmentDepositIdx = 0;
      for (size_t depIdx = 0; depIdx < context->selectedTransfers.size(); ++depIdx) {
        const auto& transfer = context->selectedTransfers[depIdx];
        if (transfer.type != TransactionTypes::OutputType::Commitment) continue;
        if (transfer.term != parameters::HEAT_TERM) continue;

        Crypto::KeyDerivation ecdh;
        Crypto::generate_key_derivation(transfer.transactionPublicKey, m_keys.viewSecretKey, ecdh);
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        const uint32_t outIdx = transfer.outputInTransaction;
        preimage[32] = outIdx & 0xFF; preimage[33] = (outIdx >> 8) & 0xFF;
        preimage[34] = (outIdx >> 16) & 0xFF; preimage[35] = (outIdx >> 24) & 0xFF;
        Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
        std::array<uint8_t, 32> depositSecret;
        memcpy(depositSecret.data(), h.data, 32);
        CryptoNote::DepositCommitmentKeys ck = CryptoNote::deriveCommitmentKeys(depositSecret);
        KeyPair commitmentKeyPair = {ck.commitKey, ck.keyScalar};

        std::vector<CryptoNote::COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry> filteredDecoys;
        for (const auto& d : decoys)
          if (d.global_amount_index != static_cast<uint32_t>(transfer.globalOutputIndex))
            filteredDecoys.push_back(d);
        const size_t numDecoys = std::min(filteredDecoys.size(), ringSize - 1);
        const size_t actualRing = numDecoys + 1;
        const size_t realPos = Crypto::rand<size_t>() % actualRing;

        std::vector<uint32_t> absIndices;
        std::vector<const Crypto::PublicKey*> ringKeys;
        size_t decoyPos = 0;
        for (size_t slot = 0; slot < actualRing; ++slot) {
          if (slot == realPos) {
            absIndices.push_back(transfer.globalOutputIndex);
            ringKeys.push_back(&commitmentKeyPair.publicKey);
          } else {
            absIndices.push_back(filteredDecoys[decoyPos].global_amount_index);
            ringKeys.push_back(&filteredDecoys[decoyPos].commit_key);
            ++decoyPos;
          }
        }

        std::vector<size_t> order(actualRing);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return absIndices[a] < absIndices[b]; });
        size_t sortedRealPos = 0;
        std::vector<uint32_t> sortedAbs(actualRing);
        std::vector<const Crypto::PublicKey*> sortedKeys(actualRing);
        for (size_t s = 0; s < actualRing; ++s) {
          sortedAbs[s] = absIndices[order[s]];
          sortedKeys[s] = ringKeys[order[s]];
          if (order[s] == realPos) sortedRealPos = s;
        }

        std::vector<uint32_t> relOffsets(actualRing);
        relOffsets[0] = sortedAbs[0];
        for (size_t s = 1; s < actualRing; ++s) relOffsets[s] = sortedAbs[s] - sortedAbs[s - 1];

        TransactionInputCommitmentSpend csInput;
        csInput.amount = transfer.amount;
        csInput.outputIndexes = relOffsets;
        csInput.keyImage = ck.keyImage;
        transaction->addInput(csInput);
        transaction->signInputCommitmentSpend(commitmentDepositIdx, sortedKeys, commitmentKeyPair, sortedRealPos);
        commitmentDepositIdx++;
      }

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, amountXfg + amountHeat, context->selectedTransfers);
      notifyBalanceChanged(events);

      return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    } catch (std::exception &e) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
      return {};
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendLpRemoveV10Transaction(
      std::shared_ptr<SendTransactionContext> &&context,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t lpSharesBurned,
      uint64_t minXfg,
      uint64_t minHeat)
  {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }
    try {
      std::unique_ptr<ITransaction> transaction = createTransaction();
      uint64_t fee = m_currency.minimumFee();

      // Auth tag extra: TransactionExtraAmmRemoveLiquidity
      std::vector<uint8_t> extra;
      addAmmRemoveLiquidityToExtra(extra, lpSharesBurned, minXfg, minHeat);
      CryptoNote::BinaryArray extraData(extra.begin(), extra.end());
      transaction->appendExtra(extraData);

      transaction->setUnlockTime(0);

      // Build commitment spend inputs from selected LP deposits
      const size_t ringSize = static_cast<size_t>(context->mixIn);
      const auto& decoys = context->commitmentOuts;
      size_t commitmentDepositIdx = 0;
      for (size_t depIdx = 0; depIdx < context->selectedTransfers.size(); ++depIdx) {
        const auto& transfer = context->selectedTransfers[depIdx];
        if (transfer.type != TransactionTypes::OutputType::Commitment) continue;
        if (transfer.term != parameters::DEPOSIT_TERM_LP) continue;

        // Re-derive key scalar via ECDH
        Crypto::KeyDerivation ecdh;
        Crypto::generate_key_derivation(transfer.transactionPublicKey, m_keys.viewSecretKey, ecdh);
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        const uint32_t outIdx = transfer.outputInTransaction;
        preimage[32] = outIdx & 0xFF; preimage[33] = (outIdx >> 8) & 0xFF;
        preimage[34] = (outIdx >> 16) & 0xFF; preimage[35] = (outIdx >> 24) & 0xFF;
        Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
        std::array<uint8_t, 32> depositSecret;
        memcpy(depositSecret.data(), h.data, 32);
        CryptoNote::DepositCommitmentKeys ck = CryptoNote::deriveCommitmentKeys(depositSecret);
        KeyPair commitmentKeyPair = {ck.commitKey, ck.keyScalar};

        // Filter decoys
        std::vector<CryptoNote::COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry> filteredDecoys;
        for (const auto& d : decoys)
          if (d.global_amount_index != static_cast<uint32_t>(transfer.globalOutputIndex))
            filteredDecoys.push_back(d);
        const size_t numDecoys = std::min(filteredDecoys.size(), ringSize - 1);
        const size_t actualRing = numDecoys + 1;
        const size_t realPos = Crypto::rand<size_t>() % actualRing;

        // Build ring
        std::vector<uint32_t> absIndices;
        std::vector<const Crypto::PublicKey*> ringKeys;
        size_t decoyPos = 0;
        for (size_t slot = 0; slot < actualRing; ++slot) {
          if (slot == realPos) {
            absIndices.push_back(transfer.globalOutputIndex);
            ringKeys.push_back(&commitmentKeyPair.publicKey);
          } else {
            absIndices.push_back(filteredDecoys[decoyPos].global_amount_index);
            ringKeys.push_back(&filteredDecoys[decoyPos].commit_key);
            ++decoyPos;
          }
        }

        // Sort by global index
        std::vector<size_t> order(actualRing);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return absIndices[a] < absIndices[b]; });
        size_t sortedRealPos = 0;
        std::vector<uint32_t> sortedAbs(actualRing);
        std::vector<const Crypto::PublicKey*> sortedKeys(actualRing);
        for (size_t s = 0; s < actualRing; ++s) {
          sortedAbs[s] = absIndices[order[s]];
          sortedKeys[s] = ringKeys[order[s]];
          if (order[s] == realPos) sortedRealPos = s;
        }

        // Convert to relative offsets
        std::vector<uint32_t> relOffsets(actualRing);
        relOffsets[0] = sortedAbs[0];
        for (size_t s = 1; s < actualRing; ++s) relOffsets[s] = sortedAbs[s] - sortedAbs[s - 1];

        // Build and sign commitment input
        TransactionInputCommitmentSpend csInput;
        csInput.amount = transfer.amount;
        csInput.outputIndexes = relOffsets;
        csInput.keyImage = ck.keyImage;
        transaction->addInput(csInput);
        transaction->signInputCommitmentSpend(commitmentDepositIdx, sortedKeys, commitmentKeyPair, sortedRealPos);
        commitmentDepositIdx++;
      }

      // Change: return fee remainder to self as KeyOutput
      uint64_t totalLp = context->foundMoney;
      if (totalLp > lpSharesBurned) {
        uint64_t changeAmount = totalLp - lpSharesBurned;
        std::vector<uint64_t> decomposedChange = splitAmount(changeAmount, context->dustPolicy.dustThreshold);
        for (uint64_t changeOut : decomposedChange)
          transaction->addOutput(changeOut, m_keys.address);
      }

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, lpSharesBurned, context->selectedTransfers);
      notifyBalanceChanged(events);

      return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    } catch (std::exception &e) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
      return {};
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendLpClaimFeesTransaction(
      std::shared_ptr<SendTransactionContext> &&context,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t lpShares,
      uint64_t minXfg,
      uint64_t minHeat)
  {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }
    try {
      // Fee-only claim: extra already set in makeLpClaimFeesRequest.
      // Use standard key-input signing path.
      WalletLegacyTransaction &txInfo = m_transactionsCache.getTransaction(context->transactionId);
      Crypto::SecretKey transactionSK = reinterpret_cast<const Crypto::SecretKey&>(txInfo.secretKey);
      return doSendTransaction(std::move(context), events, transactionSK);
    } catch (std::exception &e) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
      return {};
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendHeatDepositV10Transaction(
      std::shared_ptr<SendTransactionContext> &&context,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t cdAmount,
      uint64_t bankingFee,
      uint32_t termEpochs)
  {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }
    try {
      std::unique_ptr<ITransaction> transaction = createTransaction();
      uint64_t totalHeat = context->foundMoney;
      uint64_t needed = cdAmount + bankingFee;
      uint64_t changeAmount = (totalHeat > needed) ? (totalHeat - needed) : 0;

      // HEAT CD commitment output (finite term)
      uint32_t cdTermBlocks = termEpochs * parameters::TESTNET_EPOCH_DURATION_BLOCKS; // TODO: real epoch mapping
      const uint32_t cdIdx = static_cast<uint32_t>(transaction->getOutputCount());
      std::array<uint8_t, 32> cdSecret;
      {
        Crypto::SecretKey txSecretKey;
        transaction->getTransactionSecretKey(txSecretKey);
        Crypto::KeyDerivation ecdh;
        Crypto::generate_key_derivation(m_keys.address.viewPublicKey, txSecretKey, ecdh);
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        preimage[32] = cdIdx & 0xFF; preimage[33] = (cdIdx >> 8) & 0xFF;
        preimage[34] = (cdIdx >> 16) & 0xFF; preimage[35] = (cdIdx >> 24) & 0xFF;
        Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
        memcpy(cdSecret.data(), h.data, 32);
      }
      CryptoNote::DepositCommitmentKeys cdKeys = CryptoNote::deriveCommitmentKeys(cdSecret);
      CryptoNote::TransactionOutputCommitment cdOut;
      cdOut.commitKey = cdKeys.commitKey;
      cdOut.term = cdTermBlocks;
      transaction->addOutput(cdAmount, cdOut);

      // Banking fee to dev fund (regular KeyOutput)
      AccountPublicAddress devAddr;
      m_currency.parseAccountAddressString(FUEGO_DEV_FUND_ADDRESS, devAddr);
      transaction->addOutput(bankingFee, devAddr);

      // HEAT change (FOREVER term)
      if (changeAmount > 0) {
        const uint32_t chIdx = static_cast<uint32_t>(transaction->getOutputCount());
        std::array<uint8_t, 32> chSecret;
        {
          Crypto::SecretKey txSecretKey;
          transaction->getTransactionSecretKey(txSecretKey);
          Crypto::KeyDerivation ecdh;
          Crypto::generate_key_derivation(m_keys.address.viewPublicKey, txSecretKey, ecdh);
          uint8_t preimage[36];
          memcpy(preimage, &ecdh, 32);
          preimage[32] = chIdx & 0xFF; preimage[33] = (chIdx >> 8) & 0xFF;
          preimage[34] = (chIdx >> 16) & 0xFF; preimage[35] = (chIdx >> 24) & 0xFF;
          Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
          memcpy(chSecret.data(), h.data, 32);
        }
        CryptoNote::DepositCommitmentKeys cKeys = CryptoNote::deriveCommitmentKeys(chSecret);
        CryptoNote::TransactionOutputCommitment chOut;
        chOut.commitKey = cKeys.commitKey;
        chOut.term = parameters::HEAT_TERM;
        transaction->addOutput(changeAmount, chOut);
      }

      transaction->setUnlockTime(0);

      // HEAT commitment spend inputs
      const size_t ringSize = static_cast<size_t>(context->mixIn);
      const auto& decoys = context->commitmentOuts;
      size_t commitmentDepositIdx = 0;
      for (size_t depIdx = 0; depIdx < context->selectedTransfers.size(); ++depIdx) {
        const auto& transfer = context->selectedTransfers[depIdx];
        if (transfer.type != TransactionTypes::OutputType::Commitment) continue;

        Crypto::KeyDerivation ecdh;
        Crypto::generate_key_derivation(transfer.transactionPublicKey, m_keys.viewSecretKey, ecdh);
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        const uint32_t outIdx = transfer.outputInTransaction;
        preimage[32] = outIdx & 0xFF; preimage[33] = (outIdx >> 8) & 0xFF;
        preimage[34] = (outIdx >> 16) & 0xFF; preimage[35] = (outIdx >> 24) & 0xFF;
        Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
        std::array<uint8_t, 32> depositSecret;
        memcpy(depositSecret.data(), h.data, 32);
        CryptoNote::DepositCommitmentKeys ck = CryptoNote::deriveCommitmentKeys(depositSecret);
        KeyPair commitmentKeyPair = {ck.commitKey, ck.keyScalar};

        std::vector<CryptoNote::COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry> filteredDecoys;
        for (const auto& d : decoys)
          if (d.global_amount_index != static_cast<uint32_t>(transfer.globalOutputIndex))
            filteredDecoys.push_back(d);
        const size_t numDecoys = std::min(filteredDecoys.size(), ringSize - 1);
        const size_t actualRing = numDecoys + 1;
        if (actualRing == 0) continue;
        const size_t realPos = Crypto::rand<size_t>() % actualRing;

        std::vector<uint32_t> absIndices;
        std::vector<const Crypto::PublicKey*> ringKeys;
        size_t decoyPos = 0;
        for (size_t slot = 0; slot < actualRing; ++slot) {
          if (slot == realPos) {
            absIndices.push_back(transfer.globalOutputIndex);
            ringKeys.push_back(&commitmentKeyPair.publicKey);
          } else {
            absIndices.push_back(filteredDecoys[decoyPos].global_amount_index);
            ringKeys.push_back(&filteredDecoys[decoyPos].commit_key);
            ++decoyPos;
          }
        }

        std::vector<size_t> order(actualRing);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return absIndices[a] < absIndices[b]; });
        size_t sortedRealPos = 0;
        std::vector<uint32_t> sortedAbs(actualRing);
        std::vector<const Crypto::PublicKey*> sortedKeys(actualRing);
        for (size_t s = 0; s < actualRing; ++s) {
          sortedAbs[s] = absIndices[order[s]];
          sortedKeys[s] = ringKeys[order[s]];
          if (order[s] == realPos) sortedRealPos = s;
        }

        std::vector<uint32_t> relOffsets(actualRing);
        relOffsets[0] = sortedAbs[0];
        for (size_t s = 1; s < actualRing; ++s) relOffsets[s] = sortedAbs[s] - sortedAbs[s - 1];

        TransactionInputCommitmentSpend csInput;
        csInput.amount = transfer.amount;
        csInput.outputIndexes = relOffsets;
        csInput.keyImage = ck.keyImage;
        transaction->addInput(csInput);
        transaction->signInputCommitmentSpend(commitmentDepositIdx, sortedKeys, commitmentKeyPair, sortedRealPos);
        commitmentDepositIdx++;
      }

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, cdAmount, context->selectedTransfers);
      notifyBalanceChanged(events);

      return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    } catch (std::exception &e) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
      return {};
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendHeatTransferV10Transaction(
      std::shared_ptr<SendTransactionContext> &&context,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      const AccountPublicAddress& recipient,
      uint64_t amount)
  {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }
    try {
      std::unique_ptr<ITransaction> transaction = createTransaction();
      uint64_t totalHeat = context->foundMoney;
      uint64_t changeAmount = (totalHeat > amount) ? (totalHeat - amount) : 0;

      // HEAT commitment output for recipient (FOREVER term, using recipient's view key)
      const uint32_t rcptIdx = static_cast<uint32_t>(transaction->getOutputCount());
      std::array<uint8_t, 32> rcptSecret;
      {
        Crypto::SecretKey txSecretKey;
        transaction->getTransactionSecretKey(txSecretKey);
        Crypto::KeyDerivation ecdh;
        Crypto::generate_key_derivation(recipient.viewPublicKey, txSecretKey, ecdh);
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        preimage[32] = rcptIdx & 0xFF; preimage[33] = (rcptIdx >> 8) & 0xFF;
        preimage[34] = (rcptIdx >> 16) & 0xFF; preimage[35] = (rcptIdx >> 24) & 0xFF;
        Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
        memcpy(rcptSecret.data(), h.data, 32);
      }
      CryptoNote::DepositCommitmentKeys rcptKeys = CryptoNote::deriveCommitmentKeys(rcptSecret);
      CryptoNote::TransactionOutputCommitment rcptOut;
      rcptOut.commitKey = rcptKeys.commitKey;
      rcptOut.term = parameters::HEAT_TERM;
      transaction->addOutput(amount, rcptOut);

      // HEAT change (FOREVER term, sender's view key)
      if (changeAmount > 0) {
        const uint32_t chIdx = static_cast<uint32_t>(transaction->getOutputCount());
        std::array<uint8_t, 32> chSecret;
        {
          Crypto::SecretKey txSecretKey;
          transaction->getTransactionSecretKey(txSecretKey);
          Crypto::KeyDerivation ecdh;
          Crypto::generate_key_derivation(m_keys.address.viewPublicKey, txSecretKey, ecdh);
          uint8_t preimage[36];
          memcpy(preimage, &ecdh, 32);
          preimage[32] = chIdx & 0xFF; preimage[33] = (chIdx >> 8) & 0xFF;
          preimage[34] = (chIdx >> 16) & 0xFF; preimage[35] = (chIdx >> 24) & 0xFF;
          Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
          memcpy(chSecret.data(), h.data, 32);
        }
        CryptoNote::DepositCommitmentKeys cKeys = CryptoNote::deriveCommitmentKeys(chSecret);
        CryptoNote::TransactionOutputCommitment chOut;
        chOut.commitKey = cKeys.commitKey;
        chOut.term = parameters::HEAT_TERM;
        transaction->addOutput(changeAmount, chOut);
      }

      transaction->setUnlockTime(0);

      // v12 HEAT send auth — declares HEAT amount for per-asset balance verification.
      // Format: tag 0xF9 + 8 bytes little-endian heatAmount.
      std::vector<uint8_t> extra;
      uint64_t heatAmountLE = amount;
      extra.push_back(TX_EXTRA_HEAT_SEND_AUTH);
      for (size_t b = 0; b < sizeof(uint64_t); ++b) {
        extra.push_back(static_cast<uint8_t>(heatAmountLE & 0xFF));
        heatAmountLE >>= 8;
      }
      CryptoNote::BinaryArray extraData(extra.begin(), extra.end());
      transaction->appendExtra(extraData);

      // HEAT commitment spend inputs
      const size_t ringSize = static_cast<size_t>(context->mixIn);
      const auto& decoys = context->commitmentOuts;
      size_t commitmentDepositIdx = 0;
      for (size_t depIdx = 0; depIdx < context->selectedTransfers.size(); ++depIdx) {
        const auto& transfer = context->selectedTransfers[depIdx];
        if (transfer.type != TransactionTypes::OutputType::Commitment) continue;

        Crypto::KeyDerivation ecdh;
        Crypto::generate_key_derivation(transfer.transactionPublicKey, m_keys.viewSecretKey, ecdh);
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        const uint32_t outIdx = transfer.outputInTransaction;
        preimage[32] = outIdx & 0xFF; preimage[33] = (outIdx >> 8) & 0xFF;
        preimage[34] = (outIdx >> 16) & 0xFF; preimage[35] = (outIdx >> 24) & 0xFF;
        Crypto::Hash h = Crypto::cn_fast_hash(preimage, sizeof(preimage));
        std::array<uint8_t, 32> depositSecret;
        memcpy(depositSecret.data(), h.data, 32);
        CryptoNote::DepositCommitmentKeys ck = CryptoNote::deriveCommitmentKeys(depositSecret);
        KeyPair commitmentKeyPair = {ck.commitKey, ck.keyScalar};

        std::vector<CryptoNote::COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry> filteredDecoys;
        for (const auto& d : decoys)
          if (d.global_amount_index != static_cast<uint32_t>(transfer.globalOutputIndex))
            filteredDecoys.push_back(d);
        const size_t numDecoys = std::min(filteredDecoys.size(), ringSize - 1);
        const size_t actualRing = numDecoys + 1;
        if (actualRing == 0) continue;
        const size_t realPos = Crypto::rand<size_t>() % actualRing;

        std::vector<uint32_t> absIndices;
        std::vector<const Crypto::PublicKey*> ringKeys;
        size_t decoyPos = 0;
        for (size_t slot = 0; slot < actualRing; ++slot) {
          if (slot == realPos) {
            absIndices.push_back(transfer.globalOutputIndex);
            ringKeys.push_back(&commitmentKeyPair.publicKey);
          } else {
            absIndices.push_back(filteredDecoys[decoyPos].global_amount_index);
            ringKeys.push_back(&filteredDecoys[decoyPos].commit_key);
            ++decoyPos;
          }
        }

        std::vector<size_t> order(actualRing);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return absIndices[a] < absIndices[b]; });
        size_t sortedRealPos = 0;
        std::vector<uint32_t> sortedAbs(actualRing);
        std::vector<const Crypto::PublicKey*> sortedKeys(actualRing);
        for (size_t s = 0; s < actualRing; ++s) {
          sortedAbs[s] = absIndices[order[s]];
          sortedKeys[s] = ringKeys[order[s]];
          if (order[s] == realPos) sortedRealPos = s;
        }

        std::vector<uint32_t> relOffsets(actualRing);
        relOffsets[0] = sortedAbs[0];
        for (size_t s = 1; s < actualRing; ++s) relOffsets[s] = sortedAbs[s] - sortedAbs[s - 1];

        TransactionInputCommitmentSpend csInput;
        csInput.amount = transfer.amount;
        csInput.outputIndexes = relOffsets;
        csInput.keyImage = ck.keyImage;
        transaction->addInput(csInput);
        transaction->signInputCommitmentSpend(commitmentDepositIdx, sortedKeys, commitmentKeyPair, sortedRealPos);
        commitmentDepositIdx++;
      }

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, amount, context->selectedTransfers);
      notifyBalanceChanged(events);

      return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    } catch (std::exception &e) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
      return {};
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makePlaceOrderV13Request(
      TransactionId& transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>>& events,
      uint8_t side, uint64_t amount, uint64_t price,
      uint32_t expiration, uint64_t fee, uint64_t mixIn) {
    throwIf(amount == 0, error::WRONG_AMOUNT);
    throwIf(side > 1, error::WRONG_AMOUNT);

    uint64_t neededMoney = getSumWithOverflowCheck(amount, fee);
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn;

    context->foundMoney = selectTransfersToSend(neededMoney, false, context->dustPolicy.dustThreshold, context->selectedTransfers);
    throwIf(context->foundMoney < neededMoney, error::WRONG_AMOUNT);

    // Generate random orderId
    Crypto::Hash orderId;
    Crypto::generate_random_bytes(sizeof(orderId.data), orderId.data);

    // Limit deposits always go to the pool commit key (one-sided deposit)
    // The pool tracks pendingXfg/pendingHeat and uses m_limitDeposits map
    Crypto::PublicKey poolKey = CryptoNote::computePoolCommitKey();

    context->isV11LimitDeposit = true;
    context->v11DepositSide = side;
    context->v11DepositAmount = amount;
    context->v11DepositTargetPrice = price;
    context->v11DepositExpiration = expiration;
    context->v11DepositOrderId = orderId;

    // Compute address_hash = cn_fast_hash(spendKey||viewKey) for privacy
    uint8_t keyData[64];
    memcpy(keyData, m_keys.address.spendPublicKey.data, 32);
    memcpy(keyData + 32, m_keys.address.viewPublicKey.data, 32);
    Crypto::cn_fast_hash(keyData, 64, context->v11DepositAddressHash);

    transactionId = m_transactionsCache.addNewTransaction(neededMoney, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;

    std::vector<uint8_t> extra;
    addLimitDepositToExtra(extra, side, amount, price, expiration, orderId, context->v11DepositAddressHash);
    context->extra = std::string(extra.begin(), extra.end());

    Crypto::SecretKey transactionSK;
    return makeGetRandomOutsRequest(std::move(context), false, transactionSK);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeCancelOrderV13Request(
      TransactionId& transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>>& events,
      const Crypto::Hash& orderId, uint64_t fee, uint64_t mixIn) {
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn;

    context->isV11LimitWithdraw = true;
    context->v11WithdrawOrderId = orderId;

    transactionId = m_transactionsCache.addNewTransaction(0, fee, std::string(), {}, 0, {});
    context->transactionId = transactionId;

    std::vector<uint8_t> extra;
    addLimitWithdrawToExtra(extra, orderId);
    context->extra = std::string(extra.begin(), extra.end());

    Crypto::SecretKey transactionSK;
    return makeGetRandomOutsRequest(std::move(context), false, transactionSK);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeMarketBuyV13Request(
      TransactionId& transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>>& events,
      uint64_t xfgWanted, uint64_t maxHeatCost,
      uint64_t fee, uint64_t mixIn) {
    // Market buy (BUY_XFG with HEAT) uses AMM swap: direction=0 (XFG→HEAT swap from pool perspective)
    // Actual: user pays HEAT, receives XFG. AMM direction=1 (HEAT→XFG from pool view).
    throwIf(xfgWanted == 0, error::WRONG_AMOUNT);
    uint64_t heatInput = maxHeatCost;
    return makeAmmSwapV10Request(transactionId, events, 1, heatInput, xfgWanted, 1, fee, mixIn);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeMarketSellV13Request(
      TransactionId& transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>>& events,
      uint64_t xfgToSell, uint64_t minHeatReceive,
      uint64_t fee, uint64_t mixIn) {
    // Market sell (SELL_XFG for HEAT) uses AMM swap: direction=0 (XFG→HEAT)
    throwIf(xfgToSell == 0, error::WRONG_AMOUNT);
    return makeAmmSwapV10Request(transactionId, events, 0, xfgToSell, minHeatReceive, minHeatReceive, fee, mixIn);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendPlaceOrderV13Transaction(
      std::shared_ptr<SendTransactionContext>&& context,
      std::deque<std::unique_ptr<WalletLegacyEvent>>& events,
      uint8_t side, uint64_t amount, uint64_t targetPrice,
      uint32_t expiration, const Crypto::Hash& orderId,
      const Crypto::Hash& addressHash) {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }

    try {
      WalletLegacyTransaction& transactionInfo = m_transactionsCache.getTransaction(context->transactionId);
      std::unique_ptr<ITransaction> transaction = createTransaction();
      uint64_t fee = m_currency.minimumFee();

      // Deposit output → pool commit key (one-sided deposit, tracked by m_limitDeposits)
      Crypto::PublicKey poolKey = CryptoNote::computePoolCommitKey();
      uint32_t poolTerm = (side == 1)
        ? parameters::DEPOSIT_TERM_POOL_XFG   // 'POLX' — pool receives XFG
        : parameters::DEPOSIT_TERM_POOL_HEAT;  // 'POLH' — pool receives HEAT
      CryptoNote::TransactionOutputCommitment poolOut;
      poolOut.commitKey = poolKey;
      poolOut.term = poolTerm;
      transaction->addOutput(amount, poolOut);

      // Change output → user
      uint64_t changeAmount = context->foundMoney - amount - fee;
      std::vector<uint64_t> decomposedChange = splitAmount(changeAmount, context->dustPolicy.dustThreshold);
      for (uint64_t changeOut : decomposedChange)
        transaction->addOutput(changeOut, m_keys.address);

      transaction->setUnlockTime(0);

      // Add limit deposit extra
      std::vector<uint8_t> extra;
      addLimitDepositToExtra(extra, side, amount, targetPrice, expiration, orderId, addressHash);
      CryptoNote::BinaryArray extraData(extra.begin(), extra.end());
      transaction->appendExtra(extraData);

      // Inputs with ring signing
      std::vector<TransactionSourceEntry> sources;
      prepareKeyInputs(context->selectedTransfers, context->outs, sources, context->mixIn);
      std::vector<TransactionTypes::InputKeyInfo> inputs = convertSources(std::vector<TransactionSourceEntry>(sources));

      std::vector<KeyPair> ephKeys;
      for (size_t i = 0; i < inputs.size(); ++i) {
        KeyPair ephKey;
        transaction->addInput(m_keys, inputs[i], ephKey);
        ephKeys.push_back(std::move(ephKey));
      }
      for (size_t i = 0; i < inputs.size(); ++i)
        transaction->signInputKey(i, inputs[i], ephKeys[i]);

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, amount, context->selectedTransfers);
      notifyBalanceChanged(events);

      return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    } catch (std::exception& e) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
      return {};
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::doSendCancelOrderV13Transaction(
      std::shared_ptr<SendTransactionContext>&& context,
      std::deque<std::unique_ptr<WalletLegacyEvent>>& events,
      const Crypto::Hash& orderId) {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }

    try {
      WalletLegacyTransaction& transactionInfo = m_transactionsCache.getTransaction(context->transactionId);
      std::unique_ptr<ITransaction> transaction = createTransaction();
      uint64_t fee = m_currency.minimumFee();

      // Cancel tx only needs TX_EXTRA_LIMIT_WITHDRAW — the blockchain
      // validates against m_limitDeposits and returns the deposit.
      // The return output is implied by the deposit record (side + amount).
      transaction->setUnlockTime(0);

      std::vector<uint8_t> extra;
      addLimitWithdrawToExtra(extra, orderId);
      CryptoNote::BinaryArray extraData(extra.begin(), extra.end());
      transaction->appendExtra(extraData);

      // Cancel does not spend regular UTXOs — the pool returns the deposit.
      // Still need a fee input — user covers the cancellation tx fee.
      uint64_t neededFee = getSumWithOverflowCheck(fee, context->dustPolicy.dustThreshold);
      context->foundMoney = selectTransfersToSend(neededFee, false, context->dustPolicy.dustThreshold, context->selectedTransfers);
      throwIf(context->foundMoney < neededFee, error::WRONG_AMOUNT);

      uint64_t changeAmount = context->foundMoney - fee;
      std::vector<uint64_t> decomposedChange = splitAmount(changeAmount, context->dustPolicy.dustThreshold);
      for (uint64_t changeOut : decomposedChange)
        transaction->addOutput(changeOut, m_keys.address);

      std::vector<TransactionSourceEntry> sources;
      prepareKeyInputs(context->selectedTransfers, context->outs, sources, context->mixIn);
      std::vector<TransactionTypes::InputKeyInfo> inputs = convertSources(std::vector<TransactionSourceEntry>(sources));

      std::vector<KeyPair> ephKeys;
      for (size_t i = 0; i < inputs.size(); ++i) {
        KeyPair ephKey;
        transaction->addInput(m_keys, inputs[i], ephKey);
        ephKeys.push_back(std::move(ephKey));
      }
      for (size_t i = 0; i < inputs.size(); ++i)
        transaction->signInputKey(i, inputs[i], ephKeys[i]);

      Transaction lowlevelTransaction = convertTransaction(*transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, 0, context->selectedTransfers);
      notifyBalanceChanged(events);

      return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    } catch (std::exception& e) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
      return {};
    }
  }

} /* namespace CryptoNote */
