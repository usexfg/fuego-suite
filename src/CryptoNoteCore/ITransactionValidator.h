// Copyright (c) 2017-2026 Fuego Developers
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

#include "CryptoNoteBasic.h"

#include <string>

namespace CryptoNote {

  struct BlockInfo {
    uint32_t height;
    Crypto::Hash id;

    BlockInfo() {
      clear();
    }

    void clear() {
      height = 0;
      id = CryptoNote::NULL_HASH;
    }

    bool empty() const {
      return id == CryptoNote::NULL_HASH;
    }
  };

  class ITransactionValidator {
  public:
    virtual ~ITransactionValidator() {}

    virtual bool checkTransactionInputs(const CryptoNote::Transaction& tx, BlockInfo& maxUsedBlock) = 0;
    virtual bool checkTransactionInputs(const CryptoNote::Transaction& tx, BlockInfo& maxUsedBlock, BlockInfo& lastFailed) = 0;
    virtual bool haveSpentKeyImages(const CryptoNote::Transaction& tx) = 0;
    virtual bool checkTransactionSize(size_t blobSize) = 0;

    // Pre-check of the v11+ settlement tags for mempool admission and block
    // template selection. Returns false, with a reason, only when the
    // transaction is CERTAIN to be rejected by block validation.
    //
    // A true return is not a guarantee of validity — pushBlock remains
    // authoritative. This deliberately implements only checks with no false
    // positives: rejecting a transaction that would in fact be valid is
    // censorship. In particular it does NOT mirror the AMM rate checks, whose
    // inputs (pool reserves) move with every block, so a verdict taken at
    // admission would be stale by the time a template is built.
    virtual bool checkTransactionSettlement(const CryptoNote::Transaction& tx, std::string& reason) = 0;
  };

}
