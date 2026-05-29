---
title: "Burning XFG"
description: "Create a permanent burn deposit in simplewallet."
keywords: ["burn", "HEAT", "forever", "commitment", "fuego", "xfg"]
---

## Burn command

```
burn <amount>
```

Example — permanently lock 8 XFG:

```
burn 8
```

The wallet prompts for confirmation and displays the consequences. Confirm only if you understand that this XFG cannot be recovered.

## What happens on-chain

A burn creates a `TransactionOutputCommitment` with `DEPOSIT_TERM_FOREVER`. This is identical to a normal CD output except the term is set to the maximum possible value, meaning no maturity height is ever reached.

With the v10+ update, both HEAT burns and CD deposits write a single unified `0xD5` tag into `tx_extra`. The deposit type (e.g., `0x08` for HEAT burns, `0x01` for COLD) is encoded inside the encrypted payload, obscuring the difference to outside observers. This allows HEAT burns and CD deposits to be indistinguishable on-chain and feed into the same commitment decoy pool.

The blockchain enforces this — there is no `withdraw` or `rollover` path for a forever-term deposit. The consensus rules reject any spend of a forever-term commitment.

## Store your deposit secret

After burning, your simplewallet stores an encrypted deposit secret in the `0xD5` tag. You will need this secret to generate the proof that claims your HEAT tokens.

<Warning>
  If you lose your wallet file and seed phrase after burning, you lose the ability to generate a proof and claim HEAT. Back up your wallet file before and after burning.
</Warning>

## Verify the burn was recorded

```
list_burns
```

Lists all burn deposits in your wallet with their amount and on-chain status.

```
burn_info 0
```

Displays detailed information about burn ID 0, including the block height it was confirmed in.

## After the burn

Once the burn transaction is confirmed, proceed to [generate a proof](/features/heat-burns/generating-proofs) to claim HEAT tokens off-chain.
