// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

//! C interface to `sigma_fun`'s proof that a secp256k1 point and an ed25519
//! point share one 252-bit discrete logarithm (CROSS_CURVE_PLAN X1).
//!
//! The proof commits to each bit as `r*G + bit*2^i*H`. It is sound only if
//! nobody knows the discrete logarithm of `H` to `G`, so both `H` are derived
//! here by hashing a fixed label to the curve.

#![allow(non_snake_case)]

use core::slice;
use curve25519_dalek::edwards::{CompressedEdwardsY, EdwardsPoint};
use curve25519_dalek::scalar::Scalar as EdScalar;
use rand_chacha::ChaCha20Rng;
use rand_core::OsRng;
use curve25519_dalek::traits::IsIdentity;
use sha2::{Digest, Sha256};
use sigma_fun::ext::dl_secp256k1_ed25519_eq::{CrossCurveDLEQ, CrossCurveDLEQProof};
use sigma_fun::secp256k1::fun::{marker::*, Point};
use sigma_fun::HashTranscript;
use std::panic::catch_unwind;
use std::sync::OnceLock;

pub type ProofSystem = CrossCurveDLEQ<HashTranscript<Sha256, ChaCha20Rng>>;

const SECP_GENERATOR_LABEL: &[u8] = b"Fuego/cross-curve-dleq/H/secp256k1";
const ED_GENERATOR_LABEL: &[u8] = b"Fuego/cross-curve-dleq/H/ed25519";

/// Upper bound on an encoded proof; the encoding has one size (see the tests).
pub const MAX_PROOF_SIZE: usize = 60_000;

const OK: i32 = 1;
const REFUSED: i32 = 0;

pub fn secp_commitment_generator() -> Point {
    (0u32..)
        .find_map(|counter| {
            let mut hasher = Sha256::new();
            hasher.update(SECP_GENERATOR_LABEL);
            hasher.update(counter.to_be_bytes());
            let x: [u8; 32] = hasher.finalize().into();
            Point::<EvenY>::from_xonly_bytes(x)
        })
        .expect("a hash lands on the curve within a few attempts")
        .normalize()
}

pub fn ed_commitment_generator() -> EdwardsPoint {
    (0u32..)
        .find_map(|counter| {
            let mut hasher = Sha256::new();
            hasher.update(ED_GENERATOR_LABEL);
            hasher.update(counter.to_be_bytes());
            let encoding: [u8; 32] = hasher.finalize().into();
            let point = CompressedEdwardsY(encoding).decompress()?.mul_by_cofactor();
            (!point.is_identity()).then_some(point)
        })
        .expect("a hash decodes to a point within a few attempts")
}

fn proof_system() -> &'static ProofSystem {
    static SYSTEM: OnceLock<ProofSystem> = OnceLock::new();
    SYSTEM.get_or_init(|| ProofSystem::new(secp_commitment_generator(), ed_commitment_generator()))
}

fn bincode_config() -> impl bincode::config::Config {
    bincode::config::standard().with_fixed_int_encoding().with_limit::<MAX_PROOF_SIZE>()
}

pub fn encode(proof: &CrossCurveDLEQProof) -> Option<Vec<u8>> {
    bincode::encode_to_vec(bincode::serde::Compat(proof), bincode_config()).ok()
}

/// Decodes a proof that fills `bytes` exactly.
pub fn decode(bytes: &[u8]) -> Option<CrossCurveDLEQProof> {
    let (proof, read): (bincode::serde::Compat<CrossCurveDLEQProof>, usize) =
        bincode::decode_from_slice(bytes, bincode_config()).ok()?;
    (read == bytes.len()).then_some(proof.0)
}

/// Proves for the little-endian scalar `secret` (nonzero, below 2^252) and
/// returns the proof with the ed25519 and secp256k1 points it speaks about.
pub fn prove(secret: [u8; 32]) -> Option<(Vec<u8>, [u8; 32], [u8; 33])> {
    if secret[31] & 0xf0 != 0 || secret == [0u8; 32] {
        return None;
    }
    let scalar = EdScalar::from_canonical_bytes(secret)?;
    let (proof, (secp_point, ed_point)) = proof_system().prove(&scalar, &mut OsRng);
    Some((encode(&proof)?, ed_point.compress().to_bytes(), secp_point.to_bytes()))
}

pub fn verify(proof: &[u8], ed_point: [u8; 32], secp_point: [u8; 33]) -> bool {
    let (Some(proof), Some(ed), Some(secp)) = (
        decode(proof),
        CompressedEdwardsY(ed_point).decompress(),
        Point::<Normal, Public, NonZero>::from_bytes(secp_point),
    ) else {
        return false;
    };
    // Only the canonical encoding of the point is the statement.
    ed.compress().to_bytes() == ed_point && proof_system().verify(&proof, (secp, ed))
}

#[no_mangle]
pub extern "C" fn fuego_cross_curve_dleq_max_proof_size() -> usize {
    MAX_PROOF_SIZE
}

/// Writes the proof (at most `proof_capacity` bytes) and its two points.
/// Returns 1 on success and 0 otherwise.
///
/// # Safety
/// `secret` is 32 readable bytes, `proof_out` is `proof_capacity` writable
/// bytes, `ed_point_out` 32 and `secp_point_out` 33 writable bytes.
#[no_mangle]
pub unsafe extern "C" fn fuego_cross_curve_dleq_prove(
    secret: *const u8,
    proof_out: *mut u8,
    proof_capacity: usize,
    proof_size_out: *mut usize,
    ed_point_out: *mut u8,
    secp_point_out: *mut u8,
) -> i32 {
    if secret.is_null()
        || proof_out.is_null()
        || proof_size_out.is_null()
        || ed_point_out.is_null()
        || secp_point_out.is_null()
    {
        return REFUSED;
    }
    let mut secret_bytes = [0u8; 32];
    secret_bytes.copy_from_slice(slice::from_raw_parts(secret, 32));
    let proved = catch_unwind(|| prove(secret_bytes));
    secret_bytes.fill(0);
    match proved {
        Ok(Some((proof, ed_point, secp_point))) if proof.len() <= proof_capacity => {
            slice::from_raw_parts_mut(proof_out, proof.len()).copy_from_slice(&proof);
            *proof_size_out = proof.len();
            slice::from_raw_parts_mut(ed_point_out, 32).copy_from_slice(&ed_point);
            slice::from_raw_parts_mut(secp_point_out, 33).copy_from_slice(&secp_point);
            OK
        }
        _ => REFUSED,
    }
}

/// Returns 1 when `proof` shows the two points share one 252-bit discrete
/// logarithm, 0 otherwise (including for any malformed input).
///
/// # Safety
/// `proof` is `proof_size` readable bytes, `ed_point` 32 and `secp_point` 33.
#[no_mangle]
pub unsafe extern "C" fn fuego_cross_curve_dleq_verify(
    proof: *const u8,
    proof_size: usize,
    ed_point: *const u8,
    secp_point: *const u8,
) -> i32 {
    if proof.is_null() || ed_point.is_null() || secp_point.is_null() || proof_size > MAX_PROOF_SIZE {
        return REFUSED;
    }
    let proof = slice::from_raw_parts(proof, proof_size).to_vec();
    let mut ed = [0u8; 32];
    ed.copy_from_slice(slice::from_raw_parts(ed_point, 32));
    let mut secp = [0u8; 33];
    secp.copy_from_slice(slice::from_raw_parts(secp_point, 33));
    match catch_unwind(move || verify(&proof, ed, secp)) {
        Ok(true) => OK,
        _ => REFUSED,
    }
}
