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

#include "CryptoNoteCore/BlockchainMessages.h"

namespace CryptoNote {

NewBlockMessage::NewBlockMessage(const Crypto::Hash& hash) : blockHash(hash) {}

void NewBlockMessage::get(Crypto::Hash& hash) const {
  hash = blockHash;
}

NewAlternativeBlockMessage::NewAlternativeBlockMessage(const Crypto::Hash& hash) : blockHash(hash) {}

void NewAlternativeBlockMessage::get(Crypto::Hash& hash) const {
  hash = blockHash;
}

ChainSwitchMessage::ChainSwitchMessage(std::vector<Crypto::Hash>&& hashes) : blocksFromCommonRoot(std::move(hashes)) {}

ChainSwitchMessage::ChainSwitchMessage(const ChainSwitchMessage& other) : blocksFromCommonRoot(other.blocksFromCommonRoot) {}

void ChainSwitchMessage::get(std::vector<Crypto::Hash>& hashes) const {
  hashes = blocksFromCommonRoot;
}

BlockchainMessage::BlockchainMessage(NewBlockMessage&& message) : type(MessageType::NEW_BLOCK_MESSAGE), newBlockMessage(std::move(message)) {}

BlockchainMessage::BlockchainMessage(NewAlternativeBlockMessage&& message) : type(MessageType::NEW_ALTERNATIVE_BLOCK_MESSAGE), newAlternativeBlockMessage(std::move(message)) {}

BlockchainMessage::BlockchainMessage(ChainSwitchMessage&& message) : type(MessageType::CHAIN_SWITCH_MESSAGE) {
	chainSwitchMessage = new ChainSwitchMessage(std::move(message));
}

BlockchainMessage::BlockchainMessage(const BlockchainMessage& other) : type(other.type) {
  switch (type) {
    case MessageType::NEW_BLOCK_MESSAGE:
      new (&newBlockMessage) NewBlockMessage(other.newBlockMessage);
      break;
    case MessageType::NEW_ALTERNATIVE_BLOCK_MESSAGE:
      new (&newAlternativeBlockMessage) NewAlternativeBlockMessage(other.newAlternativeBlockMessage);
      break;
    case MessageType::CHAIN_SWITCH_MESSAGE:
	  chainSwitchMessage = new ChainSwitchMessage(*other.chainSwitchMessage);
      break;
  }
}

BlockchainMessage::~BlockchainMessage() {
  switch (type) {
    case MessageType::NEW_BLOCK_MESSAGE:
      newBlockMessage.~NewBlockMessage();
      break;
    case MessageType::NEW_ALTERNATIVE_BLOCK_MESSAGE:
      newAlternativeBlockMessage.~NewAlternativeBlockMessage();
      break;
    case MessageType::CHAIN_SWITCH_MESSAGE:
	  delete chainSwitchMessage;
      break;
  }
}

BlockchainMessage::MessageType BlockchainMessage::getType() const {
  return type;
}

bool BlockchainMessage::getNewBlockHash(Crypto::Hash& hash) const {
  if (type == MessageType::NEW_BLOCK_MESSAGE) {
    newBlockMessage.get(hash);
    return true;
  } else {
    return false;
  }
}

bool BlockchainMessage::getNewAlternativeBlockHash(Crypto::Hash& hash) const {
  if (type == MessageType::NEW_ALTERNATIVE_BLOCK_MESSAGE) {
    newAlternativeBlockMessage.get(hash);
    return true;
  } else {
    return false;
  }
}

bool BlockchainMessage::getChainSwitch(std::vector<Crypto::Hash>& hashes) const {
  if (type == MessageType::CHAIN_SWITCH_MESSAGE) {
    chainSwitchMessage->get(hashes);
    return true;
  } else {
    return false;
  }
}

}
