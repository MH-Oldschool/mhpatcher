#include "pad.h"
#include <libpad.h>
#include <kernel.h>
#include <string.h>

static char pad_area[256] __attribute__((aligned(64)));
static unsigned prev;

void pad_init(void) {
    padInit(0);
    padPortOpen(0, 0, pad_area);
    prev = 0;
    int n = 0, st;
    while (n++ < 60) {
        st = padGetState(0, 0);
        if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) break;
        if (st == PAD_STATE_DISCONN) break;
    }
}

static unsigned map(unsigned raw) {
    unsigned b = 0;
    if (raw & PAD_UP)     b |= BTN_UP;
    if (raw & PAD_DOWN)   b |= BTN_DOWN;
    if (raw & PAD_LEFT)   b |= BTN_LEFT;
    if (raw & PAD_RIGHT)  b |= BTN_RIGHT;
    if (raw & PAD_CROSS)  b |= BTN_CROSS;
    if (raw & PAD_CIRCLE) b |= BTN_CIRCLE;
    if (raw & PAD_START)  b |= BTN_START;
    if (raw & PAD_SELECT) b |= BTN_SELECT;
    return b;
}

unsigned pad_poll(void) {
    struct padButtonStatus data;
    unsigned cur = 0;
    if (padGetState(0, 0) == PAD_STATE_STABLE) {
        if (padRead(0, 0, &data) != 0) {
            cur = map(0xffff ^ data.btns);  /* active low */
        }
    }
    unsigned edge = cur & ~prev;
    prev = cur;
    return edge;
}
