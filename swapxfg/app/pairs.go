package app

// Pair IDs match fuegod's SwapPair enum (SwapTypes.h).
const (
	PairSOL uint8 = 0
	PairETH uint8 = 1
	PairXMR uint8 = 2
	PairBCH    uint8 = 3
	PairARB    uint8 = 4
	PairBASE   uint8 = 5
	PairKMD    uint8 = 6 // KMD_SPV (daemon-side only)
	PairBNB    uint8 = 7
	PairCD     uint8 = 99 // not a swap pair — CD/XFG secondary market mode
	PairDaemon uint8 = 100 // daemon status monitoring view
)

// ActivePairs lists all supported pairs in display order (SOL first).
var ActivePairs = []uint8{PairSOL, PairETH, PairXMR, PairBCH, PairARB, PairBASE, PairBNB}

// PairName returns the display name for a pair (e.g. "ETH/XFG").
func PairName(pair uint8) string {
	switch pair {
	case PairSOL:
		return "SOL/XFG"
	case PairETH:
		return "ETH/XFG"
	case PairXMR:
		return "XMR/XFG"
	case PairBCH:
		return "BCH/XFG"
	case PairARB:
		return "ARB/XFG"
	case PairBASE:
		return "BASE/XFG"
	case PairBNB:
		return "BNB/XFG"
	case PairCD:
		return "CD"
	default:
		return "???"
	}
}

func PairLabelLong(pair uint8) string {
	switch pair {
	case PairSOL:
		return "Solana"
	case PairETH:
		return "Ethereum"
	case PairXMR:
		return "Monero"
	case PairBCH:
		return "Bitcoin Cash"
	case PairARB:
		return "Arbitrum L2"
	case PairBASE:
		return "Base L2"
	case PairBNB:
		return "BNB Chain"
	case PairCD:
		return "Certificates Of Deposit"
	case PairDaemon:
		return "Daemon Status"
	default:
		return "Unknown"
	}
}

// PairShort returns the short counterparty symbol (e.g. "ETH").
func PairShort(pair uint8) string {
	switch pair {
	case PairSOL:
		return "SOL"
	case PairETH:
		return "ETH"
	case PairXMR:
		return "XMR"
	case PairBCH:
		return "BCH"
	case PairARB:
		return "ARB"
	case PairBASE:
		return "BASE"
	case PairBNB:
		return "BNB"
	default:
		return "?"
	}
}

// PairFromString returns the pair ID for a string name, or 255 if unknown.
func PairFromString(s string) uint8 {
	switch s {
	case "sol", "SOL":
		return PairSOL
	case "eth", "ETH":
		return PairETH
	case "xmr", "XMR":
		return PairXMR
	case "bch", "BCH":
		return PairBCH
	case "arb", "ARB":
		return PairARB
	case "base", "BASE":
		return PairBASE
	case "bnb", "BNB":
		return PairBNB
	case "cd", "CD":
		return PairCD
	default:
		return 255
	}
}

// TradingViewSymbol returns a TradingView symbol URL fragment for the pair.
// Returns empty string if no TradingView symbol exists for the pair.
func TradingViewSymbol(pair uint8) string {
	switch pair {
	case PairSOL:
		return "BINANCE:XFGLSOL"
	case PairETH:
		return "BINANCE:XFGETH"
	case PairXMR:
		return "BINANCE:FGXMR"
	case PairBCH:
		return "BINANCE:FGXBCH"
	case PairARB:
		return "BINANCE:FGXARB"
	case PairBASE:
		return "BINANCE:FGXBASE"
	case PairBNB:
		return "BINANCE:FGXBNB"
	default:
		return ""
	}
}

// HotkeyPair maps hotkey rune to pair ID. Returns 255 if not a pair hotkey.
func HotkeyPair(r rune) uint8 {
	switch r {
	case '0':
		return PairSOL
	case '1':
		return PairETH
	case '2':
		return PairXMR
	case '3':
		return PairBCH
	case '4':
		return PairARB
	case '5':
		return PairBASE
	case '6':
		return PairBNB
	case 'c':
		return PairCD
	default:
		return 255
	}
}
