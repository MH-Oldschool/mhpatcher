/* Simulated memory card (in RAM) and USB drive (a host directory) with fault
 * injection, standing in for src/ps2/mcard.c and usb.c. */
#include "fake_io.h"
#include "../src/app/psu.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- fake memory card --------------------------------------------------- */

node card[256];
int card_capacity_kb = 8000;
int card_present = 1, card_swapped = 0;
int fail_write_from = -1;     /* mc_write_new call index that starts failing */
int fail_write_count = 0;     /* how many calls fail from there (-1 = all)   */
int write_calls = 0;
int fail_remove = 0;
int corrupt_read_at = -1, corrupt_read_count = 1, read_calls = 0, fail_read_count = 0;

void card_reset(void) {
    for (int i = 0; i < 256; i++) { free(card[i].data); memset(&card[i], 0, sizeof card[i]); }
    card_capacity_kb = 8000; card_present = 1; card_swapped = 0;
    fail_write_from = -1; fail_write_count = 0; write_calls = 0;
    fail_remove = 0; corrupt_read_at = -1; corrupt_read_count = 1; read_calls = 0; fail_read_count = 0;
}

static int split(const char *path, char *dir, char *name) {
    dir[0] = name[0] = 0;
    if (path[0] != '/') return -1;
    const char *p = path + 1, *s = strchr(p, '/');
    if (!s) { snprintf(dir, 32, "%s", p); return 1; }      /* "/DIR" */
    snprintf(dir, 32, "%.*s", (int)(s - p), p);
    snprintf(name, 32, "%s", s + 1);
    return 2;                                             /* "/DIR/name" */
}
node *find(const char *dir, const char *name) {
    for (int i = 0; i < 256; i++)
        if (card[i].used && !strcmp(card[i].dir, name ? dir : "") &&
            !strcmp(card[i].name, name ? name : dir)) return &card[i];
    return NULL;
}
node *alloc_node(void) {
    for (int i = 0; i < 256; i++) if (!card[i].used) { memset(&card[i], 0, sizeof card[i]); card[i].used = 1; return &card[i]; }
    return NULL;
}
int used_kb(void) {
    int kb = 0;
    for (int i = 0; i < 256; i++) {
        if (!card[i].used) continue;
        if (card[i].is_dir) {
            int n = 0;
            for (int j = 0; j < 256; j++) if (card[j].used && !strcmp(card[j].dir, card[i].name)) n++;
            kb += (n + 2 + 1) / 2;
        } else kb += (card[i].size + 1023) / 1024;
    }
    return kb;
}

int mc_status(int port, int *free_kb) {
    if (free_kb) *free_kb = card_capacity_kb - used_kb();
    if (!card_present) return MC_ST_NONE;
    if (card_swapped) { card_swapped = 0; return MC_ST_CHANGED; }
    return MC_ST_SAME;
}
int mc_dir_info(int port, const char *dir, mc_entry *info) {
    if (!card_present) return -1;
    node *n = find(dir, NULL);
    if (!n || !n->is_dir) return 0;
    if (info) { memset(info, 0, sizeof *info); strcpy(info->name, n->name); info->attr = 0x8427;
                memcpy(info->created, n->created, 8); memcpy(info->modified, n->modified, 8); }
    return 1;
}
int mc_list(int port, const char *dir, mc_entry *out, int max) {
    if (!card_present) return -1;
    int c = 0;
    for (int i = 0; i < 256; i++) {
        node *n = &card[i];
        if (!n->used || strcmp(n->dir, dir)) continue;
        if (n->is_dir) return MC_LIST_SUBDIR;
        if (c >= max) return MC_LIST_FULL;
        memset(&out[c], 0, sizeof out[c]);
        strcpy(out[c].name, n->name); out[c].size = n->size; out[c].attr = 0x8497;
        memcpy(out[c].created, n->created, 8); memcpy(out[c].modified, n->modified, 8);
        c++;
    }
    return c;
}
int mc_read_exact(int port, const char *path, void *buf, int len) {
    char d[32], f[32];
    if (!card_present || split(path, d, f) != 2) return -1;
    node *n = find(d, f);
    if (!n || (int)n->size != len) return -1;
    memcpy(buf, n->data, len);
    /* corrupt reads [at, at+count) with a different flip each time, so two
     * corrupted reads never agree with each other */
    int call = read_calls++;
    if (corrupt_read_at >= 0 && call >= corrupt_read_at && call < corrupt_read_at + corrupt_read_count && len)
        ((uint8_t *)buf)[len / 2] ^= (uint8_t)(1 + (call - corrupt_read_at) % 255);
    return len;
}
int mc_write_new(int port, const char *path, const void *buf, int len) {
    char d[32], f[32];
    if (!card_present || split(path, d, f) != 2) return -1;
    node *dir = find(d, NULL);
    if (!dir || !dir->is_dir || find(d, f)) return -1;
    int call = write_calls++;
    int failing = fail_write_from >= 0 && call >= fail_write_from &&
                  (fail_write_count < 0 || call < fail_write_from + fail_write_count);
    int kb_free = card_capacity_kb - used_kb();
    int put = len;
    if (failing) put = len / 3;                                  /* interrupted write */
    if ((put + 1023) / 1024 + 1 > kb_free) { put = 0; failing = 1; }
    node *n = alloc_node();
    strcpy(n->dir, d); strcpy(n->name, f);
    n->data = malloc(len ? len : 1); n->size = put; memcpy(n->data, buf, put);
    return failing ? -1 : len;
}
int mc_mkdir(int port, const char *path) {
    char d[32], f[32];
    if (!card_present || split(path, d, f) != 1 || find(d, NULL)) return -1;
    node *n = alloc_node(); strcpy(n->name, d); n->is_dir = 1;
    return 0;
}
int mc_remove(int port, const char *path) {
    char d[32], f[32];
    if (!card_present || fail_remove) return -1;
    int k = split(path, d, f);
    node *n = k == 2 ? find(d, f) : k == 1 ? find(d, NULL) : NULL;
    if (!n) return -1;
    if (n->is_dir) for (int i = 0; i < 256; i++) if (card[i].used && !strcmp(card[i].dir, n->name)) return -1;
    free(n->data); memset(n, 0, sizeof *n);
    return 0;
}
int mc_set_dates(int port, const char *path, const uint8_t created[8], const uint8_t modified[8]) {
    char d[32], f[32];
    int k = split(path, d, f);
    node *n = k == 2 ? find(d, f) : k == 1 ? find(d, NULL) : NULL;
    if (!n) return -1;
    memcpy(n->created, created, 8); memcpy(n->modified, modified, 8);
    return 0;
}

/* load a real extracted save directory onto the fake card */
void card_load(const char *root, const char *dirname, int stamp) {
    char p[512];
    node *dn = alloc_node(); strcpy(dn->name, dirname); dn->is_dir = 1;
    memset(dn->created, stamp, 8);
    snprintf(p, sizeof p, "%s/%s", root, dirname);
    DIR *dd = opendir(p);
    struct dirent *de;
    while (dd && (de = readdir(dd))) {
        if (de->d_name[0] == '.') continue;
        char fp[768]; snprintf(fp, sizeof fp, "%s/%s", p, de->d_name);
        FILE *fh = fopen(fp, "rb"); fseek(fh, 0, SEEK_END); long sz = ftell(fh); rewind(fh);
        node *n = alloc_node(); strcpy(n->dir, dirname); strcpy(n->name, de->d_name);
        n->data = malloc(sz); n->size = sz;
        if (fread(n->data, 1, sz, fh) != (size_t)sz) abort();
        fclose(fh);
        n->created[1] = stamp; n->modified[2] = stamp;
    }
    if (dd) closedir(dd);
}

/* fingerprint of a card save directory: names, sizes, bytes */
uint32_t card_sig(const char *dirname) {
    uint32_t h = 0; int n = 0;
    /* order-independent: xor of per-file crcs */
    for (int i = 0; i < 256; i++) {
        if (!card[i].used || strcmp(card[i].dir, dirname)) continue;
        uint32_t c = crc32(0, card[i].name, strlen(card[i].name));
        c = crc32(c, card[i].data, card[i].size);
        h ^= c; n++;
    }
    return find(dirname, NULL) ? (h ^ (uint32_t)n * 2654435761u) : 0;
}

/* install-path whole-file calls (mcard.c mc_read / mc_write) */
static node *file_at(const char *path) {
    char d[32], f[32];
    return split(path, d, f) == 2 ? find(d, f) : NULL;
}
int mc_read(int port, const char *path, void *buf, int maxlen) {
    node *n = file_at(path);
    if (fail_read_count > 0) { fail_read_count--; return -5; } /* any other card error */
    if (!card_present || !n) return -4;                       /* sceMcResNoEntry */
    int c = (int)n->size < maxlen ? (int)n->size : maxlen;
    memcpy(buf, n->data, c);
    int call = read_calls++;
    if (corrupt_read_at >= 0 && call >= corrupt_read_at && call < corrupt_read_at + corrupt_read_count && c)
        ((uint8_t *)buf)[c / 2] ^= (uint8_t)(1 + (call - corrupt_read_at) % 255);
    return c;
}
int mc_write(int port, const char *path, const void *buf, int len) {
    char d[32], f[32];
    if (!card_present || split(path, d, f) != 2 || !find(d, NULL)) return -1;
    node *n = find(d, f);
    if (!n) { n = alloc_node(); strcpy(n->dir, d); strcpy(n->name, f); }
    int call = write_calls++;
    int failing = fail_write_from >= 0 && call >= fail_write_from &&
                  (fail_write_count < 0 || call < fail_write_from + fail_write_count);
    int put = failing ? len / 3 : len;                        /* interrupted write */
    if (n->size < (uint32_t)len) { n->data = realloc(n->data, len); memset(n->data + n->size, 0, len - n->size); n->size = len; }
    memcpy(n->data, buf, put);                                /* overwrites, no truncation */
    if (failing) mc_note_error("write", path, -1);            /* as mcard.c does */
    return failing ? -1 : len;
}

/* error-note helpers from mcard.c */
static char mc_err[64];
void mc_clear_error(void) { mc_err[0] = 0; }
const char *mc_last_error(void) { return mc_err; }
void mc_note_error(const char *what, const char *path, int code) {
    snprintf(mc_err, sizeof mc_err, "%s %s: %d", what, path, code);
}

/* ---- fake USB drive (host directory) ------------------------------------ */

char usb_base[512];
int usb_present = 1, usb_full_after = -1, usb_no_rename = 0, usb_written = 0;

static int host_path(const char *path, char *out) {
    if (strncmp(path, USB_ROOT, strlen(USB_ROOT))) return -1;
    snprintf(out, 768, "%s/%s", usb_base, path + strlen(USB_ROOT));
    return 0;
}
void usb_init_start(void) {}
int usb_init_state(void) { return usb_init_result; }
int usb_init_result = USB_INIT_OK;
const char *usb_init_detail(void) { return usb_init_result == USB_INIT_BUSY ? "bdm: id 12 res 1" : ""; }
int usb_ready(void) { return usb_present; }
int usb_size(const char *path) {
    char p[768]; struct stat st;
    if (!usb_present || host_path(path, p) || stat(p, &st)) return -1;
    return (int)st.st_size;
}
int usb_read(const char *path, void *buf, int max) {
    char p[768];
    if (!usb_present || host_path(path, p)) return -1;
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz > max) { fclose(f); return USB_TOO_BIG; }
    size_t got = fread(buf, 1, sz, f); fclose(f);
    return got == (size_t)sz ? (int)sz : -1;
}
int usb_write(const char *path, const void *buf, int len) {
    char p[768];
    if (!usb_present || host_path(path, p)) return -1;
    FILE *f = fopen(p, "wb"); if (!f) return -1;
    int put = len;
    if (usb_full_after >= 0 && usb_written + len > usb_full_after) put = usb_full_after - usb_written;
    if (put < 0) put = 0;
    fwrite(buf, 1, put, f); fclose(f);
    usb_written += put;
    return put == len ? len : -1;
}
int usb_remove(const char *path) { char p[768]; return (!usb_present || host_path(path, p) || unlink(p)) ? -1 : 0; }
int usb_rename(const char *a, const char *b) {
    char pa[768], pb[768];
    if (!usb_present || usb_no_rename || host_path(a, pa) || host_path(b, pb)) return -1;
    return rename(pa, pb) ? -1 : 0;
}
int usb_mkdir(const char *path) {
    char p[768]; struct stat st;
    if (!usb_present || host_path(path, p)) return -1;
    if (!mkdir(p, 0777)) return 0;
    return (!stat(p, &st) && S_ISDIR(st.st_mode)) ? 0 : -1;
}
void usb_reset(void) {
    char cmd[1100];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'/MHSAVES", usb_base);
    if (system(cmd)) {}
    usb_present = 1; usb_full_after = -1; usb_no_rename = 0; usb_written = 0;
}
int usb_exists(const char *name) {
    char p[768]; struct stat st;
    snprintf(p, sizeof p, "%s/MHSAVES/%s", usb_base, name);
    return !stat(p, &st);
}
uint32_t usb_file_crc(const char *name) {
    static uint8_t b[0x200000];
    char p[768]; snprintf(p, sizeof p, "%s/MHSAVES/%s", usb_base, name);
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    size_t n = fread(b, 1, sizeof b, f); fclose(f);
    return crc32(0, b, n);
}

