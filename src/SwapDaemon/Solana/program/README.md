# xfg_htlc — Solana HTLC program (build workspace)

Anchor program backing XFG↔SOL atomic swaps. The program source is one level
up (`../htlc_program.rs`); this directory only holds the build manifest and a
**pinned `Cargo.lock`** so the program builds reproducibly with the Solana SBF
toolchain.

The daemon's `SolRpcClient` talks to this program over Anchor's ABI (8-byte
instruction discriminators `sha256("global:<ix>")[..8]`, the `HtlcState`
account discriminator, and the PDA seeds below).

## What it does

`lock(hash_lock, timeout_slot, amount_lamports)` escrows SOL at a PDA, `claim(preimage)`
releases it iff `keccak256(preimage) == hash_lock`, `refund()` returns it to the
sender after `timeout_slot`.

- HTLC state PDA: `find_program_address([b"xfg_htlc", sender, hash_lock])`
- Vault PDA:      `find_program_address([b"xfg_htlc", htlc_pubkey])`
- **Hashlock = `keccak256(adaptor_secret)`** — the daemon derives this in
  `SwapHashLock::solHashLockHex`. (Committing the adaptor *point* `T = t·G`
  instead — the historical bug — makes every claim fail `InvalidPreimage`.)

## Why the dependency pins

`cargo-build-sbf` from Solana 1.18 bundles `rustc <= 1.79` (even with
`--tools-version v1.43`). Several 2025 crate releases adopted Cargo's unstable
`edition2024` or raised their MSRV to 1.85, which that rustc rejects. `Cargo.toml`
pins them back (and `Cargo.lock` captures the full resolved tree):

| Pin | Reason |
|-----|--------|
| `blake3 = 1.5.5` | 1.8 → `digest 0.11` → `block-buffer 0.12` (edition2024) |
| `proc-macro-crate = 3.2.0` | 3.5 → `toml_edit 0.25` → `toml_datetime 1.x` (edition2024) |
| `indexmap = 2.9.0` | 2.14 is edition2024 |
| `unicode-segmentation = 1.12.0` | 1.13 requires rustc 1.85 |

**Do not run `cargo update`.** Build with `--locked`.

## Build

```bash
cd src/SwapDaemon/Solana/program
cargo build-sbf --tools-version v1.43 --   # uses the pinned Cargo.lock
# -> target/deploy/xfg_htlc.so
```

## Deploy to a local validator

```bash
# 1. validator (macOS needs COPYFILE_DISABLE=1 to unpack genesis)
COPYFILE_DISABLE=1 solana-test-validator --reset --rpc-port 8899

# 2. point the CLI at it + fund the deploy payer
solana config set --url http://127.0.0.1:8899
solana airdrop 100

# 3. deploy. The deploy address MUST equal declare_id! in ../htlc_program.rs.
#    For your own deploy: generate a program keypair, set declare_id to its
#    pubkey (or `anchor keys sync`), then:
solana program deploy target/deploy/xfg_htlc.so --program-id <program-keypair.json>
```

`declare_id!` is currently set to a dev/localnet deployment
(`J4H9vUpp5CtJF9x4iPAMj7fqp5fpH9KTGcRzRC8e72ig`). The daemon's
`sol_program_id` config must match the deployed address.

> Program keypairs are intentionally **not** committed (`.gitignore`d). The
> committed `declare_id` is illustrative; supply your own keypair to deploy.
