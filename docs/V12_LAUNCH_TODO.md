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

### 3. Alias registration fee: re-mint the burned total to the dev fund
From v11 the alias registration fee is burned and counted in its own total (not the Eternal Flame), so no alias transaction carries an output to the dev wallet. Re-minting that total to the dev fund needs a consensus rule (for example: at each quarter boundary the coinbase must carry one output to `FUEGO_DEV_FUND_ADDRESS` equal to the pending alias-burn total, verified with the dev view key as the old per-alias check was, then the total resets). Decide the period and implement before the first re-mint is due.

## Should do

- Tests that drive `pushTransaction` / `popTransaction` for the limit-order stop tag (AuntPoL legs and the alias-burn total are covered).
- A test harness that mines a valid v11 block, to cover `pushBlock` rejecting a mismatched `HearthState`.
