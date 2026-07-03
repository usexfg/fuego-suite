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

namespace CryptoNote {

OrderbookP2pHandler::OrderbookP2pHandler(OrderbookMempool& mempool)
  : m_mempool(mempool) {}

bool OrderbookP2pHandler::handleOrderPlace(const Order& order) {
  if (order.amount == 0 || order.price == 0) return false;
  if (order.expiration > 0 && order.expiration < 1000) return false; // too short

  return m_mempool.addOrder(order);
}

bool OrderbookP2pHandler::handleOrderCancel(const Crypto::Hash& orderId) {
  return m_mempool.cancelOrder(orderId);
}

void OrderbookP2pHandler::handleReceipt(const OrderbookReceipt& receipt) {
  m_mempool.restoreFromReceipt(receipt);
}

} // namespace CryptoNote
