// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// walletd JSON-RPC createSubaddress / getSubaddresses through the production
// PaymentServiceJsonRpcServer, WalletService and WalletGreen.

#include <cstdio>
#include <string>

#include "gtest/gtest.h"

#include "Common/JsonValue.h"
#include "CryptoNoteCore/Currency.h"
#include "Logging/ConsoleLogger.h"
#include "NullNode.h"
#include "PaymentGate/PaymentServiceJsonRpcServer.h"
#include "PaymentGate/WalletService.h"
#include "System/Dispatcher.h"
#include "System/Event.h"
#include "Wallet/WalletGreen.h"

using namespace CryptoNote;
using Common::JsonValue;

namespace {

class RpcProbe : public PaymentService::PaymentServiceJsonRpcServer {
public:
  using PaymentService::PaymentServiceJsonRpcServer::PaymentServiceJsonRpcServer;
  using PaymentService::PaymentServiceJsonRpcServer::processJsonRpcRequest;
};

class WalletdSubaddressRpc : public ::testing::Test {
protected:
  WalletdSubaddressRpc()
      : logger(Logging::ERROR),
        currency(CurrencyBuilder(logger).currency()),
        stopEvent(dispatcher) {
    config.walletFile = ::testing::TempDir() + "walletd_subaddress_rpc.wallet";
    config.walletPassword = "password";
    std::remove(config.walletFile.c_str());
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& creator = green;
    creator.initialize(config.walletFile, config.walletPassword);
    creator.createAddress();
    creator.save();
    creator.shutdown();
  }

  ~WalletdSubaddressRpc() override { std::remove(config.walletFile.c_str()); }

  JsonValue call(RpcProbe& rpc, const std::string& method, const std::string& params) {
    const JsonValue request = JsonValue::fromString(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + method + "\",\"params\":" + params + "}");
    JsonValue response(JsonValue::OBJECT);
    rpc.processJsonRpcRequest(request, response);
    return response;
  }

  Logging::ConsoleLogger logger;
  Currency currency;
  System::Dispatcher dispatcher;
  System::Event stopEvent;
  NullNode node;
  PaymentService::WalletConfiguration config;
};

TEST_F(WalletdSubaddressRpc, CreatesAndListsSubaddresses) {
  WalletGreen wallet(dispatcher, currency, node, logger);
  PaymentService::WalletService service(currency, dispatcher, node, wallet, wallet, config, logger);
  service.init();
  RpcProbe rpc(dispatcher, stopEvent, service, logger);

  const JsonValue created = call(rpc, "createSubaddress", "{\"major\":2,\"minor\":5}");
  ASSERT_TRUE(created.contains("result")) << created.toString();
  const std::string address = created("result")("address").getString();
  EXPECT_EQ(address.compare(0, 5, "funsa"), 0) << address;

  const JsonValue listed = call(rpc, "getSubaddresses", "{}");
  ASSERT_TRUE(listed.contains("result")) << listed.toString();
  const JsonValue& subaddresses = listed("result")("subaddresses");
  ASSERT_EQ(subaddresses.size(), 1u);
  EXPECT_EQ(subaddresses[0]("major").getInteger(), 2);
  EXPECT_EQ(subaddresses[0]("minor").getInteger(), 5);
  EXPECT_EQ(subaddresses[0]("address").getString(), address);

  const JsonValue addresses = call(rpc, "getAddresses", "{}");
  ASSERT_TRUE(addresses.contains("result")) << addresses.toString();
  EXPECT_EQ(addresses("result")("addresses")[1].getString(), address);
}

TEST_F(WalletdSubaddressRpc, RefusesThePrimaryIndexADuplicateAndMissingIndices) {
  WalletGreen wallet(dispatcher, currency, node, logger);
  PaymentService::WalletService service(currency, dispatcher, node, wallet, wallet, config, logger);
  service.init();
  RpcProbe rpc(dispatcher, stopEvent, service, logger);

  EXPECT_TRUE(call(rpc, "createSubaddress", "{\"major\":0,\"minor\":0}").contains("error"));
  ASSERT_TRUE(call(rpc, "createSubaddress", "{\"major\":1,\"minor\":1}").contains("result"));
  EXPECT_TRUE(call(rpc, "createSubaddress", "{\"major\":1,\"minor\":1}").contains("error"));
  EXPECT_TRUE(call(rpc, "createSubaddress", "{\"major\":1}").contains("error"));
  EXPECT_EQ(call(rpc, "getSubaddresses", "{}")("result")("subaddresses").size(), 1u);
}

} // namespace
