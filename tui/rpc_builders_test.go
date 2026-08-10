package main

import (
	"strings"
	"testing"
)

func mustWallet(t *testing.T, c RpcCall, method string) {
	t.Helper()
	if c.Kind != RpcWallet {
		t.Fatalf("%s: kind=%v want RpcWallet", method, c.Kind)
	}
	if c.Method != method {
		t.Fatalf("method=%q want %q", c.Method, method)
	}
	if c.Params == nil {
		t.Fatalf("%s: nil params", method)
	}
}

func TestBuildHeatMint_NotLegacyBurnDeposit(t *testing.T) {
	c := BuildHeatMint(10_000_000, 1_000_000, DefaultMixin())
	mustWallet(t, c, "heat_mint")
	if c.Method == "createBurnDeposit" {
		t.Fatal("must not use legacy createBurnDeposit for mint")
	}
	if c.Params["xfg_burned"].(uint64) != 10_000_000 {
		t.Fatalf("xfg_burned=%v", c.Params["xfg_burned"])
	}
	if c.Params["heat_minted"].(uint64) != 1_000_000 {
		t.Fatalf("heat_minted=%v", c.Params["heat_minted"])
	}
	if c.Params["mixin"].(uint64) != DynamaxMixin {
		t.Fatalf("mixin=%v want dynamax 0", c.Params["mixin"])
	}
}

func TestEstimateHeatMinted_PoolAndLaunch(t *testing.T) {
	// pool: 100 XFG : 10 HEAT → burn 10 XFG → 1 HEAT
	got := EstimateHeatMinted(10_000_000, 100_000_000, 10_000_000)
	if got != 1_000_000 {
		t.Fatalf("pool estimate=%d want 1000000", got)
	}
	// empty pool → 10:1 launch
	got2 := EstimateHeatMinted(10_000_000, 0, 0)
	if got2 != 1_000_000 {
		t.Fatalf("launch estimate=%d want 1000000", got2)
	}
}

func TestBuildHeatDeposit_NotCreateDepositMint(t *testing.T) {
	c := BuildHeatDeposit(5_000_000, 12, DefaultMixin())
	mustWallet(t, c, "heat_deposit")
	if c.Method == "createDeposit" {
		t.Fatal("primary HEAT CD path must not be createDeposit")
	}
	if c.Params["amount"].(uint64) != 5_000_000 {
		t.Fatalf("amount=%v", c.Params["amount"])
	}
	if c.Params["term_epochs"].(uint32) != 12 {
		t.Fatalf("term_epochs=%v", c.Params["term_epochs"])
	}
	if c.Params["mixin"].(uint64) != DynamaxMixin {
		t.Fatalf("mixin=%v", c.Params["mixin"])
	}
}

func TestBuildGetAndWithdrawDeposit(t *testing.T) {
	g := BuildGetDeposit(7)
	mustWallet(t, g, "getDeposit")
	if g.Params["depositId"].(uint64) != 7 {
		t.Fatal("depositId")
	}
	w := BuildWithdrawDeposit(7)
	mustWallet(t, w, "withdrawDeposit")
}

func TestBuildAmmSwap_DynamaxMixin(t *testing.T) {
	c := BuildAmmSwap(0, 1_000_000, 900_000, DefaultMixin())
	mustWallet(t, c, "amm_swap")
	if c.Params["direction"].(uint8) != 0 {
		t.Fatal("direction")
	}
	if c.Params["mixin"].(uint64) != DynamaxMixin {
		t.Fatal("mixin")
	}
	if c.Params["input_amount"].(uint64) != 1_000_000 {
		t.Fatal("input_amount")
	}
}

func TestBuildLimitOrders(t *testing.T) {
	p := BuildPlaceLimitOrder(1, 2_000_000, 15_800_000, 0, DefaultMixin())
	mustWallet(t, p, "place_limit_order")
	if p.Params["side"].(uint8) != 1 {
		t.Fatal("side")
	}
	if p.Params["mixin"].(uint64) != DynamaxMixin {
		t.Fatal("mixin")
	}
	can := BuildCancelLimitOrder("abc123", DefaultMixin())
	mustWallet(t, can, "cancel_limit_order")
	if can.Params["order_id"].(string) != "abc123" {
		t.Fatal("order_id")
	}
}

func TestBuildHearthDaemon(t *testing.T) {
	pool := BuildAmmPoolInfo()
	if pool.Kind != RpcDaemonGET || pool.Path != "/amm_pool_info" {
		t.Fatalf("pool=%+v", pool)
	}
	ob := BuildOrderbookState(20)
	if ob.Kind != RpcDaemonJSONRPC || ob.Method != "get_orderbook_state" {
		t.Fatalf("orderbook=%+v", ob)
	}
	if ob.Params["depth"].(int) != 20 {
		t.Fatal("depth")
	}
	hm := BuildHeatMetrics()
	if hm.Path != "/heat_metrics" {
		t.Fatal("heat_metrics path")
	}
}

func TestBuildSendTransaction_DynamaxAnonymity(t *testing.T) {
	c := BuildSendTransaction("SRC", "DST", 1_000_000, DefaultMixin())
	mustWallet(t, c, "sendTransaction")
	if c.Params["anonymity"].(uint64) != DynamaxMixin {
		t.Fatalf("anonymity=%v want dynamax 0 (not fixed 4)", c.Params["anonymity"])
	}
	// fixed wrong ring size regression guard
	if anon, ok := c.Params["anonymity"].(uint64); ok && anon == 4 {
		t.Fatal("must not hardcode anonymity=4")
	}
}

func TestBuildSendHeat(t *testing.T) {
	c := BuildSendHeat("addr", 100, DefaultMixin())
	mustWallet(t, c, "send_heat")
	if c.Params["mixin"].(uint64) != DynamaxMixin {
		t.Fatal("mixin")
	}
}

func TestBuildSubaddresses(t *testing.T) {
	cr := BuildCreateAddress()
	mustWallet(t, cr, "createAddress")
	ls := BuildGetAddresses()
	mustWallet(t, ls, "getAddresses")
}

func TestBuildAliases(t *testing.T) {
	look := BuildGetAlias("fuego001")
	if look.Kind != RpcDaemonPOSTJSON || look.Path != "/get_alias" {
		t.Fatalf("get_alias=%+v", look)
	}
	if look.Params["alias"].(string) != "fuego001" {
		t.Fatal("alias param")
	}
	by := BuildGetAliasByAddress("addr1")
	if by.Path != "/get_alias_by_address" {
		t.Fatal("path")
	}
	reg := BuildRegisterAlias("fuego001", "addr1", DefaultMixin())
	mustWallet(t, reg, "register_alias")
	if reg.Params["mixin"].(uint64) != DynamaxMixin {
		t.Fatal("mixin")
	}
}

func TestWalletBinaryCandidates_IncludesConfigured(t *testing.T) {
	c := WalletBinaryCandidates("fire_wallet")
	if len(c) < 2 {
		t.Fatalf("candidates=%v", c)
	}
	// PaymentGate-style first
	if c[0] != "walletd" {
		t.Fatalf("first=%s want walletd", c[0])
	}
	foundConfigured := false
	for _, n := range c {
		if n == "fire_wallet" {
			foundConfigured = true
		}
		if n == "xfg-swapd" {
			t.Fatal("swapd should not be wallet candidate")
		}
	}
	if !foundConfigured {
		t.Fatal("configured fire_wallet missing from candidates")
	}
}

func TestDefaultMixin_IsDynamax(t *testing.T) {
	if DefaultMixin() != 0 {
		t.Fatal("DefaultMixin must be 0 for dynamax")
	}
}

// Structural: menu exposes every in-scope feature (atomic swaps excluded).
func TestMenuCoversInScopeFeatures(t *testing.T) {
	needles := []string{
		"Mint HEAT",
		"HEAT CD Deposit",
		"View Deposit",
		"Withdraw Deposit",
		"Hearth Pool",
		"Hearth Orderbook",
		"Swap XFG/HEAT",
		"Place Limit Order",
		"Cancel Limit Order",
		"List Subaddresses",
		"New Subaddress",
		"Lookup Alias",
		"Register Alias",
		"Send Transaction",
	}
	labels := make([]string, len(menu))
	for i, m := range menu {
		labels[i] = string(m)
	}
	joined := strings.Join(labels, " | ")
	for _, n := range needles {
		found := false
		for _, l := range labels {
			if l == n {
				found = true
				break
			}
		}
		if !found {
			t.Errorf("menu missing %q; have: %s", n, joined)
		}
	}
	// Atomic swaps not required as a managed feature
	for _, l := range labels {
		if l == "Active Swaps" {
			t.Log("Active Swaps still listed but is no-op / out of scope")
		}
	}
}
