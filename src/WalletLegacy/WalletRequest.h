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

#pragma once

#include "INode.h"
// #include "WalletSynchronizationContext.h"
#include "WalletLegacy/WalletSendTransactionContext.h"
#include "WalletLegacy/WalletLegacyEvent.h"

#include <boost/optional.hpp>

#include <deque>
#include <functional>
#include <memory>

namespace CryptoNote {

class WalletRequest
{
public:
  typedef std::function<void(std::deque<std::unique_ptr<WalletLegacyEvent>>&, std::unique_ptr<WalletRequest>&, std::error_code)> Callback;

  virtual ~WalletRequest() {};

  virtual void perform(INode& node, std::function<void (WalletRequest::Callback, std::error_code)> cb) = 0;
};

class WalletGetRandomOutsByAmountsRequest: public WalletRequest
{
public:
  WalletGetRandomOutsByAmountsRequest(const std::vector<uint64_t>& amounts, uint64_t outsCount, std::shared_ptr<SendTransactionContext> context, Callback cb) :
    m_amounts(amounts), m_outsCount(outsCount), m_context(context), m_cb(cb) {};

  virtual ~WalletGetRandomOutsByAmountsRequest() {};

  virtual void perform(INode& node, std::function<void (WalletRequest::Callback, std::error_code)> cb) override
  {
    node.getRandomOutsByAmounts(std::move(m_amounts), m_outsCount, std::ref(m_context->outs), std::bind(cb, m_cb, std::placeholders::_1));
  };

private:
  std::vector<uint64_t> m_amounts;
  uint64_t m_outsCount;
  std::shared_ptr<SendTransactionContext> m_context;
  Callback m_cb;
};

class WalletGetRandomCommitmentOutsRequest: public WalletRequest
{
public:
  WalletGetRandomCommitmentOutsRequest(uint64_t amount, uint64_t outsCount, uint32_t maxHeight,
      std::shared_ptr<SendTransactionContext> context, Callback cb)
    : m_amount(amount), m_outsCount(outsCount), m_maxHeight(maxHeight), m_context(context), m_cb(cb) {}

  virtual ~WalletGetRandomCommitmentOutsRequest() {}

  virtual void perform(INode& node, std::function<void(WalletRequest::Callback, std::error_code)> cb) override
  {
    node.getRandomCommitmentOutsForAmount(m_amount, m_outsCount, m_maxHeight,
      std::ref(m_context->commitmentOuts), std::bind(cb, m_cb, std::placeholders::_1));
  }

private:
  uint64_t m_amount;
  uint64_t m_outsCount;
  uint32_t m_maxHeight;
  std::shared_ptr<SendTransactionContext> m_context;
  Callback m_cb;
};

class WalletRelayTransactionRequest: public WalletRequest
{
public:
  WalletRelayTransactionRequest(const CryptoNote::Transaction& tx, Callback cb) : m_tx(tx), m_cb(cb) {};
  virtual ~WalletRelayTransactionRequest() {};

  virtual void perform(INode& node, std::function<void (WalletRequest::Callback, std::error_code)> cb) override
  {
    node.relayTransaction(m_tx, std::bind(cb, m_cb, std::placeholders::_1));
  }

private:
  CryptoNote::Transaction m_tx;
  Callback m_cb;
};

class WalletRelayDepositTransactionRequest final: public WalletRequest
{
public:
  WalletRelayDepositTransactionRequest(const Transaction& tx, Callback cb) : m_tx(tx), m_cb(cb) {}
  virtual ~WalletRelayDepositTransactionRequest() {}

  virtual void perform(INode& node, std::function<void (WalletRequest::Callback, std::error_code)> cb)
  {
    node.relayTransaction(m_tx, std::bind(cb, m_cb, std::placeholders::_1));
  }

private:
  Transaction m_tx;
  Callback m_cb;
};

} //namespace CryptoNote
