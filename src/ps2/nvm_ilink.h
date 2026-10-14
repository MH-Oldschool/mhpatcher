/* Console i.Link ID, the identity the DNAS patch is sealed to. */
#ifndef PS2_NVM_ILINK_H
#define PS2_NVM_ILINK_H

#include <stdint.h>

typedef struct {
    uint8_t  id[8];          /* in NVM byte order */
    uint16_t bios_version;   /* e.g. 0x0160 for 1.60 */
    int      ok;             /* 0 if no read method returned a plausible ID */
} ilink_info;

void nvm_read_ilink(ilink_info *out);

#endif
