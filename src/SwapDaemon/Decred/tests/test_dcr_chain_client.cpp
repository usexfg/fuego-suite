#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "SwapDaemon/Decred/DcrChainClient.h"

using namespace XfgSwap;

static void test_chain_name() {
  std::cout << "test_chain_name..." << std::endl;

  auto rpc = std::make_unique<DcrRpcClient>("127.0.0.1", 9108);
  DcrChainClient client(std::move(rpc));

  assert(client.chainName() == "DCR");

  std::cout << "  PASSED" << std::endl;
}

static void test_lock_error() {
  std::cout << "test_lock_error..." << std::endl;

  auto rpc = std::make_unique<DcrRpcClient>("127.0.0.1", 9108);
  DcrChainClient client(std::move(rpc));

  SwapParams params;
  auto result = client.lock(params);
  assert(!result.success);
  assert(result.error.find("DCR") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_verifyLock_error() {
  std::cout << "test_verifyLock_error..." << std::endl;

  auto rpc = std::make_unique<DcrRpcClient>("127.0.0.1", 9108);
  DcrChainClient client(std::move(rpc));

  SwapParams params;
  params.ctrLockTxId = "deadbeef";
  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("DCR") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_claim_error() {
  std::cout << "test_claim_error..." << std::endl;

  auto rpc = std::make_unique<DcrRpcClient>("127.0.0.1", 9108);
  DcrChainClient client(std::move(rpc));

  SwapParams params;
  auto result = client.claim(params);
  assert(!result.success);
  assert(result.error.find("DCR") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_refund_error() {
  std::cout << "test_refund_error..." << std::endl;

  auto rpc = std::make_unique<DcrRpcClient>("127.0.0.1", 9108);
  DcrChainClient client(std::move(rpc));

  SwapParams params;
  auto result = client.refund(params);
  assert(!result.success);
  assert(result.error.find("DCR") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

int main() {
  std::cout << "=== DcrChainClient Tests ===" << std::endl;

  test_chain_name();
  test_lock_error();
  test_verifyLock_error();
  test_claim_error();
  test_refund_error();

  std::cout << "\nAll tests passed." << std::endl;
  return 0;
}
