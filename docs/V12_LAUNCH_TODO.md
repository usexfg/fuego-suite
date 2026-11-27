# V12 Launch To-Do

Items that must be done, or decided, before v12 (CD activation) goes live. Each entry says what has to change and how to verify it. Source changes need a `CHANGELOG.agent.md` entry.

## Must do

### 0. HEAT spends — DONE 2026-11-19 (plan owner-approved 2026-11-18, before v11)
Confirmed on a local testnet (2026-11-18): a mint confirmed on chain (`heat_supply` 49.75 HEAT) and SimpleWallet reported 0 HEAT. Four defects stack; each must go.

**0.1 Consensus: v11 HEAT is unspendable.** `checkCommitmentSpendInput` and the CommitmentTransfer check reject a ring whose members are all `HEAT_TERM` ("all ring members are burned outputs"). Every mint and every HEAT change output is `HEAT_TERM`, so no HEAT spend can pass. The guard exists because pre-v11 `HEAT_TERM` outputs (0x08-era burns, allowed in v10 blocks below `HEATWAVE_TAG_CUTOFF_HEIGHT`) are burns.
- Rule for v11+ blocks: a `HEAT_TERM` member created in a block below v11 is a burn; a ring holding one is rejected. A ring of `HEAT_TERM` members all created in v11+ blocks is valid. Pre-v11 blocks keep the current rule unchanged (replay).
- Effect: pre-v11 `HEAT_TERM` outputs can never be spent or used as decoys. The current rule let a ring mixing a pre-v11 burn with one CD decoy pass; the new rule closes that.
- Tests: a post-v11 all-`HEAT_TERM` ring passes; a ring with a pre-v11 `HEAT_TERM` member fails, alone or mixed; a pre-v11 block still fails an all-`HEAT_TERM` ring.

**0.2 Node decoys: rings drawn from the node can be invalid.** `getRandomCommitmentOutputsForAmount` returns any commitment of the amount, including pool-reserve, slashed, immature, pre-v11 burn and other-asset members, all of which consensus rejects.
- The request carries the term the ring must share (`term`, 0 = no term filter). The node always drops pool-reserve, slashed, immature and pre-v11 `HEAT_TERM` members, and with a term set returns only that term. `INode::getRandomCommitmentOutsForAmount` gains the parameter (NodeRpcProxy, InProcessNode, test stubs).
- Wallets pass the real input's term. HEAT spends stop capping decoy height at the real input's height (that made the real input always the youngest member); CD claims keep the cap, which the interest rule needs.
- Test: every member the node returns passes `checkCommitmentSpendInput`'s per-member checks.

**0.3 Transfers: HEAT is dropped from every query.** `TransfersContainer::isIncluded` drops `HEAT_TERM` commitments from every type filter.
- New type flag `IncludeTypeHeat` for `HEAT_TERM` commitments.
- Queries that must not count HEAT as XFG stop using all-type flags: WalletLegacy reserve proof (key outputs only); WalletGreen available balance (key + multisig outputs; it also counted matured deposits and CDs as XFG).
- Test in `TestTransfersContainer`: a `HEAT_TERM` output appears under `IncludeTypeHeat` only.

**0.4 Wallet indexes: HEAT never becomes a spendable record.**
- WalletLegacy: deposit records include HEAT outputs. A `HEAT_TERM` record is unlocked when created and is never treated as a burn. Records are created on confirmation for every transaction, the wallet's own included (they were made only for external ones). The HEAT balance is the sum of spendable HEAT records. Selection skips records whose transfer is not available.
- WalletGreen: the deposit index is unique by (transaction, output); it was unique by transaction, so a mint's second bill was dropped and spends were matched to the wrong record. HEAT records include `HEAT_TERM` outputs, are unlocked when created whatever tag the transaction carries, and have `unlockHeight` = creation height (it overflowed). HEAT balances skip spent records and split locked/unlocked by `unlockHeight`.
- Existing testnet wallets must `reset` once to index past HEAT outputs. Mainnet holds no v11 HEAT yet.

**0.5 WalletLegacy commitment spends.** Every sender (CD, send HEAT, sell, withdraw/claim, LP add/remove/claim) fetches decoys per (amount, term) of each input, adds all inputs before signing any, and wipes signing keys. LP add keeps HEAT out of the XFG selection.

**0.6 Found while doing 0.1–0.5 (same approval):**
- Consensus maturity read marker terms as block counts. LP shares (`0xFFFFFFFD`), swap-receive XFG (`'SWRX'`) and DIGM never matured, so none could be spent; the transfers container did the same. From v11 only CD terms are time-locked (`isCdCommitmentTerm`, shared by consensus, the container and both wallets), and only CD terms count as CDs for interest.
- DIGM mint is disabled at v11 and DIGM classifies as HEAT, so a DIGM output could stand in a HEAT ring. From v11 a ring holding a DIGM member is rejected.
- LP remove: settlement paid the formula payout at inclusion, and HEAT must balance exactly, so a wallet's payout matched only if no block moved the reserves. From v11 the payout is the declared minimums, which must not exceed the formula payout; the pool keeps the rest, as LP add already does for shares.
- LP add (WalletLegacy) declared `amountXfg + amountHeat` as shares; consensus caps shares at the AMM formula, so every add was rejected. The wallet computes shares with the formula from the node's reserves and share supply, less slippage.
- HEAT sells (both wallets) paid XFG proceeds into a swap-receive commitment that neither wallet counts as XFG. Proceeds become ordinary XFG outputs.
- LP fee claim (`lp_claim`, `AmmClaim`): not a settlement tag from v11, so the transaction only paid a fee. LP fees reach LPs through the reserves on LP remove; the command is removed.
- WalletLegacy CD withdraw paid HEAT principal and interest as XFG outputs (cross-asset, rejected) and signed inputs before adding the rest and the bonus-claim extras. It now pays HEAT and signs last.

**0.7 Found during testnet verification (same approval):**
- `popTransactions` cleared every output index (key, multisig, commitment, swap escrow) after popping a block. After any reorg or rejected alternative block the node held no ring members and refused every ring spend. Popping now removes only that block's outputs (`testBlockPopKeepsEarlierOutputs`).
- `pushTransaction` recorded each commitment key image as spent and then looked it up as a double spend, so no transaction spending HEAT, a CD or LP shares could connect and any block holding one was rejected (`testCommitmentSpendConnectsOnce`).
- The stateless output check judged CD terms by the mainnet range on every network; testnet CDs never reached consensus (`testOutputCheckUsesTheNetworkTermRange`).
- `check_tx_semantic`'s aggregate amount check rejected v11 payouts (LP remove, limit withdraw) whose outputs exceed their inputs. From v11 the per-asset settlement check is the only amount rule.
- `get_limit_orders` requests from wallets were uninitialised (`active_only` read from stack garbage), so expired orders could be invisible to auto-withdraw and `cancel_order`.
- WalletLegacy: `reset` kept deposit records pointing at renumbered transactions; own sent transactions never stored their hash; height, unlock height and hash of deposit records were lost on reload; a failed send kept its deposits marked spent. All fixed; records rebuild their derived fields on load and failed sends release their deposits.
- SimpleWallet's console dropped every command exception silently; it now prints them.

**Verified on a local testnet (2026-11-19, SimpleWallet):** mint → HEAT balance; sell HEAT (pool +5 HEAT); CD (AuntPoL XFG leg +0.5970393 XFG, the declared fee exactly); send HEAT to a second wallet; LP add (70,706,084 shares); LP remove at the declared payout; CD claim after maturity (principal returned; bonus 0 with an empty Bonus Vault); HEAT-escrow limit order and its withdraw; wallet reopen without rescan keeps HEAT, CDs and balances.

**Open:**
- walletd (WalletGreen) end-to-end on testnet: not run. Its fixes (per-output deposit index, HEAT records, sell proceeds, balances, decoy terms) are built and unit-tested only.
- Two WalletLegacy sends started close together can pick the same XFG fee input: inputs are chosen when the request is made and marked used only when it is built. Seen once on testnet (auto-withdraw and a manual `cancel_order` at the same moment); the node rejected one and the auto-withdraw retried 10 blocks later and succeeded.
- CD interest payout with a funded Bonus Vault and CD yield pool: not exercised on testnet.
- `list_cds` prints mainnet day estimates on testnet.

**Verify (testnet, both SimpleWallet and walletd):** mint → HEAT balance shows → send HEAT, sell HEAT, CD (AuntPoL XFG leg grows by the declared fee) → each is mined.

### 1. CLI wallet CD deposits need XFG inputs
**DONE 2026-11-19: a SimpleWallet CD was mined on testnet and the AuntPoL XFG leg grew by exactly the declared fee.** WalletLegacy now selects XFG for `network fee + declared CD fee`, fetches XFG decoys and then HEAT decoys for each distinct HEAT amount, adds every input before signing any, and wipes the signing keys. The verify step below has not passed: SimpleWallet sees no HEAT (item 0).

`WalletTransactionSender::doSendHeatDepositV10Transaction` (`src/WalletLegacy/WalletTransactionSender.cpp`) spends only HEAT commitment inputs. It adds no XFG inputs, so the transaction pays neither the network fee nor the CD creation fee, and consensus rejects every CD that SimpleWallet / WalletLegacy builds. WalletGreen (walletd, GUI) builds them correctly.

Needed:
- In `makeHeatDepositV10Request`, select regular XFG transfers covering `network fee + declared CD creation fee` (the fee is `cdCreationFeeXfg(...)` padded by `WALLET_MINT_TWAP_MARGIN_BPS`, declared in `TransactionExtraCdCreationFee`).
- Request random outputs for those XFG amounts alongside the HEAT commitment decoys (both request kinds, then build).
- In the sender, add the XFG key inputs and an XFG change output, then sign key inputs and commitment spends after all inputs are present (as WalletGreen's `addAndSignInputs` does).
- Verify: a CD built by SimpleWallet on testnet passes `checkTransactionSettlement` and is mined; the AuntPoL XFG leg grows by the declared fee.

### 2. Bonus Vault — no change needed (owner, 2026-11-18)
The Bonus Vault is the protocol's minimum CD yield for lean epochs. The code already does this: `calculateCdBonus` tops a CD up to `CD_YIELD_FLOOR_RATE` in any epoch whose fee rate fell below it, capped at settlement by the vault balance. The loyalty multipliers this entry used to describe were replaced by that floor. The CD creation fee schedule (0.25% at 6 epochs to 1% at 72) is unchanged.

### 3. Re-mint burn and quarterly re-mint — DONE (2026-11-12)
`TX_EXTRA_REMINT_BURN` is its own burn type and counter; alias registrations pay their fee with one. Each FCI quarter boundary (from v11) the coinbase must end with one output paying the not-yet-re-minted total to the dev fund (`Blockchain::remintDue` / `coinbasePaysRemint`). `testBlockConnectEnforcesHearthStateAndRemint` mines testnet blocks through `pushBlock` / `popBlock` across the quarter boundary.

### 4. Aliases open at v12 (owner decision 2026-11-27) — DONE
Rule: `aliasTagsRefused(v)` for v11 (every 0xEA/0xEC/0xED tag invalid in blocks, pool, template); `aliasOwnerRulesApply(v)` from v12 (funsa owner, owner hash binding, `aliasTagsApply` claim checks, re-mint burn fee). Wallets refuse before v12 (`ALIASES_NOT_ACTIVE`, `planAliasRegistration`). Owners are `(0, ALIAS_MINOR_BASE + k)`; payment subaddresses never reach `ALIAS_MINOR_BASE` (`docs/developer/funsa-subaddress-rollout-plan.md` 4.4). The e2e alias scenarios run from `TESTNET_UPGRADE_HEIGHT_V12 + 2`. Verify: `alias_owner_rule_test`, `wallet_operations_test`, `core_tests` (`testAliasTagsRefusedInV11Settlement`, `testPoolDropsRegistrationFailingTheOwnerRule`), e2e `HeatWalletScenario`.
- Testnet wipe: `pushTransaction` refuses alias tags at v11, but a testnet chain that already holds v11 blocks with alias tags (registered under the earlier v11 rule) replays them as unregistered and no longer matches the aliases its wallets and peers saw. Wipe such testnet chains before v12.
- Restore scan: Fuego wallets (WalletGreen, SimpleWallet, walletd) have no restore-scan code, so nothing in this repo scans for alias owners after a restore. The scan window for alias owners is major 0, minors `ALIAS_MINOR_BASE + k` (0x80000000 + k), next to the payment range from minor 1. It applies to Valise and to any Fuego restore scan added later.

## Should do

- Standalone dev donation command: DONE 2026-11-12, named `dev_donate` 2026-11-17 (SimpleWallet command and walletd JSON-RPC). The quarterly re-mint amount is only uninformative about alias count once people actually send standalone burns.

- Tests that drive `pushTransaction` / `popTransaction` for the limit-order stop tag: DONE 2026-11-18 (`testRestingOrderListFollowsEveryChange` pushes and pops a signed stop transaction; resting state and escrow checked both ways).
- A test harness that mines a valid v11 block, to cover `pushBlock` rejecting a mismatched `HearthState`: DONE 2026-11-12 (`testBlockConnectEnforcesHearthStateAndRemint`).
- Expired limit deposits nobody withdraws: DONE 2026-11-17. Blocks walk only resting orders (and withdrawn ones for pruning), so abandoned orders cost no block work and stay claimable; wallets withdraw their own expired or stopped orders after each sync.
- Revisit after the new privacy methods are implemented: the CD-terms / ring-size reset in `CurrencyBuilder`. The constructor picks the minimum ring size (`minMixin(0)` for testnet) and the network ID string from `m_testnet`, then ends with `testnet(false)`; `testnet(bool)` resets the CD deposit terms, address prefix and unlock window but never the minimum ring size or the network ID. Until 2026-11-13 `m_testnet` was uninitialised there (UBSan read 48), so a mainnet node could start with minimum ring size 0 and the testnet network ID; it is now initialised to false, so mainnet is always correct and testnet always keeps the mainnet minimum ring size and network ID. When the privacy model's ring rules are set, make `testnet(bool)` set every network-dependent value.
- Testnet `cdActivationHeight()`: DONE 2026-11-18. It returned the v11 height (30) while CD outputs open only in v12 blocks (181 at the time; 1001 since the 2026-11-19 testnet schedule); a v10 block at height 30 can hold a legacy commitment, and a ring whose youngest member sat there passed the legacy floor. `cdActivationHeight()` is now the v12 height on every network (`testCdActivationHeight`).
- `heat_cd` prints `Usage: heat_deposit`; `fuegod --start-mining` did not start the miner on an isolated testnet (the `start_mining` RPC did). Both in item 0 scope.
- WalletLegacy LP add and the other WalletLegacy commitment spends: item 0.5.

## FCI at v12 (owner decision 2026-11-25)
Through v11 the FCI records votes and runs every step except two, both reserved for v12:
- the quarterly hearthstone re-base (already gated on `BLOCK_MAJOR_VERSION_12` in `FuegoCostIndex.cpp`);
- DIGM rewards to blocks carrying an FCI vote. Not implemented in any version yet; needs a design (amount, source, gating) before v12.

