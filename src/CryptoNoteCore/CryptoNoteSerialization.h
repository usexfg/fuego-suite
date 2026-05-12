// Copyright (c) 2017-2025 Elderfire Privacy Council
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
#include "crypto/chacha8.h"
#include "Serialization/ISerializer.h"
#include "crypto/crypto.h"

namespace Crypto {

bool serialize(PublicKey& pubKey, Common::StringView name, CryptoNote::ISerializer& serializer);
bool serialize(SecretKey& secKey, Common::StringView name, CryptoNote::ISerializer& serializer);
bool serialize(Hash& h, Common::StringView name, CryptoNote::ISerializer& serializer);
bool serialize(chacha8_iv &chacha8, Common::StringView name, CryptoNote::ISerializer &serializer);
bool serialize(KeyImage &keyImage, Common::StringView name, CryptoNote::ISerializer &serializer);
bool serialize(Signature& sig, Common::StringView name, CryptoNote::ISerializer& serializer);
bool serialize(EllipticCurveScalar& ecScalar, Common::StringView name, CryptoNote::ISerializer& serializer);
bool serialize(EllipticCurvePoint& ecPoint, Common::StringView name, CryptoNote::ISerializer& serializer);

}

namespace CryptoNote {

struct AccountKeys;
struct TransactionExtraMergeMiningTag;

void serialize(TransactionPrefix& txP, ISerializer& serializer);
void serialize(Transaction& tx, ISerializer& serializer);
void serialize(TransactionInput& in, ISerializer& serializer);
void serialize(TransactionOutput& in, ISerializer& serializer);

void serialize(BaseInput& gen, ISerializer& serializer);
void serialize(KeyInput& key, ISerializer& serializer);
void serialize(MultisignatureInput& multisignature, ISerializer& serializer);

void serialize(TransactionOutput& output, ISerializer& serializer);
void serialize(TransactionOutputTarget& output, ISerializer& serializer);
void serialize(KeyOutput& key, ISerializer& serializer);
void serialize(MultisignatureOutput& multisignature, ISerializer& serializer);

void serialize(BlockHeader& header, ISerializer& serializer);
void serialize(Block& block, ISerializer& serializer);
void serialize(ParentBlockSerializer& pbs, ISerializer& serializer);
void serialize(TransactionExtraMergeMiningTag& tag, ISerializer& serializer);

void serialize(AccountPublicAddress& address, ISerializer& serializer);
void serialize(AccountKeys& keys, ISerializer& s);
void serialize(TransactionInputs &inputs, ISerializer &serializer);

void serialize(KeyPair& keyPair, ISerializer& serializer);

}
