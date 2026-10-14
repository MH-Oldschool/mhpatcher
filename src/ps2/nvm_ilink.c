#include "nvm_ilink.h"
#include <libcdvd.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* rom0:ROMVER starts with the version as four digits: "0160" -> 0x0160. */
static uint16_t bios_version(void) {
    char buf[8] = { 0 };
    int fd = open("rom0:ROMVER", O_RDONLY);
    if (fd < 0) return 0;
    int n = read(fd, buf, 4);
    close(fd);
    if (n < 4) return 0;
    uint16_t v = 0;
    for (int i = 0; i < 4; i++)
        v = (uint16_t)((v << 4) | ((buf[i] >= '0' && buf[i] <= '9') ? buf[i] - '0' : 0));
    return v;
}

/* A failed or unset read gives all 0x00 or all 0xFF. */
static int plausible(const uint8_t *id) {
    int zero = 1, ff = 1;
    for (int i = 0; i < 8; i++) { if (id[i]) zero = 0; if (id[i] != 0xff) ff = 0; }
    return !zero && !ff;
}

/* Mechacon S-command 0x12 returns a status byte and the ID, the same bytes
 * the games' DNAS library reads. Sent raw, it also works under OPL, whose
 * cdvdfsv passes S-commands through but implements neither sceCdRI nor
 * sceCdReadNVM. */
static int read_scmd(uint8_t id[8]) {
    u8 out[16] __attribute__((aligned(64)));
    memset(out, 0, sizeof out);
    if (sceCdApplySCmd(0x12, NULL, 0, out) != 1 || out[0] != 0) return 0;
    memcpy(id, out + 1, 8);
    return plausible(id);
}

static int read_ri(uint8_t id[8]) {
    u32 res = 1;
    if (sceCdRI(id, &res) != 1 || res != 0) return 0;
    return plausible(id);
}

/* The NVM words themselves, at 0x1C0 before BIOS 1.70 and 0x1E0 after. */
static int read_nvm(uint8_t id[8], uint16_t version) {
    uint32_t word = ((version && version < 0x0170) ? 0x1C0 : 0x1E0) >> 1;
    for (int i = 0; i < 4; i++) {
        u16 data = 0; u8 res = 0;
        if (sceCdReadNVM(word + i, &data, &res) != 1 || res != 0) return 0;
        id[i * 2 + 0] = (uint8_t)(data & 0xff);
        id[i * 2 + 1] = (uint8_t)(data >> 8);
    }
    return plausible(id);
}

void nvm_read_ilink(ilink_info *out) {
    memset(out, 0, sizeof *out);
    out->bios_version = bios_version();
    out->ok = read_scmd(out->id) || read_ri(out->id) || read_nvm(out->id, out->bios_version);
    if (!out->ok) memset(out->id, 0, sizeof out->id);
}
