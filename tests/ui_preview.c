/* Renders the real src/main.c screens on the PC through a software version of
 * the video API, with scripted button presses, and saves them as .ppm files.
 * An approximation for checking layouts, not a GS emulator.
 *
 *   ./ui_preview <saves-dir> <usb-dir> <out-dir> [opl] */
#include "fake_io.h"
#include "../src/ps2/video.h"
#include "../src/ps2/pad.h"
#include "../src/ps2/nvm_ilink.h"
#include "../src/ps2/mcard.h"
#include "../src/app/saves.h"
#include "../src/dnas/patcher.h"
#include "../src/ui/bg_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- ps2sdk stubs ------------------------------------------------------- */
void SifInitRpc(int mode) { (void)mode; }
int SifLoadModule(const char *p, int a, const char *b) { (void)p; (void)a; (void)b; return 0; }
int sceCdInit(int mode) { (void)mode; return 1; }
static int opl_scenario;   /* argv[4] == "opl": i.Link unreadable, USB port taken */
void nvm_read_ilink(ilink_info *o) {
    static const uint8_t id[8] = { 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x00, 0x42 };
    memset(o, 0, sizeof *o);
    if (opl_scenario) return;
    memcpy(o->id, id, 8); o->ok = 1;
}

/* ---- memory-card calls used by the install path (on the fake card) ------ */
void mc_init(void) {}
int mc_present(int port) { return port == 0 && card_present; }
static node *file_at(const char *path) {
    char d[32], f[32];
    const char *s = strchr(path + 1, '/');
    if (path[0] != '/' || !s) return NULL;
    snprintf(d, sizeof d, "%.*s", (int)(s - path - 1), path + 1);
    snprintf(f, sizeof f, "%s", s + 1);
    return find(d, f);
}
int mc_has_mh2(int port) { return port == 0 && file_at("/" MH2_SAVE_DIR "/" MH2_SAVE_DIR); }
int mc_has_mhg(int port) { return port == 0 && file_at("/" MHG_SAVE_DIR "/" MHG_SAVE_FILE); }
int mc_has_mh1e(int port) { return port == 0 && file_at("/" MH1E_SAVE_DIR "/" MH1E_SAVE_FILE); }

/* ---- software video ----------------------------------------------------- */
#define W 640
#define H 448
static float fb[H][W][3];
static const char *out_dir;
static const char *shot;            /* name to save at the next vid_end */

static void blend(int x, int y, const float c[3], float a) {
    if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
    for (int i = 0; i < 3; i++) fb[y][x][i] = c[i] * a + fb[y][x][i] * (1 - a);
}
static void unpack(uint32_t col, float c[3], float *a) {
    c[0] = (col & 0xff) / 255.0f; c[1] = ((col >> 8) & 0xff) / 255.0f; c[2] = ((col >> 16) & 0xff) / 255.0f;
    unsigned al = col >> 24; if (al > 0x80) al = 0x80;
    *a = al / 128.0f;
}

void vid_init(void) {}

void vid_begin(void) {
    float c[3]; float a;
    unpack(COL_BG, c, &a);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) memcpy(fb[y][x], c, sizeof c);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint16_t p = bg_pixels[(y * BG_H / H) * BG_W + (x * BG_W / W)];
            fb[y][x][0] = (p & 31) / 31.0f; fb[y][x][1] = ((p >> 5) & 31) / 31.0f; fb[y][x][2] = ((p >> 10) & 31) / 31.0f;
        }
}
void vid_end(void) {
    if (!shot) return;
    char p[512]; snprintf(p, sizeof p, "%s/%s.ppm", out_dir, shot);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        for (int i = 0; i < 3; i++) { float v = fb[y][x][i]; fputc(v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255 + 0.5f), f); }
    fclose(f);
    shot = NULL;
}
void vid_rect(float x, float y, float w, float h, uint32_t col) {
    float c[3], a; unpack(col, c, &a);
    for (int yy = (int)y; yy < (int)(y + h); yy++) for (int xx = (int)x; xx < (int)(x + w); xx++) blend(xx, yy, c, a);
}
void vid_scrim(float x, float y, float w, float h, uint32_t col) { vid_rect(x, y, w, h, col); }
static void blit(int sx, int sy, int sw, int sh, float dx, float dy, uint32_t col) {
    float c[3], a; unpack(col, c, &a);
    int ix = (int)(dx + 0.5f), iy = (int)(dy + 0.5f);
    for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) {
        int cov = atlas_alpha[(sy + y) * ATLAS_W + sx + x];
        float at = (cov >> 1) / 128.0f;                       /* CLUT alpha 0..127, MODULATE */
        blend(ix + x, iy + y, c, at * a);
    }
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
        blit(ATLAS_ASCII_X + (idx % ATLAS_ASCII_COLS) * ATLAS_ASCII_CELLW,
             ATLAS_ASCII_Y + (idx / ATLAS_ASCII_COLS) * ATLAS_ASCII_CELLH,
             ATLAS_ASCII_CELLW, ATLAS_ASCII_CELLH, cx, y, color);
        cx += atlas_ascii_w[idx];
    }
    return (int)(cx - x);
}
int vid_text_w(const char *s) {
    int w = 0;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        w += (c < ATLAS_ASCII_FIRST || c > ATLAS_ASCII_LAST) ? atlas_ascii_w[0] : atlas_ascii_w[c - ATLAS_ASCII_FIRST];
    }
    return w;
}

/* ---- scripted pad ------------------------------------------------------- */
typedef struct { unsigned key; const char *shot; void (*hook)(void); } step;

static void usb_unplug(void)  { usb_present = 0; }
static void usb_plug(void)    { usb_present = 1; }
static void card_breaks(void) { fail_write_from = write_calls; fail_write_count = -1; }
static void card_heals(void)  { fail_write_from = -1; }

static step script[] = {
    { 0, "01_language", NULL },          { BTN_CIRCLE, NULL, NULL },
    { 0, "02_menu_install", NULL },      { BTN_DOWN, NULL, NULL },
    { 0, "02_menu_remove", NULL },       { BTN_DOWN, NULL, NULL },
    { 0, "03_menu_backup", NULL },       { BTN_CIRCLE, NULL, NULL },
    { 0, "04_card_select", NULL },       { BTN_CIRCLE, NULL, NULL },
    { 0, "05_usb_search", NULL },        { 0, NULL, NULL },
    { 0, "06_list", NULL },              { BTN_CIRCLE, NULL, NULL },          /* MH2 */
    { 0, "07_action_mh2", NULL },        { BTN_CIRCLE, NULL, NULL },          /* back up */
    { 0, "08_confirm_backup_new", NULL },{ BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL },
    { 0, "09_backup_done", NULL },       { BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL },
    { 0, "10_list_after", NULL },        { BTN_CIRCLE, NULL, NULL },
    { BTN_CIRCLE, NULL, NULL },
    { 0, "11_confirm_backup_over", NULL },{ BTN_CROSS, NULL, NULL },
    { BTN_DOWN, NULL, NULL },
    { 0, "12_action_restore", NULL },    { BTN_CIRCLE, NULL, NULL },
    { 0, "13_confirm_restore_over", NULL },{ BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL },
    { 0, "14_restore_done_patch", NULL },{ BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL },
    { BTN_DOWN, NULL, NULL }, { BTN_DOWN, NULL, NULL }, { BTN_DOWN, NULL, NULL },
    { BTN_DOWN, NULL, NULL },                                                 /* -> MH1 USA */
    { 0, "15_list_mh1u", NULL },         { BTN_CIRCLE, NULL, NULL },
    { BTN_DOWN, NULL, NULL },            { BTN_CIRCLE, NULL, card_breaks },
    { BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL },
    { 0, "16_fail_lost", card_heals },   { BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL },
    { BTN_UP, NULL, NULL },                                                   /* -> MHG (USB only) */
    { BTN_CIRCLE, NULL, NULL },
    { 0, "17_action_usb_only", NULL },   { BTN_CIRCLE, NULL, NULL },
    { 0, "18_confirm_restore_new", NULL },{ BTN_CROSS, NULL, NULL },
    { BTN_CROSS, NULL, NULL },
    { BTN_START, NULL, usb_unplug },
    { 0, NULL, NULL },
    { 0, "19_usb_none", NULL },          { BTN_CROSS, NULL, usb_plug },
    { BTN_CROSS, NULL, NULL },           /* card select -> menu */
    { BTN_CROSS, NULL, NULL },           /* menu -> language */
    { BTN_DOWN, NULL, NULL },            { BTN_CIRCLE, NULL, NULL },          /* Japanese */
    { 0, "20_jp_menu", NULL },           { BTN_DOWN, NULL, NULL },
    { BTN_CIRCLE, NULL, NULL },          { BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL }, { 0, NULL, NULL }, { 0, NULL, NULL },
    { 0, "21_jp_list", NULL },           { BTN_CIRCLE, NULL, NULL },
    { BTN_DOWN, NULL, NULL },
    { 0, "22_jp_action", NULL },         { BTN_CIRCLE, NULL, NULL },
    { 0, "23_jp_confirm_restore", NULL },{ BTN_CROSS, NULL, NULL },
    { BTN_UP, NULL, NULL },              { BTN_CIRCLE, NULL, NULL },
    { 0, "24_jp_confirm_backup", NULL }, { BTN_CROSS, NULL, NULL },
    { BTN_CROSS, NULL, NULL },           { BTN_START, NULL, usb_unplug },
    { 0, NULL, NULL },
    { 0, "25_jp_usb_none", NULL },
};
static step opl_script[] = {
    { 0, NULL, NULL },                   { BTN_CIRCLE, NULL, NULL },
    { BTN_CIRCLE, NULL, NULL },          /* Install patches */
    { 0, "opl_01_no_ilink", NULL },      { BTN_CROSS, NULL, NULL },
    { BTN_DOWN, NULL, NULL },            { BTN_DOWN, NULL, NULL },
    { BTN_CIRCLE, NULL, NULL },          /* Backup saves */
    { BTN_CIRCLE, NULL, NULL },          /* card */
    { 0, NULL, NULL }, { 0, NULL, NULL },
    { 0, "opl_02_usb_busy", NULL },      { BTN_CROSS, NULL, NULL },
    { BTN_CROSS, NULL, NULL },           { BTN_CROSS, NULL, NULL },
    { BTN_DOWN, NULL, NULL },            { BTN_CIRCLE, NULL, NULL },          /* Japanese */
    { BTN_DOWN, NULL, NULL },            { BTN_CIRCLE, NULL, NULL },
    { BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL }, { 0, NULL, NULL },
    { 0, "opl_03_jp_usb_busy", NULL },   { BTN_CROSS, NULL, NULL },
    { BTN_CROSS, NULL, NULL },           { BTN_UP, NULL, NULL },
    { BTN_UP, NULL, NULL },              { BTN_CIRCLE, NULL, NULL },          /* Install patches */
    { 0, "opl_04_jp_no_ilink", NULL },
};
/* card: MH2 and MH1 Europe, no MHG; the cursor skips MHG */
static step install_script[] = {
    { 0, NULL, NULL },                   { BTN_CIRCLE, NULL, NULL },          /* English */
    { BTN_CIRCLE, NULL, NULL },          /* Install patches */
    { BTN_CIRCLE, NULL, NULL },          /* card */
    { 0, NULL, NULL },
    { 0, "ins_01_games", NULL },         { BTN_DOWN, NULL, NULL },
    { 0, "ins_02_games_mh1e", NULL },    { BTN_CIRCLE, NULL, NULL },
    { 0, "ins_03_confirm_mh1e", NULL },  { BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL },
    { 0, "ins_04_done", NULL },          { BTN_CIRCLE, NULL, NULL },          /* card select */
    { BTN_CROSS, NULL, NULL },           { BTN_CROSS, NULL, NULL },           /* menu, language */
    { BTN_DOWN, NULL, NULL },            { BTN_CIRCLE, NULL, NULL },          /* Japanese */
    { BTN_CIRCLE, NULL, NULL },          { BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL },                   { BTN_DOWN, NULL, NULL },
    { 0, "ins_05_jp_games_mh1e", NULL }, { BTN_CIRCLE, NULL, NULL },
    { 0, "ins_06_jp_confirm_mh1e", NULL },{ BTN_CIRCLE, NULL, card_breaks },
    { 0, NULL, NULL },
    { 0, "ins_07_jp_fail_lost", card_heals },
};
/* same card, Remove patches on MH1 Europe */
static step remove_script[] = {
    { 0, NULL, NULL },                   { BTN_CIRCLE, NULL, NULL },          /* English */
    { BTN_DOWN, NULL, NULL },
    { 0, "rm_01_menu", NULL },           { BTN_CIRCLE, NULL, NULL },          /* Remove patches */
    { BTN_CIRCLE, NULL, NULL },          /* card */
    { 0, NULL, NULL },                   /* scan */
    { BTN_DOWN, NULL, NULL },            { BTN_CIRCLE, NULL, NULL },          /* MH1 Europe */
    { 0, "rm_02_confirm", NULL },        { BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL },
    { 0, "rm_03_done", NULL },           { BTN_CIRCLE, NULL, NULL },          /* card select */
    { BTN_CROSS, NULL, NULL },           { BTN_CROSS, NULL, NULL },           /* menu, language */
    { BTN_DOWN, NULL, NULL },            { BTN_CIRCLE, NULL, NULL },          /* Japanese */
    { 0, "rm_04_jp_menu", NULL },        { BTN_CIRCLE, NULL, NULL },
    { BTN_CIRCLE, NULL, NULL },          { 0, NULL, NULL },                   /* card, scan */
    { BTN_CIRCLE, NULL, NULL },          /* MH2 */
    { 0, "rm_05_jp_confirm", NULL },     { BTN_CIRCLE, NULL, NULL },
    { 0, NULL, NULL },
    { 0, "rm_06_jp_done", NULL },
};
static step *steps = script;
static unsigned nsteps = sizeof script / sizeof script[0];
static unsigned pos;

void pad_init(void) {}
unsigned pad_poll(void) {
    if (pos >= nsteps) exit(0);
    step *s = &steps[pos++];
    if (s->hook) s->hook();
    shot = s->shot;
    return s->key;
}

int app_main(void);

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s saves-dir usb-dir out-dir [opl|install|remove]\n", argv[0]); return 2; }
    if (argc > 4 && !strcmp(argv[4], "opl")) {
        opl_scenario = 1;
        usb_init_result = USB_INIT_BUSY;
        steps = opl_script; nsteps = sizeof opl_script / sizeof opl_script[0];
    }
    snprintf(usb_base, sizeof usb_base, "%s", argv[2]);
    out_dir = argv[3];
    if (argc > 4 && (!strcmp(argv[4], "install") || !strcmp(argv[4], "remove"))) {
        if (!strcmp(argv[4], "install")) { steps = install_script; nsteps = sizeof install_script / sizeof install_script[0]; }
        else                             { steps = remove_script;  nsteps = sizeof remove_script / sizeof remove_script[0]; }
        card_reset();
        card_load(argv[1], save_dir(SG_MH2), 1);
        card_load(argv[1], save_dir(SG_MH1E), 1);
        return app_main();
    }
    /* drive: backups of MHG and MH1 USA; card: MH2, MH1 JP, MH1 USA */
    usb_reset();
    int oc[SG_COUNT], ou[SG_COUNT];
    card_reset(); card_load(argv[1], save_dir(SG_MHG), 1);  save_scan(0, oc, ou); save_backup(0, SG_MHG);
    card_reset(); card_load(argv[1], save_dir(SG_MH1U), 1); save_scan(0, oc, ou); save_backup(0, SG_MH1U);
    card_reset();
    card_load(argv[1], save_dir(SG_MH2), 1);
    card_load(argv[1], save_dir(SG_MHGK), 1);
    card_load(argv[1], save_dir(SG_MH1J), 1);
    card_load(argv[1], save_dir(SG_MH1U), 2);
    return app_main();
}
