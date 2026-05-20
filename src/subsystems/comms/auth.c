/**
 * @file auth.c
 * @brief HMAC-SHA256/8 for TT&C v2 air-layer frame authentication (§4.7).
 *
 * STM32L476 does not carry the HASH peripheral (only L4P5/L4Q5 do), so
 * this file provides a self-contained software SHA-256 (FIPS 180-4).
 * Code size is ~1.2 kB Thumb-2; latency is ~0.3 ms per frame at 80 MHz.
 *
 * The PSK is defined in the build-generated psk.h (excluded from VCS).
 * Build system generates it from the POCAT_PSK environment variable:
 *   POCAT_PSK=<32 hex chars> ./build.sh
 */

#include "auth.h"
#include "psk.h"   /* generated: static const uint8_t COMMS_PSK[AUTH_PSK_LEN]; */
#include <string.h>
#include <stddef.h>

/* ---- SHA-256 (FIPS 180-4) ---- */

#define SHA256_BLOCK  64u
#define SHA256_DIGEST 32u

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t  buf[SHA256_BLOCK];
    uint8_t  buf_len;
} sha256_ctx_t;

static inline uint32_t rotr32(uint32_t x, uint8_t n) { return (x >> n) | (x << (32u - n)); }

static void sha256_transform(uint32_t state[8], const uint8_t block[SHA256_BLOCK])
{
    uint32_t w[64];
    for (uint8_t i = 0; i < 16u; i++) {
        w[i] = ((uint32_t)block[i*4]   << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] <<  8) |
               (uint32_t) block[i*4+3];
    }
    for (uint8_t i = 16u; i < 64u; i++) {
        uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2],  17) ^ rotr32(w[i-2],  19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (uint8_t i = 0; i < 64u; i++) {
        uint32_t S1    = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch    = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0    = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bit_count = 0;
    ctx->buf_len   = 0;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        ctx->buf[ctx->buf_len++] = data[i];
        ctx->bit_count += 8u;
        if (ctx->buf_len == SHA256_BLOCK) {
            sha256_transform(ctx->state, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out[SHA256_DIGEST])
{
    uint64_t bc = ctx->bit_count;

    /* Padding: 0x80 then zeros then 64-bit big-endian bit count. */
    ctx->buf[ctx->buf_len++] = 0x80u;
    if (ctx->buf_len > 56u) {
        while (ctx->buf_len < SHA256_BLOCK) { ctx->buf[ctx->buf_len++] = 0x00u; }
        sha256_transform(ctx->state, ctx->buf);
        ctx->buf_len = 0;
    }
    while (ctx->buf_len < 56u) { ctx->buf[ctx->buf_len++] = 0x00u; }
    for (int8_t i = 7; i >= 0; i--) {
        ctx->buf[ctx->buf_len++] = (uint8_t)(bc >> (i * 8));
    }
    sha256_transform(ctx->state, ctx->buf);

    for (uint8_t i = 0; i < 8u; i++) {
        out[i*4]   = (uint8_t)(ctx->state[i] >> 24);
        out[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        out[i*4+2] = (uint8_t)(ctx->state[i] >>  8);
        out[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}

/* ---- HMAC-SHA256/8 ---- */

static void hmac_sha256_8(const uint8_t key[AUTH_PSK_LEN],
                           const uint8_t *data1, uint8_t len1,
                           const uint8_t *data2, uint8_t len2,
                           uint8_t out[AUTH_TAG_LEN])
{
    /* HMAC with a 16-byte key: no key hashing needed (key < block size). */
    uint8_t k_ipad[SHA256_BLOCK];
    uint8_t k_opad[SHA256_BLOCK];

    memset(k_ipad, 0x36u, SHA256_BLOCK);
    memset(k_opad, 0x5Cu, SHA256_BLOCK);
    for (uint8_t i = 0; i < AUTH_PSK_LEN; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }

    /* inner = SHA256(k_ipad || data1 || data2) */
    uint8_t inner[SHA256_DIGEST];
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, SHA256_BLOCK);
    sha256_update(&ctx, data1, len1);
    if (data2 != NULL && len2 > 0u) {
        sha256_update(&ctx, data2, len2);
    }
    sha256_final(&ctx, inner);

    /* outer = SHA256(k_opad || inner) — first 8 bytes = our tag */
    uint8_t outer[SHA256_DIGEST];
    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, SHA256_BLOCK);
    sha256_update(&ctx, inner, SHA256_DIGEST);
    sha256_final(&ctx, outer);

    memcpy(out, outer, AUTH_TAG_LEN);
}

/* ---- Public API ---- */

void auth_tag(const uint8_t *header, const uint8_t *payload,
              uint8_t payload_len, uint8_t out_tag[AUTH_TAG_LEN])
{
    /* header is 5 bytes; AUTH_TAG_PRESENT must already be set in header[2] */
    hmac_sha256_8(COMMS_PSK, header, 5u, payload, payload_len, out_tag);
}

bool auth_verify(const uint8_t *header, const uint8_t *payload,
                 uint8_t payload_len, const uint8_t tag[AUTH_TAG_LEN])
{
    uint8_t expected[AUTH_TAG_LEN];
    auth_tag(header, payload, payload_len, expected);

    /* Constant-time compare — prevents timing oracle attacks. */
    uint8_t diff = 0;
    for (uint8_t i = 0; i < AUTH_TAG_LEN; i++) {
        diff |= expected[i] ^ tag[i];
    }
    return (diff == 0u);
}
