# Tempo pair plan (remediation plan 3.3)

Status: plan only. Nothing here is implemented. Tempo is not in the swap catalog.

## 1. Why Tempo cannot be a pair today (F18)

| Fact | Source | Consequence |
|---|---|---|
| Tempo (chain id 4217) has no native coin; `eth_getBalance` returns a constant | `docs/dexfg/DEXFG_SWAP_CHAINS.md` | There is no native value to escrow |
| `HashedTimelock.lock()` escrows `msg.value` only | `src/SwapDaemon/Ethereum/HashedTimelock.sol` | The EVM transport cannot lock anything on Tempo |
| Value on Tempo is held in TIP-20 tokens, an extension of ERC-20 (balances, allowances, `transfer`, `transferFrom`) with 32-byte transfer memos and TIP-403 transfer policies | Tempo TIP-20 overview and specification | A swap must escrow a TIP-20 token through `approve` + `transferFrom` |
| Transaction fees are paid in a USD-denominated TIP-20 token, converted by a protocol Fee AMM; the fee token is chosen per transaction | Tempo TIP-20 overview; Tempo blog "Stablecoin fees" | The daemon's signer must hold and name a fee token; native-gas assumptions in `EthRpcClient` do not hold |
| Sources disagree on whether mainnet is live (one secondary source says March 2026; an older Tempo docs page says testnet only) | search results, 2026-11-07 | Mainnet availability is verified on chain before any work starts (step 0) |

## 2. What the pair is

XFG against one USD stablecoin issued as a TIP-20 token on Tempo. The maker sells XFG for
that token, or buys XFG with it; the roles and the XFG leg are unchanged.

## 3. Work items

| # | Item | Change | Done when |
|---|---|---|---|
| 0 | Chain facts | Read chain id, block time, finality, the fee-token rules of the transaction envelope and whether a plain EIP-1559 transaction is accepted with a default fee token, from a Tempo node (`eth_chainId`, block headers, a test transaction) | Recorded in this file with the node version and date; the catalog's `minBlockTimeMs`/`maxBlockTimeMs` and `txEnvelope` follow from it |
| 1 | Settlement token | Choose one USD stablecoin TIP-20 deployment. Its address comes from the issuer's own documentation and is checked on chain (`symbol`, `decimals`, issuer-controlled mint). A token anyone can create through the TIP-20 factory with the same symbol is not acceptable | Address, decimals and issuer reference recorded here |
| 2 | Price feed | A feed for that token, not for a same-named token elsewhere: a DeFiLlama id for the Tempo deployment if DeFiLlama covers Tempo, else the issuer's canonical asset id with a recorded 1:1 redemption basis. Checked with `scripts/probe-feed-ids.py` and `scripts/verify-defillama-assets.py` | `AssetCatalog.cpp` priced entry and matrix row pass the live audit |
| 3 | Escrow contract | `TokenHashedTimelock`: `lock(token, receiver, hashLock, timelock, amount)` pulls `amount` with `transferFrom` after the sender's `approve` and refuses the lock unless its token balance rose by exactly `amount`; `claim(id, preimage)` pays the receiver when `keccak256(preimage) == hashLock`; `refund(id)` pays the sender after `timelock`. One token per lock; no native value accepted. Reentrancy guarded; state changed before every external call | Foundry tests: happy path, wrong preimage, early refund, double claim, double refund, fee-on-transfer token refused, allowance shortfall, claim and refund both blocked by a TIP-403 policy (documented outcome below) |
| 4 | TIP-403 risk | A token policy can block the receiver or the sender. A blocked claim leaves the lock to the refund; a blocked refund leaves funds locked until the policy changes. Readiness must state this, and the daemon checks both parties against the token's policy before locking | Readiness error names the policy when either party is not authorized |
| 5 | Deployment and pin | Deploy `TokenHashedTimelock` through the same CREATE2 deployer as R4 if the deterministic deployment proxy exists on Tempo; otherwise record the deployer used. Pin its runtime code hash as R3 does for `HashedTimelock` | `hasDeployedHtlcRegistry`-style check passes against the pinned hash on Tempo |
| 6 | Daemon transport | `EthRpcClient`: token lock (approve, lock, receipt and event check), token claim and refund, balance and allowance reads, fee-token selection from step 0. `EthChainClient` readiness: token, registry pin, fee-token balance, policy authorization | Unit tests against a local node with the contract and a TIP-20-compatible test token |
| 7 | Catalog | `SwapPair` id 46, family EVM, settlement asset from step 1, decimals of that token, `support = STAGED` until item 9 passes; regenerate `DEXFG_SWAP_CHAINS.md` and Valise's generated catalog | `test_swap_pair_catalog` and the docs check pass |
| 8 | Valise | Tempo moves from `tier: wallet` to `tier: swap` only after item 9; Swap Settings gains the token, registry and fee-token fields | Valise tests for the config writer |
| 9 | End-to-end gate | Two daemons, XFG testnet and a Tempo test network: happy path, wrong preimage, timeout refund on both sides, policy-blocked claim | Run recorded in the agent changelog; then `support` moves to ACTIVE |

## 4. Order and cost

Items 0-2 decide whether the pair is worth building: without a verifiable issuer deployment
and a feed for it, the pair would be UNPRICED and could not start a swap (the same state as
GLEEC and PEAQ). Items 3-6 are a new contract and a new transport path in `EthRpcClient`;
items 7-9 follow the existing catalog and gate procedure.

## 5. References

- Tempo TIP-20 overview: https://docs.tempo.xyz/protocol/tip20/overview
- Tempo TIP-20 specification: https://docs.tempo.xyz/protocol/tip20/spec
- Tempo blog, stablecoin fees: https://tempo.xyz/blog/stablecoin-fees
