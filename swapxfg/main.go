package main

import (
	"fmt"
	"os"
	"strings"

	"github.com/usexfg/swapxfg/app"
)

func main() {
	cfg := app.DefaultConfig()

	for i := 1; i < len(os.Args); i++ {
		arg := os.Args[i]
		next := func() string {
			if i+1 < len(os.Args) {
				i++
				return os.Args[i]
			}
			fmt.Fprintf(os.Stderr, "missing value for %s\n", arg)
			os.Exit(1)
			return ""
		}
		switch arg {
		case "--daemon", "-d", "--efier", "-e":
			cfg.DaemonRPC = next()
		case "--wallet", "-w":
			cfg.WalletRPC = next()
		case "--testnet":
			cfg.DaemonRPC = "http://127.0.0.1:28280"
			cfg.WalletRPC = "http://127.0.0.1:28282"
			cfg.Testnet = true
		case "--pair", "-p":
			p := app.PairFromString(strings.ToLower(next()))
			if p == 255 {
				fmt.Fprintf(os.Stderr, "unknown pair (use: sol, eth, xmr, bch)\n")
				os.Exit(1)
			}
			cfg.StartPair = p
		case "--no-splash":
			cfg.NoSplash = true
		case "--compact":
			cfg.Compact = true
		case "--bridge-port":
			var p int
			fmt.Sscanf(next(), "%d", &p)
			cfg.BridgePort = p
		case "--no-bridge":
			cfg.NoBridge = true
		case "--bch-rpc":
			cfg.BchRPC = next()
		case "--no-bch":
			cfg.NoBch = true
		case "--help", "-h":
			fmt.Println("swapxfg — Fuego cross-chain adaptor swap terminal")
			fmt.Println()
			fmt.Println("Usage: swapxfg [flags]")
			fmt.Println()
			fmt.Println("Connection:")
			fmt.Println("  --daemon, -d    Fuego daemon RPC (default: http://127.0.0.1:18180)")
			fmt.Println("                  Aliases: --efier, -e")
			fmt.Println("  --wallet, -w    Wallet RPC endpoint (optional, enables balance + swap signing)")
			fmt.Println("  --testnet       Use testnet defaults (:28280 daemon, :28282 wallet)")
			fmt.Println()
			fmt.Println("Display:")
			fmt.Println("  --pair, -p      Starting pair: sol, eth, xmr, bch (default: sol)")
			fmt.Println("  --no-splash     Skip splash screen")
			fmt.Println("  --compact       Compact layout for small terminals")
			fmt.Println("  --bridge-port   Port for MetaMask/Phantom bridge server (default: random)")
			fmt.Println("  --no-bridge     Disable the browser bridge server")
			fmt.Println("  --bch-rpc       Electron Cash RPC (default: http://127.0.0.1:7773)")
			fmt.Println("  --no-bch        Disable BCH / Electron Cash connection")
			fmt.Println()
			fmt.Println("  --help, -h      Show this help")
			os.Exit(0)
		default:
			fmt.Fprintf(os.Stderr, "unknown flag: %s (try --help)\n", arg)
			os.Exit(1)
		}
	}

	if err := app.Run(cfg); err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		os.Exit(1)
	}
}
