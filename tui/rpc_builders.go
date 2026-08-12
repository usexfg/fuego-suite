package main

// RPC builders map UI intents to PaymentGate / fire_wallet / daemon calls.
//
// Dynamax (dynamic max mixin): protocol allows uniform ring sizes {32, 16, 8}
// (see DynamicRingSizeCalculator / MIN_TX_MIXIN_SIZE_V10=8, MAX_TX_MIXIN_SIZE=32).
// Clients probe with maxMixin (32); the wallet/daemon then pick the largest
// approved size achievable with the current decoy pool. This is NOT mixin=0.

const (
	// MinTxMixin is the mainnet dynamax floor (V10+).
	MinTxMixin uint64 = 8
	// MaxTxMixin is the dynamax ceiling; default outbound probe value.
	MaxTxMixin uint64 = 32
	// DynamaxMixin is the default client-supplied mixin: request the maximum so
	// DynamicRingSizeCalculator can settle on 32, 16, or 8 from available outs.
	DynamaxMixin uint64 = MaxTxMixin
)

// RpcKind classifies where a call is sent.
type RpcKind int

const (
	RpcWallet RpcKind = iota
	RpcDaemonGET
	RpcDaemonPOSTJSON // POST body to daemon path (JSON object)
	RpcDaemonJSONRPC  // POST JSON-RPC to daemon /json_rpc
)

// RpcCall is a pure description of a remote call (method + params + target).
type RpcCall struct {
	Kind   RpcKind
	Method string                 // wallet / json_rpc method name (empty for plain HTTP paths)
	Path   string                 // daemon path e.g. /heat_metrics, /get_alias
	Params map[string]interface{} // wallet params or JSON body
}

// DefaultMixin returns MaxTxMixin (32) so wallets probe at dynamax ceiling.
func DefaultMixin() uint64 { return DynamaxMixin }

// ── HEAT mint (current path: heat_mint / mintHeatV10) ────────────────────────

// BuildHeatMint builds wallet heat_mint params (not createBurnDeposit).
// xfgBurned and heatMinted are atomic units; mixin should be MaxTxMixin (32).
func BuildHeatMint(xfgBurned, heatMinted, mixin uint64) RpcCall {
	if mixin == 0 {
		mixin = DynamaxMixin
	}
	return RpcCall{
		Kind:   RpcWallet,
		Method: "heat_mint",
		Params: map[string]interface{}{
			"xfg_burned":  xfgBurned,
			"heat_minted": heatMinted,
			"mixin":       mixin,
		},
	}
}

// EstimateHeatMinted estimates HEAT from pool reserves (same formula as SimpleWallet mint_heat).
// Falls back to launch 10:1 (XFG per HEAT) when pool empty.
func EstimateHeatMinted(xfgBurned, reserveXfg, reserveHeat uint64) uint64 {
	if xfgBurned == 0 {
		return 0
	}
	if reserveXfg > 0 && reserveHeat > 0 {
		return xfgBurned * reserveHeat / reserveXfg
	}
	// Launch ratio: 1 HEAT = 10 XFG → heat = xfg / 10
	return xfgBurned / 10
}

// ── HEAT CD (deposit HEAT after mint — heat_deposit, not createDeposit mint) ─

// BuildHeatDeposit locks HEAT for term_epochs (current CD path).
func BuildHeatDeposit(amountAtomic uint64, termEpochs uint32, mixin uint64) RpcCall {
	if mixin == 0 {
		mixin = DynamaxMixin
	}
	return RpcCall{
		Kind:   RpcWallet,
		Method: "heat_deposit",
		Params: map[string]interface{}{
			"amount":       amountAtomic,
			"term_epochs":  termEpochs,
			"banking_fee":  uint64(0),
			"fee":          uint64(0),
			"mixin":        mixin,
		},
	}
}

// BuildGetDeposit fetches a deposit by id.
func BuildGetDeposit(depositID uint64) RpcCall {
	return RpcCall{
		Kind:   RpcWallet,
		Method: "getDeposit",
		Params: map[string]interface{}{"depositId": depositID},
	}
}

// BuildWithdrawDeposit withdraws a matured deposit.
func BuildWithdrawDeposit(depositID uint64) RpcCall {
	return RpcCall{
		Kind:   RpcWallet,
		Method: "withdrawDeposit",
		Params: map[string]interface{}{"depositId": depositID},
	}
}

// ── Hearth AMM + limit orders ────────────────────────────────────────────────

// BuildAmmSwap builds amm_swap (direction 0=XFG→HEAT, 1=HEAT→XFG).
func BuildAmmSwap(direction uint8, inputAmount, minOutput, mixin uint64) RpcCall {
	if mixin == 0 {
		mixin = DynamaxMixin
	}
	return RpcCall{
		Kind:   RpcWallet,
		Method: "amm_swap",
		Params: map[string]interface{}{
			"direction":       direction,
			"input_amount":    inputAmount,
			"expected_output": uint64(0),
			"min_output":      minOutput,
			"fee":             uint64(0),
			"mixin":           mixin,
		},
	}
}

// BuildPlaceLimitOrder builds place_limit_order (side 0=BUY_XFG, 1=SELL_XFG).
func BuildPlaceLimitOrder(side uint8, amount, targetPrice uint64, expiration uint32, mixin uint64) RpcCall {
	if mixin == 0 {
		mixin = DynamaxMixin
	}
	return RpcCall{
		Kind:   RpcWallet,
		Method: "place_limit_order",
		Params: map[string]interface{}{
			"side":         side,
			"amount":       amount,
			"target_price": targetPrice,
			"expiration":   expiration,
			"fee":          uint64(0),
			"mixin":        mixin,
		},
	}
}

// BuildCancelLimitOrder cancels an open limit order by id/hash string.
func BuildCancelLimitOrder(orderID string, mixin uint64) RpcCall {
	if mixin == 0 {
		mixin = DynamaxMixin
	}
	return RpcCall{
		Kind:   RpcWallet,
		Method: "cancel_limit_order",
		Params: map[string]interface{}{
			"order_id": orderID,
			"fee":      uint64(0),
			"mixin":    mixin,
		},
	}
}

// BuildAmmPoolInfo is a daemon GET for Hearth pool reserves.
func BuildAmmPoolInfo() RpcCall {
	return RpcCall{Kind: RpcDaemonGET, Path: "/amm_pool_info"}
}

// BuildOrderbookState requests the Hearth order book via daemon HTTP
// POST /getorderbook (RpcServer::on_get_order_book). Not JSON-RPC.
func BuildOrderbookState(depth int) RpcCall {
	if depth <= 0 {
		depth = 20
	}
	return RpcCall{
		Kind: RpcDaemonPOSTJSON,
		Path: "/getorderbook",
		Params: map[string]interface{}{
			"pair":  uint8(0),
			"depth": depth,
		},
	}
}

// BuildGetLimitOrders lists limit orders via daemon HTTP POST /get_limit_orders
// (or wallet get_limit_orders when using fire_wallet). Prefer daemon for book-wide view.
func BuildGetLimitOrders() RpcCall {
	return RpcCall{
		Kind:   RpcDaemonPOSTJSON,
		Path:   "/get_limit_orders",
		Params: map[string]interface{}{},
	}
}

// BuildWalletGetLimitOrders lists this wallet's open limit orders (PaymentGate/fire_wallet).
func BuildWalletGetLimitOrders() RpcCall {
	return RpcCall{
		Kind:   RpcWallet,
		Method: "get_limit_orders",
		Params: map[string]interface{}{},
	}
}

// BuildHeatMetrics is a daemon GET for HEAT supply metrics.
func BuildHeatMetrics() RpcCall {
	return RpcCall{Kind: RpcDaemonGET, Path: "/heat_metrics"}
}

// ── Transfer / send (dynamax mixin probe = max 32) ───────────────────────────

// BuildSendTransaction builds PaymentGate-style sendTransaction.
// anonymity/mixin is the probe ceiling (default 32); wallet dynamax settles 8/16/32.
func BuildSendTransaction(sourceAddr, destAddr string, amountAtomic uint64, mixin uint64) RpcCall {
	if mixin == 0 {
		mixin = DynamaxMixin
	}
	return RpcCall{
		Kind:   RpcWallet,
		Method: "sendTransaction",
		Params: map[string]interface{}{
			"sourceAddresses": []string{sourceAddr},
			"transfers": []map[string]interface{}{
				{"address": destAddr, "amount": amountAtomic},
			},
			"changeAddress": sourceAddr,
			"anonymity":     mixin,
		},
	}
}

// BuildSendHeat builds send_heat (PaymentGate / fire_wallet).
func BuildSendHeat(address string, amountAtomic, mixin uint64) RpcCall {
	if mixin == 0 {
		mixin = DynamaxMixin
	}
	return RpcCall{
		Kind:   RpcWallet,
		Method: "send_heat",
		Params: map[string]interface{}{
			"address": address,
			"amount":  amountAtomic,
			"mixin":   mixin,
		},
	}
}

// ── Subaddresses ─────────────────────────────────────────────────────────────

// BuildCreateAddress creates a new subaddress (empty params = new spend key).
func BuildCreateAddress() RpcCall {
	return RpcCall{
		Kind:   RpcWallet,
		Method: "createAddress",
		Params: map[string]interface{}{},
	}
}

// BuildGetAddresses lists wallet addresses / subaddresses.
func BuildGetAddresses() RpcCall {
	return RpcCall{
		Kind:   RpcWallet,
		Method: "getAddresses",
		Params: map[string]interface{}{},
	}
}

// ── Aliases ──────────────────────────────────────────────────────────────────

// BuildGetAlias looks up alias → address via daemon.
func BuildGetAlias(alias string) RpcCall {
	return RpcCall{
		Kind:   RpcDaemonPOSTJSON,
		Path:   "/get_alias",
		Params: map[string]interface{}{"alias": alias},
	}
}

// BuildGetAliasByAddress looks up address → alias via daemon.
func BuildGetAliasByAddress(address string) RpcCall {
	return RpcCall{
		Kind:   RpcDaemonPOSTJSON,
		Path:   "/get_alias_by_address",
		Params: map[string]interface{}{"address": address},
	}
}

// BuildRegisterAlias registers an @alias via wallet RPC (WalletGreen path when exposed).
// Method name matches SimpleWallet register_alias command surface.
func BuildRegisterAlias(alias, ownerAddress string, mixin uint64) RpcCall {
	if mixin == 0 {
		mixin = DynamaxMixin
	}
	return RpcCall{
		Kind:   RpcWallet,
		Method: "register_alias",
		Params: map[string]interface{}{
			"alias":   alias,
			"address": ownerAddress,
			"mixin":   mixin,
		},
	}
}

// WalletBinaryCandidates returns binaries to try for PaymentGate-style JSON-RPC
// (createAddress, heat_mint, sendTransaction, …). Prefers walletd/unified
// (PaymentGateService) over fire_wallet CLI which may not bind JSON-RPC the same way.
func WalletBinaryCandidates(configured string) []string {
	out := make([]string, 0, 5)
	seen := map[string]bool{}
	add := func(s string) {
		if s == "" || seen[s] {
			return
		}
		seen[s] = true
		out = append(out, s)
	}
	// PaymentGate RPC first for container + --bind-port flows used by TUI
	add("walletd")
	add("unified")
	add(configured)
	add("fire_wallet")
	return out
}
