/* Standard SHA-1 (FIPS 180-1), not Sony's modified DNAS variant; the outer
 * key derivation uses the standard one. */
#ifndef DNAS_SHA1_H
#define DNAS_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[5];
    uint64_t len;        /* total message length in bytes */
    uint8_t  block[64];
    size_t   fill;       /* bytes currently buffered in block[] */
} sha1_ctx;

void sha1_init(sha1_ctx *c);
void sha1_update(sha1_ctx *c, const void *data, size_t len);
void sha1_final(sha1_ctx *c, uint8_t out[20]);
void sha1(const void *data, size_t len, uint8_t out[20]);

#endif
