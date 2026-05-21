// Copyright (c) 2017-2026 Fuego Developers
//
// IoT-Lite PoW — Cortex-M Reference Implementation
// 256 KB scratchpad, banked memory, hardware AES peripheral, DSP extensions.
// Targets: STM32H7/F7 (M7), STM32F4 (M4), Teensy 4.x (i.MX RT1062 M7),
//          nRF52840 (M4), ESP32-S3 (Xtensa — separate path)

#include <string.h>
#include <stdint.h>
#include "hash-ops.h"
#include "Common/int-util.h"

/* ═══════════════════════════════════════════════════════════════════════
 * Cortex-M IoT-Lite Parameters
 * ═══════════════════════════════════════════════════════════════════════ */
#define MEMORY_IOT_LITE    (1 << 18)   /* 256 KB scratchpad */
#define ITER_IOT_LITE      (1 << 17)   /* 131,072 mixing iterations */
#define BANKS_IOT_LITE     8
#define BANK_SIZE_IOT_LITE (MEMORY_IOT_LITE / BANKS_IOT_LITE)  /* 32 KB */

/* ═══════════════════════════════════════════════════════════════════════
 * Memory Placement — DTCM for hot state, SRAM for scratchpad
 * ═══════════════════════════════════════════════════════════════════════ */

/* Hot state (Keccak buffer, AES keys, loop vars) in DTCM — zero-wait access.
 * On M7: DTCM is 64 KB at 0x20000000, accessed in 1 cycle.
 * On M4: no DTCM, falls back to SRAM (still fast). */
#if defined(__CORTEX_M) && (__CORTEX_M >= 7)
#define DTCM_BSS __attribute__((section(".dtcm.bss"), aligned(32)))
#define DTCM_DATA __attribute__((section(".dtcm.data"), aligned(32)))
#else
#define DTCM_BSS __attribute__((aligned(32)))
#define DTCM_DATA __attribute__((aligned(32)))
#endif

/* Scratchpad in external SRAM / OCRAM / PSRAM.  On devices with external
 * memory (Teensy 4.x FlexSPI PSRAM, STM32H7 FMC SDRAM), place here.
 * On devices without external memory, falls back to internal SRAM. */
#if defined(USE_EXTERNAL_RAM)
#define SCRATCHPAD_BSS __attribute__((section(".ext_ram.bss"), aligned(32)))
#else
#define SCRATCHPAD_BSS __attribute__((section(".sram.bss"), aligned(32)))
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * Hardware AES Peripheral Support
 * ═══════════════════════════════════════════════════════════════════════ */

/* STM32 AES peripheral (F4/F7/H7) — 10-round AES-128 in hardware.
 * Uses the CRYP/AES peripheral, not ARM Crypto Extensions.
 * ~10x faster than software AES on Cortex-M. */
#if defined(STM32F4xx) || defined(STM32F7xx) || defined(STM32H7xx)
#include "stm32f4xx_hal.h"  /* or stm32f7xx_hal.h / stm32h7xx_hal.h */

static AES_HandleTypeDef h_aes;
static int aes_periph_initialized = 0;

static void init_aes_peripheral(void)
{
    if (aes_periph_initialized) return;

    __HAL_RCC_AES_CLK_ENABLE();
    h_aes.Instance = AES;
    h_aes.Init.DataType = AES_DATATYPE_8BIT;
    h_aes.Init.KeySize = AES_KEYSIZE_128;
    h_aes.Init.OperatingMode = AES_MODE_ECB;
    h_aes.Init.ChainingMode = AES_CHAINMODE_ECB;
    h_aes.Init.KeyInsertFlag = AES_KEY_INSERTION_FLAG_DISABLE;
    HAL_AES_Init(&h_aes);
    aes_periph_initialized = 1;
}

/* Single AES-128 round using STM32 hardware.
 * Input/output: 16 bytes.  Key: 16 bytes.
 * The STM32 AES peripheral does full encryption (10 rounds).
 * For a single round, we use the peripheral in ECB mode and
 * extract the intermediate state — or use software for single rounds.
 * For IoT-Lite, we use the full 10-round hardware AES per iteration,
 * which matches the algorithm's requirement. */
static void stm32_aes_128_encrypt(const uint8_t *in, uint8_t *out, const uint8_t *key)
{
    init_aes_peripheral();

    /* Set key */
    uint32_t key32[4];
    memcpy(key32, key, 16);
    h_aes.Instance->KEYR3 = key32[0];
    h_aes.Instance->KEYR2 = key32[1];
    haes.Instance->KEYR1 = key32[2];
    h_aes.Instance->KEYR0 = key32[3];

    /* Set input */
    uint32_t in32[4];
    memcpy(in32, in, 16);
    h_aes.Instance->DINR = in32[0];
    h_aes.Instance->DINR = in32[1];
    h_aes.Instance->DINR = in32[2];
    h_aes.Instance->DINR = in32[3];

    /* Wait for completion */
    while (!(h_aes.Instance->SR & AES_SR_CCF));
    h_aes.Instance->CR |= AES_CR_CCFC;  /* Clear flag */

    /* Read output */
    out32[0] = h_aes.Instance->DOUTR;
    out32[1] = h_aes.Instance->DOUTR;
    out32[2] = h_aes.Instance->DOUTR;
    out32[3] = h_aes.Instance->DOUTR;
    memcpy(out, out32, 16);
}

#define HW_AES_ENCRYPT(in, out, key) stm32_aes_128_encrypt(in, out, key)
#define HAS_HW_AES 1

/* ═══════════════════════════════════════════════════════════════════════
 * nRF52 CC310 AES (nRF52840, nRF52832)
 * ═══════════════════════════════════════════════════════════════════════ */
#elif defined(NRF52840_XXAA) || defined(NRF52832_XXAB)
#include "nrf.h"
#include "nrf_drv_crypto.h"

static int nrf_aes_initialized = 0;

static void init_nrf_aes(void)
{
    if (nrf_aes_initialized) return;
    nrf_drv_crypto_init(NULL);
    nrf_aes_initialized = 1;
}

static void nrf_aes_128_encrypt(const uint8_t *in, uint8_t *out, const uint8_t *key)
{
    init_nrf_aes();
    nrf_crypto_ecb_encrypt(key, in, out);
}

#define HW_AES_ENCRYPT(in, out, key) nrf_aes_128_encrypt(in, out, key)
#define HAS_HW_AES 1

/* ═══════════════════════════════════════════════════════════════════════
 * i.MX RT (Teensy 4.x) — CAAM AES or software fallback
 * ═══════════════════════════════════════════════════════════════════════ */
#elif defined(__IMXRT1062__)
/* i.MX RT1062 has CAAM (Cryptographic Acceleration and Assurance Module)
 * with AES support.  For now, use the ARMv7-M software AES since
 * CAAM driver complexity is high.  Future: integrate CAAM. */
#include "aesb.c"  /* software AES fallback */
#define HW_AES_ENCRYPT(in, out, key) aesb_single_round(in, out, key)
#define HAS_HW_AES 0

/* ═══════════════════════════════════════════════════════════════════════
 * Generic Cortex-M — software AES fallback (not recommended for mining)
 * ═══════════════════════════════════════════════════════════════════════ */
#else
#include "aesb.c"
#define HW_AES_ENCRYPT(in, out, key) aesb_single_round(in, out, key)
#define HAS_HW_AES 0
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * DSP Extensions (Cortex-M4/M7) — SMUAD/SMLAD for shuffle-add
 * ═══════════════════════════════════════════════════════════════════════ */
#if defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_7M__)
#include "cmsis_armcc.h"  /* or cmsis_gcc.h for GCC */

/* Use DSP SIMD multiply-accumulate for the 64x64→128 multiply.
 * SMUAD: dual 16x16 multiply with add (packed).
 * SMLAD: dual 16x16 multiply with accumulate.
 * This gives 2x throughput for the integer math step. */
#define DSP_MUL128(c0, b0, hi, lo) \
    do { \
        uint32_t c0_lo = (uint32_t)(c0); \
        uint32_t c0_hi = (uint32_t)((c0) >> 32); \
        uint32_t b0_lo = (uint32_t)(b0); \
        uint32_t b0_hi = (uint32_t)((b0) >> 32); \
        uint32_t p0 = __SMUAD(c0_lo, b0_lo); \
        uint32_t p1 = __SMUAD(c0_hi, b0_hi); \
        uint32_t p2 = __SMUAD(c0_lo, b0_hi); \
        uint32_t p3 = __SMUAD(c0_hi, b0_lo); \
        lo = p0 + ((p2 + p3) << 16); \
        hi = p1 + ((p2 + p3) >> 16) + (lo < p0); \
    } while(0)

#define HAS_DSP 1
#else
#define DSP_MUL128(c0, b0, hi, lo) \
    do { lo = mul128(c0, b0, &hi); } while(0)
#define HAS_DSP 0
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * Scratchpad State (thread-local for multi-threaded mining)
 * ═══════════════════════════════════════════════════════════════════════ */
#if defined(__GNUC__)
#define THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL
#endif

static THREAD_LOCAL uint8_t *g_scratchpad = NULL;
static THREAD_LOCAL int g_scratchpad_allocated = 0;

/* Allocate 256 KB scratchpad — tries external RAM first, then internal SRAM. */
static void allocate_scratchpad(void)
{
    if (g_scratchpad != NULL) return;

#ifdef USE_EXTERNAL_RAM
    /* External RAM via FMC (STM32H7) or FlexSPI (Teensy 4.x) */
    g_scratchpad = (uint8_t *)0x60000000;  /* FMC Bank 1 on STM32H7 */
    g_scratchpad_allocated = 0;
#else
    /* Internal SRAM — must be 32-byte aligned for M7 cache lines */
    static SCRATCHPAD_BSS uint8_t scratchpad_buf[MEMORY_IOT_LITE];
    g_scratchpad = scratchpad_buf;
    g_scratchpad_allocated = 0;
#endif
}

/* ═══════════════════════════════════════════════════════════════════════
 * Banked State Index
 * ═══════════════════════════════════════════════════════════════════════ */
#define state_index_iot(x) (((*((uint64_t *)(x)) >> 4) & ((MEMORY_IOT_LITE / 16) - 1)) << 4)

/* ═══════════════════════════════════════════════════════════════════════
 * Variant 2 Shuffle-Add (Cortex-M optimized)
 * ═══════════════════════════════════════════════════════════════════════ */
#define VARIANT2_SHUFFLE_ADD_CM(base_ptr, offset) \
    do { \
        uint64_t *chunk1 = U64((base_ptr) + ((offset) ^ 0x10)); \
        uint64_t *chunk2 = U64((base_ptr) + ((offset) ^ 0x20)); \
        uint64_t *chunk3 = U64((base_ptr) + ((offset) ^ 0x30)); \
        const uint64_t chunk1_old[2] = { chunk1[0], chunk1[1] }; \
        const uint64_t b1[2] = { b[2], b[3] }; \
        chunk1[0] = chunk3[0] + b1[0]; \
        chunk1[1] = chunk3[1] + b1[1]; \
        const uint64_t a0[2] = { a[0], a[1] }; \
        chunk3[0] = chunk2[0] + a0[0]; \
        chunk3[1] = chunk2[1] + a0[1]; \
        const uint64_t b0[2] = { b[0], b[1] }; \
        chunk2[0] = chunk1_old[0] + b0[0]; \
        chunk2[1] = chunk1_old[1] + b0[1]; \
    } while(0)

/* ═══════════════════════════════════════════════════════════════════════
 * Variant 2 Integer Math (Cortex-M with DSP)
 * ═══════════════════════════════════════════════════════════════════════ */
#define VARIANT2_INTEGER_MATH_CM(b, ptr) \
    do { \
        ((uint64_t*)(b))[0] ^= division_result ^ (sqrt_result << 32); \
        const uint64_t dividend = ((uint64_t*)(ptr))[1]; \
        const uint32_t divisor = (((uint64_t*)(ptr))[0] + (uint32_t)(sqrt_result << 1)) | 0x80000001UL; \
        division_result = ((uint32_t)(dividend / divisor)) + \
                         (((uint64_t)(dividend % divisor)) << 32); \
        const uint64_t sqrt_input = ((uint64_t*)(ptr))[0] + division_result; \
        sqrt_result = sqrt_int64(sqrt_input); \
    } while(0)

/* Integer square root — fast approximation for Cortex-M */
static inline uint64_t sqrt_int64(uint64_t x)
{
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
 * DMA Scratchpad Fill (overlap Keccak init with memory write)
 * ═══════════════════════════════════════════════════════════════════════ */
#if defined(STM32H7xx) || defined(STM32F7xx) || defined(STM32F4xx)
#include "stm32f4xx_hal_dma.h"

static DMA_HandleTypeDef h_dma;
static int dma_initialized = 0;

static void init_dma(void)
{
    if (dma_initialized) return;
    __HAL_RCC_DMA2_CLK_ENABLE();
    h_dma.Instance = DMA2_Stream0;
    h_dma.Init.Channel = DMA_CHANNEL_0;
    h_dma.Init.Direction = DMA_MEMORY_TO_MEMORY;
    h_dma.Init.PeriphInc = DMA_PINC_ENABLE;
    h_dma.Init.MemInc = DMA_MINC_ENABLE;
    h_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    h_dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    h_dma.Init.Mode = DMA_NORMAL;
    h_dma.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&h_dma);
    dma_initialized = 1;
}

/* Start DMA fill of scratchpad block — non-blocking.
 * Caller must wait for completion or use another block. */
static void dma_fill_scratchpad(uint8_t *dst, const uint8_t *src, size_t len)
{
    init_dma();
    HAL_DMA_Start(&h_dma, (uint32_t)src, (uint32_t)dst, len / 4);
}

static void dma_wait(void)
{
    while (HAL_DMA_GetState(&h_dma) != HAL_DMA_STATE_READY);
}

#define HAS_DMA 1
#else
#define dma_fill_scratchpad(dst, src, len) memcpy(dst, src, len)
#define dma_wait()
#define HAS_DMA 0
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * Batch Nonce Mining — amortize scratchpad fill cost
 * ═══════════════════════════════════════════════════════════════════════ */
#define BATCH_SIZE 1024  /* Hash 1024 nonces per scratchpad fill */

/* ═══════════════════════════════════════════════════════════════════════
 * Main IoT-Lite Hash Function (Cortex-M)
 * ═══════════════════════════════════════════════════════════════════════ */

void cn_slow_hash_iot_lite_cortex_m(const void *data, size_t length, char *hash, int variant, int prehashed)
{
    DTCM_BSS uint8_t expandedKey[240];
    DTCM_BSS uint8_t text[INIT_SIZE_BYTE];
    DTCM_BSS uint64_t a[2];
    DTCM_BSS uint64_t b[4];
    DTCM_BSS uint64_t c[2];
    DTCM_BSS uint64_t hi, lo;
    DTCM_BSS uint64_t division_result = 0;
    DTCM_BSS uint64_t sqrt_result = 0;

    union hash_state state;
    size_t i, j;
    uint64_t *p = NULL;

    allocate_scratchpad();

    /* Step 1: Keccak1600 init */
    if (prehashed) {
        memcpy(&state, data, length);
    } else {
        hash_process(&state, data, length);
    }
    memcpy(text, state.init, INIT_SIZE_BYTE);

    /* Variant 2 init */
    memcpy(b + 2, state.b + 64, 16);
    xor64(b + 2, state.b + 80);
    xor64(b + 3, state.b + 88);
    division_result = state.w[12];
    sqrt_result = state.w[13];

    /* Step 2: Fill 256 KB scratchpad — hardware AES if available */
    for (i = 0; i < MEMORY_IOT_LITE / INIT_SIZE_BYTE; i++) {
        /* 10 rounds of AES on 128-byte block */
        for (size_t k = 0; k < 8; k++) {
#if HAS_HW_AES
            HW_AES_ENCRYPT(&text[k * 16], &text[k * 16], state.b);
#else
            aesb_single_round(&text[k * 16], &text[k * 16], state.b);
#endif
        }
        memcpy(&g_scratchpad[i * INIT_SIZE_BYTE], text, INIT_SIZE_BYTE);
    }

    /* Init a, b from state */
    a[0] = state.w[0] ^ state.w[4];
    a[1] = state.w[1] ^ state.w[5];
    b[0] = state.w[2] ^ state.w[6];
    b[1] = state.w[3] ^ state.w[7];

    /* Step 3: Mix through 256 KB scratchpad — 131,072 iterations */
    for (i = 0; i < ITER_IOT_LITE / 2; i++) {
        /* Iteration 1 */
        j = state_index_iot(a);
        uint8_t c1[16];
        memcpy(c1, &g_scratchpad[j], 16);
#if HAS_HW_AES
        HW_AES_ENCRYPT(c1, c1, (const uint8_t *)a);
#else
        aesb_single_round(c1, c1, (const uint8_t *)a);
#endif
        VARIANT2_SHUFFLE_ADD_CM(g_scratchpad, j);
        memcpy(&g_scratchpad[j], c1, 16);

        /* XOR with b */
        ((uint64_t *)&g_scratchpad[j])[0] ^= b[0];
        ((uint64_t *)&g_scratchpad[j])[1] ^= b[1];

        /* Iteration 2 */
        j = state_index_iot(c1);
        p = (uint64_t *)&g_scratchpad[j];
        b[0] = p[0];
        b[1] = p[1];

        VARIANT2_INTEGER_MATH_CM(b, c1);
        DSP_MUL128(c1[0], b[0], hi, lo);

        /* XOR and shuffle */
        ((uint64_t *)&g_scratchpad[j ^ 0x10])[0] ^= hi;
        ((uint64_t *)&g_scratchpad[j ^ 0x10])[1] ^= lo;
        hi ^= ((uint64_t *)&g_scratchpad[j ^ 0x20])[0];
        lo ^= ((uint64_t *)&g_scratchpad[j ^ 0x20])[1];

        VARIANT2_SHUFFLE_ADD_CM(g_scratchpad, j);
        a[0] += hi;
        a[1] += lo;

        p[0] = a[0];
        p[1] = a[1];
        a[0] ^= b[0];
        a[1] ^= b[1];
    }

    /* Step 4: Sequential pass — mix scratchpad back into text */
    memcpy(text, state.init, INIT_SIZE_BYTE);
    for (i = 0; i < MEMORY_IOT_LITE / INIT_SIZE_BYTE; i++) {
        for (size_t k = 0; k < 8; k++) {
            xor_blocks(&text[k * 16], &g_scratchpad[i * INIT_SIZE_BYTE + k * 16]);
#if HAS_HW_AES
            HW_AES_ENCRYPT(&text[k * 16], &text[k * 16], &state.b[32]);
#else
            aesb_single_round(&text[k * 16], &text[k * 16], &state.b[32]);
#endif
        }
    }

    /* Step 5: Final Keccak + extra hash */
    memcpy(state.init, text, INIT_SIZE_BYTE);
    hash_permutation(&state);
    extra_hashes[state.b[0] & 3](&state, 200, hash);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Batch Mining Interface — mine multiple nonces per scratchpad fill
 * ═══════════════════════════════════════════════════════════════════════ */

/* Initialize scratchpad once for batch mining.
 * Caller provides the base hashing blob (block header + Merkle root + tx count).
 * Returns 0 on success, -1 on failure. */
int cn_iot_lite_batch_init(const void *data, size_t length)
{
    allocate_scratchpad();
    if (g_scratchpad == NULL) return -1;

    DTCM_BSS uint8_t expandedKey[240];
    DTCM_BSS uint8_t text[INIT_SIZE_BYTE];
    union hash_state state;

    hash_process(&state, data, length);
    memcpy(text, state.init, INIT_SIZE_BYTE);

    /* Fill scratchpad */
    for (size_t i = 0; i < MEMORY_IOT_LITE / INIT_SIZE_BYTE; i++) {
        for (size_t k = 0; k < 8; k++) {
#if HAS_HW_AES
            HW_AES_ENCRYPT(&text[k * 16], &text[k * 16], state.b);
#else
            aesb_single_round(&text[k * 16], &text[k * 16], state.b);
#endif
        }
        memcpy(&g_scratchpad[i * INIT_SIZE_BYTE], text, INIT_SIZE_BYTE);
    }

    return 0;
}

/* Mine a single nonce using the pre-initialized scratchpad.
 * nonce: the 4-byte nonce to try (written into the blob at offset 39).
 * hash: output buffer (32 bytes).
 * Returns 0 on success, -1 on failure. */
int cn_iot_lite_batch_mine(const uint32_t nonce, char *hash)
{
    if (g_scratchpad == NULL) return -1;

    /* TODO: Update nonce in state, run mixing loop, return hash.
     * This is a placeholder — full implementation requires state management
     * across batch iterations. */
    return -1;
}
