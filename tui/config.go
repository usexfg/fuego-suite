package main

type Config struct {
	NetworkName     string
	CoinName        string
	AddressPrefix   string
	NodeBinary      string
	WalletBinary    string
	NodeRPCPort     int
	WalletRPCPort   int
	NodeP2PPort     int
	DataDir         string
	CoinUnits       int64
	StakeAmount     int64
	BurnSmallAmount int64
	BurnLargeAmount int64
	TestTxAmount    int64
	// RPC method names (used as JSON-RPC method field in wallet RPC)
	CreateStakeRPC        string
	GetStakeStatusRPC     string
	CreateBurnRPC         string
	RequestConsensusRPC   string
	GetConsensusRPC       string
	GetPendingVotesRPC    string
	RegisterEnindexRPC    string
	IncreaseStakeRPC     string
	UpdateEnindexRPC     string
	GetAddressesRPC      string
	GetBalanceRPC        string
	SendTransactionRPC   string
	CreateAddressRPC     string
}

var MainnetConfig = Config{
	NetworkName:     "Mainnet",
	CoinName:        "XFG",
	AddressPrefix:   "fire",
	NodeBinary:      "fuegod",
	WalletBinary:    "walletd",
	NodeRPCPort:     18180,
	WalletRPCPort:   18183,
	NodeP2PPort:     10808,
	DataDir:         ".fuego",
	CoinUnits:       10000000,
	StakeAmount:     8000000000,
	BurnSmallAmount: 8000000,
	BurnLargeAmount: 8000000000,
	TestTxAmount:    10000000,
	CreateStakeRPC:        "create_stake_deposit",
	GetStakeStatusRPC:     "get_stake_status",
	CreateBurnRPC:         "create_burn_deposit",
	RequestConsensusRPC:   "request_elderfier_consensus",
	GetConsensusRPC:       "get_consensus_requests",
	GetPendingVotesRPC:    "get_pending_votes",
	RegisterEnindexRPC:    "register_to_enindex",
	IncreaseStakeRPC:     "increase_stake",
	UpdateEnindexRPC:     "update_enindex",
	GetAddressesRPC:      "get_addresses",
	GetBalanceRPC:        "get_balance",
	SendTransactionRPC:   "send_transaction",
	CreateAddressRPC:     "create_address",
}

var TestnetConfig = Config{
	NetworkName:     "TESTNET",
	CoinName:        "TEST",
	AddressPrefix:   "1075740",
	NodeBinary:      "fuegod",
	WalletBinary:    "walletd",
	NodeRPCPort:     28280,
	WalletRPCPort:   28283,
	NodeP2PPort:     20808,
	DataDir:         ".fuego-testnet",
	CoinUnits:       10000000,
	StakeAmount:     800000000,
	BurnSmallAmount: 8000000,
	BurnLargeAmount: 8000000000,
	TestTxAmount:    10000000,
	CreateStakeRPC:        "create_stake_deposit",
	GetStakeStatusRPC:     "get_stake_status",
	CreateBurnRPC:         "create_burn_deposit",
	RequestConsensusRPC:   "request_elderfier_consensus",
	GetConsensusRPC:       "get_consensus_requests",
	GetPendingVotesRPC:    "get_pending_votes",
	RegisterEnindexRPC:    "register_to_enindex",
	IncreaseStakeRPC:     "increase_stake",
	UpdateEnindexRPC:     "update_enindex",
	GetAddressesRPC:      "get_addresses",
	GetBalanceRPC:        "get_balance",
	SendTransactionRPC:   "send_transaction",
	CreateAddressRPC:     "create_address",
}

var CurrentConfig Config
