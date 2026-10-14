/* Save backup and restore between a memory card and a USB drive, one
 * MHSAVES/<dir>.psu per game.
 *
 * Backup only reads the card. The new file is written as .tmp and verified
 * before it replaces the old backup, which is kept as .psu.bak.
 *
 * Restore validates the backup and the card's free space first. An existing
 * save is copied to the drive as <dir>_before_restore[_N].psu (never
 * overwritten) and kept in RAM; if writing the card fails, it is put back.
 * rom0:MCSERV has no rename, so files are rewritten in place. */
#ifndef APP_SAVES_H
#define APP_SAVES_H

#include <stdint.h>

typedef enum {
    SG_MH2 = 0,     /* Monster Hunter 2 (dos)   BISLPM-66280MH2 */
    SG_MHG,         /* Monster Hunter G         BISLPM-65869MHG */
    SG_MHGK,        /* Monster Hunter G (Korea) BKSLKA-25219MHG */
    SG_MH1J,        /* Monster Hunter (Japan)   BISLPM-65495MH  */
    SG_MH1U,        /* Monster Hunter (USA)     BASLUS-20896MH  */
    SG_MH1E,        /* Monster Hunter (Europe)  BESLES-52707MH  */
    SG_COUNT
} save_game;

typedef enum {
    SV_OK = 0,
    SV_E_USB_READ,      /* drive removed or unreadable */
    SV_E_USB_WRITE,     /* drive full or write-protected */
    SV_E_MC_GONE,       /* card removed or swapped since the scan */
    SV_E_MC_READ,
    SV_E_MC_SPACE,
    SV_E_BAD_BACKUP,    /* damaged, or another game's save */
    SV_E_TOO_BIG,       /* subdirectories or too large */
    SV_E_MC_WRITE,      /* write failed, original put back */
    SV_E_MC_WRITE_NEW,  /* write failed, partial copy removed */
    SV_E_MC_LOST        /* write failed, original only on the drive */
} save_result;

const char *save_dir(save_game g);

/* 1 if the save carries the console-sealed online patch. */
int save_has_patch(save_game g);

/* Returns -1 if the card is gone. */
int save_scan(int port, int on_card[SG_COUNT], int on_usb[SG_COUNT]);

save_result save_backup(int port, save_game g);
save_result save_restore(int port, save_game g);

/* Backup path, or needed/free KB, for the result screen. */
extern char save_detail[96];

/* Name the next pre-restore copy will get (probes the drive). */
void save_safety_name(save_game g, char *out, int outlen);

/* Card space a save needs, in KB. */
int save_kb_needed(const uint32_t *sizes, int nfiles);

#endif
