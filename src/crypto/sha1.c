#include "sha1.h"
#include <string.h>

static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1_block(sha1_ctx *c, const uint8_t *p) {
    uint32_t w[80];
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (i = 16; i < 80; i++)
        w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3], f = c->h[4];
    for (i = 0; i < 80; i++) {
        uint32_t k, t;
        if (i < 20)       { t = (b & d) | (~b & e);          k = 0x5a827999; }
        else if (i < 40)  { t = b ^ d ^ e;                   k = 0x6ed9eba1; }
        else if (i < 60)  { t = (b & d) | (b & e) | (d & e); k = 0x8f1bbcdc; }
        else              { t = b ^ d ^ e;                   k = 0xca62c1d6; }
        uint32_t tmp = rol(a, 5) + t + f + k + w[i];
        f = e; e = d; d = rol(b, 30); b = a; a = tmp;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += d; c->h[3] += e; c->h[4] += f;
}

void sha1_init(sha1_ctx *c) {
    c->h[0] = 0x67452301; c->h[1] = 0xefcdab89; c->h[2] = 0x98badcfe;
    c->h[3] = 0x10325476; c->h[4] = 0xc3d2e1f0;
    c->len = 0; c->fill = 0;
}

void sha1_update(sha1_ctx *c, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    c->len += len;
    while (len) {
        size_t n = 64 - c->fill;
        if (n > len) n = len;
        memcpy(c->block + c->fill, p, n);
        c->fill += n; p += n; len -= n;
        if (c->fill == 64) { sha1_block(c, c->block); c->fill = 0; }
    }
}

void sha1_final(sha1_ctx *c, uint8_t out[20]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    uint8_t zero = 0;
    while (c->fill != 56) sha1_update(c, &zero, 1);
    uint8_t lenbuf[8];
    int i;
    for (i = 0; i < 8; i++) lenbuf[i] = (uint8_t)(bits >> (56 - 8*i));
    sha1_update(c, lenbuf, 8);
    for (i = 0; i < 5; i++) {
        out[i*4]   = (uint8_t)(c->h[i] >> 24);
        out[i*4+1] = (uint8_t)(c->h[i] >> 16);
        out[i*4+2] = (uint8_t)(c->h[i] >> 8);
        out[i*4+3] = (uint8_t)(c->h[i]);
    }
}

void sha1(const void *data, size_t len, uint8_t out[20]) {
    sha1_ctx c; sha1_init(&c); sha1_update(&c, data, len); sha1_final(&c, out);
}
