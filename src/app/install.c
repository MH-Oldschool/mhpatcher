#include "install.h"
#include "../ps2/mcard.h"
#include "../dnas/savecodec.h"
#include <string.h>

#define MH2_PATH "/" MH2_SAVE_DIR "/" MH2_SAVE_FILE
#define MHG_PATH "/" MHG_SAVE_DIR "/" MHG_SAVE_FILE

static uint8_t work[0x14000]   __attribute__((aligned(64)));
static uint8_t backup[0x14000] __attribute__((aligned(64)));
static uint8_t verify[0x14000] __attribute__((aligned(64)));

static const uint16_t MH2_SEED = 0x4d32; /* "M2" */
static const uint16_t MHG_SEED = 0x4d47; /* "MG" */

/* Writes up to twice; a mismatch is read once more before it counts. */
static install_result put_verified(int port, const char *path, const uint8_t *data, int len) {
    install_result why = INS_ERR_WRITE;
    for (int w = 0; w < 2; w++) {
        if (mc_write(port, path, data, len) != len) { why = INS_ERR_WRITE; continue; }
        for (int r = 0; r < 2; r++) {
            int got = mc_read(port, path, verify, sizeof verify);
            if (got == len && memcmp(verify, data, len) == 0) return INS_OK;
        }
        why = INS_ERR_VERIFY;
        mc_note_error("verify", path, 0);
    }
    return why;
}

/* Puts the original back, or removes the file if there was none. */
static install_result put_back(int port, const char *path, int had_original, int len, install_result why) {
    if (!had_original) { mc_remove(port, path); return why; }
    return put_verified(port, path, backup, len) == INS_OK ? why : INS_ERR_VERIFY_LOST;
}

static install_result install_mh2(int port, const uint8_t ilink[8]) {
    int had_backup = mc_read(port, MH2_PATH, backup, sizeof backup) == MH2_PATCHBIN_SIZE;

    if (!patcher_build_mh2_patchbin(ilink, MH2_SEED, work)) return INS_ERR_BUILD;

    install_result r = put_verified(port, MH2_PATH, work, MH2_PATCHBIN_SIZE);
    return r == INS_OK ? INS_OK : put_back(port, MH2_PATH, had_backup, MH2_PATCHBIN_SIZE, r);
}

static install_result install_mhg(int port, const uint8_t ilink[8]) {
    int got = mc_read(port, MHG_PATH, backup, sizeof backup);
    if (got != MHG_SAVE_SIZE) return INS_ERR_NO_SAVE;

    memcpy(work, backup, MHG_SAVE_SIZE);
    if (!mhg_save_decode(work)) return INS_ERR_NO_SAVE;   /* bad checksum/tag */
    if (!patcher_mhg_inject(ilink, work)) return INS_ERR_BUILD;
    mhg_save_encode(work, MHG_SEED);

    install_result r = put_verified(port, MHG_PATH, work, MHG_SAVE_SIZE);
    return r == INS_OK ? INS_OK : put_back(port, MHG_PATH, 1, MHG_SAVE_SIZE, r);
}

install_result install_game(mh_game game, int port, const uint8_t ilink[8]) {
    mc_clear_error();
    return (game == GAME_MH2) ? install_mh2(port, ilink) : install_mhg(port, ilink);
}
