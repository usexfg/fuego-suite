// Copyright (c) 2017-2026 Fuego Developers
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

#include "IndexManager.h"

namespace CryptoNote {

void IndexManager::clear() {
    m_spentKeys.clear();
    m_outputs.clear();
    m_multisigOutputs.clear();
    m_commitmentOutputs.clear();
    m_allUnifiedOutputs.clear();
    m_transactionMap.clear();
}

} // namespace CryptoNote
