/* Controller 1, digital buttons only. */
#ifndef PS2_PAD_H
#define PS2_PAD_H

#define BTN_UP     0x0001
#define BTN_DOWN   0x0002
#define BTN_LEFT   0x0004
#define BTN_RIGHT  0x0008
#define BTN_CROSS  0x0010   /* X */
#define BTN_CIRCLE 0x0020   /* O */
#define BTN_START  0x0040
#define BTN_SELECT 0x0080

void pad_init(void);
/* BTN_* bits of the buttons pressed since the last call. */
unsigned pad_poll(void);

#endif
