#include "mcard.h"
#include "../dnas/patcher.h"
#include <libmc.h>
#include <string.h>
#include <stdio.h>

#define MC_LIST_MAX 40

/* mcman open modes (not newlib O_ flags) */
#define MC_RD  0x01
#define MC_WR  (0x200 | 0x02 | 0x01 | 0x10)  /* create, write, read, file */

void mc_init(void) {
    mcInit(MC_TYPE_PS2);
}

int mc_present(int port) {
    int type = 0, free = 0, format = 0, result = -1;
    mcGetInfo(port, 0, &type, &free, &format);
    mcSync(0, NULL, &result);
    if (type != MC_TYPE_PS2) return 0;
    if (format != 1) return 0;
    return 1;
}

static int exists(int port, const char *path) {
    int r = -1;
    int fd = mcOpen(port, 0, path, MC_RD);
    mcSync(0, NULL, &fd);
    if (fd < 0) return 0;
    mcClose(fd);
    mcSync(0, NULL, &r);
    return 1;
}

int mc_has_mh2(int port) {
    return exists(port, "/" MH2_SAVE_DIR "/" MH2_SAVE_DIR);
}
int mc_has_mhg(int port) {
    return exists(port, "/" MHG_SAVE_DIR "/" MHG_SAVE_FILE);
}

/* Real cards sometimes fail a single transfer and succeed on the next, so
 * reads and writes are retried. A missing file is not retried, and retries
 * never call mcGetInfo, which would hide a card swap. */
#define MC_TRIES 3
#define MC_CHUNK 0x8000

static char mc_err[64];
void mc_clear_error(void) { mc_err[0] = 0; }
const char *mc_last_error(void) { return mc_err; }
void mc_note_error(const char *what, const char *path, int code) {
    const char *base = strrchr(path, '/');
    snprintf(mc_err, sizeof mc_err, "%s %.31s: %d", what, base ? base + 1 : path, code);
}

/* One open + whole read (<= maxlen) + close. exact >= 0: the file must be
 * exactly that long. Returns bytes read, or a negative mcman code. */
static int read_once(int port, const char *path, void *buf, int maxlen, int exact) {
    int fd = mcOpen(port, 0, path, MC_RD);
    mcSync(0, NULL, &fd);
    if (fd < 0) return fd;
    int r = -1, size = -1, ret;
    mcSeek(fd, 0, 2);  mcSync(0, NULL, &size);   /* 2 = from end -> file size */
    mcSeek(fd, 0, 0);  mcSync(0, NULL, &r);       /* back to start */
    if (size < 0 || r != 0) ret = size < 0 ? size : -1;
    else if (exact >= 0 && size != exact) ret = -1000;          /* wrong size */
    else {
        int want = size < maxlen ? size : maxlen;
        ret = want;
        for (int off = 0; off < want; ) {
            int n = want - off > MC_CHUNK ? MC_CHUNK : want - off, got = -1;
            mcRead(fd, (uint8_t *)buf + off, n);
            mcSync(0, NULL, &got);
            if (got != n) { ret = got < 0 ? got : -1001; break; }  /* short read */
            off += n;
        }
    }
    mcClose(fd);
    mcSync(0, NULL, &r);
    return ret;
}

/* One open (creating if needed) + write + close; the close is what flushes
 * mcman's cache to the card, so its result counts. */
static int write_once(int port, const char *path, const void *buf, int len) {
    int fd = mcOpen(port, 0, path, MC_WR);
    mcSync(0, NULL, &fd);
    if (fd < 0) return fd;
    int ret = len;
    for (int off = 0; off < len; ) {
        int n = len - off > MC_CHUNK ? MC_CHUNK : len - off, put = -1;
        mcWrite(fd, (const uint8_t *)buf + off, n);
        mcSync(0, NULL, &put);
        if (put != n) { ret = put < 0 ? put : -1002; break; }    /* short write */
        off += n;
    }
    int r = -1;
    mcClose(fd);
    mcSync(0, NULL, &r);
    if (ret == len && r < 0) ret = r;
    return ret;
}

int mc_read(int port, const char *path, void *buf, int maxlen) {
    int ret = -1;
    for (int t = 0; t < MC_TRIES; t++) {
        ret = read_once(port, path, buf, maxlen, -1);
        if (ret >= 0 || ret == sceMcResNoEntry) return ret;
    }
    mc_note_error("read", path, ret);
    return ret;
}

int mc_write(int port, const char *path, const void *buf, int len) {
    int ret = -1;
    for (int t = 0; t < MC_TRIES; t++) {
        ret = write_once(port, path, buf, len);
        if (ret == len) return len;
    }
    mc_note_error("write", path, ret);
    return ret < 0 ? ret : -1;
}

/* ---- save backup / restore ---------------------------------------------- */


int mc_status(int port, int *free_kb) {
    int type = 0, free = 0, format = 0, result = -99;
    mcGetInfo(port, 0, &type, &free, &format);
    mcSync(0, NULL, &result);
    if (free_kb) *free_kb = free;                    /* 1 KB clusters */
    if (result < -1) return MC_ST_NONE;              /* unformatted or access error */
    if (type != MC_TYPE_PS2 || format != 1) return MC_ST_NONE;
    return result == 0 ? MC_ST_SAME : MC_ST_CHANGED;
}

static sceMcTblGetDir mc_tbl[MC_LIST_MAX] __attribute__((aligned(64)));

static void to_entry(const sceMcTblGetDir *t, mc_entry *e) {
    memset(e, 0, sizeof *e);
    memcpy(e->name, t->EntryName, sizeof e->name - 1);
    e->size = t->FileSizeByte;
    e->attr = t->AttrFile;
    memcpy(e->created, &t->_Create, 8);
    memcpy(e->modified, &t->_Modify, 8);
}

int mc_dir_info(int port, const char *dir, mc_entry *info) {
    char path[48];
    snprintf(path, sizeof path, "/%s", dir);
    int ret = -1;
    mcGetDir(port, 0, path, 0, 1, mc_tbl);
    mcSync(0, NULL, &ret);
    if (ret < 0) return ret == sceMcResNoEntry ? 0 : ret;
    if (ret == 0) return 0;
    if (!(mc_tbl[0].AttrFile & MC_ATTR_SUBDIR)) return 0;   /* a file, not a save dir */
    if (info) to_entry(&mc_tbl[0], info);
    return 1;
}

int mc_list(int port, const char *dir, mc_entry *out, int max) {
    char path[48];
    snprintf(path, sizeof path, "/%s/*", dir);
    int ret = -1;
    mcGetDir(port, 0, path, 0, MC_LIST_MAX, mc_tbl);
    mcSync(0, NULL, &ret);
    if (ret < 0) return ret;
    if (ret >= MC_LIST_MAX) return MC_LIST_FULL;         /* may have been cut off */
    int n = 0;
    for (int i = 0; i < ret; i++) {
        const char *nm = (const char *)mc_tbl[i].EntryName;
        if (!strcmp(nm, ".") || !strcmp(nm, "..")) continue;
        if (mc_tbl[i].AttrFile & MC_ATTR_SUBDIR) return MC_LIST_SUBDIR;
        if (n >= max) return MC_LIST_FULL;
        to_entry(&mc_tbl[i], &out[n++]);
    }
    return n;
}

int mc_read_exact(int port, const char *path, void *buf, int len) {
    int ret = -1;
    for (int t = 0; t < MC_TRIES; t++) {
        ret = read_once(port, path, buf, len, len);
        if (ret == len) return len;
        if (ret == sceMcResNoEntry) break;
    }
    mc_note_error("read", path, ret);
    return ret < 0 ? ret : -1;
}

int mc_write_new(int port, const char *path, const void *buf, int len) {
    int r = -1;
    int fd = mcOpen(port, 0, path, MC_RD);            /* refuse to write over a file */
    mcSync(0, NULL, &fd);
    if (fd >= 0) { mcClose(fd); mcSync(0, NULL, &r); mc_note_error("exists", path, 0); return -1; }

    int ret = -1;
    for (int t = 0; t < MC_TRIES; t++) {
        if (t) { mcDelete(port, 0, path); mcSync(0, NULL, &r); }  /* drop a partial file */
        ret = write_once(port, path, buf, len);
        if (ret == len) return len;
    }
    mc_note_error("write", path, ret);
    return ret < 0 ? ret : -1;
}

int mc_mkdir(int port, const char *path) {
    int r = -1;
    mcMkDir(port, 0, path);
    mcSync(0, NULL, &r);
    return r < 0 ? r : 0;
}

int mc_remove(int port, const char *path) {
    int r = -1;
    mcDelete(port, 0, path);
    mcSync(0, NULL, &r);
    return r < 0 ? r : 0;
}

int mc_set_dates(int port, const char *path, const uint8_t created[8], const uint8_t modified[8]) {
    static sceMcTblGetDir info __attribute__((aligned(64)));
    memset(&info, 0, sizeof info);
    memcpy(&info._Create, created, 8);
    memcpy(&info._Modify, modified, 8);
    int r = -1;
    mcSetFileInfo(port, 0, path, &info, sceMcFileInfoCreate | sceMcFileInfoModify);
    mcSync(0, NULL, &r);
    return r < 0 ? r : 0;
}
