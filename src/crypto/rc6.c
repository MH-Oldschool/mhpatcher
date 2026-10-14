#include "rc6.h"
#include <string.h>

static uint32_t rol(uint32_t x, uint32_t n) {
    n &= 31; return n ? ((x << n) | (x >> (32 - n))) : x;
}
static uint32_t ror(uint32_t x, uint32_t n) {
    n &= 31; return n ? ((x >> n) | (x << (32 - n))) : x;
}
static uint32_t ld32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void st32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
/* f(x) = x*(2x+1), low 32 bits */
static uint32_t fx(uint32_t x) { return x * (2u * x + 1u); }

void rc6_key_setup(rc6_key *k, const uint8_t key[16]) {
    uint32_t l[4], i;
    for (i = 0; i < 4; i++) l[i] = ld32(key + 4*i);
    k->s[0] = 0xb7e15163;
    for (i = 1; i < 44; i++) k->s[i] = k->s[i-1] + 0x9e3779b9;
    uint32_t a = 0, b = 0, ii = 0, jj = 0, n;
    for (n = 0; n < 3 * 44; n++) {
        a = k->s[ii] = rol(k->s[ii] + a + b, 3);
        b = l[jj]    = rol(l[jj] + a + b, a + b);
        ii = (ii + 1) % 44;
        jj = (jj + 1) % 4;
    }
}

static void enc_block(const rc6_key *k, uint8_t *blk) {
    uint32_t a = ld32(blk), b = ld32(blk+4), c = ld32(blk+8), d = ld32(blk+12);
    int i;
    b += k->s[0];
    d += k->s[1];
    for (i = 1; i <= 20; i++) {
        uint32_t t = rol(fx(b), 5);
        uint32_t u = rol(fx(d), 5);
        a = rol(a ^ t, u) + k->s[2*i];
        c = rol(c ^ u, t) + k->s[2*i+1];
        uint32_t tmp = a; a = b; b = c; c = d; d = tmp;
    }
    a += k->s[42];
    c += k->s[43];
    st32(blk, a); st32(blk+4, b); st32(blk+8, c); st32(blk+12, d);
}

static void dec_block(const rc6_key *k, uint8_t *blk) {
    uint32_t a = ld32(blk), b = ld32(blk+4), c = ld32(blk+8), d = ld32(blk+12);
    int i;
    c -= k->s[43];
    a -= k->s[42];
    for (i = 20; i >= 1; i--) {
        uint32_t tmp = d; d = c; c = b; b = a; a = tmp;
        uint32_t u = rol(fx(d), 5);
        uint32_t t = rol(fx(b), 5);
        c = ror(c - k->s[2*i+1], t) ^ u;
        a = ror(a - k->s[2*i], u) ^ t;
    }
    d -= k->s[1];
    b -= k->s[0];
    st32(blk, a); st32(blk+4, b); st32(blk+8, c); st32(blk+12, d);
}

void rc6_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
                     const uint8_t *in, uint8_t *out, size_t len) {
    rc6_key k; rc6_key_setup(&k, key);
    uint8_t chain[16]; memcpy(chain, iv, 16);
    size_t off;
    for (off = 0; off < len; off += 16) {
        uint8_t blk[16];
        int j; for (j = 0; j < 16; j++) blk[j] = in[off+j] ^ chain[j];
        enc_block(&k, blk);
        memcpy(out + off, blk, 16);
        memcpy(chain, blk, 16);
    }
}

void rc6_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                     const uint8_t *in, uint8_t *out, size_t len) {
    rc6_key k; rc6_key_setup(&k, key);
    uint8_t chain[16]; memcpy(chain, iv, 16);
    size_t off;
    for (off = 0; off < len; off += 16) {
        uint8_t cph[16]; memcpy(cph, in + off, 16);
        uint8_t blk[16]; memcpy(blk, cph, 16);
        dec_block(&k, blk);
        int j; for (j = 0; j < 16; j++) out[off+j] = blk[j] ^ chain[j];
        memcpy(chain, cph, 16);
    }
}
