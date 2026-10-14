#include "outer_key.h"
#include "../crypto/des.h"
#include "../crypto/sha1.h"
#include <string.h>

/* Fixed inputs copied by symbol16062 from MH1 overlay VAs 0xa37ab0/ad0/ad8. */
static const uint8_t FIXED_INPUT[32] = {
    0xe8,0x4d,0xb7,0x62,0xb8,0x65,0xae,0xf3,0x52,0x86,0x68,0x9b,0xa7,0x25,0x6b,0x4b,
    0xfa,0xd4,0xba,0x5b,0xb3,0xc3,0x2d,0xca,0xcf,0x36,0x6b,0x16,0xc3,0xd5,0xba,0x5e };
static const uint8_t FIXED_IV[8]  = {0x81,0xbe,0x60,0xe7,0x1b,0xc7,0x3f,0xa4};
static const uint8_t FIXED_KEY[8] = {0x37,0x45,0xcd,0x63,0x10,0x19,0x0b,0xde};

void dnas_outer_key(const uint8_t ilink[8], uint8_t out[32]) {
    uint8_t first[32];
    des_cbc_encrypt(FIXED_KEY, FIXED_IV, FIXED_INPUT, first, 32);
    const uint8_t *second_key = first + 16;
    const uint8_t *second_iv  = first + 24;

    /* a282b8 response-byte permutation into record_dcd29 / record_57709 */
    uint8_t dcd29[8] = { ilink[0], 0x46, 0x00, 0x08, ilink[4], ilink[5], ilink[6], ilink[7] };
    uint8_t s57709[8] = { ilink[1], ilink[2], ilink[3], 0, 0, 0, 0, 0 };

    uint8_t h[16];
    h[0]=s57709[2]; h[1]=s57709[1]; h[2]=s57709[0];
    h[3]=dcd29[0];  h[4]=dcd29[7];  h[5]=dcd29[6]; h[6]=dcd29[5]; h[7]=dcd29[4];
    memset(h + 8, 0, 8);

    /* block = h || h[12:16] || h[8:12] || h[4:8] || h[0:4]  (32 bytes) */
    uint8_t block[32];
    memcpy(block +  0, h,       16);
    memcpy(block + 16, h + 12,  4);
    memcpy(block + 20, h +  8,  4);
    memcpy(block + 24, h +  4,  4);
    memcpy(block + 28, h +  0,  4);

    /* work = block*5 || block[0:8]  (168 bytes) */
    uint8_t work[168];
    int i;
    for (i = 0; i < 5; i++) memcpy(work + 32*i, block, 32);
    memcpy(work + 160, block, 8);

    uint8_t second[128];
    des_cbc_encrypt(second_key, second_iv, work, second, 128);

    uint8_t digest[20];
    sha1(work, 168, digest);

    /* opcode chain: 3,1,4,2,5,6,7,0 */
    static const int opcodes[8] = {3, 1, 4, 2, 5, 6, 7, 0};
    for (i = 0; i < 8; i++) {
        int op = opcodes[i];
        const uint8_t *key, *iv;
        if (op >= 1 && op <= 3) {
            key = second + op*16 + 8; iv = second + op*16;
            des_cbc_encrypt(key, iv, work, work, 168);
        } else if (op == 4 || op == 6 || op == 7) {
            key = second + op*16; iv = second + op*16 + 8;
            des_cbc_decrypt(key, iv, work, work, 168);
        } else if (op == 5) {
            des_cbc_encrypt(digest + 4, digest + 12, work, work, 168);
            des_cbc_decrypt(second + 5*16, second + 5*16 + 8, work, work, 168);
        } else { /* op == 0 */
            des_cbc_decrypt(digest, digest + 8, work, work, 168);
            des_cbc_encrypt(second + 8, second, work, work, 168);
        }
    }

    des_cbc_encrypt(first, first + 8, work, work, 168);
    memcpy(out, work + 136, 32);
}
