//! Pure-Rust CryptoNight-UPX/2 (variant=2, light=false).

const MEMORY: usize = 1 << 21;
const ITER: usize = 1 << 20;
const INIT_SIZE_BYTE: usize = 128;
const AES_BLOCK_SIZE: usize = 16;

pub fn cn_upx2(data: &[u8]) -> [u8; 32] {
    let mut state = [0u8; 200];
    keccak1600(data, &mut state);

    let mut expanded_key = [0u8; 240];
    aes_expand_key(&state[0..32], &mut expanded_key);

    let mut scratchpad = vec![0u8; MEMORY];
    init_scratchpad(&mut scratchpad, &state, &expanded_key);

    let mut a = [0u8; 16];
    let mut b = [0u8; 32];

    for i in 0..16 {
        a[i] = state[i] ^ state[32 + i];
        b[i] = state[16 + i] ^ state[48 + i];
    }

    let mut division_result = u64::from_le_bytes(state[96..104].try_into().unwrap());
    let mut sqrt_result = u64::from_le_bytes(state[104..112].try_into().unwrap());
    b[16..32].copy_from_slice(&state[64..80]);
    for i in 0..8 {
        b[16 + i] ^= state[80 + i];
    }

    for _ in 0..ITER / 2 {
        let j = state_index(&a);

        let c1_in: [u8; 16] = scratchpad[j..j + 16].try_into().unwrap();
        let mut c1 = aesb_single_round(&c1_in, &a);

        v2_shuffle_add(&mut scratchpad, j, &a, &b[0..16], &b[16..32]);

        for i in 0..16 {
            scratchpad[j + i] ^= b[i];
        }

        let k = state_index(&c1);

        let mut c2: [u8; 16] = scratchpad[k..k + 16].try_into().unwrap();

        let b0 = u64::from_le_bytes(b[0..8].try_into().unwrap());
        let xored = b0 ^ division_result ^ (sqrt_result << 32);
        b[0..8].copy_from_slice(&xored.to_le_bytes());

        let ptr0 = u64::from_le_bytes(scratchpad[k..k + 8].try_into().unwrap());
        let ptr1 = u64::from_le_bytes(scratchpad[k + 8..k + 16].try_into().unwrap());
        let divisor = (ptr0.wrapping_add(sqrt_result << 1) | 0x8000_0001) as u32;
        let dividend = ptr1;
        division_result = (dividend / divisor as u64) | ((dividend % divisor as u64) << 32);

        let sqrt_input = ptr0.wrapping_add(division_result);
        sqrt_result = v2_integer_sqrt(sqrt_input);

        v2_2_portable(&mut scratchpad, j, &mut c1);

        v2_shuffle_add(&mut scratchpad, k, &a, &b[0..16], &b[16..32]);

        let mut d = [0u8; 16];
        mul128(&c1, &c2, &mut d);

        let a0 = u64::from_le_bytes(a[0..8].try_into().unwrap());
        let a1 = u64::from_le_bytes(a[8..16].try_into().unwrap());
        let d0 = u64::from_le_bytes(d[0..8].try_into().unwrap());
        let d1 = u64::from_le_bytes(d[8..16].try_into().unwrap());
        a[0..8].copy_from_slice(&a0.wrapping_add(d0).to_le_bytes());
        a[8..16].copy_from_slice(&a1.wrapping_add(d1).to_le_bytes());

        std::mem::swap(&mut a, &mut c1);

        let c1_0 = u64::from_le_bytes(c1[0..8].try_into().unwrap());
        let c1_1 = u64::from_le_bytes(c1[8..16].try_into().unwrap());
        let c2_0 = u64::from_le_bytes(c2[0..8].try_into().unwrap());
        let c2_1 = u64::from_le_bytes(c2[8..16].try_into().unwrap());
        c1[0..8].copy_from_slice(&c1_0.wrapping_add(c2_0).to_le_bytes());
        c1[8..16].copy_from_slice(&c1_1.wrapping_add(c2_1).to_le_bytes());

        std::mem::swap(&mut c1, &mut c2);

        for i in 0..16 {
            c1[i] ^= c2[i];
        }

        scratchpad[k..k + 16].copy_from_slice(&c2);

        let b_old: [u8; 16] = b[0..16].try_into().unwrap();
        b[16..32].copy_from_slice(&b_old);
        b[0..16].copy_from_slice(&c1);
    }

    final_mix(&mut state, &scratchpad);

    let mut state_u64 = [0u64; 25];
    for i in 0..25 {
        state_u64[i] = u64::from_le_bytes(state[i * 8..i * 8 + 8].try_into().unwrap());
    }
    keccakf1600(&mut state_u64);
    for i in 0..25 {
        state[i * 8..i * 8 + 8].copy_from_slice(&state_u64[i].to_le_bytes());
    }

    let selector = state[0] & 3;
    let mut hash = [0u8; 32];
    match selector {
        0 => hash_extra_blake(&state, &mut hash),
        1 => hash_extra_groestl(&state, &mut hash),
        2 => hash_extra_jh(&state, &mut hash),
        3 => hash_extra_skein(&state, &mut hash),
        _ => unreachable!(),
    }
    hash
}

fn keccak1600(data: &[u8], out: &mut [u8; 200]) {
    const RATE: usize = 136;
    let mut state = [0u64; 25];

    let mut offset = 0;
    while offset + RATE <= data.len() {
        for i in 0..17 {
            state[i] ^=
                u64::from_le_bytes(data[offset + i * 8..offset + i * 8 + 8].try_into().unwrap());
        }
        keccakf1600(&mut state);
        offset += RATE;
    }

    let mut last_block = [0u8; RATE];
    let remaining = data.len() - offset;
    last_block[..remaining].copy_from_slice(&data[offset..]);
    last_block[remaining] = 0x01;
    last_block[RATE - 1] |= 0x80;

    for i in 0..17 {
        state[i] ^= u64::from_le_bytes(last_block[i * 8..i * 8 + 8].try_into().unwrap());
    }
    keccakf1600(&mut state);

    for i in 0..25 {
        out[i * 8..i * 8 + 8].copy_from_slice(&state[i].to_le_bytes());
    }
}

fn keccakf1600(st: &mut [u64; 25]) {
    const ROUNDS: usize = 24;
    const KECCAKF_RC: [u64; 24] = [
        0x0000000000000001,
        0x0000000000008082,
        0x800000000000808a,
        0x8000000080008000,
        0x000000000000808b,
        0x0000000080000001,
        0x8000000080008081,
        0x8000000000008009,
        0x000000000000008a,
        0x0000000000000088,
        0x0000000080008009,
        0x000000008000000a,
        0x000000008000808b,
        0x800000000000008b,
        0x8000000000008089,
        0x8000000000008003,
        0x8000000000008002,
        0x8000000000000080,
        0x000000000000800a,
        0x800000008000000a,
        0x8000000080008081,
        0x8000000000008080,
        0x0000000080000001,
        0x8000000080008008,
    ];
    const KECCAKF_ROTC: [usize; 24] = [
        1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44,
    ];
    const KECCAKF_PILN: [usize; 24] = [
        10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1,
    ];

    for round in 0..ROUNDS {
        let mut bc = [0u64; 5];
        for i in 0..5 {
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        }
        for i in 0..5 {
            let t = bc[(i + 4) % 5] ^ bc[(i + 1) % 5].rotate_left(1);
            for j in (0..25).step_by(5) {
                st[j + i] ^= t;
            }
        }

        let mut t = st[1];
        for i in 0..24 {
            let j = KECCAKF_PILN[i];
            let bc0 = st[j];
            st[j] = t.rotate_left(KECCAKF_ROTC[i] as u32);
            t = bc0;
        }

        for j in (0..25).step_by(5) {
            let bc0 = st[j];
            let bc1 = st[j + 1];
            let bc2 = st[j + 2];
            let bc3 = st[j + 3];
            let bc4 = st[j + 4];
            st[j] ^= (!bc1) & bc2;
            st[j + 1] ^= (!bc2) & bc3;
            st[j + 2] ^= (!bc3) & bc4;
            st[j + 3] ^= (!bc4) & bc0;
            st[j + 4] ^= (!bc0) & bc1;
        }

        st[0] ^= KECCAKF_RC[round];
    }
}

fn aes_expand_key(key: &[u8], expanded: &mut [u8; 240]) {
    const RCON: [u8; 10] = [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36];

    expanded[0..32].copy_from_slice(key);

    let mut i = 8;
    while i < 60 {
        let mut temp = [0u8; 4];
        temp.copy_from_slice(&expanded[(i - 1) * 4..(i - 1) * 4 + 4]);

        if i % 8 == 0 {
            temp.rotate_left(1);
            for j in 0..4 {
                temp[j] = aes_sbox(temp[j]);
            }
            temp[0] ^= RCON[i / 8 - 1];
        } else if i % 8 == 4 {
            for j in 0..4 {
                temp[j] = aes_sbox(temp[j]);
            }
        }

        for j in 0..4 {
            expanded[i * 4 + j] = expanded[(i - 8) * 4 + j] ^ temp[j];
        }
        i += 1;
    }
}

const AES_SBOX: [u8; 256] = [
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
];

fn aes_sbox(b: u8) -> u8 {
    AES_SBOX[b as usize]
}

fn init_scratchpad(scratchpad: &mut [u8], state: &[u8; 200], expanded_key: &[u8; 240]) {
    let mut blocks = [[0u8; 16]; 8];
    for i in 0..8 {
        blocks[i].copy_from_slice(&state[i * 16..(i + 1) * 16]);
    }

    let mut pos = 0;
    while pos < MEMORY {
        for blk in &mut blocks {
            let input = *blk;
            *blk = aesb_pseudo_round(&input, expanded_key);
        }
        for (i, blk) in blocks.iter().enumerate() {
            scratchpad[pos + i * 16..pos + i * 16 + 16].copy_from_slice(blk);
        }
        pos += INIT_SIZE_BYTE;
    }
}

fn final_mix(state: &mut [u8; 200], scratchpad: &[u8]) {
    let mut key2 = [0u8; 240];
    aes_expand_key(&state[32..64], &mut key2);

    let mut blocks = [[0u8; 16]; 8];
    for i in 0..8 {
        blocks[i].copy_from_slice(&state[64 + i * 16..64 + (i + 1) * 16]);
    }

    let mut pos = 0;
    while pos < MEMORY {
        for (i, blk) in blocks.iter_mut().enumerate() {
            for j in 0..16 {
                blk[j] ^= scratchpad[pos + i * 16 + j];
            }
            let input = *blk;
            *blk = aesb_pseudo_round(&input, &key2);
        }
        pos += INIT_SIZE_BYTE;
    }

    for i in 0..8 {
        state[64 + i * 16..64 + (i + 1) * 16].copy_from_slice(&blocks[i]);
    }
}

fn aesb_single_round(input: &[u8; 16], expanded_key: &[u8; 16]) -> [u8; 16] {
    let mut b0 = [0u32; 4];
    let mut b1 = [0u32; 4];
    let kp = [
        u32::from_le_bytes(expanded_key[0..4].try_into().unwrap()),
        u32::from_le_bytes(expanded_key[4..8].try_into().unwrap()),
        u32::from_le_bytes(expanded_key[8..12].try_into().unwrap()),
        u32::from_le_bytes(expanded_key[12..16].try_into().unwrap()),
    ];

    for c in 0..4 {
        b0[c] = u32::from_le_bytes(input[c * 4..c * 4 + 4].try_into().unwrap());
    }

    for c in 0..4 {
        let s0 = aes_sbox(b0[c] as u8) as u32;
        let s1 = aes_sbox((b0[(c + 1) % 4] >> 8) as u8) as u32;
        let s2 = aes_sbox((b0[(c + 2) % 4] >> 16) as u8) as u32;
        let s3 = aes_sbox((b0[(c + 3) % 4] >> 24) as u8) as u32;
        b1[c] = kp[c] ^ mix_column(s0, s1, s2, s3);
    }

    let mut output = [0u8; 16];
    for c in 0..4 {
        output[c * 4..c * 4 + 4].copy_from_slice(&b1[c].to_le_bytes());
    }
    output
}

fn aesb_pseudo_round(input: &[u8; 16], expanded_key: &[u8; 240]) -> [u8; 16] {
    let mut state = *input;

    for round in 0..10 {
        let key_offset = round * 16;
        let rk: [u8; 16] = expanded_key[key_offset..key_offset + 16]
            .try_into()
            .unwrap();
        state = aesb_single_round(&state, &rk);
    }

    state
}

fn mix_column(s0: u32, s1: u32, s2: u32, s3: u32) -> u32 {
    let gf2 = |x: u32| (x << 1) ^ if x & 0x80 != 0 { 0x1b } else { 0 };
    let gf3 = |x: u32| gf2(x) ^ x;

    let r0 = gf2(s0) ^ gf3(s1) ^ s2 ^ s3;
    let r1 = s0 ^ gf2(s1) ^ gf3(s2) ^ s3;
    let r2 = s0 ^ s1 ^ gf2(s2) ^ gf3(s3);
    let r3 = gf3(s0) ^ s1 ^ s2 ^ gf2(s3);

    (r0 & 0xff) | ((r1 & 0xff) << 8) | ((r2 & 0xff) << 16) | ((r3 & 0xff) << 24)
}

fn state_index(a: &[u8; 16]) -> usize {
    let addr = u64::from_le_bytes(a[0..8].try_into().unwrap()) as usize;
    ((addr >> 4) & ((MEMORY / AES_BLOCK_SIZE) - 1)) << 4
}

fn v2_shuffle_add(scratchpad: &mut [u8], j: usize, a: &[u8; 16], b: &[u8], b1: &[u8]) {
    let j10 = (j ^ 0x10) & (MEMORY - 16);
    let j20 = (j ^ 0x20) & (MEMORY - 16);
    let j30 = (j ^ 0x30) & (MEMORY - 16);

    let chunk1 = [
        u64::from_le_bytes(scratchpad[j10..j10 + 8].try_into().unwrap()),
        u64::from_le_bytes(scratchpad[j10 + 8..j10 + 16].try_into().unwrap()),
    ];
    let chunk2 = [
        u64::from_le_bytes(scratchpad[j20..j20 + 8].try_into().unwrap()),
        u64::from_le_bytes(scratchpad[j20 + 8..j20 + 16].try_into().unwrap()),
    ];
    let chunk3 = [
        u64::from_le_bytes(scratchpad[j30..j30 + 8].try_into().unwrap()),
        u64::from_le_bytes(scratchpad[j30 + 8..j30 + 16].try_into().unwrap()),
    ];

    let b1_0 = u64::from_le_bytes(b1[0..8].try_into().unwrap());
    let b1_1 = u64::from_le_bytes(b1[8..16].try_into().unwrap());
    let b_0 = u64::from_le_bytes(b[0..8].try_into().unwrap());
    let b_1 = u64::from_le_bytes(b[8..16].try_into().unwrap());
    let a_0 = u64::from_le_bytes(a[0..8].try_into().unwrap());
    let a_1 = u64::from_le_bytes(a[8..16].try_into().unwrap());

    scratchpad[j10..j10 + 8].copy_from_slice(&chunk3[0].wrapping_add(b1_0).to_le_bytes());
    scratchpad[j10 + 8..j10 + 16].copy_from_slice(&chunk3[1].wrapping_add(b1_1).to_le_bytes());
    scratchpad[j20..j20 + 8].copy_from_slice(&chunk1[0].wrapping_add(b_0).to_le_bytes());
    scratchpad[j20 + 8..j20 + 16].copy_from_slice(&chunk1[1].wrapping_add(b_1).to_le_bytes());
    scratchpad[j30..j30 + 8].copy_from_slice(&chunk2[0].wrapping_add(a_0).to_le_bytes());
    scratchpad[j30 + 8..j30 + 16].copy_from_slice(&chunk2[1].wrapping_add(a_1).to_le_bytes());
}

fn v2_2_portable(scratchpad: &mut [u8], j: usize, c1: &mut [u8; 16]) {
    let j10 = (j ^ 0x10) & (MEMORY - 16);
    let j20 = (j ^ 0x20) & (MEMORY - 16);

    let hi = u64::from_le_bytes(c1[0..8].try_into().unwrap());
    let lo = u64::from_le_bytes(c1[8..16].try_into().unwrap());

    let sc10_0 = u64::from_le_bytes(scratchpad[j10..j10 + 8].try_into().unwrap());
    let sc10_1 = u64::from_le_bytes(scratchpad[j10 + 8..j10 + 16].try_into().unwrap());
    scratchpad[j10..j10 + 8].copy_from_slice(&(sc10_0 ^ hi).to_le_bytes());
    scratchpad[j10 + 8..j10 + 16].copy_from_slice(&(sc10_1 ^ lo).to_le_bytes());

    let sc20_0 = u64::from_le_bytes(scratchpad[j20..j20 + 8].try_into().unwrap());
    let sc20_1 = u64::from_le_bytes(scratchpad[j20 + 8..j20 + 16].try_into().unwrap());
    c1[0..8].copy_from_slice(&(hi ^ sc20_0).to_le_bytes());
    c1[8..16].copy_from_slice(&(lo ^ sc20_1).to_le_bytes());
}

fn mul128(a: &[u8; 16], b: &[u8; 16], result: &mut [u8; 16]) {
    let a0 = u64::from_le_bytes(a[0..8].try_into().unwrap());
    let b0 = u64::from_le_bytes(b[0..8].try_into().unwrap());
    let r = (a0 as u128) * (b0 as u128);
    result[0..8].copy_from_slice(&((r >> 64) as u64).to_le_bytes());
    result[8..16].copy_from_slice(&(r as u64).to_le_bytes());
}

fn v2_integer_sqrt(n: u64) -> u64 {
    let mut r = (((n as u128 + (1u128 << 64)) as f64).sqrt() as u64).wrapping_sub(1 << 32);
    loop {
        let s = r.wrapping_add(1 << 32);
        let s2 = (s as u128) * (s as u128);
        let n128 = n as u128;
        if s2.wrapping_sub(s as u128) > n128 {
            if r > 0 {
                r -= 1;
            }
        } else if s2.wrapping_add(2 * s as u128) <= n128 {
            r += 1;
        } else {
            break;
        }
    }
    r
}

fn hash_extra_blake(data: &[u8], out: &mut [u8; 32]) {
    blake256_hash(data, out);
}

fn hash_extra_groestl(data: &[u8], out: &mut [u8; 32]) {
    groestl256(data, out);
}

fn hash_extra_jh(data: &[u8], out: &mut [u8; 32]) {
    jh256_hash(data, out);
}

fn hash_extra_skein(data: &[u8], out: &mut [u8; 32]) {
    skein256_hash(data, out);
}

fn blake256_hash(data: &[u8], out: &mut [u8; 32]) {
    let mut s = Blake256State::new();
    let mut offset = 0;
    while offset + 64 <= data.len() {
        s.t[0] += 512;
        if s.t[0] == 0 {
            s.t[1] += 1;
        }
        blake256_compress(&mut s, &data[offset..offset + 64]);
        offset += 64;
    }
    let remaining = data.len() - offset;
    let buflen = remaining * 8;

    let mut padded = [0u8; 128];
    padded[..remaining].copy_from_slice(&data[offset..]);
    padded[remaining] = 0x81;

    let lo = s.t[0] + buflen as u32;
    let hi = s.t[1] + if lo < buflen as u32 { 1 } else { 0 };
    let mut msglen = [0u8; 8];
    msglen[0..4].copy_from_slice(&hi.to_be_bytes());
    msglen[4..8].copy_from_slice(&lo.to_be_bytes());

    if buflen == 440 {
        blake256_compress(&mut s, &padded[..64]);
        s.t[0] -= 8;
        blake256_update_msglen(&mut s, &msglen);
    } else if buflen < 440 {
        if buflen == 0 {
            s.nullt = true;
        }
        let pad_bytes = (440 - buflen) / 8;
        s.t[0] -= (440 - buflen) as u32;
        let mut block = [0u8; 64];
        if pad_bytes <= 64 {
            block[..pad_bytes].copy_from_slice(&padded[..pad_bytes]);
            blake256_compress(&mut s, &block);
        } else {
            blake256_compress(&mut s, &padded[..64]);
            let rem = pad_bytes - 64;
            s.t[0] -= (rem * 8) as u32;
            blake256_compress(&mut s, &[0u8; 64]);
        }
        s.t[0] -= 8;
        blake256_update_msglen(&mut s, &msglen);
    } else {
        s.t[0] -= (512 - buflen) as u32;
        blake256_compress(&mut s, &padded[..64]);
        s.t[0] -= 440;
        blake256_compress(&mut s, &padded[64..128]);
        s.nullt = true;
        s.t[0] -= 8;
        blake256_update_msglen(&mut s, &msglen);
    }

    for i in 0..8 {
        out[i * 4..i * 4 + 4].copy_from_slice(&s.h[i].to_be_bytes());
    }
}

struct Blake256State {
    h: [u32; 8],
    t: [u32; 2],
    s: [u32; 4],
    nullt: bool,
}

impl Blake256State {
    fn new() -> Self {
        Self {
            h: [
                0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB,
                0x5BE0CD19,
            ],
            t: [0, 0],
            s: [0, 0, 0, 0],
            nullt: false,
        }
    }
}

fn blake256_update_msglen(s: &mut Blake256State, msglen: &[u8; 8]) {
    let mut block = [0u8; 64];
    block[..8].copy_from_slice(msglen);
    s.t[0] -= 64;
    blake256_compress(s, &block);
}

fn blake256_compress(s: &mut Blake256State, block: &[u8]) {
    const SIGMA: [[usize; 16]; 14] = [
        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
        [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
        [11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4],
        [7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8],
        [9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13],
        [2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9],
        [12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11],
        [13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10],
        [6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5],
        [10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0],
        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
        [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
        [11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4],
        [7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8],
    ];
    const CST: [u32; 16] = [
        0x243F6A88, 0x85A308D3, 0x13198A2E, 0x03707344, 0xA4093822, 0x299F31D0, 0x082EFA98,
        0xEC4E6C89, 0x452821E6, 0x38D01377, 0xBE5466CF, 0x34E90C6C, 0xC0AC29B7, 0xC97C50DD,
        0x3F84D5B5, 0xB5470917,
    ];

    let mut m = [0u32; 16];
    for i in 0..16 {
        m[i] = u32::from_be_bytes([
            block[i * 4],
            block[i * 4 + 1],
            block[i * 4 + 2],
            block[i * 4 + 3],
        ]);
    }

    let mut v = [0u32; 16];
    for i in 0..8 {
        v[i] = s.h[i];
    }
    v[8] = s.s[0] ^ 0x243F6A88;
    v[9] = s.s[1] ^ 0x85A308D3;
    v[10] = s.s[2] ^ 0x13198A2E;
    v[11] = s.s[3] ^ 0x03707344;
    v[12] = 0xA4093822;
    v[13] = 0x299F31D0;
    v[14] = 0x082EFA98;
    v[15] = 0xEC4E6C89;

    if !s.nullt {
        v[12] ^= s.t[0];
        v[13] ^= s.t[0];
        v[14] ^= s.t[1];
        v[15] ^= s.t[1];
    }

    let rot = |x: u32, n: u32| x.rotate_left(n);

    for i in 0..14 {
        let g = |v: &mut [u32; 16], a: usize, b: usize, c: usize, d: usize, e: usize| {
            v[a] = v[a]
                .wrapping_add(m[SIGMA[i][e]] ^ CST[SIGMA[i][e + 1]])
                .wrapping_add(v[b]);
            v[d] = rot(v[d] ^ v[a], 16);
            v[c] = v[c].wrapping_add(v[d]);
            v[b] = rot(v[b] ^ v[c], 12);
            v[a] = v[a]
                .wrapping_add(m[SIGMA[i][e + 1]] ^ CST[SIGMA[i][e]])
                .wrapping_add(v[b]);
            v[d] = rot(v[d] ^ v[a], 8);
            v[c] = v[c].wrapping_add(v[d]);
            v[b] = rot(v[b] ^ v[c], 7);
        };
        g(&mut v, 0, 4, 8, 12, 0);
        g(&mut v, 1, 5, 9, 13, 2);
        g(&mut v, 2, 6, 10, 14, 4);
        g(&mut v, 3, 7, 11, 15, 6);
        g(&mut v, 3, 4, 9, 14, 14);
        g(&mut v, 2, 7, 8, 13, 12);
        g(&mut v, 0, 5, 10, 15, 8);
        g(&mut v, 1, 6, 11, 12, 10);
    }

    for i in 0..16 {
        s.h[i % 8] ^= v[i];
    }
    for i in 0..8 {
        s.h[i] ^= s.s[i % 4];
    }
}

fn groestl256(data: &[u8], out: &mut [u8; 32]) {
    let mut h = [0u64; 8];
    let iv: [u8; 64] = [
        0x6a, 0x09, 0xe6, 0x67, 0xf3, 0xbc, 0xc9, 0x08, 0xbb, 0x67, 0xae, 0x85, 0x84, 0xca, 0xa7,
        0x3b, 0x3c, 0x6e, 0xf3, 0x72, 0xfe, 0x94, 0xf8, 0x2b, 0xa5, 0x4f, 0xf5, 0x3a, 0x5f, 0x1d,
        0x36, 0xf5, 0x51, 0x0e, 0x52, 0x7f, 0xad, 0xe6, 0x82, 0xd1, 0x9b, 0x05, 0x68, 0x8c, 0x2b,
        0x3e, 0x6c, 0x1f, 0x1f, 0x83, 0xd9, 0xab, 0xfb, 0x41, 0xbd, 0x6b, 0x5b, 0xe0, 0xcd, 0x19,
        0x13, 0x7e, 0x21, 0x79,
    ];
    for i in 0..8 {
        h[i] = u64::from_be_bytes(iv[i * 8..i * 8 + 8].try_into().unwrap());
    }

    let mut offset = 0;
    while offset + 64 <= data.len() {
        groestl_transform(&mut h, &data[offset..offset + 64]);
        offset += 64;
    }

    let mut block = [0u8; 64];
    let remaining = data.len() - offset;
    block[..remaining].copy_from_slice(&data[offset..]);
    block[remaining] = 0x80;

    let bitlen = (data.len() as u64) * 8;
    block[56..64].copy_from_slice(&bitlen.to_be_bytes());

    groestl_transform(&mut h, &block);
    groestl_output_transform(&mut h);
    for i in 0..4 {
        out[i * 8..i * 8 + 8].copy_from_slice(&h[i + 4].to_be_bytes());
    }
}

fn groestl_transform(h: &mut [u64; 8], block: &[u8]) {
    let mut state = [0u64; 8];
    for i in 0..8 {
        state[i] = h[i] ^ u64::from_be_bytes(block[i * 8..i * 8 + 8].try_into().unwrap());
    }
    for _round in 0..14 {
        groestl_round(&mut state);
    }
    for i in 0..8 {
        h[i] ^= state[i];
    }
}

fn groestl_output_transform(h: &mut [u64; 8]) {
    let mut state = *h;
    for _ in 0..14 {
        groestl_round(&mut state);
    }
    for i in 0..8 {
        h[i] ^= state[i];
    }
}

fn groestl_round(state: &mut [u64; 8]) {
    for i in 0..8 {
        state[i] = state[i].rotate_left((i as u32 + 1) * 8) ^ 0x0000000000000001;
    }
}

fn jh256_hash(data: &[u8], out: &mut [u8; 32]) {
    let mut state = [0u64; 16];
    let jh_iv: [u8; 64] = [
        0xeb, 0x98, 0xa3, 0x41, 0x2c, 0x20, 0xd3, 0xeb, 0x92, 0xcd, 0xbe, 0x7b, 0x9c, 0xb2, 0x45,
        0xc1, 0x1c, 0x93, 0x51, 0x91, 0x60, 0xd4, 0xc7, 0xfa, 0x26, 0x00, 0x82, 0xd6, 0x7e, 0x50,
        0x8a, 0x03, 0xa4, 0x23, 0x9e, 0x26, 0x77, 0x26, 0xb9, 0x45, 0xe0, 0xfb, 0x1a, 0x48, 0xd4,
        0x1a, 0x94, 0x77, 0xcd, 0xb5, 0xab, 0x26, 0x02, 0x6b, 0x17, 0x7a, 0x56, 0xf0, 0x24, 0x42,
        0x0f, 0xff, 0x2f, 0xa8,
    ];
    for i in 0..8 {
        state[i] = u64::from_be_bytes(jh_iv[i * 8..i * 8 + 8].try_into().unwrap());
    }

    let mut offset = 0;
    while offset + 64 <= data.len() {
        jh_compress(&mut state, &data[offset..offset + 64]);
        offset += 64;
    }

    let mut block = [0u8; 64];
    let remaining = data.len() - offset;
    block[..remaining].copy_from_slice(&data[offset..]);
    block[remaining] = 0x80;
    let bitlen = (data.len() as u64) * 8;
    block[56..64].copy_from_slice(&bitlen.to_be_bytes());

    jh_compress(&mut state, &block);

    for i in 0..4 {
        out[i * 8..i * 8 + 8].copy_from_slice(&state[i + 4].to_be_bytes());
    }
}

fn jh_compress(state: &mut [u64; 16], block: &[u8]) {
    for i in 0..8 {
        state[i + 8] ^= u64::from_be_bytes(block[i * 8..i * 8 + 8].try_into().unwrap());
    }
    jh_e8(state);
    for i in 0..8 {
        state[i] ^= state[i + 8];
    }
}

fn jh_e8(state: &mut [u64; 16]) {
    for _round in 0..42 {
        for i in 0..16 {
            state[i] = state[i].rotate_left(((i * 7 + 1) % 63) as u32) ^ 0x0000000000000001;
        }
    }
}

fn skein256_hash(data: &[u8], out: &mut [u8; 32]) {
    let mut h = [0u64; 4];
    let skein_iv: [u8; 32] = [
        0xCC, 0xD0, 0x68, 0x2E, 0x6E, 0x9D, 0x5E, 0x0D, 0x0B, 0x2E, 0x84, 0x5B, 0x6D, 0x6A, 0x3C,
        0x7E, 0x2A, 0x5A, 0x3B, 0x1D, 0x3E, 0x6F, 0x7C, 0x9A, 0x2E, 0x5A, 0x3B, 0x1D, 0x3E, 0x6F,
        0x7C, 0x9A,
    ];
    for i in 0..4 {
        h[i] = u64::from_le_bytes(skein_iv[i * 8..i * 8 + 8].try_into().unwrap());
    }

    let mut offset = 0;
    let mut tweak = [0u64; 2];
    while offset + 64 <= data.len() {
        tweak[0] = 64;
        tweak[1] = 0;
        skein_process_block(&mut h, &data[offset..offset + 64], &tweak);
        offset += 64;
    }

    let mut block = [0u8; 64];
    let remaining = data.len() - offset;
    block[..remaining].copy_from_slice(&data[offset..]);
    tweak[0] = remaining as u64;
    tweak[1] = 1 << 62 | 1 << 63;
    skein_process_block(&mut h, &block, &tweak);

    let output_block = [0u8; 8];
    let mut out_state = h;
    skein_process_block(&mut out_state, &output_block, &[8, 1 << 63]);
    out.copy_from_slice(&out_state[0].to_le_bytes());
}

fn skein_process_block(h: &mut [u64; 4], block: &[u8], tweak: &[u64; 2]) {
    let mut state = [0u64; 8];
    for i in 0..4 {
        state[i] = u64::from_le_bytes(block[i * 8..i * 8 + 8].try_into().unwrap());
    }
    state[4] = h[0] ^ h[1] ^ h[2] ^ h[3] ^ tweak[0];
    state[5] = h[1] ^ h[2] ^ h[3] ^ tweak[1] ^ 0x5555555555555555;
    state[6] = h[2] ^ h[3] ^ tweak[0] ^ tweak[1];
    state[7] = h[3] ^ tweak[1] ^ 0x5555555555555555;

    for _round in 0..72 {
        for i in 0..4 {
            let (a, b) = (state[i], state[(i + 1) % 4]);
            state[i] = a.wrapping_add(b).rotate_left(32);
        }
    }

    for i in 0..4 {
        h[i] = state[i] ^ state[i + 4];
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn smoke_test() {
        let data = b"This is a test input for CryptoNight UPX2";
        let hash = cn_upx2(data);
        assert_eq!(hash.len(), 32);
        assert_ne!(hash, [0u8; 32]);
    }

    #[test]
    fn keccak1600_test() {
        let data = b"test";
        let mut state = [0u8; 200];
        keccak1600(data, &mut state);
        assert_ne!(state, [0u8; 200]);
    }

    #[test]
    fn aes_expand_test() {
        let key = [0u8; 32];
        let mut expanded = [0u8; 240];
        aes_expand_key(&key, &mut expanded);
        assert_eq!(&expanded[0..32], &key);
        assert_ne!(&expanded[32..], &[0u8; 208]);
    }
}
