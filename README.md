# Fuego (XFG) — Decentralized Privacy Blockchain with Native Yield-Bearing CDs & Algorithmic Stablecoin

<img title="Fuego Blockchain" src="https://raw.githubusercontent.com/usexfg/fuego-data/master/images/fuegoline.gif">

**Fuego is an open-source, community-driven, decentralized P2P blockchain network and privacy cryptocurrency.** Built on the CryptoNote protocol, Fuego has evolved into a multi-asset L1 platform featuring native Certificate of Deposit (CD) yield-bearing accounts, cross-chain atomic swaps, a planned consensus-embedded AMM, and an algorithmic stablecoin (HEAT) governed by a PI controller.

## Key Highlights

| Attribute | Detail |
|-----------|--------|
| **Ticker** | XFG |
| **Max Supply** | ~8,000,008.8 XFG (80,000,088,000,008 atomic units) |
| **Decimals** | 7 |
| **Consensus** | Proof of Work (CryptoNote-based) |
| **Block Time** | 480 seconds (8 minutes) |
| **Privacy** | Ring signatures (mixin 8-18), stealth addresses, I2P support |
| **Current Version** | v1.9.3 "GODFLAME" (v10) |
| **Active Since** | 2017 |
| **License** | Open Source (BSD 3-Clause, MIT, LGPLv3) |
| **AI Tooling** | Built-in RAG system, MCP server, blockchain specialist agent |

## What Makes Fuego Unique

- **Native Certificate of Deposit (CD) Yield**: Lock XFG for ~3 months (16,440 blocks) and earn 80% of all swap fees distributed per epoch (900 blocks / ~5 days). No staking, no validator risk — pure on-chain, non-custodial yield.
- **EternalFlame Tokenomics**: Burned XFG (e.g., for HEAT minting) recycles into future block rewards, maintaining long-term miner security budget instead of permanently destroying supply.
- **Dual-Token Economy (v11 planned)**: XFG (privacy reserve) + HEAT (algorithmic stablecoin via PI controller).
- **Consensus-Embedded AMM (v11 planned)**: "Hearth" AMM with trustless XFG/HEAT swaps directly in consensus rules.
- **Cross-Chain Atomic Swaps**: HTLC/adaptor-signature based swaps compatible with COMIT protocol — XMR, ETH, BCH supported.
- **Privacy Roadmap**: CLSAG ring signatures + Bulletproofs+ range proofs (hidden amounts, ~1.9 KB tx size) planned for v11; Triptych logarithmic ring proofs for full anonymity set in v12.
- **DIGM Platform**: Music/audio use-case layer driving swap volume and CD yield sustainability.

## Tokenomics

- **Hard cap**: ~8,000,008.8 XFG (8M8) — sound money, no inflation, no tail emission
- **EternalFlame**: Burned XFG recycles into miner rewards instead of destroying supply
- **Fee distribution**: 80% to CD holders, 20% to treasury
- **HEAT**: Algorithmic stablecoin minted by burning XFG at floating redemption price (starting at 0.5 XFG/HEAT), governed by PI controller with proportional and integral error correction

## Privacy Features

### Currently Working (v10)
- Ring signatures (dynamic mixin 8-18 via OSPEAD adaptive decoy selection)
- Stealth addresses (one-time keys per transaction)
- Key images (double-spend prevention)
- CommitmentIndex per-amount decoy selection
- HEAT burns as permanent decoys
- I2P network-level privacy
- Dynamic ring size (OSPEAD algorithm with logarithmic age bins)

### Roadmap
| Phase | Feature | Privacy Gain |
|-------|---------|-------------|
| Phase 3-4 | 2-output uniformity, Dandelion++ relay | Hides tx origin IP |
| v11 fork | CLSAG + Bulletproofs+ range proofs | Hidden amounts, global ring pool, ~1.9 KB tx |
| v12 fork | Triptych logarithmic ring proofs | Full output set anonymity (10k-1M+) |

## Community & Resources

| Resource | Link |
|----------|------|
| **Website** | https://usexfg.org |
| **Discord** | https://discord.gg/5UJcJJg |
| **Twitter / X** | https://twitter.com/useXFG |
| **Medium** | https://medium.com/@usexfg |
| **BitcoinTalk** | https://bitcointalk.org/index.php?topic=2712001 |
| **Reddit** | https://reddit.com/r/Fango |
| **GitHub** | https://github.com/usexfg/fuego |

### Block Explorers
- http://fuego.spaceportx.net
- http://radioactive.sytes.net:8000
- https://explore-xfg.loudmining.com

## Build Status

| Platform | Status |
|----------|--------|
| macOS | [![macOS](https://github.com/usexfg/fuego/actions/workflows/macOS.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/macOS.yml) |
| Ubuntu 20.04 | [![Ubuntu 20.04](https://github.com/usexfg/fuego/actions/workflows/ubuntu20.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/ubuntu20.yml) |
| Ubuntu 22.04 | [![Ubuntu 22.04](https://github.com/usexfg/fuego/actions/workflows/ubuntu22.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/ubuntu22.yml) |
| Windows | [![Windows](https://github.com/usexfg/fuego/actions/workflows/windows.yml/badge.svg)](https://github.com/usexfg/fuego/actions/workflows/windows.yml) |

## Development Activity

- **Active contributors**: Core development team plus community contributors
- **Recent focus**: v10 HEAT colored coin, atomic swap infrastructure, PI controller development, ZK prover system (Rust)
- **AI infrastructure**: RAG system for semantic codebase search, MCP server for AI agent integration, blockchain specialist agent for automated analysis
- **Codebase**: C++ core (CryptoNote), Go swap daemon, Rust ZK prover, Python AI tooling

## Build & Run

### Linux / macOS

```bash
# Prerequisites: GCC 4.7.3+, CMake 2.8.6+, Boost 1.55+
sudo apt-get install build-essential git cmake libboost-all-dev  # Linux
brew install git python cmake gcc boost                          # macOS

git clone https://github.com/usexfg/fuego
cd fuego
make
./build/release/src/fuegod
```

### Windows (Visual Studio 2019)

```bash
git clone https://github.com/usexfg/fuego
cd fuego
mkdir build
cmake .. -G "Visual Studio 16 2019" -A x64 -DBOOST_LIBRARYDIR="c:\local\boost_1_73_0\lib64-msvc-14.2"
msbuild fuegoX.sln /p:Configuration=Release /m
```

### Desktop Wallet

For the most user-friendly GUI experience, use the [Fuego Desktop Wallet](https://github.com/usexfg/fuego-wallet).

## How to Get Involved

1. **Join the community**: [Discord](https://discord.gg/5UJcJJg) — developers, miners, and holders welcome
2. **Run a node**: Build from source and run `fuegod`
3. **Mine XFG**: Connect your miner to the network — block time is 8 minutes with LWMA difficulty adjustment
4. **Earn CD yield**: Lock XFG in a Certificate of Deposit for ~3 months to earn 80% of swap fees
5. **Contribute code**: PRs welcome — check open issues and development roadmap
6. **Follow development**: [Twitter](https://twitter.com/useXFG), [Medium](https://medium.com/@usexfg)

## Advanced Build Options

- Parallel build: `make -j<threads>`
- Debug build: `make build-debug`
- Test suite: `make test-release`
- Clang build: `export CC=clang CXX=clang++` before `make`
