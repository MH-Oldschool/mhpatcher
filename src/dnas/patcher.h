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

typedef enum { GAME_MH2 = 0, GAME_MHG = 1 } mh_game;

#define MH2_BLOB_LEN 0xd250
#define MHG_BLOB_LEN 0x1770

#define MH2_SAVE_DIR  "BISLPM-66280MH2"
#define MH2_SAVE_FILE "patch.bin"
#define MHG_SAVE_DIR  "BISLPM-65869MHG"
#define MHG_SAVE_FILE "BISLPM-65869MHG"

/* Re-seal the baked blob for `ilink` (8 bytes) into out. Returns blob length,
 * or 0 on an internal consistency failure (baked tag mismatch). */
uint32_t patcher_reseal(mh_game game, const uint8_t ilink[8], uint8_t *out);

/* Build the complete MH2 patch.bin (MH2_PATCHBIN_SIZE) for this console. */
int patcher_build_mh2_patchbin(const uint8_t ilink[8], uint16_t seed,
                               uint8_t out[MH2_PATCHBIN_SIZE]);

/* Inject the resealed MHG blob into a *decoded* MHG save (MHG_SAVE_SIZE),
 * in place. Caller decodes the on-card file, calls this, then re-encodes. */
int patcher_mhg_inject(const uint8_t ilink[8], uint8_t *decoded_save);

#endif
