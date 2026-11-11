# SWAPXFG: Fuego Cross-Chain Swap Terminal User Guide

SWAPXFG is the official elite terminal interface for performing privacy-preserving atomic swaps between Fuego (XFG) and external blockchains (SOL, ETH, XMR, BCH). It leverages **Adaptor Signatures** (MuSig2) on the XFG side to ensure that your swaps never reveal your stealth address or link your trades on-chain.

---

## 1. Getting Started

### 1.1 Installation
Ensure you have Go 1.24+ installed.
```bash
cd swapxfg
go build -o swapxfg .
```

### 1.2 Connecting to the Network
SWAPXFG requires a connection to a Fuego daemon (`fuegod`) and a wallet (`walletd` or `fuego-wallet-rpc`).

**Standard Mainnet Start:**
```bash
./swapxfg --daemon http://127.0.0.1:18180 --wallet http://127.0.0.1:18282
```

**Testnet Start:**
```bash
./swapxfg --testnet
```

**Bridge Support (for MetaMask/Phantom):**
```bash
./swapxfg --bridge-port 8545
```

---

## 2. The Terminal Interface

The TUI is divided into four primary quadrants:
1.  **Market Header**: Shows active pair (e.g., XFG/SOL) and connection status.
2.  **Orderbook**: Real-time buy/sell offers from the P2P network.
3.  **Balances**: Your current XFG balance and connected bridge balances.
4.  **Command Console**: Where you issue instructions to the terminal.

### 2.1 Navigation
-   `pair <name>`: Switch active market (e.g., `pair eth`, `pair xmr`).
-   `c`: Quick-jump to the **CD (Confidential Deposit) Market**.
-   `help`: Displays the internal command reference.

---

## 3. External Connectivity

### 3.1 Browser Bridges (ETH/SOL)
To swap with Ethereum or Solana, you must connect SWAPXFG to your browser wallet.
1.  Run SWAPXFG with `--bridge-port`.
2.  In the terminal, type `connect metamask` or `connect phantom`.
3.  Your browser will open a local bridge page. Confirm the connection.
4.  Balances will now populate in the SWAPXFG interface.

### 3.2 Bitcoin Cash (BCH)
Connect to an Electron Cash instance for BCH swaps:
-   `connect bch`: Attempts to link with a running Electron Cash JSON-RPC server.
-   **Note**: Automated BCH locking is currently in development. Users must manually verify and claim swaps via the `bch claim` command using pre-computed preimages provided by the `SwapDaemon`.

---

## 4. Swap Operations

SWAPXFG supports two types of order fulfillment: **Soft Orders** and **AFK Locks**.

### 4.1 Soft Orders (Intents)
A Soft Order is an off-chain intent to trade. Your funds are **not locked** until a counterparty accepts the offer. This allows you to place multiple orders without committing capital immediately.
-   `offer <xfg_amt> <target_amt> <pair> <timeout_hrs>`: Prepares a soft offer.
-   `confirm-offer <...> true`: Publishes the intent to the network.

### 4.2 AFK Locks (On-Chain)
An AFK Lock commits XFG into a 2-of-2 MuSig2 escrow immediately. This is safer for long-running "Away From Keyboard" trades.
-   `confirm-offer <...> false`: Locks funds on-chain and posts the offer.

### 4.3 Accepting an Offer
1.  Select an offer from the orderbook using arrow keys.
2.  Type `accept <offer_id>`.
3.  Follow the prompts to lock counterparty funds (e.g., `eth lock` or `bch lock`).

---

## 5. CD Market Operations

The CD Market allows for the secondary trading of locked Confidential Deposits (yielding Hearth fees).

-   `c`: Enter CD Market mode.
-   `sell cd <key_image> <price_xfg>`: Lists your locked CD for sale.
-   `accept_cd <offer_id>`: Purchase a locked CD. Ownership transfers atomically upon fulfillment.

---

## 6. Troubleshooting & Security

### 6.1 Privacy Mandate
Fuego enforces a **minimum ring size of 8** for all version 10+ transactions. Ensure your wallet has sufficient outputs to satisfy this requirement, or use the `optimizer` command in `fuegod` to prepare your decoys.

### 6.2 Connection Issues
-   **"No wallet connected"**: Ensure your `walletd` is running and the `--wallet` flag matches the RPC port.
-   **Bridge Port Conflicts**: If port 8545 is taken, use a different port and update your browser settings accordingly.

---

*Fuego (XFG) — Sovereign Privacy Banking.*
