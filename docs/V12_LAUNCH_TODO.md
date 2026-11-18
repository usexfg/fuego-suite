# V12 Launch To-Do

Items that must be done, or decided, before v12 (CD activation) goes live. Each entry says what has to change and how to verify it. Source changes need a `CHANGELOG.agent.md` entry.

## Must do

### 0. Wallets cannot see HEAT
`TransfersContainer::isIncluded` (`src/Transfers/TransfersContainer.cpp`) drops every `HEAT_TERM` commitment from every type filter, `IncludeTypeAll` included. WalletLegacy's HEAT balance and both wallets' deposit indexes (`WalletLegacy` and `WalletGreen::insertNewDeposit`) read through that filter, so neither wallet ever records a spendable HEAT output. Confirmed on a local testnet (2026-11-18): a mint confirmed on chain (`heat_supply` 49.75 HEAT) and SimpleWallet reported 0 HEAT. Every HEAT spend (CD, send, sell, LP add) is blocked in both wallets.

Needed: a HEAT output type flag in `ITransfersContainer`, HEAT balance and deposit-index queries that use it, and an audit of the six `IncludeTypeAll`/`IncludeAll` queries (`WalletLegacy.cpp` 318, 1693, 1945; `WalletGreen.cpp` 3193, 3399, 5781) so HEAT never counts as XFG. Verify: mint, then spend HEAT from SimpleWallet and from walletd on testnet.

### 0b. HEAT decoys can break rings
`Blockchain::getRandomCommitmentOutputsForAmount` returns any commitment output of the amount, including pool-reserve outputs (consensus rejects a ring holding one) and XFG-class commitments (consensus rejects mixed asset classes). A HEAT spend from either wallet fails whenever a drawn decoy is one of these. Needed: the node returns only members a ring of the requested asset may hold (the RPC request needs the asset class), and a test that a returned set always passes `checkCommitmentSpendInput`'s member checks.

### 1. CLI wallet CD deposits need XFG inputs
**Code DONE 2026-11-18; end-to-end blocked by item 0.** WalletLegacy now selects XFG for `network fee + declared CD fee`, fetches XFG decoys and then HEAT decoys for each distinct HEAT amount, adds every input before signing any, and wipes the signing keys. The verify step below has not passed: SimpleWallet sees no HEAT (item 0).

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

- Standalone dev donation command: DONE 2026-11-12, named `dev_donate` 2026-11-17 (SimpleWallet command and walletd JSON-RPC). The quarterly re-mint amount is only uninformative about alias count once people actually send standalone burns.

- Tests that drive `pushTransaction` / `popTransaction` for the limit-order stop tag: DONE 2026-11-18 (`testRestingOrderListFollowsEveryChange` pushes and pops a signed stop transaction; resting state and escrow checked both ways).
- A test harness that mines a valid v11 block, to cover `pushBlock` rejecting a mismatched `HearthState`: DONE 2026-11-12 (`testBlockConnectEnforcesHearthStateAndRemint`).
- Expired limit deposits nobody withdraws: DONE 2026-11-17. Blocks walk only resting orders (and withdrawn ones for pruning), so abandoned orders cost no block work and stay claimable; wallets withdraw their own expired or stopped orders after each sync.
- Revisit after the new privacy methods are implemented: the CD-terms / ring-size reset in `CurrencyBuilder`. The constructor picks the minimum ring size (`minMixin(0)` for testnet) and the network ID string from `m_testnet`, then ends with `testnet(false)`; `testnet(bool)` resets the CD deposit terms, address prefix and unlock window but never the minimum ring size or the network ID. Until 2026-11-13 `m_testnet` was uninitialised there (UBSan read 48), so a mainnet node could start with minimum ring size 0 and the testnet network ID; it is now initialised to false, so mainnet is always correct and testnet always keeps the mainnet minimum ring size and network ID. When the privacy model's ring rules are set, make `testnet(bool)` set every network-dependent value.
- Testnet `cdActivationHeight()`: DONE 2026-11-18. It returned the v11 height (30) while CD outputs open only in v12 blocks (181); a v10 block at height 30 can hold a legacy commitment, and a ring whose youngest member sat there passed the legacy floor. `cdActivationHeight()` is now the v12 height on every network (`testCdActivationHeight`).
- WalletLegacy LP add (`makeLpAddV10Request` / `doSendLpAddV10Transaction`) puts HEAT transfers in the XFG selection (key decoys requested for HEAT amounts; `prepareKeyInputs` asserts key outputs) and signs XFG inputs before the HEAT inputs are added. Rebuild it the way the CD sender now works.
- Other WalletLegacy commitment spends (send HEAT, sell, withdraw) fetch HEAT decoys for the first input's amount only; any spend of mixed HEAT amounts fails its ring check.
- `heat_cd` prints `Usage: heat_deposit`; `fuegod --start-mining` did not start the miner on an isolated testnet (the `start_mining` RPC did).
