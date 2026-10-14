#include "video.h"
#include "../ui/bg_data.h"
#include <gsKit.h>
#include <dmaKit.h>
#include <string.h>

static GSGLOBAL *gs;
static GSTEXTURE atlas;
static GSTEXTURE bg;
static u32 clut[256] __attribute__((aligned(64)));
static u8  atlas_buf[ATLAS_W * ATLAS_H] __attribute__((aligned(64)));
static u16 bg_buf[BG_W * BG_H] __attribute__((aligned(64)));

static u64 col64(uint32_t c) {
    u8 r = c & 0xff, g = (c >> 8) & 0xff, b = (c >> 16) & 0xff, a = (c >> 24) & 0xff;
    return GS_SETREG_RGBAQ(r, g, b, a > 0x80 ? 0x80 : a, 0x00);
}

void vid_init(void) {
    gs = gsKit_init_global();
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_FROMSPR);
    dmaKit_chan_init(DMA_CHANNEL_TOSPR);

    /* Interlaced SD in the console's region, so any TV input shows it (480p
     * only works over component/VGA). Keep GS_FIELD: with a full-height
     * framebuffer, GS_FRAME makes gsKit underflow the vertical magnification.
     * On PAL the 448-line UI is centred in the 576-line frame. */
    gs->Mode = gsKit_check_rom();
    gs->Interlace = GS_INTERLACED;
    gs->Field = GS_FIELD;
    gs->Width = 640;
    gs->Height = 448;
    gs->PSM = GS_PSM_CT32;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->PrimAlphaEnable = GS_SETTING_ON;

    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);   /* source over */
    gsKit_TexManager_init(gs);   /* uploads textures and the atlas CLUT */

    /* The atlas holds glyph coverage; a grey ramp CLUT with alpha = coverage
     * lets each draw tint it with the primitive colour. */
    for (int i = 0; i < 256; i++)
        clut[i] = RGBA(0x80, 0x80, 0x80, i >> 1);

    memcpy(bg_buf, bg_pixels, sizeof bg_buf);
    bg.Width = BG_W;
    bg.Height = BG_H;
    bg.PSM = GS_PSM_CT16;
    bg.Mem = (u32 *)bg_buf;
    bg.Filter = GS_FILTER_LINEAR;
    gsKit_setup_tbw(&bg);

    memcpy(atlas_buf, atlas_alpha, sizeof atlas_buf);
    atlas.Width = ATLAS_W;
    atlas.Height = ATLAS_H;
    atlas.PSM = GS_PSM_T8;
    atlas.ClutPSM = GS_PSM_CT32;
    atlas.Mem = (u32 *)atlas_buf;
    atlas.Clut = clut;
    atlas.Filter = GS_FILTER_NEAREST;
    gsKit_setup_tbw(&atlas);
}

void vid_begin(void) {
    gsKit_clear(gs, col64(COL_BG));
    gsKit_TexManager_bind(gs, &bg);
    gsKit_prim_sprite_texture(gs, &bg, 0, 0, 0, 0,
                              640, 448, (float)BG_W, (float)BG_H, 1,
                              GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0));
}

void vid_end(void) {
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
    gsKit_TexManager_nextFrame(gs);
}

void vid_rect(float x, float y, float w, float h, uint32_t color) {
    gsKit_prim_sprite(gs, x, y, x + w, y + h, 1, col64(color));
}

void vid_scrim(float x, float y, float w, float h, uint32_t color) {
    gsKit_prim_sprite(gs, x, y, x + w, y + h, 2, col64(color));
}

static void blit(int sx, int sy, int sw, int sh, float dx, float dy, uint32_t color) {
    gsKit_TexManager_bind(gs, &atlas);
    /* whole-pixel destination and half-texel UVs keep glyphs sharp */
    float ix = (float)(int)(dx + 0.5f), iy = (float)(int)(dy + 0.5f);
    gsKit_prim_sprite_texture(gs, &atlas,
        ix, iy, (float)sx + 0.5f, (float)sy + 0.5f,
        ix + sw, iy + sh, (float)(sx + sw) + 0.5f, (float)(sy + sh) + 0.5f,
        1, col64(color));
}

int vid_phrase(atlas_str id, ui_lang lang, float x, float y, uint32_t color) {
    const atlas_rect *r = &atlas_rects[id][lang];
    blit(r->x, r->y, r->w, r->h, x, y, color);
    return r->w;
}
int vid_phrase_w(atlas_str id, ui_lang lang) { return atlas_rects[id][lang].w; }
int vid_phrase_h(atlas_str id, ui_lang lang) { return atlas_rects[id][lang].h; }

int vid_text(const char *s, float x, float y, uint32_t color) {
    float cx = x;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c < ATLAS_ASCII_FIRST || c > ATLAS_ASCII_LAST) { cx += atlas_ascii_w[0]; continue; }
        int idx = c - ATLAS_ASCII_FIRST;
        int gx = ATLAS_ASCII_X + (idx % ATLAS_ASCII_COLS) * ATLAS_ASCII_CELLW;
        int gy = ATLAS_ASCII_Y + (idx / ATLAS_ASCII_COLS) * ATLAS_ASCII_CELLH;
        blit(gx, gy, ATLAS_ASCII_CELLW, ATLAS_ASCII_CELLH, cx, y, color);
        cx += atlas_ascii_w[idx];
    }
    return (int)(cx - x);
}

int vid_text_w(const char *s) {
    int w = 0;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        w += (c < ATLAS_ASCII_FIRST || c > ATLAS_ASCII_LAST) ? atlas_ascii_w[0]
                                                             : atlas_ascii_w[c - ATLAS_ASCII_FIRST];
    }
    return w;
}
