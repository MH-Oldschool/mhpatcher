/* Memory card access. Port 0 = slot 1, port 1 = slot 2. */
#ifndef PS2_MCARD_H
#define PS2_MCARD_H

#include <stdint.h>

/* mc_read result for a missing file (sceMcResNoEntry) */
#define MC_NO_ENTRY (-4)

void mc_init(void);

/* 1 if a formatted PS2 card is inserted. */
int mc_present(int port);

/* 1 if the game's main save exists. */
int mc_has_mh2(int port);
int mc_has_mhg(int port);
int mc_has_mh1e(int port);

/* Whole-file read (up to maxlen). Returns bytes read, or <0. */
int mc_read(int port, const char *path, void *buf, int maxlen);

/* Whole-file write; creates the file, does not truncate. Returns len, or <0. */
int mc_write(int port, const char *path, const void *buf, int len);

/* Last card error for the error screens, e.g. "read patch.bin: -2". */
void mc_clear_error(void);
const char *mc_last_error(void);
void mc_note_error(const char *what, const char *path, int code);

/* Whether the card is the same one as at the last call. Every mcGetInfo
 * clears the card's "changed" flag, so free space (in KB, may be NULL) comes
 * from this same call. */
#define MC_ST_NONE    0
#define MC_ST_SAME    1
#define MC_ST_CHANGED 2
int mc_status(int port, int *free_kb);

typedef struct {
    char     name[32];
    uint32_t size;
    uint16_t attr;          /* file mode bits */
    uint8_t  created[8];    /* sceMcStDateTime */
    uint8_t  modified[8];
} mc_entry;

/* 1 if the top-level directory exists (fills *info), 0 if not, <0 on error. */
int mc_dir_info(int port, const char *dir, mc_entry *info);

/* Files of a top-level directory. Returns the count, MC_LIST_SUBDIR,
 * MC_LIST_FULL (more than max), or another negative value on error. */
#define MC_LIST_SUBDIR (-100)
#define MC_LIST_FULL   (-101)
int mc_list(int port, const char *dir, mc_entry *out, int max);

/* Read a file that must be exactly len bytes long. Returns len, or <0. */
int mc_read_exact(int port, const char *path, void *buf, int len);

/* Create a file that must not exist yet. Returns len, or <0. */
int mc_write_new(int port, const char *path, const void *buf, int len);

int mc_mkdir(int port, const char *path);     /* 0 or <0 */
int mc_remove(int port, const char *path);    /* file or empty directory */

int mc_set_dates(int port, const char *path, const uint8_t created[8], const uint8_t modified[8]);

#endif
