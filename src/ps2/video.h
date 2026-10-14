/* gsKit 2D drawing: rectangles and text from the prebuilt atlas. */
#ifndef PS2_VIDEO_H
#define PS2_VIDEO_H

#include <stdint.h>
#include "../ui/atlas_data.h"

typedef enum { LANG_EN = 0, LANG_JP = 1 } ui_lang;

#define RGBA(r,g,b,a) ((uint32_t)(r) | ((uint32_t)(g)<<8) | ((uint32_t)(b)<<16) | ((uint32_t)(a)<<24))

/* alpha 0x80 is opaque on the GS */
#define COL_BG      RGBA(0x16,0x12,0x0c,0xff)
#define COL_GOLD    RGBA(0xd9,0xb0,0x5a,0x80)
#define COL_BONE    RGBA(0xef,0xe6,0xcf,0x80)
#define COL_TEXT    RGBA(0xf2,0xec,0xdc,0x80)
#define COL_DIM     RGBA(0x9a,0x8c,0x70,0x80)
#define COL_GREEN   RGBA(0x8a,0xd0,0x66,0x80)
#define COL_RED     RGBA(0xe0,0x6a,0x4a,0x80)
#define COL_WHITE   RGBA(0xff,0xff,0xff,0x80)

/* translucent panels over the backdrop */
#define COL_HEADER  RGBA(0x10,0x0a,0x06,0x70)
#define COL_PANELT  RGBA(0x18,0x12,0x0a,0x62)
#define COL_SELT    RGBA(0x7a,0x46,0x1c,0x6a)

void vid_init(void);
void vid_begin(void);          /* clear and draw the backdrop */
void vid_end(void);            /* flush and flip on vsync */

void vid_rect(float x, float y, float w, float h, uint32_t color);
void vid_scrim(float x, float y, float w, float h, uint32_t color);

/* translated phrase; returns its width */
int  vid_phrase(atlas_str id, ui_lang lang, float x, float y, uint32_t color);
int  vid_phrase_w(atlas_str id, ui_lang lang);
int  vid_phrase_h(atlas_str id, ui_lang lang);

/* ASCII text (ids, paths, error codes) */
int  vid_text(const char *s, float x, float y, uint32_t color);
int  vid_text_w(const char *s);

#endif
