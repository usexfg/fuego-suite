// Copyright (c) 2017-2025 Elderfire Privacy Council
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>

#pragma once

#include <vector>

#include <CryptoNote.h>

namespace CryptoNote {

class NewBlockMessage {
public:
  NewBlockMessage(const Crypto::Hash& hash);
  NewBlockMessage() = default;
  void get(Crypto::Hash& hash) const;
private:
  Crypto::Hash blockHash;
};

class NewAlternativeBlockMessage {
public:
  NewAlternativeBlockMessage(const Crypto::Hash& hash);
  NewAlternativeBlockMessage() = default;
  void get(Crypto::Hash& hash) const;
private:
  Crypto::Hash blockHash;
};

class ChainSwitchMessage {
public:
  ChainSwitchMessage(std::vector<Crypto::Hash>&& hashes);
  ChainSwitchMessage(const ChainSwitchMessage& other);
  void get(std::vector<Crypto::Hash>& hashes) const;
private:
  std::vector<Crypto::Hash> blocksFromCommonRoot;
};

class BlockchainMessage {
public:
  enum class MessageType {
    NEW_BLOCK_MESSAGE,
    NEW_ALTERNATIVE_BLOCK_MESSAGE,
    CHAIN_SWITCH_MESSAGE
  };

  BlockchainMessage(NewBlockMessage&& message);
  BlockchainMessage(NewAlternativeBlockMessage&& message);
  BlockchainMessage(ChainSwitchMessage&& message);

  BlockchainMessage(const BlockchainMessage& other);

  ~BlockchainMessage();

  MessageType getType() const;

  bool getNewBlockHash(Crypto::Hash& hash) const;
  bool getNewAlternativeBlockHash(Crypto::Hash& hash) const;
  bool getChainSwitch(std::vector<Crypto::Hash>& hashes) const;
private:
  const MessageType type;

  union {
    NewBlockMessage newBlockMessage;
    NewAlternativeBlockMessage newAlternativeBlockMessage;
    ChainSwitchMessage* chainSwitchMessage;
  };
};

}
