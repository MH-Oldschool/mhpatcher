/* Re-seals the official patch to a console's i.Link ID and builds the bytes
 * the game's own download would have saved to the card.
 *
 * Per console only the 128-byte session header at blob [0x180,0x200) and the
 * save checksum change; the signed ciphertext and ID-80 signature are kept as
 * they are. The memory card load path does not check the ID-95 signature. */
#ifndef DNAS_PATCHER_H
#define DNAS_PATCHER_H

#include <stdint.h>
#include "savecodec.h"

typedef enum { GAME_MH2 = 0, GAME_MHG = 1, GAME_MH1E = 2 } mh_game;

#define MH2_BLOB_LEN 0xd250
#define MHG_BLOB_LEN 0x1770
#define MH1E_BLOB_LEN 0x1f70

#define MH2_SAVE_DIR  "BISLPM-66280MH2"
#define MH2_SAVE_FILE "patch.bin"
#define MHG_SAVE_DIR  "BISLPM-65869MHG"
#define MHG_SAVE_FILE "BISLPM-65869MHG"
#define MH1E_SAVE_DIR  "BESLES-52707MH"
#define MH1E_SAVE_FILE "BESLES-52707MH"

/* Re-seal the baked blob for `ilink` (8 bytes) into out. Returns blob length,
 * or 0 on an internal consistency failure (baked tag mismatch). */
uint32_t patcher_reseal(mh_game game, const uint8_t ilink[8], uint8_t *out);

/* Build the complete MH2 patch.bin (MH2_PATCHBIN_SIZE) for this console. */
int patcher_build_mh2_patchbin(const uint8_t ilink[8], uint16_t seed,
                               uint8_t out[MH2_PATCHBIN_SIZE]);

/* Inject the resealed MHG or MH1 (Europe) blob into a *decoded* game save
 * of that game, in place. Caller decodes the on-card file, calls this, then
 * re-encodes. */
int patcher_save_inject(mh_game game, const uint8_t ilink[8], uint8_t *decoded_save);

/* Build the patch.bin MH2 writes for a new save (no patch: the build stamp,
 * "1000" and length 0, which the load path skips) into out, byte for byte. */
void patcher_build_mh2_fresh(uint8_t out[MH2_PATCHBIN_SIZE]);

/* 1 if a decoded MH2 patch.bin payload holds no patch: its patch header is a
 * new save's (build stamp, "1000", length 0). */
int patcher_mh2_unpatched(const uint8_t payload[MH2_PAYLOAD_SIZE]);

/* Put a *decoded* MHG or MH1 (Europe) game save back in the state the game
 * gives a new save: no patch blob, and the patch header holding the game's
 * build stamp, version "1000" and length 0, which the load path skips.
 * Returns 1 if anything changed. */
int patcher_save_remove(mh_game game, uint8_t *decoded_save);

#endif
