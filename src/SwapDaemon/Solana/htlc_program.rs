// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
//
// Solana HTLC/PTLC program for XFG/SOL atomic swaps.
//
// In the adaptor-sig swap protocol:
//   - XFG side: Musig2 joint escrow (adaptor signatures)
//   - SOL side: HTLC with Keccak-256 hashlock derived from adaptor secret
//               OR pure PTLC with ed25519 adaptor verification on-chain
//
// Flow (Alice-locks, legacy HTLC — lock_type = 0):
//   1. Bob generates adaptor secret t, publishes H = Keccak256(t)
//   2. XFG funded to Musig2 escrow
//   3. Alice locks SOL in this HTLC with hashlock H and slot timeout
//   4. Bob claims SOL by revealing t (preimage) — on-chain secret reveal
//   5. Alice extracts t; Bob spends XFG escrow
//
// Flow (PURE PTLC, lock_type = 1 — plan P3, docs/PTLC_PURE_PLAN.md §5):
//   1. Bob publishes adaptor POINT T = t*G (no hash commitment anywhere)
//   2. Alice locks SOL with lock_ptlc(ptlc_point = T)
//   3. Bob calls claim_ptlc(adaptor_sig, presig_r, presig_s_prime) where
//      adaptor_sig = completed Fuego Signature (c || s), presig_r = nonce
//      point N = (k+t)*G, presig_s_prime = r_hat = k − e·x of the pre-sig
//   4. Program verifies N == s·G + e·P  (equivalently s·G == N − e·P) with
//      e = sc_reduce32(Keccak256(presig_r ‖ T ‖ claim_context)), recovers
//      the secret t = s − r_hat, and REQUIRES t·G == T before paying out.
//      The recovered t is stored in state.preimage so the counterparty can
//      adapt their XFG pre-signature off-chain (scriptless scripts).
//   5. Timeout refund remains available for BOTH lock types (funds safety:
//      a PTLC escrow whose adaptor never completes must stay refundable).
//
// Build/deploy: see program/README.md (uses cargo build-sbf with a pinned
// Cargo.lock to fit the Solana SBF toolchain's rust ceiling).
//
// declare_id must match the deployed program keypair (regen + rebuild for local e2e).
// Default illustrative localnet id; operators set sol_program_id to their deploy.
//
// BACKWARD COMPATIBILITY (account layout): `ptlc_point` and `lock_type` are
// appended AFTER `bump`, so offsets of every legacy field are unchanged and
// off-chain parsers that stop at `preimage` keep working. Fresh accounts are
// always created by this binary with lock_type = 0 (HTLC) unless lock_ptlc()
// was used, preserving legacy behavior. Accounts created by the PREVIOUS
// deployment (155 bytes, missing the trailing 33 bytes) will fail Anchor
// borsh deserialization under the new binary — settle those with the prior
// program deployment before upgrading.

use anchor_lang::prelude::*;
use anchor_lang::solana_program::keccak::hash as keccak256;
use curve25519_dalek::constants::ED25519_BASEPOINT_POINT;
use curve25519_dalek::edwards::{CompressedEdwardsY, EdwardsPoint};
use curve25519_dalek::scalar::Scalar;

declare_id!("J4H9vUpp5CtJF9x4iPAMj7fqp5fpH9KTGcRzRC8e72ig");

/// Seed prefix for HTLC vault PDA.
const HTLC_SEED: &[u8] = b"xfg_htlc";

/// Legacy hashlock escrow (keccak256(preimage) == hash_lock).
const LOCK_TYPE_HTLC: u8 = 0;
/// Pure PTLC point escrow (ed25519 adaptor verification in claim_ptlc).
const LOCK_TYPE_PTLC: u8 = 1;

#[program]
pub mod xfg_htlc {
    use super::*;

    /// Lock SOL into an HTLC escrow.
    ///
    /// * `hash_lock`      - SHA-256 of the adaptor secret (32 bytes)
    /// * `timeout_slot`   - Solana slot after which sender can refund
    /// * `amount_lamports`- SOL amount in lamports to lock
    pub fn lock(
        ctx: Context<Lock>,
        hash_lock: [u8; 32],
        timeout_slot: u64,
        amount_lamports: u64,
    ) -> Result<()> {
        let clock = Clock::get()?;
        require!(timeout_slot > clock.slot, HtlcError::TimeoutInPast);
        require!(amount_lamports > 0, HtlcError::ZeroAmount);

        // Transfer SOL from sender to vault PDA
        let ix = anchor_lang::solana_program::system_instruction::transfer(
            &ctx.accounts.sender.key(),
            &ctx.accounts.vault.key(),
            amount_lamports,
        );
        anchor_lang::solana_program::program::invoke(
            &ix,
            &[
                ctx.accounts.sender.to_account_info(),
                ctx.accounts.vault.to_account_info(),
                ctx.accounts.system_program.to_account_info(),
            ],
        )?;

        // Initialize the HTLC state
        let htlc = &mut ctx.accounts.htlc;
        htlc.sender = ctx.accounts.sender.key();
        htlc.recipient = ctx.accounts.recipient.key();
        htlc.amount = amount_lamports;
        htlc.hash_lock = hash_lock;
        htlc.timeout_slot = timeout_slot;
        htlc.claimed = false;
        htlc.refunded = false;
        htlc.preimage = [0u8; 32];
        htlc.bump = ctx.bumps.vault;
        htlc.ptlc_point = [0u8; 32]; // unused for HTLC locks
        htlc.lock_type = LOCK_TYPE_HTLC;

        emit!(Locked {
            htlc_id: htlc.key(),
            sender: htlc.sender,
            recipient: htlc.recipient,
            amount: amount_lamports,
            hash_lock,
            timeout_slot,
        });

        Ok(())
    }

    /// Lock SOL into a PURE PTLC escrow against the adaptor POINT T = t*G.
    /// No hash commitment exists anywhere on-chain (scriptless scripts).
    ///
    /// * `ptlc_point`     - ed25519 adaptor point T = t*G (32 bytes, compressed)
    /// * `timeout_slot`   - Solana slot after which sender can refund
    /// * `amount_lamports`- SOL amount in lamports to lock
    pub fn lock_ptlc(
        ctx: Context<LockPtlc>,
        ptlc_point: [u8; 32],
        timeout_slot: u64,
        amount_lamports: u64,
    ) -> Result<()> {
        let clock = Clock::get()?;
        require!(timeout_slot > clock.slot, HtlcError::TimeoutInPast);
        require!(amount_lamports > 0, HtlcError::ZeroAmount);

        // Reject identity / small-order / non-decodable points up front so a
        // malformed T can never be funded (it would be unclaimable).
        decode_ed25519_point(&ptlc_point)?;

        // Transfer SOL from sender to vault PDA
        let ix = anchor_lang::solana_program::system_instruction::transfer(
            &ctx.accounts.sender.key(),
            &ctx.accounts.vault.key(),
            amount_lamports,
        );
        anchor_lang::solana_program::program::invoke(
            &ix,
            &[
                ctx.accounts.sender.to_account_info(),
                ctx.accounts.vault.to_account_info(),
                ctx.accounts.system_program.to_account_info(),
            ],
        )?;

        let htlc = &mut ctx.accounts.htlc;
        htlc.sender = ctx.accounts.sender.key();
        htlc.recipient = ctx.accounts.recipient.key();
        htlc.amount = amount_lamports;
        htlc.hash_lock = [0u8; 32]; // unused for PTLC locks
        htlc.timeout_slot = timeout_slot;
        htlc.claimed = false;
        htlc.refunded = false;
        htlc.preimage = [0u8; 32];
        htlc.bump = ctx.bumps.vault;
        htlc.ptlc_point = ptlc_point;
        htlc.lock_type = LOCK_TYPE_PTLC;

        emit!(LockedPtlc {
            htlc_id: htlc.key(),
            sender: htlc.sender,
            recipient: htlc.recipient,
            amount: amount_lamports,
            ptlc_point,
            timeout_slot,
        });

        Ok(())
    }

    /// Claim locked SOL by revealing the preimage (adaptor secret t).
    ///
    /// Anyone can call this as long as preimage is valid, but SOL goes
    /// to the designated recipient. HTLC locks only (legacy fallback).
    pub fn claim(ctx: Context<Claim>, preimage: [u8; 32]) -> Result<()> {
        // Validate + mark claimed in a scope so the &mut htlc borrow ends
        // before the CPI below.
        let (amount, htlc_key, bump) = {
            let htlc = &mut ctx.accounts.htlc;
            require!(htlc.lock_type == LOCK_TYPE_HTLC, HtlcError::NotLegacyHtlc);
            require!(!htlc.claimed, HtlcError::AlreadyClaimed);
            require!(!htlc.refunded, HtlcError::AlreadyRefunded);

            // Verify Keccak-256(preimage) == hash_lock
            let computed = keccak256(&preimage);
            require!(computed.to_bytes() == htlc.hash_lock, HtlcError::InvalidPreimage);

            htlc.claimed = true;
            htlc.preimage = preimage;
            (htlc.amount, htlc.key(), htlc.bump)
        };

        // The vault is a system-owned PDA. A program may NOT directly debit an
        // account it does not own, so move the lamports via a system-program
        // transfer signed by the vault PDA seeds [b"xfg_htlc", htlc_key, bump].
        transfer_vault_to(
            &ctx.accounts.vault,
            &ctx.accounts.recipient,
            &ctx.accounts.system_program,
            amount,
            htlc_key,
            bump,
        )?;

        emit!(Claimed {
            htlc_id: htlc_key,
            preimage,
        });

        Ok(())
    }

    /// Claim a PURE PTLC escrow with an adapted (completed) ed25519 adaptor
    /// signature. Permissionless like claim(); SOL pays to the designated
    /// recipient.
    ///
    /// # Arguments
    /// * `adaptor_sig`     - completed Fuego Signature (bytes 0..32 = challenge
    ///                       c, carried for auditability; bytes 32..64 = the
    ///                       adapted response s = r_hat + t, which is what the
    ///                       equation below consumes)
    /// * `presig_r`        - adaptor nonce point N = (k+t)*G, compressed y
    /// * `presig_s_prime`  - pre-signature response r_hat = k − e·x
    ///
    /// # What is verified (exactly)
    /// 1. State is lock_type == 1 with a well-formed bound point T
    ///    (decodable, non-identity, non-small-order).
    /// 2. Challenge recomputation:
    ///       e = sc_reduce32(Keccak256(presig_r ‖ T ‖ claim_context))
    ///    with claim_context = Keccak256(htlc_pda ‖ recipient ‖ amount_le),
    ///    binding every claim to THIS escrow (anti-replay across HTLCs).
    /// 3. On-curve adaptor/Schnorr equation (curve25519 arithmetic via
    ///    curve25519-dalek, the same crate solana-program links):
    ///       N == s·G + e·P
    ///    Substituting s = r_hat + t this is exactly the full adaptor
    ///    relation N == r_hat·G + T + e·P, i.e. the pre-signature becomes a
    ///    valid signature precisely when the discrete log t of T is folded in.
    /// 4. Secret recovery + binding: t = s − r_hat (mod L) must be nonzero and
    ///    MUST satisfy t·G == T. This proves the submitted completion encodes
    ///    the real adaptor secret — the value stored on-chain is therefore
    ///    trustworthy for the counterparty's XFG-side adaptation.
    pub fn claim_ptlc(
        ctx: Context<ClaimPtlc>,
        adaptor_sig: [u8; 64],
        presig_r: [u8; 32],
        presig_s_prime: [u8; 32],
    ) -> Result<()> {
        let (amount, htlc_key, bump, secret) = {
            let htlc = &mut ctx.accounts.htlc;
            require!(htlc.lock_type == LOCK_TYPE_PTLC, HtlcError::NotPurePtlc);
            require!(!htlc.claimed, HtlcError::AlreadyClaimed);
            require!(!htlc.refunded, HtlcError::AlreadyRefunded);

            // 1. Bound adaptor point T — validated again here (defense in depth
            //    vs any future writer of HtlcState).
            let t_point = decode_ed25519_point(&htlc.ptlc_point)?;

            // Nonce point N from the pre-signature.
            let n_point = decode_ed25519_point(&presig_r)?;

            // 2. Challenge recomputation.
            let context =
                claim_context(&htlc.key(), &ctx.accounts.recipient.key(), htlc.amount);
            let mut challenge_buf = [0u8; 96];
            challenge_buf[..32].copy_from_slice(&presig_r);
            challenge_buf[32..64].copy_from_slice(&htlc.ptlc_point);
            challenge_buf[64..].copy_from_slice(&context);
            let e = Scalar::from_bytes_mod_order(keccak256(&challenge_buf).to_bytes());

            // 3. Completed response s (low-level scalar reduction mod L).
            let mut s_bytes = [0u8; 32];
            s_bytes.copy_from_slice(&adaptor_sig[32..]);
            let s = Scalar::from_bytes_mod_order(s_bytes);

            // N == s·G + e·P   (⇔ s·G == N − e·P)
            let lhs = n_point.compress();
            let rhs_point = &s * &ED25519_BASEPOINT_POINT + &e * &t_point;
            if lhs != rhs_point.compress() {
                return Err(error!(HtlcError::AdaptorVerifyFailed));
            }

            // 4. Recover the adaptor secret and bind it to the stored point.
            let r_hat = Scalar::from_bytes_mod_order(presig_s_prime);
            let t_scalar = s - r_hat;
            if t_scalar == Scalar::zero() {
                return Err(error!(HtlcError::SecretMismatch));
            }
            let recovered = (&t_scalar * &ED25519_BASEPOINT_POINT).compress();
            if recovered != t_point.compress() {
                return Err(error!(HtlcError::SecretMismatch));
            }

            htlc.claimed = true;
            htlc.preimage = t_scalar.to_bytes();
            (
                htlc.amount,
                htlc.key(),
                htlc.bump,
                t_scalar.to_bytes(),
            )
        };

        transfer_vault_to(
            &ctx.accounts.vault,
            &ctx.accounts.recipient,
            &ctx.accounts.system_program,
            amount,
            htlc_key,
            bump,
        )?;

        emit!(ClaimedPtlc {
            htlc_id: htlc_key,
            secret,
        });

        Ok(())
    }

    /// Refund locked SOL to the sender after timeout.
    ///
    /// Works for BOTH lock types: a PTLC escrow whose adaptor completion never
    /// arrives must remain refundable (funds safety), so NO lock_type guard.
    pub fn refund(ctx: Context<Refund>) -> Result<()> {
        let (amount, htlc_key, bump) = {
            let htlc = &mut ctx.accounts.htlc;
            require!(!htlc.claimed, HtlcError::AlreadyClaimed);
            require!(!htlc.refunded, HtlcError::AlreadyRefunded);

            let clock = Clock::get()?;
            require!(clock.slot >= htlc.timeout_slot, HtlcError::TimeoutNotReached);

            htlc.refunded = true;
            (htlc.amount, htlc.key(), htlc.bump)
        };

        // Vault is a system-owned PDA — refund via a signed system transfer
        // (see claim() for why direct lamport mutation is invalid).
        transfer_vault_to(
            &ctx.accounts.vault,
            &ctx.accounts.sender,
            &ctx.accounts.system_program,
            amount,
            htlc_key,
            bump,
        )?;

        emit!(Refunded {
            htlc_id: htlc_key,
        });

        Ok(())
    }
}

// ─── Curve helpers ────────────────────────────────────────────────────

/// Decode a compressed ed25519 point, rejecting the identity and all
/// small-order points (anything whose cofactor multiple is the identity).
/// Small-order inputs would otherwise allow degenerate equations to pass.
fn decode_ed25519_point(bytes: &[u8; 32]) -> Result<EdwardsPoint> {
    let point = CompressedEdwardsY(*bytes)
        .decompress()
        .ok_or_else(|| error!(HtlcError::MalformedCurvePoint))?;
    // Canonical identity encoding: sign byte 0x00, y = 1.
    let mut identity_bytes = [0u8; 32];
    identity_bytes[0] = 1;
    let cofactor_multiple = point.mul_by_cofactor().compress().to_bytes();
    if cofactor_multiple == identity_bytes {
        return Err(error!(HtlcError::MalformedCurvePoint));
    }
    Ok(point)
}

/// Claim context binding a completion to THIS escrow:
/// keccak256(htlc_pda ‖ recipient_pubkey ‖ amount_u64_le).
fn claim_context(htlc_key: &Pubkey, recipient: &Pubkey, amount: u64) -> [u8; 32] {
    let mut buf = [0u8; 72];
    buf[..32].copy_from_slice(htlc_key.as_ref());
    buf[32..64].copy_from_slice(recipient.as_ref());
    buf[64..].copy_from_slice(&amount.to_le_bytes());
    keccak256(&buf).to_bytes()
}

/// Move the vault PDA's lamports via a system-program CPI signed by the
/// vault PDA seeds [b"xfg_htlc", htlc_key, bump]. Shared by claim /
/// claim_ptlc / refund.
fn transfer_vault_to<'info>(
    vault: &SystemAccount<'info>,
    destination: &UncheckedAccount<'info>,
    system_program: &Program<'info, System>,
    amount: u64,
    htlc_key: Pubkey,
    bump: u8,
) -> Result<()> {
    let seeds: &[&[u8]] = &[HTLC_SEED, htlc_key.as_ref(), &[bump]];
    let signer: &[&[&[u8]]] = &[seeds];
    let ix = anchor_lang::solana_program::system_instruction::transfer(
        vault.key,
        destination.key,
        amount,
    );
    anchor_lang::solana_program::program::invoke_signed(
        &ix,
        &[
            vault.to_account_info(),
            destination.to_account_info(),
            system_program.to_account_info(),
        ],
        signer,
    )
    .map_err(Into::into)
}

// ─── Accounts ─────────────────────────────────────────────────────────

#[derive(Accounts)]
#[instruction(hash_lock: [u8; 32], timeout_slot: u64, amount_lamports: u64)]
pub struct Lock<'info> {
    #[account(mut)]
    pub sender: Signer<'info>,

    /// CHECK: Recipient pubkey stored in HTLC state, validated at claim time.
    pub recipient: UncheckedAccount<'info>,

    #[account(
        init,
        payer = sender,
        space = 8 + HtlcState::INIT_SPACE,
        seeds = [HTLC_SEED, sender.key().as_ref(), &hash_lock],
        bump,
    )]
    pub htlc: Account<'info, HtlcState>,

    /// CHECK: Vault PDA that holds the locked SOL.
    #[account(
        mut,
        seeds = [HTLC_SEED, htlc.key().as_ref()],
        bump,
    )]
    pub vault: SystemAccount<'info>,

    pub system_program: Program<'info, System>,
}

#[derive(Accounts)]
#[instruction(ptlc_point: [u8; 32], timeout_slot: u64, amount_lamports: u64)]
pub struct LockPtlc<'info> {
    #[account(mut)]
    pub sender: Signer<'info>,

    /// CHECK: Recipient pubkey stored in PTLC state, paid at claim time.
    pub recipient: UncheckedAccount<'info>,

    #[account(
        init,
        payer = sender,
        space = 8 + HtlcState::INIT_SPACE,
        seeds = [HTLC_SEED, sender.key().as_ref(), &ptlc_point],
        bump,
    )]
    pub htlc: Account<'info, HtlcState>,

    /// CHECK: Vault PDA that holds the locked SOL.
    #[account(
        mut,
        seeds = [HTLC_SEED, htlc.key().as_ref()],
        bump,
    )]
    pub vault: SystemAccount<'info>,

    pub system_program: Program<'info, System>,
}

#[derive(Accounts)]
pub struct Claim<'info> {
    #[account(
        mut,
        constraint = !htlc.claimed @ HtlcError::AlreadyClaimed,
        constraint = !htlc.refunded @ HtlcError::AlreadyRefunded,
    )]
    pub htlc: Account<'info, HtlcState>,

    /// CHECK: Must match htlc.recipient
    #[account(
        mut,
        constraint = recipient.key() == htlc.recipient @ HtlcError::WrongRecipient,
    )]
    pub recipient: UncheckedAccount<'info>,

    /// CHECK: Vault PDA holding the SOL.
    #[account(
        mut,
        seeds = [HTLC_SEED, htlc.key().as_ref()],
        bump = htlc.bump,
    )]
    pub vault: SystemAccount<'info>,

    pub system_program: Program<'info, System>,
}

#[derive(Accounts)]
pub struct ClaimPtlc<'info> {
    #[account(
        mut,
        constraint = !htlc.claimed @ HtlcError::AlreadyClaimed,
        constraint = !htlc.refunded @ HtlcError::AlreadyRefunded,
    )]
    pub htlc: Account<'info, HtlcState>,

    /// CHECK: Must match htlc.recipient
    #[account(
        mut,
        constraint = recipient.key() == htlc.recipient @ HtlcError::WrongRecipient,
    )]
    pub recipient: UncheckedAccount<'info>,

    /// CHECK: Vault PDA holding the SOL.
    #[account(
        mut,
        seeds = [HTLC_SEED, htlc.key().as_ref()],
        bump = htlc.bump,
    )]
    pub vault: SystemAccount<'info>,

    pub system_program: Program<'info, System>,
}

#[derive(Accounts)]
pub struct Refund<'info> {
    #[account(
        mut,
        constraint = !htlc.claimed @ HtlcError::AlreadyClaimed,
        constraint = !htlc.refunded @ HtlcError::AlreadyRefunded,
    )]
    pub htlc: Account<'info, HtlcState>,

    /// CHECK: Must match htlc.sender
    #[account(
        mut,
        constraint = sender.key() == htlc.sender @ HtlcError::WrongSender,
    )]
    pub sender: UncheckedAccount<'info>,

    /// CHECK: Vault PDA holding the SOL.
    #[account(
        mut,
        seeds = [HTLC_SEED, htlc.key().as_ref()],
        bump = htlc.bump,
    )]
    pub vault: SystemAccount<'info>,

    pub system_program: Program<'info, System>,
}

// ─── State ────────────────────────────────────────────────────────────

#[account]
#[derive(InitSpace)]
pub struct HtlcState {
    pub sender: Pubkey,          // 32
    pub recipient: Pubkey,       // 32
    pub amount: u64,             // 8
    pub hash_lock: [u8; 32],     // 32  Keccak-256(adaptor_secret) — HTLC only
    pub timeout_slot: u64,       // 8   Solana slot for refund eligibility
    pub claimed: bool,           // 1
    pub refunded: bool,          // 1
    pub preimage: [u8; 32],      // 32  HTLC: revealed preimage. PTLC: recovered
                                 //     secret t = s − r_hat (set on claim)
    pub bump: u8,                // 1   Vault PDA bump
    pub ptlc_point: [u8; 32],    // 32  NEW (P3): adaptor point T = t*G, zeros for HTLC
    pub lock_type: u8,           // 1   NEW (P3): 0 = HTLC (legacy default), 1 = PTLC
}
// Total: 32+32+8+32+8+1+1+32+1+32+1 = 180 bytes + 8 discriminator = 188
// NOTE: new fields are appended AFTER `bump`; legacy-field offsets unchanged.

// ─── Events ───────────────────────────────────────────────────────────

#[event]
pub struct Locked {
    pub htlc_id: Pubkey,
    pub sender: Pubkey,
    pub recipient: Pubkey,
    pub amount: u64,
    pub hash_lock: [u8; 32],
    pub timeout_slot: u64,
}

#[event]
pub struct LockedPtlc {
    pub htlc_id: Pubkey,
    pub sender: Pubkey,
    pub recipient: Pubkey,
    pub amount: u64,
    pub ptlc_point: [u8; 32],
    pub timeout_slot: u64,
}

#[event]
pub struct Claimed {
    pub htlc_id: Pubkey,
    pub preimage: [u8; 32],
}

#[event]
pub struct ClaimedPtlc {
    pub htlc_id: Pubkey,
    pub secret: [u8; 32], // adaptor secret t recovered from the completion
}

#[event]
pub struct Refunded {
    pub htlc_id: Pubkey,
}

// ─── Errors ───────────────────────────────────────────────────────────

#[error_code]
pub enum HtlcError {
    #[msg("Timeout slot must be in the future")]
    TimeoutInPast,
    #[msg("Amount must be greater than zero")]
    ZeroAmount,
    #[msg("HTLC already claimed")]
    AlreadyClaimed,
    #[msg("HTLC already refunded")]
    AlreadyRefunded,
    #[msg("Keccak-256(preimage) does not match hash lock")]
    InvalidPreimage,
    #[msg("Timeout slot not yet reached")]
    TimeoutNotReached,
    #[msg("Recipient does not match HTLC state")]
    WrongRecipient,
    #[msg("Sender does not match HTLC state")]
    WrongSender,
    #[msg("This lock is not a legacy HTLC escrow (lock_type != 0)")]
    NotLegacyHtlc,
    #[msg("This lock is not a pure PTLC escrow (lock_type != 1)")]
    NotPurePtlc,
    #[msg("Curve point is malformed, identity, or small-order")]
    MalformedCurvePoint,
    #[msg("Adaptor equation N == s*G + e*P failed")]
    AdaptorVerifyFailed,
    #[msg("Recovered adaptor secret does not satisfy t*G == ptlc_point")]
    SecretMismatch,
}
