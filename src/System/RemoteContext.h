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

#include <cassert>
#include <System/Dispatcher.h>
#include <System/Event.h>
#include <System/Future.h>
#include <System/InterruptedException.h>

namespace System {

template<class T = void> class RemoteContext {
public:
  // Start a thread, execute operation in it, continue execution of current context.
  RemoteContext(Dispatcher& d, std::function<T()>&& operation)
      : dispatcher(d), event(d), procedure(std::move(operation)), future(System::Detail::async<T>([this] { return asyncProcedure(); })), interrupted(false) {
  }

  // Run other task on dispatcher until future is ready, then return lambda's result, or rethrow exception. UB if called more than once.
  T get() const {
    wait();
    return future.get();
  }

  // Run other task on dispatcher until future is ready.
  void wait() const {
    while (!event.get()) {
      try {
        event.wait();
      } catch (InterruptedException&) {
        interrupted = true;
      }
    }

    if (interrupted) {
      dispatcher.interrupt();
    }
  }

  // Wait future to complete.
  ~RemoteContext() {
    try {
      wait();
    } catch (std::exception&) {
    }

    try {
      // windows future implementation doesn't wait for completion on destruction
      if (future.valid()) {
        future.wait();
      }
    } catch (std::exception&) {
    }
  }

private:
  struct NotifyOnDestruction {
    NotifyOnDestruction(Dispatcher& d, Event& e) : dispatcher(d), event(e) {
    }

    ~NotifyOnDestruction() {
      // make a local copy; event reference will be dead when function is called
      auto localEvent = &event;
      // die if this throws...
      dispatcher.remoteSpawn([=] { localEvent->set(); });
    }

    Dispatcher& dispatcher;
    Event& event;
  };

  // This function is executed in future object
  T asyncProcedure() {
    NotifyOnDestruction guard(dispatcher, event);
    assert(procedure != nullptr);
    return procedure();
  }

  Dispatcher& dispatcher;
  mutable Event event;
  std::function<T()> procedure;
  mutable System::Detail::Future<T> future;
  mutable bool interrupted;
};

}
