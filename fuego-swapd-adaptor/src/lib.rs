//! Fuego Adaptor Signature Protocol — Schnorr adaptor signatures for
//! trustless cross-chain atomic swaps on secp256k1.
#![allow(non_snake_case)]

use num_bigint::BigUint;
use num_traits::Zero;
use secp256k1::{
    hashes::{sha256, Hash as _, HashEngine as _},
    rand, schnorr, Keypair, Message, PublicKey, Scalar, Secp256k1, SecretKey,
};

// ─── Curve constants ─────────────────────────────────────────────────────────

const SECP256K1_ORDER: &str = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141";

fn curve_order() -> BigUint {
    BigUint::parse_bytes(SECP256K1_ORDER.as_bytes(), 16).unwrap()
}

fn scalar_to_biguint(s: &Scalar) -> BigUint {
    BigUint::from_bytes_be(&s.to_be_bytes())
}

fn biguint_to_scalar(n: &BigUint) -> Scalar {
    let mut bytes = [0u8; 32];
    let be_bytes = n.to_bytes_be();
    let offset = 32usize.saturating_sub(be_bytes.len());
    bytes[offset..].copy_from_slice(&be_bytes);
    Scalar::from_be_bytes(bytes).unwrap()
}

fn scalar_negate(s: &Scalar) -> Scalar {
    let order = curve_order();
    let val = scalar_to_biguint(s);
    if val.is_zero() { *s } else { biguint_to_scalar(&(order - val)) }
}

fn scalar_add(a: &Scalar, b: &Scalar) -> Scalar {
    let order = curve_order();
    let a_val = scalar_to_biguint(a);
    let b_val = scalar_to_biguint(b);
    biguint_to_scalar(&((a_val + b_val) % order))
}

// ─── Public types ────────────────────────────────────────────────────────────

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AdaptorSecret(pub [u8; 32]);

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AdaptorPubkey(PublicKey);

#[derive(Clone, Debug)]
pub struct NonceCommitment { pub R: PublicKey }

#[derive(Clone, Debug)]
pub struct AdaptorPresig {
    pub R: PublicKey,
    pub s_prime: [u8; 32],
}

// ─── AdaptorSecret ───────────────────────────────────────────────────────────

impl AdaptorSecret {
    pub fn random(rng: &mut impl rand::Rng) -> Self {
        let mut bytes = [0u8; 32];
        rng.fill_bytes(&mut bytes);
        AdaptorSecret(bytes)
    }

    pub fn to_scalar(&self) -> Option<Scalar> {
        Scalar::from_be_bytes(self.0).ok()
    }

    pub fn to_pubkey(&self, secp: &Secp256k1<impl secp256k1::Signing>) -> Option<AdaptorPubkey> {
        let kp = Keypair::from_seckey_slice(secp, &self.0).ok()?;
        Some(AdaptorPubkey(kp.public_key()))
    }
}

// ─── AdaptorPubkey ───────────────────────────────────────────────────────────

impl AdaptorPubkey {
    pub fn to_pubkey(&self) -> PublicKey { self.0 }
    pub fn from_pubkey(pk: PublicKey) -> Self { AdaptorPubkey(pk) }
}

// ─── Nonce operations ────────────────────────────────────────────────────────

impl NonceCommitment {
    pub fn combine(&self, other: &NonceCommitment) -> Result<NonceCommitment, secp256k1::Error> {
        Ok(NonceCommitment { R: self.R.combine(&other.R)? })
    }
}

pub fn generate_nonce(rng: &mut impl rand::Rng) -> (SecretKey, NonceCommitment) {
    let secp = Secp256k1::new();
    let kp = Keypair::new(&secp, rng);
    let k = SecretKey::from_keypair(&kp);
    (k, NonceCommitment { R: kp.public_key() })
}

// ─── Challenge (standard Schnorr, uses compressed pubkeys) ───────────────────

pub fn compute_challenge(R: &PublicKey, P: &PublicKey, msg: &Message) -> Scalar {
    let mut engine = sha256::HashEngine::default();

    // Use tagged hash for domain separation
    let tag = sha256::Hash::hash(b"Fuego/adaptor_challenge");
    engine.input(&tag[..]);
    engine.input(&tag[..]);

    engine.input(&R.serialize());
    engine.input(&P.serialize());
    engine.input(&msg[..]);

    let hash = sha256::Hash::from_engine(engine);
    Scalar::from_be_bytes(hash.to_byte_array()).expect("challenge")
}

// ─── Adaptor signature ───────────────────────────────────────────────────────

/// Create adaptor presig: `s' = k + e * sk + t`.
pub fn adaptor_sign(
    sk: &SecretKey,
    k: &SecretKey,
    t: &AdaptorSecret,
    msg: &Message,
) -> Option<AdaptorPresig> {
    let secp = Secp256k1::new();
    let kp = Keypair::from_secret_key(&secp, sk);
    let P = kp.public_key();

    let nonce_kp = Keypair::from_secret_key(&secp, k);
    let R = nonce_kp.public_key();

    let e = compute_challenge(&R, &P, msg);
    let t_scalar = t.to_scalar()?;

    // s' = k + e * sk + t (mod n)
    let e_sk = sk.mul_tweak(&e).ok()?;           // e * sk
    let e_sk_t = e_sk.add_tweak(&t_scalar).ok()?; // e*sk + t
    let tmp = Scalar::from_be_bytes(e_sk_t.secret_bytes()).ok()?;
    let s_prime = k.add_tweak(&tmp).ok()?;        // k + e*sk + t

    Some(AdaptorPresig { R, s_prime: s_prime.secret_bytes() })
}

/// Verify adaptor presig: `s' * G = R + e * P + T`.
pub fn adaptor_verify(
    P: &PublicKey,
    T: &AdaptorPubkey,
    presig: &AdaptorPresig,
    msg: &Message,
) -> bool {
    let secp = Secp256k1::new();
    let e = compute_challenge(&presig.R, P, msg);

    // LHS: s' * G
    let lhs = match SecretKey::from_slice(&presig.s_prime) {
        Ok(sk) => Keypair::from_secret_key(&secp, &sk).public_key(),
        Err(_) => return false,
    };

    // RHS: R + e * P + T
    let e_P = match P.mul_tweak(&secp, &e) {
        Ok(p) => p, Err(_) => return false,
    };
    let R_eP = match presig.R.combine(&e_P) {
        Ok(p) => p, Err(_) => return false,
    };
    let rhs = match R_eP.combine(&T.to_pubkey()) {
        Ok(p) => p, Err(_) => return false,
    };

    lhs == rhs
}

/// Extract secret `t = s' - s` from the complete on-chain signature.
pub fn extract_secret(
    presig: &AdaptorPresig,
    complete_sig: &schnorr::Signature,
) -> Option<AdaptorSecret> {
    let sig_bytes = complete_sig.as_ref();
    if sig_bytes.len() < 64 { return None; }

    let s = Scalar::from_be_bytes(sig_bytes[32..64].try_into().ok()?).ok()?;
    let s_prime = Scalar::from_be_bytes(presig.s_prime).ok()?;

    let neg_s = scalar_negate(&s);
    let t = scalar_add(&s_prime, &neg_s);
    Some(AdaptorSecret(t.to_be_bytes()))
}

pub fn verify_secret(secret: &AdaptorSecret, T: &AdaptorPubkey) -> bool {
    let secp = Secp256k1::new();
    let kp = match Keypair::from_seckey_slice(&secp, &secret.0) {
        Ok(kp) => kp, Err(_) => return false,
    };
    kp.public_key() == T.to_pubkey()
}

/// Create the complete Schnorr signature using the **same nonce** as the
/// adaptor presig, to enable secret extraction via `t = s' - s`.
pub fn complete_schnorr_sig(
    sk: &SecretKey,
    k: &SecretKey,
    msg: &Message,
) -> schnorr::Signature {
    let secp = Secp256k1::new();
    let kp = Keypair::from_secret_key(&secp, sk);
    let P = kp.public_key();

    let nonce_kp = Keypair::from_secret_key(&secp, k);
    let R = nonce_kp.public_key();

    let e = compute_challenge(&R, &P, msg);

    // s = k + e * sk
    let e_sk = sk.mul_tweak(&e).expect("e*sk in range");
    let e_sk_scalar = Scalar::from_be_bytes(e_sk.secret_bytes()).expect("scalar");
    let s = k.add_tweak(&e_sk_scalar).expect("k + e*sk in range");

    // Encode as BIP-340 schnorr sig: [R_x(32) || s(32)]
    // Use even-y R
    let (R_xonly, _parity) = nonce_kp.x_only_public_key();
    let mut bytes = [0u8; 64];
    bytes[..32].copy_from_slice(&R_xonly.serialize());
    bytes[32..].copy_from_slice(&s.secret_bytes());
    schnorr::Signature::from_slice(&bytes).expect("valid sig")
}

// ─── MuSig2 key aggregation ───────────────────────────────────────────────────

fn musig2_coeff(L: &sha256::Hash, pk: &PublicKey) -> Option<Scalar> {
    let mut engine = sha256::HashEngine::default();
    let tag = sha256::Hash::hash(b"MuSig/keyagg");
    engine.input(&tag[..]);
    engine.input(&tag[..]);
    engine.input(&L[..]);
    engine.input(&pk.serialize());
    let hash = sha256::Hash::from_engine(engine);
    Scalar::from_be_bytes(hash.to_byte_array()).ok()
}

pub fn musig2_key_agg(pubkeys: &[PublicKey]) -> Option<PublicKey> {
    let secp = Secp256k1::new();
    if pubkeys.is_empty() { return None; }
    if pubkeys.len() == 1 { return Some(pubkeys[0]); }

    let mut sorted: Vec<[u8; 33]> = pubkeys.iter().map(|p| p.serialize()).collect();
    sorted.sort();

    let mut engine = sha256::HashEngine::default();
    let tag = sha256::Hash::hash(b"MuSig/KeyAgg list");
    engine.input(&tag[..]);
    engine.input(&tag[..]);
    for pk_bytes in &sorted { engine.input(pk_bytes); }
    let L = sha256::Hash::from_engine(engine);

    let mut agg = None;
    for pk_bytes in &sorted {
        let pk = PublicKey::from_slice(pk_bytes).ok()?;
        let c = musig2_coeff(&L, &pk)?;
        let term = pk.mul_tweak(&secp, &c).ok()?;
        agg = match agg {
            None => Some(term),
            Some(a) => Some(a.combine(&term).ok()?),
        };
    }
    agg
}

// ─── Protocol helpers ────────────────────────────────────────────────────────

#[derive(Clone, Debug)]
pub struct AdaptorSwapMessage {
    pub swap_id: [u8; 32],
    pub my_pubkey: PublicKey,
    pub aggregated_pubkey: PublicKey,
    pub my_nonce: NonceCommitment,
    pub aggregated_nonce: NonceCommitment,
    pub my_presig: Option<AdaptorPresig>,
    pub tweak_commitment: Option<AdaptorPubkey>,
}

// ─── Tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use rand::rngs::StdRng;
    use rand::SeedableRng;

    fn make_msg(data: &[u8]) -> Message {
        Message::from_digest(sha256::Hash::hash(data).to_byte_array())
    }

    #[test]
    fn test_adaptor_sig_roundtrip() {
        let secp = Secp256k1::new();
        let mut rng = StdRng::seed_from_u64(42);

        let alice_kp = Keypair::new(&secp, &mut rng);
        let alice_sk = SecretKey::from_keypair(&alice_kp);
        let alice_pk = alice_kp.public_key();

        let t = AdaptorSecret::random(&mut rng);
        let T = t.to_pubkey(&secp).unwrap();

        let (k, _) = generate_nonce(&mut rng);
        let msg = make_msg(b"atomic swap #42");

        // Alice creates adaptor presig
        let presig = adaptor_sign(&alice_sk, &k, &t, &msg).unwrap();
        assert!(adaptor_verify(&alice_pk, &T, &presig, &msg));

        // Alice creates the complete sig using the SAME nonce
        let complete_sig = complete_schnorr_sig(&alice_sk, &k, &msg);

        // Bob extracts t from the on-chain sig
        let extracted = extract_secret(&presig, &complete_sig).unwrap();
        assert!(verify_secret(&extracted, &T));
        assert_eq!(extracted.0, t.0);
    }

    #[test]
    fn test_wrong_tweak_fails() {
        let mut rng = StdRng::seed_from_u64(99);
        let secp = Secp256k1::new();
        let msg = make_msg(b"test swap");

        let kp = Keypair::new(&secp, &mut rng);
        let sk = SecretKey::from_keypair(&kp);
        let P = kp.public_key();

        let t = AdaptorSecret::random(&mut rng);
        let wrong_t = AdaptorSecret::random(&mut rng);
        let T = t.to_pubkey(&secp).unwrap();
        let wrong_T = wrong_t.to_pubkey(&secp).unwrap();

        let (k, _) = generate_nonce(&mut rng);
        let presig = adaptor_sign(&sk, &k, &t, &msg).unwrap();

        assert!(adaptor_verify(&P, &T, &presig, &msg));
        assert!(!adaptor_verify(&P, &wrong_T, &presig, &msg));
    }

    #[test]
    fn test_musig2_key_agg() {
        let secp = Secp256k1::new();
        let mut rng = StdRng::seed_from_u64(7);
        let pk_a = Keypair::new(&secp, &mut rng).public_key();
        let pk_b = Keypair::new(&secp, &mut rng).public_key();
        let agg = musig2_key_agg(&[pk_a, pk_b]).unwrap();
        let agg2 = musig2_key_agg(&[pk_b, pk_a]).unwrap();
        assert_eq!(agg, agg2);
    }

    #[test]
    fn test_scalar_negate() {
        let s = Scalar::from_be_bytes([1u8; 32]).unwrap();
        let neg = scalar_negate(&s);
        let zero = scalar_add(&s, &neg);
        assert!(scalar_to_biguint(&zero).is_zero());
    }

    #[test]
    fn test_scalar_add() {
        let a = Scalar::from_be_bytes([2u8; 32]).unwrap();
        let b = Scalar::from_be_bytes([3u8; 32]).unwrap();
        let sum = scalar_add(&a, &b);
        let diff = scalar_add(&sum, &scalar_negate(&b));
        assert_eq!(scalar_to_biguint(&diff), scalar_to_biguint(&a));
    }

    #[test]
    fn test_nonce_combination() {
        let mut rng = StdRng::seed_from_u64(42);
        let (_, nc_a) = generate_nonce(&mut rng);
        let (_, nc_b) = generate_nonce(&mut rng);
        let combined = nc_a.combine(&nc_b).unwrap();
        let combined_rev = nc_b.combine(&nc_a).unwrap();
        assert_eq!(combined.R, combined_rev.R);
    }
}
