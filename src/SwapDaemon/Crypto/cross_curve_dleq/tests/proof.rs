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

#![allow(non_snake_case)]

use curve25519_dalek::constants::{ED25519_BASEPOINT_POINT, ED25519_BASEPOINT_TABLE};
use curve25519_dalek::edwards::EdwardsPoint;
use curve25519_dalek::scalar::Scalar as EdScalar;
use curve25519_dalek::traits::Identity;
use fuego_cross_curve_dleq::{
    decode, ed_commitment_generator, encode, prove, secp_commitment_generator, verify, ProofSystem, MAX_PROOF_SIZE,
};
use rand_chacha::ChaCha20Rng;
use rand_core::{OsRng, RngCore};
use sha2::Sha256;
use sigma_fun::ext::dl_secp256k1_ed25519_eq::{CoreProof, CrossCurveDLEQProof};
use sigma_fun::secp256k1::fun::{g, marker::*, s, Point, Scalar, G};
use sigma_fun::{Either, FiatShamir, HashTranscript};

type Transcript = HashTranscript<Sha256, ChaCha20Rng>;

fn random_secret() -> [u8; 32] {
    let mut secret = [0u8; 32];
    OsRng.fill_bytes(&mut secret);
    secret[31] &= 0x0f;
    secret
}

#[test]
fn a_proof_verifies_for_its_points_only() {
    let (proof, ed, secp) = prove(random_secret()).unwrap();
    assert!(verify(&proof, ed, secp));
    let (_, other_ed, other_secp) = prove(random_secret()).unwrap();
    assert!(!verify(&proof, other_ed, secp));
    assert!(!verify(&proof, ed, other_secp));
}

#[test]
fn the_points_are_the_secret_times_each_base_point() {
    let secret = random_secret();
    let (_, ed, secp) = prove(secret).unwrap();
    let scalar = EdScalar::from_canonical_bytes(secret).unwrap();
    assert_eq!((&scalar * &ED25519_BASEPOINT_TABLE).compress().to_bytes(), ed);
    let mut big_endian = secret;
    big_endian.reverse();
    let secp_scalar = Scalar::<Secret, Zero>::from_bytes(big_endian).unwrap().non_zero().unwrap();
    assert_eq!(g!(secp_scalar * G).normalize().to_bytes(), secp);
}

#[test]
fn secrets_outside_the_range_are_refused() {
    assert!(prove([0u8; 32]).is_none());
    let mut two_to_252 = [0u8; 32];
    two_to_252[31] = 0x10;
    assert!(prove(two_to_252).is_none());
}

#[test]
fn the_encoding_has_one_size_and_no_slack() {
    let (proof, ed, secp) = prove(random_secret()).unwrap();
    let (other, _, _) = prove(random_secret()).unwrap();
    assert_eq!(proof.len(), other.len());
    assert!(proof.len() <= MAX_PROOF_SIZE);
    assert_eq!(encode(&decode(&proof).unwrap()).unwrap(), proof);

    let mut longer = proof.clone();
    longer.push(0);
    assert!(!verify(&longer, ed, secp));
    assert!(!verify(&proof[..proof.len() - 1], ed, secp));
    assert!(!verify(&[], ed, secp));
}

#[test]
fn every_changed_byte_is_refused() {
    let (proof, ed, secp) = prove(random_secret()).unwrap();
    for position in (0..proof.len()).step_by(97) {
        let mut changed = proof.clone();
        changed[position] ^= 1;
        assert!(!verify(&changed, ed, secp), "byte {position}");
    }
}

#[test]
fn a_point_with_a_torsion_component_is_refused() {
    let (proof, ed, secp) = prove(random_secret()).unwrap();
    let order_two = EdwardsPoint::identity().compress().to_bytes().map(|byte| byte ^ 0);
    let mut minus_one = [0xffu8; 32];
    minus_one[0] = 0xec;
    minus_one[31] = 0x7f;
    let torsion = curve25519_dalek::edwards::CompressedEdwardsY(minus_one).decompress().unwrap();
    assert!(!torsion.is_torsion_free() && order_two.len() == 32);
    let shifted = curve25519_dalek::edwards::CompressedEdwardsY(ed).decompress().unwrap() + torsion;
    assert!(!verify(&proof, shifted.compress().to_bytes(), secp));
}

#[test]
fn the_commitment_generators_are_hashed_points_of_prime_order() {
    let ed = ed_commitment_generator();
    assert!(ed.is_torsion_free() && ed != ED25519_BASEPOINT_POINT && ed != EdwardsPoint::identity());
    assert_ne!(secp_commitment_generator(), G.normalize());
}

/// A proof for two points with different discrete logarithms, built from the
/// knowledge that `H = h*G` on each curve. Every bit commitment is `r*G`,
/// which also opens as a commitment to any other value.
fn forged_with_known_generator_logs(
    system_generators: (Point, EdwardsPoint),
    xP: Scalar,
    xQ: EdScalar,
    hP: Scalar,
    hQ: EdScalar,
) -> (CrossCurveDLEQProof, (Point, EdwardsPoint)) {
    let (HP, HQ) = system_generators;
    let claim = (g!(xP * G).normalize(), &xQ * &ED25519_BASEPOINT_TABLE);
    let blindings: Vec<(Scalar, EdScalar)> =
        (0..252).map(|_| (Scalar::random(&mut OsRng), EdScalar::random(&mut OsRng))).collect();
    let commitments: Vec<(Point, EdwardsPoint)> =
        blindings.iter().map(|(rP, rQ)| (g!(rP * G).normalize(), rQ * &ED25519_BASEPOINT_TABLE)).collect();
    let (sumP, sumQ) = blindings
        .iter()
        .fold((Scalar::<Secret, Zero>::zero(), EdScalar::zero()), |(aP, aQ), (rP, rQ)| (s!(aP + rP), aQ + rQ));
    // sum(C) - r*G must equal x*H = (x*h)*G, so publish r = sum - x*h.
    let published = (s!(sumP - xP * hP).public(), sumQ - xQ * hQ);

    let mut powers = vec![(HP, HQ)];
    for i in 1..252 {
        let (previousP, previousQ) = powers[i - 1];
        powers.push((g!(previousP + previousP).normalize().non_zero().unwrap(), previousQ + previousQ));
    }
    let bit_statements: Vec<_> = powers
        .iter()
        .zip(&commitments)
        .map(|((H2P, H2Q), (CP, CQ))| ((*CP, *CQ), (g!(CP - H2P).normalize().non_zero().unwrap(), CQ - H2Q)))
        .collect();
    let unblindedP = g!(xP * HP).normalize();
    let unblindedQ = xQ * HQ;
    let statement = (bit_statements, ((claim.0, (HP, unblindedP)), (claim.1, (HQ, unblindedQ))));
    let witness = (blindings.into_iter().map(Either::Left).collect(), (xP, xQ));
    let proof = FiatShamir::<CoreProof, Transcript>::default().prove(&witness, &statement, Some(&mut OsRng));
    (CrossCurveDLEQProof { sum_blindings: published, commitments, proof }, claim)
}

#[test]
fn a_generator_with_a_known_logarithm_admits_forgery() {
    // With H = G (h = 1), as when the curves' base points are passed as the
    // commitment generators, unequal logarithms verify.
    let system = ProofSystem::new(G.normalize(), ED25519_BASEPOINT_POINT);
    let (xP, xQ) = (Scalar::random(&mut OsRng), EdScalar::random(&mut OsRng));
    let (forged, claim) = forged_with_known_generator_logs(
        (G.normalize(), ED25519_BASEPOINT_POINT),
        xP,
        xQ,
        Scalar::one(),
        EdScalar::one(),
    );
    assert!(system.verify(&forged, claim), "H = G is unsound");
}

#[test]
fn the_same_forgery_fails_against_the_hashed_generators() {
    let (HP, HQ) = (secp_commitment_generator(), ed_commitment_generator());
    let (xP, xQ) = (Scalar::random(&mut OsRng), EdScalar::random(&mut OsRng));
    let (forged, claim) =
        forged_with_known_generator_logs((HP, HQ), xP, xQ, Scalar::one(), EdScalar::one());
    assert!(!ProofSystem::new(HP, HQ).verify(&forged, claim));
    let encoded = encode(&forged).unwrap();
    assert!(!verify(&encoded, claim.1.compress().to_bytes(), claim.0.to_bytes()));
}
