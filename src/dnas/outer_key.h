/* The console "outer" RC6 key and IV (16 + 16 bytes) derived from the i.Link
 * ID, as done by symbol16062 in the MH1 install overlay. */
#ifndef DNAS_OUTER_KEY_H
#define DNAS_OUTER_KEY_H

#include <stdint.h>

/* ilink: 8 bytes as stored in NVM. out: 32 bytes (key[0..15] || iv[16..31]). */
void dnas_outer_key(const uint8_t ilink[8], uint8_t out[32]);

#endif
