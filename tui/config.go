package main

type Config struct {
	NetworkName   string
	CoinName      string
	AddressPrefix string
	NodeBinary    string
	WalletBinary  string
	IsTestnet     bool
	NodeRPCPort   int
	NodeP2PPort   int
	DataDir       string
	CoinUnits     int64
	StakeAmount   int64
	BurnTiers     []int64 // 0.8, 8, 80, 800 in atomic
	TestTxAmount  int64
}

var MainnetConfig = Config{
	NetworkName:   "Mainnet",
	CoinName:      "XFG",
	AddressPrefix: "1753191",
	NodeBinary:    "fuegod",
	WalletBinary:  "fire_wallet",
	IsTestnet:     false,
	NodeRPCPort:   18180,
	NodeP2PPort:   10808,
	DataDir:       ".fuego",
	CoinUnits:     10000000,
	StakeAmount:   8000000000,
	BurnTiers:     []int64{8000000, 80000000, 800000000, 8000000000}, // 0.8, 8, 80, 800 XFG
	TestTxAmount:  10000000,
}

var TestnetConfig = Config{
	NetworkName:   "Fuego Testnet",
	CoinName:      "TEST",
	AddressPrefix: "1075740",
	NodeBinary:    "testnetd",
	WalletBinary:  "test_wallet",
	IsTestnet:     true,
	NodeRPCPort:   28280,
	NodeP2PPort:   20808,
	DataDir:       ".fuego-testnet",
	CoinUnits:     10000000,
	StakeAmount:   40000000000,
	BurnTiers:     []int64{8000000, 80000000, 800000000, 8000000000}, // 0.8, 8, 80, 800 TEST
	TestTxAmount:  1000,
}

var CurrentConfig Config
