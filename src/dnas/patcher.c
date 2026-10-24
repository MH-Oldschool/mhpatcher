#include "patcher.h"
#include "outer_key.h"
#include "baked_data.h"
#include "../crypto/rc6.h"
#include <string.h>

/* where the blob and its 0x40-byte metadata block sit in the decoded data;
 * the MHG and MH1 (Europe) game saves share the layout */
#define MH2_BLOB_OFF   0x40
#define SAVE_BLOB_OFF  0x208
#define SAVE_META_OFF  0x10208

/* The patch header the game writes at boot (MHG 0x237940, MH1 Europe
 * 0x2482d0): its build stamp, "1000", length 0. Indexed by mh_game. */
static const char FRESH_NAME[3][11] = { "0601162208", "0411210839", "0502180939" };

/* the official patch of each game, indexed by mh_game */
static const struct {
    const uint8_t *blob, *hdr_plain, *meta;
    uint32_t len;
} BAKED[] = {
    { mh2_blob,  mh2_hdr_plain,  mh2_meta,  MH2_BLOB_LEN },
    { mhg_blob,  mhg_hdr_plain,  mhg_meta,  MHG_BLOB_LEN },
    { mh1e_blob, mh1e_hdr_plain, mh1e_meta, MH1E_BLOB_LEN },
};

/* tag inside the session header plaintext */
static const char SESSION_TAG[16] = {
    'd','3','7','b','8','2','a','9','3','d','8','3','1','a','7','4' };

/* Re-seal the console session header of a blob that is already in place at
 * blob_ptr. Rewrites only [0x180,0x200). Returns 1 on success. */
static int reseal_header_in_place(mh_game game, const uint8_t ilink[8],
                                  uint8_t *blob_ptr) {
    const uint8_t *hdr_plain = BAKED[game].hdr_plain;
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
    uint32_t len = BAKED[game].len;
    memcpy(out, BAKED[game].blob, len);
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

/* The patch header a new save holds: build stamp, "1000", length 0. */
static void fresh_meta(mh_game game, uint8_t meta[MH2_HEADER_SIZE]) {
    memset(meta, 0, MH2_HEADER_SIZE);
    memcpy(meta, FRESH_NAME[game], 10);
    memcpy(meta + 10, "1000", 4);
}

/* The file header of a new save's patch.bin: the seed (0x26d7) and filler
 * bytes come from the game's random stream, the same on every console. */
static const uint8_t MH2_FRESH_HDR[MH2_HEADER_SIZE] = {
    0x01,0x00,0xd7,0x26,0x54,0x68,0xba,0xd5,0xe0,0xfd,0x83,0xb8,0x3b,0x84,0x0e,0xb1,
    0x0b,0x59,0xd3,0x0c,0x2f,0xc0,0x9a,0xa5,0x9d,0x29,0xa6,0xbb,0xcf,0x44,0x97,0x41,
    0xfd,0xcb,0x5b,0xde,0x2d,0xdf,0x90,0x07,0xb5,0x9c,0xd9,0x1d,0x8a,0x61,0xa6,0x89,
    0xb1,0xf9,0x72,0xe6,0x35,0x5e,0x8c,0x12,0xcb,0xb4,0x94,0xf3,0x3c,0x72,0x94,0x77,
};

void patcher_build_mh2_fresh(uint8_t out[MH2_PATCHBIN_SIZE]) {
    /* MH2 0x3d54f0 clears the resident image and sets this header; a new save
     * stores it as patch.bin (0x3d6580). That file also has 0x550 zero bytes
     * at 0x7d00, so its checksum fails and the game skips it (seen on every
     * new save; cause unknown). */
    static uint8_t payload[MH2_PAYLOAD_SIZE];
    memset(payload, 0, sizeof payload);
    fresh_meta(GAME_MH2, payload);
    mh2_patchbin_encode(payload, out, MH2_FRESH_HDR, 0x26d7);
    memset(out + 0x7d00, 0, 0x550);
}

int patcher_mh2_unpatched(const uint8_t payload[MH2_PAYLOAD_SIZE]) {
    uint8_t meta[MH2_HEADER_SIZE];
    fresh_meta(GAME_MH2, meta);
    return memcmp(payload, meta, MH2_HEADER_SIZE) == 0;
}

int patcher_save_inject(mh_game game, const uint8_t ilink[8], uint8_t *decoded_save) {
    /* the rest of the player's save is left untouched */
    memcpy(decoded_save + SAVE_BLOB_OFF, BAKED[game].blob, BAKED[game].len);
    memcpy(decoded_save + SAVE_META_OFF, BAKED[game].meta, MH2_HEADER_SIZE);
    return reseal_header_in_place(game, ilink, decoded_save + SAVE_BLOB_OFF);
}

int patcher_save_remove(mh_game game, uint8_t *decoded_save) {
    uint8_t meta[MH2_HEADER_SIZE];
    fresh_meta(game, meta);

    uint8_t *blob = decoded_save + SAVE_BLOB_OFF, *hdr = decoded_save + SAVE_META_OFF;
    int changed = memcmp(hdr, meta, sizeof meta) != 0;
    for (uint32_t i = 0; !changed && i < SAVE_META_OFF - SAVE_BLOB_OFF; i++) changed = blob[i] != 0;
    memset(blob, 0, SAVE_META_OFF - SAVE_BLOB_OFF);
    memcpy(hdr, meta, sizeof meta);
    return changed;
}
