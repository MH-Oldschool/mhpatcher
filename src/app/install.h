/* Patch install: write, verify, and put the original back on failure. */
#ifndef APP_INSTALL_H
#define APP_INSTALL_H

#include <stdint.h>
#include "../dnas/patcher.h"

typedef enum {
    INS_OK = 0,
    INS_ERR_NO_SAVE,     /* MHG: existing save missing/invalid */
    INS_ERR_BUILD,       /* patcher core refused (bad baked data) */
    INS_ERR_WRITE,       /* write failed, original restored */
    INS_ERR_VERIFY,      /* read-back mismatch, original restored */
    INS_ERR_VERIFY_LOST  /* failed and the original could not be restored */
} install_result;

install_result install_game(mh_game game, int port, const uint8_t ilink[8]);

#endif
