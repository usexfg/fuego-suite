// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
//
// Solana HTLC program for XFG/SOL atomic swaps.
//
// In the adaptor-sig swap protocol:
//   - XFG side: Musig2 joint escrow (adaptor signatures)
//   - SOL side: HTLC with Keccak-256 hashlock derived from adaptor secret
//
// Flow:
//   1. Bob generates adaptor secret t, computes H = Keccak256(t)
//   2. Alice funds XFG → Musig2 escrow
//   3. Bob locks SOL in this HTLC with hashlock H and slot timeout
//   4. Alice claims SOL by revealing t (preimage)
//   5. Bob sees t on-chain, adapts XFG pre-sig, spends escrow
//
// Build:  anchor build
// Deploy: anchor deploy --provider.cluster devnet
//
// Program ID is set after first deploy — update declare_id!() below.

use anchor_lang::prelude::*;
use anchor_lang::solana_program::keccak::hash as keccak256;

declare_id!("FUEGhtLc1111111111111111111111111111111111");

/// Seed prefix for HTLC vault PDA.
const HTLC_SEED: &[u8] = b"xfg_htlc";

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

    /// Claim locked SOL by revealing the preimage (adaptor secret t).
    ///
    /// Anyone can call this as long as preimage is valid, but SOL goes
    /// to the designated recipient.
    pub fn claim(ctx: Context<Claim>, preimage: [u8; 32]) -> Result<()> {
        let htlc = &mut ctx.accounts.htlc;
        require!(!htlc.claimed, HtlcError::AlreadyClaimed);
        require!(!htlc.refunded, HtlcError::AlreadyRefunded);

        // Verify Keccak-256(preimage) == hash_lock
        let computed = keccak256(&preimage);
        require!(computed.to_bytes() == htlc.hash_lock, HtlcError::InvalidPreimage);

        htlc.claimed = true;
        htlc.preimage = preimage;

        // Transfer SOL from vault PDA to recipient
        let amount = htlc.amount;
        let htlc_key = htlc.key();
        let bump = htlc.bump;
        let seeds = &[HTLC_SEED, htlc_key.as_ref(), &[bump]];
        let signer_seeds = &[&seeds[..]];

        **ctx.accounts.vault.try_borrow_mut_lamports()? -= amount;
        **ctx.accounts.recipient.try_borrow_mut_lamports()? += amount;

        emit!(Claimed {
            htlc_id: htlc.key(),
            preimage,
        });

        Ok(())
    }

    /// Refund locked SOL to the sender after timeout.
    pub fn refund(ctx: Context<Refund>) -> Result<()> {
        let htlc = &mut ctx.accounts.htlc;
        require!(!htlc.claimed, HtlcError::AlreadyClaimed);
        require!(!htlc.refunded, HtlcError::AlreadyRefunded);

        let clock = Clock::get()?;
        require!(clock.slot >= htlc.timeout_slot, HtlcError::TimeoutNotReached);

        htlc.refunded = true;

        // Transfer SOL from vault PDA back to sender
        let amount = htlc.amount;
        **ctx.accounts.vault.try_borrow_mut_lamports()? -= amount;
        **ctx.accounts.sender.try_borrow_mut_lamports()? += amount;

        emit!(Refunded {
            htlc_id: htlc.key(),
        });

        Ok(())
    }
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
}

// ─── State ────────────────────────────────────────────────────────────

#[account]
#[derive(InitSpace)]
pub struct HtlcState {
    pub sender: Pubkey,          // 32
    pub recipient: Pubkey,       // 32
    pub amount: u64,             // 8
    pub hash_lock: [u8; 32],     // 32  Keccak-256(adaptor_secret)
    pub timeout_slot: u64,       // 8   Solana slot for refund eligibility
    pub claimed: bool,           // 1
    pub refunded: bool,          // 1
    pub preimage: [u8; 32],      // 32  Set on claim (adaptor secret t)
    pub bump: u8,                // 1   Vault PDA bump
}
// Total: 32+32+8+32+8+1+1+32+1 = 147 bytes + 8 discriminator = 155

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
pub struct Claimed {
    pub htlc_id: Pubkey,
    pub preimage: [u8; 32],
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
}
