/* Patch install: write, verify, and put the original back on failure. */
#ifndef APP_INSTALL_H
#define APP_INSTALL_H

#include <stdint.h>
#include "../dnas/patcher.h"

typedef enum {
    INS_OK = 0,
    INS_ERR_NO_SAVE,     /* save missing/invalid or unreadable; nothing written */
    INS_ERR_BUILD,       /* patcher core refused (bad baked data) */
    INS_ERR_WRITE,       /* write failed, original restored */
    INS_ERR_VERIFY,      /* read-back mismatch, original restored */
    INS_ERR_VERIFY_LOST  /* failed and the original could not be restored */
} install_result;

install_result install_game(mh_game game, int port, const uint8_t ilink[8]);

/* Puts the save back in the state the game gives a new save, without the
 * patch. INS_OK too when there was no patch (nothing is written then). */
install_result remove_game(mh_game game, int port);

#endif
