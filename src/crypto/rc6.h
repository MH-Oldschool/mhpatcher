/* RC6-32/20/16 in CBC mode: 16-byte key and blocks, little-endian words. */
#ifndef DNAS_RC6_H
#define DNAS_RC6_H

#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t s[44]; } rc6_key;

void rc6_key_setup(rc6_key *k, const uint8_t key[16]);

/* key, iv: 16 bytes. len must be a multiple of 16. in/out may overlap. */
void rc6_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
                     const uint8_t *in, uint8_t *out, size_t len);
void rc6_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                     const uint8_t *in, uint8_t *out, size_t len);

#endif
