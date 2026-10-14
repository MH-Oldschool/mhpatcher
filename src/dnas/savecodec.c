#include "savecodec.h"
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* key = 1 if (key & 0xffff)==0; return (176 * (key & 0xffff)) % 0xff53 */
static uint16_t next_key(uint16_t key) {
    uint32_t k = key;
    if (k == 0) k = 1;
    return (uint16_t)((176u * k) % 0xff53u);
}

uint16_t mh_checksum(const uint8_t *data, size_t words) {
    uint16_t sum = 0;
    size_t i;
    for (i = 0; i < words; i++) sum = (uint16_t)(sum + rd16(data + 2*i));
    return sum;
}

/* XOR a run of `words` 16-bit words starting at byte offset, evolving key from
 * seed. Symmetric: the same call encodes and decodes. */
static void xor_words(uint8_t *buf, size_t offset, size_t words, uint16_t seed) {
    uint16_t key = seed;
    size_t i;
    for (i = 0; i < words; i++) {
        uint8_t *p = buf + offset + 2*i;
        wr16(p, (uint16_t)(rd16(p) ^ key));
        key = next_key(key);
    }
}

int mhg_save_decode(uint8_t *buf) {
    uint16_t version = rd16(buf + 0);
    uint16_t seed    = rd16(buf + 2);
    uint16_t stored  = rd16(buf + 4);
    uint16_t tag     = rd16(buf + 6);
    xor_words(buf, 8, MHG_SAVE_WORDS, seed);
    uint16_t sum = mh_checksum(buf + 8, MHG_SAVE_WORDS);
    return (version == 0x0100 && tag == 0x5963 && sum == stored) ? 1 : 0;
}

void mhg_save_encode(uint8_t *buf, uint16_t seed) {
    uint16_t sum = mh_checksum(buf + 8, MHG_SAVE_WORDS);
    wr16(buf + 0, 0x0100);
    wr16(buf + 2, seed);
    wr16(buf + 4, sum);
    wr16(buf + 6, 0x5963);
    xor_words(buf, 8, MHG_SAVE_WORDS, seed);
}

int mh2_patchbin_decode(const uint8_t *file, uint8_t *payload) {
    uint16_t version = rd16(file + 0);
    uint16_t seed    = rd16(file + 2);
    uint16_t stored  = rd16(file + 4);
    memcpy(payload, file + MH2_HEADER_SIZE, MH2_PAYLOAD_SIZE);
    xor_words(payload, 0, MH2_PAYLOAD_SIZE / 2, seed);
    uint16_t sum = mh_checksum(payload, MH2_PAYLOAD_SIZE / 2);
    return (version == 0x0001 && sum == stored) ? 1 : 0;
}

void mh2_patchbin_encode(const uint8_t *payload, uint8_t *file,
                         const uint8_t header_template[MH2_HEADER_SIZE],
                         uint16_t seed) {
    memcpy(file, header_template, MH2_HEADER_SIZE);
    wr16(file + 0, 0x0001);
    wr16(file + 2, seed);
    wr16(file + 4, mh_checksum(payload, MH2_PAYLOAD_SIZE / 2));
    memcpy(file + MH2_HEADER_SIZE, payload, MH2_PAYLOAD_SIZE);
    xor_words(file, MH2_HEADER_SIZE, MH2_PAYLOAD_SIZE / 2, seed);
}
