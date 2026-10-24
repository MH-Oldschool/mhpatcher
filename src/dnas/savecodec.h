/* Monster Hunter save obfuscation: a 16-bit XOR stream with an LCG key
 * schedule and a 16-bit additive checksum. Used by the MHG and MH1 (Europe)
 * game saves and the MH2 patch.bin. */
#ifndef DNAS_SAVECODEC_H
#define DNAS_SAVECODEC_H

#include <stddef.h>
#include <stdint.h>

/* MH2 standalone patch.bin: 0x40-byte header + 0x10040 encoded payload. */
#define MH2_PATCHBIN_SIZE 0x10080
#define MH2_PAYLOAD_SIZE  0x10040
#define MH2_HEADER_SIZE   0x40

/* MHG whole game save file BISLPM-65869MHG. */
#define MHG_SAVE_SIZE     0x130d0
#define MHG_SAVE_WORDS    0x9860   /* encoded 16-bit words after 8-byte header */

/* MH1 (Europe) whole game save file BESLES-52707MH. */
#define MH1E_SAVE_SIZE    0x11c0c
#define MH1E_SAVE_WORDS   0x8dfe

/* 16-bit additive checksum over a word range (sum of little-endian u16). */
uint16_t mh_checksum(const uint8_t *data, size_t words);

/* Decode/encode a game save in place (8-byte header + `words` 16-bit words,
 * MHG_SAVE_WORDS or MH1E_SAVE_WORDS). Returns 1 on success. decode validates
 * version(0x0100)/tag(0x5963)/checksum. */
int mh_save_decode(uint8_t *buf, size_t words);
void mh_save_encode(uint8_t *buf, size_t words, uint16_t seed);

/* Decode/encode the MH2 patch.bin payload. `file` is MH2_PATCHBIN_SIZE.
 * decode writes MH2_PAYLOAD_SIZE plaintext to payload; returns 1 on success.
 * encode reads MH2_PAYLOAD_SIZE plaintext and writes MH2_PATCHBIN_SIZE. */
int  mh2_patchbin_decode(const uint8_t *file, uint8_t *payload);
void mh2_patchbin_encode(const uint8_t *payload, uint8_t *file,
                         const uint8_t header_template[MH2_HEADER_SIZE],
                         uint16_t seed);

#endif
