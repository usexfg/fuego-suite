<img title="The Long Night Is Coming" src="https://github.com/usexfg/fuego-data/blob/master/fuego-images/fuegoline.gif?raw=true"><img/>

### Fuego is open-source peer-to-peer decentralized private cryptocurrency built by advocates of freedom thru sound money and free open-source software.

Based upon the CryptoNote protocol & philosophy.

#### Resources

-   [Website](https://usexfg.org)
-   Explorer: <https://explore-xfg.loudmining.com>
-   Explorer: <http://fuego.spaceportx.net>
-   [Discord](https://discord.gg/5UJcJJg)
-   [Twitter](https://twitter.com/useXFG)
-   [Medium](https://medium.com/@usexfg)
-   [Bitcoin Talk](https://bitcointalk.org/index.php?topic=2712001)

### Features

| Category | Feature | Description |
|----------|---------|-------------|
| **Core** | CryptoNote | Ring-signature privacy at protocol level |
| **Core** | Dynamic Ring Size | 32, 16, or min 8 decoys per transaction, adaptive to pool depth |
| **Core** | Sub-addresses | Multiple addresses from single seed; integrated (v1) wallets |
| **Core** | Fire Aliases | 8 character only, on-chain '@' aliases for fire addresses; (@fuegoxfg for development) integrated with ecosystem wallets |
| **Assets** | HⲶ∆T colored-coin | Stable value | Algorithmic MoE (medium of exchange) — burn XFG to mint at PI redemption price |
| **Assets** | Hearth Exchange | Block-discrete on-chain exchange with adaptive-spread AMM pool band; batch-clears at VWAP |
| **Assets** | Certificates of Deposit | HEAT-denominated time-locked deposits earning yield from protocol fees |
| **Stability** | PI Controller | Negative-feedback redemption price targeting value-band equilibrium |
| **Stability** | Mælisandre Simulator | (`Testnet`**only**) ЅШ𑫛𑫒**❋**XFG fee injection to test protocol liquidity operations w/o needing actual cross-chain testnet activity |
| **Swaps** | Atomic Swaps | Cross-chain XFG swaps via COMIT protocol with HTLC adaptor signatures |
| **Swaps** | ЅШ𑫛𑫒**❋**XFG | Integrated TUI, Go-based swap engine with inter-chain fee routing |
| **Network** | I2P / Tor / Meshtastic* | Pluggable transport-layer privacy (*)research phase|
| **Wallet** | `fire_wallet`| Full-featured CLI: send, receive, deposits, HEAT mint/swap, pool operations |
| **Wallet** | TUI(suite) | Go-based terminal UI for atomic swaps, CD operations, HEAT mint |
| **GUI Wallet/SDK** | Fuego Wallet | Cross-platform GUI — [fuego-wallet](https://github.com/usexfg/fuego-wallet) Fuego SDK |
| **Dev Tools** | RPC API | JSON-RPC: heat_metrics, amm_quote, amm_pool_info, swap fees, CD queries |
| **Dev Tools** | MCP Server | AI-agent integration for swap/mint automation |
| **Dev Tools** | Fuego-Skills | AI-agent tools for codebase analytics, knowledge graph, and specialization |

_____________________________

<sup>"Working software is the primary measure of progress." [‣]</sup>

##### Build Status [‣]:http://agilemanifesto.org/

[![Build check](https://github.com/usexfg/fuego-suite/actions/workflows/check.yml/badge.svg)](https://github.com/usexfg/fuego-suite/actions/workflows/check.yml)
[![macOS](https://github.com/usexfg/fuego-suite/actions/workflows/macOS.yml/badge.svg)](https://github.com/usexfg/fuego-suite/actions/workflows/macOS.yml)
[![AppImage Linux](https://github.com/usexfg/fuego-suite/actions/workflows/appimage.yml/badge.svg)](https://github.com/usexfg/fuego-suite/actions/workflows/appimage.yml)
[![Ubuntu 24.04](https://github.com/usexfg/fuego-suite/actions/workflows/ubuntu24.yml/badge.svg)](https://github.com/usexfg/fuego-suite/actions/workflows/ubuntu24.yml)
[![Ubuntu 22.04](https://github.com/usexfg/fuego-suite/actions/workflows/ubuntu22.yml/badge.svg)](https://github.com/usexfg/fuego-suite/actions/workflows/ubuntu22.yml)
[![Windows](https://github.com/usexfg/fuego-suite/actions/workflows/windows.yml/badge.svg)](https://github.com/usexfg/fuego-suite/actions/workflows/windows.yml)
[![Docker Images](https://github.com/usexfg/fuego-suite/actions/workflows/docker.yml/badge.svg)](https://github.com/usexfg/fuego-suite/actions/workflows/docker.yml)
[![Android (Termux)](https://github.com/usexfg/fuego-suite/actions/workflows/termux.yml/badge.svg)](https://github.com/usexfg/fuego-suite/actions/workflows/termux.yml)
[![Raspberry Pi (ARM64)](https://github.com/usexfg/fuego-suite/actions/workflows/raspberry-pi.yml/badge.svg)](https://github.com/usexfg/fuego-suite/actions/workflows/raspberry-pi.yml)

### Build Requirements

**Boost Version**: Fuego requires Boost 1.86 or below (for io_service compatibility)

- **macOS**: Builds Boost 1.86 from source automatically
- **Linux**: Uses system packages (1.74+ on Ubuntu 22.04, 1.83+ on Ubuntu 24.04)
- **Windows**: Uses vcpkg packages (1.84+)

### Linux (Ubuntu/Debian)

#### 1. Install Dependencies

```bash
sudo apt install build-essential git cmake libboost-all-dev libjsoncpp-dev libssl-dev
```

#### 2. Clone and Build

```bash
git clone --recursive https://github.com/usexfg/fuego-suite
cd fuego-suite && git submodule init && git submodule update
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Binaries will be in `build/release/src/`. The Go TUI builds automatically if Go 1.24+ is installed.

#### 3. Start the Daemon

```bash
./build/release/src/fuegod
```

### macOS

#### 1. Install Dependencies

Install [Xcode](https://developer.apple.com/xcode/) and [Homebrew](https://brew.sh/), then:

```bash
xcode-select --install
brew install git cmake boost
```

#### 2. Build

```bash
git clone https://github.com/usexfg/fuego-suite
cd fuego-suite
mkdir build && cd build
cmake ..
make
```

Binaries will be in `build/src/`.

### Windows

#### 1. Prerequisites

- [Visual Studio 2019 Community Edition](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=Community&rel=16)
- [CMake](https://cmake.org/download/)
- [Boost 1.73.0 MSVC 14.2](https://sourceforge.net/projects/boost/files/boost-binaries/1.73.0/boost_1_73_0-msvc-14.2-64.exe/download)

When installing Visual Studio, include **Desktop development with C++** and **MSVC v142 - VS 2019 C++ x64/x86 build tools**.

#### 2. Build

From `x64 Native Tools Command Prompt for VS 2019`:

```bash
git clone https://github.com/usexfg/fuego-suite
cd fuego-suite
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64 -DBOOST_LIBRARYDIR="c:\local\boost_1_73_0\lib64-msvc-14.2"
msbuild fuegoX.sln /p:Configuration=Release /m
```

Binaries will be in `src/Release/`.

### Docker

```bash
docker build -t fuego .
docker run -p 11898:11898 -p 11899:11899 fuego
```

### Build Variants

| Command | Description |
|---------|-------------|
| `make` | Release build + TUI |
| `make build-release` | CMake release build |
| `make build-debug` | Debug build with symbols |
| `make build-static` | Static-linked build |
| `make build-tui` | Go TUI only |
| `make test-release` | Build and run tests |
| `make -j$(nproc)` | Parallel build (all cores) |

For Clang: `export CC=clang CXX=clang++` before `make`.

### Terminal User Interface (TUI)

Go-based TUI for swaps, wallet management, and CD operations. Requires Go 1.24+.

```bash
make build-tui
./tui/build/fuego-tui
```

Navigate with arrow keys or j/k, select with Enter, quit with q or Ctrl+C.

### HⲶ∆T Stablecoin & Hearth Exchange

HⲶ∆⟙ is Fuego's algorithmic stablecoin — a medium-of-exchange asset pegged to purchasing power. It is **not** pegged to a fiat currency; its target band adjusts for inflation over time.

| Operation | CLI Command | Description |
|-----------|------------|-------------|
| Mint HEAT | `mint_heat <xfg_amount>` | Burn XFG to create HEAT at PI redemption price |
| Swap | `swap <dir> <in> <out> <min>` | Swap XFG↔HⲶ∆T on Hearth exchange at bid/ask spread |
| MM Pool Info | `pool_info` | Show exchange depth, MM pool orders, spread |
| HEAT Metrics | `heat_info` | Show HEAT supply, redemption price, treasury, CD yield |
| HEAT Balance | `balance` | Now shows HEAT balance alongside XFG |

**Daemon RPC endpoints**: `/heat_metrics`, `/amm_quote`, `/amm_pool_info`, `/addswapfee`

**Key Properties**:
- Launch ratio 10:1 (1 HⲶ∆T = 10 XFG) at genesis
- Peg target $1.58 USD; mint at +3.33% premium (hardcoded, PI controller removed)
- Protocol buys or mints HⲶ∆T for CD yield payments (structural demand, protocol pays 0% fee)
- On-chain exchange replaces constant-product AMM; MM pool orders regenerate each block ±10% depth
- Pool reserves tracked on-chain with unspendable pool keys

### Certificates of Deposit (CD) — V12 activation

Epoch (900 blocks) term-locked HⲶ∆⟙ deposits earning real yield thru protocol revenue, not inflation. Each CD requires an upfront banking fee of a mere 0.1% of CD amount (fee also in HⲶ∆⟙) paid at time of CD creation (no fees at claim) and sent to **@fuegoxfg** as a donation to the Fuego Developer Fund.

| Command | Description |
|---------|-------------|
| `heat_deposit <amount> <term_epochs>` | Lock HEAT for epoch-based yield |
| `heat_withdraw <deposit_id>` | Redeem matured CD with accrued interest |
| `heat_list` | List active HEAT CDs and balance |

### Atomic Swaps (swapxfg)

Cross-chain XFG atomic swaps via COMIT protocol with HTLC adaptor signatures. Fee routing bridges SwapDaemon into on-chain CD yield and treasury.

### Documentation

Comprehensive docs at [docs.usexfg.org](https://docs.usexfg.org) and in `docs/`:
- `getting-started/` — Onboarding
- `features/` — Feature guides
- `api-reference/` — RPC API
- `design/` + `developer/` — Architecture & protocols
- `security/` — Audits & hardening
- `HEAT_STABLECOIN_VISION.md` — HEAT economic design
- `ATOMIC_SWAP_PLAN.md` — Swap protocol plan
- `PRIVACY_ROADMAP.md` — Privacy milestones

### Advanced

- **Parallel build**: `make -j$(nproc)`
- **Debug build**: `make build-debug`
- **Tests**: `make test-release`
- **Static binary**: `make build-static`

---

For the most user-friendly graphic interface experience, see [Fuego Desktop Wallet](https://github.com/usexfg/fuego-wallet).

_____________________________
<sup>Made with ❤️‍🔥 by Elderfire Mob</sup>


Join Fuego's worldwide community [Discord](https://discordapp.com/invite/5UJcJJg), [Reddit](https://reddit.com/r/Fango), or [Twitter](https://twitter.com/usexfg).
