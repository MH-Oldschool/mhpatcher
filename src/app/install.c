#include "install.h"
#include "../ps2/mcard.h"
#include "../dnas/savecodec.h"
#include <string.h>

#define MH2_PATH "/" MH2_SAVE_DIR "/" MH2_SAVE_FILE
#define MHG_PATH "/" MHG_SAVE_DIR "/" MHG_SAVE_FILE
#define MH1E_PATH "/" MH1E_SAVE_DIR "/" MH1E_SAVE_FILE

static uint8_t work[0x14000]   __attribute__((aligned(64)));
static uint8_t backup[0x14000] __attribute__((aligned(64)));
static uint8_t verify[0x14000] __attribute__((aligned(64)));

static const uint16_t MH2_SEED = 0x4d32; /* "M2", for a patch.bin made from scratch */

/* Saves are written back with the seed they already have (file bytes 2-3). */
static uint16_t file_seed(const uint8_t *file) { return (uint16_t)(file[2] | (file[3] << 8)); }

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
    int got = mc_read(port, MH2_PATH, backup, sizeof backup);
    if (got < 0 && got != MC_NO_ENTRY) return INS_ERR_NO_SAVE;   /* unreadable: keep it */
    int had_backup = got == MH2_PATCHBIN_SIZE;

    if (!patcher_build_mh2_patchbin(ilink, had_backup ? file_seed(backup) : MH2_SEED, work))
        return INS_ERR_BUILD;

    install_result r = put_verified(port, MH2_PATH, work, MH2_PATCHBIN_SIZE);
    return r == INS_OK ? INS_OK : put_back(port, MH2_PATH, had_backup, MH2_PATCHBIN_SIZE, r);
}

/* patch.bin is never deleted: the game would re-create it from RAM on the
 * next save. It is overwritten with the file a new save gets, unless it
 * already is one. */
static install_result remove_mh2(int port) {
    static uint8_t payload[MH2_PAYLOAD_SIZE];
    int got = mc_read(port, MH2_PATH, backup, sizeof backup);
    if (got < 0 && got != MC_NO_ENTRY) return INS_ERR_NO_SAVE;   /* unreadable: keep it */
    int had_backup = got == MH2_PATCHBIN_SIZE;
    /* A new save's file fails its checksum (patcher_build_mh2_fresh), so only
     * the header counts. */
    if (had_backup) {
        mh2_patchbin_decode(backup, payload);
        if (patcher_mh2_unpatched(payload)) return INS_OK;      /* no patch: nothing to write */
    }

    patcher_build_mh2_fresh(work);
    install_result r = put_verified(port, MH2_PATH, work, MH2_PATCHBIN_SIZE);
    return r == INS_OK ? INS_OK : put_back(port, MH2_PATH, had_backup, MH2_PATCHBIN_SIZE, r);
}

/* MHG and MH1 (Europe) keep the patch inside the game save. Installs the
 * patch, or with ilink NULL removes it. */
static install_result update_save(mh_game game, int port, const uint8_t *ilink) {
    const char *path = (game == GAME_MHG) ? MHG_PATH : MH1E_PATH;
    int size         = (game == GAME_MHG) ? MHG_SAVE_SIZE : MH1E_SAVE_SIZE;
    size_t words     = (game == GAME_MHG) ? MHG_SAVE_WORDS : MH1E_SAVE_WORDS;

    int got = mc_read(port, path, backup, sizeof backup);
    if (got != size) return INS_ERR_NO_SAVE;

    memcpy(work, backup, size);
    if (!mh_save_decode(work, words)) return INS_ERR_NO_SAVE;   /* bad checksum/tag */
    if (ilink) {
        if (!patcher_save_inject(game, ilink, work)) return INS_ERR_BUILD;
    } else if (!patcher_save_remove(game, work)) {
        return INS_OK;                                          /* no patch: nothing to write */
    }
    mh_save_encode(work, words, file_seed(backup));

    install_result r = put_verified(port, path, work, size);
    return r == INS_OK ? INS_OK : put_back(port, path, 1, size, r);
}

install_result install_game(mh_game game, int port, const uint8_t ilink[8]) {
    mc_clear_error();
    return (game == GAME_MH2) ? install_mh2(port, ilink) : update_save(game, port, ilink);
}

install_result remove_game(mh_game game, int port) {
    mc_clear_error();
    return (game == GAME_MH2) ? remove_mh2(port) : update_save(game, port, NULL);
}
