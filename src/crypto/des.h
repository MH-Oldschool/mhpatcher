/* Single DES in CBC mode without padding (as `openssl enc -des-cbc -nopad`). */
#ifndef DNAS_DES_H
#define DNAS_DES_H

#include <stddef.h>
#include <stdint.h>

/* key, iv: 8 bytes each. len must be a multiple of 8. in/out may overlap. */
void des_cbc_encrypt(const uint8_t key[8], const uint8_t iv[8],
                     const uint8_t *in, uint8_t *out, size_t len);
void des_cbc_decrypt(const uint8_t key[8], const uint8_t iv[8],
                     const uint8_t *in, uint8_t *out, size_t len);

#endif
