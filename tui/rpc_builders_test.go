package main

import (
	"os"
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
	if c.Params["mixin"].(uint64) != MaxTxMixin {
		t.Fatalf("mixin=%v want dynamax max %d (not 0)", c.Params["mixin"], MaxTxMixin)
	}
	if c.Params["mixin"].(uint64) < MinTxMixin {
		t.Fatal("dynamax floor is min mixin 8")
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
	if ob.Kind != RpcDaemonPOSTJSON || ob.Path != "/getorderbook" {
		t.Fatalf("orderbook must use daemon POST /getorderbook, got %+v", ob)
	}
	if ob.Method != "" {
		t.Fatalf("orderbook must not use JSON-RPC method name, got %q", ob.Method)
	}
	if ob.Params["depth"].(int) != 20 {
		t.Fatal("depth")
	}
	// Guard: never use the broken get_orderbook_state JSON-RPC path
	if ob.Path == "/json_rpc" || ob.Method == "get_orderbook_state" {
		t.Fatal("broken orderbook transport")
	}
	list := BuildGetLimitOrders()
	if list.Path != "/get_limit_orders" || list.Kind != RpcDaemonPOSTJSON {
		t.Fatalf("list orders=%+v", list)
	}
	wlist := BuildWalletGetLimitOrders()
	mustWallet(t, wlist, "get_limit_orders")
	hm := BuildHeatMetrics()
	if hm.Path != "/heat_metrics" {
		t.Fatal("heat_metrics path")
	}
}

func TestBuildSendTransaction_DynamaxAnonymity(t *testing.T) {
	c := BuildSendTransaction("SRC", "DST", 1_000_000, DefaultMixin())
	mustWallet(t, c, "sendTransaction")
	if c.Params["anonymity"].(uint64) != MaxTxMixin {
		t.Fatalf("anonymity=%v want max mixin %d (dynamax probe)", c.Params["anonymity"], MaxTxMixin)
	}
	// fixed wrong ring size regression guards
	if anon, ok := c.Params["anonymity"].(uint64); ok && anon == 4 {
		t.Fatal("must not hardcode anonymity=4")
	}
	if anon, ok := c.Params["anonymity"].(uint64); ok && anon == 0 {
		t.Fatal("mixin 0 is not dynamax — pass max 32 so wallet can settle 8/16/32")
	}
	// zero input coerced to dynamax max
	c0 := BuildSendTransaction("SRC", "DST", 1_000_000, 0)
	if c0.Params["anonymity"].(uint64) != MaxTxMixin {
		t.Fatalf("mixin 0 must coerce to MaxTxMixin, got %v", c0.Params["anonymity"])
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
	if DefaultMixin() != MaxTxMixin {
		t.Fatalf("DefaultMixin=%d want MaxTxMixin=%d (probe ceiling for dynamax 8/16/32)", DefaultMixin(), MaxTxMixin)
	}
	if MinTxMixin != 8 || MaxTxMixin != 32 {
		t.Fatalf("dynamax range is 8..32, got min=%d max=%d", MinTxMixin, MaxTxMixin)
	}
	if DynamaxMixin == 0 {
		t.Fatal("DynamaxMixin must not be 0")
	}
}

// Structural: PaymentGate registers the TUI method names (source of truth for unified wallet).
func TestPaymentGateRegistersTuiMethods(t *testing.T) {
	path := "../src/PaymentGate/PaymentServiceJsonRpcServer.cpp"
	b, err := os.ReadFile(path)
	if err != nil {
		t.Skip("PaymentGate source not found:", err)
	}
	src := string(b)
	need := []string{
		`"heat_mint"`,
		`"heat_deposit"`,
		`"amm_swap"`,
		`"place_limit_order"`,
		`"cancel_limit_order"`,
		`"get_limit_orders"`,
		`"register_alias"`,
		`"createAddress"`,
		`"getAddresses"`,
		`"sendTransaction"`,
		`"getDeposit"`,
		`"withdrawDeposit"`,
	}
	for _, n := range need {
		if !strings.Contains(src, n) {
			t.Errorf("PaymentGate missing handler registration for %s", n)
		}
	}
	// Daemon orderbook path (not JSON-RPC get_orderbook_state)
	rpcPath := "../src/Rpc/RpcServer.cpp"
	rb, err := os.ReadFile(rpcPath)
	if err != nil {
		t.Skip("RpcServer source not found:", err)
	}
	rs := string(rb)
	if !strings.Contains(rs, `"/getorderbook"`) {
		t.Error("daemon missing /getorderbook")
	}
	if !strings.Contains(rs, `"/get_limit_orders"`) {
		t.Error("daemon missing /get_limit_orders")
	}
}

// Structural: PaymentGate heatDeposit must call heatDepositV10, never createDeposit.
func TestPaymentGateHeatDepositIsNotCreateDeposit(t *testing.T) {
	path := "../src/PaymentGate/WalletService.cpp"
	b, err := os.ReadFile(path)
	if err != nil {
		t.Skip("WalletService source not found:", err)
	}
	src := string(b)
	// Isolate the heatDeposit function body between its signature and the next method
	start := strings.Index(src, "WalletService::heatDeposit(")
	if start < 0 {
		t.Fatal("WalletService::heatDeposit not found")
	}
	// next method after heatDeposit
	rest := src[start:]
	// find end: next "std::error_code WalletService::" after start of body
	bodyStart := strings.Index(rest, "{")
	if bodyStart < 0 {
		t.Fatal("heatDeposit body start not found")
	}
	// crude: take until next WalletService:: after body start
	after := rest[bodyStart:]
	nextFn := strings.Index(after[1:], "WalletService::")
	var body string
	if nextFn > 0 {
		body = after[:nextFn+1]
	} else {
		body = after
	}
	if strings.Contains(body, "createDeposit(") {
		t.Fatal("heatDeposit must not call createDeposit (XFG path / DEPOSIT_MIN_TERM)")
	}
	if !strings.Contains(body, "heatDepositV10") {
		t.Fatal("heatDeposit must call WalletGreen::heatDepositV10")
	}
}

// Structural: WalletGreen heatDepositV10 exists and selects HEAT_TERM deposits.
func TestWalletGreenHeatDepositSpendsHeatTerm(t *testing.T) {
	path := "../src/Wallet/WalletGreen.cpp"
	b, err := os.ReadFile(path)
	if err != nil {
		t.Skip("WalletGreen source not found:", err)
	}
	src := string(b)
	start := strings.Index(src, "WalletGreen::heatDepositV10(")
	if start < 0 {
		t.Fatal("WalletGreen::heatDepositV10 not found")
	}
	rest := src[start:]
	end := strings.Index(rest, "WalletGreen::ammSwapV10(")
	if end < 0 {
		end = len(rest)
	}
	window := rest[:end]
	if !strings.Contains(window, "HEAT_TERM") {
		t.Fatal("heatDepositV10 must select HEAT_TERM deposits")
	}
	if !strings.Contains(window, "termEpochs") {
		t.Fatal("heatDepositV10 must take epoch terms")
	}
	if !strings.Contains(window, "signInputCommitmentSpend") {
		t.Fatal("heatDepositV10 must spend HEAT via commitment inputs")
	}
	// must not be a thin createDeposit wrapper
	if strings.Contains(window, "createDeposit(") {
		t.Fatal("heatDepositV10 must not call createDeposit")
	}
}

// Structural: ammSwapV10 direction 1 spends HEAT commitment deposits.
func TestWalletGreenAmmSwapDir1SpendsHeat(t *testing.T) {
	path := "../src/Wallet/WalletGreen.cpp"
	b, err := os.ReadFile(path)
	if err != nil {
		t.Skip("WalletGreen source not found:", err)
	}
	src := string(b)
	start := strings.Index(src, "WalletGreen::ammSwapV10(")
	if start < 0 {
		t.Fatal("ammSwapV10 not found")
	}
	// take function window until sendHeatV10
	rest := src[start:]
	end := strings.Index(rest, "WalletGreen::sendHeatV10(")
	if end < 0 {
		end = len(rest)
		if end > 12000 {
			end = 12000
		}
	}
	body := rest[:end]
	if !strings.Contains(body, "direction == 1") && !strings.Contains(body, "direction ==1") {
		// HEAT→XFG branch present after direction==0 early return
		if !strings.Contains(body, "HEAT→XFG") && !strings.Contains(body, "Insufficient unlocked HEAT for AMM swap") {
			t.Fatal("ammSwapV10 missing HEAT→XFG branch")
		}
	}
	if !strings.Contains(body, "signInputCommitmentSpend") {
		t.Fatal("ammSwapV10 HEAT→XFG must use commitment spends")
	}
	if !strings.Contains(body, "DEPOSIT_TERM_POOL_HEAT") {
		t.Fatal("ammSwapV10 HEAT→XFG must deposit HEAT to pool")
	}
	if !strings.Contains(body, "addAmmSwapAuthToExtra") {
		t.Fatal("ammSwapV10 must attach AMM auth extra")
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
		"List Limit Orders",
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
