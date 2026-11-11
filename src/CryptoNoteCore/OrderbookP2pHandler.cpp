// Copyright (c) 2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "OrderbookP2pHandler.h"
#include "OrderbookMempool.h"
#include <cstring>

namespace CryptoNote {

OrderbookP2pHandler::OrderbookP2pHandler(OrderbookMempool& mempool)
  : m_mempool(mempool) {}

bool OrderbookP2pHandler::handleOrderPlace(const Order& order) {
  if (order.amount == 0 || order.price == 0) return false;
  if (order.expiration > 0 && order.expiration < 1000) return false; // too short

  // Signature / identity validation (legacy multi-party order model):
  // - a valid order must carry a non-zero sender identity hash
  // - must include at least one partial signature as proof of intent
  // - partial sig count is bounded to prevent memory exhaustion
  static const size_t MAX_PARTIAL_SIGS = 8;
  static const Crypto::Hash kZeroHash{};
  if (memcmp(order.addressHash.data, kZeroHash.data, sizeof(order.addressHash.data)) == 0) return false;
  if (order.partialSigs.empty()) return false;
  if (order.partialSigs.size() > MAX_PARTIAL_SIGS) return false;

  return m_mempool.addOrder(order);
}

bool OrderbookP2pHandler::handleOrderCancel(const Crypto::Hash& orderId) {
  return m_mempool.cancelOrder(orderId);
}

void OrderbookP2pHandler::handleReceipt(const OrderbookReceipt& receipt) {
  m_mempool.restoreFromReceipt(receipt);
}

} // namespace CryptoNote
