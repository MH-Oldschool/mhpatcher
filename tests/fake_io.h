#ifndef HOST_FAKE_IO_H
#define HOST_FAKE_IO_H
#include "../src/ps2/mcard.h"
#include "../src/ps2/usb.h"
#include <stdint.h>

typedef struct {
    int used, is_dir;
    char dir[32], name[32];          /* dir="" for a top-level directory */
    uint8_t *data;
    uint32_t size;
    uint8_t created[8], modified[8];
} node;

extern node card[256];
extern int card_capacity_kb, card_present, card_swapped;
extern int fail_write_from, fail_write_count, write_calls, fail_remove;
extern int corrupt_read_at, corrupt_read_count, read_calls;
extern char usb_base[512];
extern int usb_present, usb_full_after, usb_no_rename, usb_written;
extern int usb_init_result;

void card_reset(void);
node *find(const char *dir, const char *name);
node *alloc_node(void);
int used_kb(void);
void card_load(const char *root, const char *dirname, int stamp);
uint32_t card_sig(const char *dirname);
void usb_reset(void);
int usb_exists(const char *name);
uint32_t usb_file_crc(const char *name);
#endif
