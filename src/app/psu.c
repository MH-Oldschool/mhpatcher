#include "psu.h"
#include <string.h>

/* directory-entry field offsets (McFsEntry) */
#define E_MODE     0x00
#define E_LENGTH   0x04
#define E_CREATED  0x08
#define E_MODIFIED 0x18
#define E_TAG      0x24      /* reserved bytes: our integrity tag */
#define E_NAME     0x40

#define TAG_MAGIC  "MHBK"
#define TAG_LEN    12        /* magic, crc32, file length */

#define M_EXISTS   0x8000
#define M_DIR      0x0020
#define M_FILE     0x0010

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint16_t get16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t get32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t pad_cluster(uint32_t n) { return (n + PSU_CLUSTER - 1) / PSU_CLUSTER * PSU_CLUSTER; }

uint32_t crc32(uint32_t crc, const void *buf, uint32_t len) {
    const uint8_t *p = buf;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
    }
    return ~crc;
}

int psu_name_ok(const char *name) {
    size_t n = strnlen(name, PSU_NAME_MAX);
    if (n == 0 || n >= PSU_NAME_MAX) return 0;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = name[i];
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?') return 0;
    }
    return 1;
}

uint32_t psu_size(const psu_save *s) {
    uint32_t n = 3 * PSU_ENTRY;
    for (int i = 0; i < s->nfiles; i++) n += PSU_ENTRY + pad_cluster(s->files[i].size);
    return n;
}

static void entry(uint8_t *e, uint16_t mode, uint32_t length, const uint8_t created[8],
                  const uint8_t modified[8], const char *name) {
    memset(e, 0, PSU_ENTRY);
    put16(e + E_MODE, mode);
    put32(e + E_LENGTH, length);
    memcpy(e + E_CREATED, created, 8);
    memcpy(e + E_MODIFIED, modified, 8);
    strncpy((char *)e + E_NAME, name, PSU_NAME_MAX - 1);
}

uint32_t psu_build(const psu_save *s, uint8_t *out, uint32_t max) {
    if (s->nfiles < 0 || s->nfiles > PSU_MAX_FILES || !psu_name_ok(s->name)) return 0;
    for (int i = 0; i < s->nfiles; i++)
        if (!psu_name_ok(s->files[i].name) || (!s->files[i].data && s->files[i].size)) return 0;
    uint32_t len = psu_size(s);
    if (len > max) return 0;

    uint8_t *p = out;
    entry(p, s->mode ? s->mode : PSU_MODE_DIR, s->nfiles + 2, s->created, s->modified, s->name);
    p += PSU_ENTRY;
    entry(p, PSU_MODE_DIR, 0, s->created, s->created, ".");
    p += PSU_ENTRY;
    entry(p, PSU_MODE_DIR, 0, s->created, s->created, "..");
    p += PSU_ENTRY;
    for (int i = 0; i < s->nfiles; i++) {
        const psu_file *f = &s->files[i];
        uint16_t mode = (f->mode & M_FILE) ? f->mode : PSU_MODE_FILE;
        entry(p, mode | M_EXISTS, f->size, f->created, f->modified, f->name);
        p += PSU_ENTRY;
        memcpy(p, f->data, f->size);
        memset(p + f->size, 0, pad_cluster(f->size) - f->size);
        p += pad_cluster(f->size);
    }

    /* integrity tag: CRC over the file with the tag bytes still zero */
    uint32_t crc = crc32(0, out, len);
    memcpy(out + E_TAG, TAG_MAGIC, 4);
    put32(out + E_TAG + 4, crc);
    put32(out + E_TAG + 8, len);
    return len;
}

static int is_dir(uint16_t m)  { return (m & (M_EXISTS | M_DIR | M_FILE)) == (M_EXISTS | M_DIR); }
static int is_file(uint16_t m) { return (m & (M_EXISTS | M_DIR | M_FILE)) == (M_EXISTS | M_FILE); }

static void copy_name(char *dst, const uint8_t *e) {
    memcpy(dst, e + E_NAME, PSU_NAME_MAX);
    dst[PSU_NAME_MAX - 1] = 0;
}

psu_status psu_parse(const uint8_t *buf, uint32_t len, psu_save *s) {
    memset(s, 0, sizeof *s);
    if (len < 3 * PSU_ENTRY) return PSU_E_SHORT;

    const uint8_t *d = buf;
    if (!is_dir(get16(d + E_MODE))) return PSU_E_FORMAT;
    if (!is_dir(get16(d + PSU_ENTRY + E_MODE)) || !is_dir(get16(d + 2 * PSU_ENTRY + E_MODE)))
        return PSU_E_FORMAT;
    if (strcmp((const char *)d + PSU_ENTRY + E_NAME, ".") ||
        strcmp((const char *)d + 2 * PSU_ENTRY + E_NAME, ".."))
        return PSU_E_FORMAT;

    /* integrity tag, if this file was written by us */
    if (!memcmp(d + E_TAG, TAG_MAGIC, 4)) {
        uint32_t want = get32(d + E_TAG + 4), tlen = get32(d + E_TAG + 8);
        if (tlen != len) return PSU_E_TAG;
        uint8_t first[PSU_ENTRY];
        memcpy(first, d, PSU_ENTRY);
        memset(first + E_TAG, 0, TAG_LEN);
        uint32_t crc = crc32(0, first, PSU_ENTRY);
        crc = crc32(crc, d + PSU_ENTRY, len - PSU_ENTRY);
        if (crc != want) return PSU_E_TAG;
    }

    uint32_t count = get32(d + E_LENGTH);
    if (count < 2 || count - 2 > PSU_MAX_FILES) return PSU_E_FILES;
    copy_name(s->name, d);
    if (!psu_name_ok(s->name)) return PSU_E_FORMAT;
    s->mode = get16(d + E_MODE);
    memcpy(s->created, d + E_CREATED, 8);
    memcpy(s->modified, d + E_MODIFIED, 8);
    s->nfiles = count - 2;

    uint32_t off = 3 * PSU_ENTRY;
    for (int i = 0; i < s->nfiles; i++) {
        if (len - off < PSU_ENTRY) return PSU_E_SHORT;
        const uint8_t *e = buf + off;
        psu_file *f = &s->files[i];
        f->mode = get16(e + E_MODE);
        if (!is_file(f->mode)) return PSU_E_FILES;      /* subdirectory or empty slot */
        copy_name(f->name, e);
        if (!psu_name_ok(f->name)) return PSU_E_FILES;
        for (int j = 0; j < i; j++)
            if (!strcmp(s->files[j].name, f->name)) return PSU_E_DUP;
        f->size = get32(e + E_LENGTH);
        memcpy(f->created, e + E_CREATED, 8);
        memcpy(f->modified, e + E_MODIFIED, 8);
        off += PSU_ENTRY;
        if (f->size > len - off) return PSU_E_SHORT;
        f->data = buf + off;
        /* the last file's cluster padding may be cut short by other tools */
        uint32_t padded = pad_cluster(f->size);
        off += (padded <= len - off) ? padded : len - off;
    }
    return PSU_OK;
}
