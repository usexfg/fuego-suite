// Copyright (c) 2017-2025 Elderfire Privacy Council
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2014-2017 The XDN developers
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "StagedDepositRpc.h"
#include "CryptoNoteCore/StagedUnlock.h"
#include "Serialization/SerializationOverloads.h"
#include "Serialization/ISerializer.h"

namespace PaymentService {

// GetStagedUnlockSchedule implementation
void GetStagedUnlockSchedule::Request::serialize(CryptoNote::ISerializer& serializer) {
    serializer(depositId, "depositId");
}

void GetStagedUnlockSchedule::Response::serialize(CryptoNote::ISerializer& serializer) {
    serializer(useStagedUnlock, "useStagedUnlock");
    serializer(stages, "stages");
    serializer(status, "status");
    serializer(totalUnlockedAmount, "totalUnlockedAmount");
    serializer(remainingLockedAmount, "remainingLockedAmount");
}

// GetStagedDeposits implementation
void GetStagedDeposits::Request::serialize(CryptoNote::ISerializer& serializer) {
    // No parameters
}

void GetStagedDeposits::Response::serialize(CryptoNote::ISerializer& serializer) {
    serializer(deposits, "deposits");
    serializer(totalUnlockedAmount, "totalUnlockedAmount");
    serializer(totalRemainingLockedAmount, "totalRemainingLockedAmount");
}






// ProcessStagedUnlocks implementation
void ProcessStagedUnlocks::Request::serialize(CryptoNote::ISerializer& serializer) {
    // No parameters
}

void ProcessStagedUnlocks::Response::serialize(CryptoNote::ISerializer& serializer) {
    serializer(newlyUnlockedDeposits, "newlyUnlockedDeposits");
    serializer(totalUnlockedAmount, "totalUnlockedAmount");
    serializer(message, "message");
}

} // namespace PaymentService

// StagedDepositInfo implementation
void StagedDepositInfo::serialize(CryptoNote::ISerializer& serializer) {
    serializer(depositId, "depositId");
    serializer(amount, "amount");
    serializer(term, "term");
    serializer(interest, "interest");
    serializer(height, "height");
    serializer(unlockHeight, "unlockHeight");
    serializer(locked, "locked");
    serializer(useStagedUnlock, "useStagedUnlock");
    serializer(stages, "stages");
    serializer(status, "status");
    serializer(totalUnlockedAmount, "totalUnlockedAmount");
    serializer(remainingLockedAmount, "remainingLockedAmount");
    serializer(creatingTransactionHash, "creatingTransactionHash");
    serializer(spendingTransactionHash, "spendingTransactionHash");
    serializer(address, "address");
}