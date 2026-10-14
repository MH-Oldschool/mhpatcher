/* EMS ".psu" save files, as used by uLaunchELF and mymc: 512-byte directory
 * entries (the save directory, ".", "..", then each file followed by its data
 * padded to 1 KB).
 *
 * Files we write carry a tag in reserved bytes of the first entry: "MHBK", a
 * CRC-32 of the file with the tag zeroed, and the length. Untagged files from
 * other tools are accepted on their structure alone. */
#ifndef APP_PSU_H
#define APP_PSU_H

#include <stdint.h>

#define PSU_ENTRY      512
#define PSU_CLUSTER    1024
#define PSU_MAX_FILES  32
#define PSU_NAME_MAX   32        /* incl. NUL */

#define PSU_MODE_FILE  0x8497    /* exists | closed | file | rwx */
#define PSU_MODE_DIR   0x8427    /* exists | 0x400 | dir | rwx  */

typedef struct {
    char           name[PSU_NAME_MAX];
    uint32_t       size;
    uint16_t       mode;
    uint8_t        created[8];   /* sceMcStDateTime */
    uint8_t        modified[8];
    const uint8_t *data;
} psu_file;

typedef struct {
    char     name[PSU_NAME_MAX];
    uint16_t mode;
    uint8_t  created[8];
    uint8_t  modified[8];
    int      nfiles;
    psu_file files[PSU_MAX_FILES];
} psu_save;

typedef enum {
    PSU_OK = 0,
    PSU_E_SHORT,       /* truncated */
    PSU_E_FORMAT,      /* not a save container */
    PSU_E_FILES,       /* bad file count, subdirectory or name */
    PSU_E_DUP,         /* duplicate file name */
    PSU_E_TAG          /* tag present but does not match */
} psu_status;

uint32_t crc32(uint32_t crc, const void *buf, uint32_t len);

/* Size psu_build() will produce. */
uint32_t psu_size(const psu_save *s);

/* Returns the length written, or 0 if it does not fit or s is invalid. */
uint32_t psu_build(const psu_save *s, uint8_t *out, uint32_t max);

/* On PSU_OK, s->files[].data point into buf. */
psu_status psu_parse(const uint8_t *buf, uint32_t len, psu_save *s);

/* 1 if usable as a memory card file name. */
int psu_name_ok(const char *name);

#endif
