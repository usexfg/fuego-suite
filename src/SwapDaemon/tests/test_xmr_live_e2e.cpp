// Copyright (c) 2017-2026 Fuego Developers
//
// MANUAL live e2e for the XMR Monero-side lock/sweep mechanics (NOT CI;
// requires a running private-testnet monerod + monero-wallet-rpc with a funded
// open wallet). Drives the daemon's real MoneroRpcClient:
//   lock  : createSharedAddress(A+B) -> transferToShared(amount)
//   sweep : sweepSharedAddress(combined a+b spend, av+bv view) -> dest
// proving a real on-chain transfer to a computed shared address can be swept
// with the combined key. State (combined keys + shared addr) is passed between
// the two invocations via a temp file; mining happens in between (shell).
//
// Usage:
//   test_xmr_live_e2e lock  <amountPiconero> <walletPort>
//   test_xmr_live_e2e sweep <destAddress> <targetHeight> <walletPort>

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "SwapDaemon/Monero/MoneroRpcClient.h"
#include "SwapDaemon/Monero/MoneroAddress.h"
#include "crypto/crypto.h"

extern "C" {
#include "crypto/crypto-ops.h"
}

using namespace XfgSwap;

static std::string hexenc(const uint8_t* p, int n) {
  static const char* h = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < n; ++i) { s += h[p[i] >> 4]; s += h[p[i] & 15]; }
  return s;
}

static const char* STATE = "/tmp/xmr_live_state.txt";

int main(int argc, char** argv) {
  if (argc < 3) { std::cerr << "need mode + args\n"; return 2; }
  const std::string mode = argv[1];

  if (mode == "lock") {
    uint64_t amount = std::strtoull(argv[2], nullptr, 10);
    uint16_t wport = (argc > 3) ? (uint16_t)std::strtoul(argv[3], nullptr, 10) : 28083;
    MoneroRpcClient rpc("127.0.0.1", 28081, "127.0.0.1", wport);

    // Two parties' spend + view key shares.
    Crypto::PublicKey As, Bs, Av, Bv;
    Crypto::SecretKey as, bs, av, bv;
    Crypto::generate_keys(As, as);
    Crypto::generate_keys(Bs, bs);
    Crypto::generate_keys(Av, av);
    Crypto::generate_keys(Bv, bv);

    // Combined secrets (2-term, the correct model): a+b spend, av+bv view.
    unsigned char cs[32], cv[32];
    sc_add(cs, as.data, bs.data); sc_reduce32(cs);
    sc_add(cv, av.data, bv.data); sc_reduce32(cv);

    std::string shared;
    if (!rpc.createSharedAddress(hexenc(As.data, 32), hexenc(Bs.data, 32),
                                 hexenc(Av.data, 32), hexenc(Bv.data, 32),
                                 shared, MoneroAddress::TESTNET)) {
      std::cerr << "createSharedAddress failed\n"; return 1;
    }
    std::cout << "shared address: " << shared << "\n";

    // Record the daemon height at lock time so the sweep restores the from-keys
    // wallet from the lock block instead of rescanning from genesis.
    uint64_t lockHeight = 0;
    rpc.getHeight(lockHeight);
    uint64_t restoreFrom = (lockHeight > 2) ? (lockHeight - 2) : 1;

    MoneroTransferResult tr;
    bool ok = rpc.transferToShared(shared, amount, tr);
    std::cout << "transferToShared ok=" << ok << " success=" << tr.success
              << " tx=" << tr.txHash << " err=" << tr.error << "\n";
    if (!ok || !tr.success) return 1;

    std::ofstream f(STATE);
    f << hexenc(cs, 32) << "\n" << hexenc(cv, 32) << "\n" << shared << "\n"
      << restoreFrom << "\n";
    std::cout << "LOCK OK (state written, restoreHeight=" << restoreFrom << ")\n";
    return 0;
  }

  if (mode == "sweep") {
    std::string dest = argv[2];
    uint64_t target = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 0;
    uint16_t wport = (argc > 4) ? (uint16_t)std::strtoul(argv[4], nullptr, 10) : 28083;
    MoneroRpcClient rpc("127.0.0.1", 28081, "127.0.0.1", wport);

    std::string cs, cv, shared, restoreStr;
    std::ifstream f(STATE);
    std::getline(f, cs); std::getline(f, cv); std::getline(f, shared);
    std::getline(f, restoreStr);
    if (cs.size() != 64 || cv.size() != 64) { std::cerr << "bad state\n"; return 2; }
    uint64_t restoreHeight = restoreStr.empty() ? 1 : std::strtoull(restoreStr.c_str(), nullptr, 10);

    MoneroTransferResult sr;
    // Unique wallet name per run so generate_from_keys never collides with a
    // leftover wallet file from a prior attempt.
    std::string wname = "live_" + std::to_string((unsigned long long)::time(nullptr));
    std::cout << "sweep restoreHeight=" << restoreHeight << " targetHeight=" << target << "\n";
    bool ok = rpc.sweepSharedAddress(cs, cv, dest, sr, wname, restoreHeight,
                                     target, MoneroAddress::TESTNET);
    std::cout << "sweepSharedAddress ok=" << ok << " success=" << sr.success
              << " tx=" << sr.txHash << " err=" << sr.error << "\n";
    if (!ok || !sr.success) return 1;
    std::cout << "SWEEP OK\n";
    return 0;
  }

  std::cerr << "unknown mode\n";
  return 2;
}
