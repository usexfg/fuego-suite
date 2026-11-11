package app

type Config struct {
	DaemonRPC  string // fuegod RPC endpoint
	WalletRPC  string // fire_wallet RPC endpoint (empty = no wallet)
	WalletUser string // --rpc-user for fire_wallet (optional)
	WalletPass string // --rpc-password for fire_wallet (optional)
	Testnet    bool
	StartPair  uint8 // initial pair to display
	NoSplash   bool
	Compact    bool
	BridgePort int    // 0 = random; bridge serves MetaMask/Phantom pages
	NoBridge   bool   // disable bridge server entirely
	BchRPC       string // Electron Cash RPC endpoint (empty = no BCH)
	NoBch        bool   // disable BCH connection
	Headless     bool   // run in headless mode (no TUI)
	HeadlessPort int    // HTTP control API port (default: 18190)
	// ControlToken authorizes the headless control API. Required whenever
	// headless mode is enabled: /offer and /cancel make walletd sign with the
	// operator's key, so an unauthenticated listener would let any local
	// process — or, via DNS rebinding, any web page the operator visits —
	// place signed offers.
	ControlToken   string // --control-token or $XFG_CONTROL_TOKEN
	NoControlToken bool   // refuse to start headless without a token
	StatusPort     int    // xfg-swapd status port (default: 18900)
}

func DefaultConfig() Config {
	return Config{
		DaemonRPC: "http://127.0.0.1:18180",
		WalletRPC: "",
		StartPair: PairSOL,
		BchRPC:       "http://127.0.0.1:7773",
		HeadlessPort: 18190,
		StatusPort:   18900,
	}
}
