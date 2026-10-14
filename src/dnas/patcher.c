#include "patcher.h"
#include "outer_key.h"
#include "baked_data.h"
#include "../crypto/rc6.h"
#include <string.h>

/* where the blob and its 0x40-byte metadata block sit in the decoded data */
#define MH2_BLOB_OFF   0x40
#define MHG_BLOB_OFF   0x208
#define MHG_META_OFF   0x10208

/* tag inside the session header plaintext */
static const char SESSION_TAG[16] = {
    'd','3','7','b','8','2','a','9','3','d','8','3','1','a','7','4' };

/* Re-seal the console session header of a blob that is already in place at
 * blob_ptr. Rewrites only [0x180,0x200). Returns 1 on success. */
static int reseal_header_in_place(mh_game game, const uint8_t ilink[8],
                                  uint8_t *blob_ptr) {
    const uint8_t *hdr_plain = (game == GAME_MH2) ? mh2_hdr_plain : mhg_hdr_plain;
    /* sanity check on the baked data */
    if (memcmp(hdr_plain + 32, SESSION_TAG, 16) != 0) return 0;

    uint8_t okey[32];
    dnas_outer_key(ilink, okey);
    uint8_t sealed[128];
    rc6_cbc_encrypt(okey, okey + 16, hdr_plain, sealed, 128);
    memcpy(blob_ptr + 0x180, sealed, 128);
    return 1;
}

uint32_t patcher_reseal(mh_game game, const uint8_t ilink[8], uint8_t *out) {
    const uint8_t *blob = (game == GAME_MH2) ? mh2_blob : mhg_blob;
    uint32_t len        = (game == GAME_MH2) ? MH2_BLOB_LEN : MHG_BLOB_LEN;
    memcpy(out, blob, len);
    if (!reseal_header_in_place(game, ilink, out)) return 0;
    return len;
}

int patcher_build_mh2_patchbin(const uint8_t ilink[8], uint16_t seed,
                               uint8_t out[MH2_PATCHBIN_SIZE]) {
    /* everything outside the session header stays as in the official patch */
    static uint8_t payload[MH2_PAYLOAD_SIZE];
    memcpy(payload, mh2_payload, MH2_PAYLOAD_SIZE);
    if (!reseal_header_in_place(GAME_MH2, ilink, payload + MH2_BLOB_OFF)) return 0;
    mh2_patchbin_encode(payload, out, mh2_meta, seed);
    return 1;
}

int patcher_mhg_inject(const uint8_t ilink[8], uint8_t *decoded_save) {
    /* the rest of the player's save is left untouched */
    memcpy(decoded_save + MHG_BLOB_OFF, mhg_blob, MHG_BLOB_LEN);
    memcpy(decoded_save + MHG_META_OFF, mhg_meta, MH2_HEADER_SIZE);
    return reseal_header_in_place(GAME_MHG, ilink, decoded_save + MHG_BLOB_OFF);
}
