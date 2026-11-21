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

#include "CryptoNoteCore/SubaddressTx.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "INode.h"
#include "crypto/crypto.h"
#include "crypto/subaddress.h"
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
#include "Common/FileSystem.h"
#include "Common/Int128.h"
#include "Common/PathTools.h"
#include "WalletLegacy/WalletRequest.h"  // for WalletGetOutputsHeightsRequest
#include "INode.h"

#include <Logging/LoggerGroup.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
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
    bool isSubaddress = false;
    return m_currency.parseAccountAddressString(address, ignore, isSubaddress);
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

    for (const TransactionMessage &message : messages)
    {
      AccountPublicAddress address;
      bool isSubaddress = false;
      bool extracted = m_currency.parseAccountAddressString(message.address, address, isSubaddress);
      if (!extracted)
      {
        throw std::system_error(make_error_code(error::BAD_ADDRESS));
      }

      context->messages.push_back({message.message, true, address});
    }

    transactionId = recordSendHoldingInputs(*context, [&] { return m_transactionsCache.addNewTransaction(neededMoney, fee, extra, transfers, unlockTimestamp, messages); });
    context->mixIn = mixIn;
    context->ttl = ttl;

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

    transactionId = recordSendHoldingInputs(*context, [&] { return m_transactionsCache.addNewTransaction(neededMoney, fee, std::string(), {}, 0, {}); });
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
    throwIf(depositIds.empty(), error::DEPOSIT_DOESNOT_EXIST);
    std::vector<TransactionOutputInformation> depositTransfers;
    const uint64_t depositsTotal = selectDepositTransfers(depositIds, depositTransfers);
    size_t commitmentCount = 0;
    for (const auto &transfer : depositTransfers) {
      commitmentCount += transfer.type == TransactionTypes::OutputType::Commitment ? 1 : 0;
    }
    throwIf(commitmentCount != 0 && commitmentCount != depositTransfers.size(), error::WRONG_PARAMETERS);

    if (commitmentCount > 0) {
      std::shared_ptr<SendTransactionContext> context = newCommitmentSpendContext(0);
      attachCommitmentInputs(*context, depositIds, true);
      selectXfg(*context, fee);
      context->isCommitmentWithdraw = true;
      context->xfgSpentOnFees = fee;
      return registerCommitmentSpend(std::move(context), transactionId, depositsTotal, fee, events);
    }

    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->foundMoney = depositsTotal;
    context->selectedTransfers = std::move(depositTransfers);
    throwIf(context->foundMoney < fee, error::WRONG_AMOUNT);

    transactionId = recordSendHoldingInputs(*context, [&] { return m_transactionsCache.addNewTransaction(context->foundMoney, fee, std::string(), {}, 0, {}); });
    context->mixIn = 0;
    setSpendingTransactionToDeposits(transactionId, depositIds);
    return doSendDepositWithdrawTransaction(std::move(context), events, depositIds);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeDonateToTreasuryRequest(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t amount,
      uint64_t fee,
      uint64_t mixIn,
      Crypto::SecretKey &transactionSK)
  {
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn == 0 ? m_currency.maxMixin() : mixIn;

    uint64_t neededMoney = getSumWithOverflowCheck(amount, fee);
    context->foundMoney = selectTransfersToSend(neededMoney, false, context->dustPolicy.dustThreshold, context->selectedTransfers);
    throwIf(context->foundMoney < neededMoney, error::WRONG_AMOUNT);

    // TreasuryFund tag: asset 0 (XFG) — the burned amount credits the
    // treasury counter → Treasury LP Manager. No outputs besides change.
    std::vector<uint8_t> extraVec;
    CryptoNote::addTreasuryFundToExtra(extraVec, 0, amount);
    std::string extra(extraVec.begin(), extraVec.end());

    transactionId = recordSendHoldingInputs(*context, [&] { return m_transactionsCache.addNewTransaction(neededMoney, fee, extra, {}, 0, {}); });

    return makeGetRandomOutsRequest(std::move(context), false, transactionSK);
  }

  // REMOVED: makeWithdrawLegacyBondRequest — no bond claims exist (0xCB/0xCC retired).

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

    transactionId = recordSendHoldingInputs(*context, [&] { return m_transactionsCache.addNewTransaction(neededMoney, fee, extra, transfers, unlockTimestamp, messages); });
    context->mixIn = mixIn;
    Crypto::SecretKey transactionSK;

    if (context->mixIn)
    {
      return makeGetRandomOutsRequest(std::move(context), false, transactionSK);
    }

    return doSendTransaction(std::move(context), events, transactionSK);
  }

  // The node answers one decoy set per selected input, in request order. A node
  // that answers fewer, or for other amounts, would have the ring builder index
  // past the sets it returned.
  bool WalletTransactionSender::decoysMatchSelectedInputs(const SendTransactionContext &context)
  {
    if (context.outs.size() != context.selectedTransfers.size())
    {
      return false;
    }
    for (size_t i = 0; i < context.outs.size(); ++i)
    {
      if (context.outs[i].amount != context.selectedTransfers[i].amount)
      {
        return false;
      }
    }
    return true;
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

    if (!ec && !decoysMatchSelectedInputs(*context))
    {
      ec = make_error_code(error::INTERNAL_WALLET_ERROR);
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

    if (context->usesCommitmentPipeline)
    {
      nextRequest = requestNextCommitmentDecoys(std::move(context), events);
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
  //
  // Retained for the amount mask and for spending pre-v11 outputs. New outputs
  // must go through deriveSelfCommitmentKey / deriveRecipientCommitmentKey so
  // the sender and any view-key holder cannot derive the spend scalar.
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

  Crypto::PublicKey WalletTransactionSender::deriveSelfCommitmentKey(
      const ITransaction& tx,
      size_t outputIndex) {
    return deriveCommitmentOutputKeyFor(tx, outputIndex, m_keys.address.viewPublicKey,
                                        m_keys.address.spendPublicKey);
  }

  Crypto::PublicKey WalletTransactionSender::deriveRecipientCommitmentKey(
      const ITransaction& tx,
      size_t outputIndex,
      const AccountPublicAddress& recipient) {
    return deriveCommitmentOutputKeyFor(tx, outputIndex, recipient.viewPublicKey,
                                        recipient.spendPublicKey);
  }

  Crypto::PublicKey WalletTransactionSender::deriveCommitmentOutputKeyFor(
      const ITransaction& tx,
      size_t outputIndex,
      const Crypto::PublicKey& recipientViewPublicKey,
      const Crypto::PublicKey& recipientSpendPublicKey) {
    const uint32_t commitOutputIndex = static_cast<uint32_t>(outputIndex);
    Crypto::SecretKey txSecretKey;
    if (!tx.getTransactionSecretKey(txSecretKey)) {
      throw std::runtime_error("commitment output: could not retrieve tx secret key");
    }
    Crypto::KeyDerivation ecdh;
    if (!Crypto::generate_key_derivation(recipientViewPublicKey, txSecretKey, ecdh)) {
      throw std::runtime_error("commitment output: ECDH key derivation failed");
    }
    Crypto::PublicKey commitKey;
    if (!CryptoNote::deriveCommitmentOutputKey(ecdh, commitOutputIndex,
                                              recipientSpendPublicKey, commitKey)) {
      throw std::runtime_error("commitment output: owner-bound key derivation failed");
    }
    return commitKey;
  }

  // Signing side. Delegates to the shared resolver so this wallet and WalletGreen
  // make the owner-bound / legacy decision identically: owner-bound outputs
  // require the recipient spend secret, pre-v11 outputs fall back to the original
  // ECDH-derived scalar so old funds stay spendable, and an output whose commit
  // key was never recorded is treated as legacy. Rejects rather than guesses if
  // the recorded key reproduces under neither derivation.
  void WalletTransactionSender::resolveCommitmentSpendKey(
      const Crypto::KeyDerivation& ecdh,
      size_t outputIndex,
      const Crypto::PublicKey& commitKey,
      const Crypto::SecretKey& recipientSpendSecret,
      KeyPair& outKeyPair,
      Crypto::KeyImage& outKeyImage) {
    CryptoNote::CommitmentSpendKeys keys;
    if (!CryptoNote::resolveCommitmentSpendKeys(ecdh, outputIndex, commitKey,
                                                recipientSpendSecret, keys)) {
      throw std::runtime_error("commitment spend: output matches neither derivation");
    }
    outKeyPair = {keys.commitKey, keys.spendScalar};
    outKeyImage = keys.keyImage;
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
      const size_t commitOutputIndex = static_cast<size_t>(transaction->getOutputCount());
      CryptoNote::TransactionOutputCommitment commitOut;
      commitOut.commitKey = deriveSelfCommitmentKey(*transaction, commitOutputIndex);
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

      // ── Deposit type detection ─────────────────────────────────────
      // 0x08 HEAT extras are retired: no new ones are created. A caller-set
      // 0x08 tag is still recognized for historical deposits; otherwise
      // deposits carry no commitment extra (CDs are commitment outputs).
      bool isHeatDeposit = false;
      Deposit::Type detectedType = Deposit::Type::HEAT;

      if (!context->extra.empty()) {
        uint8_t tag = static_cast<uint8_t>(context->extra[0]);
        if (tag == TX_EXTRA_HEAT_COMMITMENT) {
          detectedType = Deposit::Type::HEAT;
          isHeatDeposit = true;
        }
      } else {
        if (context->depositTerm == parameters::HEAT_TERM) {
          // HEAT burns must use mintHeatV10(), not legacy 0x08 extras.
          throw std::runtime_error("HEAT burn/mint must use mintHeatV10(). Legacy 0x08 commitment extras are retired.");
        }
        // Term-locked deposits need no commitment extra.
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
      // An unconfirmed record carries the send time the wallet file saves, so the
      // withdraw expires from when it was sent even across reloads. It moves no
      // XFG: amount 0 and no key inputs.
      m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, 0, {});
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

  // REMOVED: doSendLegacyBondWithdrawTransaction — no bond claims exist
  // (0xCB/0xCC retired). Legacy XFG multisig deposits withdraw principal-only
  // via the regular deposit-withdraw path (calculateInterest == 0).

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
      bool isSubaddress = false;
      if (!m_currency.parseAccountAddressString(de.address, addr, isSubaddress))
      {
        throw std::system_error(make_error_code(error::BAD_ADDRESS));
      }

      decompose_amount_into_digits(
          de.amount, dust_threshold,
          [&](uint64_t chunk) { splitted_dsts.push_back(TransactionDestinationEntry(chunk, addr, isSubaddress)); },
          [&](uint64_t a_dust) { splitted_dsts.push_back(TransactionDestinationEntry(a_dust, addr, isSubaddress)); });
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
      throwIf(!m_transactionsCache.getDeposit(id, deposit), error::DEPOSIT_DOESNOT_EXIST);
      throwIf(deposit.spendingTransactionId != WALLET_LEGACY_INVALID_TRANSACTION_ID, error::DEPOSIT_LOCKED);

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
    context->mixIn = mixIn == 0 ? m_currency.maxMixin() : mixIn;

    context->foundMoney = selectTransfersToSend(neededMoney, false, context->dustPolicy.dustThreshold, context->selectedTransfers);
    throwIf(context->foundMoney < neededMoney, error::WRONG_AMOUNT);

    context->isV10HeatMint = true;
    context->v10XfgBurned = xfgBurned;
    context->v10HeatMinted = heatMinted;
    context->dynamicRingSize = m_currency.isTestnet();  // allow bootstrap ring sizes on testnet

    transactionId = recordSendHoldingInputs(*context, [&] { return m_transactionsCache.addNewTransaction(neededMoney, fee, std::string(), {}, 0, {}); });

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
        CryptoNote::TransactionOutputCommitment heatOut;
        heatOut.commitKey = deriveSelfCommitmentKey(*transaction, commitOutputIndex);
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
    const uint64_t epochBlocks = m_currency.isTestnet()
        ? parameters::TESTNET_EPOCH_DURATION_BLOCKS : parameters::EPOCH_DURATION_BLOCKS;
    throwIf(amount == 0 || bankingFee == 0, error::WRONG_AMOUNT);
    throwIf(termEpochs == 0 || termEpochs == parameters::HEAT_TERM ||
            termEpochs > std::numeric_limits<uint32_t>::max() / epochBlocks, error::WRONG_AMOUNT);

    std::shared_ptr<SendTransactionContext> context = newCommitmentSpendContext(mixIn);
    attachCommitmentInputs(*context, selectSpendableDeposits(parameters::HEAT_TERM, amount), false);
    const uint64_t xfgForFees = getSumWithOverflowCheck(fee, bankingFee);
    selectXfg(*context, xfgForFees);

    context->isV10HeatDeposit = true;
    context->cdAmount = amount;
    context->cdCreationFee = bankingFee;
    context->xfgSpentOnFees = xfgForFees;
    context->depositTerm = static_cast<uint32_t>(termEpochs * epochBlocks);
    return registerCommitmentSpend(std::move(context), transactionId, xfgForFees, fee, events);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeHeatTransferV10Request(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      const AccountPublicAddress& recipient,
      uint64_t amount,
      uint64_t fee,
      uint64_t mixIn,
      bool isSubaddress)
  {
    throwIf(amount == 0, error::WRONG_AMOUNT);
    std::shared_ptr<SendTransactionContext> context = newCommitmentSpendContext(mixIn);
    attachCommitmentInputs(*context, selectSpendableDeposits(parameters::HEAT_TERM, amount), false);
    selectXfg(*context, fee);

    context->isV10HeatTransfer = true;
    context->v10HeatRecipient = recipient;
    context->v10HeatTransferAmount = amount;
    context->v10HeatRecipientIsSubaddress = isSubaddress;
    context->xfgSpentOnFees = fee;
    return registerCommitmentSpend(std::move(context), transactionId, fee, fee, events);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeLpAddV10Request(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t amountXfg,
      uint64_t amountHeat,
      uint64_t lpShares,
      uint64_t fee,
      uint64_t mixIn)
  {
    throwIf(amountXfg == 0 || amountHeat == 0 || lpShares == 0, error::WRONG_AMOUNT);
    std::shared_ptr<SendTransactionContext> context = newCommitmentSpendContext(mixIn);
    attachCommitmentInputs(*context, selectSpendableDeposits(parameters::HEAT_TERM, amountHeat), false);
    const uint64_t xfgSpent = getSumWithOverflowCheck(amountXfg, fee);
    selectXfg(*context, xfgSpent);

    context->isV10LpAdd = true;
    context->v10XfgBurned = amountXfg;
    context->v10HeatMinted = amountHeat;
    context->lpShares = lpShares;
    context->xfgSpentOnFees = xfgSpent;
    return registerCommitmentSpend(std::move(context), transactionId, xfgSpent, fee, events);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeLpRemoveV10Request(
      TransactionId &transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
      uint64_t lpShares,
      uint64_t payoutXfg,
      uint64_t payoutHeat,
      uint64_t fee,
      uint64_t mixIn)
  {
    throwIf(lpShares == 0 || payoutXfg <= fee, error::WRONG_AMOUNT);
    std::shared_ptr<SendTransactionContext> context = newCommitmentSpendContext(mixIn);
    attachCommitmentInputs(*context, selectSpendableDeposits(parameters::DEPOSIT_TERM_LP, lpShares), false);

    context->isV10LpRemove = true;
    context->v10LpSharesBurned = lpShares;
    context->v10LpMinXfg = payoutXfg;
    context->v10LpMinHeat = payoutHeat;
    return registerCommitmentSpend(std::move(context), transactionId, 0, fee, events);
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
    throwIf(inputAmount == 0 || outputAmount == 0 || direction > 1, error::WRONG_AMOUNT);

    if (direction == 1) {
      throwIf(outputAmount <= fee, error::WRONG_AMOUNT);
      std::shared_ptr<SendTransactionContext> context = newCommitmentSpendContext(mixIn);
      attachCommitmentInputs(*context, selectSpendableDeposits(parameters::HEAT_TERM, inputAmount), false);
      context->isV10AmmSwap = true;
      context->v10SwapDirection = direction;
      context->v10SwapInput = inputAmount;
      context->v10SwapOutput = outputAmount;
      context->v10SwapMinOutput = minOutput;
      return registerCommitmentSpend(std::move(context), transactionId, 0, fee, events);
    }

    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn;

    uint64_t neededMoney = getSumWithOverflowCheck(inputAmount, fee);
    context->foundMoney = selectTransfersToSend(neededMoney, false, context->dustPolicy.dustThreshold, context->selectedTransfers);
    throwIf(context->foundMoney < neededMoney, error::WRONG_AMOUNT);

    transactionId = recordSendHoldingInputs(*context, [&] { return m_transactionsCache.addNewTransaction(neededMoney, fee, std::string(), {}, 0, {}); });
    context->isV10AmmSwap = true;
    context->v10SwapDirection = direction;
    context->v10SwapInput = inputAmount;
    context->v10SwapOutput = outputAmount;
    context->v10SwapMinOutput = minOutput;

    Crypto::SecretKey &unusedTransactionSecret = context->transactionSecretKey;
    return makeGetRandomOutsRequest(std::move(context), false, unusedTransactionSecret);
  }

  // ── Commitment-spend pipeline ─────────────────────────────────────────────
  // XFG decoys (when the transaction spends XFG), then decoys for every
  // (amount, term) ring of its commitment inputs, then one builder per kind.
  // Every builder adds all inputs before signing any and wipes the keys.

  std::shared_ptr<SendTransactionContext> WalletTransactionSender::newCommitmentSpendContext(uint64_t mixIn)
  {
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn == 0 ? m_currency.maxMixin() : mixIn;
    context->dynamicRingSize = m_currency.isTestnet();
    return context;
  }

  std::vector<DepositId> WalletTransactionSender::selectSpendableDeposits(uint32_t term, uint64_t needed)
  {
    std::vector<DepositId> selectedIds;
    uint64_t total = 0;
    const size_t depositCount = m_transactionsCache.getDepositCount();
    for (DepositId id = 0; id < depositCount && total < needed; ++id) {
      Deposit deposit;
      if (m_transactionsCache.getDeposit(id, deposit) && deposit.term == term && !deposit.locked &&
          deposit.spendingTransactionId == WALLET_LEGACY_INVALID_TRANSACTION_ID && depositTransferAvailable(id)) {
        total = getSumWithOverflowCheck(total, deposit.amount);
        selectedIds.push_back(id);
      }
    }
    throwIf(total < needed, error::WRONG_AMOUNT);
    return selectedIds;
  }

  bool WalletTransactionSender::depositTransferAvailable(DepositId depositId)
  {
    Hash transactionHash;
    uint32_t outputInTransaction = 0;
    TransactionOutputInformation transfer;
    ITransfersContainer::TransferState state;
    return m_transactionsCache.getDepositInTransactionInfo(depositId, transactionHash, outputInTransaction) &&
           m_transferDetails.getTransfer(transactionHash, outputInTransaction, transfer, state) &&
           state == ITransfersContainer::TransferState::TransferAvailable;
  }

  void WalletTransactionSender::attachCommitmentInputs(SendTransactionContext &context,
                                                       const std::vector<DepositId> &depositIds,
                                                       bool capDecoysAtDepositHeight)
  {
    const size_t firstAttached = context.commitmentTransfers.size();
    selectDepositTransfers(depositIds, context.commitmentTransfers);
    std::set<CommitmentRing> rings;
    for (size_t k = 0; k < depositIds.size(); ++k) {
      Deposit deposit;
      throwIf(!m_transactionsCache.getDeposit(depositIds[k], deposit), error::DEPOSIT_DOESNOT_EXIST);
      const TransactionOutputInformation &transfer = context.commitmentTransfers[firstAttached + k];
      throwIf(transfer.type != TransactionTypes::OutputType::Commitment || transfer.amount != deposit.amount ||
              transfer.term != deposit.term, error::DEPOSIT_DOESNOT_EXIST);
      const CommitmentRing ring{deposit.amount, deposit.term,
                                capDecoysAtDepositHeight ? static_cast<uint32_t>(deposit.height) : 0};
      context.commitmentRings.push_back(ring);
      rings.insert(ring);
    }
    context.spentDepositIds.insert(context.spentDepositIds.end(), depositIds.begin(), depositIds.end());
    context.ringsAwaitingDecoys.assign(rings.begin(), rings.end());
  }

  void WalletTransactionSender::selectXfg(SendTransactionContext &context, uint64_t needed)
  {
    context.foundMoney = selectTransfersToSend(needed, false, context.dustPolicy.dustThreshold, context.selectedTransfers);
    throwIf(context.foundMoney < needed, error::WRONG_AMOUNT);
  }

  // Refuses before the send's record exists, so a refused send leaves no record.
  // Every caller holds the wallet's cache lock, so nothing can claim the inputs
  // between the check and the reservation.
  TransactionId WalletTransactionSender::recordSendHoldingInputs(SendTransactionContext &context,
                                                                 const std::function<TransactionId()> &addRecord)
  {
    const bool anyHeld = std::any_of(context.selectedTransfers.begin(), context.selectedTransfers.end(),
                                     [this](const TransactionOutputInformation &out) { return m_transactionsCache.isUsed(out); });
    throwIf(anyHeld, error::WRONG_STATE);
    const TransactionId transactionId = addRecord();
    context.transactionId = transactionId;
    m_transactionsCache.reserveOutputs(transactionId, context.selectedTransfers);
    return transactionId;
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::registerCommitmentSpend(
      std::shared_ptr<SendTransactionContext> &&context, TransactionId &transactionId,
      uint64_t xfgLeavingWallet, uint64_t fee, std::deque<std::unique_ptr<WalletLegacyEvent>> &events)
  {
    transactionId = recordSendHoldingInputs(*context, [&] { return m_transactionsCache.addNewTransaction(xfgLeavingWallet, fee, std::string(), {}, 0, {}); });
    context->usesCommitmentPipeline = true;
    setSpendingTransactionToDeposits(transactionId, context->spentDepositIds);
    if (context->selectedTransfers.empty()) {
      return requestNextCommitmentDecoys(std::move(context), events);
    }
    Crypto::SecretKey &unusedTransactionSecret = context->transactionSecretKey;
    return makeGetRandomOutsRequest(std::move(context), false, unusedTransactionSecret);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::requestNextCommitmentDecoys(
      std::shared_ptr<SendTransactionContext> context,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events)
  {
    if (context->ringsAwaitingDecoys.empty()) {
      return buildCommitmentSpend(std::move(context), events);
    }
    const CommitmentRing ring = context->ringsAwaitingDecoys.back();
    context->ringsAwaitingDecoys.pop_back();
    auto &decoys = context->decoysByRing[ring];
    const uint64_t outsCount = m_currency.maxMixin() + 1;
    return std::unique_ptr<WalletRequest>(new WalletGetCommitmentDecoysRequest(ring, outsCount, decoys, context,
        std::bind(&WalletTransactionSender::onCommitmentDecoysFetched, this, context,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
  }

  void WalletTransactionSender::onCommitmentDecoysFetched(std::shared_ptr<SendTransactionContext> context,
                                                          std::deque<std::unique_ptr<WalletLegacyEvent>> &events,
                                                          std::unique_ptr<WalletRequest> &nextRequest,
                                                          std::error_code ec)
  {
    if (m_isStoping) {
      ec = make_error_code(error::TX_CANCELLED);
    }
    if (ec) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, ec));
      return;
    }
    nextRequest = requestNextCommitmentDecoys(std::move(context), events);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::buildCommitmentSpend(
      std::shared_ptr<SendTransactionContext> &&context,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events)
  {
    if (m_isStoping) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::TX_CANCELLED)));
      return {};
    }
    CommitmentSigningKeys signingKeys;
    try {
      std::unique_ptr<ITransaction> transaction = createTransaction();
      transaction->setUnlockTime(0);
      if (context->isV10HeatDeposit) {
        buildHeatDeposit(*transaction, *context);
      } else if (context->isV10HeatTransfer) {
        buildHeatTransfer(*transaction, *context);
      } else if (context->isV10AmmSwap) {
        buildHeatSell(*transaction, *context);
      } else if (context->isV10LpAdd) {
        buildLpAdd(*transaction, *context);
      } else if (context->isV10LpRemove) {
        buildLpRemove(*transaction, *context);
      } else if (context->isV11LimitDeposit) {
        buildLimitDeposit(*transaction, *context);
      } else {
        buildCommitmentWithdraw(*transaction, *context);
      }
      addAndSignAllInputs(*transaction, *context, signingKeys);
      return relayBuiltTransaction(std::move(context), *transaction, events);
    } catch (std::system_error &err) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, err.code()));
    } catch (std::exception &) {
      events.push_back(makeCompleteEvent(m_transactionsCache, context->transactionId, make_error_code(error::INTERNAL_WALLET_ERROR)));
    }
    return {};
  }

  void WalletTransactionSender::buildHeatDeposit(ITransaction &transaction, SendTransactionContext &context)
  {
    addOwnCommitmentOutput(transaction, context.cdAmount, context.depositTerm);
    addOwnHeatOutputs(transaction, commitmentInputTotal(context) - context.cdAmount);
    addOwnXfgOutputs(transaction, context.foundMoney - context.xfgSpentOnFees, context);
    std::vector<uint8_t> cdFeeExtra;
    addCdCreationFeeToExtra(cdFeeExtra, context.cdCreationFee);
    transaction.appendExtra(CryptoNote::BinaryArray(cdFeeExtra.begin(), cdFeeExtra.end()));
  }

  void WalletTransactionSender::buildHeatTransfer(ITransaction &transaction, SendTransactionContext &context)
  {
    const AccountPublicAddress &recipient = context.v10HeatRecipient;
    const uint64_t amount = context.v10HeatTransferAmount;
    const bool recipientIsSubaddress = context.v10HeatRecipientIsSubaddress;
    const uint32_t recipientIndex = static_cast<uint32_t>(transaction.getOutputCount());
    Crypto::SecretKey txSecret;
    if (!transaction.getTransactionSecretKey(txSecret)) {
      throw std::runtime_error("HEAT send: missing tx secret key");
    }
    SubaddressTxKeys outputTxKeys;
    Crypto::KeyDerivation recipientDerivation;
    if (recipientIsSubaddress) {
      OutputTxKey recipientKey;
      if (!deriveOutputTxKey(txSecret, recipientIndex, recipient, true, recipientKey)) {
        wipeSecretKey(txSecret);
        throw std::runtime_error("HEAT send: subaddress tx key derivation failed");
      }
      recipientDerivation = recipientKey.derivation;
      outputTxKeys.additionalPubKeys.push_back(recipientKey.txPublicKey);
      outputTxKeys.janusBases.push_back(recipientKey.janusBase);
    } else if (!Crypto::generate_key_derivation(recipient.viewPublicKey, txSecret, recipientDerivation)) {
      wipeSecretKey(txSecret);
      throw std::runtime_error("HEAT send: ECDH derivation failed");
    }
    wipeSecretKey(txSecret);
    CryptoNote::TransactionOutputCommitment recipientOutput;
    const bool keyDerived = CryptoNote::deriveCommitmentOutputKey(
        recipientDerivation, recipientIndex, recipient.spendPublicKey, recipientOutput.commitKey);
    wipeKeyDerivation(recipientDerivation);
    if (!keyDerived) {
      throw std::runtime_error("HEAT send: commitment key derivation failed");
    }
    recipientOutput.term = parameters::HEAT_TERM;
    transaction.addOutput(amount, recipientOutput);

    const size_t firstOwnOutput = transaction.getOutputCount();
    addOwnHeatOutputs(transaction, commitmentInputTotal(context) - amount);
    addOwnXfgOutputs(transaction, context.foundMoney - context.xfgSpentOnFees, context);
    if (recipientIsSubaddress) {
      for (size_t output = firstOwnOutput; output < transaction.getOutputCount(); ++output) {
        outputTxKeys.additionalPubKeys.push_back(transaction.getTransactionPublicKey());
        outputTxKeys.janusBases.push_back(unlinkableJanusFiller());
      }
      std::vector<uint8_t> keysExtra;
      if (!encodeSubaddressTxKeys(outputTxKeys, keysExtra)) {
        throw std::runtime_error("HEAT send: subaddress tx keys extra failed");
      }
      transaction.appendExtra(CryptoNote::BinaryArray(keysExtra.begin(), keysExtra.end()));
    }

    std::vector<uint8_t> sendAuth;
    uint64_t heatAmountLE = amount;
    sendAuth.push_back(TX_EXTRA_HEAT_SEND_AUTH);
    for (size_t b = 0; b < sizeof(uint64_t); ++b) {
      sendAuth.push_back(static_cast<uint8_t>(heatAmountLE & 0xFF));
      heatAmountLE >>= 8;
    }
    transaction.appendExtra(CryptoNote::BinaryArray(sendAuth.begin(), sendAuth.end()));
  }

  void WalletTransactionSender::buildHeatSell(ITransaction &transaction, SendTransactionContext &context)
  {
    const uint64_t fee = m_currency.minimumFee();
    CryptoNote::TransactionOutputCommitment poolOutput;
    poolOutput.commitKey = computePoolCommitKey();
    poolOutput.term = parameters::DEPOSIT_TERM_POOL_HEAT;
    transaction.addOutput(context.v10SwapInput, poolOutput);
    addOwnXfgOutputs(transaction, context.v10SwapOutput - fee, context);
    addOwnHeatOutputs(transaction, commitmentInputTotal(context) - context.v10SwapInput);
    std::vector<uint8_t> extra;
    addAmmSwapAuthToExtra(extra, context.v10SwapDirection, context.v10SwapInput, context.v10SwapOutput,
                          context.v10SwapMinOutput);
    transaction.appendExtra(CryptoNote::BinaryArray(extra.begin(), extra.end()));
  }

  void WalletTransactionSender::buildLpAdd(ITransaction &transaction, SendTransactionContext &context)
  {
    addOwnCommitmentOutput(transaction, context.lpShares, parameters::DEPOSIT_TERM_LP);
    addOwnHeatOutputs(transaction, commitmentInputTotal(context) - context.v10HeatMinted);
    addOwnXfgOutputs(transaction, context.foundMoney - context.xfgSpentOnFees, context);
    std::vector<uint8_t> extra;
    addLpAddAuthToExtra(extra, context.v10XfgBurned, context.v10HeatMinted, context.lpShares);
    transaction.appendExtra(CryptoNote::BinaryArray(extra.begin(), extra.end()));
  }

  void WalletTransactionSender::buildLpRemove(ITransaction &transaction, SendTransactionContext &context)
  {
    const uint64_t fee = m_currency.minimumFee();
    addOwnXfgOutputs(transaction, context.v10LpMinXfg - fee, context);
    addOwnHeatOutputs(transaction, context.v10LpMinHeat);
    const uint64_t lpChange = commitmentInputTotal(context) - context.v10LpSharesBurned;
    if (lpChange > 0) {
      addOwnCommitmentOutput(transaction, lpChange, parameters::DEPOSIT_TERM_LP);
    }
    std::vector<uint8_t> extra;
    addLpRemoveAuthToExtra(extra, context.v10LpSharesBurned, context.v10LpMinXfg, context.v10LpMinHeat);
    transaction.appendExtra(CryptoNote::BinaryArray(extra.begin(), extra.end()));
  }

  void WalletTransactionSender::buildLimitDeposit(ITransaction &transaction, SendTransactionContext &context)
  {
    const bool escrowsXfg = context.v11DepositSide == 1;
    CryptoNote::TransactionOutputCommitment poolOutput;
    poolOutput.commitKey = computePoolCommitKey();
    poolOutput.term = escrowsXfg ? parameters::DEPOSIT_TERM_POOL_XFG : parameters::DEPOSIT_TERM_POOL_HEAT;
    transaction.addOutput(context.v11DepositAmount, poolOutput);
    if (!escrowsXfg) {
      addOwnHeatOutputs(transaction, commitmentInputTotal(context) - context.v11DepositAmount);
    }
    addOwnXfgOutputs(transaction, context.foundMoney - context.xfgSpentOnFees, context);
    std::vector<uint8_t> extra;
    addLimitDepositToExtra(extra, context.v11DepositSide, context.v11DepositAmount, context.v11DepositTargetPrice,
                           context.v11DepositExpiration, context.v11DepositOrderId, context.v11DepositAddressHash);
    transaction.appendExtra(CryptoNote::BinaryArray(extra.begin(), extra.end()));
  }

  void WalletTransactionSender::buildCommitmentWithdraw(ITransaction &transaction, SendTransactionContext &context)
  {
    const CdClaims claims = claimableInterest(context);
    context.claimedInterest = claims.perInput;
    addOwnHeatOutputs(transaction, getSumWithOverflowCheck(commitmentInputTotal(context), claims.total));
    addOwnXfgOutputs(transaction, context.foundMoney - context.xfgSpentOnFees, context);
    const size_t firstCommitmentInput = context.selectedTransfers.size();
    for (size_t k = 0; k < claims.bonus.size(); ++k) {
      if (claims.bonus[k] == 0) {
        continue;
      }
      throwIf(firstCommitmentInput + k > std::numeric_limits<uint8_t>::max(), error::WRONG_AMOUNT);
      TransactionExtraCdBonusClaim bonusClaim;
      bonusClaim.inputIndex = static_cast<uint8_t>(firstCommitmentInput + k);
      bonusClaim.claimedBonus = claims.bonus[k];
      std::vector<uint8_t> extra;
      addCdBonusClaimToExtra(extra, bonusClaim);
      transaction.appendExtra(extra);
    }
  }

  // Accrued interest per CD input, as consensus accepts it now. Withdrawing
  // spends the CD for good, so a node that cannot report it aborts the
  // withdrawal instead of claiming zero. The per-transaction base and bonus
  // sums must fit the pool and Bonus Vault backing, so both scale down.
  WalletTransactionSender::CdClaims WalletTransactionSender::claimableInterest(const SendTransactionContext &context)
  {
    CdClaims claims;
    const uint32_t currentHeight = m_node.getLastLocalBlockHeight();
    const uint32_t firstHeatwaveHeight = m_currency.upgradeHeight(BLOCK_MAJOR_VERSION_11);
    std::vector<uint64_t> base(context.spentDepositIds.size(), 0);
    claims.bonus.assign(context.spentDepositIds.size(), 0);
    uint64_t baseTotal = 0, bonusTotal = 0;
    uint64_t poolBacking = UINT64_MAX, bonusVaultBacking = UINT64_MAX;
    bool backingReported = false;
    for (size_t k = 0; k < context.spentDepositIds.size(); ++k) {
      Deposit deposit;
      if (!m_transactionsCache.getDeposit(context.spentDepositIds[k], deposit) || !isCdCommitmentTerm(deposit.term) ||
          static_cast<uint32_t>(deposit.height) < firstHeatwaveHeight) {
        continue;
      }
      INode::CdClaimInfo info;
      const std::error_code ec = m_node.getCdClaimInfo(deposit.amount, static_cast<uint32_t>(deposit.height),
                                                       currentHeight, info, deposit.term);
      if (ec) {
        throw std::system_error(ec, "cannot read a CD's accrued interest from the node; withdrawal aborted "
                                    "so the interest is not forfeited");
      }
      if (info.formulaInterest == 0 || !info.poolInfoPresent) {
        continue;
      }
      backingReported = true;
      poolBacking = std::min(poolBacking, std::min(info.feePoolBalance, info.vaultBalance));
      bonusVaultBacking = std::min(bonusVaultBacking, info.bonusVaultBalance);
      const bool splitReported = info.baseInterest > 0 || info.bonusInterest > 0;
      base[k] = splitReported ? info.baseInterest : info.formulaInterest;
      claims.bonus[k] = splitReported ? info.claimableBonus : 0;
      baseTotal = getSumWithOverflowCheck(baseTotal, base[k]);
      bonusTotal = getSumWithOverflowCheck(bonusTotal, claims.bonus[k]);
    }
    auto scaleToBacking = [](std::vector<uint64_t> &amounts, uint64_t total, uint64_t backing) {
      if (total <= backing || total == 0) {
        return;
      }
      for (auto &amount : amounts) {
        amount = static_cast<uint64_t>((static_cast<uint128_t>(amount) * backing) / total);
      }
    };
    if (backingReported) {
      scaleToBacking(base, baseTotal, poolBacking);
      scaleToBacking(claims.bonus, bonusTotal, bonusVaultBacking);
    }
    claims.perInput.resize(base.size());
    for (size_t k = 0; k < base.size(); ++k) {
      claims.perInput[k] = getSumWithOverflowCheck(base[k], claims.bonus[k]);
      claims.total = getSumWithOverflowCheck(claims.total, claims.perInput[k]);
    }
    return claims;
  }

  uint64_t WalletTransactionSender::commitmentInputTotal(const SendTransactionContext &context)
  {
    uint64_t total = 0;
    for (const auto &transfer : context.commitmentTransfers) {
      total = getSumWithOverflowCheck(total, transfer.amount);
    }
    return total;
  }

  void WalletTransactionSender::addOwnHeatOutputs(ITransaction &transaction, uint64_t amount)
  {
    if (amount == 0) {
      return;
    }
    for (uint64_t bill : decomposeHeatIntoBills(amount)) {
      addOwnCommitmentOutput(transaction, bill, parameters::HEAT_TERM);
    }
  }

  void WalletTransactionSender::addOwnXfgOutputs(ITransaction &transaction, uint64_t amount,
                                                 const SendTransactionContext &context)
  {
    for (uint64_t chunk : splitAmount(amount, context.dustPolicy.dustThreshold)) {
      if (chunk > 0) {
        transaction.addOutput(chunk, m_keys.address);
      }
    }
  }

  void WalletTransactionSender::addOwnCommitmentOutput(ITransaction &transaction, uint64_t amount, uint32_t term)
  {
    CryptoNote::TransactionOutputCommitment output;
    output.commitKey = deriveSelfCommitmentKey(transaction, transaction.getOutputCount());
    output.term = term;
    transaction.addOutput(amount, output);
  }

  void WalletTransactionSender::addAndSignAllInputs(ITransaction &transaction, SendTransactionContext &context,
                                                    CommitmentSigningKeys &signingKeys)
  {
    std::vector<TransactionSourceEntry> sources;
    prepareKeyInputs(context.selectedTransfers, context.outs, sources, context.mixIn);
    const std::vector<TransactionTypes::InputKeyInfo> keyInputs = convertSources(std::move(sources));
    signingKeys.keyInputs.resize(keyInputs.size());
    for (size_t i = 0; i < keyInputs.size(); ++i) {
      transaction.addInput(m_keys, keyInputs[i], signingKeys.keyInputs[i]);
    }
    for (size_t k = 0; k < context.commitmentTransfers.size(); ++k) {
      const uint64_t claimedInterest = k < context.claimedInterest.size() ? context.claimedInterest[k] : 0;
      signingKeys.commitmentSpends.push_back(prepareCommitmentSpend(
          context.commitmentTransfers[k], context.decoysByRing.at(context.commitmentRings[k]), claimedInterest));
      transaction.addInput(signingKeys.commitmentSpends.back().input);
    }
    for (size_t i = 0; i < keyInputs.size(); ++i) {
      transaction.signInputKey(i, keyInputs[i], signingKeys.keyInputs[i]);
    }
    for (size_t k = 0; k < signingKeys.commitmentSpends.size(); ++k) {
      const PreparedCommitmentSpend &spend = signingKeys.commitmentSpends[k];
      std::vector<const Crypto::PublicKey *> ring;
      for (const auto &member : spend.ring) {
        ring.push_back(&member);
      }
      transaction.signInputCommitmentSpend(keyInputs.size() + k, ring, spend.keys, spend.realIndex);
    }
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::relayBuiltTransaction(
      std::shared_ptr<SendTransactionContext> &&context, ITransaction &transaction,
      std::deque<std::unique_ptr<WalletLegacyEvent>> &events)
  {
    Transaction lowlevelTransaction = convertTransaction(transaction, static_cast<size_t>(m_upperTransactionSizeLimit));
    m_transactionsCache.updateTransaction(context->transactionId, lowlevelTransaction, context->xfgSpentOnFees,
                                          context->selectedTransfers);
    notifyBalanceChanged(events);

    if (context->isCommitmentWithdraw) {
      m_transactionsCache.getTransaction(context->transactionId).hash = transaction.getTransactionHash();
      uint64_t interestsSum = 0, totalSum = 0;
      countDepositsTotalSumAndInterestSum(context->spentDepositIds, m_transactionsCache, totalSum, interestsSum);
      UnconfirmedSpentDepositDetails unconfirmed;
      unconfirmed.depositsSum = totalSum;
      unconfirmed.fee = context->xfgSpentOnFees;
      unconfirmed.transactionId = context->transactionId;
      m_transactionsCache.addDepositSpendingTransaction(transaction.getTransactionHash(), unconfirmed);
      const std::vector<DepositId> depositIds = context->spentDepositIds;
      return std::unique_ptr<WalletRequest>(new WalletRelayDepositTransactionRequest(lowlevelTransaction,
        std::bind(&WalletTransactionSender::relayDepositTransactionCallback, this, context, depositIds,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
    }
    return std::unique_ptr<WalletRequest>(new WalletRelayTransactionRequest(lowlevelTransaction,
      std::bind(&WalletTransactionSender::relayTransactionCallback, this, context,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)));
  }

  WalletTransactionSender::PreparedCommitmentSpend WalletTransactionSender::prepareCommitmentSpend(
      const TransactionOutputInformation &transfer,
      const std::vector<COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry> &decoys,
      uint64_t claimedInterest)
  {
    PreparedCommitmentSpend spend;
    Crypto::KeyDerivation derivation;
    Crypto::generate_key_derivation(transfer.transactionPublicKey, m_keys.viewSecretKey, derivation);
    resolveCommitmentSpendKey(derivation, transfer.outputInTransaction, transfer.commitmentKey, m_keys.spendSecretKey,
                              spend.keys, spend.input.keyImage);
    wipeKeyDerivation(derivation);

    const uint32_t realGlobalIndex = static_cast<uint32_t>(transfer.globalOutputIndex);
    std::vector<std::pair<uint32_t, Crypto::PublicKey>> members;
    std::set<uint32_t> seen{realGlobalIndex};
    for (const auto &decoy : decoys) {
      if (seen.insert(decoy.global_amount_index).second) {
        members.push_back({decoy.global_amount_index, decoy.commit_key});
      }
    }
    const size_t ringSize = DynamicRingSizeCalculator::commitmentRingSize(
        members.size() + 1, m_currency.maxMixin(), m_currency.minMixin(BLOCK_MAJOR_VERSION_10));
    members.resize(std::min(members.size(), ringSize - 1));
    members.push_back({realGlobalIndex, spend.keys.publicKey});
    std::sort(members.begin(), members.end(),
              [](const std::pair<uint32_t, Crypto::PublicKey> &a, const std::pair<uint32_t, Crypto::PublicKey> &b) {
                return a.first < b.first;
              });

    spend.input.amount = transfer.amount;
    spend.input.claimedInterest = claimedInterest;
    uint32_t previousIndex = 0;
    for (size_t position = 0; position < members.size(); ++position) {
      spend.input.outputIndexes.push_back(members[position].first - previousIndex);
      previousIndex = members[position].first;
      spend.ring.push_back(members[position].second);
      if (members[position].first == realGlobalIndex) {
        spend.realIndex = position;
      }
    }
    return spend;
  }

  WalletTransactionSender::CommitmentSigningKeys::~CommitmentSigningKeys()
  {
    for (auto &keyPair : keyInputs) {
      wipeSecretKey(keyPair.secretKey);
    }
    for (auto &spend : commitmentSpends) {
      wipeSecretKey(spend.keys.secretKey);
    }
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
      CryptoNote::TransactionOutputCommitment receiveOut;
      receiveOut.commitKey = deriveSelfCommitmentKey(*transaction, commitIdx);
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

  std::unique_ptr<WalletRequest> WalletTransactionSender::makePlaceOrderV13Request(
      TransactionId& transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>>& events,
      uint8_t side, uint64_t amount, uint64_t price,
      uint32_t expiration, uint64_t fee, uint64_t mixIn) {
    throwIf(amount == 0, error::WRONG_AMOUNT);
    throwIf(side > 1, error::WRONG_AMOUNT);

    std::shared_ptr<SendTransactionContext> context = newCommitmentSpendContext(mixIn);
    const bool escrowsXfg = side == 1;
    if (!escrowsXfg) {
      attachCommitmentInputs(*context, selectSpendableDeposits(parameters::HEAT_TERM, amount), false);
    }
    const uint64_t xfgSpent = escrowsXfg ? getSumWithOverflowCheck(amount, fee) : fee;
    selectXfg(*context, xfgSpent);

    Crypto::Hash orderId;
    Crypto::generate_random_bytes(sizeof(orderId.data), orderId.data);
    context->isV11LimitDeposit = true;
    context->v11DepositSide = side;
    context->v11DepositAmount = amount;
    context->v11DepositTargetPrice = price;
    context->v11DepositExpiration = expiration;
    context->v11DepositOrderId = orderId;
    context->v11DepositAddressHash = limitOrderAddressHash(m_keys.spendSecretKey, orderId);
    context->xfgSpentOnFees = xfgSpent;
    return registerCommitmentSpend(std::move(context), transactionId, xfgSpent, fee, events);
  }

  std::unique_ptr<WalletRequest> WalletTransactionSender::makeCancelOrderV13Request(
      TransactionId& transactionId,
      std::deque<std::unique_ptr<WalletLegacyEvent>>& events,
      const Crypto::Hash& orderId, const INode::LimitDepositRpcEntry& order, uint64_t fee, uint64_t mixIn,
      bool stopOnly) {
    std::shared_ptr<SendTransactionContext> context = std::make_shared<SendTransactionContext>();
    context->dustPolicy.dustThreshold = m_currency.defaultDustThreshold();
    context->mixIn = mixIn == 0 ? m_currency.maxMixin() : mixIn;

    context->isV11LimitWithdraw = true;
    context->v11WithdrawStopOnly = stopOnly;
    context->v11WithdrawOrderId = orderId;

    Crypto::Hash ownerHash{};
    throwIf(order.order_id != Common::podToHex(orderId) || !Common::podFromHex(order.address_hash, ownerHash),
            error::WRONG_PARAMETERS);
    if (!isOwnLimitOrder(m_keys.spendSecretKey, orderId, ownerHash)) {
      throw std::system_error(make_error_code(error::WRONG_PARAMETERS), "Limit order belongs to another wallet");
    }
    context->v11WithdrawSide = order.side;
    context->v11WithdrawAmount = order.amount;
    context->v11WithdrawProceedsXfg = order.proceeds_xfg;
    context->v11WithdrawProceedsHeat = order.proceeds_heat;

    // The cancellation still pays a network fee from regular inputs; select
    // that here so change and payouts can be built deterministically below.
    uint64_t neededFee = getSumWithOverflowCheck(fee, context->dustPolicy.dustThreshold);
    context->foundMoney = selectTransfersToSend(neededFee, false, context->dustPolicy.dustThreshold, context->selectedTransfers);
    throwIf(context->foundMoney < neededFee, error::WRONG_AMOUNT);

    transactionId = recordSendHoldingInputs(*context, [&] { return m_transactionsCache.addNewTransaction(0, fee, std::string(), {}, 0, {}); });

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

      // Cancel does not spend regular UTXOs beyond the network fee — the pool
      // returns the deposit and any fill proceeds. Build the exact payout so
      // the consensus equality check verifies the full claim.
      uint64_t changeAmount = context->foundMoney - fee;
      std::vector<uint64_t> decomposedChange = splitAmount(changeAmount, context->dustPolicy.dustThreshold);
      for (uint64_t changeOut : decomposedChange)
        transaction->addOutput(changeOut, m_keys.address);

      auto addHeatCommitmentOutput = [&](uint64_t heatAmount) {
        const uint32_t outIdx = static_cast<uint32_t>(transaction->getOutputCount());
        Crypto::SecretKey txSecretKey;
        transaction->getTransactionSecretKey(txSecretKey);
        Crypto::KeyDerivation ecdh;
        Crypto::generate_key_derivation(m_keys.address.viewPublicKey, txSecretKey, ecdh);
        uint8_t preimage[36];
        memcpy(preimage, &ecdh, 32);
        preimage[32] = outIdx & 0xFF; preimage[33] = (outIdx >> 8) & 0xFF;
        preimage[34] = (outIdx >> 16) & 0xFF; preimage[35] = (outIdx >> 24) & 0xFF;
        Crypto::Hash h;
        Crypto::cn_fast_hash(preimage, sizeof(preimage), h);
        std::array<uint8_t, 32> secret;
        memcpy(secret.data(), h.data, 32);
        CryptoNote::TransactionOutputCommitment heatOut;
        heatOut.commitKey = deriveSelfCommitmentKey(*transaction, outIdx);
        heatOut.term = parameters::HEAT_TERM;
        transaction->addOutput(heatAmount, heatOut);
      };

      if (context->v11WithdrawStopOnly) {
        // Stop only: no payout; the escrow waits frozen for the withdraw.
      } else if (context->v11WithdrawSide == 1) {
        // SELL_XFG: return the remaining XFG deposit plus HEAT fill proceeds.
        if (context->v11WithdrawAmount > 0) {
          transaction->addOutput(context->v11WithdrawAmount, m_keys.address);
        }
        if (context->v11WithdrawProceedsHeat > 0) {
          addHeatCommitmentOutput(context->v11WithdrawProceedsHeat);
        }
      } else {
        // BUY_XFG: return XFG fill proceeds plus the remaining HEAT deposit.
        if (context->v11WithdrawProceedsXfg > 0) {
          transaction->addOutput(context->v11WithdrawProceedsXfg, m_keys.address);
        }
        if (context->v11WithdrawAmount > 0) {
          addHeatCommitmentOutput(context->v11WithdrawAmount);
        }
      }

      LimitOrderKeys orderKeys = deriveLimitOrderKeys(m_keys.spendSecretKey, orderId);
      std::vector<uint8_t> extra;
      if (context->v11WithdrawStopOnly) {
        Crypto::Signature proof;
        Crypto::generate_signature(getLimitCancelAuthHash(orderId, orderKeys.addressHash),
                                   orderKeys.spendPublicKey, orderKeys.spendSecretKey, proof);
        addOrderCancelToExtra(extra, orderId, orderKeys.spendPublicKey, orderKeys.viewPublicKey, proof);
      } else {
        Crypto::Hash outputsHash = getLimitWithdrawOutputHash(transaction->getTransactionPrefix().outputs);
        Crypto::Hash authHash = getLimitWithdrawAuthHash(orderId, orderKeys.addressHash, outputsHash);
        Crypto::Signature proof;
        Crypto::generate_signature(authHash, orderKeys.spendPublicKey, orderKeys.spendSecretKey, proof);
        addLimitWithdrawToExtra(extra, orderId, orderKeys.spendPublicKey,
                                orderKeys.viewPublicKey, outputsHash, proof);
      }
      wipeLimitOrderSecret(orderKeys);
      CryptoNote::BinaryArray extraData(extra.begin(), extra.end());
      transaction->appendExtra(extraData);

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
