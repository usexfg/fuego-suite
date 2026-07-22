---
title: "Changelog"
description: "Fuego Suite Updates & Features"
---

<CodeGroup>
```text Changelog
v1.11 docs + HEATWAVE-era product (Mintlify 2026-07)
* HΞ∆₮ documented as purchasing-power flatcoin (launch \$1.58), not USD stablecoin.
* Hearth: XFG/HEAT AMM + hybrid orderbook market band (pool orders).
* Dynamixn: approved ring sizes 32 / 16 / 8 (BMV 10+); same-amount decoys.
* Subaddresses for receive documented (WalletGreen createSubAddress).
* SwapXFG / Alice-locks atomic-swap narrative; fee pipes split vs Hearth LPs.
* HEAT CDs earn from atomic-swap fees; Hearth 0.3% → LPs only.
* Testnet: mixIn=0 wallet bootstrap; mining race + difficulty floor fixes.
* Docs roadmap page for Mintlify accuracy work.

v1.10.02
* AFK atomic swaps: soft-order auto-execution engine (OfferManager), griefing
  protection with taker rate-limiting, partial fills, price-drift auto-cancel,
  headless daemon mode with StatusServer monitoring endpoint.
* Cross-chain timelock-ordering safety via SwapTimelock (SOL/ETH/XMR/BCH/ARB/BASE).
* ETH EIP-155 offline signing (secp256k1); adaptor-secret at-rest persistence
  with CryptoNight-KDF + ChaCha8 + HMAC-SHA256.
* Ed25519 P2P message authentication for all swap peer messages.
* Wallet integrity upgraded to HMAC-SHA256 (v7); file permissions set to 0600.
* RPC error responses sanitized (no internal details exposed to clients).
* Alias registration privacy: per-tx sub-address rotation, ring-sig mixin.
* Small-subgroup point rejection in adaptor/DLEQ verifiers.
* RNG fork safety (pthread_atfork reseed); modulo-bias elimination.
* Taker P2P swap-request relay; cancel-command fix.
* Arbitrum and Base L2 chain support (EVM adaptor swaps).
* SwapP2P hardened: default loopback bind, worker cap, bounded message queue.
* PriceOracle bootstrap drift guard (±50% from seed rate).

v1.10.01
* Clean up repo for release.
* Fix stream tools endianness conversion for integers.

v1.9.3
* Intent-based soft orders for AFK atomic swaps.
* Implement AFK Adaptor Swaps: Taker flow, monitoring view, and refund mechanism.
* TUI Updates.
* ZK prover for HEAT merkle.
* Elderfier broadcast fix to keep merkle as source of truth not p2pmsg.
* Update Boost dependency to 1.86.
* RPC Server Modernization.
* Implement CD banking fee burn and SwapDaemon CD support.

v1.9.2
* Add subaddresses support to core and simplewallet.
* Initialize Fuego Flutter Wallet project.
* Add Elderfier CLI menu system.
* Digm coin branch integration.

v1.9.1
* Add comprehensive walletd build workflow.
* Update difficulty calculation adjustments.

v1.9.0
* Add comprehensive simplewallet functionality for checking CDs, Burns, and Aliases.
* Dandelion++ support (Upgrade Height v10).
* (FuegoMeshtastic and FuegoI2P components exist but are in integration phases, see codebase).
```
</CodeGroup>

> **Privacy Note:** Fuego relies on ring signatures (dynamixn sizes 32/16/8 on BMV 10+) and receive subaddresses. Per-amount decoy selection and HEAT bill denominations bulk the ring pool. Amounts transferred may still be visible on-chain until later privacy majors.
