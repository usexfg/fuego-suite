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

#include "Common/ObserverManager.h"

namespace CryptoNote {

template <typename Observer, typename Base>
class IObservableImpl : public Base {
public:

  virtual void addObserver(Observer* observer) override {
    m_observerManager.add(observer);
  }

  virtual void removeObserver(Observer* observer) override {
    m_observerManager.remove(observer);
  }

protected:
  Tools::ObserverManager<Observer> m_observerManager;
};

}
