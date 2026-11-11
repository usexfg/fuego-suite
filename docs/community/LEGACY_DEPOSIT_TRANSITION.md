# On Legacy Deposits, Yield, and What Changed

If you created a COLD deposit before the v10 migration, you were shown a figure: 80% APY. That figure came from a system that no longer exists, and it is my responsibility to explain why.

## What the original COLD system was

COLD used token inflation to generate yield. New coins were minted to pay depositors. Every deposit earning 80% was funded by diluting every XFG holder by a proportional amount. The 80% was a transfer, not a return. Savers gained yield at the expense of everyone else — including the miners who secure the network and the users who provide swap liquidity.

This is how every Proof of Stake chain operates today. It is also why they all trend toward zero in real terms: the yield is a redistribution, not production. The mechanism pays depositors by shrinking the purchasing power of everyone who *isn't* depositing. It is extractive by design.

We deleted it.

## What replaced it

Fuego v10 uses protocol revenue to fund yield. Specifically:

- **80% of atomic swap fees** go to the CD yield pool every epoch (~5 days).
- **100% of Hearth swap fees** go directly to liquidity providers (the people who supply XFG+HEAT to the pool).
- That pool buys HEAT from the Hearth AMM and credits it to CD holders.

This is real yield. It comes from trading activity, not token creation. If swap volume is high, rates are high. If swap volume is low, rates are low. Nobody is diluted. No new coins are minted to pay depositors. The protocol earns revenue from users who trade — and shares it with users who lock.

Current CD rates under this model run 15–30% annualized, depending on volume.

## What you should do with your legacy deposits

Your pre-v10 COLD deposit still holds XFG. You have not lost anything.

**Step 1:** Withdraw your principal.
```
withdraw_bond <deposit_id>
```
Your XFG returns to your wallet. No lock. No penalty. No fee.

**Step 2:** Burn it to mint HEAT.

HEAT is the flatcoin — pegged to $1.58 (the December 2008 dollar, CPI-adjusted). Minting destroys XFG and creates HEAT at the current oracle rate.

**Step 3:** Lock HEAT in a CD.

Choose a term from 1 to 72 epochs (~5 days to ~1 year). Your HEAT earns real yield from the swap fee pool every epoch. No dilution. No inflation. Just protocol revenue shared with savers.

## What you should not expect

The 80% figure from the original COLD system is fugazi tbh, but at the time was the best idea we had. It was a model funded by destroying the value of everyone else's tokens. As bad of an idea it is- it is a model not uncommon in crypto- dare i say the most common for deposit interest.. however it was an essential stepping stone of experience that led us on our way to building Fuego Reserve. 

The replacement is a real-yield system producing interest from actual protocol activity. As volume grows, rates rise. If the treasury backstops shortfalls, the floor holds. The system earns its yield. It does not print it.  It offers what the old system cannot offer which is why there really isnt anything we CAN redeem, moving into a system based on real yield & sound money assurance- this system is  built to earn a variable APY based on each epoch's activity, but real yield instead of the promise of token issuance. So i admit, i did say one thing and do another, but for this time I hope you would agree, we chose a better replacement.

## Why this is better

The old system asked you to trust an inflation schedule. The new system asks you to verify protocol revenue. You can see swap fees on-chain every epoch. You can calculate exactly what the pool earned and what your share is. Heat supply and burned XFG are always available as well. The math is auditable and economics are transparent. The more we make this so, the more we can lean into & benefit from the assurances of a sound money system.
