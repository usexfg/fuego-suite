# Legacy Bond Migration Guide — Fuego v1.10.00+

If you hold a **pre-v10 multisig (COLD) deposit**, you can migrate it to a **Legacy Bond**
earning yield from the protocol's swap fee pool.

---

## What a Legacy Bond Is

- Your locked XFG is registered as a protocol bond earning **50% of the CD yield pool**
- Target APY: ~50% (variable, depends on swap fee volume on Hearth)
- Lock period: 72 epochs (~360 days at 5 days/epoch)
- At maturity: you withdraw **principal + accrued interest** in a single transaction
- **Withdrawal fee: 0 XFG** (the protocol covers the network fee)
- Future (v11): optional quarterly staged withdrawals (principal + interest, 1% service fee)

---

## Step 1: Check Your Deposits

Open the Fuego wallet and run:

```
deposits
```

This lists all your deposits. Look for deposits marked as **COLD** (type `0xCD`) created
before the v10 migration. You only need to migrate deposits of this type.

```
ID  Amount         Type  Locked  Unlock Height  Status
42  1,234.56 XFG   COLD  Yes     32000          Active
```

Take note of the deposit **ID** you want to migrate.

---

## Step 2: Migrate to Legacy Bond

```
migrate_deposit <deposit_id>
```

Example:
```
migrate_deposit 42
```

The wallet will show:
```
=== Legacy Bond Migration ===
Deposit ID:             42
Amount:                 1,234.56 XFG
Original TX:            a1b2c3d4...
Creation Height:        28000

Legacy Bond Terms:
  Lock Period:          72 epochs (~360 days)
  CD Share:             50% of CD yield pool
  Target APY:           ~50%
  Withdrawal Fee:       Free

Cost: 0.01 XFG (network fee)
Confirm? (1) OK  (2) No
```

Type `1` and press Enter to confirm.

**What happens:**
- Your deposit is tagged as a Legacy Bond (0xCB on-chain marker)
- Your XFG stays locked — **you keep full control of the principal**
- Interest begins accruing from the swap fee pool immediately
- You can withdraw at any time after 72 epochs

---

## Step 3: Wait for Maturity

Your bond matures after 72 epochs (~360 days).

During this time:
- Swap fees on Hearth generate interest in the bond yield pool
- Your share is proportional to your bond amount vs total bond pool
- Interest is visible via the node's fee rate tracker

You can check your bond anytime:
```
deposits           # look for LEGACY_BOND type
show_deposit 42   # detailed view
```

---

## Step 4: Withdraw (at or after maturity)

```
withdraw_bond <deposit_id>
```

Example:
```
withdraw_bond 42
```

The wallet shows your accrued interest:
```
=== Legacy Bond Withdrawal ===
Principal:      1,234.56 XFG
Interest:       432.10 XFG
Total:          1,666.66 XFG
Fee:            0 XFG (protocol covers)

Confirm withdrawal? (1) OK  (2) No
```

Type `1` to withdraw principal + interest to your wallet.

---

## FAQ

**Can I withdraw early?**
Not yet. Your principal is locked for 72 epochs. A quarterly staged
withdrawal option (4 × 25% principal, 1% service fee on interest) is planned
for v11.

**What happens if swap volume is low?**
Interest is variable. Low swap volume = lower yield. The 50% CD share split
ensures you earn proportional to protocol activity.

**Is my principal safe?**
Yes. The bond does NOT transfer your XFG — it adds a metadata tag to your
existing deposit. You retain full custody. The protocol only routes swap fees
to the yield pool; your principal never leaves your control.

**What if I already migrated to L2 (0xCE tag)?**
Use the existing L2 claim path. `migrate_deposit` will reject 0xCE-tagged
deposits with an appropriate error message.

**What happens after all legacy bonds mature?**
The legacy bond yield pool will be repurposed as a community tanda fund (v11+).

---

## Commands Reference

| Command | When | What it does |
|---|---|---|
| `deposits` | Any time | Lists all your deposits and their types |
| `migrate_deposit <id>` | Once per deposit | Registers a COLD deposit as a Legacy Bond |
| `withdraw_bond <id>` | After 72 epochs | Withdraws principal + accrued interest |
| `show_deposit <id>` | Any time | Shows detailed deposit information including bond status |

---

## Timeline

| Milestone | Block Height | What changes |
|---|---|---|
| v10 (now) | 445,000+ | Bond migration + fee routing to YEM Reserve |
| v11 | 1,111,111 | Staged quarterly withdrawals, coinbase coupon payouts, full YEM engine |
