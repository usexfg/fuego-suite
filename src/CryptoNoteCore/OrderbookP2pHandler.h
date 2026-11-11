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

#pragma once

#include "OrderbookTypes.h"

namespace CryptoNote {

class OrderbookMempool;

class OrderbookP2pHandler {
public:
  OrderbookP2pHandler(OrderbookMempool& mempool);

  bool handleOrderPlace(const Order& order);
  bool handleOrderCancel(const Crypto::Hash& orderId);

  // Called when a settlement block is received with a receipt
  void handleReceipt(const OrderbookReceipt& receipt);

private:
  OrderbookMempool& m_mempool;
};

} // namespace CryptoNote
