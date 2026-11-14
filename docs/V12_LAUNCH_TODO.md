# V12 Launch To-Do

Items that must be done, or decided, before v12 (CD activation) goes live. Each entry says what has to change and how to verify it. Source changes need a `CHANGELOG.agent.md` entry.

## Must do

### 1. CLI wallet CD deposits need XFG inputs
`WalletTransactionSender::doSendHeatDepositV10Transaction` (`src/WalletLegacy/WalletTransactionSender.cpp`) spends only HEAT commitment inputs. It adds no XFG inputs, so the transaction pays neither the network fee nor the CD creation fee, and consensus rejects every CD that SimpleWallet / WalletLegacy builds. WalletGreen (walletd, GUI) builds them correctly.

Needed:
- In `makeHeatDepositV10Request`, select regular XFG transfers covering `network fee + declared CD creation fee` (the fee is `cdCreationFeeXfg(...)` padded by `WALLET_MINT_TWAP_MARGIN_BPS`, declared in `TransactionExtraCdCreationFee`).
- Request random outputs for those XFG amounts alongside the HEAT commitment decoys (both request kinds, then build).
- In the sender, add the XFG key inputs and an XFG change output, then sign key inputs and commitment spends after all inputs are present (as WalletGreen's `addAndSignInputs` does).
- Verify: a CD built by SimpleWallet on testnet passes `checkTransactionSettlement` and is mined; the AuntPoL XFG leg grows by the declared fee.

### 2. Review the Bonus Vault before CDs go live
The loyalty multipliers (2.5× at 72 epochs, 2× at 36, 1.5× at 18, 1.25× at 6) pull HEAT into long locks. The stated goal is liquid HEAT that prices XFG well. Decide whether to flatten or cut the multipliers, together with the CD creation fee schedule (0.25% at 6 epochs to 1% at 72; per year of lock that is 3.0%/1.55%/1.18%/1.0%, so long locks are still the cheapest per year).

### 3. Re-mint burn and quarterly re-mint — DONE (2026-11-12)
`TX_EXTRA_REMINT_BURN` is its own burn type and counter; alias registrations pay their fee with one. Each FCI quarter boundary (from v11) the coinbase must end with one output paying the not-yet-re-minted total to the dev fund (`Blockchain::remintDue` / `coinbasePaysRemint`). `testBlockConnectEnforcesHearthStateAndRemint` mines testnet blocks through `pushBlock` / `popBlock` across the quarter boundary.

## Should do

- Standalone dev fund burn command: DONE 2026-11-12 (`dev_fund_burn` in SimpleWallet; `dev_fund_burn` JSON-RPC in walletd). The quarterly re-mint amount is only uninformative about alias count once people actually send standalone burns.

- Tests that drive `pushTransaction` / `popTransaction` for the limit-order stop tag (AuntPoL legs and the alias-burn total are covered).
- A test harness that mines a valid v11 block, to cover `pushBlock` rejecting a mismatched `HearthState`: DONE 2026-11-12 (`testBlockConnectEnforcesHearthStateAndRemint`).
- Expired limit deposits nobody withdraws are never pruned (`pruneSettledLimitDeposits` drops only withdrawn ones). They stay in `m_limitDeposits`, and the expiry, auction, backstop and prune passes walk every entry each block. Each one locks at least `LIMIT_ORDER_MIN_XFG` (1 XFG) or `LIMIT_ORDER_MIN_HEAT` (0.1 HEAT), and the owner can withdraw it at any time, so the set grows only with capital left locked. A fix needs an index of resting orders kept in step at about 12 consensus mutation sites (deposit, stop, withdraw, expiry, prune, restore, each of their pops, cache load); a desync there splits consensus. Decide before v11 Hearth goes live.
- Revisit after the new privacy methods are implemented: the CD-terms / ring-size reset in `CurrencyBuilder`. The constructor picks the minimum ring size (`minMixin(0)` for testnet) and the network ID string from `m_testnet`, then ends with `testnet(false)`; `testnet(bool)` resets the CD deposit terms, address prefix and unlock window but never the minimum ring size or the network ID. Until 2026-11-13 `m_testnet` was uninitialised there (UBSan read 48), so a mainnet node could start with minimum ring size 0 and the testnet network ID; it is now initialised to false, so mainnet is always correct and testnet always keeps the mainnet minimum ring size and network ID. When the privacy model's ring rules are set, make `testnet(bool)` set every network-dependent value.
- Testnet `Currency::cdActivationHeight()` returns the v11 height (30), pinned by `testCdActivationHeight`, but `check_tx_outputs` admits CD outputs only in v12 blocks (height 181 on testnet). Wallets now gate CDs on the next block's version, matching consensus. Align `cdActivationHeight()` with the version rule or document why the claim rule keeps the earlier height.
