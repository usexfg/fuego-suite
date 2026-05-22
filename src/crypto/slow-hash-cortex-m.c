/* ═══════════════════════════════════════════════════════════════════════
 * IoT-Lite PoW — Cortex-M Reference Implementation
 *
 * Sequential-dependent access (same as CryptoNight-R), 256 KB scratchpad,
 * optimized for Cortex-M4/M7 with HW AES peripheral and DSP extensions.
 *
 * ═══════════════════════════════════════════════════════════════════════ */

#include <string.h>
#include <stdint.h>
#include "hash-ops.h"
#include "oaes_lib.h"
#include "variant2_int_sqrt.h"
#include "Common/int-util.h"

/* ═══════════════════════════════════════════════════════════════════════
 * IoT-Lite Parameters — same as portable implementation
 * ═══════════════════════════════════════════════════════════════════════ */
#define MEMORY_IOT_LITE    (1 << 18)   /* 256 KB scratchpad */
#define ITER_IOT_LITE      (1 << 18)   /* 262,144 mixing iterations */
#define AES_BLOCK_SIZE     16
#define AES_KEY_SIZE       32
#define INIT_SIZE_BLK      8
#define INIT_SIZE_BYTE    (INIT_SIZE_BLK * AES_BLOCK_SIZE)

/* ═══════════════════════════════════════════════════════════════════════
 * Memory Placement — DTCM for hot state, SRAM for scratchpad
 * ═══════════════════════════════════════════════════════════════════════ */
#if defined(__CORTEX_M) && (__CORTEX_M >= 7)
#define DTCM_BSS __attribute__((section(".dtcm.bss"), aligned(16)))
#else
#define DTCM_BSS __attribute__((aligned(16)))
#endif

static DTCM_BSS uint8_t scratchpad[MEMORY_IOT_LITE];

/* ═══════════════════════════════════════════════════════════════════════
 * Hardware AES Peripheral — STM32 CRYP AES / nRF CC310
 * ═══════════════════════════════════════════════════════════════════════ */
#if defined(STM32F4xx) || defined(STM32F7xx) || defined(STM32H7xx)
#include "stm32f4xx_hal.h"

static void stm32_aes_single_round(const uint8_t *in, uint8_t *out, const uint8_t *key)
{
    /* On Cortex-M, software AES for single-round operations is typically
     * faster than setting up the AES peripheral for each call.
     * For IoT-Lite, use the portable aesb_single_round from the oaes library. */
    aesb_single_round(in, out, key);
}

#define HAS_HW_AES 0  /* Single-round AES uses software path */

#elif defined(NRF52840_XXAA) || defined(NRF52832_XXAB)
#define HAS_HW_AES 0

/* ═══════════════════════════════════════════════════════════════════════
 * DSP Extensions — use SMUAD for shuffle-add (Cortex-M4/M7)
 * ═══════════════════════════════════════════════════════════════════════ */
#elif defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_7M__)
#define HAS_DSP 1

/* DSP-accelerated 64-bit multiply for the integer math step */
#define MUL128_DSP(c0, b0, hi, lo) \
    do { \
        uint32_t c_lo = (uint32_t)(c0); \
        uint32_t c_hi = (uint32_t)((c0) >> 32); \
        uint32_t b_lo = (uint32_t)(b0); \
        uint32_t b_hi = (uint32_t)((b0) >> 32); \
        uint32_t p0 = __SMUAD(c_lo, b_lo); \
        uint32_t p1 = __SMUAD(c_hi, b_hi); \
        uint32_t p2 = __SMUAD(c_lo, b_hi); \
        uint32_t p3 = __SMUAD(c_hi, b_lo); \
        lo = p0 + ((p2 + p3) << 16); \
        hi = p1 + ((p2 + p3) >> 16) + (lo < p0); \
    } while(0)

#else
#define MUL128_DSP(c0, b0, hi, lo) do { lo = mul128(c0, b0, &hi); } while(0)
#define HAS_DSP 0
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * Helper Macros — match the portable implementation exactly
 * ═══════════════════════════════════════════════════════════════════════ */
#define U64(x) ((uint64_t *)(x))

static void xor_blocks(uint8_t *a, const uint8_t *b) {
    U64(a)[0] ^= U64(b)[0];
    U64(a)[1] ^= U64(b)[1];
}

static void xor64(uint64_t *a, const uint64_t b) {
    *a ^= b;
}

static void copy_block(uint8_t *dst, const uint8_t *src) {
    memcpy(dst, src, AES_BLOCK_SIZE);
}

static void swap_blocks(uint8_t *a, uint8_t *b) {
    uint64_t t[2];
    U64(t)[0] = U64(a)[0];
    U64(t)[1] = U64(a)[1];
    U64(a)[0] = U64(b)[0];
    U64(a)[1] = U64(b)[1];
    U64(b)[0] = U64(t)[0];
    U64(b)[1] = U64(t)[1];
}

static void sum_half_blocks(uint8_t *a, const uint8_t *b) {
    U64(a)[0] += U64(b)[0];
    U64(a)[1] += U64(b)[1];
}

static size_t e2i_iot(const uint8_t *a) {
    return (*((uint64_t *)a) / AES_BLOCK_SIZE) & ((MEMORY_IOT_LITE / AES_BLOCK_SIZE) - 1);
}

static void mul_iot(const uint8_t *ca, const uint8_t *cb, uint8_t *cres) {
    uint64_t a0 = ((uint64_t *)ca)[0];
    uint64_t b0 = ((uint64_t *)cb)[0];
#if HAS_DSP
    uint64_t hi, lo;
    MUL128_DSP(a0, b0, hi, lo);
    ((uint64_t *)cres)[0] = hi;
    ((uint64_t *)cres)[1] = lo;
#else
    ((uint64_t *)cres)[0] = mul128(a0, b0, &((uint64_t *)cres)[1]);
#endif
}

#define VARIANT2_SHUFFLE_ADD_CM(base_ptr, offset) \
    do { \
        uint64_t *chunk1 = U64((base_ptr) + ((offset) ^ 0x10)); \
        uint64_t *chunk2 = U64((base_ptr) + ((offset) ^ 0x20)); \
        uint64_t *chunk3 = U64((base_ptr) + ((offset) ^ 0x30)); \
        const uint64_t chunk1_old[2] = { chunk1[0], chunk1[1] }; \
        const uint64_t b1[2] = { ((uint64_t *)b)[2], ((uint64_t *)b)[3] }; \
        chunk1[0] = chunk3[0] + b1[0]; \
        chunk1[1] = chunk3[1] + b1[1]; \
        const uint64_t a0[2] = { ((uint64_t *)a)[0], ((uint64_t *)a)[1] }; \
        chunk3[0] = chunk2[0] + a0[0]; \
        chunk3[1] = chunk2[1] + a0[1]; \
        const uint64_t b0[2] = { ((uint64_t *)b)[0], ((uint64_t *)b)[1] }; \
        chunk2[0] = chunk1_old[0] + b0[0]; \
        chunk2[1] = chunk1_old[1] + b0[1]; \
    } while(0)

/* Integer square root — same algorithm as variant2_int_sqrt.h */
static inline uint64_t sqrt_int64(uint64_t x) {
    if (x == 0) return 0;
    uint64_t res = 0;
    uint64_t bit = 1ULL << 62;
    while (bit > x) bit >>= 2;
    while (bit != 0) {
        if (x >= res + bit) {
            x -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Main IoT-Lite Hash Function (Cortex-M)
 *
 * Same sequential-dependent access pattern as the portable implementation.
 * No banks, no parallel lanes — ASIC-resistant by design.
 * ═══════════════════════════════════════════════════════════════════════ */
void cn_slow_hash_iot_lite(const void *data, size_t length, char *hash, int variant, int prehashed) {
    DTCM_BSS uint8_t text[INIT_SIZE_BYTE];
    DTCM_BSS uint8_t a[AES_BLOCK_SIZE];
    DTCM_BSS uint8_t b[AES_BLOCK_SIZE * 2];
    DTCM_BSS uint8_t c1[AES_BLOCK_SIZE];
    DTCM_BSS uint8_t c2[AES_BLOCK_SIZE];
    DTCM_BSS uint8_t d[AES_BLOCK_SIZE];
    uint64_t division_result = 0;
    uint64_t sqrt_result = 0;
    size_t i, j;
    union hash_state state;
    oaes_ctx *aes_ctx;

    static void (*const extra_hashes[4])(const void *, size_t, char *) = {
        hash_extra_blake, hash_extra_groestl, hash_extra_jh, hash_extra_skein
    };

    if (prehashed) {
        memcpy(&state, data, length);
    } else {
        hash_process(&state, data, length);
    }
    /* state.b[0..63] is the key area, state.b[64..191] is the init area */
    memcpy(text, state.b + 64, INIT_SIZE_BYTE);

    if (variant >= 2) {
        memcpy(b + AES_BLOCK_SIZE, state.b + 64, AES_BLOCK_SIZE);
        for (size_t k = 0; k < 8; k++) (b + AES_BLOCK_SIZE)[k] ^= (state.b + 80)[k];
        for (size_t k = 0; k < 8; k++) (b + AES_BLOCK_SIZE + 8)[k] ^= (state.b + 88)[k];
        division_result = state.w[12];
        sqrt_result = state.w[13];
    }

    aes_ctx = (oaes_ctx *) oaes_alloc();
    oaes_key_import_data(aes_ctx, state.b, AES_KEY_SIZE);
    for (i = 0; i < MEMORY_IOT_LITE / INIT_SIZE_BYTE; i++) {
        for (j = 0; j < INIT_SIZE_BLK; j++) {
            aesb_pseudo_round(&text[AES_BLOCK_SIZE * j], &text[AES_BLOCK_SIZE * j], aes_ctx->key->exp_data);
        }
        memcpy(&scratchpad[i * INIT_SIZE_BYTE], text, INIT_SIZE_BYTE);
    }

    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        a[i] = state.b[i] ^ state.b[AES_BLOCK_SIZE * 2 + i];
        b[i] = state.b[AES_BLOCK_SIZE + i] ^ state.b[AES_BLOCK_SIZE * 3 + i];
    }

    for (i = 0; i < ITER_IOT_LITE / 2; i++) {
        j = e2i_iot(a) * AES_BLOCK_SIZE;
        copy_block(c1, &scratchpad[j]);
        aesb_single_round(c1, c1, a);
        VARIANT2_SHUFFLE_ADD_CM(scratchpad, j);
        copy_block(&scratchpad[j], c1);
        xor_blocks(&scratchpad[j], b);

        j = e2i_iot(c1) * AES_BLOCK_SIZE;
        copy_block(c2, &scratchpad[j]);

        if (variant >= 2) {
            ((uint64_t *)c2)[0] ^= division_result ^ (sqrt_result << 32);
            const uint64_t dividend = ((uint64_t *)c1)[1];
            const uint32_t divisor = (((uint64_t *)c1)[0] + (uint32_t)(sqrt_result << 1)) | 0x80000001UL;
            division_result = ((uint32_t)(dividend / divisor)) + (((uint64_t)(dividend % divisor)) << 32);
            const uint64_t sqrt_input = ((uint64_t *)c1)[0] + division_result;
            sqrt_result = sqrt_int64(sqrt_input);
            ((uint64_t *)c2)[0] ^= sqrt_result;
        }

        mul_iot(c1, c2, d);

        if (variant >= 2) {
            xor_blocks(&scratchpad[j ^ 0x10], d);
            xor_blocks(d, &scratchpad[j ^ 0x20]);
        }

        VARIANT2_SHUFFLE_ADD_CM(scratchpad, j);
        sum_half_blocks(a, d);
        swap_blocks(a, c1);
        xor_blocks(a, c2);
        copy_block(&scratchpad[j], c2);

        if (variant >= 2) {
            copy_block(b + AES_BLOCK_SIZE, b);
        }
        copy_block(b, a);
        copy_block(a, c1);
    }

    memcpy(text, state.b + 64, INIT_SIZE_BYTE);
    oaes_key_import_data(aes_ctx, &state.b[32], AES_KEY_SIZE);
    for (i = 0; i < MEMORY_IOT_LITE / INIT_SIZE_BYTE; i++) {
        for (j = 0; j < INIT_SIZE_BLK; j++) {
            xor_blocks(&text[j * AES_BLOCK_SIZE], &scratchpad[i * INIT_SIZE_BYTE + j * AES_BLOCK_SIZE]);
            aesb_pseudo_round(&text[AES_BLOCK_SIZE * j], &text[AES_BLOCK_SIZE * j], aes_ctx->key->exp_data);
        }
    }
    memcpy(state.b + 64, text, INIT_SIZE_BYTE);
    hash_permutation(&state);
    extra_hashes[state.b[0] & 3](&state, 200, hash);
    oaes_free((OAES_CTX **) &aes_ctx);
}
